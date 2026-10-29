/// @file test_radiance_b01.cpp
/// Radiance batch 01 (RAD-016 .. RAD-026): Coalesced Energy, Detonation
/// Amplifier, Drakehound Packmaster, Firebrand Raider, Mordekaiser Twice
/// Slain, Ekko Revolutionary, Ziggs Short-Fused (+ the Bomb gear token),
/// Blossoming Dreamroot, Consult the Heavens, Entrenched Boar.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "cards/radiance_helpers.h"   // kBombTokenCardDefId (the shared Bomb gear token)
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kCoalesced      = 995;
constexpr CardDefId kAmplifier      = 996;
constexpr CardDefId kDrakehound     = 997;
constexpr CardDefId kFirebrand      = 998;
constexpr CardDefId kMordekaiser    = 999;
constexpr CardDefId kEkko           = 1000;
constexpr CardDefId kZiggs          = 1001;
constexpr CardDefId kDreamroot      = 1002;
constexpr CardDefId kConsult        = 1003;
constexpr CardDefId kBoar           = 1004;
constexpr CardDefId kBomb           = kBombTokenCardDefId;
constexpr CardDefId kOtterpus       = 840;  // 2-energy 2-Might unit, no play or exhaust text
constexpr CardDefId kWatchfulSentry = 96;   // "[Deathknell] — Draw 1."
constexpr CardDefId kConcertPoster  = 983;  // a [Deploy] gear

/// Answers yes/no and mode prompts from a queue of int answers, picks `want`
/// when offered objects, passes priority otherwise. Records every int prompt
/// it was shown.
class B01Agent : public AgentInterface {
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

class RadianceB01Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B01Agent p1_, p2_;
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
    /// A card on top of `p`'s Main Deck (main_deck.back() is the top).
    GameObjectId cardOnTopOfDeck(PlayerId p, CardDefId def_id) {
        auto id = cardInHand(p, def_id);
        auto& ps = s().player(p);
        ps.hand.erase(std::find(ps.hand.begin(), ps.hand.end(), id));
        s().getObject(id).zone = ZoneType::MainDeck;
        ps.main_deck.push_back(id);
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
    GameObjectId runeInDeck(PlayerId p, Domain d) {
        auto id = rune(p, d);
        auto& r = s().getObject(id);
        r.location = std::nullopt;
        r.zone = ZoneType::RuneDeck;
        s().player(p).rune_deck.push_back(id);
        return id;
    }
    void play(GameObjectId card, std::optional<LocationId> where, PlayerId who = P1) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = who;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
    }
    void power(PlayerId p, Domain d, int n) {
        s().player(p).rune_pool.power[static_cast<int>(d)] += n;
    }
    bool inZoneList(const std::vector<GameObjectId>& v, GameObjectId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
    void controlBf(BattlefieldId bf, PlayerId p) { s().battlefields[bf].controller = p; }
};

// ─── RAD-016 Coalesced Energy ───────────────────────────────────────────────

TEST_F(RadianceB01Test, CoalescedEnergy_EndOfTurnRecyclesAllRunesAndChannelsFourExhausted) {
    makeEngine();
    boardObject(P1, kCoalesced);
    auto r1 = rune(P1, Domain::Mind);
    auto r2 = rune(P1, Domain::Mind, /*exhausted=*/true);
    auto r3 = rune(P1, Domain::Calm);
    std::vector<GameObjectId> deck_runes;
    for (int i = 0; i < 4; ++i) deck_runes.push_back(runeInDeck(P1, Domain::Mind));
    engine_->testHook_endingStep();

    for (auto r : {r1, r2, r3}) {
        EXPECT_EQ(s().getObject(r).zone, ZoneType::RuneDeck) << "recycled";
        EXPECT_FALSE(s().getObject(r).location.has_value());
    }
    int on_board = 0;
    for (auto& [id, o] : s().objects)
        if (o.isRune() && o.controller == P1 && o.location.has_value()) {
            ++on_board;
            EXPECT_TRUE(o.is_exhausted) << "channeled exhausted";
        }
    EXPECT_EQ(on_board, 4);
    EXPECT_EQ(s().player(P1).rune_deck.size(), 3u);
}

TEST_F(RadianceB01Test, CoalescedEnergy_DoesNothingOnTheOpponentsTurn) {
    makeEngine();
    boardObject(P1, kCoalesced);
    auto r1 = rune(P1, Domain::Mind);
    runeInDeck(P1, Domain::Mind);
    s().turn.turn_player = P2;
    engine_->testHook_endingStep();
    EXPECT_TRUE(s().getObject(r1).location.has_value());
    EXPECT_EQ(s().player(P1).rune_deck.size(), 1u);
}

// ─── RAD-017 Detonation Amplifier ───────────────────────────────────────────

TEST_F(RadianceB01Test, Amplifier_KillAnotherFriendlyUnitToDraw) {
    makeEngine();
    s().player(P1).rune_pool.energy = 4;
    power(P1, Domain::Chaos, 1);
    auto fodder = boardObject(P1, kOtterpus);
    auto top = cardOnTopOfDeck(P1, kOtterpus);
    auto amp = cardInHand(P1, kAmplifier);
    p1_.answers = {1};   // yes, kill something
    p1_.want = fodder;
    play(amp, LocationId{BattlefieldLocation{1}});
    ASSERT_TRUE(s().getObject(amp).isAtBattlefield());
    EXPECT_FALSE(s().getObject(fodder).location.has_value()) << "killed as the cost";
    EXPECT_TRUE(inZoneList(s().player(P1).hand, top)) << "drew 1";
}

TEST_F(RadianceB01Test, Amplifier_DecliningKillsNothingAndDrawsNothing) {
    makeEngine();
    s().player(P1).rune_pool.energy = 4;
    power(P1, Domain::Chaos, 1);
    auto fodder = boardObject(P1, kOtterpus);
    cardOnTopOfDeck(P1, kOtterpus);
    auto amp = cardInHand(P1, kAmplifier);
    p1_.answers = {0};
    play(amp, LocationId{BattlefieldLocation{1}});
    EXPECT_TRUE(s().getObject(fodder).location.has_value());
    EXPECT_TRUE(s().player(P1).hand.empty());
}

TEST_F(RadianceB01Test, Amplifier_FriendlyDeathknellHereTriggersAnAdditionalTime) {
    makeEngine();
    boardObject(P1, kAmplifier, 0);
    auto sentry = boardObject(P1, kWatchfulSentry, 0);
    for (int i = 0; i < 3; ++i) cardOnTopOfDeck(P1, kOtterpus);
    engine_->testHook_executor().killObject(sentry);
    engine_->testHook_runChain();
    EXPECT_EQ(s().player(P1).hand.size(), 2u) << "Draw 1, twice";
}

TEST_F(RadianceB01Test, Amplifier_DeathknellElsewhereTriggersOnce) {
    makeEngine();
    boardObject(P1, kAmplifier, 0);
    auto sentry = boardObject(P1, kWatchfulSentry, 1);
    for (int i = 0; i < 3; ++i) cardOnTopOfDeck(P1, kOtterpus);
    engine_->testHook_executor().killObject(sentry);
    engine_->testHook_runChain();
    EXPECT_EQ(s().player(P1).hand.size(), 1u);
}

TEST_F(RadianceB01Test, Amplifier_HasDeploy) {
    makeEngine();
    EXPECT_TRUE(card_registry.get(kAmplifier)->deploys());
}

// ─── RAD-018 Drakehound Packmaster ──────────────────────────────────────────

TEST_F(RadianceB01Test, Drakehound_PlaysTheOpponentsTopCardForFree) {
    makeEngine();
    s().player(P1).rune_pool.energy = 9;
    power(P1, Domain::Fury, 2);
    auto stolen = cardOnTopOfDeck(P2, kOtterpus);
    auto hound = cardInHand(P1, kDrakehound);
    p1_.answers = {0};   // base
    play(hound, LocationId{BaseLocation{P1}});
    const auto& o = s().getObject(stolen);
    EXPECT_EQ(o.controller, P1);
    EXPECT_EQ(o.owner, P2);
    EXPECT_TRUE(o.isAtBase());
    EXPECT_FALSE(inZoneList(s().player(P2).main_deck, stolen));
    EXPECT_FALSE(inZoneList(s().player(P2).banishment, stolen));
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0) << "nothing more was paid";
}

TEST_F(RadianceB01Test, Drakehound_DeployGearGoesToABattlefield) {
    makeEngine();
    s().player(P1).rune_pool.energy = 9;
    power(P1, Domain::Fury, 2);
    auto poster = cardOnTopOfDeck(P2, kConcertPoster);
    auto hound = cardInHand(P1, kDrakehound);
    p1_.answers = {1};   // battlefield #1
    play(hound, LocationId{BaseLocation{P1}});
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0, 1})) << "the two battlefields";
    EXPECT_EQ(s().getObject(poster).location, std::optional<LocationId>{BattlefieldLocation{1}});
    EXPECT_EQ(s().getObject(poster).controller, P1);
}

TEST_F(RadianceB01Test, Drakehound_EmptyOpponentDeckDoesNothing) {
    makeEngine();
    s().player(P1).rune_pool.energy = 9;
    power(P1, Domain::Fury, 2);
    auto hound = cardInHand(P1, kDrakehound);
    play(hound, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().getObject(hound).isAtBase());
    EXPECT_TRUE(p1_.int_prompts.empty());
}

// ─── RAD-019 Firebrand Raider ───────────────────────────────────────────────

TEST_F(RadianceB01Test, Firebrand_EntersReadyAndReadiesEnemyUnits) {
    makeEngine();
    s().player(P1).rune_pool.energy = 6;
    power(P1, Domain::Fury, 1);
    auto enemy = boardObject(P2, kOtterpus, 0);
    s().getObject(enemy).is_exhausted = true;
    auto fb = cardInHand(P1, kFirebrand);
    play(fb, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(fb).isAtBase());
    EXPECT_FALSE(s().getObject(fb).is_exhausted) << "I enter ready";
    EXPECT_FALSE(s().getObject(enemy).is_exhausted) << "ready all enemy units";
}

TEST_F(RadianceB01Test, Firebrand_OtherUnitsEnterReady) {
    makeEngine();
    boardObject(P1, kFirebrand);
    // A unit Firebrand's controller plays.
    s().player(P1).rune_pool.energy = 2;
    auto mine = cardInHand(P1, kOtterpus);
    play(mine, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(mine).isAtBase());
    EXPECT_FALSE(s().getObject(mine).is_exhausted);
    // A unit the opponent plays on their turn.
    s().turn.turn_player = P2;
    s().player(P2).rune_pool.energy = 2;
    auto theirs = cardInHand(P2, kOtterpus);
    play(theirs, LocationId{BaseLocation{P2}}, P2);
    ASSERT_TRUE(s().getObject(theirs).isAtBase());
    EXPECT_FALSE(s().getObject(theirs).is_exhausted);
}

TEST_F(RadianceB01Test, Firebrand_UnitsDontReadyDuringTheAwakenPhase) {
    makeEngine();
    boardObject(P1, kFirebrand);
    auto tired = boardObject(P1, kOtterpus);
    auto fresh = boardObject(P1, kOtterpus);
    s().getObject(tired).is_exhausted = true;
    engine_->testHook_cleanup();             // end of turn 5: tired is exhausted

    // Turn 6 (P1): the Awaken Phase readies everything P1 controls.
    s().turn.turn_number = 6;
    s().turn.phase = TurnPhase::AwakenPhase;
    s().getObject(tired).is_exhausted = false;
    s().turn.phase = TurnPhase::BeginningStep;
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(tired).is_exhausted) << "did not ready during the Awaken Phase";
    EXPECT_FALSE(s().getObject(fresh).is_exhausted);

    // Readying it by an effect later this turn works.
    engine_->testHook_executor().readyObject(tired);
    engine_->testHook_cleanup();
    EXPECT_FALSE(s().getObject(tired).is_exhausted);
}

TEST_F(RadianceB01Test, Firebrand_ArrivingAfterTheAwakenPhaseLocksNothing) {
    makeEngine();
    auto tired = boardObject(P1, kOtterpus);
    s().getObject(tired).is_exhausted = true;
    engine_->testHook_cleanup();
    s().turn.turn_number = 6;
    s().getObject(tired).is_exhausted = false;   // readied in turn 6's Awaken Phase
    boardObject(P1, kFirebrand);                 // arrives afterwards
    engine_->testHook_cleanup();
    EXPECT_FALSE(s().getObject(tired).is_exhausted);
}

// ─── RAD-020 Mordekaiser, Twice Slain ───────────────────────────────────────

TEST_F(RadianceB01Test, Mordekaiser_KillsAFriendlyUnitToSurviveOncePerTurn) {
    makeEngine();
    auto mord = boardObject(P1, kMordekaiser, 0);
    auto fodder = boardObject(P1, kOtterpus);
    s().getObject(mord).damage_marked = 3;
    engine_->testHook_cleanup();
    p1_.want = fodder;
    engine_->testHook_executor().killObject(mord);
    engine_->testHook_runChain();
    const auto& m = s().getObject(mord);
    ASSERT_TRUE(m.location.has_value()) << "survived";
    EXPECT_TRUE(m.isAtBase()) << "recalled";
    EXPECT_TRUE(m.is_exhausted);
    EXPECT_EQ(m.damage_marked, 0) << "healed";
    EXPECT_FALSE(s().getObject(fodder).location.has_value()) << "the friendly unit was killed";

    // Once each turn: the next death this turn sticks.
    boardObject(P1, kOtterpus);
    engine_->testHook_cleanup();
    engine_->testHook_executor().killObject(mord);
    engine_->testHook_runChain();
    EXPECT_FALSE(s().getObject(mord).location.has_value());
}

TEST_F(RadianceB01Test, Mordekaiser_DiesWithNoOtherFriendlyUnit) {
    makeEngine();
    auto mord = boardObject(P1, kMordekaiser, 0);
    boardObject(P2, kOtterpus, 0);
    engine_->testHook_cleanup();
    engine_->testHook_executor().killObject(mord);
    engine_->testHook_runChain();
    EXPECT_FALSE(s().getObject(mord).location.has_value());
}

TEST_F(RadianceB01Test, Mordekaiser_AvailableAgainNextTurn) {
    makeEngine();
    auto mord = boardObject(P1, kMordekaiser, 0);
    boardObject(P1, kOtterpus);
    boardObject(P1, kOtterpus);
    engine_->testHook_cleanup();
    engine_->testHook_executor().killObject(mord);
    engine_->testHook_runChain();
    ASSERT_TRUE(s().getObject(mord).location.has_value());
    s().turn.turn_number += 1;
    engine_->testHook_cleanup();
    engine_->testHook_executor().killObject(mord);
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(mord).location.has_value()) << "a new turn";
}

TEST_F(RadianceB01Test, Mordekaiser_Keywords) {
    makeEngine();
    const auto& d = card_db.get(kMordekaiser);
    EXPECT_TRUE(d.keywords.has(Keyword::Accelerate));
    EXPECT_TRUE(d.keywords.has(Keyword::Assault));
    EXPECT_EQ(d.assault_value, 3);
}

// ─── RAD-021 Ekko, Revolutionary ────────────────────────────────────────────

TEST_F(RadianceB01Test, Ekko_BanishesAndReplaysHimselfToBaseReady) {
    makeEngine();
    auto ekko = boardObject(P1, kEkko, 0);
    auto& e = s().getObject(ekko);
    e.is_exhausted = true;
    e.damage_marked = 2;
    e.buff_count = 1;
    e.recomputeMight();
    auto fodder = cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 1;
    power(P1, Domain::Fury, 1);
    Intent act;
    act.type = IntentType::ActivateAbility;
    act.player = P1;
    act.ability_source = ekko;
    engine_->testHook_executeIntent(act);
    engine_->testHook_runChain();
    const auto& back = s().getObject(ekko);
    EXPECT_TRUE(back.isAtBase());
    EXPECT_FALSE(back.is_exhausted) << "I enter ready";
    EXPECT_EQ(back.damage_marked, 0);
    EXPECT_EQ(back.buff_count, 0) << "a new object";
    EXPECT_EQ(back.current_might, 4);
    EXPECT_FALSE(inZoneList(s().player(P1).banishment, ekko));
    EXPECT_TRUE(inZoneList(s().player(P1).trash, fodder)) << "Discard 1";
}

TEST_F(RadianceB01Test, Ekko_EntersReadyAndHasAnActionAbility) {
    makeEngine();
    const Card* c = card_registry.get(kEkko);
    EXPECT_TRUE(c->entersReadyOnPlay());
    EXPECT_TRUE(c->isActionAbility());
    const auto cost = c->getActivationCost();
    EXPECT_TRUE(cost.discard);
    EXPECT_EQ(cost.energy, 1);
    EXPECT_EQ(cost.power, 1);
    EXPECT_EQ(cost.power_domain, Domain::Fury);
}

// ─── RAD-023 Ziggs, Short-Fused + Bomb token ────────────────────────────────

TEST_F(RadianceB01Test, Ziggs_PlaysABombToABattlefield) {
    makeEngine();
    s().player(P1).rune_pool.energy = 5;
    power(P1, Domain::Fury, 1);
    auto ziggs = cardInHand(P1, kZiggs);
    p1_.answers = {1};   // battlefield #1
    play(ziggs, LocationId{BaseLocation{P1}});
    int bombs = 0;
    for (auto& [id, o] : s().objects)
        if (o.card_def_id == kBomb && o.location.has_value()) {
            ++bombs;
            EXPECT_TRUE(o.isGear());
            EXPECT_TRUE(o.isToken());
            EXPECT_EQ(o.controller, P1);
            EXPECT_EQ(o.location, std::optional<LocationId>{BattlefieldLocation{1}});
        }
    EXPECT_EQ(bombs, 1);
    EXPECT_TRUE(card_registry.get(kBomb)->deploys());
}

TEST_F(RadianceB01Test, Ziggs_FriendlyGearAbilitiesDealOneBonusDamage) {
    makeEngine();
    boardObject(P1, kZiggs);
    auto bomb = boardObject(P1, kBomb, 0);
    auto enemy = boardObject(P2, kDrakehound, 0);   // 9 Might: survives
    engine_->testHook_cleanup();                    // aura pass
    EXPECT_EQ(s().getObject(bomb).spell_bonus_damage, 1);
    p1_.want = enemy;
    auto& ex = engine_->testHook_executor();
    ex.killObject(bomb);
    // The engine has no gear death dispatch: queue the Bomb's death ability.
    ex.queueTriggeredAbility(bomb, P1, TriggerType::WhenIDie);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy).damage_marked, 3) << "2 + 1 Bonus Damage from Ziggs";
}

TEST_F(RadianceB01Test, Ziggs_BonusDamageEndsWhenHeDies) {
    makeEngine();
    auto ziggs = boardObject(P1, kZiggs);
    auto poster = boardObject(P1, kConcertPoster, 1);   // any friendly gear
    auto enemy_gear = boardObject(P2, kConcertPoster, 0);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(poster).spell_bonus_damage, 1);
    EXPECT_EQ(s().getObject(enemy_gear).spell_bonus_damage, 0) << "friendly gear only";
    engine_->testHook_executor().killObject(ziggs);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(poster).spell_bonus_damage, 0);
}

TEST_F(RadianceB01Test, Bomb_WithoutZiggsDealsTwo) {
    makeEngine();
    auto bomb = boardObject(P1, kBomb, 0);
    auto enemy = boardObject(P2, kDrakehound, 0);
    engine_->testHook_cleanup();
    p1_.want = enemy;
    auto& ex = engine_->testHook_executor();
    ex.killObject(bomb);
    ex.queueTriggeredAbility(bomb, P1, TriggerType::WhenIDie);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy).damage_marked, 2);
}

// ─── RAD-024 Blossoming Dreamroot ───────────────────────────────────────────

TEST_F(RadianceB01Test, Dreamroot_WinningACombatHereDraws) {
    makeEngine();
    boardObject(P1, kDreamroot, 0);
    auto top = cardOnTopOfDeck(P1, kOtterpus);
    s().player(P1).last_combat_bf = 0;
    s().player(P1).last_combat_attacker = true;
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    EXPECT_TRUE(inZoneList(s().player(P1).hand, top));
}

TEST_F(RadianceB01Test, Dreamroot_ConqueringWithoutACombatDoesNotDraw) {
    makeEngine();
    boardObject(P1, kDreamroot, 0);
    cardOnTopOfDeck(P1, kOtterpus);
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    EXPECT_TRUE(s().player(P1).hand.empty());
}

TEST_F(RadianceB01Test, Dreamroot_HasDeploy) {
    makeEngine();
    EXPECT_TRUE(card_registry.get(kDreamroot)->deploys());
}

// ─── RAD-025 Consult the Heavens ────────────────────────────────────────────

TEST_F(RadianceB01Test, Consult_GivesShieldTwoAndPredicts) {
    makeEngine();
    auto unit = boardObject(P1, kOtterpus, 0);
    auto bottom = cardOnTopOfDeck(P1, kOtterpus);
    auto top = cardOnTopOfDeck(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 1;
    auto spell = cardInHand(P1, kConsult);
    p1_.want = unit;
    play(spell, std::nullopt);
    const auto& u = s().getObject(unit);
    EXPECT_TRUE(u.hasKeyword(Keyword::Shield));
    EXPECT_EQ(u.shield_value, 2);
    // The agent's first option at the Predict is "recycle it": the top card
    // went to the bottom.
    const auto& deck = s().player(P1).main_deck;
    ASSERT_EQ(deck.size(), 2u);
    EXPECT_EQ(deck.front(), top);
    EXPECT_EQ(deck.back(), bottom);
}

TEST_F(RadianceB01Test, Consult_IsAReaction) {
    makeEngine();
    EXPECT_TRUE(card_db.get(kConsult).keywords.has(Keyword::Reaction));
}

// ─── RAD-026 Entrenched Boar ────────────────────────────────────────────────

TEST_F(RadianceB01Test, Boar_MayEnterReadyAndThenCantMove) {
    makeEngine();
    s().player(P1).rune_pool.energy = 2;
    auto boar = cardInHand(P1, kBoar);
    p1_.answers = {1};
    play(boar, LocationId{BaseLocation{P1}});
    const auto& b = s().getObject(boar);
    ASSERT_TRUE(b.isAtBase());
    EXPECT_FALSE(b.is_exhausted);
    auto it = b.card_counters.find("__cant_move_turn");
    ASSERT_NE(it, b.card_counters.end());
    EXPECT_EQ(it->second, s().turn.turn_number + 1);
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0, 1}));
}

TEST_F(RadianceB01Test, Boar_DecliningEntersExhausted) {
    makeEngine();
    s().player(P1).rune_pool.energy = 2;
    auto boar = cardInHand(P1, kBoar);
    p1_.answers = {0};
    play(boar, LocationId{BaseLocation{P1}});
    const auto& b = s().getObject(boar);
    ASSERT_TRUE(b.isAtBase());
    EXPECT_TRUE(b.is_exhausted);
    EXPECT_EQ(b.card_counters.count("__cant_move_turn"), 0u);
}

TEST_F(RadianceB01Test, Boar_ReadyAtABattlefieldCantMoveToBaseThisTurn) {
    makeEngine();
    controlBf(0, P1);
    s().player(P1).rune_pool.energy = 2;
    auto boar = cardInHand(P1, kBoar);
    p1_.answers = {1};
    play(boar, LocationId{BattlefieldLocation{0}});
    engine_->testHook_cleanup();
    ASSERT_TRUE(s().getObject(boar).isAtBattlefield());
    EXPECT_FALSE(s().getObject(boar).is_exhausted);
    EXPECT_TRUE(s().getObject(boar).cant_move_to_base);
    bool offered_to_base = false;
    for (const auto& a : engine_->generateLegalActions())
        if (a.type == IntentType::StandardMove && !a.units_to_move.empty() &&
            a.units_to_move.front() == boar && a.move_destination.has_value() &&
            std::holds_alternative<BaseLocation>(*a.move_destination))
            offered_to_base = true;
    EXPECT_FALSE(offered_to_base);
    s().turn.turn_number += 1;
    engine_->testHook_cleanup();
    EXPECT_FALSE(s().getObject(boar).cant_move_to_base) << "only this turn";
}

}  // namespace
}  // namespace riftbound::test
