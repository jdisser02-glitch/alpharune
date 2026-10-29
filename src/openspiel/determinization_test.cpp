// Determinization test for RiftboundState::ResampleFromInfostate.
//
// For each game, walk N random decisions, then from the acting player's
// point of view sample worlds and check that:
//   1. the observation (ISMCTS infostate key) is unchanged;
//   2. the observer's hand, every public zone and every zone size are unchanged;
//   3. the opponent's hand is re-dealt (differs in some sample) from the pool
//      of the opponent's hand + deck + facedown cards (same multiset);
//   4. a sampled world can be played to the end (also through the replay
//      path, by advancing a clone of it).
//
// Returns 0 on success, 1 on any failure.
// Run: RIFTBOUND_DECK1=decks/riven_sacramento.txt ./build/src/openspiel/riftbound_determinization_test

#include "open_spiel/spiel.h"
#include "openspiel/riftbound_state.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

std::string getenvOr(const char* name, std::string fallback) {
    if (const char* v = std::getenv(name)) return v;
    return fallback;
}

using riftbound::GameState;
using riftbound::PlayerId;

std::vector<riftbound::CardDefId> defs(const GameState& gs, const std::vector<riftbound::GameObjectId>& ids) {
    std::vector<riftbound::CardDefId> out;
    for (auto id : ids) out.push_back(gs.getObject(id).card_def_id);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<riftbound::CardDefId> hiddenPool(const GameState& gs, PlayerId p) {
    std::vector<riftbound::GameObjectId> ids(gs.player(p).hand.begin(), gs.player(p).hand.end());
    ids.insert(ids.end(), gs.player(p).main_deck.begin(), gs.player(p).main_deck.end());
    for (const auto& bf : gs.battlefields)
        for (auto f : bf.facedown) if (gs.getObject(f).controller == p) ids.push_back(f);
    return defs(gs, ids);
}

}  // namespace

int main() {
    const std::string deck1 = getenvOr("RIFTBOUND_DECK1", "decks/riven_sacramento.txt");
    const std::string deck2 = getenvOr("RIFTBOUND_DECK2", "decks/riven_build_b.txt");
    auto game = ::open_spiel::LoadGame("riftbound(deck1=" + deck1 + ",deck2=" + deck2 +
                                       ",registry=cards/registry.json,seed=0)");
    int failures = 0, checks = 0;
    const int games = std::atoi(getenvOr("RIFTBOUND_DET_GAMES", "6").c_str());
    for (int g = 0; g < games; ++g) {
        std::mt19937_64 rng(1000 + g);
        auto state = game->NewInitialState();
        const int walk = 40 + (25 * g) % 300;
        for (int i = 0; i < walk && !state->IsTerminal(); ++i) {
            auto legal = state->LegalActions();
            state->ApplyAction(legal[rng() % legal.size()]);
        }
        if (state->IsTerminal()) continue;
        const int player = state->CurrentPlayer();
        const auto* rs = static_cast<const riftbound::openspiel::RiftboundState*>(state.get());
        const GameState& real = rs->engineState();
        const PlayerId me = player == 0 ? PlayerId::Player1 : PlayerId::Player2;
        const PlayerId opp = riftbound::opponent(me);
        const auto key = state->ObservationString(player);
        bool opp_hand_changed = false;
        std::uniform_real_distribution<double> u(0.0, 1.0);
        for (int k = 0; k < 8; ++k) {
            auto world = state->ResampleFromInfostate(player, [&] { return u(rng); });
            const auto* ws = static_cast<const riftbound::openspiel::RiftboundState*>(world.get());
            const GameState& w = ws->engineState();
            ++checks;
            auto fail = [&](const std::string& what) {
                std::cerr << "game " << g << " sample " << k << ": " << what << "\n";
                ++failures;
            };
            if (world->ObservationString(player) != key) {
                fail("observation changed");
                std::vector<float> a(game->ObservationTensorSize()), b(a.size());
                state->ObservationTensor(player, absl::MakeSpan(a));
                world->ObservationTensor(player, absl::MakeSpan(b));
                for (size_t q = 0; q < a.size(); ++q)
                    if (a[q] != b[q]) std::cerr << "  feature " << q << ": " << a[q] << " -> " << b[q] << "\n";
            }
            if (defs(w, w.player(me).hand) != defs(real, real.player(me).hand)) fail("own hand changed");
            if (w.player(opp).hand.size() != real.player(opp).hand.size()) fail("opponent hand size changed");
            if (hiddenPool(w, opp) != hiddenPool(real, opp)) fail("opponent hidden pool changed");
            if (defs(w, w.player(me).main_deck) != defs(real, real.player(me).main_deck))
                fail("own deck composition changed");
            for (const auto& [id, o] : real.objects) {
                if (!o.location.has_value() || o.is_hidden) continue;   // board objects are public
                if (w.getObject(id).card_def_id != o.card_def_id) { fail("public object changed"); break; }
            }
            if (defs(w, w.player(opp).hand) != defs(real, real.player(opp).hand)) opp_hand_changed = true;
            // A clone of the world that goes live (resume or replay) must stay
            // in the sampled world, not fall back to the real one.
            {
                auto step = world->Clone();
                step->ApplyAction(step->LegalActions().front());
                const auto& sw = static_cast<const riftbound::openspiel::RiftboundState*>(step.get())->engineState();
                const auto now = defs(sw, sw.player(opp).hand);
                if (now != defs(w, w.player(opp).hand) && now == defs(real, real.player(opp).hand) &&
                    defs(w, w.player(opp).hand) != defs(real, real.player(opp).hand))
                    fail("live clone reverted to the real opponent hand");
            }
            // Play the sampled world out, through a clone (exercises replay).
            auto play = world->Clone();
            for (int s = 0; s < 3000 && !play->IsTerminal(); ++s) {
                auto legal = play->LegalActions();
                if (legal.empty()) { fail("no legal actions in a sampled world"); break; }
                play->ApplyAction(legal[rng() % legal.size()]);
            }
        }
        if (!real.player(opp).hand.empty() && !opp_hand_changed) {
            std::cerr << "game " << g << ": opponent hand never re-dealt in 8 samples\n";
            ++failures;
        }
        std::cout << "game " << g << " (decision " << walk << ", player " << player
                  << "): 8 worlds checked\n";
    }
    std::cout << checks << " sampled worlds, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
