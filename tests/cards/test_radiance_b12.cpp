/// @file test_radiance_b12.cpp
/// Radiance batch 12 (card text reconstructed from web summaries): Forbidden
/// Technique (ready a unit; replay from trash for [1][R][R], then banished),
/// Incorrigible Construct ([Show Off] a Mech -> enters ready), Whirling
/// Twirligig (three token abilities), Stoic Ferryman (move another unit here
/// to base), Orianna, Exquisite (no combat damage without a friendly gear
/// here), Hunting Blind ([Deploy], pull an enemy unit here, Shield 2 aura),
/// Strigian Oracle (Predict on exhaust), Trinket Peddler ([Disarm]),
/// Seraphine, Inspiring ([Disarm 2], draw on three simultaneous exhaustions),
/// Spirit of the Forge (return a gear: ready + Might), Armored Tusker (Shield
/// damage when it moves).

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kForbidden  = 1107;
constexpr CardDefId kConstruct  = 1108;
constexpr CardDefId kTwirligig  = 1109;
constexpr CardDefId kFerryman   = 1110;
constexpr CardDefId kOrianna    = 1111;
constexpr CardDefId kBlind      = 1112;
constexpr CardDefId kOracle     = 1113;
constexpr CardDefId kPeddler    = 1114;
constexpr CardDefId kSeraphine  = 1115;
constexpr CardDefId kSpirit     = 1116;
constexpr CardDefId kTusker     = 1117;
constexpr CardDefId kOtterpus   = 840;   // 2-energy 2-Might unit, no play or exhaust text

/// Answers yes/no and mode prompts from a queue of int answers, picks `want`
/// when offered objects, passes priority otherwise. Records every int prompt
/// it was shown.
class B12Agent : public AgentInterface {
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

class RadianceB12Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B12Agent p1_, p2_;
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
        u.assault_value = def.assault_value;
        u.shield_value = def.shield_value;
        u.deflect_value = def.deflect_value;
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
        c.shield_value = def.shield_value;
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
    GameObjectId deckCard(PlayerId p) {
        auto id = s().createObject();
        auto& c = s().getObject(id);
        c.owner = c.controller = p;
        c.name = "DeckCard";
        c.zone = ZoneType::MainDeck;
        s().player(p).main_deck.push_back(id);   // back() is the top
        return id;
    }
    void play(GameObjectId card, std::optional<LocationId> where) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
    }
    void move(std::vector<GameObjectId> units, BattlefieldId to) {
        engine_->testHook_executeIntent(
            Intent::standardMove(P1, std::move(units), BattlefieldLocation{to}));
        engine_->testHook_runChain();
    }
    void activate(GameObjectId source, int index, std::vector<GameObjectId> targets = {}) {
        Intent act;
        act.type = IntentType::ActivateAbility;
        act.player = P1;
        act.ability_source = source;
        act.ability_index = index;
        act.targets = std::move(targets);
        engine_->testHook_executeIntent(act);
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
    /// Units named `name` that `p` controls at their base.
    std::vector<GameObjectId> tokensAtBase(PlayerId p, const std::string& name) {
        std::vector<GameObjectId> out;
        for (auto& [id, o] : s().objects)
            if (o.isUnit() && o.controller == p && o.name == name && o.isAtBase()) out.push_back(id);
        return out;
    }
    static bool contains(const std::vector<GameObjectId>& v, GameObjectId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
};

// ─── RAD-010 Forbidden Technique ────────────────────────────────────────────

TEST_F(RadianceB12Test, ForbiddenTechnique_ReadiesAUnit) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto unit = boardObject(P1, kOtterpus);
    s().getObject(unit).is_exhausted = true;
    s().player(P1).rune_pool.energy = 2;
    auto ft = cardInHand(P1, kForbidden);
    p1_.want = unit;
    play(ft, std::nullopt);
    engine_->testHook_runChain();
    EXPECT_FALSE(s().getObject(unit).is_exhausted);
    EXPECT_EQ(s().getObject(ft).zone, ZoneType::Trash) << "played from hand: trashed as usual";
}

TEST_F(RadianceB12Test, ForbiddenTechnique_ReplayFromTrashForOneRedRedThenBanished) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto unit = boardObject(P1, kOtterpus);
    s().getObject(unit).is_exhausted = true;
    auto ft = cardInTrash(P1, kForbidden);
    engine_->testHook_cleanup();   // builds the trash-replay grant
    auto& pool = s().player(P1).rune_pool;
    pool.energy = 1;
    pool.power[static_cast<int>(Domain::Fury)] = 2;
    std::optional<Intent> replay;
    for (const auto& a : engine_->generateLegalActions())
        if (a.card == ft && a.play_source == Intent::PlaySource::Trash) replay = a;
    ASSERT_TRUE(replay.has_value()) << "playable from the trash for [1][R][R]";
    p1_.want = unit;
    engine_->testHook_executeIntent(*replay);
    engine_->testHook_runChain();
    EXPECT_FALSE(s().getObject(unit).is_exhausted);
    EXPECT_EQ(s().getObject(ft).zone, ZoneType::Banishment) << "then banish this";
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
    EXPECT_EQ(s().player(P1).rune_pool.power[static_cast<int>(Domain::Fury)], 0);
}

TEST_F(RadianceB12Test, ForbiddenTechnique_NoReplayWithoutTheFuryPower) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kOtterpus);
    auto ft = cardInTrash(P1, kForbidden);
    engine_->testHook_cleanup();
    s().player(P1).rune_pool.energy = 3;   // Energy only: [R][R] can't be paid
    for (const auto& a : engine_->generateLegalActions())
        EXPECT_FALSE(a.card == ft && a.play_source == Intent::PlaySource::Trash);
}

// ─── RAD-011 Incorrigible Construct ─────────────────────────────────────────

TEST_F(RadianceB12Test, Construct_EntersReadyShowingOffAMechFromHand) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    s().player(P1).rune_pool.energy = 4;
    auto construct = cardInHand(P1, kConstruct);
    cardInHand(P1, kConstruct);   // a Mech to reveal
    play(construct, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(construct).isAtBase());
    EXPECT_FALSE(s().getObject(construct).is_exhausted);
}

TEST_F(RadianceB12Test, Construct_EntersReadyShowingOffAFriendlyMech) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kConstruct);
    s().player(P1).rune_pool.energy = 4;
    auto construct = cardInHand(P1, kConstruct);
    play(construct, LocationId{BaseLocation{P1}});
    EXPECT_FALSE(s().getObject(construct).is_exhausted);
}

TEST_F(RadianceB12Test, Construct_EntersExhaustedWithoutAMech) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P2, kConstruct);          // an enemy Mech doesn't count
    cardInHand(P1, kOtterpus);            // not a Mech
    s().player(P1).rune_pool.energy = 4;
    auto construct = cardInHand(P1, kConstruct);
    play(construct, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().getObject(construct).is_exhausted);
}

// ─── RAD-022 Whirling Twirligig ─────────────────────────────────────────────

TEST_F(RadianceB12Test, Twirligig_ExhaustForAnExhaustedGold) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto tw = boardObject(P1, kTwirligig);
    activate(tw, 0);
    EXPECT_TRUE(s().getObject(tw).is_exhausted);
    int gold = 0;
    for (auto& [id, o] : s().objects)
        if (o.isGear() && o.controller == P1 && o.name == "Gold") {
            ++gold;
            EXPECT_TRUE(o.is_exhausted);
            EXPECT_EQ(o.card_def_id, kGoldGearCardDefId);
        }
    EXPECT_EQ(gold, 1);
}

TEST_F(RadianceB12Test, Twirligig_KillAFriendlyGearForARecruit) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto tw = boardObject(P1, kTwirligig);
    auto other = boardObject(P1, kTwirligig);
    p1_.want = other;
    activate(tw, 1);
    EXPECT_EQ(s().getObject(other).zone, ZoneType::Trash) << "the gear killed as the cost";
    EXPECT_TRUE(s().getObject(tw).location.has_value());
    auto recruits = tokensAtBase(P1, "Recruit");
    ASSERT_EQ(recruits.size(), 1u);
    EXPECT_EQ(s().getObject(recruits[0]).current_might, 1);
}

TEST_F(RadianceB12Test, Twirligig_KillAFriendlyUnitForAMech) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto tw = boardObject(P1, kTwirligig);
    auto a = boardObject(P1, kOtterpus);
    auto b = boardObject(P1, kOtterpus);
    boardObject(P2, kOtterpus);   // not a friendly unit
    int offers = 0;
    for (const auto& act : engine_->generateLegalActions())
        if (act.type == IntentType::ActivateAbility && act.ability_source == tw && act.ability_index == 2) {
            ++offers;
            ASSERT_EQ(act.targets.size(), 1u);
            EXPECT_TRUE(act.targets[0] == a || act.targets[0] == b);
        }
    EXPECT_EQ(offers, 2) << "one per friendly unit to kill";
    activate(tw, 2, {a});
    EXPECT_EQ(s().getObject(a).zone, ZoneType::Trash);
    EXPECT_TRUE(s().getObject(b).location.has_value());
    auto mechs = tokensAtBase(P1, "Mech");
    ASSERT_EQ(mechs.size(), 1u);
    EXPECT_EQ(s().getObject(mechs[0]).current_might, 3);
    EXPECT_TRUE(hasTag(s().getObject(mechs[0]), "Mech"));
}

// ─── RAD-031 Stoic Ferryman ─────────────────────────────────────────────────

TEST_F(RadianceB12Test, Ferryman_PlayedToABattlefieldMovesAnotherUnitThereToBase) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    controlBf(0, P1);
    auto ally = boardObject(P1, kOtterpus, 0);
    s().player(P1).rune_pool.energy = 4;
    auto ferry = cardInHand(P1, kFerryman);
    p1_.answers = {1};
    p1_.want = ally;
    play(ferry, LocationId{BattlefieldLocation{0}});
    ASSERT_TRUE(s().getObject(ferry).isAtBattlefield());
    EXPECT_TRUE(s().getObject(ally).isAtBase());
}

TEST_F(RadianceB12Test, Ferryman_PlayedToBaseDoesNothing) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    controlBf(0, P1);
    auto ally = boardObject(P1, kOtterpus, 0);
    s().player(P1).rune_pool.energy = 4;
    auto ferry = cardInHand(P1, kFerryman);
    p1_.answers = {1};
    p1_.want = ally;
    play(ferry, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().getObject(ally).isAtBattlefield());
    EXPECT_TRUE(p1_.int_prompts.empty()) << "only when played to a battlefield";
}

TEST_F(RadianceB12Test, Ferryman_DeclineKeepsTheUnitAndHasShield3) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    controlBf(0, P1);
    auto ally = boardObject(P1, kOtterpus, 0);
    s().player(P1).rune_pool.energy = 4;
    auto ferry = cardInHand(P1, kFerryman);
    p1_.answers = {0};
    play(ferry, LocationId{BattlefieldLocation{0}});
    EXPECT_TRUE(s().getObject(ally).isAtBattlefield());
    EXPECT_TRUE(card_db.get(kFerryman).keywords.has(Keyword::Shield));
    EXPECT_EQ(card_db.get(kFerryman).shield_value, 3);
}

// ─── RAD-042 Orianna, Exquisite ─────────────────────────────────────────────

TEST_F(RadianceB12Test, Orianna_NoCombatDamageWithoutAFriendlyGearHere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto ori = boardObject(P1, kOrianna);
    boardObject(P2, kTwirligig);   // an enemy gear (at its own base) doesn't help
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(ori).dealsNoCombatDamage());
}

TEST_F(RadianceB12Test, Orianna_DealsCombatDamageWithAFriendlyGearHere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto ori = boardObject(P1, kOrianna);
    auto gear = boardObject(P1, kTwirligig);
    engine_->testHook_cleanup();
    EXPECT_FALSE(s().getObject(ori).dealsNoCombatDamage());
    // The gear leaves: she stops dealing combat damage again.
    engine_->testHook_executor().killObject(gear);
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(ori).dealsNoCombatDamage());
}

TEST_F(RadianceB12Test, Orianna_AFriendlyGearAtHerBattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    controlBf(0, P1);
    auto ori = boardObject(P1, kOrianna, 0);
    boardObject(P1, kBlind, 0);   // [Deploy] gear: stays at the battlefield
    engine_->testHook_cleanup();
    EXPECT_FALSE(s().getObject(ori).dealsNoCombatDamage());
    EXPECT_EQ(card_db.get(kOrianna).shield_value, 1);
}

// ─── RAD-044 Hunting Blind ──────────────────────────────────────────────────

TEST_F(RadianceB12Test, HuntingBlind_DeploysAndMovesAnEnemyUnitHere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto enemy = boardObject(P2, kOtterpus);   // at P2's base
    auto& pool = s().player(P1).rune_pool;
    pool.energy = 4;
    pool.power[static_cast<int>(Domain::Calm)] = 2;
    auto blind = cardInHand(P1, kBlind);
    EXPECT_FALSE(offersPlayAt(blind, LocationId{BaseLocation{P1}})) << "[Deploy]: only to a battlefield";
    p1_.answers = {1};
    p1_.want = enemy;
    play(blind, LocationId{BattlefieldLocation{1}});
    ASSERT_TRUE(s().getObject(blind).isAtBattlefield());
    EXPECT_EQ(s().getObject(enemy).battlefieldId(), std::optional<BattlefieldId>{1});
}

TEST_F(RadianceB12Test, HuntingBlind_FriendlyUnitsHereHaveShield2) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    controlBf(0, P1);
    controlBf(1, P1);
    auto blind = boardObject(P1, kBlind, 1);
    auto here = boardObject(P1, kOtterpus, 1);
    auto there = boardObject(P1, kOtterpus, 0);
    engine_->testHook_cleanup();
    auto shieldFrom = [&](GameObjectId u) {
        int v = 0;
        for (const auto& ae : s().getObject(u).aura_effects)
            if (ae.source == blind && ae.keyword == Keyword::Shield) v += ae.keyword_value;
        return v;
    };
    EXPECT_EQ(shieldFrom(here), 2);
    EXPECT_TRUE(s().getObject(here).hasKeyword(Keyword::Shield));
    EXPECT_EQ(shieldFrom(there), 0) << "only units here";
    // A combat at the Blind's battlefield with P1 defending (CR 323.2 sets
    // the designation during cleanup).
    auto& bf1 = s().battlefields[1];
    bf1.combat_in_progress = true;
    bf1.attacker = P2;
    bf1.defender = P1;
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(here).current_might, 4) << "2 + [Shield 2] while defending";
}

// ─── RAD-053 Strigian Oracle ────────────────────────────────────────────────

TEST_F(RadianceB12Test, Oracle_PredictsWhenExhausted) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto bottom = deckCard(P1);
    auto top = deckCard(P1);
    auto oracle = boardObject(P1, kOracle);
    p1_.want = top;   // recycle the looked-at card
    move({oracle}, 1);
    const auto& deck = s().player(P1).main_deck;
    ASSERT_EQ(deck.size(), 2u);
    EXPECT_EQ(deck.front(), top) << "recycled to the bottom";
    EXPECT_EQ(deck.back(), bottom);
}

// ─── RAD-054 Trinket Peddler ────────────────────────────────────────────────

TEST_F(RadianceB12Test, Peddler_DisarmGivesAnEnemyUnitHereMinusOne) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto peddler = boardObject(P1, kPeddler, 0);
    auto here = boardObject(P2, kOtterpus, 0);
    auto there = boardObject(P2, kOtterpus, 1);
    p1_.want = here;
    engine_->testHook_executor().queueTriggeredAbility(peddler, P1, TriggerType::WhenIAttack);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(here).current_might, 1) << "2 - 1 this turn";
    EXPECT_EQ(s().getObject(there).current_might, 2);
}

// ─── RAD-065 Seraphine, Inspiring ───────────────────────────────────────────

TEST_F(RadianceB12Test, Seraphine_Disarm2) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto sera = boardObject(P1, kSeraphine, 0);
    auto enemy = boardObject(P2, kTusker, 0);
    p1_.want = enemy;
    engine_->testHook_executor().queueTriggeredAbility(sera, P1, TriggerType::WhenIAttack);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy).current_might, 2) << "4 - 2 this turn";
}

TEST_F(RadianceB12Test, Seraphine_ThreeUnitsExhaustedAtOnceDrawOnceEachTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kSeraphine);
    std::vector<GameObjectId> units;
    for (int i = 0; i < 6; ++i) units.push_back(boardObject(P1, kOtterpus));
    deckCard(P1);
    auto top = deckCard(P1);
    p1_.answers = {1, 1};
    move({units[0], units[1], units[2]}, 1);
    ASSERT_EQ(p1_.int_prompts.size(), 1u);
    EXPECT_EQ(p1_.int_prompts[0], (std::vector<int>{0, 1}));
    EXPECT_TRUE(contains(s().player(P1).hand, top));
    EXPECT_EQ(s().player(P1).hand.size(), 1u);

    // Once each turn: another three at once draws nothing.
    move({units[3], units[4], units[5]}, 1);
    EXPECT_EQ(p1_.int_prompts.size(), 1u);
    EXPECT_EQ(s().player(P1).hand.size(), 1u);
}

TEST_F(RadianceB12Test, Seraphine_TwoUnitsAreNotEnoughButSheCounts) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto sera = boardObject(P1, kSeraphine);
    auto a = boardObject(P1, kOtterpus);
    auto b = boardObject(P1, kOtterpus);
    auto c = boardObject(P1, kOtterpus);
    deckCard(P1);
    move({a, b}, 1);
    EXPECT_TRUE(p1_.int_prompts.empty()) << "two units: no draw offered";
    EXPECT_TRUE(s().player(P1).hand.empty());
    // Seraphine herself plus two others is three friendly units.
    p1_.answers = {1};
    move({sera, c}, 1);
    EXPECT_TRUE(p1_.int_prompts.empty()) << "still only two at once";
    s().getObject(a).is_exhausted = false;
    s().getObject(b).is_exhausted = false;
    s().getObject(sera).is_exhausted = false;
    move({sera, a, b}, 0);
    ASSERT_EQ(p1_.int_prompts.size(), 1u);
    EXPECT_EQ(s().player(P1).hand.size(), 1u);
}

TEST_F(RadianceB12Test, Seraphine_DecliningStillUsesTheTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kSeraphine);
    std::vector<GameObjectId> units;
    for (int i = 0; i < 6; ++i) units.push_back(boardObject(P1, kOtterpus));
    deckCard(P1);
    p1_.answers = {0};
    move({units[0], units[1], units[2]}, 1);
    EXPECT_EQ(p1_.int_prompts.size(), 1u);
    EXPECT_TRUE(s().player(P1).hand.empty());
    move({units[3], units[4], units[5]}, 1);
    EXPECT_EQ(p1_.int_prompts.size(), 1u) << "once each turn";
    // Next turn it works again.
    s().turn.turn_number += 1;
    for (auto u : units) s().getObject(u).is_exhausted = false;
    p1_.answers = {1};
    p1_.next = 0;
    move({units[0], units[1], units[2]}, 0);
    EXPECT_EQ(p1_.int_prompts.size(), 2u);
    EXPECT_EQ(s().player(P1).hand.size(), 1u);
}

// ─── RAD-066 Spirit of the Forge ────────────────────────────────────────────

TEST_F(RadianceB12Test, Spirit_ReturningAGearReadiesItAndAddsTheGearsCost) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto gear = boardObject(P1, kTwirligig);   // Energy cost 4
    auto& pool = s().player(P1).rune_pool;
    pool.energy = 6;
    pool.power[static_cast<int>(Domain::Mind)] = 1;
    auto spirit = cardInHand(P1, kSpirit);
    p1_.answers = {1};
    p1_.want = gear;
    play(spirit, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(spirit).isAtBase());
    EXPECT_EQ(s().getObject(gear).zone, ZoneType::Hand);
    EXPECT_TRUE(contains(s().player(P1).hand, gear));
    EXPECT_FALSE(s().getObject(spirit).is_exhausted);
    EXPECT_EQ(s().getObject(spirit).current_might, 8) << "4 + the gear's Energy cost 4";
}

TEST_F(RadianceB12Test, Spirit_DecliningKeepsTheGear) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto gear = boardObject(P1, kTwirligig);
    auto& pool = s().player(P1).rune_pool;
    pool.energy = 6;
    pool.power[static_cast<int>(Domain::Mind)] = 1;
    auto spirit = cardInHand(P1, kSpirit);
    p1_.answers = {0};
    play(spirit, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().getObject(gear).location.has_value());
    EXPECT_TRUE(s().getObject(spirit).is_exhausted);
    EXPECT_EQ(s().getObject(spirit).current_might, 4);
}

TEST_F(RadianceB12Test, Spirit_NoGearNoChoice) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P2, kTwirligig);   // an enemy gear can't be returned
    auto& pool = s().player(P1).rune_pool;
    pool.energy = 6;
    pool.power[static_cast<int>(Domain::Mind)] = 1;
    auto spirit = cardInHand(P1, kSpirit);
    play(spirit, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(p1_.int_prompts.empty());
    EXPECT_TRUE(s().getObject(spirit).is_exhausted);
}

// ─── RAD-078 Armored Tusker ─────────────────────────────────────────────────

TEST_F(RadianceB12Test, Tusker_MovingDealsItsShieldToAUnitAtABattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto tusker = boardObject(P1, kTusker);
    auto enemy = boardObject(P2, kTusker, 0);   // 4 Might
    p1_.answers = {1};
    p1_.want = enemy;
    move({tusker}, 1);
    EXPECT_EQ(s().getObject(enemy).damage_marked, 3) << "[Shield 3]";
    EXPECT_TRUE(s().getObject(enemy).location.has_value());
    EXPECT_TRUE(card_db.get(kTusker).keywords.has(Keyword::Tank));
}

TEST_F(RadianceB12Test, Tusker_DamageCountsShieldGainedThisTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto tusker = boardObject(P1, kTusker);
    auto enemy = boardObject(P2, kTusker, 0);
    engine_->testHook_executor().giveTemporaryKeyword(tusker, Keyword::Shield, 2);
    p1_.answers = {1};
    p1_.want = enemy;
    move({tusker}, 1);
    engine_->testHook_cleanup();
    EXPECT_FALSE(s().getObject(enemy).location.has_value()) << "5 damage kills the 4 [M] unit";
}

TEST_F(RadianceB12Test, Tusker_Decline) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto tusker = boardObject(P1, kTusker);
    auto enemy = boardObject(P2, kTusker, 0);
    p1_.answers = {0};
    p1_.want = enemy;
    move({tusker}, 1);
    EXPECT_EQ(s().getObject(enemy).damage_marked, 0);
    ASSERT_EQ(p1_.int_prompts.size(), 1u);
}

}  // namespace
}  // namespace riftbound::test
