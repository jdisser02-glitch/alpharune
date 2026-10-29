/// @file test_radiance_b11.cpp
/// Radiance batch 11: Agony's Embrace, Last Caress, Exemplar of Demacia,
/// Demacian Standard, Bandle Scouts' Academy, Black Market, Black Rose
/// Sanctum, Cosmic Vista, Durand Memorial, Firelights' Hideout, Packed
/// Amphitheater, Neeko, Blending In.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kTheExile          = 961;
constexpr CardDefId kRivenBrokenBlade  = 963;   // champion unit, 4 [M]
constexpr CardDefId kResourceExtractor = 1020;  // [Deploy] gear, [1], no play trigger
constexpr CardDefId kOtterpus          = 840;   // 2-energy 2-Might unit
constexpr CardDefId kAgony             = 1095;
constexpr CardDefId kLastCaress        = 1096;
constexpr CardDefId kExemplar          = 1097;
constexpr CardDefId kStandard          = 1098;
constexpr CardDefId kBandleScouts      = 1099;
constexpr CardDefId kBlackMarket       = 1100;
constexpr CardDefId kBlackRose         = 1101;
constexpr CardDefId kCosmicVista       = 1102;
constexpr CardDefId kDurand            = 1103;
constexpr CardDefId kFirelights        = 1104;
constexpr CardDefId kPacked            = 1105;
constexpr CardDefId kNeeko             = 1106;

/// Answers yes/no and mode prompts from a queue of int answers, picks `want`
/// when offered objects, passes priority otherwise. Records every prompt.
class B11Agent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    GameObjectId want = kInvalidId;
    std::vector<std::vector<int>> int_prompts;
    std::vector<std::vector<GameObjectId>> obj_prompts;
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
            std::vector<GameObjectId> offered;
            for (const auto& i : legal)
                if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects.front());
            obj_prompts.push_back(offered);
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class RadianceB11Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B11Agent p1_, p2_;
    GameState& s() { return engine_->mutableState(); }

    void makeEngine() {
        engine_ = std::make_unique<GameEngine>(card_db, events, card_registry);
        auto& st = s();
        st.mode = ModeOfPlay{};
        st.players[0].id = P1;
        st.players[1].id = P2;
        st.players[0].turns_taken = st.players[1].turns_taken = 4;
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
    GameObjectId setLegend(PlayerId p, CardDefId def_id = kTheExile) {
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
        o.card_type = CardType::Battlefield;
        o.card_def_id = def_id;
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
    /// A face-down card at `bf`, hidden on `turn`.
    GameObjectId hiddenAt(PlayerId p, CardDefId def_id, BattlefieldId bf, int turn) {
        auto id = cardInHand(p, def_id);
        auto& ps = s().player(p);
        ps.hand.erase(std::find(ps.hand.begin(), ps.hand.end(), id));
        auto& c = s().getObject(id);
        c.is_hidden = true;
        c.hidden_at = bf;
        c.hidden_on_turn = turn;
        c.zone = ZoneType::FacedownZone;
        s().battlefields[bf].facedown.push_back(id);
        return id;
    }
    GameObjectId deckCard(PlayerId p) {
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
    int runesOnBoard(PlayerId p) {
        int n = 0;
        for (auto& [id, o] : s().objects)
            if (o.isRune() && o.controller == p && o.location.has_value()) ++n;
        return n;
    }
    void play(PlayerId p, GameObjectId card, std::optional<LocationId> where) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = p;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
    void playReaction(PlayerId p, GameObjectId card) {
        Intent i;
        i.type = IntentType::PlayReaction;
        i.player = p;
        i.card = card;
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
    void controlBf(BattlefieldId bf, PlayerId p) { s().battlefields[bf].controller = p; }
    bool inHand(PlayerId p, GameObjectId id) {
        const auto& h = s().player(p).hand;
        return std::find(h.begin(), h.end(), id) != h.end();
    }
    bool inTrash(PlayerId p, GameObjectId id) {
        const auto& t = s().player(p).trash;
        return std::find(t.begin(), t.end(), id) != t.end();
    }
    int tokensNamed(PlayerId p, const std::string& name) {
        int n = 0;
        for (auto& [id, o] : s().objects)
            if (o.controller == p && o.name == name && o.isToken() && o.location.has_value()) ++n;
        return n;
    }
};

// ─── RAD-153 Agony's Embrace ────────────────────────────────────────────────

TEST_F(RadianceB11Test, AgonysEmbrace_PlayFromFaceDownOnMyTurnDrawsOnceEachTurn) {
    makeEngine();
    setLegend(P1, kAgony); setLegend(P2);
    deckCard(P1); deckCard(P1);
    auto first = hiddenAt(P1, kOtterpus, 0, 4);
    auto second = hiddenAt(P1, kOtterpus, 1, 4);
    playReaction(P1, first);
    ASSERT_TRUE(s().getObject(first).isAtBattlefield());
    EXPECT_EQ(s().player(P1).hand.size(), 1u) << "draw 1, no discard on my turn";
    EXPECT_TRUE(p1_.obj_prompts.empty()) << "no discard prompt on my turn";

    playReaction(P1, second);
    EXPECT_EQ(s().player(P1).hand.size(), 1u) << "once each turn";

    s().turn.turn_number += 1;
    auto third = hiddenAt(P1, kOtterpus, 0, 4);
    playReaction(P1, third);
    EXPECT_EQ(s().player(P1).hand.size(), 2u) << "again next turn";
}

TEST_F(RadianceB11Test, AgonysEmbrace_OnTheOpponentsTurnDrawThenDiscard) {
    makeEngine();
    setLegend(P1, kAgony); setLegend(P2);
    s().turn.turn_player = P2;
    auto drawn = deckCard(P1);
    auto old = cardInHand(P1, kOtterpus);
    auto hidden = hiddenAt(P1, kOtterpus, 0, 4);
    p1_.want = old;
    playReaction(P1, hidden);
    EXPECT_TRUE(inHand(P1, drawn)) << "drew 1";
    EXPECT_FALSE(inHand(P1, old));
    EXPECT_TRUE(inTrash(P1, old)) << "then discarded the chosen card";
    ASSERT_EQ(p1_.obj_prompts.size(), 1u);
    EXPECT_EQ(p1_.obj_prompts[0].size(), 2u) << "either card in hand";
}

TEST_F(RadianceB11Test, AgonysEmbrace_PlayFromHandDoesNothing) {
    makeEngine();
    setLegend(P1, kAgony); setLegend(P2);
    deckCard(P1);
    s().player(P1).rune_pool.energy = 2;
    auto unit = cardInHand(P1, kOtterpus);
    play(P1, unit, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().player(P1).hand.empty());
    EXPECT_EQ(s().player(P1).main_deck.size(), 1u);
}

// ─── RAD-154 Last Caress ────────────────────────────────────────────────────

TEST_F(RadianceB11Test, LastCaress_PlaysAUnitToAnyBattlefieldAndBuffsIt) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    controlBf(1, P2);   // not a battlefield P1 could normally play to
    auto caress = cardInHand(P1, kLastCaress);
    auto unit = cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 4;
    p1_.answers = {1, 2};   // yes; Battlefield #1
    p1_.want = unit;
    play(P1, caress, std::nullopt);
    ASSERT_EQ(p1_.int_prompts.size(), 2u);
    EXPECT_EQ(p1_.int_prompts[1], (std::vector<int>{0, 1, 2})) << "base or either battlefield";
    EXPECT_EQ(s().getObject(unit).battlefieldId(), std::optional<BattlefieldId>{1});
    EXPECT_EQ(s().getObject(unit).buff_count, 1);
    EXPECT_EQ(s().getObject(unit).current_might, 3);
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0) << "paid [2] for each card";
}

TEST_F(RadianceB11Test, LastCaress_OnTheOpponentsTurnOnlyAFaceDownUnitCanBePlayed) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    s().turn.turn_player = P2;
    auto caress = cardInHand(P1, kLastCaress);
    auto from_hand = cardInHand(P1, kOtterpus);   // no [Reaction]: can't be played now
    auto hidden = hiddenAt(P1, kOtterpus, 0, 4);
    s().player(P1).rune_pool.energy = 4;
    p1_.answers = {1, 2};
    p1_.want = hidden;
    playReaction(P1, caress);
    ASSERT_EQ(p1_.obj_prompts.size(), 1u);
    EXPECT_EQ(p1_.obj_prompts[0], (std::vector<GameObjectId>{hidden}));
    EXPECT_TRUE(inHand(P1, from_hand));
    const auto& h = s().getObject(hidden);
    EXPECT_FALSE(h.is_hidden);
    EXPECT_EQ(h.battlefieldId(), std::optional<BattlefieldId>{1}) << "moved off its own battlefield";
    EXPECT_TRUE(s().battlefields[0].facedown.empty());
    EXPECT_EQ(h.buff_count, 1);
    EXPECT_EQ(s().player(P1).rune_pool.energy, 2) << "face down: ignoring its cost";
}

TEST_F(RadianceB11Test, LastCaress_DeclineDoesNothing) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto caress = cardInHand(P1, kLastCaress);
    auto unit = cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 4;
    p1_.answers = {0};
    play(P1, caress, std::nullopt);
    EXPECT_TRUE(inHand(P1, unit));
    EXPECT_EQ(s().player(P1).rune_pool.energy, 2);
}

TEST_F(RadianceB11Test, LastCaress_HasReaction) {
    makeEngine();
    EXPECT_TRUE(card_db.get(kLastCaress).keywords.has(Keyword::Reaction));
    EXPECT_EQ(card_db.get(kLastCaress).super_type, SuperType::Signature);
}

// ─── RAD-155 Exemplar of Demacia ────────────────────────────────────────────

TEST_F(RadianceB11Test, Exemplar_DeployGearMovesAFriendlyUnitThere) {
    makeEngine();
    auto legend = setLegend(P1, kExemplar); setLegend(P2);
    auto unit = boardObject(P1, kOtterpus);
    rune(P1, Domain::Body);   // the [A]
    s().player(P1).rune_pool.energy = 1;   // the gear's [1]
    auto gear = cardInHand(P1, kResourceExtractor);
    p1_.answers = {1};
    p1_.want = unit;
    play(P1, gear, LocationId{BattlefieldLocation{1}});
    ASSERT_TRUE(s().getObject(gear).isAtBattlefield());
    EXPECT_EQ(s().getObject(unit).battlefieldId(), std::optional<BattlefieldId>{1});
    EXPECT_TRUE(s().getObject(legend).is_exhausted) << "exhaust me";
    EXPECT_EQ(runesOnBoard(P1), 0) << "pay [A]: the rune was recycled";
}

TEST_F(RadianceB11Test, Exemplar_DecliningCostsNothing) {
    makeEngine();
    auto legend = setLegend(P1, kExemplar); setLegend(P2);
    auto unit = boardObject(P1, kOtterpus);
    rune(P1, Domain::Body);
    s().player(P1).rune_pool.energy = 1;
    auto gear = cardInHand(P1, kResourceExtractor);
    p1_.answers = {0};
    play(P1, gear, LocationId{BattlefieldLocation{1}});
    EXPECT_TRUE(s().getObject(unit).isAtBase());
    EXPECT_FALSE(s().getObject(legend).is_exhausted);
    EXPECT_EQ(runesOnBoard(P1), 1);
}

TEST_F(RadianceB11Test, Exemplar_NoOfferWithoutAnAToPay) {
    makeEngine();
    setLegend(P1, kExemplar); setLegend(P2);
    auto unit = boardObject(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 1;
    auto gear = cardInHand(P1, kResourceExtractor);
    p1_.answers = {1};
    play(P1, gear, LocationId{BattlefieldLocation{1}});
    EXPECT_TRUE(p1_.int_prompts.empty());
    EXPECT_TRUE(s().getObject(unit).isAtBase());
}

// ─── RAD-156 Demacian Standard ──────────────────────────────────────────────

TEST_F(RadianceB11Test, DemacianStandard_LoneAttackerHereGetsPlusTwo) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kStandard, 0);
    auto attacker = boardObject(P1, kOtterpus, 0);
    auto defender = boardObject(P2, kOtterpus, 0);
    s().getObject(attacker).combat_designation = CombatDesignation::Attacker;
    s().getObject(defender).combat_designation = CombatDesignation::Defender;
    events.emit(CombatStartedEvent{0, P1, P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(attacker).current_might, 4) << "2 + 2 this turn";
    EXPECT_EQ(s().getObject(defender).current_might, 2);
}

TEST_F(RadianceB11Test, DemacianStandard_AttackElsewhereGivesNothing) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kStandard, 1);
    auto attacker = boardObject(P1, kOtterpus, 0);
    auto defender = boardObject(P2, kOtterpus, 0);
    s().getObject(attacker).combat_designation = CombatDesignation::Attacker;
    s().getObject(defender).combat_designation = CombatDesignation::Defender;
    events.emit(CombatStartedEvent{0, P1, P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(attacker).current_might, 2);
}

TEST_F(RadianceB11Test, DemacianStandard_AfterHoldingHereMayReturnToHand) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto gear = boardObject(P1, kStandard, 0);
    controlBf(0, P1);
    p1_.answers = {1};
    engine_->testHook_scoreHold(P1, 0);   // "When you hold here" reaches the gear
    engine_->testHook_runChain();
    EXPECT_TRUE(inHand(P1, gear));
    EXPECT_FALSE(s().getObject(gear).location.has_value());
}

TEST_F(RadianceB11Test, DemacianStandard_NoReturnOfferWithoutAHold) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto gear = boardObject(P1, kStandard, 0);
    p1_.answers = {1};
    events.emit(PhaseChangedEvent{TurnPhase::ScoringStep, TurnPhase::MainPhase, P1});
    engine_->testHook_runChain();
    EXPECT_TRUE(p1_.int_prompts.empty());
    EXPECT_TRUE(s().getObject(gear).isAtBattlefield());
}

TEST_F(RadianceB11Test, DemacianStandard_IsDeploy) {
    makeEngine();
    ASSERT_NE(card_registry.get(kStandard), nullptr);
    EXPECT_TRUE(card_registry.get(kStandard)->deploys());
}

// ─── RAD-157 Bandle Scouts' Academy ─────────────────────────────────────────

TEST_F(RadianceB11Test, BandleScouts_HidingHereMakesAnExhaustedGold) {
    // The engine does not announce hides to battlefield cards yet; queue the
    // ability the way that dispatch would.
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto bf_card = setBattlefieldCard(0, kBandleScouts);
    auto hidden = hiddenAt(P1, kOtterpus, 0, 5);
    engine_->testHook_executor().queueTriggeredAbility(bf_card, P1, TriggerType::WhenYouHideACard,
                                                       hidden);
    engine_->testHook_runChain();
    int gold = 0;
    for (auto& [id, o] : s().objects)
        if (o.isGear() && o.controller == P1 && o.name == "Gold") {
            ++gold;
            EXPECT_TRUE(o.is_exhausted);
            EXPECT_EQ(o.card_def_id, kGoldGearCardDefId);
        }
    EXPECT_EQ(gold, 1);
}

TEST_F(RadianceB11Test, BandleScouts_CardHiddenElsewhereDoesNotCount) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto bf_card = setBattlefieldCard(0, kBandleScouts);
    auto hidden = hiddenAt(P1, kOtterpus, 1, 5);
    engine_->testHook_executor().queueTriggeredAbility(bf_card, P1, TriggerType::WhenYouHideACard,
                                                       hidden);
    engine_->testHook_runChain();
    for (auto& [id, o] : s().objects) EXPECT_NE(o.name, "Gold");
}

// ─── RAD-158 Black Market ───────────────────────────────────────────────────

TEST_F(RadianceB11Test, BlackMarket_EachPlayerMayDrawOnceOnTheirTurn) {
    makeEngine();
    auto l1 = setLegend(P1); auto l2 = setLegend(P2);
    setBattlefieldCard(0, kBlackMarket);
    controlBf(0, P2);   // P1 may use it without controlling it
    auto top = deckCard(P1);
    rune(P1, Domain::Calm);
    rune(P1, Domain::Fury);
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(l2).granted_abilities.empty()) << "not P2's turn";

    auto offers = [&]() {
        std::vector<Intent> out;
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::ActivateAbility && a.granted_ability_def == kBlackMarket)
                out.push_back(a);
        return out;
    };
    auto o = offers();
    ASSERT_EQ(o.size(), 1u);
    EXPECT_EQ(o[0].ability_source, l1);
    engine_->testHook_executeIntent(o[0]);
    engine_->testHook_runChain();
    EXPECT_TRUE(inHand(P1, top)) << "draw 1";
    EXPECT_EQ(runesOnBoard(P1), 0) << "[A][A]";

    rune(P1, Domain::Calm);
    rune(P1, Domain::Fury);
    deckCard(P1);
    engine_->testHook_cleanup();
    EXPECT_TRUE(offers().empty()) << "once on each of their turns";

    s().turn.turn_number += 1;
    engine_->testHook_cleanup();
    EXPECT_EQ(offers().size(), 1u) << "again on the next turn";
}

TEST_F(RadianceB11Test, BlackMarket_NotOfferedWithoutTwoPower) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kBlackMarket);
    deckCard(P1);
    rune(P1, Domain::Calm);
    engine_->testHook_cleanup();
    for (const auto& a : engine_->generateLegalActions())
        EXPECT_FALSE(a.type == IntentType::ActivateAbility && a.granted_ability_def == kBlackMarket);
}

// ─── RAD-159 Black Rose Sanctum ─────────────────────────────────────────────

TEST_F(RadianceB11Test, BlackRoseSanctum_FirstNonTokenDeathHereEachTurnMakesARecruit) {
    // The engine does not announce deaths to battlefield cards yet; queue the
    // ability the way that dispatch would.
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto bf_card = setBattlefieldCard(0, kBlackRose);
    auto& ex = engine_->testHook_executor();
    auto a = boardObject(P1, kOtterpus, 0);
    auto b = boardObject(P1, kOtterpus, 0);
    ex.killObject(a);
    ex.queueTriggeredAbility(bf_card, P1, TriggerType::WhenAFriendlyUnitDies, a);
    engine_->testHook_runChain();
    EXPECT_EQ(tokensNamed(P1, "Recruit"), 1);
    for (auto& [id, o] : s().objects)
        if (o.name == "Recruit") EXPECT_TRUE(o.isAtBase());

    ex.killObject(b);
    ex.queueTriggeredAbility(bf_card, P1, TriggerType::WhenAFriendlyUnitDies, b);
    engine_->testHook_runChain();
    EXPECT_EQ(tokensNamed(P1, "Recruit"), 1) << "only the first time each turn";

    // The other player has their own first time.
    auto c = boardObject(P2, kOtterpus, 0);
    ex.killObject(c);
    ex.queueTriggeredAbility(bf_card, P2, TriggerType::WhenAFriendlyUnitDies, c);
    engine_->testHook_runChain();
    EXPECT_EQ(tokensNamed(P2, "Recruit"), 1);
}

TEST_F(RadianceB11Test, BlackRoseSanctum_TokenOrElsewhereDoesNotCount) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto bf_card = setBattlefieldCard(0, kBlackRose);
    auto& ex = engine_->testHook_executor();
    auto elsewhere = boardObject(P1, kOtterpus, 1);
    ex.killObject(elsewhere);
    ex.queueTriggeredAbility(bf_card, P1, TriggerType::WhenAFriendlyUnitDies, elsewhere);
    auto tok = ex.createToken(P1, CardType::Unit, "Sprite", 1, {}, KeywordSet{},
                              LocationId{BattlefieldLocation{0}}, false);
    ex.killObject(tok);
    ex.queueTriggeredAbility(bf_card, P1, TriggerType::WhenAFriendlyUnitDies, tok);
    engine_->testHook_runChain();
    EXPECT_EQ(tokensNamed(P1, "Recruit"), 0);
}

// ─── RAD-160 Cosmic Vista ───────────────────────────────────────────────────

TEST_F(RadianceB11Test, CosmicVista_HoldWithAnEmptyRuneDeckScoresAnExtraPoint) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kCosmicVista);
    controlBf(0, P1);
    ASSERT_TRUE(s().player(P1).rune_deck.empty());
    engine_->testHook_scoreHold(P1, 0);
    engine_->testHook_runChain();
    EXPECT_EQ(s().player(P1).score, 2) << "1 for the hold + 1";
}

TEST_F(RadianceB11Test, CosmicVista_NoExtraPointWithRunesLeft) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kCosmicVista);
    controlBf(0, P1);
    auto r = rune(P1, Domain::Calm);
    auto& ro = s().getObject(r);
    ro.location = std::nullopt;
    ro.zone = ZoneType::RuneDeck;
    s().player(P1).rune_deck.push_back(r);
    engine_->testHook_scoreHold(P1, 0);
    engine_->testHook_runChain();
    EXPECT_EQ(s().player(P1).score, 1);
}

// ─── RAD-161 Durand Memorial ────────────────────────────────────────────────

TEST_F(RadianceB11Test, DurandMemorial_DefenderChoosesAUnitHereForTank) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kDurand);
    controlBf(0, P1);
    auto a = boardObject(P1, kOtterpus, 0);
    auto b = boardObject(P1, kOtterpus, 0);
    auto elsewhere = boardObject(P1, kOtterpus, 1);
    auto attacker = boardObject(P2, kOtterpus, 0);
    s().getObject(a).combat_designation = CombatDesignation::Defender;
    s().getObject(b).combat_designation = CombatDesignation::Defender;
    s().getObject(attacker).combat_designation = CombatDesignation::Attacker;
    p1_.want = b;
    events.emit(CombatStartedEvent{0, P2, P1});
    engine_->testHook_runChain();
    ASSERT_EQ(p1_.obj_prompts.size(), 1u);
    EXPECT_EQ(p1_.obj_prompts[0].size(), 2u) << "only P1's units here";
    EXPECT_TRUE(s().getObject(b).hasKeyword(Keyword::Tank));
    EXPECT_FALSE(s().getObject(a).hasKeyword(Keyword::Tank));
    EXPECT_FALSE(s().getObject(elsewhere).hasKeyword(Keyword::Tank));
    EXPECT_TRUE(p2_.obj_prompts.empty()) << "the attacker gets nothing";
}

// ─── RAD-162 Firelights' Hideout ────────────────────────────────────────────

TEST_F(RadianceB11Test, FirelightsHideout_NextUnitPlayedThisTurnHasAccelerate) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kFirelights);
    auto first = cardInHand(P1, kOtterpus);
    auto second = cardInHand(P1, kOtterpus);
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    engine_->testHook_cleanup();

    auto& pool = s().player(P1).rune_pool;
    pool.energy = 3;
    pool.power[static_cast<int>(Domain::Mind)] = 1;
    p1_.answers = {1};   // pay the [Accelerate] cost
    play(P1, first, LocationId{BaseLocation{P1}});
    engine_->testHook_cleanup();
    ASSERT_EQ(p1_.int_prompts.size(), 1u) << "Accelerate offered";
    EXPECT_FALSE(s().getObject(first).is_exhausted) << "entered ready";
    EXPECT_EQ(pool.energy, 0);

    pool.energy = 3;
    pool.power[static_cast<int>(Domain::Mind)] = 1;
    p1_.answers = {1};
    play(P1, second, LocationId{BaseLocation{P1}});
    EXPECT_EQ(p1_.int_prompts.size(), 1u) << "only the next unit";
    EXPECT_TRUE(s().getObject(second).is_exhausted);
}

TEST_F(RadianceB11Test, FirelightsHideout_GrantEndsWithTheTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kFirelights);
    auto unit = cardInHand(P1, kOtterpus);
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(unit).card_counters["__granted_accelerate"], 1);
    s().turn.turn_number += 1;
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(unit).card_counters.count("__granted_accelerate"), 0u);
}

TEST_F(RadianceB11Test, FirelightsHideout_OpponentsConquestDoesNotGrantMe) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kFirelights);
    auto mine = cardInHand(P1, kOtterpus);
    auto theirs = cardInHand(P2, kOtterpus);
    engine_->testHook_scoreConquer(P2, 0);
    engine_->testHook_runChain();
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(mine).card_counters.count("__granted_accelerate"), 0u);
    EXPECT_EQ(s().getObject(theirs).card_counters["__granted_accelerate"], 1);
}

// ─── RAD-164 Packed Amphitheater ────────────────────────────────────────────

TEST_F(RadianceB11Test, PackedAmphitheater_ChampionUnitsHereHavePlusOne) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kPacked);
    controlBf(0, P1);
    controlBf(1, P1);
    auto champ_here = boardObject(P1, kRivenBrokenBlade, 0);
    auto plain_here = boardObject(P1, kOtterpus, 0);
    auto champ_away = boardObject(P1, kRivenBrokenBlade, 1);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(champ_here).current_might, 5);
    EXPECT_EQ(s().getObject(plain_here).current_might, 2);
    EXPECT_EQ(s().getObject(champ_away).current_might, 4);
}

// ─── RAD-167 Neeko, Blending In ─────────────────────────────────────────────

TEST_F(RadianceB11Test, Neeko_Data) {
    makeEngine();
    const auto& d = card_db.get(kNeeko);
    EXPECT_EQ(d.card_type, CardType::Unit);
    EXPECT_EQ(d.super_type, SuperType::Champion);
    EXPECT_TRUE(d.domains.empty()) << "neutral";
    EXPECT_EQ(d.energy_cost, 6);
    EXPECT_EQ(d.power_cost, 1);
    EXPECT_EQ(d.might, 4);
    EXPECT_TRUE(d.hasTag("Neeko"));
}

TEST_F(RadianceB11Test, Legends_CarryTheirChampionTags) {
    makeEngine();
    EXPECT_TRUE(card_db.get(kAgony).hasTag("Evelynn"));
    EXPECT_TRUE(card_db.get(kExemplar).hasTag("Jarvan IV"));
    EXPECT_EQ(card_db.get(kAgony).card_type, CardType::Legend);
    EXPECT_EQ(card_db.get(kExemplar).card_type, CardType::Legend);
}

}  // namespace
}  // namespace riftbound::test
