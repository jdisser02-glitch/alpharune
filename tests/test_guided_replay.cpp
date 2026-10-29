/// Tests for tools/guided_replay: a tiny synthetic RiftLite log replays to the
/// expected state, a card missing from the hand is pulled from the deck (or
/// created), and the name / deck inference helpers behave.

#include <gtest/gtest.h>

#include "cards/card_registry.h"
#include "core/card_db.h"
#include "core/events.h"
#include "engine/game_engine.h"
#include "rules/deck_validator.h"
#include "tools/guided_replay.h"

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace riftbound;

namespace {

std::string repoRoot() {
    if (const char* root = std::getenv("RIFTBOUND_ROOT")) return root;
    for (auto* p : {".", "..", "../.."})
        if (std::filesystem::exists(std::filesystem::path(p) / "decks")) return p;
    return ".";
}

nlohmann::json ev(int game, const char* text, nlohmann::json actor, const char* kind, nlohmann::json extra = {}) {
    nlohmann::json e = {{"game", game}, {"t", "0:00"}, {"text", text}, {"actor", std::move(actor)}, {"kind", kind}};
    for (auto it = extra.begin(); it != extra.end(); ++it) e[it.key()] = it.value();
    return e;
}

/// A mirror match of two Riven players. Alice (capture player) goes first and
/// ends her first turn; Bob plays Scuttle Crab; Alice plays Scuttle Crab; Bob
/// moves his Crab to Emperor's Dais, both pass, Bob conquers.
nlohmann::json syntheticGame() {
    nlohmann::json legend = {{"legend", "Riven, The Exile"},
                             {"cards", {{{"name", "Riven, The Exile"}, {"code", "RAD-149"}},
                                        {{"name", "Riven, Broken Blade"}, {"code", "RAD-043"}}}}};
    nlohmann::json events = nlohmann::json::array({
        ev(1, "Alice locked in a battlefield.", "Alice", "battlefield_locked"),
        ev(1, "Bob locked in a battlefield.", "Bob", "battlefield_locked"),
        ev(1, "Alice chose Alice to take the first turn.", "Alice", "first_turn", {{"first", "Alice"}}),
        ev(1, "Alice finalized mulligan (0 recycled, 0 redrawn).", "Alice", "mulligan", {{"recycled", 0}, {"redrawn", 0}}),
        ev(1, "Bob finalized mulligan (1 recycled, 1 redrawn).", "Bob", "mulligan", {{"recycled", 1}, {"redrawn", 1}}),
        ev(1, "Both mulligans are complete. Starting the game.", "both", "mulligan"),
        ev(1, "Alice ended their turn.", "Alice", "ended_turn"),
        ev(1, "Bob paid 2E — Exhaust 2.", "Bob", "paid", {{"energy", 2}}),
        ev(1, "Bob played Scuttle Crab from hand to base.", "Bob", "played", {{"card", "Scuttle Crab"}, {"from", "hand"}, {"to", "base"}}),
        ev(1, "Chain resolved: Scuttle Crab.", nullptr, "chain_resolved", {{"card", "Scuttle Crab"}}),
        ev(1, "Bob ended their turn.", "Bob", "ended_turn"),
        ev(1, "Alice paid 2E — Exhaust 2.", "Alice", "paid", {{"energy", 2}}),
        ev(1, "Alice played Scuttle Crab from hand to base.", "Alice", "played", {{"card", "Scuttle Crab"}, {"from", "hand"}, {"to", "base"}}),
        ev(1, "Alice ended their turn.", "Alice", "ended_turn"),
        ev(1, "Bob moved Scuttle Crab to Emperor's Dais.", "Bob", "moved", {{"card", "Scuttle Crab"}, {"to", "Emperor's Dais"}, {"battlefield", "Emperor's Dais"}}),
        ev(1, "Bob passed focus at Emperor's Dais.", "Bob", "passed_focus", {{"battlefield", "Emperor's Dais"}}),
        ev(1, "Alice ended showdown responses at Emperor's Dais.", "Alice", "showdown", {{"battlefield", "Emperor's Dais"}}),
        ev(1, "Bob conquered Emperor's Dais and scored 1.", "Bob", "conquered", {{"battlefield", "Emperor's Dais"}, {"amount", 1}}),
        ev(1, "Bob ended their turn.", "Bob", "ended_turn"),
    });
    return {{"players", {"Alice", "Bob"}}, {"capture_player", "Alice"}, {"opponent", "Bob"},
            {"legends", {{"Alice", legend}, {"Bob", legend}}},
            {"games", {{{"game", 1}, {"events", {0, static_cast<int>(events.size())}}, {"first_player", "Alice"},
                        {"score_from_log", {{"Alice", 0}, {"Bob", 1}}}}}},
            {"events", events}};
}

}  // namespace

class GuidedReplayTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry.loadAll();
        db.buildFromClasses(registry);
        candidates = guided::findCandidateDeckLists(repoRoot());
    }
    CardRegistry registry;
    CardDB db;
    std::vector<std::string> candidates;
};

TEST_F(GuidedReplayTest, NamesAndCodesResolve) {
    EXPECT_TRUE(guided::nameMatches("Riven, The Exile", "The Exile"));
    EXPECT_TRUE(guided::nameMatches("Emperor's Dais", "emperor's dais"));
    EXPECT_FALSE(guided::nameMatches("Scuttle Crab", "Astral Heron"));
    EXPECT_EQ(guided::normalizeName("  Targon\xE2\x80\x99s   Peak. "), "targon's peak");
    const CardDef* by_code = guided::resolveCard(db, "", "RAD-149");
    ASSERT_NE(by_code, nullptr);
    EXPECT_EQ(by_code->name, "The Exile");
    const CardDef* by_name = guided::resolveCard(db, "Riven, The Exile");
    ASSERT_NE(by_name, nullptr);
    EXPECT_EQ(by_name->card_type, CardType::Legend);
    EXPECT_EQ(guided::seedForFirstPlayer(1, PlayerId::Player1), 1u);
}

TEST_F(GuidedReplayTest, InfersALegalDeckFromTheLog) {
    auto game = guided::humanGameFromJson(syntheticGame(), 1, "synthetic.json");
    ASSERT_EQ(game.events.size(), 19u);
    EXPECT_EQ(game.first_player, "Alice");
    auto d = guided::inferDeck(game, "Bob", db, candidates);
    EXPECT_EQ(d.deck.main_deck.size(), 39u);
    EXPECT_EQ(d.deck.rune_deck.size(), 12u);
    EXPECT_EQ(d.deck.battlefields.size(), 3u);
    EXPECT_EQ(db.get(d.deck.legend).name, "The Exile");
    EXPECT_EQ(db.get(d.deck.chosen_champion).name, "Riven, Broken Blade");
    int crabs = 0;
    for (auto id : d.deck.main_deck) crabs += db.get(id).name == "Scuttle Crab";
    EXPECT_GE(crabs, 1);
    EXPECT_TRUE(d.notes["legal"].get<bool>()) << d.notes.dump();
    EXPECT_TRUE(d.notes["filler"]["same_legend"].get<bool>());
    EXPECT_NE(std::find(d.battlefield_names.begin(), d.battlefield_names.end(), "Emperor's Dais"),
              d.battlefield_names.end());
}

TEST_F(GuidedReplayTest, SyntheticLogReplaysToTheExpectedState) {
    auto game = guided::humanGameFromJson(syntheticGame(), 1, "synthetic.json");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    guided::GuidedOptions opts;
    opts.seed = 3;
    auto res = guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
    const auto& s = res.summary;
    EXPECT_EQ(s["desyncs"].get<int>(), 0) << s["desync_list"].dump(1);
    EXPECT_GE(s["decisions_human_known"].get<int>(), 8);
    // Bob conquered Emperor's Dais once: the engine's score agrees with the log.
    EXPECT_EQ(s["result"]["engine_scores"][1].get<int>(), 1) << s.dump(1);
    EXPECT_EQ(s["result"]["engine_scores"][0].get<int>(), 0);
    EXPECT_EQ(s["stop_reason"].get<std::string>(), "log ended");
    EXPECT_EQ(s["log_events_consumed"].get<int>(), 19);
    EXPECT_GE(res.header["turns"].get<int>(), 4);
}

TEST_F(GuidedReplayTest, MissingCardIsPulledFromTheDeckOrCreated) {
    auto deck = DeckValidator::loadFromDeckList(repoRoot() + "/decks/riven_sacramento.txt", db);
    EventBus events;
    GameEngine engine(db, events, registry);
    auto sr = engine.beginGame(deck, deck, 11);
    ASSERT_EQ(sr.kind, StepKind::NeedDecision);
    GameState& st = engine.mutableState();
    auto& ps = st.player(PlayerId::Player1);
    ASSERT_FALSE(ps.main_deck.empty());

    // A card that is in the deck and not in the (still empty) hand.
    const GameObjectId some = ps.main_deck.front();
    const CardDefId def = st.getObject(some).card_def_id;
    const size_t deck_before = ps.main_deck.size(), hand_before = ps.hand.size();
    EXPECT_EQ(guided::ensureCardIn(st, db, PlayerId::Player1, def, ZoneType::Hand, {}, {}), "pulled");
    EXPECT_EQ(ps.main_deck.size(), deck_before - 1);
    EXPECT_EQ(ps.hand.size(), hand_before + 1);
    bool in_hand = false;
    for (auto id : ps.hand) in_hand |= st.getObject(id).card_def_id == def;
    EXPECT_TRUE(in_hand);
    EXPECT_EQ(st.getObject(ps.hand.back()).zone, ZoneType::Hand);
    EXPECT_EQ(guided::ensureCardIn(st, db, PlayerId::Player1, def, ZoneType::Hand, {}, {}), "already");

    // A card the deck does not hold at all is conjured.
    const CardDef* foreign = db.findByName("Stupefy");
    ASSERT_NE(foreign, nullptr);
    for (auto id : ps.main_deck) ASSERT_NE(st.getObject(id).card_def_id, foreign->id);
    EXPECT_EQ(guided::ensureCardIn(st, db, PlayerId::Player1, foreign->id, ZoneType::Hand, {}, {}), "created");
    EXPECT_EQ(st.getObject(ps.hand.back()).name, "Stupefy");
    EXPECT_EQ(st.getObject(ps.hand.back()).owner, PlayerId::Player1);
    EXPECT_EQ(ps.main_deck.size(), deck_before - 1);
}
