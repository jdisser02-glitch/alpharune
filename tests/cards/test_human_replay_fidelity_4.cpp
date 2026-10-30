/// @file test_human_replay_fidelity_4.cpp
/// Engine and card gaps behind the first desync of real RiftLite games
/// (lab/tools/human_review.py), fourth pass:
///   - Focus did not pass after a card played in a showdown when its
///     controller used an [Add] ability at chain priority to pay for it: the
///     [Add]'s "focus stays" flag (CR 430.2.a) was read for the whole play
///     (CR 346: focus passes when the chain closes). Real game: Emp4 played
///     Hextech Ray with focus at Rockfall Path using Kai'Sa's [Add]; W then
///     played Hidden Blade before combat. The engine went straight to combat.
///   - [Quick-Draw] gear (it has [Reaction]) was not offered in a showdown
///     (CR 813.1.b). Real game: Receipt played Cloth Armor onto Orianna,
///     Exquisite while defending Ruined Monastery.
///   - Hiding paid its [A] only by recycling a rune, never with power already
///     in the Rune Pool. Real game: Damus used Seal of Rage's "[Add] [R]" to
///     hide Switcheroo and kept three runes for Bewitching Spirit.
///   - Temporal Breach played the banished unit back to Rockfall Path ("Units
///     can't be played here"); it stays banished (Vendetta FAQ: "can't" beats
///     "can"). Real game: ponti's Elder Dragon never came back, so its
///     "deal 1 to each enemy" never killed Emp4's Forecaster.
///   - Aspiring Engineer bounced a gear from the board instead of returning a
///     gear from the trash. Real game: El Contador got Sprite Fountain back.
///   - Edge of Night, played from face down, attached itself to the first
///     friendly unit there, not the one the player chose. Real game: Hob
///     attached it to K'Sante, Courageous, not Vex.
///   - "This combat" lasted the whole turn (CR 468.7.c): Fortified Position's
///     [Shield 2] made a First Mate 7 [M] in a later combat at another
///     battlefield that turn.

#include "tests/cards/card_test_fixture.h"

#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "engine/effect_executor.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace riftbound::test {
namespace {

constexpr CardDefId kHextechRay        = 9;     // [Action] 1E+1P Fury: deal 3 to a unit at a battlefield
constexpr CardDefId kGold              = 326;   // Kill this, [E]: [Reaction] — [Add] [A]
constexpr CardDefId kOtterpus          = 840;   // 2 [M] unit, no text
constexpr CardDefId kClothArmor        = 387;   // [Quick-Draw] Equipment, 1E
constexpr CardDefId kTideturner        = 199;   // [Hidden] unit
constexpr CardDefId kTemporalBreach    = 853;
constexpr CardDefId kAspiringEngineer  = 384;   // 3E+1P Mind unit
constexpr CardDefId kSpriteFountain    = 640;   // gear
constexpr CardDefId kFortifiedPosition = 274;   // battlefield
constexpr CardDefId kEdgeOfNight       = 460;   // [Hidden] Equipment: from face down, attach to a unit here

/// Picks `want` among offered objects, uses `once` (an activation source) one
/// time when offered, otherwise passes and answers the first option.
class Fidelity4Agent : public AgentInterface {
public:
    GameObjectId want = kInvalidId;
    GameObjectId once = kInvalidId;
    bool used = false;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        if (!used && once != kInvalidId)
            for (const auto& i : legal)
                if (i.ability_source == once &&
                    (i.type == IntentType::ActivateAbility || i.type == IntentType::ActivateReactionAbility ||
                     i.type == IntentType::ActivateActionAbility)) {
                    used = true;
                    return i;
                }
        for (const auto& i : legal) {
            for (auto t : i.chosen_objects) if (t == want) return i;
            for (auto t : i.targets) if (t == want && i.type == IntentType::MakeChoice) return i;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassFocus) return i;
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return legal.front();
    }
};

class ReplayFidelity4 : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    Fidelity4Agent p1_, p2_;
    GameState& s() { return engine_->mutableState(); }

    void makeEngine() {
        engine_ = std::make_unique<GameEngine>(card_db, events, card_registry);
        auto& st = s();
        st.mode = ModeOfPlay{};
        st.players[0].id = P1;
        st.players[1].id = P2;
        st.players[0].turns_taken = st.players[1].turns_taken = 5;
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
    GameObjectId setLegend(PlayerId p) {
        auto id = s().createObject();
        auto& lo = s().getObject(id);
        lo.owner = lo.controller = p;
        lo.card_type = CardType::Legend;
        lo.zone = ZoneType::LegendZone;
        s().player(p).legend_zone = id;
        return id;
    }
    void stamp(GameObject& u, PlayerId p, CardDefId def_id) {
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
        u.might_bonus = def.might_bonus;
        u.assault_value = def.assault_value;
        u.shield_value = def.shield_value;
    }
    GameObjectId boardObject(PlayerId p, CardDefId def_id, int at_bf = -1) {
        auto id = s().createObject();
        auto& u = s().getObject(id);
        stamp(u, p, def_id);
        if (at_bf >= 0) {
            u.zone = ZoneType::BattlefieldZone;
            u.location = BattlefieldLocation{static_cast<BattlefieldId>(at_bf)};
        } else {
            u.zone = ZoneType::Base;
            u.location = BaseLocation{p};
        }
        u.recomputeMight();
        return id;
    }
    GameObjectId cardIn(PlayerId p, CardDefId def_id, ZoneType zone) {
        auto id = s().createObject();
        stamp(s().getObject(id), p, def_id);
        s().getObject(id).zone = zone;
        (zone == ZoneType::Trash ? s().player(p).trash : s().player(p).hand).push_back(id);
        return id;
    }
    GameObjectId battlefieldCard(int bf, CardDefId def_id) {
        auto id = s().createObject();
        auto& o = s().getObject(id);
        o.card_def_id = def_id;
        o.card_type = CardType::Battlefield;
        o.name = card_db.get(def_id).name;
        s().battlefields[static_cast<size_t>(bf)].card_object_id = id;
        return id;
    }
    void showdownAt0(PlayerId focus) {
        s().turn.ns_state = NeutralShowdownState::Showdown;
        s().turn.oc_state = OpenClosedState::Open;
        s().battlefields[0].showdown_in_progress = true;
        s().turn.focus_holder = focus;
    }
    bool onBoard(GameObjectId id) { return s().objectExists(id) && s().getObject(id).location.has_value(); }
};

// ── CR 346: a card played with focus passes it, [Add] or not ──

TEST_F(ReplayFidelity4, FocusPassesAfterASpellPaidWithAnAddAtChainPriority) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto target = boardObject(P2, kOtterpus, 0);
    s().getObject(target).base_might = 6;
    s().getObject(target).recomputeMight();
    boardObject(P1, kOtterpus, 0);
    auto ray = cardIn(P1, kHextechRay, ZoneType::Hand);
    auto gold = boardObject(P1, kGold);
    s().player(P1).rune_pool.energy = 1;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Fury)] = 1;
    p1_.once = gold;       // P1 uses Gold's [Add] at chain priority
    showdownAt0(P1);
    s().turn.players_passed_focus = {P2};

    Intent play;
    play.type = IntentType::PlayActionCard;
    play.player = P1;
    play.card = ray;
    play.targets = {target};
    int actions = 0;
    auto next = engine_->testHook_resolveShowdownDecision(0, play, P1, actions);

    ASSERT_TRUE(p1_.used) << "the [Add] was used while Hextech Ray was on the chain";
    EXPECT_EQ(s().getObject(target).damage_marked, 3) << "Hextech Ray resolved";
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(*next, P2) << "focus passes when the chain closes (CR 346)";
    EXPECT_TRUE(s().turn.players_passed_focus.empty()) << "a play resets the passes";
}

// ── [Quick-Draw] has [Reaction]: playable with focus in a showdown ──

TEST_F(ReplayFidelity4, QuickDrawGearIsPlayableInAShowdownAndAttaches) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto unit = boardObject(P2, kOtterpus, 0);
    boardObject(P1, kOtterpus, 0);
    auto armor = cardIn(P2, kClothArmor, ZoneType::Hand);
    s().player(P2).rune_pool.energy = 1;
    showdownAt0(P2);

    Intent qd;
    bool offered = false;
    for (const auto& i : engine_->testHook_generateShowdownActions(P2))
        if (i.card == armor && i.type == IntentType::PlayReaction && i.targets == std::vector<GameObjectId>{unit}) {
            offered = true;
            qd = i;
        }
    ASSERT_TRUE(offered) << "Cloth Armor onto the defender, with focus in the showdown";

    int actions = 0;
    auto next = engine_->testHook_resolveShowdownDecision(0, qd, P2, actions);
    EXPECT_EQ(s().getObject(armor).attached_to.value_or(kInvalidId), unit);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(*next, P1);
}

TEST_F(ReplayFidelity4, QuickDrawGearNeedsItsCostInAShowdown) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P2, kOtterpus, 0);
    auto armor = cardIn(P2, kClothArmor, ZoneType::Hand);
    showdownAt0(P2);
    for (const auto& i : engine_->testHook_generateShowdownActions(P2))
        EXPECT_NE(i.card, armor) << "nothing to pay with";
}

// ── Hiding's [A] can be paid with power already in the pool ──

TEST_F(ReplayFidelity4, HidePaysItsPowerFromTheRunePool) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kOtterpus, 0);
    s().battlefields[0].controller = P1;
    auto hidden = cardIn(P1, kTideturner, ZoneType::Hand);
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Fury)] = 1;   // Seal of Rage's [Add] [R]

    Intent hide;
    bool offered = false;
    for (const auto& i : engine_->generateLegalActions())
        if (i.type == IntentType::HideCard && i.card == hidden) { offered = true; hide = i; }
    ASSERT_TRUE(offered) << "no rune on the board, but [R] in the pool pays the [A]";

    engine_->testHook_executeIntent(hide);
    EXPECT_TRUE(s().getObject(hidden).is_hidden);
    EXPECT_EQ(s().player(P1).rune_pool.totalPower(), 0);
}

TEST_F(ReplayFidelity4, HideStillRecyclesARuneWithAnEmptyPool) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kOtterpus, 0);
    s().battlefields[0].controller = P1;
    auto hidden = cardIn(P1, kTideturner, ZoneType::Hand);
    auto r = s().createObject();
    {
        auto& o = s().getObject(r);
        o.owner = o.controller = P1;
        o.card_type = CardType::Rune;
        o.name = "Chaos Rune";
        o.domains = {Domain::Chaos};
        o.zone = ZoneType::Base;
        o.location = BaseLocation{P1};
    }
    Intent hide;
    hide.type = IntentType::HideCard;
    hide.player = P1;
    hide.card = hidden;
    hide.chosen_battlefield = 0;
    engine_->testHook_executeIntent(hide);
    EXPECT_TRUE(s().getObject(hidden).is_hidden);
    EXPECT_FALSE(onBoard(r)) << "the rune is recycled";
}

// ── Temporal Breach cannot play the unit back where units can't be played ──

TEST_F(ReplayFidelity4, TemporalBreachLeavesTheUnitBanishedAtRockfallPath) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto dragon = boardObject(P2, kOtterpus, 0);
    s().battlefields[0].controller = P2;
    s().battlefields[0].blocks_unit_play = true;   // Rockfall Path
    auto tb = cardIn(P1, kTemporalBreach, ZoneType::Hand);
    s().player(P1).rune_pool.energy = 2;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Mind)] = 1;
    p1_.want = dragon;

    Intent play;
    play.type = IntentType::PlayCard;
    play.player = P1;
    play.card = tb;
    play.targets = {dragon};
    engine_->testHook_executeIntent(play);
    engine_->testHook_runChain();

    EXPECT_FALSE(onBoard(dragon)) << "it can't be played to Rockfall Path";
    EXPECT_EQ(s().getObject(dragon).zone, ZoneType::Banishment);
    const auto& bz = s().player(P2).banishment;
    EXPECT_NE(std::find(bz.begin(), bz.end(), dragon), bz.end());
}

TEST_F(ReplayFidelity4, TemporalBreachPlaysTheUnitBackToAnOrdinaryBattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto unit = boardObject(P2, kOtterpus, 0);
    s().battlefields[0].controller = P2;
    s().getObject(unit).damage_marked = 1;
    auto tb = cardIn(P1, kTemporalBreach, ZoneType::Hand);
    s().player(P1).rune_pool.energy = 2;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Mind)] = 1;
    p1_.want = unit;

    Intent play;
    play.type = IntentType::PlayCard;
    play.player = P1;
    play.card = tb;
    play.targets = {unit};
    engine_->testHook_executeIntent(play);
    engine_->testHook_runChain();

    ASSERT_TRUE(onBoard(unit));
    EXPECT_TRUE(s().getObject(unit).location == std::optional<LocationId>{BattlefieldLocation{0}});
    EXPECT_EQ(s().getObject(unit).damage_marked, 0) << "a new object";
    const auto& bz = s().player(P2).banishment;
    EXPECT_EQ(std::find(bz.begin(), bz.end(), unit), bz.end());
}

// ── Aspiring Engineer: "return a gear from your trash to your hand" ──

TEST_F(ReplayFidelity4, AspiringEngineerReturnsAGearFromTheTrash) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto board_gear = boardObject(P1, kGold);
    auto fountain = cardIn(P1, kSpriteFountain, ZoneType::Trash);
    auto engineer = cardIn(P1, kAspiringEngineer, ZoneType::Hand);
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Mind)] = 1;
    p1_.want = fountain;

    Intent play;
    play.type = IntentType::PlayCard;
    play.player = P1;
    play.card = engineer;
    play.play_location = BaseLocation{P1};
    engine_->testHook_executeIntent(play);
    engine_->testHook_runChain();

    const auto& hand = s().player(P1).hand;
    EXPECT_NE(std::find(hand.begin(), hand.end(), fountain), hand.end()) << "Sprite Fountain is back in hand";
    EXPECT_EQ(s().getObject(fountain).zone, ZoneType::Hand);
    const auto& trash = s().player(P1).trash;
    EXPECT_EQ(std::find(trash.begin(), trash.end(), fountain), trash.end());
    EXPECT_TRUE(onBoard(board_gear)) << "a gear on the board is not a candidate";
}

// ── "This combat" ends with the combat (CR 468.7.c) ──

TEST_F(ReplayFidelity4, ThisCombatShieldExpiresWhenTheCombatEnds) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto unit = boardObject(P1, kOtterpus, 0);
    engine_->testHook_executor().giveKeywordThisCombat(unit, Keyword::Shield, 2);
    auto& u = s().getObject(unit);
    EXPECT_TRUE(u.hasKeyword(Keyword::Shield));
    EXPECT_EQ(u.shield_value, 2);
    u.combat_designation = CombatDesignation::Defender;
    u.recomputeMight();
    EXPECT_EQ(u.current_might, 4);

    u.combat_designation = CombatDesignation::None;
    GameEngine::expireThisCombatEffects(u, card_db);
    EXPECT_FALSE(u.hasKeyword(Keyword::Shield));
    EXPECT_EQ(u.shield_value, 0);
    EXPECT_EQ(u.temp_shield_value, 0);
    u.combat_designation = CombatDesignation::Defender;
    u.recomputeMight();
    EXPECT_EQ(u.current_might, 2) << "a later combat this turn does not see it";
}

TEST_F(ReplayFidelity4, ThisCombatMightAndTankExpireButThisTurnGrantsStay) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto unit = boardObject(P1, kOtterpus, 0);
    auto& ex = engine_->testHook_executor();
    ex.giveTemporaryMight(unit, 1);                 // "+1 [M] this turn"
    ex.giveMightThisCombat(unit, 2);                // Fiora, Peerless
    ex.giveKeywordThisCombat(unit, Keyword::Tank, 0);   // Durand Memorial
    auto& u = s().getObject(unit);
    EXPECT_EQ(u.current_might, 5);
    EXPECT_TRUE(u.hasKeyword(Keyword::Tank));
    GameEngine::expireThisCombatEffects(u, card_db);
    EXPECT_EQ(u.current_might, 3) << "the this-turn +1 stays";
    EXPECT_FALSE(u.hasKeyword(Keyword::Tank));
}

TEST_F(ReplayFidelity4, FortifiedPositionShieldsTheDefenderForThisCombatOnly) {
    // P2's 3 [M] unit attacks P1's 2 [M] unit at Fortified Position: with
    // [Shield 2] the defender is 4 [M], survives the 3 and kills the attacker;
    // once the combat is over the [Shield 2] is gone.
    makeEngine();
    setLegend(P1); setLegend(P2);
    battlefieldCard(0, kFortifiedPosition);
    s().battlefields[0].controller = P1;
    auto defender = boardObject(P1, kOtterpus, 0);
    auto attacker = boardObject(P2, kOtterpus, 0);
    s().getObject(attacker).base_might = 3;
    s().getObject(attacker).recomputeMight();
    p1_.want = defender;
    s().turn.turn_player = P2;
    engine_->testHook_cleanup();
    auto& bf = s().battlefields[0];
    bf.is_contested = true;
    bf.contested_by = P2;
    bf.combat_staged = true;
    engine_->testHook_advanceMainPhase();

    EXPECT_FALSE(onBoard(attacker)) << "4 [M] while defending with [Shield 2]";
    ASSERT_TRUE(onBoard(defender));
    EXPECT_EQ(s().getObject(defender).shield_value, 0) << "[Shield 2] lasted this combat";
    EXPECT_FALSE(s().getObject(defender).hasKeyword(Keyword::Shield));
}

// ── Edge of Night: the player picks the unit it attaches to ──

TEST_F(ReplayFidelity4, EdgeOfNightFromFaceDownAttachesToTheChosenUnit) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto first = boardObject(P1, kOtterpus, 0);
    auto chosen = boardObject(P1, kOtterpus, 0);
    s().battlefields[0].controller = P1;
    auto edge = s().createObject();
    {
        auto& o = s().getObject(edge);
        stamp(o, P1, kEdgeOfNight);
        o.zone = ZoneType::FacedownZone;
        o.is_hidden = true;
        o.hidden_at = 0;
        o.hidden_on_turn = 3;
    }
    s().battlefields[0].facedown.push_back(edge);
    p1_.want = chosen;

    Intent play;
    play.type = IntentType::PlayReaction;
    play.player = P1;
    play.card = edge;
    engine_->testHook_executeIntent(play);
    engine_->testHook_runChain();

    EXPECT_EQ(s().getObject(edge).attached_to.value_or(kInvalidId), chosen);
    EXPECT_TRUE(s().getObject(first).attachments.empty());
}

}  // namespace
}  // namespace riftbound::test
