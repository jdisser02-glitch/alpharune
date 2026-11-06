/// @file test_orianna_mordekaiser_audit.cpp
/// Audit of the Orianna (Lady of Clockwork) and Mordekaiser (Iron Revenant)
/// legends, their champions and signature cards, and the cards of the
/// S5 Xi'an City Challenge Orianna list: unit gear (a gear that is also a
/// unit, CR 143.5 / Radiance rules update), Iron Revenant's unit/gear-only
/// [A], Ornn's Forge, Veiled Temple, Frigid Jewel, Hunker Down, Forge of the
/// Fluft, Mordekaiser, Dominating and Mordekaiser, Twice Slain.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kMask         = 60;    // Mask of Foresight: 2-energy plain gear
constexpr CardDefId kSterak       = 379;   // Sterak's Gage: Equipment, +3 [M]
constexpr CardDefId kForge        = 527;   // Ornn's Forge
constexpr CardDefId kFluft        = 522;   // Forge of the Fluft
constexpr CardDefId kVeiled       = 535;   // Veiled Temple
constexpr CardDefId kFrigid       = 636;   // Frigid Jewel
constexpr CardDefId kSpriteFount  = 640;   // Sprite Fountain: 2 energy + 1 Mind power gear
constexpr CardDefId kOtterpus     = 840;   // 2-energy 2-Might unit, no text
constexpr CardDefId kPlaza        = 851;   // Plaza Guardian
constexpr CardDefId kHunker       = 976;   // Hunker Down
constexpr CardDefId kTwiceSlain   = 999;   // Mordekaiser, Twice Slain
constexpr CardDefId kOriannaMM    = 1032;  // Orianna, Mechanical Marvel
constexpr CardDefId kDominating   = 1081;  // Mordekaiser, Dominating
constexpr CardDefId kRevenant     = 1087;  // Iron Revenant
constexpr CardDefId kRealm        = 1088;  // Realm of Death
constexpr CardDefId kClockwork    = 1089;  // Lady of Clockwork
constexpr CardDefId kTheBall      = 1090;
constexpr CardDefId kExquisite    = 1111;  // Orianna, Exquisite
constexpr CardDefId kVi           = 738;   // Vi, Peacekeeper
constexpr CardDefId kBackOff      = 604;

/// Answers int prompts from `answers` in order; object prompts take the next
/// object in `wants`, else `want`, else the first option.
class AuditAgent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    std::vector<GameObjectId> wants;
    size_t wnext = 0;
    GameObjectId want = kInvalidId;
    std::vector<std::vector<int>> int_prompts;
    std::vector<std::vector<GameObjectId>> object_prompts;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        bool any_obj = false, any_val = false;
        for (const auto& i : legal) {
            if (i.type != IntentType::MakeChoice) continue;
            if (!i.chosen_objects.empty()) any_obj = true;
            else if (i.chosen_value.has_value()) any_val = true;
        }
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice && any_obj) {
            std::vector<GameObjectId> offered;
            for (const auto& i : legal)
                if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects.front());
            object_prompts.push_back(offered);
            if (wnext < wants.size()) {
                for (const auto& i : legal)
                    if (!i.chosen_objects.empty() && i.chosen_objects.front() == wants[wnext]) {
                        ++wnext;
                        return i;
                    }
            }
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            return f;
        }
        if (f.type == IntentType::MakeChoice && any_val) {
            std::vector<int> offered;
            for (const auto& i : legal) offered.push_back(i.chosen_value.value_or(-1));
            int_prompts.push_back(offered);
            if (next < answers.size()) {
                int a = answers[next++];
                for (const auto& i : legal) if (i.chosen_value == a) return i;
            }
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class OriannaMordekaiserAuditTest : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    AuditAgent p1_, p2_;
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
    GameObjectId setBattlefieldCard(BattlefieldId bf, CardDefId def_id) {
        auto id = s().createObject();
        auto& o = s().getObject(id);
        o.card_def_id = def_id;
        o.card_type = CardType::Battlefield;
        o.name = card_db.get(def_id).name;
        s().battlefields[bf].card_object_id = id;
        return id;
    }
    GameObjectId boardObject(PlayerId p, CardDefId def_id, int at_bf = -1) {
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
        u.might_bonus = def.might_bonus;
        u.shield_value = def.shield_value;
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
    GameObjectId unitWithMight(PlayerId p, int might, int at_bf = -1) {
        auto id = boardObject(p, kOtterpus, at_bf);
        auto& u = s().getObject(id);
        u.base_might = might;
        u.recomputeMight();
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
        c.zone = ZoneType::Hand;
        s().player(p).hand.push_back(id);
        return id;
    }
    GameObjectId cardInTrash(PlayerId p, CardDefId def_id) {
        auto id = cardInHand(p, def_id);
        auto& ps = s().player(p);
        ps.hand.erase(std::find(ps.hand.begin(), ps.hand.end(), id));
        s().getObject(id).zone = ZoneType::Trash;
        ps.trash.push_back(id);
        return id;
    }
    GameObjectId deckCard(PlayerId p) {   // pushed on top
        auto id = s().createObject();
        auto& c = s().getObject(id);
        c.owner = c.controller = p;
        c.card_def_id = kOtterpus;
        c.card_type = CardType::Unit;
        c.name = "Deck card";
        c.zone = ZoneType::MainDeck;
        s().player(p).main_deck.push_back(id);
        return id;
    }
    void attach(GameObjectId gear, GameObjectId unit) {
        auto& g = s().getObject(gear);
        auto& u = s().getObject(unit);
        g.attached_to = unit;
        g.location = u.location;
        g.zone = u.zone;
        u.attachments.push_back(gear);
        u.attachment_might_bonus += g.might_bonus;
        u.recomputeMight();
    }
    void play(GameObjectId card, std::optional<LocationId> where,
              std::vector<GameObjectId> targets = {}) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        i.targets = std::move(targets);
        engine_->testHook_executeIntent(i);
    }
    void activate(GameObjectId source, std::vector<GameObjectId> targets = {},
                  CardDefId granted = 0) {
        Intent i;
        i.type = IntentType::ActivateAbility;
        i.player = P1;
        i.ability_source = source;
        i.ability_index = 0;
        i.targets = std::move(targets);
        i.granted_ability_def = granted;
        engine_->testHook_executeIntent(i);
    }
    bool offersPlay(GameObjectId card) {
        for (const auto& a : engine_->generateLegalActions())
            if ((a.type == IntentType::PlayCard || a.type == IntentType::PlayActionCard ||
                 a.type == IntentType::PlayReaction) && a.card == card)
                return true;
        return false;
    }
    bool offersMove(GameObjectId unit) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::StandardMove &&
                std::find(a.units_to_move.begin(), a.units_to_move.end(), unit) != a.units_to_move.end())
                return true;
        return false;
    }
    bool dead(GameObjectId id) { return !s().getObject(id).location.has_value(); }
    void startMainPhase() {
        events.emit(PhaseChangedEvent{TurnPhase::DrawPhase, TurnPhase::MainPhase, P1});
        engine_->testHook_runChain();
    }
    /// Lady of Clockwork makes `gear` a unit gear (answers yes, picks it).
    void makeUnitGear(GameObjectId gear) {
        s().player(P1).rune_pool.energy += 1;
        p1_.answers = {1};
        p1_.next = 0;
        p1_.want = gear;
        startMainPhase();
        ASSERT_TRUE(s().getObject(gear).isUnit());
    }
};

// ─── RAD-145 Lady of Clockwork: unit gear ───────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, LadyOfClockwork_UnitGearIsStillAGear) {
    makeEngine();
    setLegend(P1, kClockwork); setLegend(P2);
    auto mask = boardObject(P1, kMask);
    makeUnitGear(mask);
    const auto& m = s().getObject(mask);
    EXPECT_TRUE(m.unit_gear);
    EXPECT_TRUE(m.isAnyGear()) << "a unit gear is a gear";
    EXPECT_FALSE(m.isGear()) << "card_type is Unit while it is one";
    EXPECT_EQ(m.current_might, 2) << "Might = its Energy cost";
    EXPECT_EQ(gearControlled(s(), P1), 1);
    EXPECT_EQ(card_registry.get(kPlaza)->selfCostReduction(s(), P1), 1)
        << "Plaza Guardian: [1] less for each gear you control, unit gear included";
}

TEST_F(OriannaMordekaiserAuditTest, LadyOfClockwork_UnitGearHereLetsOriannaExquisiteDealCombatDamage) {
    makeEngine();
    setLegend(P1, kClockwork); setLegend(P2);
    auto ori = boardObject(P1, kExquisite, 0);
    auto mask = boardObject(P1, kMask);
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(ori).dealsNoCombatDamage()) << "no friendly gear at her battlefield";

    makeUnitGear(mask);
    engine_->testHook_executor().moveToBattlefield(mask, 0);   // the unit gear moves to her
    engine_->testHook_cleanup();
    ASSERT_EQ(s().getObject(mask).battlefieldId(), std::optional<BattlefieldId>{0})
        << "a unit gear is not recalled from a battlefield (CR 149.3: non-unit gear only)";
    EXPECT_FALSE(s().getObject(ori).dealsNoCombatDamage())
        << "the unit gear is a friendly gear here";
}

TEST_F(OriannaMordekaiserAuditTest, LadyOfClockwork_AttachedEquipmentDetachesAsItBecomesAUnit) {
    makeEngine();
    setLegend(P1, kClockwork); setLegend(P2);
    auto host = unitWithMight(P1, 2);
    auto gage = boardObject(P1, kSterak);
    attach(gage, host);
    ASSERT_EQ(s().getObject(host).current_might, 5);
    makeUnitGear(gage);
    const auto& g = s().getObject(gage);
    EXPECT_FALSE(g.attached_to.has_value()) << "CR 143.5.b: it detaches as it becomes a unit";
    EXPECT_TRUE(s().getObject(host).attachments.empty());
    EXPECT_EQ(s().getObject(host).current_might, 2) << "the Might bonus is gone";
    EXPECT_EQ(g.current_might, 3) << "Sterak's Gage costs [3]";
}

TEST_F(OriannaMordekaiserAuditTest, LadyOfClockwork_TheCurrentUnitGearIsNotOfferedAgain) {
    makeEngine();
    setLegend(P1, kClockwork); setLegend(P2);
    auto mask = boardObject(P1, kMask);
    makeUnitGear(mask);
    p1_.int_prompts.clear();
    s().turn.turn_number += 1;
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {1};
    p1_.next = 0;
    startMainPhase();
    EXPECT_TRUE(p1_.int_prompts.empty()) << "no other gear to choose: nothing is offered";
    EXPECT_EQ(s().player(P1).rune_pool.energy, 1);
    EXPECT_TRUE(s().getObject(mask).isUnit()) << "it stays a unit gear";
}

TEST_F(OriannaMordekaiserAuditTest, LadyOfClockwork_AnotherChoiceRevertsTheOldOneWhichIsThenRecalled) {
    makeEngine();
    setLegend(P1, kClockwork); setLegend(P2);
    auto mask = boardObject(P1, kMask);
    auto ball = boardObject(P1, kTheBall);
    makeUnitGear(mask);
    engine_->testHook_executor().moveToBattlefield(mask, 0);
    engine_->testHook_cleanup();
    ASSERT_TRUE(s().getObject(mask).isAtBattlefield());

    s().turn.turn_number += 1;
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {1};
    p1_.next = 0;
    p1_.want = ball;
    startMainPhase();
    ASSERT_TRUE(s().getObject(ball).isUnit());
    const auto& m = s().getObject(mask);
    EXPECT_TRUE(m.isGear());
    EXPECT_FALSE(m.unit_gear);
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(mask).isAtBase())
        << "an unattached non-unit, non-Deploy gear at a battlefield is recalled (CR 149.3)";
}

// ─── RAD-143 Iron Revenant: [A] only for units or gear ─────────────────────

TEST_F(OriannaMordekaiserAuditTest, IronRevenant_PowerPaysForGearButNotForSpells) {
    makeEngine();
    auto legend = setLegend(P1, kRevenant); setLegend(P2);
    auto in_trash = cardInTrash(P1, kOtterpus);
    p1_.want = in_trash;
    activate(legend);
    ASSERT_EQ(s().getObject(in_trash).zone, ZoneType::Banishment);
    EXPECT_EQ(s().player(P1).rune_pool.unit_gear_power, 1);
    EXPECT_EQ(s().player(P1).rune_pool.universal_power, 0);

    s().player(P1).rune_pool.energy = 2;
    auto realm = cardInHand(P1, kRealm);     // [2] + 1 power: a spell
    auto fountain = cardInHand(P1, kSpriteFount);   // [2] + 1 power: a gear
    unitWithMight(P1, 2, 0);
    unitWithMight(P2, 2, 0);
    EXPECT_FALSE(engine_->testHook_canAfford(P1, realm)) << "the [A] can't pay for a spell";
    EXPECT_TRUE(engine_->testHook_canAfford(P1, fountain));
    ASSERT_TRUE(engine_->testHook_payCardCost(P1, fountain));
    EXPECT_EQ(s().player(P1).rune_pool.unit_gear_power, 0) << "spent on the gear";
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
}

// ─── OGN Ornn's Forge ───────────────────────────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, OrnnsForge_OnlyItsOwnControllerGetsTheDiscount) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kVeiled);
    setBattlefieldCard(1, kForge);
    s().battlefields[0].controller = P2;   // P2 controls the OTHER battlefield
    unitWithMight(P2, 2, 0);
    engine_->testHook_cleanup();
    ASSERT_EQ(s().battlefields[0].controller, std::optional<PlayerId>{P2});
    auto discounts = [&](PlayerId p) {
        int n = 0;
        for (const auto& m : s().player(p).cost_modifiers)
            if (m.first_gear_per_turn && m.gear_only) ++n;   // Ornn's Forge's discount
        return n;
    };
    EXPECT_EQ(discounts(P1), 0);
    EXPECT_EQ(discounts(P2), 0) << "P2 doesn't control Ornn's Forge";
    s().battlefields[1].controller = P1;
    unitWithMight(P1, 2, 1);   // holds it through the cleanup
    engine_->testHook_cleanup();
    ASSERT_EQ(s().battlefields[1].controller, std::optional<PlayerId>{P1});
    EXPECT_EQ(discounts(P1), 1);
    EXPECT_EQ(discounts(P2), 0);
}

// ─── OGN Veiled Temple ──────────────────────────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, VeiledTemple_ConquerReadiesTheChosenGearAndMayDetachIt) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kVeiled);
    auto host = unitWithMight(P1, 2, 0);
    auto other = boardObject(P1, kMask);
    auto gage = boardObject(P1, kSterak);
    attach(gage, host);
    s().getObject(gage).is_exhausted = true;
    s().getObject(other).is_exhausted = true;
    p1_.want = gage;
    p1_.answers = {1};   // detach it
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    ASSERT_FALSE(p1_.object_prompts.empty()) << "the gear is the player's choice";
    EXPECT_FALSE(s().getObject(gage).is_exhausted) << "readied";
    EXPECT_TRUE(s().getObject(other).is_exhausted) << "only the chosen gear";
    EXPECT_FALSE(s().getObject(gage).attached_to.has_value()) << "detached";
    EXPECT_EQ(s().getObject(host).current_might, 2);
}

TEST_F(OriannaMordekaiserAuditTest, VeiledTemple_KeepingTheEquipmentAttached) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kVeiled);
    auto host = unitWithMight(P1, 2, 0);
    auto gage = boardObject(P1, kSterak);
    attach(gage, host);
    s().getObject(gage).is_exhausted = true;
    p1_.want = gage;
    p1_.answers = {0};
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    EXPECT_FALSE(s().getObject(gage).is_exhausted);
    EXPECT_EQ(s().getObject(gage).attached_to, std::optional<GameObjectId>{host});
}

// ─── SFD Frigid Jewel ───────────────────────────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, FrigidJewel_SecondDrawBuffsTheChosenFriendlyUnit) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kFrigid);
    auto a = unitWithMight(P1, 2);
    auto b = unitWithMight(P1, 3);
    for (int i = 0; i < 4; ++i) deckCard(P1);
    engine_->testHook_cleanup();
    engine_->testHook_drawCards(P1, 1);
    EXPECT_TRUE(s().chain.items.empty()) << "the first draw doesn't trigger it";
    engine_->testHook_cleanup();
    p1_.want = b;
    engine_->testHook_drawCards(P1, 1);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(b).current_might, 5) << "the chosen unit gets +2 [M]";
    EXPECT_EQ(s().getObject(a).current_might, 2);
    engine_->testHook_cleanup();
    engine_->testHook_drawCards(P1, 1);
    EXPECT_TRUE(s().chain.items.empty()) << "the third draw doesn't trigger it";
}

TEST_F(OriannaMordekaiserAuditTest, FrigidJewel_DrawingTwoAtOnceCountsTheSecond) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kFrigid);
    auto a = unitWithMight(P1, 2);
    for (int i = 0; i < 4; ++i) deckCard(P1);
    engine_->testHook_cleanup();
    engine_->testHook_drawCards(P1, 2);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(a).current_might, 4);
}

// ─── UNL Hunker Down ────────────────────────────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, HunkerDown_TheReadiedUnitCantMoveThisTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto unit = unitWithMight(P1, 2);
    auto other = unitWithMight(P1, 2);
    s().getObject(unit).is_exhausted = true;
    deckCard(P1);
    s().player(P1).rune_pool.energy = 1;
    auto spell = cardInHand(P1, kHunker);
    p1_.want = unit;
    play(spell, std::nullopt);
    engine_->testHook_runChain();
    ASSERT_FALSE(s().getObject(unit).is_exhausted);
    EXPECT_FALSE(offersMove(unit)) << "it can't move this turn";
    EXPECT_TRUE(offersMove(other));
    s().turn.turn_number += 1;
    EXPECT_TRUE(offersMove(unit)) << "next turn it can";
}

// ─── OGN Forge of the Fluft ─────────────────────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, ForgeOfTheFluft_MovesAnAttachedEquipmentToAnotherUnit) {
    makeEngine();
    auto legend = setLegend(P1, kClockwork); setLegend(P2);
    setBattlefieldCard(0, kFluft);
    s().battlefields[0].controller = P1;
    auto a = unitWithMight(P1, 2);
    auto b = unitWithMight(P1, 1);
    auto gage = boardObject(P1, kSterak);
    attach(gage, a);
    engine_->testHook_cleanup();
    p1_.wants = {gage, b};
    activate(legend, {}, kFluft);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(gage).attached_to, std::optional<GameObjectId>{b});
    EXPECT_EQ(s().getObject(a).current_might, 2);
    EXPECT_EQ(s().getObject(b).current_might, 4);
    ASSERT_GE(p1_.object_prompts.size(), 2u);
    EXPECT_EQ(std::count(p1_.object_prompts[1].begin(), p1_.object_prompts[1].end(), a), 0)
        << "not offered the unit it is already on";
}

// ─── RAD-068 Orianna, Mechanical Marvel ─────────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, OriannaMechanicalMarvel_DiscountCoversAGearsPower) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kOriannaMM, 0);
    engine_->testHook_cleanup();
    s().player(P1).rune_pool.energy = 1;   // [2] + 1 power, minus [1][A]: [1]
    auto fountain = cardInHand(P1, kSpriteFount);
    EXPECT_TRUE(engine_->testHook_canAfford(P1, fountain));
    auto realm = cardInHand(P1, kRealm);
    EXPECT_FALSE(engine_->testHook_canAfford(P1, realm)) << "a spell gets no discount";
}

// ─── RAD-137 Mordekaiser, Dominating ────────────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, MordekaiserDominating_AttackKillsAGearDiscardingTheChosenCard) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto morde = boardObject(P1, kDominating, 0);
    auto enemy_gear = boardObject(P2, kMask);
    auto keep = cardInHand(P1, kOtterpus);
    auto toss = cardInHand(P1, kRealm);
    p1_.answers = {1};
    p1_.wants = {enemy_gear, toss};
    engine_->testHook_executor().queueTriggeredAbility(morde, P1, TriggerType::WhenIAttack);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy_gear).zone, ZoneType::Trash) << "a gear can be killed";
    EXPECT_EQ(s().getObject(toss).zone, ZoneType::Trash) << "the card the player picked";
    EXPECT_EQ(s().getObject(keep).zone, ZoneType::Hand);
}

TEST_F(OriannaMordekaiserAuditTest, MordekaiserDominating_DecliningKeepsTheHand) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto morde = boardObject(P1, kDominating, 0);
    auto enemy = unitWithMight(P2, 3, 0);
    auto card = cardInHand(P1, kOtterpus);
    p1_.answers = {0};
    engine_->testHook_executor().queueTriggeredAbility(morde, P1, TriggerType::WhenIAttack);
    engine_->testHook_runChain();
    EXPECT_FALSE(dead(enemy));
    EXPECT_EQ(s().getObject(card).zone, ZoneType::Hand);
}

// ─── RAD-020 Mordekaiser, Twice Slain ───────────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, MordekaiserTwiceSlain_SurvivesADeathAtBaseToo) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto mord = boardObject(P1, kTwiceSlain);
    auto fodder = unitWithMight(P1, 1, 0);
    engine_->testHook_cleanup();
    p1_.want = fodder;
    engine_->testHook_executor().killObject(mord);
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(mord).isAtBase()) << "survived";
    EXPECT_TRUE(s().getObject(mord).is_exhausted);
    EXPECT_TRUE(dead(fodder)) << "the friendly unit is killed to pay for it";
}

TEST_F(OriannaMordekaiserAuditTest, MordekaiserTwiceSlain_CanKillAUnitGearToSurvive) {
    makeEngine();
    setLegend(P1, kClockwork); setLegend(P2);
    auto mask = boardObject(P1, kMask);
    makeUnitGear(mask);
    auto mord = boardObject(P1, kTwiceSlain, 0);
    engine_->testHook_cleanup();
    p1_.want = mask;
    engine_->testHook_executor().killObject(mord);
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(mord).isAtBase());
    EXPECT_EQ(s().getObject(mask).zone, ZoneType::Trash);
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(mask).isGear()) << "a gear again in the trash";
}

// ─── Vi, Peacekeeper (Mordekaiser lists) ────────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, ViPeacekeeper_AttackStunsTheChosenEnemyUnitHere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto vi = boardObject(P1, kVi, 0);
    auto a = unitWithMight(P2, 2, 0);
    auto b = unitWithMight(P2, 3, 0);
    auto far = unitWithMight(P2, 3, 1);
    p1_.want = b;
    engine_->testHook_executor().queueTriggeredAbility(vi, P1, TriggerType::WhenIAttack);
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(b).is_stunned);
    EXPECT_FALSE(s().getObject(a).is_stunned);
    ASSERT_FALSE(p1_.object_prompts.empty());
    EXPECT_EQ(std::count(p1_.object_prompts[0].begin(), p1_.object_prompts[0].end(), far), 0)
        << "only enemy units here";
}

// ─── Back Off (Orianna lists) ───────────────────────────────────────────────

TEST_F(OriannaMordekaiserAuditTest, BackOff_DrawsOnlyWhenPlayedFromHand) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto enemy = unitWithMight(P2, 3, 0);
    unitWithMight(P1, 2, 0);
    for (int i = 0; i < 3; ++i) deckCard(P1);

    // From face down: stun, no draw.
    auto hidden = cardInHand(P1, kBackOff);
    {
        auto& ps = s().player(P1);
        ps.hand.erase(std::find(ps.hand.begin(), ps.hand.end(), hidden));
        auto& c = s().getObject(hidden);
        c.zone = ZoneType::FacedownZone;
        c.is_hidden = true;
        c.hidden_at = 0;
        c.hidden_on_turn = s().turn.turn_number - 1;
        s().battlefields[0].facedown.push_back(hidden);
    }
    p1_.want = enemy;
    Intent i;
    i.type = IntentType::PlayReaction;
    i.player = P1;
    i.card = hidden;
    engine_->testHook_executeIntent(i);
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(enemy).is_stunned);
    EXPECT_TRUE(s().player(P1).hand.empty()) << "played from face down: no draw";

    // From hand: stun and draw 1.
    s().getObject(enemy).is_stunned = false;
    s().player(P1).rune_pool.energy = 5;
    auto from_hand = cardInHand(P1, kBackOff);
    play(from_hand, std::nullopt);
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(enemy).is_stunned);
    EXPECT_EQ(s().player(P1).hand.size(), 1u) << "drew 1";
}

}  // namespace
}  // namespace riftbound::test
