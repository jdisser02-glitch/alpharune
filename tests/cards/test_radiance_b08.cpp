/// @file test_radiance_b08.cpp
/// Radiance batch 08: Transpositionist ([P], [E]: return a friendly unit, may
/// play a lower-Might unit free), For Demacia! ([Shield 2] at a battlefield),
/// Herald of the Emperor (base Might becomes 5), Hunting Dog (Deathknell
/// Bird with [Deflect]), Meticulous Restorationist (play from trash for [Y]
/// on a [7]+ card), One-Two Punch (buff + stun), Party Crasher ([1] less per
/// exhausted unit), Dune Devourer (kill a friendly unit for [2] less),
/// Mournful Spirit (Deathknell: may kill a unit or gear here) and Rousing
/// Display ([Show Off] a [7]+ card: four Recruits instead of two).

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kTranspositionist = 1065;
constexpr CardDefId kForDemacia       = 1066;
constexpr CardDefId kHerald           = 1067;
constexpr CardDefId kHuntingDog       = 1068;
constexpr CardDefId kRestorationist   = 1069;
constexpr CardDefId kOneTwoPunch      = 1070;
constexpr CardDefId kPartyCrasher     = 1071;
constexpr CardDefId kDuneDevourer     = 1072;
constexpr CardDefId kMournfulSpirit   = 1073;
constexpr CardDefId kRousingDisplay   = 1074;
constexpr CardDefId kOtterpus         = 840;  // 2-energy 2-Might unit, no play or exhaust text
constexpr CardDefId kMegaMech         = 88;   // 7-energy 8-Might vanilla unit
constexpr CardDefId kSealOfRage       = 40;   // a gear

/// Answers int prompts from `answers`, object prompts from `picks` (a pick is
/// consumed only when offered; `want` is the fallback), passes priority
/// otherwise. Records every prompt it was shown.
class B08Agent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    std::vector<GameObjectId> picks;
    size_t pnext = 0;
    GameObjectId want = kInvalidId;
    std::vector<std::vector<int>> int_prompts;
    std::vector<std::vector<GameObjectId>> obj_prompts;

    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice) {
            bool has_objects = false;
            for (const auto& i : legal) if (!i.chosen_objects.empty()) has_objects = true;
            if (has_objects) {
                std::vector<GameObjectId> offered;
                for (const auto& i : legal)
                    if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects.front());
                obj_prompts.push_back(offered);
                if (pnext < picks.size()) {
                    for (const auto& i : legal)
                        if (!i.chosen_objects.empty() && i.chosen_objects.front() == picks[pnext]) {
                            ++pnext;
                            return i;
                        }
                }
                for (const auto& i : legal)
                    if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
                return f;
            }
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

class RadianceB08Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B08Agent p1_, p2_;
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
        setLegend(P1);
        setLegend(P2);
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
    void stamp(GameObject& o, PlayerId p, CardDefId def_id) {
        const auto& def = card_db.get(def_id);
        o.owner = o.controller = p;
        o.card_def_id = def_id;
        o.card_type = def.card_type;
        o.super_type = def.super_type;
        o.name = def.name;
        o.domains = def.domains;
        o.keywords = def.keywords;
        o.tags = def.tags;
        o.base_might = def.might;
        o.might_bonus = def.might_bonus;
        o.assault_value = def.assault_value;
        o.shield_value = def.shield_value;
        o.deflect_value = def.deflect_value;
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
    GameObjectId cardInHand(PlayerId p, CardDefId def_id) {
        auto id = s().createObject();
        auto& c = s().getObject(id);
        stamp(c, p, def_id);
        c.zone = ZoneType::Hand;
        s().player(p).hand.push_back(id);
        return id;
    }
    GameObjectId cardInTrash(PlayerId p, CardDefId def_id) {
        auto id = s().createObject();
        auto& c = s().getObject(id);
        stamp(c, p, def_id);
        c.zone = ZoneType::Trash;
        s().player(p).trash.push_back(id);
        return id;
    }
    bool inHand(PlayerId p, GameObjectId id) {
        const auto& h = s().player(p).hand;
        return std::find(h.begin(), h.end(), id) != h.end();
    }
    bool onBoard(GameObjectId id) {
        return s().objectExists(id) && s().getObject(id).location.has_value();
    }
    void play(GameObjectId card, std::optional<LocationId> where = std::nullopt) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
    void activate(GameObjectId source) {
        Intent act;
        act.type = IntentType::ActivateAbility;
        act.player = P1;
        act.ability_source = source;
        act.ability_index = 0;
        engine_->testHook_executeIntent(act);
        engine_->testHook_runChain();
    }
    int unitsNamed(PlayerId p, const std::string& name) {
        int n = 0;
        for (const auto& [id, o] : s().objects)
            if (o.isUnit() && o.controller == p && o.location.has_value() && o.name == name) ++n;
        return n;
    }
    static bool contains(const std::vector<GameObjectId>& v, GameObjectId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
    void power(PlayerId p, Domain d, int n) {
        s().player(p).rune_pool.power[static_cast<int>(d)] = n;
    }
};

// ─── RAD-115 Transpositionist ───────────────────────────────────────────────

TEST_F(RadianceB08Test, Transpositionist_ReturnsAUnitAndPlaysALowerMightOneFree) {
    makeEngine();
    auto tp = boardObject(P1, kTranspositionist);
    auto otter = cardInHand(P1, kOtterpus);     // 2 [M] < 4
    auto mech = cardInHand(P1, kMegaMech);      // 8 [M]: too big
    power(P1, Domain::Chaos, 1);
    p1_.picks = {tp, otter};                    // return me, then play Otterpus
    p1_.answers = {1};
    activate(tp);
    EXPECT_TRUE(inHand(P1, tp)) << "Transpositionist returned itself";
    EXPECT_TRUE(onBoard(otter)) << "Otterpus played for free";
    EXPECT_TRUE(inHand(P1, mech));
    EXPECT_EQ(s().player(P1).rune_pool.power[static_cast<int>(Domain::Chaos)], 0) << "[P] paid";
    // The free-play prompt offered only the lower-Might unit.
    ASSERT_FALSE(p1_.obj_prompts.empty());
    const auto& last = p1_.obj_prompts.back();
    EXPECT_TRUE(contains(last, otter));
    EXPECT_FALSE(contains(last, mech));
}

TEST_F(RadianceB08Test, Transpositionist_MayDeclineThePlay) {
    makeEngine();
    auto tp = boardObject(P1, kTranspositionist);
    auto other = boardObject(P1, kMegaMech);    // 8 [M] on the board
    auto otter = cardInHand(P1, kOtterpus);
    power(P1, Domain::Chaos, 1);
    p1_.picks = {other};
    p1_.answers = {0};
    activate(tp);
    EXPECT_TRUE(inHand(P1, other));
    EXPECT_TRUE(inHand(P1, otter)) << "declined: nothing played";
    EXPECT_TRUE(s().getObject(tp).is_exhausted);
}

// ─── RAD-117 For Demacia! ───────────────────────────────────────────────────

TEST_F(RadianceB08Test, ForDemacia_GivesShield2ToFriendlyUnitsAtTheChosenBattlefield) {
    makeEngine();
    auto a = boardObject(P1, kOtterpus, 0);
    auto b = boardObject(P1, kOtterpus, 0);
    auto far = boardObject(P1, kOtterpus, 1);
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto spell = cardInHand(P1, kForDemacia);
    s().player(P1).rune_pool.energy = 3;
    p1_.answers = {0};                           // battlefield #0
    play(spell);
    EXPECT_EQ(s().getObject(a).shield_value, 2);
    EXPECT_EQ(s().getObject(b).shield_value, 2);
    EXPECT_EQ(s().getObject(far).shield_value, 0);
    EXPECT_EQ(s().getObject(enemy).shield_value, 0);
    EXPECT_TRUE(card_db.get(kForDemacia).keywords.has(Keyword::Reaction));
}

// ─── RAD-118 Herald of the Emperor ──────────────────────────────────────────

TEST_F(RadianceB08Test, Herald_ChosenFriendlyUnitsBaseMightBecomesFive) {
    makeEngine();
    auto otter = boardObject(P1, kOtterpus);
    auto herald = cardInHand(P1, kHerald);
    s().player(P1).rune_pool.energy = 5;
    p1_.want = otter;
    play(herald, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(onBoard(herald));
    EXPECT_EQ(s().getObject(otter).current_might, 5);
    EXPECT_EQ(s().getObject(herald).current_might, 3);
}

// ─── RAD-119 Hunting Dog ────────────────────────────────────────────────────

TEST_F(RadianceB08Test, HuntingDog_DeathknellPlaysABirdWithDeflectToBase) {
    makeEngine();
    auto dog = boardObject(P1, kHuntingDog, 0);
    engine_->testHook_executor().killObject(dog);
    engine_->testHook_runChain();
    GameObjectId bird = kInvalidId;
    for (const auto& [id, o] : s().objects)
        if (o.isUnit() && o.controller == P1 && o.location.has_value() && o.name == "Bird") bird = id;
    ASSERT_NE(bird, kInvalidId);
    EXPECT_TRUE(s().getObject(bird).isAtBase());
    EXPECT_EQ(s().getObject(bird).current_might, 1);
    EXPECT_TRUE(s().getObject(bird).hasKeyword(Keyword::Deflect));
    EXPECT_TRUE(s().getObject(bird).isToken());
}

// ─── RAD-120 Meticulous Restorationist ──────────────────────────────────────
// (The engine does not yet dispatch WhenYouPlayACard to cards in a trash; the
// ability is queued directly as if it had triggered.)

TEST_F(RadianceB08Test, Restorationist_PaysOrderToPlayItselfFromTrash) {
    makeEngine();
    auto resto = cardInTrash(P1, kRestorationist);
    auto mech = boardObject(P1, kMegaMech);     // the [7]+ card just played
    power(P1, Domain::Order, 1);
    p1_.answers = {1};
    engine_->testHook_executor().queueTriggeredAbility(resto, P1, TriggerType::WhenYouPlayACard, mech);
    engine_->testHook_runChain();
    EXPECT_TRUE(onBoard(resto));
    EXPECT_EQ(s().getObject(resto).zone, ZoneType::Base);
    EXPECT_EQ(s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)], 0);
    const auto& trash = s().player(P1).trash;
    EXPECT_EQ(std::find(trash.begin(), trash.end(), resto), trash.end());
}

TEST_F(RadianceB08Test, Restorationist_NothingForACheaperCardOrADecline) {
    makeEngine();
    auto resto = cardInTrash(P1, kRestorationist);
    auto otter = boardObject(P1, kOtterpus);    // 2 energy
    power(P1, Domain::Order, 1);
    p1_.answers = {1};
    engine_->testHook_executor().queueTriggeredAbility(resto, P1, TriggerType::WhenYouPlayACard, otter);
    engine_->testHook_runChain();
    EXPECT_FALSE(onBoard(resto));
    EXPECT_TRUE(p1_.int_prompts.empty()) << "no prompt for a cheaper card";

    auto mech = boardObject(P1, kMegaMech);
    p1_.answers = {0};
    p1_.next = 0;
    engine_->testHook_executor().queueTriggeredAbility(resto, P1, TriggerType::WhenYouPlayACard, mech);
    engine_->testHook_runChain();
    EXPECT_FALSE(onBoard(resto)) << "declined";
    EXPECT_EQ(s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)], 1);
}

// ─── RAD-121 One-Two Punch ──────────────────────────────────────────────────

TEST_F(RadianceB08Test, OneTwoPunch_BuffsAFriendlyUnitAndStunsAnEnemy) {
    makeEngine();
    auto ally = boardObject(P1, kOtterpus, 0);
    auto enemy = boardObject(P2, kOtterpus, 1);
    auto spell = cardInHand(P1, kOneTwoPunch);
    s().player(P1).rune_pool.energy = 3;
    p1_.picks = {ally, enemy};
    play(spell);
    EXPECT_EQ(s().getObject(ally).buff_count, 1);
    EXPECT_EQ(s().getObject(ally).current_might, 3);
    EXPECT_TRUE(s().getObject(enemy).is_stunned);
    EXPECT_TRUE(card_db.get(kOneTwoPunch).keywords.has(Keyword::Action));
}

TEST_F(RadianceB08Test, OneTwoPunch_AlreadyBuffedUnitGetsNoSecondBuff) {
    makeEngine();
    auto ally = boardObject(P1, kOtterpus, 0);
    s().getObject(ally).buff_count = 1;
    s().getObject(ally).recomputeMight();
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto spell = cardInHand(P1, kOneTwoPunch);
    s().player(P1).rune_pool.energy = 3;
    p1_.picks = {ally, enemy};
    play(spell);
    EXPECT_EQ(s().getObject(ally).buff_count, 1);
    EXPECT_TRUE(s().getObject(enemy).is_stunned);
}

// ─── RAD-122 Party Crasher ──────────────────────────────────────────────────

TEST_F(RadianceB08Test, PartyCrasher_CostsOneLessPerExhaustedUnitYouControl) {
    makeEngine();
    auto a = boardObject(P1, kOtterpus);
    auto b = boardObject(P1, kOtterpus);
    boardObject(P1, kOtterpus);                 // ready: no discount
    auto enemy = boardObject(P2, kOtterpus);
    s().getObject(a).is_exhausted = true;
    s().getObject(b).is_exhausted = true;
    s().getObject(enemy).is_exhausted = true;   // not yours: no discount
    auto crasher = cardInHand(P1, kPartyCrasher);
    power(P1, Domain::Order, 1);
    s().player(P1).rune_pool.energy = 6;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, crasher));
    s().player(P1).rune_pool.energy = 7;
    EXPECT_TRUE(engine_->testHook_canAfford(P1, crasher));
    play(crasher, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(onBoard(crasher));
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
    EXPECT_TRUE(card_db.get(kPartyCrasher).keywords.has(Keyword::Tank));
}

// ─── RAD-125 Dune Devourer ──────────────────────────────────────────────────

TEST_F(RadianceB08Test, DuneDevourer_KillsAFriendlyUnitToCostTwoLess) {
    makeEngine();
    auto otter = boardObject(P1, kOtterpus);
    auto dd = cardInHand(P1, kDuneDevourer);
    power(P1, Domain::Order, 1);
    s().player(P1).rune_pool.energy = 5;
    EXPECT_TRUE(engine_->testHook_canAfford(P1, dd));
    p1_.want = otter;
    play(dd, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(onBoard(dd));
    EXPECT_FALSE(onBoard(otter)) << "killed as the additional cost";
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), dd)) << "[2] not payable: must kill";
}

TEST_F(RadianceB08Test, DuneDevourer_MayPayFullCostInstead) {
    makeEngine();
    auto otter = boardObject(P1, kOtterpus);
    auto dd = cardInHand(P1, kDuneDevourer);
    power(P1, Domain::Order, 1);
    s().player(P1).rune_pool.energy = 7;
    p1_.want = dd;                               // "me" = pay [2] instead
    play(dd, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(onBoard(dd));
    EXPECT_TRUE(onBoard(otter));
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0) << "paid the full 7";
}

TEST_F(RadianceB08Test, DuneDevourer_NoDiscountWithoutAFriendlyUnit) {
    makeEngine();
    auto dd = cardInHand(P1, kDuneDevourer);
    power(P1, Domain::Order, 1);
    s().player(P1).rune_pool.energy = 5;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, dd));
    EXPECT_TRUE(card_db.get(kDuneDevourer).keywords.has(Keyword::Deflect));
}

// ─── RAD-126 Mournful Spirit ────────────────────────────────────────────────

TEST_F(RadianceB08Test, MournfulSpirit_DeathknellMayKillAUnitHere) {
    makeEngine();
    auto spirit = boardObject(P1, kMournfulSpirit, 0);
    auto here = boardObject(P2, kOtterpus, 0);
    auto elsewhere = boardObject(P2, kOtterpus, 1);
    p1_.answers = {1};
    p1_.want = here;
    engine_->testHook_executor().killObject(spirit);
    engine_->testHook_runChain();
    EXPECT_FALSE(onBoard(here));
    EXPECT_TRUE(onBoard(elsewhere));
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), elsewhere));
}

TEST_F(RadianceB08Test, MournfulSpirit_CanKillAGearAtBaseOrDecline) {
    makeEngine();
    auto spirit = boardObject(P1, kMournfulSpirit);
    auto gear = boardObject(P1, kSealOfRage);
    p1_.answers = {1};
    p1_.want = gear;
    engine_->testHook_executor().killObject(spirit);
    engine_->testHook_runChain();
    EXPECT_FALSE(onBoard(gear));

    auto spirit2 = boardObject(P1, kMournfulSpirit);
    auto otter = boardObject(P1, kOtterpus);
    p1_.answers = {0};
    p1_.next = 0;
    p1_.want = otter;
    engine_->testHook_executor().killObject(spirit2);
    engine_->testHook_runChain();
    EXPECT_TRUE(onBoard(otter)) << "declined";
}

// ─── RAD-127 Rousing Display ────────────────────────────────────────────────

TEST_F(RadianceB08Test, RousingDisplay_TwoRecruitsWithoutAShowOff) {
    makeEngine();
    auto spell = cardInHand(P1, kRousingDisplay);
    cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 5;
    play(spell);
    EXPECT_EQ(unitsNamed(P1, "Recruit"), 2);
}

TEST_F(RadianceB08Test, RousingDisplay_FourRecruitsWhenShowingOffASevenCostCard) {
    makeEngine();
    auto spell = cardInHand(P1, kRousingDisplay);
    cardInHand(P1, kMegaMech);                  // Energy cost 7
    s().player(P1).rune_pool.energy = 5;
    play(spell);
    EXPECT_EQ(unitsNamed(P1, "Recruit"), 4);
    for (const auto& [id, o] : s().objects)
        if (o.name == "Recruit") EXPECT_TRUE(o.isAtBase());
}

TEST_F(RadianceB08Test, RousingDisplay_AFriendlySevenCostUnitOnTheBoardAlsoCounts) {
    makeEngine();
    auto spell = cardInHand(P1, kRousingDisplay);
    boardObject(P1, kMegaMech, 0);
    s().player(P1).rune_pool.energy = 5;
    play(spell);
    EXPECT_EQ(unitsNamed(P1, "Recruit"), 4);
}

}  // namespace
}  // namespace riftbound::test
