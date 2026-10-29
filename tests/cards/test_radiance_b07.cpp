/// @file test_radiance_b07.cpp
/// Radiance batch 07: Siren of the Shallows' [Disarm], Suave Negotiator's
/// shared draw, The Waters' Wage's face-down Gold bonus, Demon Shade's
/// move-or-Ganking modes, The Good Stuff's [Deploy] draw and hold return,
/// Bushwhack Trap's extra facedown slot, Evelynn, In Control's [Disarm] and
/// facedown hold point, Lulu, Whimsical's base-Might-4, Mechanical Bandit's
/// lowest-cost reveal and Ziggs, Explosive's Bomb ability and gear-kill pump.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "cards/radiance_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kSiren      = 1055;
constexpr CardDefId kNegotiator = 1056;
constexpr CardDefId kWatersWage = 1057;
constexpr CardDefId kDemonShade = 1058;
constexpr CardDefId kGoodStuff  = 1059;
constexpr CardDefId kBushwhack  = 1060;
constexpr CardDefId kEvelynn    = 1061;
constexpr CardDefId kLulu       = 1062;
constexpr CardDefId kBandit     = 1063;
constexpr CardDefId kZiggs      = 1064;
constexpr CardDefId kOtterpus   = 840;  // 2-energy 2-Might unit, no play or exhaust text
constexpr CardDefId kRivenBB    = 963;  // 4-energy unit (Riven, Broken Blade)
constexpr CardDefId kPoster     = 983;  // Concert Poster: a [Deploy] gear

/// Answers int prompts from `answers`, object prompts from `picks` (a pick is
/// consumed only when offered; `want` is the fallback), passes priority
/// otherwise. Records every prompt it was shown.
class B07Agent : public AgentInterface {
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

class RadianceB07Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B07Agent p1_, p2_;
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
    void setBattlefieldCard(BattlefieldId bf, CardDefId def_id = kInvalidId) {
        auto id = s().createObject();
        auto& o = s().getObject(id);
        o.card_type = CardType::Battlefield;
        o.card_def_id = def_id;
        o.name = def_id != kInvalidId ? card_db.get(def_id).name : "Battlefield";
        s().battlefields[bf].card_object_id = id;
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
    /// A card on TOP of `p`'s Main Deck (back = top); blank if def_id is invalid.
    GameObjectId deckCard(PlayerId p, CardDefId def_id = kInvalidId) {
        auto top = s().createObject();
        auto& c = s().getObject(top);
        if (def_id != kInvalidId) stamp(c, p, def_id);
        c.owner = c.controller = p;
        c.zone = ZoneType::MainDeck;
        s().player(p).main_deck.push_back(top);
        return top;
    }
    /// Move `card` from P1's hand into a facedown slot at `bf`.
    void hideAt(GameObjectId card, BattlefieldId bf) {
        auto& ps = s().player(s().getObject(card).owner);
        ps.hand.erase(std::find(ps.hand.begin(), ps.hand.end(), card));
        auto& c = s().getObject(card);
        c.is_hidden = true;
        c.hidden_at = bf;
        c.zone = ZoneType::FacedownZone;
        c.location = BattlefieldLocation{bf};
        s().battlefields[bf].facedown.push_back(card);
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
    bool inHand(PlayerId p, GameObjectId id) {
        const auto& h = s().player(p).hand;
        return std::find(h.begin(), h.end(), id) != h.end();
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
    void playFromFacedown(GameObjectId card) {
        Intent i;
        i.type = IntentType::PlayReaction;
        i.player = P1;
        i.card = card;
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
    void phase(TurnPhase from, TurnPhase to, PlayerId turn_player = P1) {
        s().turn.phase = to;
        events.emit(PhaseChangedEvent{from, to, turn_player});
        engine_->testHook_runChain();
    }
    void controlBf(BattlefieldId bf, PlayerId p) { s().battlefields[bf].controller = p; }
    std::vector<GameObjectId> goldOf(PlayerId p) {
        std::vector<GameObjectId> out;
        for (auto& [id, o] : s().objects)
            if (o.isGear() && o.controller == p && o.name == "Gold" && o.location.has_value())
                out.push_back(id);
        return out;
    }
    bool offersHideAt(GameObjectId card, BattlefieldId bf) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::HideCard && a.card == card && a.chosen_battlefield == bf)
                return true;
        return false;
    }
};

// ─── RAD-097 Siren of the Shallows ──────────────────────────────────────────

TEST_F(RadianceB07Test, Siren_DisarmGivesAnEnemyHereMinusOne) {
    makeEngine();
    boardObject(P1, kSiren, 0);
    auto e1 = boardObject(P2, kOtterpus, 0);
    auto e2 = boardObject(P2, kOtterpus, 0);
    auto far = boardObject(P2, kOtterpus, 1);
    p1_.picks = {e2};
    events.emit(CombatStartedEvent{0, /*attacker=*/P1, /*defender=*/P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(e2).current_might, 1) << "[Disarm]: -1 this turn";
    EXPECT_EQ(s().getObject(e1).current_might, 2);
    EXPECT_EQ(s().getObject(far).current_might, 2);
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_EQ(p1_.obj_prompts.front().size(), 2u) << "only enemy units here";
}

TEST_F(RadianceB07Test, Siren_DefendingDoesNotDisarm) {
    makeEngine();
    boardObject(P1, kSiren, 0);
    auto e = boardObject(P2, kOtterpus, 0);
    events.emit(CombatStartedEvent{0, /*attacker=*/P2, /*defender=*/P1});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(e).current_might, 2);
}

TEST_F(RadianceB07Test, Siren_HasGanking) {
    makeEngine();
    EXPECT_TRUE(card_db.get(kSiren).keywords.has(Keyword::Ganking));
    EXPECT_EQ(card_db.get(kSiren).might, 1);
}

// ─── RAD-099 Suave Negotiator ───────────────────────────────────────────────

TEST_F(RadianceB07Test, Negotiator_YouAndTheOpponentEachDrawOne) {
    makeEngine();
    auto mine = deckCard(P1);
    auto theirs = deckCard(P2);
    s().player(P1).rune_pool.energy = 3;
    auto neg = cardInHand(P1, kNegotiator);
    play(neg, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().getObject(neg).isAtBase());
    EXPECT_TRUE(inHand(P1, mine));
    EXPECT_TRUE(inHand(P2, theirs));
}

// ─── RAD-100 The Waters' Wage ───────────────────────────────────────────────

TEST_F(RadianceB07Test, WatersWage_FromHandPlaysOneExhaustedGold) {
    makeEngine();
    s().player(P1).rune_pool.energy = 1;
    auto wage = cardInHand(P1, kWatersWage);
    play(wage);
    auto gold = goldOf(P1);
    ASSERT_EQ(gold.size(), 1u);
    EXPECT_TRUE(s().getObject(gold[0]).is_exhausted);
    EXPECT_EQ(s().getObject(gold[0]).card_def_id, kGoldGearCardDefId);
}

TEST_F(RadianceB07Test, WatersWage_FromFaceDownOnYourTurnPlaysThreeReadyGold) {
    makeEngine();
    controlBf(0, P1);
    boardObject(P1, kOtterpus, 0);
    auto wage = cardInHand(P1, kWatersWage);
    hideAt(wage, 0);
    playFromFacedown(wage);
    auto gold = goldOf(P1);
    ASSERT_EQ(gold.size(), 3u);
    for (auto g : gold) {
        EXPECT_FALSE(s().getObject(g).is_exhausted);
        EXPECT_EQ(s().getObject(g).card_def_id, kGoldGearCardDefId);
    }
}

TEST_F(RadianceB07Test, WatersWage_FromFaceDownOnTheOpponentsTurnPlaysOneExhaustedGold) {
    makeEngine();
    controlBf(0, P1);
    boardObject(P1, kOtterpus, 0);
    auto wage = cardInHand(P1, kWatersWage);
    hideAt(wage, 0);
    s().turn.turn_player = P2;
    playFromFacedown(wage);
    auto gold = goldOf(P1);
    ASSERT_EQ(gold.size(), 1u);
    EXPECT_TRUE(s().getObject(gold[0]).is_exhausted);
}

TEST_F(RadianceB07Test, WatersWage_HasHidden) {
    makeEngine();
    EXPECT_TRUE(card_db.get(kWatersWage).keywords.has(Keyword::Hidden));
}

// ─── RAD-102 Demon Shade ────────────────────────────────────────────────────

TEST_F(RadianceB07Test, DemonShade_MovesAFriendlyUnitToAnOpenBattlefield) {
    makeEngine();
    controlBf(0, P2);
    boardObject(P2, kOtterpus, 0);
    auto unit = boardObject(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 3;
    auto shade = cardInHand(P1, kDemonShade);
    p1_.answers = {2};   // move to BF1 (the open one)
    p1_.picks = {unit};
    play(shade);
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0, 2})) << "BF0 is controlled: not open";
    EXPECT_EQ(s().getObject(unit).battlefieldId(), std::optional<BattlefieldId>{1});
    EXPECT_FALSE(s().getObject(unit).hasKeyword(Keyword::Ganking));
}

TEST_F(RadianceB07Test, DemonShade_GivesGankingWhenNoBattlefieldIsOpen) {
    makeEngine();
    controlBf(0, P1);
    controlBf(1, P2);
    auto unit = boardObject(P1, kOtterpus, 0);
    boardObject(P2, kOtterpus, 1);
    s().player(P1).rune_pool.energy = 3;
    auto shade = cardInHand(P1, kDemonShade);
    p1_.answers = {0};
    p1_.picks = {unit};
    play(shade);
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0})) << "no open battlefield to move to";
    EXPECT_TRUE(s().getObject(unit).hasKeyword(Keyword::Ganking));
    EXPECT_EQ(s().getObject(unit).battlefieldId(), std::optional<BattlefieldId>{0});
}

TEST_F(RadianceB07Test, DemonShade_GankingModeDoesNothingWhileABattlefieldIsOpen) {
    makeEngine();
    auto unit = boardObject(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 3;
    auto shade = cardInHand(P1, kDemonShade);
    p1_.answers = {0};
    p1_.picks = {unit};
    play(shade);
    EXPECT_FALSE(s().getObject(unit).hasKeyword(Keyword::Ganking));
    EXPECT_TRUE(s().getObject(unit).isAtBase());
}

TEST_F(RadianceB07Test, DemonShade_NeedsAFriendlyUnit) {
    makeEngine();
    const Card* c = card_registry.get(kDemonShade);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->hasLegalTargets(s(), P1));
    boardObject(P1, kOtterpus);
    EXPECT_TRUE(c->hasLegalTargets(s(), P1));
    EXPECT_TRUE(card_db.get(kDemonShade).keywords.has(Keyword::Hidden));
}

// ─── RAD-106 The Good Stuff ─────────────────────────────────────────────────

TEST_F(RadianceB07Test, GoodStuff_PlayedToABattlefieldDiscardsOneThenDrawsThree) {
    makeEngine();
    auto junk = cardInHand(P1, kOtterpus);
    std::vector<GameObjectId> deck;
    for (int i = 0; i < 4; ++i) deck.push_back(deckCard(P1));
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 1;
    auto gs = cardInHand(P1, kGoodStuff);
    p1_.want = junk;
    play(gs, LocationId{BattlefieldLocation{1}});
    ASSERT_TRUE(s().getObject(gs).isAtBattlefield());
    EXPECT_EQ(s().getObject(junk).zone, ZoneType::Trash);
    EXPECT_EQ(s().player(P1).hand.size(), 3u);
    EXPECT_TRUE(inHand(P1, deck[3]));
    EXPECT_TRUE(inHand(P1, deck[1]));
    EXPECT_FALSE(inHand(P1, deck[0]));
}

TEST_F(RadianceB07Test, GoodStuff_DrawsThreeWithAnEmptyHand) {
    makeEngine();
    for (int i = 0; i < 3; ++i) deckCard(P1);
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 1;
    auto gs = cardInHand(P1, kGoodStuff);
    play(gs, LocationId{BattlefieldLocation{0}});
    EXPECT_EQ(s().player(P1).hand.size(), 3u);
}

TEST_F(RadianceB07Test, GoodStuff_ReturnsToHandAfterYouHoldHere) {
    makeEngine();
    setBattlefieldCard(0);
    controlBf(0, P1);
    boardObject(P1, kOtterpus, 0);
    auto gs = boardObject(P1, kGoodStuff, 0);
    engine_->testHook_scoreHold(P1, 0);
    engine_->testHook_runChain();
    phase(TurnPhase::DrawPhase, TurnPhase::MainPhase);
    EXPECT_TRUE(inHand(P1, gs));
    EXPECT_EQ(s().getObject(gs).zone, ZoneType::Hand);
}

TEST_F(RadianceB07Test, GoodStuff_StaysWithoutAHoldHere) {
    makeEngine();
    setBattlefieldCard(0);
    setBattlefieldCard(1);
    controlBf(1, P1);
    boardObject(P1, kOtterpus, 1);
    auto gs = boardObject(P1, kGoodStuff, 0);
    engine_->testHook_scoreHold(P1, 1);   // held the OTHER battlefield
    engine_->testHook_runChain();
    phase(TurnPhase::DrawPhase, TurnPhase::MainPhase);
    EXPECT_TRUE(s().getObject(gs).isAtBattlefield());
}

TEST_F(RadianceB07Test, GoodStuff_DiesWhenTheOpponentHoldsHere) {
    makeEngine();
    setBattlefieldCard(0);
    auto gs = boardObject(P1, kGoodStuff, 0);
    engine_->testHook_scoreHold(P2, 0);
    EXPECT_FALSE(s().getObject(gs).location.has_value()) << "[Deploy]";
    EXPECT_FALSE(inHand(P1, gs));
}

// ─── RAD-108 Bushwhack Trap ─────────────────────────────────────────────────

TEST_F(RadianceB07Test, Bushwhack_ControllerMayHideAnAdditionalCardHere) {
    makeEngine();
    setBattlefieldCard(0);
    setBattlefieldCard(1);
    controlBf(0, P1);
    controlBf(1, P1);
    boardObject(P1, kOtterpus, 0);
    boardObject(P1, kOtterpus, 1);
    boardObject(P1, kBushwhack, 0);
    rune(P1, Domain::Chaos);
    auto first = cardInHand(P1, kWatersWage);
    hideAt(first, 0);
    auto second = cardInHand(P1, kWatersWage);
    hideAt(second, 1);
    auto third = cardInHand(P1, kWatersWage);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().battlefields[0].facedown_max_occupancy, 2);
    EXPECT_EQ(s().battlefields[1].facedown_max_occupancy, 1);
    EXPECT_TRUE(offersHideAt(third, 0)) << "a second card may be hidden at the trap's battlefield";
    EXPECT_FALSE(offersHideAt(third, 1));
}

TEST_F(RadianceB07Test, Bushwhack_NoExtraSlotWhileTheOpponentControlsIt) {
    makeEngine();
    setBattlefieldCard(0);
    controlBf(0, P1);
    auto mine = boardObject(P1, kOtterpus, 0);
    boardObject(P1, kBushwhack, 0);
    engine_->testHook_cleanup();
    ASSERT_EQ(s().battlefields[0].facedown_max_occupancy, 2);
    // The opponent takes the battlefield: the slot goes away, and comes back
    // when the trap's controller retakes it. Re-running the aura doesn't stack.
    s().getObject(mine).location = BattlefieldLocation{1};
    auto theirs = boardObject(P2, kOtterpus, 0);
    controlBf(0, P2);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().battlefields[0].facedown_max_occupancy, 1);
    s().getObject(theirs).location = BaseLocation{P2};
    s().getObject(theirs).zone = ZoneType::Base;
    s().getObject(mine).location = BattlefieldLocation{0};
    controlBf(0, P1);
    engine_->testHook_cleanup();
    engine_->testHook_cleanup();
    EXPECT_EQ(s().battlefields[0].facedown_max_occupancy, 2);
}

TEST_F(RadianceB07Test, Bushwhack_TwoTrapsGiveTwoExtraSlots) {
    makeEngine();
    setBattlefieldCard(0);
    controlBf(0, P1);
    boardObject(P1, kOtterpus, 0);
    boardObject(P1, kBushwhack, 0);
    boardObject(P1, kBushwhack, 0);
    engine_->testHook_cleanup();
    engine_->testHook_cleanup();
    EXPECT_EQ(s().battlefields[0].facedown_max_occupancy, 3);
}

// ─── RAD-109 Evelynn, In Control ────────────────────────────────────────────

TEST_F(RadianceB07Test, Evelynn_DisarmOnAttack) {
    makeEngine();
    boardObject(P1, kEvelynn, 0);
    auto e = boardObject(P2, kOtterpus, 0);
    p1_.picks = {e};
    events.emit(CombatStartedEvent{0, /*attacker=*/P1, /*defender=*/P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(e).current_might, 1);
}

TEST_F(RadianceB07Test, Evelynn_HoldWithAFacedownCardHereScoresAnExtraPoint) {
    makeEngine();
    setBattlefieldCard(0);
    controlBf(0, P1);
    boardObject(P1, kEvelynn, 0);
    auto hidden = cardInHand(P1, kWatersWage);
    hideAt(hidden, 0);
    engine_->testHook_scoreHold(P1, 0);
    engine_->testHook_runChain();
    EXPECT_EQ(s().player(P1).score, 2) << "1 for the hold + 1 from Evelynn";
}

TEST_F(RadianceB07Test, Evelynn_HoldWithoutAFacedownCardHereScoresOnlyTheHold) {
    makeEngine();
    setBattlefieldCard(0);
    setBattlefieldCard(1);
    controlBf(0, P1);
    controlBf(1, P1);
    boardObject(P1, kEvelynn, 0);
    boardObject(P1, kOtterpus, 1);
    auto hidden = cardInHand(P1, kWatersWage);
    hideAt(hidden, 1);   // facedown, but at the other battlefield
    engine_->testHook_scoreHold(P1, 0);
    engine_->testHook_runChain();
    EXPECT_EQ(s().player(P1).score, 1);
}

// ─── RAD-110 Lulu, Whimsical ────────────────────────────────────────────────

TEST_F(RadianceB07Test, Lulu_MovingMakesAnotherUnitsBaseMightFour) {
    makeEngine();
    auto lulu = boardObject(P1, kLulu);
    auto enemy = boardObject(P2, kOtterpus, 0);
    p1_.answers = {1};
    p1_.picks = {enemy};
    engine_->testHook_executeIntent(Intent::standardMove(P1, {lulu}, BattlefieldLocation{1}));
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy).current_might, 4);
    EXPECT_EQ(s().getObject(lulu).current_might, 3);
    ASSERT_FALSE(p1_.obj_prompts.empty());
    for (auto id : p1_.obj_prompts.front()) EXPECT_NE(id, lulu) << "another unit";
}

TEST_F(RadianceB07Test, Lulu_ModifiersStackOnTheNewBase) {
    makeEngine();
    auto lulu = boardObject(P1, kLulu);
    auto ally = boardObject(P1, kOtterpus);
    s().getObject(ally).buff_count = 1;   // +1 buff on top of the base
    s().getObject(ally).recomputeMight();
    ASSERT_EQ(s().getObject(ally).current_might, 3);
    p1_.answers = {1};
    p1_.picks = {ally};
    engine_->testHook_executeIntent(Intent::standardMove(P1, {lulu}, BattlefieldLocation{1}));
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(ally).current_might, 5) << "base 4 + 1 buff";
}

TEST_F(RadianceB07Test, Lulu_DecliningChangesNothing) {
    makeEngine();
    auto lulu = boardObject(P1, kLulu);
    auto enemy = boardObject(P2, kOtterpus, 0);
    p1_.answers = {0};
    p1_.picks = {enemy};
    engine_->testHook_executeIntent(Intent::standardMove(P1, {lulu}, BattlefieldLocation{1}));
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy).current_might, 2);
}

// ─── RAD-111 Mechanical Bandit ──────────────────────────────────────────────

TEST_F(RadianceB07Test, Bandit_YouDrawYourRevealedCardWhenItIsTheCheapest) {
    makeEngine();
    boardObject(P1, kBandit);
    auto my_bottom = deckCard(P1);
    auto mine = deckCard(P1, kOtterpus);     // 2 Energy
    deckCard(P2);
    auto theirs = deckCard(P2, kRivenBB);    // 4 Energy
    phase(TurnPhase::AwakenPhase, TurnPhase::BeginningStep);
    EXPECT_TRUE(inHand(P1, mine));
    EXPECT_EQ(s().player(P1).main_deck.back(), my_bottom);
    EXPECT_EQ(s().player(P2).main_deck.front(), theirs) << "recycled to the bottom";
    EXPECT_FALSE(inHand(P2, theirs));
}

TEST_F(RadianceB07Test, Bandit_TieForLowestStillDraws) {
    makeEngine();
    boardObject(P1, kBandit);
    auto mine = deckCard(P1, kOtterpus);
    auto theirs = deckCard(P2, kOtterpus);
    phase(TurnPhase::AwakenPhase, TurnPhase::BeginningStep);
    EXPECT_TRUE(inHand(P1, mine));
    EXPECT_EQ(s().getObject(theirs).zone, ZoneType::MainDeck);
}

TEST_F(RadianceB07Test, Bandit_BothCardsAreRecycledWhenYoursIsNotTheCheapest) {
    makeEngine();
    boardObject(P1, kBandit);
    deckCard(P1);
    auto mine = deckCard(P1, kRivenBB);      // 4 Energy
    deckCard(P2);
    auto theirs = deckCard(P2, kOtterpus);   // 2 Energy
    phase(TurnPhase::AwakenPhase, TurnPhase::BeginningStep);
    EXPECT_FALSE(inHand(P1, mine));
    EXPECT_EQ(s().player(P1).main_deck.front(), mine);
    EXPECT_EQ(s().player(P2).main_deck.front(), theirs);
    EXPECT_FALSE(inHand(P2, theirs));
}

TEST_F(RadianceB07Test, Bandit_OnlyAtTheStartOfYourBeginningPhase) {
    makeEngine();
    boardObject(P1, kBandit);
    auto mine = deckCard(P1, kOtterpus);
    deckCard(P2, kRivenBB);
    s().turn.turn_player = P2;
    phase(TurnPhase::AwakenPhase, TurnPhase::BeginningStep, P2);
    EXPECT_FALSE(inHand(P1, mine));
}

// ─── RAD-112 Ziggs, Explosive ───────────────────────────────────────────────

TEST_F(RadianceB07Test, Ziggs_AbilityPlaysABombToTheChosenBattlefield) {
    makeEngine();
    auto ziggs = boardObject(P1, kZiggs);
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {1};   // battlefield #1
    activate(ziggs);
    EXPECT_TRUE(s().getObject(ziggs).is_exhausted) << "[E]";
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0) << "[1]";
    int bombs = 0;
    for (auto& [id, o] : s().objects)
        if (o.isGear() && o.controller == P1 && o.card_def_id == kBombTokenCardDefId &&
            o.battlefieldId() == std::optional<BattlefieldId>{1})
            ++bombs;
    EXPECT_EQ(bombs, 1);
}

TEST_F(RadianceB07Test, Ziggs_DefendingKillsAFriendlyGearForPlusTwo) {
    makeEngine();
    auto ziggs = boardObject(P1, kZiggs, 0);
    auto gear = boardObject(P1, kPoster);
    boardObject(P2, kOtterpus, 0);
    p1_.answers = {1};
    p1_.picks = {gear};
    events.emit(CombatStartedEvent{0, /*attacker=*/P2, /*defender=*/P1});
    engine_->testHook_runChain();
    EXPECT_FALSE(s().getObject(gear).location.has_value()) << "the gear was killed";
    EXPECT_EQ(s().getObject(ziggs).current_might, 6);
}

TEST_F(RadianceB07Test, Ziggs_DecliningKeepsTheGear) {
    makeEngine();
    auto ziggs = boardObject(P1, kZiggs, 0);
    auto gear = boardObject(P1, kPoster);
    boardObject(P2, kOtterpus, 0);
    p1_.answers = {0};
    events.emit(CombatStartedEvent{0, /*attacker=*/P2, /*defender=*/P1});
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(gear).location.has_value());
    EXPECT_EQ(s().getObject(ziggs).current_might, 4);
}

TEST_F(RadianceB07Test, Ziggs_NoPromptWithoutAFriendlyGear) {
    makeEngine();
    auto ziggs = boardObject(P1, kZiggs, 0);
    boardObject(P2, kOtterpus, 0);
    events.emit(CombatStartedEvent{0, /*attacker=*/P2, /*defender=*/P1});
    engine_->testHook_runChain();
    EXPECT_TRUE(p1_.int_prompts.empty());
    EXPECT_EQ(s().getObject(ziggs).current_might, 4);
}

}  // namespace
}  // namespace riftbound::test
