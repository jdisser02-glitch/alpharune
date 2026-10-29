/// @file test_radiance_batch_b.cpp
/// Radiance batch B: Ruined Monastery's optional discard-and-channel on
/// conquer, Invigorating Bloom's play buff and Mighty-exhaust draw, Graves,
/// Blasting Through's attack/defend sweep and lack of combat damage, Elnuk
/// Herd's buff and recycle-if-from-face-down, Antagonistic Apparatus's ready
/// entry and ready Mech token.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kRuinedMonastery = 978;
constexpr CardDefId kBloom           = 979;
constexpr CardDefId kGraves          = 980;
constexpr CardDefId kElnukHerd       = 981;
constexpr CardDefId kApparatus       = 982;
constexpr CardDefId kOtterpus        = 840;  // 2-energy 2-Might unit, no play or exhaust text

/// Answers yes/no and mode prompts from a queue of int answers, picks `want`
/// when offered objects, passes priority otherwise. Records every int prompt
/// it was shown.
class BatchBAgent : public AgentInterface {
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

class RadianceBatchBTest : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    BatchBAgent p1_, p2_;
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
    GameObjectId runeInRuneDeck(PlayerId p, Domain d) {
        auto id = s().createObject();
        auto& r = s().getObject(id);
        r.owner = r.controller = p;
        r.card_type = CardType::Rune;
        r.name = "Rune";
        r.domains = {d};
        r.zone = ZoneType::RuneDeck;
        s().player(p).rune_deck.push_back(id);
        return id;
    }
    GameObjectId deckCard(PlayerId p) {
        auto top = s().createObject();
        s().getObject(top).owner = s().getObject(top).controller = p;
        s().getObject(top).zone = ZoneType::MainDeck;
        s().player(p).main_deck.push_back(top);
        return top;
    }
    bool inHand(PlayerId p, GameObjectId id) {
        const auto& h = s().player(p).hand;
        return std::find(h.begin(), h.end(), id) != h.end();
    }
    void play(GameObjectId card, std::optional<LocationId> where) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
    }
    std::vector<std::vector<GameObjectId>> activationTargets(GameObjectId source) {
        std::vector<std::vector<GameObjectId>> out;
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::ActivateAbility && a.ability_source == source)
                out.push_back(a.targets);
        return out;
    }
};

// ─── RAD-166 Ruined Monastery ───────────────────────────────────────────────

TEST_F(RadianceBatchBTest, RuinedMonastery_ConquerMayDiscardAndChannelExhausted) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kRuinedMonastery);
    auto keep = cardInHand(P1, kOtterpus);
    auto toss = cardInHand(P1, kOtterpus);
    auto rune = runeInRuneDeck(P1, Domain::Fury);
    p1_.answers = {1};   // yes
    p1_.want = toss;
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();

    ASSERT_FALSE(p1_.int_prompts.empty()) << "the \"you may\" was asked";
    EXPECT_EQ(s().getObject(toss).zone, ZoneType::Trash);
    EXPECT_TRUE(inHand(P1, keep));
    EXPECT_EQ(s().player(P1).hand.size(), 1u);
    ASSERT_TRUE(s().getObject(rune).location.has_value()) << "channeled";
    EXPECT_EQ(s().getObject(rune).zone, ZoneType::Base);
    EXPECT_TRUE(s().getObject(rune).is_exhausted) << "channeled exhausted";
    EXPECT_TRUE(s().player(P1).rune_deck.empty());
}

TEST_F(RadianceBatchBTest, RuinedMonastery_DeclineDoesNothing) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kRuinedMonastery);
    auto card = cardInHand(P1, kOtterpus);
    auto rune = runeInRuneDeck(P1, Domain::Fury);
    p1_.answers = {0};   // no
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    EXPECT_TRUE(inHand(P1, card));
    EXPECT_FALSE(s().getObject(rune).location.has_value());
    EXPECT_EQ(s().player(P1).rune_deck.size(), 1u);
}

TEST_F(RadianceBatchBTest, RuinedMonastery_EmptyHandStillChannels) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kRuinedMonastery);
    auto rune = runeInRuneDeck(P1, Domain::Fury);
    p1_.answers = {1};
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    ASSERT_TRUE(s().getObject(rune).location.has_value()) << "do as much as you can (CR 055)";
    EXPECT_TRUE(s().getObject(rune).is_exhausted);
}

// ─── RAD-035 Invigorating Bloom ─────────────────────────────────────────────

TEST_F(RadianceBatchBTest, Bloom_PlayGivesPlusThreeThenExhaustTheMightyUnitToDraw) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto otter = boardObject(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 3;
    auto bloom = cardInHand(P1, kBloom);
    p1_.want = otter;
    play(bloom, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(bloom).location.has_value());
    EXPECT_FALSE(s().getObject(bloom).is_exhausted) << "gear enters ready";
    EXPECT_EQ(s().getObject(otter).current_might, 5) << "2 + 3 this turn";

    // Now Mighty: it can pay the ability's cost.
    EXPECT_EQ(activationTargets(bloom),
              (std::vector<std::vector<GameObjectId>>{std::vector<GameObjectId>{otter}}));
    auto top = deckCard(P1);
    Intent act;
    act.type = IntentType::ActivateAbility;
    act.player = P1;
    act.ability_source = bloom;
    act.targets = {otter};
    engine_->testHook_executeIntent(act);
    EXPECT_TRUE(inHand(P1, top)) << "draw 1";
    EXPECT_TRUE(s().getObject(otter).is_exhausted) << "the cost: exhaust a Mighty unit";
    EXPECT_TRUE(s().getObject(bloom).is_exhausted) << "the cost: [E]";
    EXPECT_TRUE(activationTargets(bloom).empty());
}

TEST_F(RadianceBatchBTest, Bloom_OnlyReadyMightyUnitsCanPay) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto bloom = boardObject(P1, kBloom);
    auto small = boardObject(P1, kOtterpus);
    auto tired = boardObject(P1, kElnukHerd);     // 5 [M] but exhausted
    s().getObject(tired).is_exhausted = true;
    boardObject(P2, kElnukHerd);                  // an enemy Mighty unit
    EXPECT_TRUE(activationTargets(bloom).empty()) << "no ready friendly Mighty unit";

    engine_->testHook_executor().giveTemporaryMight(small, 3);
    EXPECT_EQ(activationTargets(bloom),
              (std::vector<std::vector<GameObjectId>>{std::vector<GameObjectId>{small}}));
}

// ─── RAD-085 Graves, Blasting Through ───────────────────────────────────────

TEST_F(RadianceBatchBTest, Graves_AttackDealsHisMightToAllEnemiesHere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto graves = boardObject(P1, kGraves, 0);
    auto ally = boardObject(P1, kOtterpus, 0);
    auto e1 = boardObject(P2, kOtterpus, 0);
    auto e2 = boardObject(P2, kOtterpus, 0);
    auto big = boardObject(P2, kElnukHerd, 0);
    auto elsewhere = boardObject(P2, kOtterpus, 1);
    events.emit(CombatStartedEvent{0, /*attacker=*/P1, /*defender=*/P2});
    engine_->testHook_runChain();

    EXPECT_FALSE(s().getObject(e1).location.has_value()) << "4 damage kills a 2 [M] unit";
    EXPECT_FALSE(s().getObject(e2).location.has_value());
    ASSERT_TRUE(s().getObject(big).location.has_value());
    EXPECT_EQ(s().getObject(big).damage_marked, 4);
    EXPECT_EQ(s().getObject(elsewhere).damage_marked, 0) << "only units here";
    EXPECT_EQ(s().getObject(ally).damage_marked, 0) << "only enemy units";
    EXPECT_EQ(s().getObject(graves).damage_marked, 0);
}

TEST_F(RadianceBatchBTest, Graves_DefendAlsoTriggersAndHeDealsNoCombatDamage) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto graves = boardObject(P1, kGraves, 0);
    auto attacker = boardObject(P2, kElnukHerd, 0);
    events.emit(CombatStartedEvent{0, /*attacker=*/P2, /*defender=*/P1});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(attacker).damage_marked, 4);

    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(graves).dealsNoCombatDamage());
}

// ─── RAD-081 Elnuk Herd ─────────────────────────────────────────────────────

TEST_F(RadianceBatchBTest, ElnukHerd_FromHandBuffsAnotherUnitAndStays) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto ally = boardObject(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 6;
    auto herd = cardInHand(P1, kElnukHerd);
    p1_.want = ally;
    play(herd, LocationId{BaseLocation{P1}});
    EXPECT_EQ(s().getObject(ally).current_might, 5) << "2 + 3 this turn";
    EXPECT_TRUE(s().getObject(herd).location.has_value()) << "not played from face down";
    EXPECT_EQ(s().getObject(herd).current_might, 5);
}

TEST_F(RadianceBatchBTest, ElnukHerd_PlayedFromFaceDownIsRecycled) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto ally = boardObject(P1, kOtterpus, 0);
    s().player(P1).rune_pool.energy = 6;
    auto herd = cardInHand(P1, kElnukHerd);
    // The marker GameEngine::executePlayFromHidden stamps on a face-down play.
    s().getObject(herd).card_counters["__played_from_facedown"] = 1;
    p1_.want = ally;
    play(herd, LocationId{BaseLocation{P1}});
    EXPECT_EQ(s().getObject(ally).current_might, 5);
    EXPECT_FALSE(s().getObject(herd).location.has_value());
    EXPECT_EQ(s().getObject(herd).zone, ZoneType::MainDeck);
    const auto& deck = s().player(P1).main_deck;
    ASSERT_FALSE(deck.empty());
    EXPECT_EQ(deck.front(), herd) << "recycled to the bottom";
    EXPECT_EQ(s().getObject(herd).card_counters["__played_from_facedown"], 0) << "consumed";
}

TEST_F(RadianceBatchBTest, ElnukHerd_DoesNotBuffItself) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    s().player(P1).rune_pool.energy = 6;
    auto herd = cardInHand(P1, kElnukHerd);
    p1_.want = herd;
    play(herd, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(herd).location.has_value());
    EXPECT_EQ(s().getObject(herd).current_might, 5) << "\"another\" friendly unit";
}

// ─── RAD-009 Antagonistic Apparatus ─────────────────────────────────────────

TEST_F(RadianceBatchBTest, Apparatus_EntersReadyAndPlaysAReadyMechHere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    s().player(P1).rune_pool.energy = 6;
    s().player(P1).rune_pool.universal_power = 1;   // its [A]
    auto app = cardInHand(P1, kApparatus);
    play(app, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(app).location.has_value());
    EXPECT_FALSE(s().getObject(app).is_exhausted) << "I enter ready";

    int mechs = 0;
    for (auto& [id, o] : s().objects) {
        if (!o.isUnit() || !o.isToken() || o.controller != P1) continue;
        ++mechs;
        EXPECT_EQ(o.name, "Mech");
        EXPECT_EQ(o.current_might, 3);
        EXPECT_TRUE(hasTag(o, "Mech"));
        EXPECT_FALSE(o.is_exhausted) << "a ready token";
        ASSERT_TRUE(o.location.has_value());
        EXPECT_EQ(*o.location, *s().getObject(app).location) << "here";
    }
    EXPECT_EQ(mechs, 1);
}

TEST_F(RadianceBatchBTest, Apparatus_IsDomainless) {
    makeEngine();
    const auto& def = card_db.get(kApparatus);
    EXPECT_TRUE(def.domains.empty());
    EXPECT_EQ(def.energy_cost, 6);
    EXPECT_EQ(def.power_cost, 1);
}

}  // namespace
}  // namespace riftbound::test
