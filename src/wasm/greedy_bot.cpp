#include "wasm/greedy_bot.h"

#include "core/events.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>

namespace riftbound::wasm {

namespace {

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

/// A clone of the real game: its own EventBus (no subscribers) and engine.
struct Clone {
    EventBus bus;
    GameEngine engine;
    Clone(const CardDB& db, const CardRegistry& reg) : engine(db, bus, reg) {}
};

}  // namespace

// ─── Config ─────────────────────────────────────────────────────────────────

GreedyBotConfig GreedyBotConfig::parse(const std::string& spec) {
    GreedyBotConfig c;
    std::string head = spec, args;
    auto colon = spec.find(':');
    if (colon != std::string::npos) {
        head = spec.substr(0, colon);
        args = spec.substr(colon + 1);
    }
    if (head == "random") c.random = true;
    size_t pos = 0;
    while (pos < args.size()) {
        auto comma = args.find(',', pos);
        if (comma == std::string::npos) comma = args.size();
        std::string kv = args.substr(pos, comma - pos);
        pos = comma + 1;
        auto eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string key = kv.substr(0, eq), val = kv.substr(eq + 1);
        if (key == "det") c.determinizations = std::max(1, std::stoi(val));
        else if (key == "budget" || key == "budget_ms") c.budget_ms = std::stod(val);
        else if (key == "steps") c.rollout_steps = std::max(0, std::stoi(val));
        else if (key == "wide") c.wide_cutoff = std::max(1, std::stoi(val));
    }
    return c;
}

// ─── Bot ────────────────────────────────────────────────────────────────────

GreedyBot::GreedyBot(const CardDB& db, const CardRegistry& registry,
                     const DeckSubmission& deck1, const DeckSubmission& deck2,
                     uint64_t game_seed, GreedyBotConfig config, uint64_t bot_seed)
    : db_(db), registry_(registry), deck1_(deck1), deck2_(deck2),
      game_seed_(game_seed), config_(config), rng_(bot_seed) {}

bool GreedyBot::inFlight(const GameState& s) {
    if (s.chain.exists() || s.chain.resuming.has_value()) return true;
    if (s.cost_cursor.has_value()) return true;
    if (s.turn.oc_state != OpenClosedState::Open) return true;
    if (s.turn.ns_state != NeutralShowdownState::Neutral) return true;
    for (const auto& bf : s.battlefields)
        if (bf.combat_in_progress || bf.showdown_in_progress ||
            bf.combat_staged || bf.showdown_staged)
            return true;
    return false;
}

bool GreedyBot::cleanBoundary(const GameState& s) {
    // Mirrors RiftboundState::ensureLive: resumeFromSnapshot is only safe
    // at an open, neutral main-phase decision with nothing in flight.
    if (s.game_over) return false;
    if (s.turn.phase == TurnPhase::Setup || s.turn.phase == TurnPhase::Mulligan) return false;
    return !inFlight(s);
}

double GreedyBot::evaluate(const GameState& s, PlayerId bot) const {
    const PlayerId opp = opponent(bot);
    if (s.game_over) {
        if (s.winner == bot) return 1.0;
        if (s.winner == opp) return 0.0;
        return 0.5;
    }
    if (net_) return net_->winProbability(s, bot, db_);
    // Fallback heuristic: point lead, on-board might, cards in hand,
    // battlefields held. Squashed into (0,1) so it composes with terminal
    // values.
    double v = 0.0;
    v += 0.6 * (s.player(bot).score - s.player(opp).score);
    int might[2] = {0, 0};
    for (const auto& [id, o] : s.objects) {
        if (!o.isUnit()) continue;
        if (o.zone != ZoneType::Base && o.zone != ZoneType::BattlefieldZone) continue;
        might[o.controller == bot ? 0 : 1] += std::max(0, o.current_might);
    }
    v += 0.08 * (might[0] - might[1]);
    v += 0.05 * (static_cast<int>(s.player(bot).hand.size()) -
                 static_cast<int>(s.player(opp).hand.size()));
    for (const auto& bf : s.battlefields)
        if (bf.controller) v += (*bf.controller == bot) ? 0.3 : -0.3;
    return 1.0 / (1.0 + std::exp(-v));
}

GreedyBot::Overrides GreedyBot::determinize(const GameState& gs, PlayerId bot) {
    // Same re-deal as RiftboundState::ResampleFromInfostate, with the bot as
    // the observer: the opponent's hand + Main Deck (+ facedown cards) share
    // one pool, and the deck orders the bot cannot know are shuffled.
    const PlayerId opp = opponent(bot);
    auto shuffle = [&](std::vector<CardDefId>& v) {
        for (size_t i = v.size(); i > 1; --i) {
            size_t k = static_cast<size_t>(rng_() % i);
            std::swap(v[i - 1], v[k]);
        }
    };
    std::set<GameObjectId> pinned;
    auto pinItem = [&](const ChainItem& it) {
        pinned.insert(it.source);
        pinned.insert(it.targets.begin(), it.targets.end());
        pinned.insert(it.chosen_objects.begin(), it.chosen_objects.end());
    };
    for (const auto& it : gs.chain.items) pinItem(it);
    if (gs.chain.resuming.has_value()) pinItem(*gs.chain.resuming);
    auto free = [&](GameObjectId oid) {
        return gs.objectExists(oid) && !pinned.count(oid) &&
               gs.getObject(oid).card_def_id != kInvalidId;
    };
    Overrides out;
    auto deal = [&](const std::vector<GameObjectId>& slots) {
        std::vector<GameObjectId> live;
        std::vector<CardDefId> ids;
        for (auto oid : slots) {
            if (!free(oid)) continue;
            live.push_back(oid);
            ids.push_back(gs.getObject(oid).card_def_id);
        }
        shuffle(ids);
        for (size_t i = 0; i < live.size(); ++i) out.emplace_back(live[i], ids[i]);
    };
    {
        const auto& ps = gs.player(opp);
        std::vector<GameObjectId> facedown;
        for (const auto& bf : gs.battlefields)
            for (auto oid : bf.facedown)
                if (free(oid) && gs.getObject(oid).controller == opp) facedown.push_back(oid);
        std::vector<GameObjectId> slots;
        for (auto oid : ps.hand) if (free(oid)) slots.push_back(oid);
        for (auto oid : ps.main_deck) if (free(oid)) slots.push_back(oid);
        std::vector<CardDefId> pool;
        for (auto oid : slots) pool.push_back(gs.getObject(oid).card_def_id);
        for (auto oid : facedown) pool.push_back(gs.getObject(oid).card_def_id);
        shuffle(pool);
        for (auto oid : facedown) {
            bool placed = false;
            for (size_t i = 0; i < pool.size(); ++i) {
                if (db_.get(pool[i]).keywords.has(Keyword::Hidden)) {
                    out.emplace_back(oid, pool[i]);
                    pool.erase(pool.begin() + static_cast<long>(i));
                    placed = true;
                    break;
                }
            }
            if (!placed) {   // no hideable card left: keep it
                auto own = gs.getObject(oid).card_def_id;
                auto it = std::find(pool.begin(), pool.end(), own);
                if (it != pool.end()) pool.erase(it);
            }
        }
        size_t k = 0;
        for (auto oid : slots)
            if (k < pool.size()) out.emplace_back(oid, pool[k++]);
    }
    deal(gs.player(bot).main_deck);
    deal(gs.player(bot).rune_deck);
    deal(gs.player(opp).rune_deck);
    return out;
}

void GreedyBot::applyOverrides(GameState& gs, const Overrides& ov) const {
    for (const auto& [oid, def_id] : ov) {
        if (!gs.objectExists(oid) || def_id == kInvalidId) continue;
        auto& obj = gs.getObject(oid);
        if (obj.card_def_id == def_id) continue;
        const auto& def = db_.get(def_id);
        obj.card_def_id   = def_id;
        obj.name          = def.name;
        obj.card_type     = def.card_type;
        obj.super_type    = def.super_type;
        obj.tags          = def.tags;
        obj.domains       = def.domains;
        obj.base_might    = def.might;
        obj.current_might = def.might;
        obj.keywords      = def.keywords;
        obj.assault_value = def.assault_value;
        obj.shield_value  = def.shield_value;
        obj.deflect_value = def.deflect_value;
        obj.might_bonus   = def.might_bonus;
    }
}

int GreedyBot::choose(const GameEngine& engine, PlayerId bot, const std::vector<int>& history) {
    last_search_.clear();
    last_ms_ = 0;
    last_worlds_ = 0;
    last_replayed_ = false;

    const StepResult step = engine.currentStep();
    if (step.kind != StepKind::NeedDecision || step.legal.empty()) return 0;
    const int n = static_cast<int>(step.legal.size());
    const GameState& real = engine.state();

    if (config_.random) return static_cast<int>(rng_() % static_cast<uint64_t>(n));
    if (n == 1) {
        last_search_ = {{0, 1.0}};
        return 0;
    }
    // Rune payment steps: option 0, like IsMctsAgent (the cost cursor is
    // engine-internal state the snapshot/replay paths cannot reproduce).
    if (real.cost_cursor.has_value()) return 0;

    const double t0 = nowMs();
    const bool resumable = cleanBoundary(real);
    last_replayed_ = !resumable;
    int worlds = config_.determinizations;
    if (n > config_.wide_cutoff) worlds = 1;

    std::vector<double> sum(static_cast<size_t>(n), 0.0);
    std::vector<int> cnt(static_cast<size_t>(n), 0);

    for (int w = 0; w < worlds; ++w) {
        if (w > 0 && nowMs() - t0 > config_.budget_ms) break;
        const Overrides ov = determinize(real, bot);
        const uint64_t world_seed = 1 + (rng_() >> 1);
        for (int i = 0; i < n; ++i) {
            try {
                Clone c(db_, registry_);
                if (resumable) {
                    GameState snap = real;
                    applyOverrides(snap, ov);
                    c.engine.resumeFromSnapshot(std::move(snap), world_seed);
                } else {
                    c.engine.beginGame(deck1_, deck2_, game_seed_);
                    for (int idx : history) {
                        if (c.engine.isStepDone()) break;
                        c.engine.applyChoice(idx);
                    }
                    if (c.engine.isStepDone() ||
                        c.engine.currentStep().legal.size() != step.legal.size()) {
                        // Replay diverged from the live game; nothing to learn.
                        continue;
                    }
                    applyOverrides(c.engine.mutableState(), ov);
                    c.engine.reseedRng(world_seed);
                }
                if (c.engine.isStepDone()) continue;
                c.engine.applyChoice(i);
                // Settle: answer intermediate decisions (either player's)
                // with option 0 while something is still in flight.
                for (int k = 0; k < config_.rollout_steps && !c.engine.isStepDone() &&
                                inFlight(c.engine.state()); ++k)
                    c.engine.applyChoice(0);
                sum[static_cast<size_t>(i)] += evaluate(c.engine.state(), bot);
                cnt[static_cast<size_t>(i)] += 1;
            } catch (const std::exception&) {
                // A clone that fails is simply not counted.
            }
        }
        ++last_worlds_;
    }

    int best = 0;
    double best_v = -1.0;
    for (int i = 0; i < n; ++i) {
        if (cnt[static_cast<size_t>(i)] == 0) continue;
        const double v = sum[static_cast<size_t>(i)] / cnt[static_cast<size_t>(i)];
        last_search_.emplace_back(i, v);
        if (v > best_v) { best_v = v; best = i; }
    }
    last_ms_ = nowMs() - t0;
    return best;
}

}  // namespace riftbound::wasm
