/// @file test_riven_audit.cpp
/// Fidelity audit of the Riven, The Exile decks (and the Kai'Sa list they meet):
///   - The Exile's "Units you play to battlefields you control enter ready"
///     also covers a unit token played there (Emperor's Dais's Sand Soldier)
///     and a unit an effect plays there (Temporal Breach), and a hidden unit
///     played from face down;
///   - Blitzcrank, Impassive pulls the enemy unit its controller chooses;
///   - Meditation exhausts the friendly unit its controller chooses (Riven,
///     Broken Blade then triggers);
///   - Defy counters only a spell with Power cost no more than [A];
///   - Valley of Idols buffs the unit that was played, not another one there;
///   - Charm moves the enemy unit where its controller chooses (CR 355.4);
///   - Time Warp ends up banished only (not also in the trash).

#include "tests/cards/card_test_fixture.h"

#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "engine/effect_executor.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace riftbound::test {
namespace {

constexpr CardDefId kTheExile         = 961;
constexpr CardDefId kRivenBrokenBlade = 963;
constexpr CardDefId kOtterpus         = 840;   // [2] 2-Might unit, no text
constexpr CardDefId kPunchingPoro     = 794;
constexpr CardDefId kBlitzcrank       = 67;
constexpr CardDefId kMeditation       = 48;
constexpr CardDefId kDefy             = 45;
constexpr CardDefId kRebuke           = 172;   // [2] + 2 Power
constexpr CardDefId kFallingStar      = 29;    // [2] + 2 Power
constexpr CardDefId kHextechRay       = 9;     // [1] + 1 Power
constexpr CardDefId kValleyOfIdols    = 774;
constexpr CardDefId kCharm            = 43;
constexpr CardDefId kEmperorsDais     = 521;
constexpr CardDefId kTideturner       = 199;
constexpr CardDefId kTimeWarp         = 122;

// ─── Direct (card-level) fixture ────────────────────────────────────────────

class RivenAuditTest : public CardTestFixture {
protected:
    GameObjectId addLegend(PlayerId owner, CardDefId def_id) {
        auto id = state.createObject();
        auto& obj = state.getObject(id);
        obj.owner = obj.controller = owner;
        obj.card_def_id = def_id;
        obj.name = card_db.get(def_id).name;
        obj.card_type = CardType::Legend;
        obj.zone = ZoneType::LegendZone;
        state.player(owner).legend_zone = id;
        return id;
    }
    GameObjectId setBattlefieldCard(BattlefieldId bf, CardDefId def_id) {
        auto id = state.createObject();
        auto& o = state.getObject(id);
        o.card_type = CardType::Battlefield;
        o.card_def_id = def_id;
        o.name = card_db.get(def_id).name;
        o.zone = ZoneType::BattlefieldZone;
        state.battlefields[bf].card_object_id = id;
        return id;
    }
    GameObjectId findToken(PlayerId p, const std::string& name) {
        for (const auto& [id, o] : state.objects)
            if (o.isToken() && o.controller == p && o.name == name && o.location.has_value())
                return id;
        return kInvalidId;
    }
    static bool isMayPrompt(const std::vector<Intent>& legal) {
        return legal.size() == 2 && legal[0].chosen_objects.empty() &&
               legal[0].chosen_value.has_value() && legal[1].chosen_value.has_value() &&
               *legal[1].chosen_value == 1;
    }
    static Intent pickYes(const std::vector<Intent>& legal) {
        for (const auto& i : legal)
            if (i.chosen_value.has_value() && *i.chosen_value == 1) return i;
        return legal.front();
    }
    static Intent pickValue(const std::vector<Intent>& legal, int v) {
        for (const auto& i : legal)
            if (i.chosen_value.has_value() && *i.chosen_value == v) return i;
        return legal.front();
    }
    std::vector<GameObjectId> offered;   // objects offered by the last object prompt
    Intent pickObject(const std::vector<Intent>& legal, GameObjectId id) {
        offered.clear();
        for (const auto& i : legal)
            if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects[0]);
        for (const auto& i : legal)
            if (!i.chosen_objects.empty() && i.chosen_objects[0] == id) return i;
        return legal.front();
    }
    bool wasOffered(GameObjectId id) const {
        return std::find(offered.begin(), offered.end(), id) != offered.end();
    }
};

// ── Riven, The Exile: every way a unit is played to a controlled battlefield ──

TEST_F(RivenAuditTest, TheExile_EmperorsDaisSandSoldierEntersReady) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    addLegend(P1, kTheExile);
    auto dais = setBattlefieldCard(0, kEmperorsDais);
    state.battlefields[0].controller = P1;   // just conquered
    auto unit = addUnit(P1, kOtterpus, 0, 0);
    addRune(P1, Domain::Calm);

    driveResumableTrigger(kEmperorsDais, P1, dais,
        [&](const std::vector<Intent>& legal) {
            if (isMayPrompt(legal)) return pickYes(legal);
            return pickObject(legal, unit);
        }, exec, TriggerType::WhenYouConquerHere);

    EXPECT_TRUE(inHand(P1, unit)) << "the cost: return a unit here";
    auto soldier = findToken(P1, "Sand Soldier");
    ASSERT_NE(soldier, kInvalidId);
    EXPECT_EQ(state.getObject(soldier).battlefieldId(), std::optional<BattlefieldId>(0));
    EXPECT_FALSE(state.getObject(soldier).is_exhausted)
        << "a unit token played to a battlefield P1 controls enters ready";
}

TEST_F(RivenAuditTest, TheExile_NoLegendSandSoldierEntersExhausted) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto dais = setBattlefieldCard(0, kEmperorsDais);
    state.battlefields[0].controller = P1;
    auto unit = addUnit(P1, kOtterpus, 0, 0);
    addRune(P1, Domain::Calm);
    driveResumableTrigger(kEmperorsDais, P1, dais,
        [&](const std::vector<Intent>& legal) {
            if (isMayPrompt(legal)) return pickYes(legal);
            return pickObject(legal, unit);
        }, exec, TriggerType::WhenYouConquerHere);
    auto soldier = findToken(P1, "Sand Soldier");
    ASSERT_NE(soldier, kInvalidId);
    EXPECT_TRUE(state.getObject(soldier).is_exhausted);
}

TEST_F(RivenAuditTest, TheExile_UnitAnEffectPlaysToAControlledBattlefieldEntersReady) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    addLegend(P1, kTheExile);
    state.battlefields[0].controller = P1;
    auto to_bf0 = addToHand(P1, kOtterpus);
    auto to_bf1 = addToHand(P1, kOtterpus);
    auto& hand = state.player(P1).hand;
    hand.clear();

    // Temporal Breach / Fizz / Last Rites put units into play through here.
    exec.playIgnoringCost(P1, to_bf0, LocationId{BattlefieldLocation{0}});
    exec.playIgnoringCost(P1, to_bf1, LocationId{BattlefieldLocation{1}});

    ASSERT_EQ(state.getObject(to_bf0).battlefieldId(), std::optional<BattlefieldId>(0));
    EXPECT_FALSE(state.getObject(to_bf0).is_exhausted) << "BF0 is controlled";
    EXPECT_TRUE(state.getObject(to_bf1).is_exhausted) << "BF1 is not";
}

// ── Defy: "costs no more than [4] and no more than [A]" ──────────────────────

TEST_F(RivenAuditTest, Defy_CannotCounterASpellWithTwoPower) {
    EffectExecutor exec(state, events, card_db);
    auto defy = addToHand(P1, kDefy);
    for (CardDefId two_power : {kRebuke, kFallingStar}) {
        state.chain.items.clear();
        auto target = pushSpellOnChain(P2, two_power);
        invokeOnResolve(defy, kDefy, P1, {}, exec);
        EXPECT_EQ(state.chain.items.size(), 1u) << card_db.get(two_power).name;
        EXPECT_FALSE(inTrash(P2, target));
    }
}

TEST_F(RivenAuditTest, Defy_CountersASpellWithOnePower) {
    EffectExecutor exec(state, events, card_db);
    auto target = pushSpellOnChain(P2, kHextechRay);
    auto defy = addToHand(P1, kDefy);
    invokeOnResolve(defy, kDefy, P1, {}, exec);
    EXPECT_TRUE(state.chain.items.empty());
    EXPECT_TRUE(inTrash(P2, target));
}

// ── Blitzcrank, Impassive ────────────────────────────────────────────────────

TEST_F(RivenAuditTest, Blitzcrank_PullsTheChosenEnemyUnit) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto blitz = addUnit(P1, kBlitzcrank, 0, 0);
    auto enemy_here = addUnit(P2, kPunchingPoro, 0, 0);
    auto enemy_bf1 = addUnit(P2, kPunchingPoro, 0, 1);
    auto enemy_base = addUnit(P2, kPunchingPoro, 0, -1);
    auto friendly = addUnit(P1, kPunchingPoro, 0, 1);

    driveResumableTrigger(kBlitzcrank, P1, blitz,
        [&](const std::vector<Intent>& legal) {
            if (isMayPrompt(legal)) return pickYes(legal);
            return pickObject(legal, enemy_base);
        }, exec, TriggerType::WhenYouPlayMe);

    EXPECT_TRUE(wasOffered(enemy_bf1));
    EXPECT_TRUE(wasOffered(enemy_base));
    EXPECT_FALSE(wasOffered(enemy_here)) << "already here";
    EXPECT_FALSE(wasOffered(friendly)) << "an enemy unit";
    EXPECT_EQ(state.getObject(enemy_base).battlefieldId(), std::optional<BattlefieldId>(0));
    EXPECT_EQ(state.getObject(enemy_bf1).battlefieldId(), std::optional<BattlefieldId>(1));
}

TEST_F(RivenAuditTest, Blitzcrank_DecliningMovesNothing) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto blitz = addUnit(P1, kBlitzcrank, 0, 0);
    auto enemy = addUnit(P2, kPunchingPoro, 0, 1);
    driveResumableTrigger(kBlitzcrank, P1, blitz,
        [](const std::vector<Intent>& legal) { return legal.front(); },   // "No"
        exec, TriggerType::WhenYouPlayMe);
    EXPECT_EQ(state.getObject(enemy).battlefieldId(), std::optional<BattlefieldId>(1));
}

// ── Meditation (card level) ──────────────────────────────────────────────────

TEST_F(RivenAuditTest, Meditation_ExhaustsTheChosenUnitAndDrawsTwo) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto a = addUnit(P1, kOtterpus, 0, -1);
    auto b = addUnit(P1, kOtterpus, 0, 0);
    for (int i = 0; i < 3; ++i) addToDeck(P1, kOtterpus);
    auto med = addToHand(P1, kMeditation);
    state.player(P1).hand.clear();
    (void)med;

    driveResumable(kMeditation, P1, med,
        [&](const std::vector<Intent>& legal) {
            if (isMayPrompt(legal)) return pickYes(legal);
            return pickObject(legal, b);
        }, exec);

    EXPECT_TRUE(wasOffered(a));
    EXPECT_TRUE(state.getObject(b).is_exhausted) << "the chosen unit pays";
    EXPECT_FALSE(state.getObject(a).is_exhausted);
    EXPECT_EQ(handSize(P1), 2);
}

TEST_F(RivenAuditTest, Meditation_DecliningDrawsOne) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto a = addUnit(P1, kOtterpus, 0, -1);
    for (int i = 0; i < 3; ++i) addToDeck(P1, kOtterpus);
    auto med = addToHand(P1, kMeditation);
    state.player(P1).hand.clear();
    driveResumable(kMeditation, P1, med,
        [](const std::vector<Intent>& legal) { return legal.front(); }, exec);   // "No"
    EXPECT_FALSE(state.getObject(a).is_exhausted);
    EXPECT_EQ(handSize(P1), 1);
}

// ── Valley of Idols ──────────────────────────────────────────────────────────

TEST_F(RivenAuditTest, ValleyOfIdols_BuffsThePlayedUnitNotAnotherOneThere) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto valley = setBattlefieldCard(0, kValleyOfIdols);
    auto played = addUnit(P1, kOtterpus, 0, 0);      // lower object id
    auto already = addUnit(P1, kOtterpus, 0, 0);     // higher object id, already there
    addRune(P1, Domain::Calm);

    driveResumableTrigger(kValleyOfIdols, P1, valley, pickYes, exec,
                          TriggerType::WhenYouPlayAUnit, /*subject=*/played);

    EXPECT_EQ(state.getObject(played).buff_count, 1);
    EXPECT_EQ(state.getObject(already).buff_count, 0);
    EXPECT_EQ(readyRuneCount(P1), 0) << "paid [1]";
}

// ── Charm ────────────────────────────────────────────────────────────────────

TEST_F(RivenAuditTest, Charm_MovesTheEnemyUnitToTheChosenBattlefield) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto enemy = addUnit(P2, kPunchingPoro, 0, -1);   // in its base
    auto charm = addToHand(P1, kCharm);
    bool offered_base = false;
    driveResumable(kCharm, P1, charm,
        [&](const std::vector<Intent>& legal) {
            if (!legal.front().chosen_objects.empty()) return pickObject(legal, enemy);
            for (const auto& i : legal)
                if (i.chosen_value.has_value() && *i.chosen_value == 0) offered_base = true;
            return pickValue(legal, 2);   // 1 + battlefield 1
        }, exec);
    EXPECT_FALSE(offered_base) << "CR 355.4.a: not where it already is";
    EXPECT_EQ(state.getObject(enemy).battlefieldId(), std::optional<BattlefieldId>(1));
}

TEST_F(RivenAuditTest, Charm_CanStillMoveAUnitHome) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto enemy = addUnit(P2, kPunchingPoro, 0, 0);
    auto charm = addToHand(P1, kCharm);
    driveResumable(kCharm, P1, charm,
        [&](const std::vector<Intent>& legal) {
            if (!legal.front().chosen_objects.empty()) return pickObject(legal, enemy);
            return pickValue(legal, 0);
        }, exec);
    EXPECT_TRUE(state.getObject(enemy).isAtBase());
}

// ── Time Warp ("Banish this") ────────────────────────────────────────────────

TEST_F(RivenAuditTest, TimeWarp_IsBanishedNotTrashed) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto warp = addToHand(P1, kTimeWarp);
    FirstChoiceAgent agent;
    driveThroughChain(warp, P1, {}, agent, exec);
    EXPECT_EQ(state.player(P1).additional_turns.size(), 1u);
    EXPECT_EQ(state.getObject(warp).zone, ZoneType::Banishment);
    const auto& bz = state.player(P1).banishment;
    EXPECT_EQ(std::count(bz.begin(), bz.end(), warp), 1);
    EXPECT_FALSE(inTrash(P1, warp));
}

// ─── Engine-driven fixture (finalization, legend, hidden plays) ─────────────

/// Answers int prompts from `answers`, object prompts from `picks`, passes
/// priority otherwise; records every prompt it was shown.
class AuditAgent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next_answer = 0;
    std::vector<GameObjectId> picks;
    size_t next_pick = 0;
    std::vector<std::vector<int>> int_prompts;
    std::vector<std::vector<GameObjectId>> obj_prompts;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice && f.chosen_value.has_value()) {
            std::vector<int> o;
            for (const auto& i : legal) o.push_back(i.chosen_value.value_or(-1));
            int_prompts.push_back(o);
            if (next_answer < answers.size()) {
                const int a = answers[next_answer++];
                for (const auto& i : legal) if (i.chosen_value == a) return i;
            }
            return f;
        }
        if (f.type == IntentType::MakeChoice && !f.chosen_objects.empty()) {
            std::vector<GameObjectId> o;
            for (const auto& i : legal)
                if (!i.chosen_objects.empty()) o.push_back(i.chosen_objects.front());
            obj_prompts.push_back(o);
            if (next_pick < picks.size()) {
                const GameObjectId want = picks[next_pick++];
                for (const auto& i : legal)
                    if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            }
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class RivenAuditEngineTest : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    AuditAgent p1_, p2_;
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
    void stamp(GameObject& u, PlayerId p, CardDefId def_id) {
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
        stamp(s().getObject(id), p, def_id);
        s().getObject(id).zone = ZoneType::Hand;
        s().player(p).hand.push_back(id);
        return id;
    }
    void deckCards(PlayerId p, int n) {
        for (int i = 0; i < n; ++i) {
            auto top = s().createObject();
            s().getObject(top).owner = s().getObject(top).controller = p;
            s().getObject(top).zone = ZoneType::MainDeck;
            s().player(p).main_deck.push_back(top);
        }
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
};

TEST_F(RivenAuditEngineTest, Meditation_ChosenRivenPaysTheCostAndTriggers) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto riven = boardObject(P1, kRivenBrokenBlade);
    auto otter = boardObject(P1, kOtterpus);
    deckCards(P1, 3);
    auto med = cardInHand(P1, kMeditation);
    s().player(P1).rune_pool.energy = 2;
    p1_.answers = {1, 0};      // pay the additional cost; Riven: "Buff me"
    p1_.picks = {riven};       // ... by exhausting Riven

    play(med);

    ASSERT_FALSE(p1_.obj_prompts.empty());
    const auto& offered = p1_.obj_prompts.front();
    EXPECT_NE(std::find(offered.begin(), offered.end(), otter), offered.end());
    EXPECT_TRUE(s().getObject(riven).is_exhausted);
    EXPECT_FALSE(s().getObject(otter).is_exhausted);
    EXPECT_EQ(s().getObject(riven).buff_count, 1) << "Riven's exhaust trigger resolved";
    EXPECT_EQ(s().player(P1).hand.size(), 2u) << "paid: draw 2";
}

TEST_F(RivenAuditEngineTest, Blitzcrank_PlayedToAControlledBattlefieldEntersReadyAndPullsTheChosenUnit) {
    makeEngine();
    setLegend(P1, kTheExile); setLegend(P2);
    s().battlefields[0].controller = P1;
    boardObject(P1, kOtterpus, 0);
    auto enemy_bf1 = boardObject(P2, kOtterpus, 1);
    auto enemy_base = boardObject(P2, kOtterpus);
    auto blitz = cardInHand(P1, kBlitzcrank);
    s().player(P1).rune_pool.energy = 5;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Calm)] = 1;
    p1_.answers = {1};
    p1_.picks = {enemy_base};

    play(blitz, LocationId{BattlefieldLocation{0}});

    ASSERT_EQ(s().getObject(blitz).battlefieldId(), std::optional<BattlefieldId>(0));
    EXPECT_FALSE(s().getObject(blitz).is_exhausted) << "The Exile";
    EXPECT_EQ(s().getObject(enemy_base).battlefieldId(), std::optional<BattlefieldId>(0));
    EXPECT_EQ(s().getObject(enemy_bf1).battlefieldId(), std::optional<BattlefieldId>(1));
}

TEST_F(RivenAuditEngineTest, TheExile_HiddenUnitPlayedFromFaceDownEntersReady) {
    makeEngine();
    setLegend(P1, kTheExile); setLegend(P2);
    s().battlefields[0].controller = P1;
    auto tide = cardInHand(P1, kTideturner);
    auto& ps = s().player(P1);
    ps.hand.erase(std::find(ps.hand.begin(), ps.hand.end(), tide));
    auto& t = s().getObject(tide);
    t.is_hidden = true;
    t.hidden_at = 0;
    t.hidden_on_turn = 4;
    t.zone = ZoneType::FacedownZone;
    s().battlefields[0].facedown.push_back(tide);

    Intent play;
    play.type = IntentType::PlayReaction;
    play.player = P1;
    play.card = tide;
    engine_->testHook_executeIntent(play);
    engine_->testHook_runChain();

    ASSERT_EQ(s().getObject(tide).battlefieldId(), std::optional<BattlefieldId>(0));
    EXPECT_FALSE(s().getObject(tide).is_exhausted);
}

}  // namespace
}  // namespace riftbound::test
