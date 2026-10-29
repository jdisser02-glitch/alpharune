/// @file test_radiance_b03.cpp
/// Radiance batch 03 (Mind / domainless commons and uncommons, RAD-047..058):
/// Fanfare, High Note, Industrious Mechsmith, Mysterious Chronomancer,
/// Parallel Convergence, Resource Extractor, Clanging Sentry, Improvised
/// Projectile, Piltovan Guidebot, Riptide Mage.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kFanfare        = 1015;
constexpr CardDefId kHighNote       = 1016;
constexpr CardDefId kMechsmith      = 1017;
constexpr CardDefId kChronomancer   = 1018;   // 5-energy 4 [M] Mind unit
constexpr CardDefId kConvergence    = 1019;
constexpr CardDefId kExtractor      = 1020;
constexpr CardDefId kSentry         = 1021;
constexpr CardDefId kProjectile     = 1022;
constexpr CardDefId kGuidebot       = 1023;
constexpr CardDefId kRiptideMage    = 1024;
constexpr CardDefId kOtterpus       = 840;    // 2-energy 2 [M] unit, no play or attack text
constexpr CardDefId kBFSword        = 482;    // 4-energy gear

/// Answers int prompts (yes/no, modes) from a queue, picks objects from a
/// queue of wanted ids (only consumed when the wanted id is offered), then
/// `want`, else the first option; passes priority otherwise. Records every
/// int prompt it was shown.
class B03Agent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    std::vector<GameObjectId> wants;
    size_t wnext = 0;
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
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class RadianceB03Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B03Agent p1_, p2_;
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
    GameObjectId deckCard(PlayerId p) {
        auto id = s().createObject();
        auto& c = s().getObject(id);
        c.owner = c.controller = p;
        c.zone = ZoneType::MainDeck;
        c.name = "Deck card";
        s().player(p).main_deck.push_back(id);   // back = top
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
    void pool(PlayerId p, int energy, int mind_power = 0) {
        auto& rp = s().player(p).rune_pool;
        rp.energy = energy;
        rp.power[static_cast<int>(Domain::Mind)] = mind_power;
    }
    void play(GameObjectId card, std::optional<LocationId> where) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
    }
    bool offersPlayAt(GameObjectId card, LocationId where) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::PlayCard && a.card == card &&
                a.play_location == std::optional<LocationId>{where})
                return true;
        return false;
    }
    bool offersPlay(GameObjectId card) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.card == card &&
                (a.type == IntentType::PlayCard || a.type == IntentType::PlayActionCard ||
                 a.type == IntentType::PlayReaction))
                return true;
        return false;
    }
    void controlBf(BattlefieldId bf, PlayerId p) { s().battlefields[bf].controller = p; }
    int countGold(PlayerId p, bool* all_exhausted = nullptr) {
        int n = 0;
        if (all_exhausted) *all_exhausted = true;
        for (auto& [id, o] : s().objects)
            if (o.isGear() && o.controller == p && o.name == "Gold" && o.location.has_value()) {
                ++n;
                if (all_exhausted && !o.is_exhausted) *all_exhausted = false;
            }
        return n;
    }
    GameObjectId mechToken(PlayerId p, BattlefieldId bf) {
        return engine_->testHook_executor().createToken(
            p, CardType::Unit, "Mech", 3, {"Mech"}, KeywordSet{},
            LocationId{BattlefieldLocation{bf}}, /*enter_ready=*/true);
    }
};

// ─── RAD-047 Fanfare ────────────────────────────────────────────────────────

TEST_F(RadianceB03Test, Fanfare_TheNextThreeUnitsPlayedThisTurnEachDraw) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    for (int i = 0; i < 6; ++i) deckCard(P1);
    pool(P1, 3);
    auto fanfare = cardInHand(P1, kFanfare);
    play(fanfare, std::nullopt);
    EXPECT_EQ(s().getObject(fanfare).zone, ZoneType::Trash);
    EXPECT_TRUE(s().player(P1).hand.empty()) << "Fanfare itself draws nothing";

    for (int i = 0; i < 4; ++i) {
        pool(P1, 2);
        auto unit = cardInHand(P1, kOtterpus);
        play(unit, LocationId{BaseLocation{P1}});
        ASSERT_TRUE(s().getObject(unit).isAtBase());
        EXPECT_EQ(s().player(P1).hand.size(), static_cast<size_t>(std::min(i + 1, 3)))
            << "after unit #" << (i + 1);
    }
    EXPECT_EQ(s().player(P1).main_deck.size(), 3u) << "exactly three draws";
}

TEST_F(RadianceB03Test, Fanfare_EndsWithTheTurnAndIgnoresNonUnits) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    for (int i = 0; i < 4; ++i) deckCard(P1);
    pool(P1, 3);
    play(cardInHand(P1, kFanfare), std::nullopt);

    // A gear is not a unit: no draw.
    pool(P1, 4);
    auto sword = cardInHand(P1, kBFSword);
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 1;
    play(sword, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().player(P1).hand.empty());

    // Next turn the effect is over.
    s().turn.turn_number += 1;
    pool(P1, 2);
    auto unit = cardInHand(P1, kOtterpus);
    play(unit, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(unit).isAtBase());
    EXPECT_TRUE(s().player(P1).hand.empty()) << "\"this turn\" only";
    EXPECT_EQ(s().player(P1).main_deck.size(), 4u);
}

// ─── RAD-048 High Note ──────────────────────────────────────────────────────

TEST_F(RadianceB03Test, HighNote_MinusOneMightPerExhaustedFriendlyUnit) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    s().getObject(boardObject(P1, kOtterpus)).is_exhausted = true;
    s().getObject(boardObject(P1, kOtterpus)).is_exhausted = true;
    boardObject(P1, kOtterpus);                        // ready: doesn't count
    s().getObject(boardObject(P2, kOtterpus)).is_exhausted = true;   // not mine
    auto enemy = boardObject(P2, kChronomancer, 0);
    pool(P1, 3);
    auto note = cardInHand(P1, kHighNote);
    p1_.want = enemy;
    play(note, std::nullopt);
    EXPECT_EQ(s().getObject(enemy).current_might, 2) << "4 - 2 exhausted units I control";
    EXPECT_EQ(s().getObject(enemy).temp_might_bonus, -2);
}

TEST_F(RadianceB03Test, HighNote_HasReaction) {
    makeEngine();
    EXPECT_TRUE(card_db.get(kHighNote).keywords.has(Keyword::Reaction));
}

// ─── RAD-049 Industrious Mechsmith ──────────────────────────────────────────

TEST_F(RadianceB03Test, Mechsmith_PlaysAnExhaustedThreeMightMechHere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    pool(P1, 4);
    auto smith = cardInHand(P1, kMechsmith);
    play(smith, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(smith).isAtBase());
    int mechs = 0;
    for (auto& [id, o] : s().objects) {
        if (!o.isUnit() || o.controller != P1 || o.name != "Mech") continue;
        ++mechs;
        EXPECT_TRUE(o.isToken());
        EXPECT_TRUE(hasTag(o, "Mech"));
        EXPECT_EQ(o.current_might, 3);
        EXPECT_TRUE(o.isAtBase()) << "here = where the Mechsmith is";
        EXPECT_TRUE(o.is_exhausted) << "not a ready token";
    }
    EXPECT_EQ(mechs, 1);
}

// ─── RAD-050 Mysterious Chronomancer ────────────────────────────────────────

TEST_F(RadianceB03Test, Chronomancer_PlayPredictsTwo) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto bottom = deckCard(P1);
    auto second = deckCard(P1);
    auto top = deckCard(P1);
    pool(P1, 5);
    auto chrono = cardInHand(P1, kChronomancer);
    // The agent's first option for each looked-at card is "recycle".
    play(chrono, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(chrono).isAtBase());
    const auto& deck = s().player(P1).main_deck;
    ASSERT_EQ(deck.size(), 3u);
    EXPECT_EQ(deck.back(), bottom) << "both looked-at cards were recycled to the bottom";
    EXPECT_NE(std::find(deck.begin(), deck.end(), second), deck.end());
    EXPECT_NE(std::find(deck.begin(), deck.end(), top), deck.end());
}

TEST_F(RadianceB03Test, Chronomancer_Disarm2WhenIAttack) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kChronomancer, 0);
    auto enemy = boardObject(P2, kChronomancer, 0);
    auto elsewhere = boardObject(P2, kChronomancer, 1);
    p1_.want = enemy;
    events.emit(CombatStartedEvent{0, /*attacker=*/P1, /*defender=*/P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy).current_might, 2) << "4 - 2 this turn";
    EXPECT_EQ(s().getObject(elsewhere).current_might, 4) << "only an enemy unit here";
}

TEST_F(RadianceB03Test, Chronomancer_NoDisarmWhenDefending) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto mine = boardObject(P1, kChronomancer, 0);
    auto attacker = boardObject(P2, kChronomancer, 0);
    events.emit(CombatStartedEvent{0, /*attacker=*/P2, /*defender=*/P1});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(attacker).temp_might_bonus, 0) << "my Disarm is an attack trigger";
    EXPECT_EQ(s().getObject(mine).temp_might_bonus, -2) << "P2's attacking Chronomancer disarmed me";
}

// ─── RAD-051 Parallel Convergence ───────────────────────────────────────────

TEST_F(RadianceB03Test, ParallelConvergence_ReturnAFriendlyUnitToDealFive) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto payer = boardObject(P1, kOtterpus);
    auto keeper = boardObject(P1, kOtterpus, 1);
    auto enemy = boardObject(P2, kChronomancer, 0);
    pool(P1, 2, 1);
    auto spell = cardInHand(P1, kConvergence);
    EXPECT_TRUE(offersPlay(spell));
    p1_.wants = {enemy, payer};
    play(spell, std::nullopt);
    EXPECT_EQ(s().getObject(payer).zone, ZoneType::Hand) << "the additional cost";
    EXPECT_EQ(std::count(s().player(P1).hand.begin(), s().player(P1).hand.end(), payer), 1);
    EXPECT_TRUE(s().getObject(keeper).isAtBattlefield());
    EXPECT_FALSE(s().getObject(enemy).location.has_value()) << "5 damage kills a 4 [M] unit";
}

TEST_F(RadianceB03Test, ParallelConvergence_NeedsAFriendlyUnitAndAUnitAtABattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    const Card* card = card_registry.get(kConvergence);
    ASSERT_NE(card, nullptr);
    boardObject(P2, kChronomancer, 0);
    EXPECT_FALSE(card->hasLegalTargets(s(), P1)) << "no friendly unit to return";
    boardObject(P1, kOtterpus);
    EXPECT_TRUE(card->hasLegalTargets(s(), P1));
}

TEST_F(RadianceB03Test, ParallelConvergence_NotPlayableWithoutAUnitAtABattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    const Card* card = card_registry.get(kConvergence);
    ASSERT_NE(card, nullptr);
    boardObject(P1, kOtterpus);
    boardObject(P2, kOtterpus);   // at base: not a legal target
    EXPECT_FALSE(card->hasLegalTargets(s(), P1)) << "no unit at a battlefield";
    pool(P1, 2, 1);
    EXPECT_FALSE(offersPlay(cardInHand(P1, kConvergence)));
}

// ─── RAD-052 Resource Extractor ─────────────────────────────────────────────

TEST_F(RadianceB03Test, ResourceExtractor_DeploysOnlyToABattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    rune(P1, Domain::Mind);
    auto ex = cardInHand(P1, kExtractor);
    EXPECT_FALSE(offersPlayAt(ex, LocationId{BaseLocation{P1}})) << "[Deploy]";
    EXPECT_TRUE(offersPlayAt(ex, LocationId{BattlefieldLocation{1}}));
    EXPECT_TRUE(card_registry.get(kExtractor)->deploys());
}

TEST_F(RadianceB03Test, ResourceExtractor_GoldAtEndOfTurnIfIControlTheBattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kExtractor, 0);
    controlBf(0, P1);
    engine_->testHook_endingStep();
    bool exhausted = false;
    EXPECT_EQ(countGold(P1, &exhausted), 1);
    EXPECT_TRUE(exhausted);
}

TEST_F(RadianceB03Test, ResourceExtractor_NoGoldWithoutControlOrOnTheOpponentsTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kExtractor, 0);
    controlBf(0, P2);
    engine_->testHook_endingStep();
    EXPECT_EQ(countGold(P1), 0) << "the opponent controls this battlefield";

    controlBf(0, P1);
    s().turn.turn_player = P2;
    engine_->testHook_endingStep();
    EXPECT_EQ(countGold(P1), 0) << "only at the end of MY turn";
}

// ─── RAD-055 Clanging Sentry ────────────────────────────────────────────────

TEST_F(RadianceB03Test, ClangingSentry_ALoneAttackingMechDisarms) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kSentry);
    auto mech = mechToken(P1, 0);
    auto enemy = boardObject(P2, kChronomancer, 0);
    s().getObject(mech).combat_designation = CombatDesignation::Attacker;
    s().getObject(enemy).combat_designation = CombatDesignation::Defender;
    p1_.want = enemy;
    events.emit(CombatStartedEvent{0, /*attacker=*/P1, /*defender=*/P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy).current_might, 3) << "[Disarm] = -1 this turn";
}

TEST_F(RadianceB03Test, ClangingSentry_TwoSentriesGiveDisarm2Once) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kSentry);
    boardObject(P1, kSentry);
    auto mech = mechToken(P1, 0);
    auto enemy = boardObject(P2, kChronomancer, 0);
    s().getObject(mech).combat_designation = CombatDesignation::Attacker;
    s().getObject(enemy).combat_designation = CombatDesignation::Defender;
    p1_.want = enemy;
    events.emit(CombatStartedEvent{0, /*attacker=*/P1, /*defender=*/P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy).temp_might_bonus, -2) << "CR 831.2: values summed, one trigger";
}

TEST_F(RadianceB03Test, ClangingSentry_NonMechAttackerGetsNothing) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kSentry);
    auto attacker = boardObject(P1, kOtterpus, 0);
    auto enemy = boardObject(P2, kChronomancer, 0);
    s().getObject(attacker).combat_designation = CombatDesignation::Attacker;
    s().getObject(enemy).combat_designation = CombatDesignation::Defender;
    p1_.want = enemy;
    events.emit(CombatStartedEvent{0, /*attacker=*/P1, /*defender=*/P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy).temp_might_bonus, 0);
}

// ─── RAD-056 Improvised Projectile ──────────────────────────────────────────

TEST_F(RadianceB03Test, ImprovisedProjectile_DealsTheGearsEnergyCost) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto sword = boardObject(P1, kBFSword);   // costs [4]
    auto enemy = boardObject(P2, kChronomancer, 0);
    auto other = boardObject(P2, kChronomancer, 1);
    pool(P1, 3, 1);
    auto spell = cardInHand(P1, kProjectile);
    EXPECT_TRUE(offersPlay(spell));
    p1_.wants = {sword, enemy};
    play(spell, std::nullopt);
    EXPECT_FALSE(s().getObject(enemy).location.has_value()) << "4 damage kills a 4 [M] unit";
    EXPECT_EQ(s().getObject(other).damage_marked, 0);
    EXPECT_TRUE(s().getObject(sword).location.has_value()) << "the gear is only chosen";
}

TEST_F(RadianceB03Test, ImprovisedProjectile_NeedsAFriendlyGear) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P2, kChronomancer, 0);
    boardObject(P2, kBFSword);   // an enemy gear doesn't count
    const Card* card = card_registry.get(kProjectile);
    ASSERT_NE(card, nullptr);
    EXPECT_FALSE(card->hasLegalTargets(s(), P1));
    boardObject(P1, kBFSword);
    EXPECT_TRUE(card->hasLegalTargets(s(), P1));
    EXPECT_TRUE(card_db.get(kProjectile).keywords.has(Keyword::Action));
}

// ─── RAD-057 Piltovan Guidebot ──────────────────────────────────────────────

TEST_F(RadianceB03Test, Guidebot_BaseMightBecomesThePlayedCardsEnergyCost) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto bot = boardObject(P1, kGuidebot);
    ASSERT_EQ(s().getObject(bot).current_might, 3);

    p1_.answers = {1};   // yes
    pool(P1, 2);
    play(cardInHand(P1, kOtterpus), LocationId{BaseLocation{P1}});
    EXPECT_EQ(s().getObject(bot).current_might, 2) << "Otterpus costs [2]";

    p1_.answers = {1, 1, 1};   // yes again (Chronomancer's Predict prompt also asks): replaces, doesn't stack
    pool(P1, 5);
    play(cardInHand(P1, kChronomancer), LocationId{BaseLocation{P1}});
    EXPECT_EQ(s().getObject(bot).current_might, 5) << "Chronomancer costs [5]";

    p1_.answers = {0};   // decline
    pool(P1, 2);
    play(cardInHand(P1, kOtterpus), LocationId{BaseLocation{P1}});
    EXPECT_EQ(s().getObject(bot).current_might, 5);
    EXPECT_FALSE(p1_.int_prompts.empty());
}

TEST_F(RadianceB03Test, Guidebot_OtherModifiersStackOnTheNewBase) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto bot = boardObject(P1, kGuidebot);
    s().getObject(bot).buff_count = 1;
    s().getObject(bot).recomputeMight();
    ASSERT_EQ(s().getObject(bot).current_might, 4);
    p1_.answers = {1};
    pool(P1, 5);
    play(cardInHand(P1, kChronomancer), LocationId{BaseLocation{P1}});
    EXPECT_EQ(s().getObject(bot).current_might, 6) << "base 5 + buff 1";
}

// ─── RAD-058 Riptide Mage ───────────────────────────────────────────────────

TEST_F(RadianceB03Test, RiptideMage_PlayGivesAnEnemyUnitMinusThree) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto enemy = boardObject(P2, kChronomancer, 0);
    auto ally = boardObject(P1, kOtterpus);
    pool(P1, 4, 1);   // exactly the cost: no Accelerate
    auto mage = cardInHand(P1, kRiptideMage);
    p1_.want = enemy;
    play(mage, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(mage).isAtBase());
    EXPECT_EQ(s().getObject(enemy).current_might, 1) << "4 - 3 this turn";
    EXPECT_EQ(s().getObject(ally).temp_might_bonus, 0);
}

TEST_F(RadianceB03Test, RiptideMage_HasAccelerate) {
    makeEngine();
    EXPECT_TRUE(card_db.get(kRiptideMage).keywords.has(Keyword::Accelerate));
    EXPECT_EQ(card_db.get(kRiptideMage).power_cost, 1);
}

}  // namespace
}  // namespace riftbound::test
