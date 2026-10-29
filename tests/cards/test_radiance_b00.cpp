/// @file test_radiance_b00.cpp
/// Radiance batch 00 (RAD-001..015): Amateur Demolitionist and the Bomb gear
/// token, Banner of Conquest, Brawl, Lingering Animus, Scorched Earth, Rally
/// for War, War Scout, Runeblade Anvil, Shimmering Swimmer, Akali, Brash.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "cards/radiance_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

#include <deque>

namespace riftbound::test {
namespace {

constexpr CardDefId kDemolitionist = 985;
constexpr CardDefId kBanner        = 986;
constexpr CardDefId kBrawl         = 987;
constexpr CardDefId kAnimus        = 988;
constexpr CardDefId kScorched      = 989;
constexpr CardDefId kRally         = 990;
constexpr CardDefId kWarScout      = 991;
constexpr CardDefId kAnvil         = 992;
constexpr CardDefId kSwimmer       = 993;
constexpr CardDefId kAkali         = 994;
constexpr CardDefId kBomb          = kBombTokenCardDefId;
constexpr CardDefId kTheExile      = 961;  // a legend to carry granted abilities
constexpr CardDefId kOtterpus      = 840;  // 2-energy 2-Might unit, no play or exhaust text
constexpr CardDefId kRecruit       = 954;

/// Answers int prompts from a queue, picks objects from `wants` (in order,
/// then `want`) when offered, passes priority otherwise.
class B00Agent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    GameObjectId want = kInvalidId;
    std::deque<GameObjectId> wants;
    std::vector<std::vector<int>> int_prompts;
    int object_prompts = 0;
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
            ++object_prompts;
            // `wants` is consumed only by prompts that offer its front (rune
            // payments and other unrelated object prompts leave it alone).
            if (!wants.empty())
                for (const auto& i : legal)
                    if (!i.chosen_objects.empty() && i.chosen_objects.front() == wants.front()) {
                        wants.pop_front();
                        return i;
                    }
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class RadianceB00Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B00Agent p1_, p2_;
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
        // Plain legends; tests that need one with a card call setLegend again.
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
    /// A unit with `might` Might (an Otterpus with its base Might raised), so
    /// damage stays marked instead of killing it.
    GameObjectId bigUnit(PlayerId p, int at_bf, int might = 6) {
        auto id = boardObject(p, kOtterpus, at_bf);
        s().getObject(id).base_might = might;
        s().getObject(id).recomputeMight();
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
    /// Put a card on top of `p`'s Main Deck.
    GameObjectId deckTop(PlayerId p, CardDefId def_id) {
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
    void play(GameObjectId card, std::optional<LocationId> where) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
    void activate(GameObjectId source, int index, CardDefId granted = 0) {
        Intent i;
        i.type = IntentType::ActivateAbility;
        i.player = P1;
        i.ability_source = source;
        i.ability_index = index;
        i.granted_ability_def = granted;
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
    bool offersPlayAt(GameObjectId card, LocationId where) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::PlayCard && a.card == card &&
                a.play_location == std::optional<LocationId>{where})
                return true;
        return false;
    }
    bool offersActivation(GameObjectId source, int index, CardDefId granted = 0) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::ActivateAbility && a.ability_source == source &&
                a.ability_index == index && a.granted_ability_def == granted)
                return true;
        return false;
    }
    bool legendHasGrant(PlayerId p, CardDefId granter) {
        const auto& lg = s().getObject(s().player(p).legend_zone);
        for (const auto& r : lg.granted_abilities)
            if (r.source_def_id == granter) return true;
        return false;
    }
    bool banished(PlayerId p, GameObjectId id) {
        const auto& b = s().player(p).banishment;
        return std::find(b.begin(), b.end(), id) != b.end() &&
               s().getObject(id).zone == ZoneType::Banishment;
    }
    CardContext ctxFor(PlayerId p, GameObjectId source) {
        CardContext ctx{s(), events, engine_->testHook_executor(), p, source};
        ctx.registry = &card_registry;
        return ctx;
    }
    std::vector<GameObjectId> bombsOf(PlayerId p) {
        std::vector<GameObjectId> out;
        for (auto& [id, o] : s().objects)
            if (o.controller == p && o.card_def_id == kBomb) out.push_back(id);
        return out;
    }
    void controlBf(BattlefieldId bf, PlayerId p) { s().battlefields[bf].controller = p; }
    static bool onBf(const GameObject& o, BattlefieldId bf) {
        return o.battlefieldId() == std::optional<BattlefieldId>{bf};
    }
};

// ─── Card data ──────────────────────────────────────────────────────────────

TEST_F(RadianceB00Test, Data_KeywordsAndCosts) {
    EXPECT_TRUE(card_db.get(kBrawl).keywords.has(Keyword::Action));
    EXPECT_TRUE(card_db.get(kScorched).keywords.has(Keyword::Action));
    EXPECT_TRUE(card_db.get(kWarScout).keywords.has(Keyword::Accelerate));
    EXPECT_TRUE(card_db.get(kScorched).domains.empty());
    EXPECT_EQ(card_db.get(kScorched).power_cost, 1);
    EXPECT_EQ(card_db.get(kAkali).super_type, SuperType::Champion);
    EXPECT_EQ(card_db.get(kAkali).might, 5);
    EXPECT_EQ(card_db.get(kBanner).tags, std::vector<std::string>{"Device"});
    ASSERT_NE(card_registry.get(kBanner), nullptr);
    EXPECT_TRUE(card_registry.get(kBanner)->deploys());
    ASSERT_NE(card_registry.get(kBomb), nullptr) << "the Bomb token is registered with RAD-001";
    EXPECT_TRUE(card_registry.get(kBomb)->deploys());
    EXPECT_EQ(card_db.get(kBomb).super_type, SuperType::Token);
}

// ─── RAD-001 Amateur Demolitionist / Bomb token ─────────────────────────────

TEST_F(RadianceB00Test, Demolitionist_PlaysABombToTheChosenBattlefield) {
    makeEngine();
    s().player(P1).rune_pool.energy = 2;
    auto demo = cardInHand(P1, kDemolitionist);
    p1_.answers = {1};   // battlefield 1
    play(demo, LocationId{BaseLocation{P1}});
    auto bombs = bombsOf(P1);
    ASSERT_EQ(bombs.size(), 1u);
    const auto& bomb = s().getObject(bombs[0]);
    EXPECT_TRUE(onBf(bomb, 1));
    EXPECT_TRUE(bomb.isGear());
    EXPECT_TRUE(bomb.isToken());
    EXPECT_FALSE(bomb.is_exhausted);
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0, 1})) << "any battlefield";
    engine_->testHook_cleanup();
    EXPECT_TRUE(onBf(s().getObject(bombs[0]), 1)) << "[Deploy]: not recalled in cleanup";
}

TEST_F(RadianceB00Test, Bomb_DeathDealsTwoToAnEnemyThereAndSetsOffTheOtherBombsThere) {
    makeEngine();
    auto ctx = ctxFor(P1, kInvalidId);
    auto b1 = playBombToken(ctx, P1, 0);
    auto b2 = playBombToken(ctx, P1, 0);
    auto far = playBombToken(ctx, P1, 1);
    auto enemy = bigUnit(P2, 0);
    p1_.want = enemy;
    killWithDeathTrigger(ctx, b1);
    engine_->testHook_runChain();
    EXPECT_FALSE(s().getObject(b1).location.has_value());
    EXPECT_FALSE(s().getObject(b2).location.has_value()) << "the other Bomb there is killed";
    EXPECT_TRUE(onBf(s().getObject(far), 1)) << "a Bomb elsewhere is untouched";
    EXPECT_EQ(s().getObject(enemy).damage_marked, 4) << "2 from each Bomb";
}

TEST_F(RadianceB00Test, Bomb_KilledWhenTheOpponentHoldsThere) {
    makeEngine();
    auto ctx = ctxFor(P1, kInvalidId);
    auto bomb = playBombToken(ctx, P1, 1);
    engine_->testHook_scoreHold(P2, 1);
    EXPECT_FALSE(s().getObject(bomb).location.has_value()) << "[Deploy]";
}

// ─── RAD-002 Banner of Conquest ─────────────────────────────────────────────

TEST_F(RadianceB00Test, Banner_FriendlyAttackersHereGetAssault) {
    makeEngine();
    rune(P1, Domain::Fury);
    auto banner_card = cardInHand(P1, kBanner);
    EXPECT_FALSE(offersPlayAt(banner_card, LocationId{BaseLocation{P1}})) << "[Deploy]";
    boardObject(P1, kBanner, 0);
    auto here = boardObject(P1, kOtterpus, 0);
    auto there = boardObject(P1, kOtterpus, 1);
    // A combat at the Banner's battlefield: cleanup (CR 323.2) designates the
    // units there as attackers and clears the one elsewhere.
    auto& bf0 = s().battlefields[0];
    bf0.combat_in_progress = true;
    bf0.attacker = P1;
    bf0.defender = P2;
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(here).hasKeyword(Keyword::Assault));
    EXPECT_EQ(s().getObject(here).current_might, 3) << "2 + [Assault] while attacking";
    EXPECT_EQ(s().getObject(there).current_might, 2) << "not at the Banner's battlefield";
    bf0.combat_in_progress = false;
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(here).current_might, 2) << "Assault adds Might only while attacking";
}

// ─── RAD-004 Brawl ──────────────────────────────────────────────────────────

TEST_F(RadianceB00Test, Brawl_GivesAssaultEqualToMight) {
    makeEngine();
    auto& pool = s().player(P1).rune_pool;
    pool.energy = 3;
    pool.power[static_cast<int>(Domain::Chaos)] = 1;
    auto unit = bigUnit(P1, -1, 4);
    p1_.want = unit;
    auto brawl = cardInHand(P1, kBrawl);
    play(brawl, std::nullopt);
    auto& u = s().getObject(unit);
    EXPECT_TRUE(u.hasKeyword(Keyword::Assault));
    EXPECT_EQ(u.assault_value, 4);
    u.combat_designation = CombatDesignation::Attacker;
    u.recomputeMight();
    EXPECT_EQ(u.current_might, 8) << "4 + [Assault 4]";
}

// ─── RAD-005 Lingering Animus ───────────────────────────────────────────────

TEST_F(RadianceB00Test, Animus_EntersExhaustedWithoutAFriendlyDeath) {
    makeEngine();
    s().player(P1).rune_pool.energy = 2;
    auto enemy = boardObject(P2, kOtterpus, 0);
    engine_->testHook_executor().killObject(enemy);   // only an enemy died
    auto animus = cardInHand(P1, kAnimus);
    play(animus, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(animus).isAtBase());
    EXPECT_TRUE(s().getObject(animus).is_exhausted);
}

TEST_F(RadianceB00Test, Animus_EntersReadyAfterAFriendlyUnitDied) {
    makeEngine();
    s().player(P1).rune_pool.energy = 2;
    auto ally = boardObject(P1, kOtterpus, 0);
    engine_->testHook_executor().killObject(ally);
    auto animus = cardInHand(P1, kAnimus);
    play(animus, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(animus).isAtBase());
    EXPECT_FALSE(s().getObject(animus).is_exhausted);
}

TEST_F(RadianceB00Test, Animus_EntersReadyAfterAFriendlyGearDied) {
    makeEngine();
    s().player(P1).rune_pool.energy = 2;
    auto banner = boardObject(P1, kBanner, 0);
    auto ctx = ctxFor(P1, kInvalidId);
    killWithDeathTrigger(ctx, banner);
    ASSERT_FALSE(s().turn.any_unit_died_this_turn);
    auto animus = cardInHand(P1, kAnimus);
    play(animus, LocationId{BaseLocation{P1}});
    EXPECT_FALSE(s().getObject(animus).is_exhausted);
}

// ─── RAD-006 Scorched Earth ─────────────────────────────────────────────────

TEST_F(RadianceB00Test, ScorchedEarth_SplitsFiveAmongUnitsAtOneBattlefield) {
    makeEngine();
    s().player(P1).rune_pool.energy = 4;
    rune(P1, Domain::Fury);   // recycled for the [A]
    auto a = bigUnit(P2, 0);
    auto b = bigUnit(P2, 0);
    auto c = bigUnit(P2, 1);
    p1_.answers = {0};                 // battlefield 0
    p1_.wants = {a, b, a, b, a};       // one damage per pick
    auto spell = cardInHand(P1, kScorched);
    play(spell, std::nullopt);
    EXPECT_EQ(s().getObject(a).damage_marked, 3);
    EXPECT_EQ(s().getObject(b).damage_marked, 2);
    EXPECT_EQ(s().getObject(c).damage_marked, 0) << "a single battlefield";
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0, 1}));
}

TEST_F(RadianceB00Test, ScorchedEarth_OnlyBattlefieldsWithUnitsAreOffered) {
    makeEngine();
    s().player(P1).rune_pool.energy = 4;
    rune(P1, Domain::Fury);
    auto c = bigUnit(P2, 1);
    p1_.want = c;
    auto spell = cardInHand(P1, kScorched);
    play(spell, std::nullopt);
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{1}));
    EXPECT_EQ(s().getObject(c).damage_marked, 5) << "one unit takes all 5";
}

// ─── RAD-007 Rally for War ──────────────────────────────────────────────────

TEST_F(RadianceB00Test, Rally_PlaysThreeRecruitsToAnOccupiedEnemyBattlefield) {
    makeEngine();
    auto& pool = s().player(P1).rune_pool;
    pool.energy = 3;
    pool.power[static_cast<int>(Domain::Fury)] = 1;
    controlBf(0, P2);
    bigUnit(P2, 0, 9);
    p1_.answers = {0};
    auto rally = cardInHand(P1, kRally);
    play(rally, std::nullopt);
    int recruits = 0;
    for (auto& [id, o] : s().objects) {
        if (o.controller != P1 || o.card_def_id != kRecruit || !o.isToken()) continue;
        ++recruits;
        EXPECT_EQ(o.base_might, 1);
        const bool at0 = onBf(o, 0) ||
            o.last_location == std::optional<LocationId>{LocationId{BattlefieldLocation{0}}};
        EXPECT_TRUE(at0) << "played to the chosen battlefield";
    }
    EXPECT_EQ(recruits, 3);
}

TEST_F(RadianceB00Test, Rally_NeedsAnOccupiedEnemyBattlefield) {
    makeEngine();
    const Card* rally = card_registry.get(kRally);
    ASSERT_NE(rally, nullptr);
    EXPECT_FALSE(rally->hasLegalTargets(s(), P1)) << "no battlefield is controlled";
    controlBf(0, P2);
    EXPECT_FALSE(rally->hasLegalTargets(s(), P1)) << "enemy battlefield, but unoccupied";
    controlBf(1, P1);
    boardObject(P1, kOtterpus, 1);
    EXPECT_FALSE(rally->hasLegalTargets(s(), P1)) << "occupied, but mine";
    boardObject(P2, kOtterpus, 0);
    EXPECT_TRUE(rally->hasLegalTargets(s(), P1));
}

// ─── RAD-008 War Scout ──────────────────────────────────────────────────────

TEST_F(RadianceB00Test, WarScout_GivesAUnitTwoMightThisTurn) {
    makeEngine();
    s().player(P1).rune_pool.energy = 3;   // nothing left for [Accelerate]
    auto ally = boardObject(P1, kOtterpus, 0);
    p1_.want = ally;
    auto scout = cardInHand(P1, kWarScout);
    play(scout, LocationId{BaseLocation{P1}});
    EXPECT_EQ(s().getObject(ally).current_might, 4);
    EXPECT_EQ(s().getObject(ally).temp_might_bonus, 2);
    EXPECT_TRUE(s().getObject(scout).is_exhausted) << "no Accelerate paid";
}

// ─── RAD-012 Runeblade Anvil ────────────────────────────────────────────────

TEST_F(RadianceB00Test, Anvil_PlayBanishesTheTopCardWhichMayThenBePlayed) {
    makeEngine();
    auto legend = setLegend(P1, kTheExile);
    auto top = deckTop(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 1;
    auto anvil = cardInHand(P1, kAnvil);
    play(anvil, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(anvil).isAtBase());
    EXPECT_TRUE(banished(P1, top));
    EXPECT_TRUE(legendHasGrant(P1, kAnvil));
    EXPECT_FALSE(offersActivation(anvil, 1)) << "the play permission is not the Anvil's own ability";

    // Its costs are still paid: 2 Energy for the Otterpus.
    s().player(P1).rune_pool.energy = 2;
    EXPECT_TRUE(offersActivation(legend, 1, kAnvil));
    p1_.want = top;
    p1_.answers = {0};   // to base
    activate(legend, 1, kAnvil);
    EXPECT_TRUE(s().getObject(top).isAtBase());
    EXPECT_FALSE(banished(P1, top));
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0) << "paid its cost";
    EXPECT_FALSE(legendHasGrant(P1, kAnvil)) << "nothing left to play";
}

TEST_F(RadianceB00Test, Anvil_UnpaidCardStaysBanished) {
    makeEngine();
    auto legend = setLegend(P1, kTheExile);
    auto top = deckTop(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 1;
    play(cardInHand(P1, kAnvil), LocationId{BaseLocation{P1}});
    ASSERT_TRUE(banished(P1, top));
    activate(legend, 1, kAnvil);   // no Energy for it
    EXPECT_TRUE(banished(P1, top));
    EXPECT_TRUE(legendHasGrant(P1, kAnvil)) << "still playable this turn";
}

TEST_F(RadianceB00Test, Anvil_KillThisBanishesAnotherCard) {
    makeEngine();
    setLegend(P1, kTheExile);
    auto anvil = boardObject(P1, kAnvil);
    auto top = deckTop(P1, kOtterpus);
    auto& pool = s().player(P1).rune_pool;
    pool.energy = 2;
    pool.power[static_cast<int>(Domain::Chaos)] = 1;
    EXPECT_TRUE(offersActivation(anvil, 0));
    activate(anvil, 0);
    EXPECT_FALSE(s().getObject(anvil).location.has_value());
    EXPECT_EQ(s().getObject(anvil).zone, ZoneType::Trash);
    EXPECT_TRUE(banished(P1, top)) << "leaving the board banishes the top card";
    EXPECT_TRUE(legendHasGrant(P1, kAnvil));
}

TEST_F(RadianceB00Test, Anvil_BanishedOnTheOpponentsTurnStaysPlayableThroughMyNextTurn) {
    makeEngine();
    auto legend = setLegend(P1, kTheExile);
    auto anvil = boardObject(P1, kAnvil);
    auto top = deckTop(P1, kOtterpus);
    s().turn.turn_player = P2;
    auto ctx = ctxFor(P1, anvil);
    killWithDeathTrigger(ctx, anvil);
    engine_->testHook_runChain();
    ASSERT_TRUE(banished(P1, top));
    const int turn = s().turn.turn_number;
    EXPECT_TRUE(banishPlayAllowed(s(), P1, top, "__runeblade_anvil_play_until"));

    // End of the opponent's turn: "this turn" grants are gone.
    auto& lg = s().getObject(legend);
    lg.temp_granted_abilities.clear();
    lg.granted_abilities.clear();
    s().turn.turn_number = turn + 1;
    s().turn.turn_player = P1;
    events.emit(PhaseChangedEvent{TurnPhase::DrawPhase, TurnPhase::MainPhase, P1});
    engine_->testHook_runChain();
    EXPECT_TRUE(legendHasGrant(P1, kAnvil)) << "re-granted at the start of my Main Phase";
    EXPECT_TRUE(banishPlayAllowed(s(), P1, top, "__runeblade_anvil_play_until"));
    s().turn.turn_number = turn + 2;
    EXPECT_FALSE(banishPlayAllowed(s(), P1, top, "__runeblade_anvil_play_until"))
        << "expired after my next turn";
}

// ─── RAD-014 Shimmering Swimmer ─────────────────────────────────────────────

TEST_F(RadianceB00Test, Swimmer_KillAFriendlyGearToDealTwo) {
    makeEngine();
    s().player(P1).rune_pool.energy = 3;
    auto banner = boardObject(P1, kBanner, 0);
    auto enemy = bigUnit(P2, 1);
    p1_.answers = {1};    // pay the additional cost
    p1_.want = enemy;     // kill: the Banner is the only choice; damage: the enemy
    auto swimmer = cardInHand(P1, kSwimmer);
    play(swimmer, LocationId{BaseLocation{P1}});
    EXPECT_FALSE(s().getObject(banner).location.has_value()) << "killed as the additional cost";
    EXPECT_EQ(s().getObject(enemy).damage_marked, 2);
    EXPECT_TRUE(s().getObject(swimmer).isAtBase());
}

TEST_F(RadianceB00Test, Swimmer_DecliningMeansNoDamage) {
    makeEngine();
    s().player(P1).rune_pool.energy = 3;
    auto banner = boardObject(P1, kBanner, 0);
    auto enemy = bigUnit(P2, 1);
    p1_.answers = {0};
    p1_.want = enemy;
    play(cardInHand(P1, kSwimmer), LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().getObject(banner).location.has_value());
    EXPECT_EQ(s().getObject(enemy).damage_marked, 0);
}

TEST_F(RadianceB00Test, Swimmer_NotOfferedWithNothingToKill) {
    makeEngine();
    s().player(P1).rune_pool.energy = 3;
    auto enemy = bigUnit(P2, 1);
    p1_.want = enemy;
    play(cardInHand(P1, kSwimmer), LocationId{BaseLocation{P1}});
    EXPECT_TRUE(p1_.int_prompts.empty()) << "the kill is not offered";
    EXPECT_EQ(s().getObject(enemy).damage_marked, 0);
}

TEST_F(RadianceB00Test, Swimmer_KillingABombSetsItOff) {
    makeEngine();
    s().player(P1).rune_pool.energy = 3;
    auto ctx = ctxFor(P1, kInvalidId);
    auto bomb = playBombToken(ctx, P1, 1);
    auto enemy = bigUnit(P2, 1);
    p1_.answers = {1};
    p1_.want = enemy;
    play(cardInHand(P1, kSwimmer), LocationId{BaseLocation{P1}});
    EXPECT_FALSE(s().getObject(bomb).location.has_value());
    EXPECT_EQ(s().getObject(enemy).damage_marked, 4) << "2 from the Swimmer, 2 from the Bomb";
}

// ─── RAD-015 Akali, Brash ───────────────────────────────────────────────────

TEST_F(RadianceB00Test, Akali_FirstMoveAddsSecondMoveBanishes) {
    makeEngine();
    auto legend = setLegend(P1, kTheExile);
    auto akali = boardObject(P1, kAkali);
    auto top = deckTop(P1, kOtterpus);
    auto& ex = engine_->testHook_executor();
    auto& pool = s().player(P1).rune_pool;

    ex.moveToBattlefield(akali, 1);
    engine_->testHook_runChain();
    EXPECT_EQ(pool.energy, 1);
    EXPECT_EQ(pool.universal_power, 1);
    EXPECT_FALSE(banished(P1, top));

    ex.moveToBase(akali);
    engine_->testHook_runChain();
    EXPECT_EQ(pool.energy, 1) << "the second move adds nothing";
    EXPECT_TRUE(banished(P1, top));
    EXPECT_TRUE(legendHasGrant(P1, kAkali));
    EXPECT_FALSE(offersActivation(akali, 0)) << "the permission is granted to the legend";

    ex.moveToBattlefield(akali, 0);
    engine_->testHook_runChain();
    EXPECT_EQ(pool.energy, 1) << "third move: nothing";
    EXPECT_EQ(s().player(P1).banishment.size(), 1u);

    // Play the banished Otterpus with the [1][A] Akali added plus 1 more Energy.
    pool.energy = 2;
    p1_.want = top;
    p1_.answers = {0};
    activate(legend, 0, kAkali);
    EXPECT_TRUE(s().getObject(top).isAtBase());

    // A new turn: the first move adds again.
    s().turn.turn_number += 1;
    pool.energy = 0;
    pool.universal_power = 0;
    ex.moveToBase(akali);
    engine_->testHook_runChain();
    EXPECT_EQ(pool.energy, 1);
    EXPECT_EQ(pool.universal_power, 1);
}

}  // namespace
}  // namespace riftbound::test
