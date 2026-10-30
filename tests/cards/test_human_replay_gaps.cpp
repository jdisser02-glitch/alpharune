/// @file test_human_replay_gaps.cpp
/// Engine gaps found by replaying recorded human games (lab/human_games)
/// through the engine. Each test reproduces the real-game situation minimally.
///
///  * Astral Heron played as the first card of the turn to a battlefield the
///    player controls discounts the next card (CR 420.4.a: "when you play a
///    card" triggers when the play completes by resolution — the Heron is on
///    the board by then and sees its own play). Real game: Sziszu paid 7E for
///    the Heron at Rakelstake and then played Blade of the Exile (3E 1P) for
///    one Calm rune.
///  * Hiding a card costs [A], paid by recycling a rune — an exhausted rune
///    pays it too (CR 164.2.b). Real game: light "paid 4E — Exhaust 4",
///    "recycled 1 Calm rune", "put a hidden card to Emperor's Dais".

#include "tests/cards/card_test_fixture.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kTheExile        = 961;
constexpr CardDefId kBladeOfTheExile = 962;   // 3E 1P gear
constexpr CardDefId kAstralHeron     = 831;   // 7E unit
constexpr CardDefId kTideturner      = 199;   // 2E unit with [Hidden]
constexpr CardDefId kOtterpus        = 840;   // 2E 2M vanilla unit
constexpr CardDefId kRivenBrokenBlade = 963;  // 4E champion unit
constexpr CardDefId kStarSpring      = 771;

/// Passes priority; answers "yes" to every may-prompt; picks `want` when
/// offered objects (else the first); records the object prompts it saw.
class PassingAgent : public AgentInterface {
public:
    GameObjectId want = kInvalidId;
    std::vector<std::vector<GameObjectId>> object_prompts;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice && f.chosen_value.has_value()) {
            for (const auto& i : legal) if (i.chosen_value == 1) return i;
            return f;
        }
        if (f.type == IntentType::MakeChoice && !f.chosen_objects.empty()) {
            std::vector<GameObjectId> offered;
            for (const auto& i : legal) if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects.front());
            object_prompts.push_back(offered);
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class HumanReplayGaps : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    PassingAgent p1_, p2_;
    GameState& s() { return engine_->mutableState(); }

    void makeEngine() {
        engine_ = std::make_unique<GameEngine>(card_db, events, card_registry);
        auto& st = s();
        st.mode = ModeOfPlay{};
        st.players[0].id = P1;
        st.players[1].id = P2;
        for (int i = 0; i < 2; ++i) {
            BattlefieldState bf;
            bf.id = i;
            st.battlefields.push_back(bf);
        }
        engine_->testHook_setAgents(&p1_, &p2_);
        st.turn.turn_player = P1;
        st.turn.turn_number = 5;
        st.turn.phase = TurnPhase::MainPhase;
        st.turn.ns_state = NeutralShowdownState::Neutral;
        st.turn.oc_state = OpenClosedState::Open;
    }
    GameObjectId setLegend(PlayerId p, CardDefId def_id = kInvalidId) {
        auto id = s().createObject();
        auto& lo = s().getObject(id);
        lo.owner = lo.controller = p;
        lo.card_type = CardType::Legend;
        lo.card_def_id = def_id;
        if (def_id != kInvalidId) lo.name = card_db.get(def_id).name;
        lo.zone = ZoneType::LegendZone;
        s().player(p).legend_zone = id;
        return id;
    }
    GameObjectId cardInHand(PlayerId p, CardDefId def_id) {
        auto id = s().createObject();
        auto& c = s().getObject(id);
        const auto& def = card_db.get(def_id);
        c.owner = c.controller = p;
        c.card_def_id = def_id;
        c.card_type = def.card_type;
        c.super_type = def.super_type;
        c.name = def.name;
        c.domains = def.domains;
        c.keywords = def.keywords;
        c.tags = def.tags;
        c.base_might = def.might;
        c.might_bonus = def.might_bonus;
        c.zone = ZoneType::Hand;
        s().player(p).hand.push_back(id);
        return id;
    }
    GameObjectId rune(PlayerId p, Domain d, bool exhausted = false) {
        auto id = s().createObject();
        auto& r = s().getObject(id);
        r.owner = r.controller = p;
        r.card_type = CardType::Rune;
        r.name = "Rune";
        r.domains = {d};
        r.zone = ZoneType::Base;
        r.location = BaseLocation{p};
        r.is_exhausted = exhausted;
        return id;
    }
    int runesInBase(PlayerId p) {
        int n = 0;
        for (auto& [id, o] : s().objects)
            if (o.isRune() && o.controller == p && o.location.has_value()) ++n;
        return n;
    }
    void play(GameObjectId card, std::optional<LocationId> where) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
    bool offersPlayAt(GameObjectId card, LocationId where) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::PlayCard && a.card == card &&
                a.play_location == std::optional<LocationId>{where})
                return true;
        return false;
    }
    std::optional<Intent> hideOption(GameObjectId card, BattlefieldId bf) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::HideCard && a.card == card && a.chosen_battlefield == bf)
                return a;
        return std::nullopt;
    }
    void controlBf(BattlefieldId bf, PlayerId p) { s().battlefields[bf].controller = p; }
    void setBattlefieldCard(BattlefieldId bf, CardDefId def_id) {
        auto id = s().createObject();
        auto& o = s().getObject(id);
        o.card_type = CardType::Battlefield;
        o.card_def_id = def_id;
        o.name = card_db.get(def_id).name;
        s().battlefields[bf].card_object_id = id;
    }
    GameObjectId boardUnit(PlayerId p, CardDefId def_id, BattlefieldId at_bf, bool exhausted = true) {
        auto id = s().createObject();
        auto& u = s().getObject(id);
        const auto& def = card_db.get(def_id);
        u.owner = u.controller = p;
        u.card_def_id = def_id;
        u.card_type = def.card_type;
        u.super_type = def.super_type;
        u.name = def.name;
        u.domains = def.domains;
        u.keywords = def.keywords;
        u.tags = def.tags;
        u.base_might = def.might;
        u.zone = ZoneType::BattlefieldZone;
        u.location = BattlefieldLocation{at_bf};
        u.is_exhausted = exhausted;
        u.recomputeMight();
        return id;
    }
};

// ─── Star Spring moves ANOTHER unit, of the player's choice ─────────────────
// Real game: Sziszu's Scuttle Crab conquered Star Spring, Riven was played
// there from the Champion Zone and the Crab went home; the engine sent Riven
// home instead (it took the newest object id for "the played unit").

TEST_F(HumanReplayGaps, StarSpring_MovesTheOtherUnitNotTheOnePlayed) {
    makeEngine();
    setLegend(P1, kTheExile); setLegend(P2);
    setBattlefieldCard(0, kStarSpring);
    controlBf(0, P1);
    s().player(P1).rune_pool.energy = 4;
    auto riven = cardInHand(P1, kRivenBrokenBlade);      // the older object id
    auto crab = boardUnit(P1, kOtterpus, 0);              // the newer one, already here
    ASSERT_LT(riven, crab);

    play(riven, LocationId{BattlefieldLocation{0}});      // the agent answers "yes"

    EXPECT_TRUE(s().getObject(riven).isAtBattlefield()) << "the played unit stays";
    EXPECT_TRUE(s().getObject(crab).isAtBase()) << "'another unit they control here' goes home";
}

TEST_F(HumanReplayGaps, StarSpring_AsksWhichUnitWhenSeveralQualify) {
    makeEngine();
    setLegend(P1, kTheExile); setLegend(P2);
    setBattlefieldCard(0, kStarSpring);
    controlBf(0, P1);
    s().player(P1).rune_pool.energy = 4;
    auto riven = cardInHand(P1, kRivenBrokenBlade);
    auto a = boardUnit(P1, kOtterpus, 0);
    auto b = boardUnit(P1, kOtterpus, 0);
    p1_.want = b;

    play(riven, LocationId{BattlefieldLocation{0}});

    ASSERT_EQ(p1_.object_prompts.size(), 1u) << "one prompt: which unit goes home";
    EXPECT_EQ(p1_.object_prompts[0].size(), 2u);
    EXPECT_TRUE(s().getObject(riven).isAtBattlefield());
    EXPECT_TRUE(s().getObject(a).isAtBattlefield());
    EXPECT_TRUE(s().getObject(b).isAtBase());
}

// ─── Astral Heron sees its own play (CR 420.4.a) ────────────────────────────

TEST_F(HumanReplayGaps, AstralHeron_PlayedFirstToAControlledBattlefieldDiscountsTheNextCard) {
    makeEngine();
    setLegend(P1, kTheExile); setLegend(P2);
    controlBf(0, P1);
    s().player(P1).rune_pool.energy = 7;       // the Heron's 7E
    rune(P1, Domain::Calm);                     // the one rune left for the Blade
    auto heron = cardInHand(P1, kAstralHeron);
    auto blade = cardInHand(P1, kBladeOfTheExile);
    ASSERT_EQ(s().player(P1).cards_played_this_turn, 0);

    play(heron, LocationId{BattlefieldLocation{0}});
    ASSERT_TRUE(s().getObject(heron).isAtBattlefield());

    const auto& mods = s().player(P1).cost_modifiers;
    ASSERT_EQ(mods.size(), 1u) << "the Heron's own play is 'your first card this turn'";
    EXPECT_EQ(mods[0].energy_reduction, 2);
    EXPECT_EQ(mods[0].power_reduction, 2);
    EXPECT_TRUE(mods[0].next_card_only);
    // Blade of the Exile costs 3E 1P; with [2][A][A] less it is 1E and the
    // single ready rune pays it.
    EXPECT_TRUE(offersPlayAt(blade, LocationId{BaseLocation{P1}}));
}

TEST_F(HumanReplayGaps, AstralHeron_PlayedToBaseGivesNoDiscount) {
    makeEngine();
    setLegend(P1, kTheExile); setLegend(P2);
    controlBf(0, P1);
    s().player(P1).rune_pool.energy = 7;
    rune(P1, Domain::Calm);
    auto heron = cardInHand(P1, kAstralHeron);
    auto blade = cardInHand(P1, kBladeOfTheExile);

    play(heron, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(heron).isAtBase());
    EXPECT_TRUE(s().player(P1).cost_modifiers.empty()) << "if I'm at a battlefield";
    EXPECT_FALSE(offersPlayAt(blade, LocationId{BaseLocation{P1}})) << "3E 1P with one rune";
}

TEST_F(HumanReplayGaps, AstralHeron_SecondCardOfTheTurnGivesNoDiscount) {
    makeEngine();
    setLegend(P1, kTheExile); setLegend(P2);
    controlBf(0, P1);
    s().player(P1).rune_pool.energy = 9;
    auto otter = cardInHand(P1, kOtterpus);
    auto heron = cardInHand(P1, kAstralHeron);
    play(otter, LocationId{BaseLocation{P1}});
    play(heron, LocationId{BattlefieldLocation{0}});
    ASSERT_TRUE(s().getObject(heron).isAtBattlefield());
    EXPECT_TRUE(s().player(P1).cost_modifiers.empty()) << "the Heron was the second card";
}

// ─── Hide pays [A] with any rune, exhausted ones included ───────────────────

TEST_F(HumanReplayGaps, Hide_IsOfferedAndPaidWhenEveryRuneIsExhausted) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    controlBf(0, P1);
    rune(P1, Domain::Calm, /*exhausted=*/true);
    rune(P1, Domain::Calm, /*exhausted=*/true);
    auto tide = cardInHand(P1, kTideturner);
    ASSERT_TRUE(s().getObject(tide).keywords.has(Keyword::Hidden));

    auto hide = hideOption(tide, 0);
    ASSERT_TRUE(hide.has_value()) << "[A] is paid by recycling a rune, ready or exhausted";
    engine_->testHook_executeIntent(*hide);

    EXPECT_TRUE(s().getObject(tide).is_hidden);
    EXPECT_EQ(s().getObject(tide).hidden_at, 0);
    ASSERT_EQ(s().battlefields[0].facedown.size(), 1u);
    EXPECT_EQ(s().battlefields[0].facedown[0], tide);
    EXPECT_EQ(runesInBase(P1), 1) << "one rune was recycled for [A]";
    EXPECT_EQ(s().player(P1).rune_deck.size(), 1u);
    EXPECT_FALSE(s().getObject(s().player(P1).rune_deck.front()).is_exhausted);
}

TEST_F(HumanReplayGaps, Hide_PrefersRecyclingAnExhaustedRune) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    controlBf(0, P1);
    auto ready = rune(P1, Domain::Calm, /*exhausted=*/false);
    auto spent = rune(P1, Domain::Calm, /*exhausted=*/true);
    auto tide = cardInHand(P1, kTideturner);
    auto hide = hideOption(tide, 0);
    ASSERT_TRUE(hide.has_value());
    engine_->testHook_executeIntent(*hide);
    EXPECT_TRUE(s().getObject(ready).location.has_value()) << "the ready rune stays usable";
    EXPECT_FALSE(s().getObject(spent).location.has_value()) << "the exhausted rune paid [A]";
}

TEST_F(HumanReplayGaps, Hide_NeedsARuneAndAControlledBattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto tide = cardInHand(P1, kTideturner);
    rune(P1, Domain::Calm, /*exhausted=*/true);
    EXPECT_FALSE(hideOption(tide, 0).has_value()) << "BF0 is not controlled";
    controlBf(0, P1);
    EXPECT_TRUE(hideOption(tide, 0).has_value());
    for (auto& [id, o] : s().objects)
        if (o.isRune()) o.location = std::nullopt;
    EXPECT_FALSE(hideOption(tide, 0).has_value()) << "no rune to recycle";
}


// ─── CR 144.3: several units move together as one action ────────────────────
// Real game: "Emp4 moved Brynhir Thundersong to Dusk Rose Lab", "Emp4 moved
// Noxus Hopeful to Dusk Rose Lab", "Emp4 passed focus" — one showdown after
// both arrive. The engine offered only single moves, and the first one
// opened the showdown (CR 144.1.c then forbids the second).

TEST_F(HumanReplayGaps, StandardMove_OffersSimultaneousMovesOfSeveralUnits) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto a = boardUnit(P1, kOtterpus, 0, /*exhausted=*/false);
    auto b = boardUnit(P1, kOtterpus, 0, /*exhausted=*/false);
    for (auto id : {a, b}) {
        auto& u = s().getObject(id);
        u.zone = ZoneType::Base;
        u.location = BaseLocation{P1};
    }
    std::optional<Intent> pair;
    int singles = 0;
    for (const auto& in : engine_->generateLegalActions()) {
        if (in.type != IntentType::StandardMove) continue;
        if (!in.move_destination || !std::holds_alternative<BattlefieldLocation>(*in.move_destination) ||
            std::get<BattlefieldLocation>(*in.move_destination).id != 1) continue;
        if (in.units_to_move.size() == 1) ++singles;
        if (in.units_to_move.size() == 2) pair = in;
    }
    EXPECT_EQ(singles, 2);
    ASSERT_TRUE(pair.has_value()) << "both ready units to BF1 as one move";

    engine_->testHook_executeIntent(*pair);
    EXPECT_TRUE(s().getObject(a).isAtBattlefield());
    EXPECT_TRUE(s().getObject(b).isAtBattlefield());
    EXPECT_TRUE(s().getObject(a).is_exhausted) << "CR 144.3.c: both costs paid";
    EXPECT_TRUE(s().getObject(b).is_exhausted);
    EXPECT_TRUE(s().battlefields[1].showdown_staged) << "one showdown, after both arrived";
}

TEST_F(HumanReplayGaps, StandardMove_SingleMovesComeFirstForEachDestination) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    for (int i = 0; i < 2; ++i) {
        auto id = boardUnit(P1, kOtterpus, 0, /*exhausted=*/false);
        s().getObject(id).zone = ZoneType::Base;
        s().getObject(id).location = BaseLocation{P1};
    }
    // The action vocab keys a move by its destination: the first intent per
    // destination must stay the single-unit move.
    bool seen_group = false;
    for (const auto& in : engine_->generateLegalActions()) {
        if (in.type != IntentType::StandardMove) continue;
        if (in.units_to_move.size() > 1) seen_group = true;
        else EXPECT_FALSE(seen_group) << "a single move after a group move";
    }
    EXPECT_TRUE(seen_group);
}

// ─── CR 323.13: a non-combat showdown with enemies arriving becomes combat ──
// Real game: Courage attacked Packed Amphitheater with Tideturner, played
// Moonfall in the showdown moving Bigfalafel's Reckless Innovator there,
// both passed, the Innovator died to combat damage and Courage conquered —
// all in Courage's turn. The engine closed the showdown with nothing
// resolved and only fought the combat at the start of the opponent's turn.

class ShowdownMoverAgent : public AgentInterface {
public:
    GameEngine* engine = nullptr;
    GameObjectId enemy = kInvalidId;    // moved in by the "spell" on P1's first focus
    BattlefieldId bf = 0;
    bool moved = false;
    Intent selectAction(const GameState& st, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        if (!moved && engine && st.turn.focus_holder == PlayerId::Player1) {
            // The effect of a spell resolving in the showdown: an enemy unit
            // is moved to the battlefield (not a standard move).
            moved = true;
            engine->mutableState().getObject(enemy).location = BattlefieldLocation{bf};
            engine->mutableState().getObject(enemy).zone = ZoneType::BattlefieldZone;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassFocus) return i;
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return legal.front();
    }
};

TEST_F(HumanReplayGaps, Showdown_BecomesCombatWhenEnemyUnitsArriveDuringIt) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto attacker = boardUnit(P1, kOtterpus, 0);               // 2M, exhausted by its move
    auto enemy = boardUnit(P2, kOtterpus, 1);                  // parked elsewhere, 2M
    s().getObject(enemy).base_might = 1;                        // dies to 2 damage
    s().getObject(enemy).recomputeMight();
    ShowdownMoverAgent mover;
    mover.engine = engine_.get();
    mover.enemy = enemy;
    engine_->testHook_setAgents(&mover, &p2_);
    auto& bf = s().battlefields[0];
    bf.is_contested = true;
    bf.contested_by = P1;
    bf.showdown_staged = true;
    // Otterpus draws instead of scoring in a player's first two turns; this
    // is a later turn.
    s().player(P1).turns_taken = 5;
    s().player(P2).turns_taken = 5;

    // One main-phase advance: the staged showdown runs (P1 passes, P2
    // passes), the enemy that arrived makes it a combat, damage is dealt
    // and the battlefield is resolved, in this same turn.
    engine_->testHook_advanceMainPhase();

    EXPECT_TRUE(mover.moved);
    EXPECT_FALSE(s().objectExists(enemy) && s().getObject(enemy).location.has_value() &&
                 s().getObject(enemy).isAtBattlefield()) << "the 1M enemy died in combat";
    EXPECT_TRUE(s().getObject(attacker).isAtBattlefield());
    ASSERT_TRUE(bf.controller.has_value());
    EXPECT_EQ(*bf.controller, P1) << "P1 conquered in their own turn";
    EXPECT_EQ(s().player(P1).score, 1);
    EXPECT_FALSE(bf.combat_in_progress);
    EXPECT_FALSE(bf.showdown_in_progress);
}

}  // namespace
}  // namespace riftbound::test
