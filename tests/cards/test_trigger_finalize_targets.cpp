/// @file test_trigger_finalize_targets.cpp
/// Triggered abilities that choose an object ("stun an enemy unit here",
/// "buff a unit here", "give a unit +8 [M]", ...). A triggered ability's chain
/// item never carries chosen targets (TriggerManager::fireTrigger adds it with
/// none), so cards that read the object from onTrigger's `targets` never did
/// anything. Each now chooses as its trigger is finalized (CR 383.3) through
/// Card::pickTarget (choicesAtFinalization() = kFinalizeTrigger). The tests
/// run the real engine: the trigger is queued, finalized (the agent chooses)
/// and resolved by GameEngine::runChain.
///
/// Also: Mordekaiser, Twice Slain's "you may" and its cost (the friendly unit
/// dies first, as the replacement's cost).

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kSolari       = 51;    // Solari Shieldbearer
constexpr CardDefId kSpiritRefuge = 63;    // Spirit's Refuge
constexpr CardDefId kYasuo        = 76;    // Yasuo, Remorseful
constexpr CardDefId kWhiteflame   = 82;    // Whiteflame Protector
constexpr CardDefId kRiptideRex   = 92;    // Riptide Rex
constexpr CardDefId kBlastconeFae = 97;    // Blastcone Fae
constexpr CardDefId kCrackshot    = 130;   // Crackshot Corsair
constexpr CardDefId kFirstMate    = 132;   // First Mate
constexpr CardDefId kMarauder     = 191;   // Maddened Marauder
constexpr CardDefId kForgeFuture  = 212;   // Forge of the Future
constexpr CardDefId kLeona        = 238;   // Leona, Determined
constexpr CardDefId kNavori       = 278;   // Navori Fighting Pit
constexpr CardDefId kReaversRow   = 280;   // Reaver's Row
constexpr CardDefId kGemJammer    = 330;   // Gem Jammer
constexpr CardDefId kRibbon       = 361;   // Ribbon Dancer
constexpr CardDefId kSandshifter  = 479;   // Sandshifter
constexpr CardDefId kAhri         = 539;   // Ahri, Inquisitive
constexpr CardDefId kVox          = 589;   // Inviolus Vox
constexpr CardDefId kYuumi        = 618;   // Yuumi, Magical Cat
constexpr CardDefId kBlastCone    = 695;   // Blast Cone
constexpr CardDefId kPridestalker = 744;   // Pridestalker (legend)
constexpr CardDefId kRecital      = 763;   // Amateur Recital
constexpr CardDefId kTwiceSlain   = 999;   // Mordekaiser, Twice Slain
constexpr CardDefId kOtterpus     = 840;   // 2-energy 2-Might unit, no text

/// Answers int prompts from `answers` in order; object prompts take `want`
/// (else the first option). Records what each prompt offered.
class FinalizeAgent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    GameObjectId want = kInvalidId;
    std::vector<std::vector<int>> int_prompts;
    std::vector<std::vector<GameObjectId>> object_prompts;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice && !f.chosen_objects.empty()) {
            std::vector<GameObjectId> offered;
            for (const auto& i : legal)
                if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects.front());
            object_prompts.push_back(offered);
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            return f;
        }
        if (f.type == IntentType::MakeChoice && f.chosen_value.has_value()) {
            std::vector<int> offered;
            for (const auto& i : legal) offered.push_back(i.chosen_value.value_or(-1));
            int_prompts.push_back(offered);
            if (next < answers.size()) {
                const int a = answers[next++];
                for (const auto& i : legal) if (i.chosen_value == a) return i;
            }
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
    bool offered(GameObjectId id) const {
        for (const auto& p : object_prompts)
            if (std::find(p.begin(), p.end(), id) != p.end()) return true;
        return false;
    }
};

class TriggerFinalizeTargetsTest : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    FinalizeAgent p1_, p2_;
    GameState& s() { return engine_->mutableState(); }

    void SetUp() override {
        CardTestFixture::SetUp();
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
        legend(P1);
        legend(P2);
    }
    GameObjectId legend(PlayerId p, CardDefId def_id = kInvalidId) {
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
    GameObjectId battlefieldCard(BattlefieldId bf, CardDefId def_id) {
        auto id = s().createObject();
        auto& o = s().getObject(id);
        o.card_def_id = def_id;
        o.card_type = CardType::Battlefield;
        o.name = card_db.get(def_id).name;
        s().battlefields[bf].card_object_id = id;
        return id;
    }
    GameObjectId board(PlayerId p, CardDefId def_id, int at_bf = -1) {
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
        u.shield_value = def.shield_value;
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
    GameObjectId unit(PlayerId p, int might, int at_bf = -1) {
        auto id = board(p, kOtterpus, at_bf);
        auto& u = s().getObject(id);
        u.base_might = might;
        u.recomputeMight();
        return id;
    }
    /// Queue `source`'s triggered ability and run the chain (finalize + resolve).
    void fire(GameObjectId source, TriggerType t, PlayerId controller = PlayerId::Player1) {
        engine_->testHook_executor().queueTriggeredAbility(source, controller, t);
        engine_->testHook_runChain();
    }
    GameObject& o(GameObjectId id) { return s().getObject(id); }
    bool dead(GameObjectId id) { return !o(id).location.has_value(); }
    bool there(GameObjectId id, int bf) { return o(id).battlefieldId() == std::optional<BattlefieldId>(bf); }
};

// ─── Battlefields ───────────────────────────────────────────────────────────

TEST_F(TriggerFinalizeTargetsTest, NavoriFightingPit_HoldBuffsTheChosenUnitHere) {
    auto pit = battlefieldCard(0, kNavori);
    auto mine = unit(P1, 2, 0);
    auto chosen = unit(P1, 2, 0);
    auto enemy = unit(P2, 2, 0);
    auto far = unit(P1, 2, 1);
    p1_.want = chosen;
    fire(pit, TriggerType::WhenYouHoldHere);
    EXPECT_EQ(o(chosen).buff_count, 1);
    EXPECT_EQ(o(mine).buff_count, 0);
    EXPECT_TRUE(p1_.offered(enemy)) << "a unit here: either player's";
    EXPECT_FALSE(p1_.offered(far)) << "only units here";
}

TEST_F(TriggerFinalizeTargetsTest, AmateurRecital_HoldMovesTheChosenUnitToItsBase) {
    auto recital = battlefieldCard(0, kRecital);
    unit(P1, 2, 0);
    auto enemy = unit(P2, 3, 1);
    auto at_base = unit(P2, 3);
    p1_.answers = {1};
    p1_.want = enemy;
    fire(recital, TriggerType::WhenYouHoldHere);
    EXPECT_TRUE(o(enemy).isAtBase()) << "a unit at any battlefield, to its owner's base";
    EXPECT_FALSE(p1_.offered(at_base)) << "only units at a battlefield";
}

TEST_F(TriggerFinalizeTargetsTest, AmateurRecital_DecliningMovesNothing) {
    auto recital = battlefieldCard(0, kRecital);
    auto enemy = unit(P2, 3, 1);
    p1_.answers = {0};
    fire(recital, TriggerType::WhenYouHoldHere);
    EXPECT_TRUE(there(enemy, 1));
    EXPECT_TRUE(p1_.object_prompts.empty());
}

TEST_F(TriggerFinalizeTargetsTest, ReaversRow_DefendMovesTheChosenFriendlyUnitHereToBase) {
    auto row = battlefieldCard(0, kReaversRow);
    auto stay = unit(P1, 2, 0);
    auto go = unit(P1, 2, 0);
    auto enemy = unit(P2, 2, 0);
    p1_.answers = {1};
    p1_.want = go;
    fire(row, TriggerType::WhenYouDefendHere);
    EXPECT_TRUE(o(go).isAtBase());
    EXPECT_TRUE(there(stay, 0));
    EXPECT_FALSE(p1_.offered(enemy)) << "a friendly unit";
}

// ─── Gear and legend ────────────────────────────────────────────────────────

TEST_F(TriggerFinalizeTargetsTest, SpiritsRefuge_BuffsTheChosenFriendlyUnit) {
    auto refuge = board(P1, kSpiritRefuge);
    unit(P1, 2, 0);
    auto chosen = unit(P1, 2);
    auto enemy = unit(P2, 2, 0);
    p1_.want = chosen;
    fire(refuge, TriggerType::WhenYouPlayThis);
    EXPECT_EQ(o(chosen).buff_count, 1);
    EXPECT_FALSE(p1_.offered(enemy));
}

TEST_F(TriggerFinalizeTargetsTest, ForgeOfTheFuture_PlaysARecruitAtBase) {
    auto forge = board(P1, kForgeFuture);
    auto mine = unit(P1, 2, 0);
    fire(forge, TriggerType::WhenYouPlayThis);
    EXPECT_FALSE(dead(mine)) << "it used to kill a 'target'";
    int recruits = 0;
    for (const auto& [id, obj] : s().objects)
        if (obj.isUnit() && obj.name == "Recruit" && obj.controller == P1 && obj.isAtBase() &&
            obj.current_might == 1)
            ++recruits;
    EXPECT_EQ(recruits, 1);
}

TEST_F(TriggerFinalizeTargetsTest, BlastCone_MovesTheChosenEnemyToTheChosenPlaceAndMayStunIt) {
    auto cone = board(P1, kBlastCone);
    auto first = unit(P2, 2, 0);
    auto chosen = unit(P2, 3, 0);
    p1_.want = chosen;
    p1_.answers = {1, /*battlefield #1*/ 2, /*stun*/ 1};
    fire(cone, TriggerType::WhenYouPlayThis);
    EXPECT_TRUE(there(chosen, 1)) << "moved to the destination its controller chose";
    EXPECT_TRUE(there(first, 0));
    EXPECT_TRUE(o(chosen).is_stunned);
    EXPECT_TRUE(o(cone).is_exhausted);
    ASSERT_GE(p1_.int_prompts.size(), 2u);
    EXPECT_EQ(p1_.int_prompts[1], (std::vector<int>{0, 2})) << "its base or the other battlefield";
}

TEST_F(TriggerFinalizeTargetsTest, BlastCone_MayKeepItselfReady) {
    auto cone = board(P1, kBlastCone);
    auto enemy = unit(P2, 2, 0);
    p1_.want = enemy;
    p1_.answers = {1, /*base*/ 0, /*no stun*/ 0};
    fire(cone, TriggerType::WhenYouPlayThis);
    EXPECT_TRUE(o(enemy).isAtBase());
    EXPECT_FALSE(o(enemy).is_stunned);
    EXPECT_FALSE(o(cone).is_exhausted);
}

TEST_F(TriggerFinalizeTargetsTest, Pridestalker_GivesTheChosenUnitPlusOne) {
    auto lg = legend(P1, kPridestalker);
    unit(P1, 2, 0);
    auto chosen = unit(P1, 2);   // at base: any unit
    p1_.want = chosen;
    fire(lg, TriggerType::WhenYouPlayAUnit);
    EXPECT_EQ(o(chosen).temp_might_bonus, 1);
}

// ─── "When I attack / defend": an enemy unit here ───────────────────────────

TEST_F(TriggerFinalizeTargetsTest, YasuoRemorseful_DealsHisMightToTheChosenEnemyHere) {
    auto yasuo = board(P1, kYasuo, 0);
    const int might = o(yasuo).current_might;
    ASSERT_GT(might, 0);
    auto small = unit(P2, 1, 0);
    auto chosen = unit(P2, might, 0);
    auto far = unit(P2, 1, 1);
    p1_.want = chosen;
    fire(yasuo, TriggerType::WhenIAttack);
    EXPECT_TRUE(dead(chosen));
    EXPECT_FALSE(dead(small));
    EXPECT_FALSE(p1_.offered(far)) << "only enemy units here";
}

TEST_F(TriggerFinalizeTargetsTest, CrackshotCorsair_DealsOneToTheChosenEnemyHere) {
    auto corsair = board(P1, kCrackshot, 0);
    auto other = unit(P2, 3, 0);
    auto chosen = unit(P2, 3, 0);
    auto mine = unit(P1, 3, 0);
    p1_.want = chosen;
    fire(corsair, TriggerType::WhenIAttack);
    EXPECT_EQ(o(chosen).damage_marked, 1);
    EXPECT_EQ(o(other).damage_marked, 0);
    EXPECT_FALSE(p1_.offered(mine)) << "an enemy unit";
}

TEST_F(TriggerFinalizeTargetsTest, LeonaDetermined_StunsTheChosenEnemyHere) {
    auto leona = board(P1, kLeona, 0);
    auto other = unit(P2, 3, 0);
    auto chosen = unit(P2, 3, 0);
    auto far = unit(P2, 3, 1);
    p1_.want = chosen;
    fire(leona, TriggerType::WhenIAttack);
    EXPECT_TRUE(o(chosen).is_stunned);
    EXPECT_FALSE(o(other).is_stunned);
    EXPECT_FALSE(p1_.offered(far));
}

TEST_F(TriggerFinalizeTargetsTest, AhriInquisitive_GivesTheChosenEnemyHereMinusTwo) {
    auto ahri = board(P1, kAhri, 0);
    auto chosen = unit(P2, 4, 0);
    auto tiny = unit(P2, 2, 0);
    p1_.want = chosen;
    fire(ahri, TriggerType::WhenIAttackOrDefend);
    EXPECT_EQ(o(chosen).current_might, 2);
    EXPECT_EQ(o(tiny).current_might, 2);
}

TEST_F(TriggerFinalizeTargetsTest, YuumiMagicalCat_GivesAnotherOfYourUnitsHerePlusThreeAndTank) {
    auto yuumi = board(P1, kYuumi, 0);
    auto chosen = unit(P1, 2, 0);
    auto far = unit(P1, 2, 1);
    auto enemy = unit(P2, 2, 0);
    p1_.want = chosen;
    fire(yuumi, TriggerType::WhenIAttackOrDefend);
    EXPECT_EQ(o(chosen).temp_might_bonus, 3);
    EXPECT_TRUE(o(chosen).hasKeyword(Keyword::Tank));
    EXPECT_FALSE(p1_.offered(yuumi)) << "another unit";
    EXPECT_FALSE(p1_.offered(far)) << "here";
    EXPECT_FALSE(p1_.offered(enemy)) << "one of yours";
}

// ─── "When you play me" / move / conquer ────────────────────────────────────

TEST_F(TriggerFinalizeTargetsTest, SolariShieldbearer_StunsTheChosenUnit) {
    auto self = board(P1, kSolari, 0);
    auto chosen = unit(P2, 3);   // any unit, even at a base
    p1_.want = chosen;
    fire(self, TriggerType::WhenYouPlayMe);
    EXPECT_TRUE(o(chosen).is_stunned);
}

TEST_F(TriggerFinalizeTargetsTest, WhiteflameProtector_GivesTheChosenUnitPlusEight) {
    auto self = board(P1, kWhiteflame);
    auto chosen = unit(P1, 2, 0);
    p1_.want = chosen;
    fire(self, TriggerType::WhenYouPlayMe);
    EXPECT_EQ(o(chosen).temp_might_bonus, 8);
    EXPECT_EQ(o(self).temp_might_bonus, 0);
}

TEST_F(TriggerFinalizeTargetsTest, RiptideRex_DealsSixToTheChosenEnemyAtABattlefield) {
    auto rex = board(P1, kRiptideRex);
    auto chosen = unit(P2, 6, 1);
    auto at_base = unit(P2, 1);
    p1_.want = chosen;
    fire(rex, TriggerType::WhenYouPlayMe);
    EXPECT_TRUE(dead(chosen));
    EXPECT_FALSE(p1_.offered(at_base)) << "at a battlefield";
}

TEST_F(TriggerFinalizeTargetsTest, BlastconeFae_GivesTheChosenUnitMinusTwoToAMinimumOfOne) {
    auto fae = board(P1, kBlastconeFae);
    auto chosen = unit(P2, 2, 0);
    p1_.want = chosen;
    fire(fae, TriggerType::WhenYouPlayMe);
    EXPECT_EQ(o(chosen).current_might, 1);
}

TEST_F(TriggerFinalizeTargetsTest, FirstMate_ReadiesAnotherChosenUnit) {
    auto mate = board(P1, kFirstMate, 0);
    o(mate).is_exhausted = true;
    auto chosen = unit(P1, 2, 0);
    o(chosen).is_exhausted = true;
    p1_.want = chosen;
    fire(mate, TriggerType::WhenYouPlayMe);
    EXPECT_FALSE(o(chosen).is_exhausted);
    EXPECT_FALSE(p1_.offered(mate)) << "another unit";
}

TEST_F(TriggerFinalizeTargetsTest, MaddenedMarauder_MovesTheChosenUnitFromABattlefieldToItsBase) {
    auto marauder = board(P1, kMarauder);
    auto chosen = unit(P2, 3, 0);
    auto at_base = unit(P2, 3);
    p1_.want = chosen;
    fire(marauder, TriggerType::WhenYouPlayMe);
    EXPECT_TRUE(o(chosen).isAtBase());
    EXPECT_FALSE(p1_.offered(at_base));
    EXPECT_FALSE(p1_.offered(marauder)) << "the Marauder is at its base";
}

TEST_F(TriggerFinalizeTargetsTest, GemJammer_GivesTheChosenUnitGanking) {
    auto jammer = board(P1, kGemJammer);
    auto chosen = unit(P1, 2, 0);
    p1_.want = chosen;
    fire(jammer, TriggerType::WhenYouPlayMe);
    EXPECT_TRUE(o(chosen).hasKeyword(Keyword::Ganking));
}

TEST_F(TriggerFinalizeTargetsTest, Sandshifter_KillsTheChosenEnemyWithThreeMightOrLess) {
    auto shifter = board(P1, kSandshifter);
    auto chosen = unit(P2, 3, 0);
    auto big = unit(P2, 4, 0);
    p1_.want = chosen;
    fire(shifter, TriggerType::WhenYouPlayMe);
    EXPECT_TRUE(dead(chosen));
    EXPECT_FALSE(p1_.offered(big)) << "3 [M] or less";
}

TEST_F(TriggerFinalizeTargetsTest, RibbonDancer_GivesAnotherFriendlyUnitPlusOne) {
    auto dancer = board(P1, kRibbon, 0);
    auto chosen = unit(P1, 2);
    auto enemy = unit(P2, 2, 0);
    p1_.want = chosen;
    fire(dancer, TriggerType::WhenIMoveToFB);
    EXPECT_EQ(o(chosen).temp_might_bonus, 1);
    EXPECT_FALSE(p1_.offered(dancer));
    EXPECT_FALSE(p1_.offered(enemy));
}

TEST_F(TriggerFinalizeTargetsTest, InviolusVox_GivesTheChosenFriendlyUnitPlusEight) {
    auto vox = board(P1, kVox, 0);
    auto chosen = unit(P1, 2, 1);
    auto enemy = unit(P2, 2, 0);
    p1_.want = chosen;
    fire(vox, TriggerType::WhenIConquer);
    EXPECT_EQ(o(chosen).temp_might_bonus, 8);
    EXPECT_FALSE(p1_.offered(enemy));
}

// ─── The choice is made at finalization; a target gone by resolution ────────

/// Runs the card's trigger the way GameEngine::runFinalizePass and the chain
/// resolution do: once while finalizing (the agent chooses), then `between`
/// changes the board, then again as the ability resolves.
TEST_F(TriggerFinalizeTargetsTest, ATargetThatLeftBeforeResolutionIsAMistarget) {
    auto leona = board(P1, kLeona, 0);
    auto chosen = unit(P2, 3, 0);
    unit(P2, 3, 0);
    auto& exec = engine_->testHook_executor();
    Card* card = card_registry.get(kLeona);
    ChainItem ri;
    ri.id = s().chain.allocateId();
    ri.controller = P1;
    ri.is_ability = true;
    ri.source = leona;
    ri.card_def_id = kLeona;
    ri.fired_trigger = TriggerType::WhenIAttack;
    ri.finalizing = true;
    s().chain.resuming = ri;
    CardContext ctx{s(), events, exec, P1, leona};
    card->onTrigger(ctx, {});
    ASSERT_TRUE(exec.hasPendingChoice()) << "asked while finalizing";
    exec.consumePendingChoice();
    Intent pick;
    pick.type = IntentType::MakeChoice;
    pick.player = P1;
    pick.chosen_objects = {chosen};
    exec.recordChoice(pick);
    card->onTrigger(ctx, {});
    EXPECT_FALSE(o(chosen).is_stunned) << "nothing happens before the marker";
    auto& r = *s().chain.resuming;
    r.finalizing = false;
    r.choices_finalized = true;
    r.finalize_checked = 0;

    o(chosen).location = BattlefieldLocation{1};   // it left "here" in response
    card->onTrigger(ctx, {});
    s().chain.resuming.reset();
    EXPECT_FALSE(o(chosen).is_stunned) << "no longer an enemy unit here";
}

// ─── Mordekaiser, Twice Slain ───────────────────────────────────────────────

TEST_F(TriggerFinalizeTargetsTest, MordekaiserTwiceSlain_TheFriendlyUnitDiesAsTheCost) {
    auto mord = board(P1, kTwiceSlain, 0);
    auto fodder = unit(P1, 2, 0);
    p1_.answers = {1};
    p1_.want = fodder;
    engine_->testHook_cleanup();
    o(mord).damage_marked = 9;
    engine_->testHook_executor().killObject(mord);
    EXPECT_TRUE(dead(fodder)) << "killed as the replacement applies, not by a later trigger";
    EXPECT_TRUE(o(mord).isAtBase());
    EXPECT_TRUE(o(mord).is_exhausted);
    EXPECT_EQ(o(mord).damage_marked, 0);
    EXPECT_TRUE(s().chain.items.empty() || std::none_of(s().chain.items.begin(), s().chain.items.end(),
        [&](const ChainItem& ci) { return ci.source == mord; })) << "no follow-up trigger";
}

TEST_F(TriggerFinalizeTargetsTest, MordekaiserTwiceSlain_DecliningLetsHimDie) {
    auto mord = board(P1, kTwiceSlain, 0);
    auto fodder = unit(P1, 2, 0);
    p1_.answers = {0};
    engine_->testHook_cleanup();
    engine_->testHook_executor().killObject(mord);
    engine_->testHook_runChain();
    EXPECT_TRUE(dead(mord));
    EXPECT_FALSE(dead(fodder));
    ASSERT_EQ(p1_.int_prompts.size(), 1u) << "the 'you may' is asked";
}

TEST_F(TriggerFinalizeTargetsTest, MordekaiserTwiceSlain_OnceEachTurnIsNotAskedAgain) {
    auto mord = board(P1, kTwiceSlain, 0);
    unit(P1, 2, 0);
    unit(P1, 2, 0);
    p1_.answers = {1, 1};
    engine_->testHook_cleanup();
    engine_->testHook_executor().killObject(mord);
    engine_->testHook_runChain();
    ASSERT_FALSE(dead(mord));
    engine_->testHook_executor().killObject(mord);
    engine_->testHook_runChain();
    EXPECT_TRUE(dead(mord));
    EXPECT_EQ(p1_.int_prompts.size(), 1u);
}

}  // namespace
}  // namespace riftbound::test
