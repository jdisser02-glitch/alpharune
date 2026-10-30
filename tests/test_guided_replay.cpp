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
#include <fstream>
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
/// moves his Crab to Emperor's Dais, both pass, Bob conquers. With
/// `rewind_after_draw`, Bob "rewinds" right after the Crab's draw resolved —
/// the client only undoes the last action, so the Crab (whose draw was
/// already public) stays on the board, as it does in recorded games.
nlohmann::json syntheticGame(bool rewind_after_draw = false) {
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
    if (rewind_after_draw) {
        // after "Chain resolved: Scuttle Crab." (index 9): the draw, then the rewind
        events.insert(events.begin() + 9, ev(1, "Bob drew 1 card.", "Bob", "drew", {{"amount", 1}}));
        events.insert(events.begin() + 11, ev(1, "Bob rewound to before their last action.", "Bob", "rewound"));
    }
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

TEST_F(GuidedReplayTest, ActorLessSpellLinesFollowTheirPayment) {
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    // Bob pays, an actor-less "Played Defy" follows: Bob's spell, whatever the importer wrote.
    events.push_back(ev(1, "Bob paid 1E / 1P — Exhaust 1 · Recycle 1 Calm rune.", "Bob", "paid"));
    events.push_back(ev(1, "Played Defy from hand.", "Alice", "played",
                        {{"card", "Defy"}, {"from", "hand"}, {"actor_inferred", true}}));
    // Alice exhausts a rune and a unit (Kai'Sa-style free spell), then her spell.
    events.push_back(ev(1, "Alice exhausted 1 Mind rune.", "Alice", "exhausted"));
    events.push_back(ev(1, "Alice exhausted Scuttle Crab.", "Alice", "exhausted", {{"card", "Scuttle Crab"}}));
    events.push_back(ev(1, "Played Stupefy from hand.", "Alice", "played",
                        {{"card", "Stupefy"}, {"from", "hand"}, {"actor_inferred", true}}));
    // Bob hid a card at Emperor's Dais earlier: a card played from there is his.
    events.push_back(ev(1, "Bob put a hidden card to Emperor's Dais.", "Bob", "hidden_card_placed", {{"to", "Emperor's Dais"}}));
    events.push_back(ev(1, "Played Switcheroo from Emperor's Dais.", "Alice", "played",
                        {{"card", "Switcheroo"}, {"from", "Emperor's Dais"}, {"actor_inferred", true}}));
    // Nothing says who played this one.
    events.push_back(ev(1, "Chain resolved: Switcheroo.", nullptr, "chain_resolved"));
    events.push_back(ev(1, "Played Rebuke from trash.", "Alice", "played",
                        {{"card", "Rebuke"}, {"from", "trash"}, {"actor_inferred", true}}));
    // An explicit line of Bob's right after Alice's payment is left alone.
    events.push_back(ev(1, "Alice paid 2E — Exhaust 2.", "Alice", "paid"));
    events.push_back(ev(1, "Bob played Tideturner from hand to base.", "Bob", "played",
                        {{"card", "Tideturner"}, {"from", "hand"}, {"to", "base"}}));
    // The capture logged the play before its payment: Bob's runes, Bob's Gold
    // token, then the spell's effect — Bob's spell.
    events.push_back(ev(1, "Played Cull the Weak from hand.", "Alice", "played",
                        {{"card", "Cull the Weak"}, {"from", "hand"}, {"actor_inferred", true}}));
    events.push_back(ev(1, "Bob exhausted 1 Chaos rune.", "Bob", "exhausted", {{"amount", 1}, {"rune", "Chaos"}}));
    events.push_back(ev(1, "Bob exhausted 1 Calm rune.", "Bob", "exhausted", {{"amount", 1}, {"rune", "Calm"}}));
    events.push_back(ev(1, "Bob used a Gold token.", "Bob", "token_used"));
    events.push_back(ev(1, "Alice moved Scuttle Crab from base to trash.", "Alice", "killed",
                        {{"card", "Scuttle Crab"}, {"from", "base"}, {"to", "trash"}}));
    // Payment lines that lead into another play belong to that play, not to
    // the actor-less spell before them.
    events.push_back(ev(1, "Played Gust from hand.", "Alice", "played",
                        {{"card", "Gust"}, {"from", "hand"}, {"actor_inferred", true}}));
    events.push_back(ev(1, "Bob exhausted 1 Chaos rune.", "Bob", "exhausted", {{"amount", 1}, {"rune", "Chaos"}}));
    events.push_back(ev(1, "Bob played Astral Heron from hand to base.", "Bob", "played",
                        {{"card", "Astral Heron"}, {"from", "hand"}, {"to", "base"}}));
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};

    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    auto line = [&](const char* card) -> const guided::LogLine& {
        for (const auto& e : game.events) if (e.kind == "played" && e.card == card) return e;
        throw std::runtime_error(card);
    };
    EXPECT_EQ(line("Defy").actor, "Bob");
    EXPECT_FALSE(line("Defy").actor_uncertain);
    EXPECT_EQ(line("Stupefy").actor, "Alice");
    EXPECT_FALSE(line("Stupefy").actor_uncertain);
    EXPECT_EQ(line("Switcheroo").actor, "Bob");
    EXPECT_EQ(line("Rebuke").actor, "Alice");
    EXPECT_TRUE(line("Rebuke").actor_uncertain);
    EXPECT_EQ(line("Tideturner").actor, "Bob");
    EXPECT_FALSE(line("Tideturner").actor_inferred);
    EXPECT_EQ(line("Cull the Weak").actor, "Bob");
    EXPECT_FALSE(line("Cull the Weak").actor_uncertain);
    EXPECT_EQ(line("Gust").actor, "Alice");
    EXPECT_TRUE(line("Gust").actor_uncertain);
}

TEST_F(GuidedReplayTest, CaptureArtefactsAreDroppedOrFlagged) {
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    // The site repeated the move line (same second, flagged by the importer).
    nlohmann::json dup_move = events[14];
    dup_move["repeat_of_previous"] = true;
    events.insert(events.begin() + 15, dup_move);
    // A "to banished" move is an effect (a card banished from the trash), not a move.
    events.insert(events.begin() + 19, ev(1, "Bob moved Scuttle Crab to banished.", "Bob", "moved",
                                          {{"card", "Scuttle Crab"}, {"to", "banished"}}));
    // "played 3 Bomb tokens" is an effect's tokens, like the singular line the importer flags.
    events.insert(events.begin() + 20, ev(1, "Bob played 3 Bomb tokens to Emperor's Dais.", "Bob", "played",
                                          {{"card", "Bomb"}, {"amount", 3}, {"to", "Emperor's Dais"}, {"from", "hand"}}));
    // "Bob ended their turn." written twice at the same second, around Alice's scoring line.
    auto& first_end = events.back();
    first_end["seconds"] = 183;
    events.push_back(ev(1, "Alice scored 1 at turn start. Score: 0 → 1.", "Alice", "scored", {{"amount", 1}, {"score_to", 1}}));
    events.push_back(ev(1, "Bob ended their turn.", "Bob", "ended_turn", {{"seconds", 183}}));
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};

    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    int moves = 0, ends_by_bob = 0;
    bool bombs_are_tokens = false;
    for (const auto& e : game.events) {
        if (e.kind == "moved" && e.to == "Emperor's Dais") ++moves;
        if (e.kind == "ended_turn" && e.actor == "Bob") ++ends_by_bob;
        if (e.kind == "played" && e.card == "Bomb") bombs_are_tokens = e.token;
    }
    EXPECT_EQ(moves, 1) << "the repeated move line is dropped";
    EXPECT_EQ(ends_by_bob, 2) << "the repeated end-of-turn line is dropped";
    EXPECT_TRUE(bombs_are_tokens);

    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    for (auto id : d2.deck.main_deck) EXPECT_NE(db.get(id).name, "Bomb") << "token lines do not feed the deck";
    guided::GuidedOptions opts;
    opts.seed = 3;
    auto res = guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
    const auto& s = res.summary;
    EXPECT_EQ(s["desyncs"].get<int>(), 0) << s["desync_list"].dump(1);
    EXPECT_EQ(s["result"]["engine_scores"][1].get<int>(), 1) << s.dump(1);
}

TEST_F(GuidedReplayTest, ClientUndoLinesAreDropped) {
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    // Bob moves his Crab to Emperor's Dais, the client takes it back (the
    // Crab "moves" to base and is readied), then he moves it for real.
    events.insert(events.begin() + 14, ev(1, "Bob moved Scuttle Crab to Emperor's Dais.", "Bob", "moved",
                                          {{"card", "Scuttle Crab"}, {"to", "Emperor's Dais"}}));
    events.insert(events.begin() + 15, ev(1, "Bob moved Scuttle Crab to base.", "Bob", "moved",
                                          {{"card", "Scuttle Crab"}, {"to", "base"}}));
    events.insert(events.begin() + 16, ev(1, "Bob readied Scuttle Crab.", "Bob", "readied", {{"card", "Scuttle Crab"}}));
    // Alice plays her champion and puts it back into the Champion Zone: the
    // play and its payment are undone (her runes are readied again).
    events.insert(events.begin() + 11, ev(1, "Alice paid 4E — Exhaust 4.", "Alice", "paid"));
    events.insert(events.begin() + 12, ev(1, "Alice moved Riven, Broken Blade to base.", "Alice", "moved",
                                          {{"card", "Riven, Broken Blade"}, {"to", "base"}}));
    events.insert(events.begin() + 13, ev(1, "Alice moved Riven, Broken Blade to champion area.", "Alice", "moved",
                                          {{"card", "Riven, Broken Blade"}, {"to", "champion area"}}));
    events.insert(events.begin() + 14, ev(1, "Alice readied 1 Calm rune.", "Alice", "readied"));
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};

    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    EXPECT_EQ(game.events.size(), 19u) << "both undone actions leave no line";
    for (const auto& e : game.events) EXPECT_NE(e.card, "Riven, Broken Blade");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    guided::GuidedOptions opts;
    opts.seed = 3;
    auto res = guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
    EXPECT_EQ(res.summary["desyncs"].get<int>(), 0) << res.summary["desync_list"].dump(1);
}

TEST_F(GuidedReplayTest, TheOtherPlayersUnitMovingIsAnEffect) {
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    // During Bob's turn, after his conquest, Alice's Crab "moves": only the
    // turn player makes Standard Moves (CR 144.1.a), so it is the effect of
    // something Bob did (Charm, Blitzcrank), not a decision of Alice's.
    events.insert(events.begin() + 18, ev(1, "Alice moved Scuttle Crab to Emperor's Dais.", "Alice", "moved",
                                          {{"card", "Scuttle Crab"}, {"to", "Emperor's Dais"}}));
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};

    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    guided::GuidedOptions opts;
    opts.seed = 3;
    auto res = guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
    EXPECT_EQ(res.summary["desyncs"].get<int>(), 0) << res.summary["desync_list"].dump(1);
}

TEST_F(GuidedReplayTest, APaymentNamingAnAddSourceActivatesIt) {
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    events.insert(events.begin() + 11, ev(1, "Alice paid 2E / 2P — Exhaust 2 · 2\xC3\x97 Seal of Rage · Recycle 1 Fury rune.",
                                          "Alice", "paid"));
    // The same payment line repeated by the capture is one payment.
    events.insert(events.begin() + 12, ev(1, "Alice paid 2E / 2P — Exhaust 2 · 2\xC3\x97 Seal of Rage · Recycle 1 Fury rune.",
                                          "Alice", "paid"));
    events.insert(events.begin() + 13, ev(1, "Bob paid 1E / 1P — Exhaust 1 · Kai'Sa, Daughter of the Void.", "Bob", "paid"));
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};

    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    std::vector<std::string> activations;
    for (const auto& e : game.events)
        if (e.kind == "exhausted") activations.push_back(e.actor + ": " + e.card);
    ASSERT_EQ(activations.size(), 3u);
    EXPECT_EQ(activations[0], "Alice: Seal of Rage");
    EXPECT_EQ(activations[1], "Alice: Seal of Rage");
    EXPECT_EQ(activations[2], "Bob: Kai'Sa, Daughter of the Void");
}

TEST_F(GuidedReplayTest, AnActorLessPlayGoesToThePlayerWhoseDomainsHoldIt) {
    nlohmann::json file = syntheticGame();
    // Bob is now a Kai'Sa player (Fury / Mind); Riven is Calm / Chaos.
    file["legends"]["Bob"] = {{"legend", "Kai'Sa, Daughter of the Void"},
                              {"cards", {{{"name", "Kai'Sa, Daughter of the Void"}, {"code", "OGN-299"}},
                                         {{"name", "Kai'Sa, Survivor"}, {"code", "OGN-039"}}}}};
    auto& events = file["events"];
    // Alice's rune line comes right before Hextech Ray (Fury): the payment
    // rule gives the spell to Alice, whose Riven deck cannot hold it.
    events.insert(events.begin() + 11, ev(1, "Alice exhausted 1 Calm rune.", "Alice", "exhausted"));
    events.insert(events.begin() + 12, ev(1, "Played Hextech Ray from hand.", "Alice", "played",
                                          {{"card", "Hextech Ray"}, {"from", "hand"}, {"actor_inferred", true}}));
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};

    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    EXPECT_EQ(guided::settleActorsByDomain(game, db), 1);
    for (const auto& e : game.events)
        if (e.card == "Hextech Ray") EXPECT_EQ(e.actor, "Bob");
}

TEST_F(GuidedReplayTest, AccelerateFollowsTheReadiedLine) {
    // Bob plays Reckless Innovator ([Accelerate]) and the capture shows the
    // [1][C]: a rune exhausted, a rune recycled, "Bob readied Reckless
    // Innovator" — so the unit can move to Emperor's Dais this very turn.
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    events.erase(events.begin() + 14, events.end());
    for (auto e : {
             ev(1, "Bob paid 3E — Exhaust 3.", "Bob", "paid", {{"energy", 3}}),
             ev(1, "Bob played Reckless Innovator from hand to base.", "Bob", "played",
                {{"card", "Reckless Innovator"}, {"from", "hand"}, {"to", "base"}}),
             ev(1, "Bob exhausted 1 Chaos rune.", "Bob", "exhausted", {{"amount", 1}, {"rune", "Chaos"}}),
             ev(1, "Bob recycled 1 Chaos rune.", "Bob", "recycled", {{"amount", 1}, {"rune", "Chaos"}}),
             ev(1, "Bob readied Reckless Innovator.", "Bob", "readied", {{"card", "Reckless Innovator"}}),
             ev(1, "Bob moved Reckless Innovator to Emperor's Dais.", "Bob", "moved",
                {{"card", "Reckless Innovator"}, {"to", "Emperor's Dais"}, {"battlefield", "Emperor's Dais"}}),
             ev(1, "Bob passed focus at Emperor's Dais.", "Bob", "passed_focus", {{"battlefield", "Emperor's Dais"}}),
             ev(1, "Alice ended showdown responses at Emperor's Dais.", "Alice", "showdown", {{"battlefield", "Emperor's Dais"}}),
             ev(1, "Bob conquered Emperor's Dais and scored 1.", "Bob", "conquered", {{"battlefield", "Emperor's Dais"}, {"amount", 1}}),
             ev(1, "Bob ended their turn.", "Bob", "ended_turn"),
         })
        events.push_back(e);
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};

    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    const auto out = std::filesystem::temp_directory_path() / "riftbound_guided_accelerate_test";
    std::filesystem::remove_all(out);
    guided::GuidedOptions opts;
    opts.seed = 3;
    opts.out_dir = out.string();
    auto res = guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
    const auto& s = res.summary;
    EXPECT_EQ(s["desyncs"].get<int>(), 0) << s["desync_list"].dump(1);
    EXPECT_EQ(s["result"]["engine_scores"][1].get<int>(), 1) << s.dump(1);
    EXPECT_EQ(s["readiness_fixes"].get<int>(), 0) << "the unit entered ready by itself, no god-mode ready";

    // The Accelerate yes/no was answered "yes" from the log, not by default.
    std::ifstream in(out / "game_1.json");
    ASSERT_TRUE(in.good());
    nlohmann::json replay;
    in >> replay;
    bool seen = false;
    for (const auto& f : replay["frames"]) {
        if (!f.contains("options")) continue;
        for (size_t i = 0; i < f["options"].size(); ++i) {
            const std::string o = f["options"][i].get<std::string>();
            if (o.rfind("Yes — Accelerate", 0) != 0) continue;
            seen = true;
            EXPECT_EQ(f["human_choice"].get<int>(), static_cast<int>(i)) << f.dump(1);
        }
    }
    EXPECT_TRUE(seen) << "no Accelerate decision was offered";
    std::filesystem::remove_all(out);
}

TEST_F(GuidedReplayTest, ARewindAfterTheDrawDoesNotUndoThePlay) {
    auto game = guided::humanGameFromJson(syntheticGame(/*rewind_after_draw=*/true), 1, "synthetic.json");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    guided::GuidedOptions opts;
    opts.seed = 3;
    auto res = guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
    const auto& s = res.summary;
    EXPECT_EQ(s["rewinds_ignored"].get<int>(), 1) << s.dump(1);
    EXPECT_EQ(s["rewinds_applied"].get<int>(), 0);
    // The Crab stayed: its later move to Emperor's Dais and the conquest replay.
    EXPECT_EQ(s["desyncs"].get<int>(), 0) << s["desync_list"].dump(1);
    EXPECT_EQ(s["result"]["engine_scores"][1].get<int>(), 1) << s.dump(1);
}

TEST_F(GuidedReplayTest, ClientReadingsOfHidesUndonePlaysAndFaceDownPlays) {
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    // After Alice's Crab: a play taken back (into the hand, its two runes
    // readied again), a hide logged as a move, a play from banishment whose
    // destination the importer left in `from`, and a card shown from face
    // down.
    const nlohmann::json extra = nlohmann::json::array({
        ev(1, "Alice paid 2E — Exhaust 2.", "Alice", "paid"),
        ev(1, "Alice played Scuttle Crab from hand to base.", "Alice", "played",
           {{"card", "Scuttle Crab"}, {"from", "hand"}, {"to", "base"}}),
        ev(1, "Alice moved Scuttle Crab to hand.", "Alice", "moved", {{"card", "Scuttle Crab"}, {"to", "hand"}}),
        ev(1, "Alice readied 1 Calm rune.", "Alice", "readied"),
        ev(1, "Alice readied 1 Calm rune.", "Alice", "readied"),
        ev(1, "Alice recycled 1 Calm rune.", "Alice", "recycled"),
        ev(1, "Alice moved a hidden card to Emperor's Dais.", "Alice", "moved",
           {{"card", "a hidden card"}, {"to", "Emperor's Dais"}}),
        ev(1, "Alice played Seal of Rage from banished to base.", "Alice", "played",
           {{"card", "Seal of Rage"}, {"from", "banished to base"}}),
        ev(1, "Alice revealed Tideturner on the board.", "Alice", "revealed", {{"card", "Tideturner"}}),
    });
    events.insert(events.begin() + 13, extra.begin(), extra.end());
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};

    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    int crab_plays = 0, readied = 0;
    const guided::LogLine* hide = nullptr;
    const guided::LogLine* banished = nullptr;
    const guided::LogLine* facedown = nullptr;
    for (const auto& e : game.events) {
        if (e.kind == "played" && e.card == "Scuttle Crab" && e.actor == "Alice") ++crab_plays;
        if (e.kind == "readied" && e.actor == "Alice") ++readied;
        if (e.kind == "hidden_card_placed" && e.actor == "Alice") hide = &e;
        if (e.card == "Seal of Rage") banished = &e;
        if (e.card == "Tideturner") facedown = &e;
    }
    EXPECT_EQ(crab_plays, 1) << "the play taken back into the hand is gone, the real one stays";
    EXPECT_EQ(readied, 0) << "and so are the readied runes that refunded it";
    ASSERT_NE(hide, nullptr) << "'moved a hidden card to' is a hide";
    EXPECT_EQ(hide->to, "Emperor's Dais");
    EXPECT_TRUE(hide->card.empty());
    ASSERT_NE(banished, nullptr);
    EXPECT_EQ(banished->from, "banished");
    EXPECT_EQ(banished->to, "base");
    ASSERT_NE(facedown, nullptr);
    EXPECT_EQ(facedown->kind, "played") << "a card revealed on the board was played from face down";
    EXPECT_EQ(facedown->from, "face down");
    EXPECT_TRUE(facedown->soft) << "dropped, not a desync, where the engine cannot take it";
}

TEST_F(GuidedReplayTest, AUnitLoggedAtBaseAndMovedAtOnceWasPlayedThere) {
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    // After his conquest Bob plays a second Crab "to base" and "moves" it to
    // Emperor's Dais before anything readies it: a unit enters exhausted
    // (CR 143.4), so it cannot have moved — it was played to the battlefield
    // Bob now controls, the location the client logged last.
    const nlohmann::json extra = nlohmann::json::array({
        ev(1, "Bob paid 2E — Exhaust 2.", "Bob", "paid"),
        ev(1, "Bob played Scuttle Crab from hand to base.", "Bob", "played",
           {{"card", "Scuttle Crab"}, {"from", "hand"}, {"to", "base"}}),
        ev(1, "Bob moved Scuttle Crab to Emperor's Dais.", "Bob", "moved",
           {{"card", "Scuttle Crab"}, {"to", "Emperor's Dais"}, {"battlefield", "Emperor's Dais"}}),
    });
    events.insert(events.begin() + 18, extra.begin(), extra.end());
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};

    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    guided::GuidedOptions opts;
    opts.seed = 3;
    auto res = guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
    EXPECT_EQ(res.summary["desyncs"].get<int>(), 0) << res.summary["desync_list"].dump(1);
}

TEST_F(GuidedReplayTest, AnExhaustedUnitMovedAgainIsALogArtefact) {
    // Bob's Crab made its standard move to Emperor's Dais (exhausting it, CR
    // 144.2) and conquered; "Bob moved Scuttle Crab to base" later that turn,
    // with nothing in the log readying it, is a move the log itself makes
    // impossible: the desync is recorded as a log artefact.
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    events.insert(events.begin() + 18, ev(1, "Bob moved Scuttle Crab to base.", "Bob", "moved",
                                          {{"card", "Scuttle Crab"}, {"to", "base"}}));
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};
    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    guided::GuidedOptions opts;
    opts.seed = 3;
    auto res = guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
    const auto& s = res.summary;
    ASSERT_EQ(s["desyncs"].get<int>(), 1) << s["desync_list"].dump(1);
    const auto& d = s["desync_list"][0];
    EXPECT_EQ(d["reason"].get<std::string>(), "unit is exhausted (no move option)");
    EXPECT_TRUE(d.value("log_artefact", false)) << d.dump(1);
    EXPECT_NE(d.value("log_artefact_rule", std::string()).find("Bob moved Scuttle Crab to Emperor's Dais"),
              std::string::npos) << d.dump(1);
    EXPECT_EQ(s["log_artefacts"].get<int>(), 1);
    EXPECT_EQ(s["desyncs_engine"].get<int>(), 0);
}

TEST_F(GuidedReplayTest, AReadiedUnitMovingAgainIsNotAnArtefact) {
    // The same second move after "Bob readied Scuttle Crab": whatever the
    // engine thinks, the log shows nothing impossible.
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    events.insert(events.begin() + 18, ev(1, "Bob moved Scuttle Crab to base.", "Bob", "moved",
                                          {{"card", "Scuttle Crab"}, {"to", "base"}}));
    events.insert(events.begin() + 18, ev(1, "Bob readied Scuttle Crab.", "Bob", "readied", {{"card", "Scuttle Crab"}}));
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};
    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    guided::GuidedOptions opts;
    opts.seed = 3;
    auto res = guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
    const auto& s = res.summary;
    EXPECT_EQ(s["log_artefacts"].get<int>(), 0) << s["desync_list"].dump(1);
    EXPECT_EQ(s["desyncs_engine"].get<int>(), s["desyncs"].get<int>());
}

TEST_F(GuidedReplayTest, APaymentBelowTheCardsCostIsALogArtefact) {
    // On her first turn Alice "pays 2E" for Blitzcrank, Impassive, which costs
    // [5][A]: the payment the log shows cannot pay for the card.
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    const nlohmann::json extra = nlohmann::json::array({
        ev(1, "Alice paid 2E — Exhaust 2.", "Alice", "paid"),
        ev(1, "Alice played Blitzcrank, Impassive from hand to base.", "Alice", "played",
           {{"card", "Blitzcrank, Impassive"}, {"from", "hand"}, {"to", "base"}}),
    });
    events.insert(events.begin() + 6, extra.begin(), extra.end());
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};
    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    guided::GuidedOptions opts;
    opts.seed = 3;
    auto res = guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
    const auto& s = res.summary;
    ASSERT_GE(s["desyncs"].get<int>(), 1) << s.dump(1);
    const auto& d = s["desync_list"][0];
    EXPECT_EQ(d["event_text"].get<std::string>(), "Alice played Blitzcrank, Impassive from hand to base.");
    EXPECT_TRUE(d.value("log_artefact", false)) << d.dump(1);
    EXPECT_NE(d.value("log_artefact_rule", std::string()).find("paid below cost"), std::string::npos);
}

TEST_F(GuidedReplayTest, OneBattlefieldNameConqueredTwiceInATurnIsTwoBattlefields) {
    // A mirror match where both players brought Emperor's Dais: Bob conquers
    // "Emperor's Dais" twice in one turn, which one battlefield cannot give
    // (CR 472: once per battlefield per turn). Both decks bring it.
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    const nlohmann::json extra = nlohmann::json::array({
        ev(1, "Bob moved Scuttle Crab to Emperor's Dais.", "Bob", "moved",
           {{"card", "Scuttle Crab"}, {"to", "Emperor's Dais"}, {"battlefield", "Emperor's Dais"}}),
        ev(1, "Bob passed focus at Emperor's Dais.", "Bob", "passed_focus", {{"battlefield", "Emperor's Dais"}}),
        ev(1, "Alice ended showdown responses at Emperor's Dais.", "Alice", "showdown", {{"battlefield", "Emperor's Dais"}}),
        ev(1, "Bob conquered Emperor's Dais and scored 1.", "Bob", "conquered", {{"battlefield", "Emperor's Dais"}, {"amount", 1}}),
    });
    events.insert(events.begin() + 18, extra.begin(), extra.end());
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};
    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    EXPECT_EQ(d1.preferred_battlefield, "Emperor's Dais");
    EXPECT_EQ(d2.preferred_battlefield, "Emperor's Dais");
    EXPECT_EQ(d2.notes.value("battlefield_mirrored", std::string()), "Emperor's Dais");
    // Without the second conquest nothing says the other player brought it.
    auto plain = guided::humanGameFromJson(syntheticGame(), 1, "synthetic.json");
    EXPECT_EQ(guided::inferDeck(plain, "Bob", db, candidates).preferred_battlefield, "");
}

namespace {

guided::GuidedResult replayWith(const nlohmann::json& file, const CardDB& db, const CardRegistry& registry,
                                const std::vector<std::string>& candidates) {
    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    auto d1 = guided::inferDeck(game, "Alice", db, candidates);
    auto d2 = guided::inferDeck(game, "Bob", db, candidates);
    guided::GuidedOptions opts;
    opts.seed = 3;
    return guided::runGuidedReplay(game, d1, d2, db, registry, nullptr, opts);
}

}  // namespace

TEST_F(GuidedReplayTest, ADraggedUnitMovedOnceToWhereItWasDropped) {
    // "Bob moved Scuttle Crab to Emperor's Dais", at once "... to base" and
    // "... to Emperor's Dais": a move exhausts the Crab, nothing readies it,
    // so these are the spots the client logged while Bob dragged it — one
    // move, to Emperor's Dais.
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    const nlohmann::json extra = nlohmann::json::array({
        ev(1, "Bob moved Scuttle Crab to Emperor's Dais.", "Bob", "moved",
           {{"card", "Scuttle Crab"}, {"to", "Emperor's Dais"}, {"battlefield", "Emperor's Dais"}}),
        ev(1, "Bob moved Scuttle Crab to base.", "Bob", "moved", {{"card", "Scuttle Crab"}, {"to", "base"}}),
    });
    events.insert(events.begin() + 14, extra.begin(), extra.end());
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};
    auto res = replayWith(file, db, registry, candidates);
    EXPECT_EQ(res.summary["desyncs"].get<int>(), 0) << res.summary["desync_list"].dump(1);
    EXPECT_EQ(res.summary["result"]["engine_scores"][1].get<int>(), 1) << "Bob still conquers";
}

TEST_F(GuidedReplayTest, ACardDraggedOutOfTheTrashAndBackWasNotPlayed) {
    // "Bob played Scuttle Crab from trash to base", at once "Bob moved Scuttle
    // Crab from base to trash": the client's drag, taken back — no play.
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    const nlohmann::json extra = nlohmann::json::array({
        ev(1, "Bob played Scuttle Crab from trash to base.", "Bob", "played",
           {{"card", "Scuttle Crab"}, {"from", "trash"}, {"to", "base"}}),
        ev(1, "Bob moved Scuttle Crab from base to trash.", "Bob", "killed",
           {{"card", "Scuttle Crab"}, {"from", "base"}, {"to", "trash"}}),
    });
    events.insert(events.begin() + 18, extra.begin(), extra.end());
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};
    auto res = replayWith(file, db, registry, candidates);
    EXPECT_EQ(res.summary["desyncs"].get<int>(), 0) << res.summary["desync_list"].dump(1);
}

TEST_F(GuidedReplayTest, ADeathTheClientTookBackIsNotAPlayFromTheTrash) {
    // "Bob moved Scuttle Crab from Emperor's Dais to trash" with nothing that
    // kills it, then "Bob played Scuttle Crab from trash to Emperor's Dais":
    // the engine has the Crab alive there all along.
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    const nlohmann::json extra = nlohmann::json::array({
        ev(1, "Bob moved Scuttle Crab from Emperor's Dais to trash.", "Bob", "killed",
           {{"card", "Scuttle Crab"}, {"from", "Emperor's Dais"}, {"to", "trash"}}),
        ev(1, "Bob played Scuttle Crab from trash to Emperor's Dais.", "Bob", "played",
           {{"card", "Scuttle Crab"}, {"from", "trash"}, {"to", "Emperor's Dais"}}),
    });
    events.insert(events.begin() + 18, extra.begin(), extra.end());
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};
    auto res = replayWith(file, db, registry, candidates);
    EXPECT_EQ(res.summary["desyncs"].get<int>(), 0) << res.summary["desync_list"].dump(1);
}

TEST_F(GuidedReplayTest, TheFocusPassesOfAnImpossibleMoveAreLogArtefactsToo) {
    // Bob's Crab, exhausted by its move to Emperor's Dais, "moves" on to
    // another battlefield and both players "pass focus" there: the move is a
    // log artefact, and so are the passes of the showdown it opened.
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    const nlohmann::json extra = nlohmann::json::array({
        ev(1, "Bob moved Scuttle Crab to Hidden Grove.", "Bob", "moved",
           {{"card", "Scuttle Crab"}, {"to", "Hidden Grove"}, {"battlefield", "Hidden Grove"}}),
        ev(1, "Bob passed focus at Hidden Grove.", "Bob", "passed_focus", {{"battlefield", "Hidden Grove"}}),
    });
    events.insert(events.begin() + 18, extra.begin(), extra.end());
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};
    auto res = replayWith(file, db, registry, candidates);
    const auto& s = res.summary;
    ASSERT_EQ(s["desyncs"].get<int>(), 2) << s["desync_list"].dump(1);
    EXPECT_EQ(s["log_artefacts"].get<int>(), 2) << s["desync_list"].dump(1);
    EXPECT_NE(s["desync_list"][1].value("log_artefact_rule", std::string()).find("Bob moved Scuttle Crab to Hidden Grove"),
              std::string::npos) << s["desync_list"][1].dump(1);
}

TEST_F(GuidedReplayTest, AHideLoggedAfterTheEndOfTurnBelongsBeforeIt) {
    // "Bob ended their turn", then "Bob put a hidden card to Emperor's Dais"
    // and its "Bob recycled 1 Calm rune": a card is hidden only on its
    // controller's own turn (CR 811.1.b), so both go before the end of turn.
    nlohmann::json file = syntheticGame();
    auto& events = file["events"];
    events.push_back(ev(1, "Bob put a hidden card to Emperor's Dais.", "Bob", "hidden_card_placed",
                        {{"to", "Emperor's Dais"}}));
    events.push_back(ev(1, "Bob recycled 1 Calm rune.", "Bob", "recycled", {{"amount", 1}, {"rune", "Calm"}}));
    file["games"][0]["events"] = {0, static_cast<int>(events.size())};
    auto game = guided::humanGameFromJson(file, 1, "synthetic.json");
    ASSERT_GE(game.events.size(), 3u);
    const auto n = game.events.size();
    EXPECT_EQ(game.events[n - 1].kind, "ended_turn");
    EXPECT_EQ(game.events[n - 1].actor, "Bob");
    EXPECT_EQ(game.events[n - 3].kind, "hidden_card_placed");
    EXPECT_EQ(game.events[n - 2].kind, "recycled");
}
