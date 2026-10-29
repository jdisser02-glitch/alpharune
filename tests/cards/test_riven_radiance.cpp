/// @file test_riven_radiance.cpp
/// Radiance cards for the Riven, The Exile deck: the legend's enter-ready
/// grant, Blade of the Exile's exhaust equip, Reckless Innovator's Gold,
/// Bedeviling Stranger's bounce, Riven, Broken Blade's three modes and their
/// once-per-turn restriction, and Hunters' Circle's discount.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kTheExile         = 961;
constexpr CardDefId kBladeOfTheExile  = 962;
constexpr CardDefId kRivenBrokenBlade = 963;
constexpr CardDefId kStranger         = 964;
constexpr CardDefId kInnovator        = 965;
constexpr CardDefId kHuntersCircle    = 966;
constexpr CardDefId kOtterpus         = 840;  // 2-energy 2-Might unit, no play or exhaust text

/// Answers yes/no and mode prompts from a queue of int answers, picks `want`
/// when offered objects, passes priority otherwise. Records every int prompt
/// it was shown.
class RivenAgent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    GameObjectId want = kInvalidId;
    std::vector<std::vector<int>> int_prompts;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice && f.chosen_value.has_value()) {
            std::vector<int> offered;
            for (const auto& i : legal) offered.push_back(i.chosen_value.value_or(-1));
            int_prompts.push_back(offered);
            if (next < answers.size()) {
                int a = answers[next++];
                for (const auto& i : legal) if (i.chosen_value == a) return i;
            }
            return f;
        }
        if (f.type == IntentType::MakeChoice && !f.chosen_objects.empty()) {
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class RivenRadianceTest : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    RivenAgent p1_, p2_;
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
    void setBattlefieldCard(BattlefieldId bf, CardDefId def_id) {
        auto id = s().createObject();
        auto& o = s().getObject(id);
        o.card_type = CardType::Battlefield;
        o.card_def_id = def_id;
        o.name = card_db.get(def_id).name;
        s().battlefields[bf].card_object_id = id;
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
    int readyRunes(PlayerId p) {
        int n = 0;
        for (auto& [id, o] : s().objects)
            if (o.isRune() && o.controller == p && o.location.has_value() && !o.is_exhausted) ++n;
        return n;
    }
    void play(GameObjectId card, std::optional<LocationId> where) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
    }
    /// Exhaust `unit` with a standard move (base -> BF1), the usual way a
    /// unit becomes exhausted.
    void moveToBf1(GameObjectId unit) {
        engine_->testHook_executeIntent(
            Intent::standardMove(P1, {unit}, BattlefieldLocation{1}));
        engine_->testHook_runChain();
    }
    bool offersPlayAt(GameObjectId card, LocationId where) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::PlayCard && a.card == card &&
                a.play_location == std::optional<LocationId>{where})
                return true;
        return false;
    }
    void controlBf(BattlefieldId bf, PlayerId p) { s().battlefields[bf].controller = p; }
};

// ─── RAD-149 Riven, The Exile ───────────────────────────────────────────────

TEST_F(RivenRadianceTest, TheExile_UnitPlayedToAControlledBattlefieldEntersReady) {
    makeEngine();
    setLegend(P1, kTheExile); setLegend(P2);
    controlBf(0, P1);
    s().player(P1).rune_pool.energy = 2;
    auto unit = cardInHand(P1, kOtterpus);
    play(unit, LocationId{BattlefieldLocation{0}});
    ASSERT_TRUE(s().getObject(unit).isAtBattlefield());
    EXPECT_FALSE(s().getObject(unit).is_exhausted);
}

TEST_F(RivenRadianceTest, TheExile_UnitPlayedToBaseStillEntersExhausted) {
    makeEngine();
    setLegend(P1, kTheExile); setLegend(P2);
    controlBf(0, P1);
    s().player(P1).rune_pool.energy = 2;
    auto unit = cardInHand(P1, kOtterpus);
    play(unit, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(unit).isAtBase());
    EXPECT_TRUE(s().getObject(unit).is_exhausted);
}

TEST_F(RivenRadianceTest, TheExile_NoGrantWithoutTheLegend) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    controlBf(0, P1);
    s().player(P1).rune_pool.energy = 2;
    auto unit = cardInHand(P1, kOtterpus);
    play(unit, LocationId{BattlefieldLocation{0}});
    EXPECT_TRUE(s().getObject(unit).is_exhausted);
}

TEST_F(RivenRadianceTest, TheExile_ChampionFromChampionZoneMayBePlayedToAControlledBattlefield) {
    makeEngine();
    setLegend(P1, kTheExile); setLegend(P2);
    controlBf(0, P1);
    s().player(P1).rune_pool.energy = 4;
    auto riven = cardInHand(P1, kRivenBrokenBlade);
    auto& ps = s().player(P1);
    ps.hand.erase(std::find(ps.hand.begin(), ps.hand.end(), riven));
    s().getObject(riven).zone = ZoneType::ChampionZone;
    ps.champion_zone = riven;
    EXPECT_TRUE(offersPlayAt(riven, LocationId{BattlefieldLocation{0}}));
    EXPECT_FALSE(offersPlayAt(riven, LocationId{BattlefieldLocation{1}})) << "BF1 is not controlled";
}

// ─── RAD-150 Blade of the Exile ─────────────────────────────────────────────

TEST_F(RivenRadianceTest, Blade_EquipByExhaustingAFriendlyUnit) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto blade   = boardObject(P1, kBladeOfTheExile);
    auto carrier = boardObject(P1, kOtterpus);
    auto payer   = boardObject(P1, kOtterpus);
    // No runes: the [A] alternative can't be paid, only the exhaust one.
    bool offered = false, power_offered = false;
    for (const auto& a : engine_->generateLegalActions()) {
        if (a.type != IntentType::ActivateAbility || a.ability_source != blade) continue;
        if (a.targets == std::vector<GameObjectId>{carrier, payer}) offered = true;
        if (a.targets.size() == 1) power_offered = true;
    }
    EXPECT_TRUE(offered);
    EXPECT_FALSE(power_offered) << "no rune to pay [A]";

    Intent equip;
    equip.type = IntentType::ActivateAbility;
    equip.player = P1;
    equip.ability_source = blade;
    equip.targets = {carrier, payer};
    engine_->testHook_executeIntent(equip);

    EXPECT_EQ(s().getObject(blade).attached_to, std::optional<GameObjectId>{carrier});
    EXPECT_TRUE(s().getObject(payer).is_exhausted) << "the cost: exhaust a friendly unit";
    EXPECT_FALSE(s().getObject(carrier).is_exhausted);
    EXPECT_EQ(s().getObject(carrier).current_might, 5) << "2 + 3";
    EXPECT_TRUE(s().getObject(carrier).hasKeyword(Keyword::Ganking));
}

TEST_F(RivenRadianceTest, Blade_EquipForOnePower) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto blade   = boardObject(P1, kBladeOfTheExile);
    auto carrier = boardObject(P1, kOtterpus);
    s().getObject(carrier).is_exhausted = true;   // nothing to exhaust
    rune(P1, Domain::Chaos);
    Intent equip;
    equip.type = IntentType::ActivateAbility;
    equip.player = P1;
    equip.ability_source = blade;
    equip.targets = {carrier};
    engine_->testHook_executeIntent(equip);
    EXPECT_EQ(s().getObject(blade).attached_to, std::optional<GameObjectId>{carrier});
    int runes = 0;
    for (auto& [id, o] : s().objects) if (o.isRune() && o.location.has_value()) ++runes;
    EXPECT_EQ(runes, 0) << "the rune was recycled for [A]";
}

// ─── RAD-105 Reckless Innovator ─────────────────────────────────────────────

TEST_F(RivenRadianceTest, Innovator_ExhaustedMakesAnExhaustedGoldToken) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto inno = boardObject(P1, kInnovator);
    p1_.answers = {0};   // mode 0: Gold
    moveToBf1(inno);
    int gold = 0;
    for (auto& [id, o] : s().objects)
        if (o.isGear() && o.controller == P1 && o.name == "Gold") {
            ++gold;
            EXPECT_TRUE(o.is_exhausted);
            EXPECT_EQ(o.card_def_id, kGoldGearCardDefId);
        }
    EXPECT_EQ(gold, 1);
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0})) << "no Chaos power: draw not offered";
}

TEST_F(RivenRadianceTest, Innovator_PayChaosPowerToDraw) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto inno = boardObject(P1, kInnovator);
    rune(P1, Domain::Chaos);
    auto top = s().createObject();
    s().getObject(top).owner = s().getObject(top).controller = P1;
    s().getObject(top).zone = ZoneType::MainDeck;
    s().player(P1).main_deck.push_back(top);
    p1_.answers = {1};   // mode 1: pay [P], draw 1
    moveToBf1(inno);
    EXPECT_EQ(std::count(s().player(P1).hand.begin(), s().player(P1).hand.end(), top), 1);
    EXPECT_EQ(readyRunes(P1), 0) << "the Chaos rune paid the [P]";
}

TEST_F(RivenRadianceTest, Innovator_HasAccelerate) {
    makeEngine();
    EXPECT_TRUE(card_db.get(kInnovator).keywords.has(Keyword::Accelerate));
}

// ─── RAD-107 Bedeviling Stranger ────────────────────────────────────────────

TEST_F(RivenRadianceTest, Stranger_ReturnsAnEnemyUnitAtABattlefieldWithFourUnits) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kOtterpus, 0);
    boardObject(P1, kOtterpus, 0);
    auto victim = boardObject(P2, kOtterpus, 0);
    boardObject(P2, kOtterpus, 0);
    s().player(P1).rune_pool.energy = 2;
    auto stranger = cardInHand(P1, kStranger);
    p1_.answers = {1, 0};   // yes; battlefield 0
    p1_.want = victim;
    play(stranger, LocationId{BaseLocation{P1}});
    EXPECT_EQ(s().getObject(victim).zone, ZoneType::Hand);
    EXPECT_EQ(std::count(s().player(P2).hand.begin(), s().player(P2).hand.end(), victim), 1);
}

TEST_F(RivenRadianceTest, Stranger_DoesNothingWithThreeUnits) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kOtterpus, 0);
    auto victim = boardObject(P2, kOtterpus, 0);
    boardObject(P2, kOtterpus, 0);
    s().player(P1).rune_pool.energy = 2;
    auto stranger = cardInHand(P1, kStranger);
    p1_.answers = {1, 0};
    p1_.want = victim;
    play(stranger, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().getObject(victim).isAtBattlefield());
    EXPECT_TRUE(p1_.int_prompts.empty()) << "the bounce is not offered";
}

TEST_F(RivenRadianceTest, Stranger_HasHidden) {
    makeEngine();
    EXPECT_TRUE(card_db.get(kStranger).keywords.has(Keyword::Hidden));
}

// ─── RAD-043 Riven, Broken Blade ────────────────────────────────────────────

TEST_F(RivenRadianceTest, BrokenBlade_MovingExhaustsHerAndBuffsHer) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto riven = boardObject(P1, kRivenBrokenBlade);
    p1_.answers = {0};   // buff me
    moveToBf1(riven);
    EXPECT_TRUE(s().getObject(riven).is_exhausted);
    EXPECT_EQ(s().getObject(riven).buff_count, 1);
    EXPECT_EQ(s().getObject(riven).current_might, 5);
}

TEST_F(RivenRadianceTest, BrokenBlade_ThreeModesOncePerTurnEach) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto riven = boardObject(P1, kRivenBrokenBlade, 1);
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto& ex = engine_->testHook_executor();
    auto exhaustAndResolve = [&] {
        s().getObject(riven).is_exhausted = false;
        ex.exhaustObject(riven);
        engine_->testHook_runChain();
    };
    p1_.want = enemy;

    // 1st exhaustion: all three modes are not yet chosen, but with no buff
    // only "buff me" is legal.
    p1_.answers = {0};
    exhaustAndResolve();
    ASSERT_EQ(p1_.int_prompts.size(), 1u);
    EXPECT_EQ(p1_.int_prompts[0], (std::vector<int>{0}));
    EXPECT_EQ(s().getObject(riven).buff_count, 1);

    // 2nd: buff me was chosen this turn; stun and ready are offered.
    p1_.answers = {1};
    exhaustAndResolve();
    ASSERT_EQ(p1_.int_prompts.size(), 2u);
    EXPECT_EQ(p1_.int_prompts[1], (std::vector<int>{1, 2}));
    EXPECT_TRUE(s().getObject(enemy).is_stunned);
    EXPECT_EQ(s().getObject(riven).buff_count, 0) << "the stun spent the buff";

    // 3rd: give her a buff from elsewhere; only "ready me" is left.
    s().getObject(riven).buff_count = 1;
    s().getObject(riven).recomputeMight();
    p1_.answers = {2};
    exhaustAndResolve();
    ASSERT_EQ(p1_.int_prompts.size(), 3u);
    EXPECT_EQ(p1_.int_prompts[2], (std::vector<int>{2}));
    EXPECT_FALSE(s().getObject(riven).is_exhausted) << "spent the buff to ready";
    EXPECT_EQ(s().getObject(riven).buff_count, 0);

    // 4th: every mode has been chosen this turn — nothing is offered.
    s().getObject(riven).buff_count = 1;
    exhaustAndResolve();
    EXPECT_EQ(p1_.int_prompts.size(), 3u);
    EXPECT_EQ(s().getObject(riven).buff_count, 1);

    // Next turn the modes are available again.
    s().turn.turn_number += 1;
    p1_.answers = {2};
    exhaustAndResolve();
    ASSERT_EQ(p1_.int_prompts.size(), 4u);
    EXPECT_EQ(p1_.int_prompts[3], (std::vector<int>{0, 1, 2}));
}

// ─── RAD-163 Hunters' Circle ────────────────────────────────────────────────

TEST_F(RivenRadianceTest, HuntersCircle_FirstUnitPlayedHereCostsOneLess) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kHuntersCircle);
    controlBf(0, P1);
    controlBf(1, P1);
    rune(P1, Domain::Calm);   // one Energy for a 2-cost unit
    auto first = cardInHand(P1, kOtterpus);
    EXPECT_TRUE(offersPlayAt(first, LocationId{BattlefieldLocation{0}}));
    EXPECT_FALSE(offersPlayAt(first, LocationId{BattlefieldLocation{1}})) << "no discount there";
    EXPECT_FALSE(offersPlayAt(first, LocationId{BaseLocation{P1}}));

    play(first, LocationId{BattlefieldLocation{0}});
    EXPECT_TRUE(s().getObject(first).isAtBattlefield());
    EXPECT_EQ(readyRunes(P1), 0) << "paid 1 Energy";

    // The second unit played here this turn pays full price.
    rune(P1, Domain::Calm);
    auto second = cardInHand(P1, kOtterpus);
    EXPECT_FALSE(offersPlayAt(second, LocationId{BattlefieldLocation{0}}));
}

TEST_F(RivenRadianceTest, HuntersCircle_NoDiscountOnTheOpponentsTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kHuntersCircle);
    controlBf(0, P1);
    s().turn.turn_player = P2;
    rune(P1, Domain::Calm);
    auto unit = cardInHand(P1, kOtterpus);
    const auto* hc = dynamic_cast<const BattlefieldCard*>(card_registry.get(kHuntersCircle));
    ASSERT_NE(hc, nullptr);
    EXPECT_EQ(hc->playCostReductionHere(s(), s().battlefields[0], P1, s().getObject(unit)), 0);
    s().turn.turn_player = P1;
    EXPECT_EQ(hc->playCostReductionHere(s(), s().battlefields[0], P1, s().getObject(unit)), 1);
}

}  // namespace
}  // namespace riftbound::test
