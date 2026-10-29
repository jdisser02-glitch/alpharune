/// @file test_radiance_b04.cpp
/// Radiance batch 04: Supernova, The Empyrean ([Show Off] -> Predict + draw),
/// Ekko, Ingenious (return a friendly unit as an additional cost), Gravity
/// Field ([Deploy], move toll, Deathknell), Kai'Sa, Rebel ([Disarm], spells
/// have [Reaction] in a showdown), Scrutinizing Scope (opponent guesses a
/// type), Clocktower Guardian (Predict 3; Deathknell banish + play),
/// Orianna, Mechanical Marvel (once-per-turn gear discount), Zero Point
/// Inflection (draw 2 per card; overdraw penalty) and Hold 'Em.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kSupernova     = 1025;
constexpr CardDefId kEmpyrean      = 1026;
constexpr CardDefId kEkko          = 1027;
constexpr CardDefId kGravityField  = 1028;
constexpr CardDefId kKaiSaRebel    = 1029;
constexpr CardDefId kScope         = 1030;
constexpr CardDefId kClocktower    = 1031;
constexpr CardDefId kOrianna       = 1032;
constexpr CardDefId kZeroPoint     = 1033;
constexpr CardDefId kHoldEm        = 1034;
constexpr CardDefId kBlade         = 962;  // gear, Energy 3 / Power 1 (Calm, Chaos)
constexpr CardDefId kBattlePreps   = 968;  // spell with printed [Reaction]
constexpr CardDefId kOnTheHunt     = 518;  // spell, Energy 1 / Power 2
constexpr CardDefId kScorcher      = 1;    // unit, Energy 5
constexpr CardDefId kOtterpus      = 840;  // 2-energy 2-Might unit, no play text

/// Answers int prompts (yes/no, modes, guesses) from `answers`, object prompts
/// from `picks` (a pick is consumed only when it is offered), passes priority
/// otherwise. Records every prompt it was shown.
class B04Agent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    std::vector<GameObjectId> picks;
    size_t pnext = 0;
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

class RadianceB04Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B04Agent p1_, p2_;
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
    GameObjectId setLegend(PlayerId p) {
        auto id = s().createObject();
        auto& lo = s().getObject(id);
        lo.owner = lo.controller = p;
        lo.card_type = CardType::Legend;
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
    /// Puts a card on TOP of `p`'s Main Deck (back = top).
    GameObjectId deckCard(PlayerId p, CardDefId def_id = kScorcher) {
        auto id = s().createObject();
        auto& c = s().getObject(id);
        stamp(c, p, def_id);
        c.zone = ZoneType::MainDeck;
        s().player(p).main_deck.push_back(id);
        return id;
    }
    void play(GameObjectId card, std::optional<LocationId> where = std::nullopt) {
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
            if ((a.type == IntentType::PlayCard || a.type == IntentType::PlayActionCard ||
                 a.type == IntentType::PlayReaction) && a.card == card)
                return true;
        return false;
    }
    void controlBf(BattlefieldId bf, PlayerId p) { s().battlefields[bf].controller = p; }
    static bool contains(const std::vector<GameObjectId>& v, GameObjectId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
};

// ─── RAD-059 Supernova ──────────────────────────────────────────────────────

TEST_F(RadianceB04Test, Supernova_DealsTenToAUnitAndDrawsTwo) {
    makeEngine();
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto d1 = deckCard(P1);
    auto d2 = deckCard(P1);
    auto spell = cardInHand(P1, kSupernova);
    s().player(P1).rune_pool.energy = 9;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 1;
    p1_.picks = {enemy};

    play(spell);

    EXPECT_FALSE(s().getObject(enemy).location.has_value()) << "10 damage killed it";
    EXPECT_TRUE(contains(s().player(P1).hand, d1));
    EXPECT_TRUE(contains(s().player(P1).hand, d2));
}

// ─── RAD-060 The Empyrean ───────────────────────────────────────────────────

TEST_F(RadianceB04Test, Empyrean_ShowsOffAndPredictsItsPowerCostThenDrawsTwo) {
    makeEngine();
    ASSERT_EQ(card_db.get(kOnTheHunt).power_cost, 2);
    std::vector<GameObjectId> deck;
    for (int i = 0; i < 5; ++i) deck.push_back(deckCard(P1));   // deck[4] is the top
    cardInHand(P1, kOnTheHunt);                                  // Power cost 2 to show off
    auto emp = cardInHand(P1, kEmpyrean);
    s().player(P1).rune_pool.energy = 8;

    play(emp, LocationId{BaseLocation{P1}});

    ASSERT_TRUE(s().getObject(emp).isAtBase());
    EXPECT_EQ(p1_.obj_prompts.size(), 2u) << "Predict 2: one keep/recycle prompt per card";
    // The agent recycles both looked-at cards, then draws the next two.
    const auto& hand = s().player(P1).hand;
    EXPECT_FALSE(contains(hand, deck[4]));
    EXPECT_FALSE(contains(hand, deck[3]));
    EXPECT_TRUE(contains(hand, deck[2]));
    EXPECT_TRUE(contains(hand, deck[1]));
}

TEST_F(RadianceB04Test, Empyrean_NothingToShowOffNoEffect) {
    makeEngine();
    auto d = deckCard(P1);
    auto emp = cardInHand(P1, kEmpyrean);
    s().player(P1).rune_pool.energy = 8;
    play(emp, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(s().player(P1).hand, d)) << "no show off: no draw";
}

TEST_F(RadianceB04Test, Empyrean_ShowingOffAPowerlessCardStillDrawsTwo) {
    makeEngine();
    auto d1 = deckCard(P1);
    auto d2 = deckCard(P1);
    boardObject(P1, kOtterpus);   // a friendly card with Power cost 0
    auto emp = cardInHand(P1, kEmpyrean);
    s().player(P1).rune_pool.energy = 8;
    play(emp, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(p1_.obj_prompts.empty()) << "Predict 0";
    EXPECT_TRUE(contains(s().player(P1).hand, d1));
    EXPECT_TRUE(contains(s().player(P1).hand, d2));
}

// ─── RAD-061 Ekko, Ingenious ────────────────────────────────────────────────

TEST_F(RadianceB04Test, Ekko_ReturnsAFriendlyUnitAsTheAdditionalCost) {
    makeEngine();
    auto ally = boardObject(P1, kOtterpus, 0);
    auto other = boardObject(P1, kOtterpus);
    auto ekko = cardInHand(P1, kEkko);
    s().player(P1).rune_pool.energy = 3;
    EXPECT_TRUE(card_db.get(kEkko).keywords.has(Keyword::Ganking));
    EXPECT_TRUE(offersPlayAt(ekko, LocationId{BaseLocation{P1}}));
    p1_.picks = {ally};

    play(ekko, LocationId{BaseLocation{P1}});

    EXPECT_TRUE(s().getObject(ekko).isAtBase());
    EXPECT_EQ(s().getObject(ally).zone, ZoneType::Hand);
    EXPECT_TRUE(contains(s().player(P1).hand, ally));
    EXPECT_TRUE(s().getObject(other).location.has_value());
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), ekko)) << "can't return himself";
}

TEST_F(RadianceB04Test, Ekko_NotPlayableWithoutAFriendlyUnit) {
    makeEngine();
    boardObject(P2, kOtterpus, 0);
    auto ekko = cardInHand(P1, kEkko);
    s().player(P1).rune_pool.energy = 3;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, ekko));
    EXPECT_FALSE(offersPlay(ekko));
}

// ─── RAD-062 Gravity Field ──────────────────────────────────────────────────

TEST_F(RadianceB04Test, GravityField_DeploysOnlyToABattlefield) {
    makeEngine();
    auto gf = cardInHand(P1, kGravityField);
    s().player(P1).rune_pool.energy = 5;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Calm)] = 1;
    EXPECT_FALSE(offersPlayAt(gf, LocationId{BaseLocation{P1}}));
    EXPECT_TRUE(offersPlayAt(gf, LocationId{BattlefieldLocation{1}}));
}

TEST_F(RadianceB04Test, GravityField_OpponentPaysTheTollToMoveHere) {
    makeEngine();
    boardObject(P1, kGravityField, 0);
    auto mover = boardObject(P2, kOtterpus);
    s().player(P2).rune_pool.energy = 1;
    s().player(P2).rune_pool.universal_power = 1;
    engine_->testHook_executeIntent(Intent::standardMove(P2, {mover}, BattlefieldLocation{0}));
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(mover).isAtBattlefield());
    EXPECT_EQ(s().player(P2).rune_pool.energy, 0) << "paid [1]";
    EXPECT_EQ(s().player(P2).rune_pool.universal_power, 0) << "paid [A]";
}

TEST_F(RadianceB04Test, GravityField_UnpaidMoveIsUndone) {
    makeEngine();
    boardObject(P1, kGravityField, 0);
    auto mover = boardObject(P2, kOtterpus);
    engine_->testHook_executeIntent(Intent::standardMove(P2, {mover}, BattlefieldLocation{0}));
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(mover).isAtBase()) << "couldn't pay [1][A]";
}

TEST_F(RadianceB04Test, GravityField_NoTollElsewhereOrForItsController) {
    makeEngine();
    boardObject(P1, kGravityField, 0);
    auto enemy = boardObject(P2, kOtterpus);
    auto mine = boardObject(P1, kOtterpus);
    engine_->testHook_executeIntent(Intent::standardMove(P2, {enemy}, BattlefieldLocation{1}));
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(enemy).isAtBattlefield()) << "other battlefield: no toll";
    engine_->testHook_executeIntent(Intent::standardMove(P1, {mine}, BattlefieldLocation{0}));
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(mine).isAtBattlefield()) << "only opponents pay";
}

TEST_F(RadianceB04Test, GravityField_DeathknellDrawsOne) {
    makeEngine();
    auto gf = boardObject(P1, kGravityField, 0);
    auto top = deckCard(P1);
    Card* c = card_registry.get(kGravityField);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->firesOn(TriggerType::WhenIDie));
    EXPECT_TRUE(c->deploys());
    CardContext ctx{s(), events, engine_->testHook_executor(), P1, gf};
    ctx.firing_trigger = TriggerType::WhenIDie;
    c->onTrigger(ctx, {});
    EXPECT_TRUE(contains(s().player(P1).hand, top));
}

// ─── RAD-063 Kai'Sa, Rebel ──────────────────────────────────────────────────

TEST_F(RadianceB04Test, KaiSa_DisarmGivesAnEnemyUnitHereMinusOne) {
    makeEngine();
    auto kaisa = boardObject(P1, kKaiSaRebel, 0);
    auto here = boardObject(P2, kOtterpus, 0);
    auto there = boardObject(P2, kOtterpus, 1);
    p1_.picks = {here};
    engine_->testHook_executor().queueTriggeredAbility(kaisa, P1, TriggerType::WhenIAttack);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(here).current_might, 1) << "2 - 1 this turn";
    EXPECT_EQ(s().getObject(there).current_might, 2);
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), there)) << "only enemy units here";
}

TEST_F(RadianceB04Test, KaiSa_SpellsHaveReactionWhileSheIsInAShowdown) {
    makeEngine();
    boardObject(P1, kKaiSaRebel, 0);
    auto spell = cardInHand(P1, kSupernova);
    auto printed = cardInHand(P1, kBattlePreps);
    auto enemy_spell = cardInHand(P2, kSupernova);
    ASSERT_FALSE(s().getObject(spell).keywords.has(Keyword::Reaction));

    s().battlefields[0].showdown_in_progress = true;
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(spell).keywords.has(Keyword::Reaction));
    EXPECT_FALSE(s().getObject(enemy_spell).keywords.has(Keyword::Reaction)) << "your spells";

    s().battlefields[0].showdown_in_progress = false;
    engine_->testHook_cleanup();
    EXPECT_FALSE(s().getObject(spell).keywords.has(Keyword::Reaction)) << "the showdown ended";
    EXPECT_TRUE(s().getObject(printed).keywords.has(Keyword::Reaction)) << "printed Reaction kept";
}

TEST_F(RadianceB04Test, KaiSa_NoReactionWhenHerBattlefieldHasNoShowdown) {
    makeEngine();
    boardObject(P1, kKaiSaRebel, 0);
    auto spell = cardInHand(P1, kSupernova);
    s().battlefields[1].showdown_in_progress = true;   // a showdown elsewhere
    engine_->testHook_cleanup();
    EXPECT_FALSE(s().getObject(spell).keywords.has(Keyword::Reaction));
}

// ─── RAD-064 Scrutinizing Scope ─────────────────────────────────────────────

TEST_F(RadianceB04Test, Scope_RightGuessRecyclesTheTopCard) {
    makeEngine();
    auto scope = boardObject(P1, kScope);
    deckCard(P1);
    auto top = deckCard(P1, kOtterpus);   // a unit
    s().player(P1).rune_pool.energy = 1;
    p2_.answers = {0};                    // "unit"
    Intent act;
    act.type = IntentType::ActivateAbility;
    act.player = P1;
    act.ability_source = scope;
    engine_->testHook_executeIntent(act);
    EXPECT_TRUE(s().getObject(scope).is_exhausted);
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
    ASSERT_EQ(p2_.int_prompts.size(), 1u) << "the opponent guesses";
    EXPECT_EQ(p2_.int_prompts[0], (std::vector<int>{0, 1, 2}));
    EXPECT_FALSE(contains(s().player(P1).hand, top));
    EXPECT_EQ(s().player(P1).main_deck.front(), top) << "recycled to the bottom";
}

TEST_F(RadianceB04Test, Scope_WrongGuessDrawsTheTopCard) {
    makeEngine();
    auto scope = boardObject(P1, kScope);
    auto top = deckCard(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 1;
    p2_.answers = {2};                    // "spell"
    Intent act;
    act.type = IntentType::ActivateAbility;
    act.player = P1;
    act.ability_source = scope;
    engine_->testHook_executeIntent(act);
    EXPECT_TRUE(contains(s().player(P1).hand, top));
}

// ─── RAD-067 Clocktower Guardian ────────────────────────────────────────────

TEST_F(RadianceB04Test, Clocktower_PlayPredictsThree) {
    makeEngine();
    for (int i = 0; i < 4; ++i) deckCard(P1);
    auto cg = cardInHand(P1, kClocktower);
    s().player(P1).rune_pool.energy = 8;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Fury)] = 2;
    play(cg, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(cg).isAtBase());
    EXPECT_EQ(p1_.obj_prompts.size(), 3u) << "Predict 3";
}

TEST_F(RadianceB04Test, Clocktower_DeathknellBanishesTheTopCardAndMayPlayIt) {
    makeEngine();
    auto cg = boardObject(P1, kClocktower);
    auto top = deckCard(P1, kOtterpus);
    p1_.answers = {1};   // play it
    engine_->testHook_executor().killObject(cg);
    engine_->testHook_runChain();
    ASSERT_EQ(p1_.int_prompts.size(), 1u);
    EXPECT_TRUE(s().getObject(top).isAtBase()) << "played ignoring its Energy cost";
    EXPECT_FALSE(contains(s().player(P1).banishment, top));
}

TEST_F(RadianceB04Test, Clocktower_DeathknellDeclinedLeavesItBanished) {
    makeEngine();
    auto cg = boardObject(P1, kClocktower);
    auto top = deckCard(P1, kOtterpus);
    p1_.answers = {0};
    engine_->testHook_executor().killObject(cg);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(top).zone, ZoneType::Banishment);
    EXPECT_TRUE(contains(s().player(P1).banishment, top));
    EXPECT_TRUE(s().player(P1).main_deck.empty());
}

TEST_F(RadianceB04Test, Clocktower_DeathknellCantPlayWhatItCantPay) {
    makeEngine();
    auto cg = boardObject(P1, kClocktower);
    auto top = deckCard(P1, kOnTheHunt);   // Power cost 2, no runes
    engine_->testHook_executor().killObject(cg);
    engine_->testHook_runChain();
    EXPECT_TRUE(p1_.int_prompts.empty()) << "not offered: its Power cost can't be paid";
    EXPECT_TRUE(contains(s().player(P1).banishment, top));
}

// ─── RAD-068 Orianna, Mechanical Marvel ─────────────────────────────────────

TEST_F(RadianceB04Test, Orianna_GearCostsOneAndAPowerLessOncePerTurn) {
    makeEngine();
    boardObject(P1, kOrianna, 0);
    auto blade = cardInHand(P1, kBlade);
    auto unit = cardInHand(P1, kScorcher);
    s().player(P1).rune_pool.energy = 2;   // Blade costs 3 + [1]
    engine_->testHook_cleanup();
    EXPECT_TRUE(engine_->testHook_canAfford(P1, blade));
    s().player(P1).rune_pool.energy = 4;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, unit)) << "gear only";
    s().player(P1).rune_pool.energy = 2;

    play(blade, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(blade).location.has_value());
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
    engine_->testHook_cleanup();

    auto blade2 = cardInHand(P1, kBlade);
    s().player(P1).rune_pool.energy = 2;
    engine_->testHook_cleanup();
    EXPECT_FALSE(engine_->testHook_canAfford(P1, blade2)) << "once each turn";

    s().turn.turn_number += 1;
    engine_->testHook_cleanup();
    EXPECT_TRUE(engine_->testHook_canAfford(P1, blade2)) << "a new turn";
}

TEST_F(RadianceB04Test, Orianna_NoDiscountFromBase) {
    makeEngine();
    boardObject(P1, kOrianna);
    auto blade = cardInHand(P1, kBlade);
    s().player(P1).rune_pool.energy = 2;
    engine_->testHook_cleanup();
    EXPECT_FALSE(engine_->testHook_canAfford(P1, blade));
}

TEST_F(RadianceB04Test, Orianna_DiscountDoesNotStackAcrossCleanups) {
    makeEngine();
    boardObject(P1, kOrianna, 0);
    auto blade = cardInHand(P1, kBlade);
    for (int i = 0; i < 5; ++i) engine_->testHook_cleanup();
    s().player(P1).rune_pool.energy = 1;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, blade)) << "still only [1][A] off";
}

// ─── RAD-069 Zero Point Inflection ──────────────────────────────────────────

TEST_F(RadianceB04Test, ZeroPoint_PlayingACardDrawsTwo) {
    makeEngine();
    boardObject(P1, kZeroPoint);
    auto d1 = deckCard(P1);
    auto d2 = deckCard(P1);
    auto unit = cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 2;
    play(unit, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(contains(s().player(P1).hand, d1));
    EXPECT_TRUE(contains(s().player(P1).hand, d2));
}

TEST_F(RadianceB04Test, ZeroPoint_DrawingWithNineOtherCardsBackfires) {
    makeEngine();
    auto zpi = boardObject(P1, kZeroPoint);
    for (int i = 0; i < 9; ++i) cardInHand(P1, kScorcher);
    deckCard(P1);
    engine_->testHook_cleanup();
    engine_->testHook_executor().drawCards(P1, 1);
    engine_->testHook_runChain();
    EXPECT_TRUE(s().player(P1).hand.empty()) << "discarded the hand";
    EXPECT_EQ(s().player(P1).trash.size(), 11u) << "10 discarded + Zero Point Inflection";
    EXPECT_FALSE(s().getObject(zpi).location.has_value()) << "killed itself";
    EXPECT_EQ(s().player(P2).score, 1);
}

TEST_F(RadianceB04Test, ZeroPoint_DrawingWithEightOtherCardsIsFine) {
    makeEngine();
    auto zpi = boardObject(P1, kZeroPoint);
    for (int i = 0; i < 8; ++i) cardInHand(P1, kScorcher);
    deckCard(P1);
    engine_->testHook_cleanup();
    engine_->testHook_executor().drawCards(P1, 1);
    engine_->testHook_runChain();
    EXPECT_EQ(s().player(P1).hand.size(), 9u);
    EXPECT_TRUE(s().getObject(zpi).location.has_value());
    EXPECT_EQ(s().player(P2).score, 0);
}

// Its own "draw 2" counts only the cards besides the two drawn: 8 cards left
// in hand after the play + 2 drawn = 10 in hand but 8 "other" cards.
TEST_F(RadianceB04Test, ZeroPoint_OwnDrawTwoWithEightOtherCardsIsFine) {
    makeEngine();
    auto zpi = boardObject(P1, kZeroPoint);
    for (int i = 0; i < 8; ++i) cardInHand(P1, kScorcher);
    for (int i = 0; i < 3; ++i) deckCard(P1);
    auto unit = cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 2;
    play(unit, LocationId{BaseLocation{P1}});
    EXPECT_EQ(s().player(P1).hand.size(), 10u);
    EXPECT_TRUE(s().getObject(zpi).location.has_value());
    EXPECT_EQ(s().player(P2).score, 0);
}

TEST_F(RadianceB04Test, ZeroPoint_OwnDrawTwoWithNineOtherCardsBackfires) {
    makeEngine();
    auto zpi = boardObject(P1, kZeroPoint);
    for (int i = 0; i < 9; ++i) cardInHand(P1, kScorcher);
    for (int i = 0; i < 3; ++i) deckCard(P1);
    auto unit = cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 2;
    play(unit, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().player(P1).hand.empty());
    EXPECT_FALSE(s().getObject(zpi).location.has_value());
    EXPECT_EQ(s().player(P2).score, 1);
}

// ─── RAD-070 Hold 'Em ───────────────────────────────────────────────────────

TEST_F(RadianceB04Test, HoldEm_CostsTwoLessAfterPlayingToABattlefield) {
    makeEngine();
    controlBf(0, P1);
    auto spell = cardInHand(P1, kHoldEm);
    s().player(P1).rune_pool.energy = 1;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, spell)) << "full price [3]";
    EXPECT_TRUE(card_db.get(kHoldEm).keywords.has(Keyword::Action));

    auto unit = cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 3;
    play(unit, LocationId{BattlefieldLocation{0}});
    ASSERT_TRUE(s().getObject(unit).isAtBattlefield());
    EXPECT_TRUE(engine_->testHook_canAfford(P1, spell)) << "[1] after the discount";

    p1_.picks = {unit};
    play(spell);
    EXPECT_EQ(s().getObject(unit).current_might, 5) << "2 + 3 this turn";
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
}

TEST_F(RadianceB04Test, HoldEm_PlayingToBaseDoesNotCount) {
    makeEngine();
    auto spell = cardInHand(P1, kHoldEm);
    auto unit = cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 3;
    play(unit, LocationId{BaseLocation{P1}});
    EXPECT_FALSE(engine_->testHook_canAfford(P1, spell));
}

}  // namespace
}  // namespace riftbound::test
