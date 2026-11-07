/// @file test_orianna_lists_audit.cpp
/// Audit of the cards of three more Orianna (Lady of Clockwork) lists —
/// S5 Shanghai (Irelia / Jayce / Brutalizer), Ka Zhi Yu BO3 (Hwei /
/// Stellacorn / Frigid Jewel) and S5 Fuzhou (Ornn, Forge God + Mechanical
/// Marvel) — and of what watching the bot play them showed:
///   - a triggered ability whose leading "you may" is declined (or can't be
///     done) at finalization leaves the chain (CR 383.3.a.2);
///   - start-of-Main-Phase triggers are finalized and resolved before the
///     turn player's first main-phase decision;
///   - Irelia, Fervent / Jae Medarda: "when you choose me" (the chosen unit
///     itself is a watcher; other friendly units being chosen are not "me");
///   - Command: Attack: moving The Ball fires "when you move this"; the gear
///     to reveal is the player's choice;
///   - Resonating Strike: the unit chosen at play can go to a battlefield;
///   - Jayce, Brilliant Inventor: only the turn's first non-token gear
///     triggers the gear half;
///   - "play a Gold gear token exhausted" (Plundering Poro 778 & co.);
///   - [Weaponmaster] (Ornn, Forge God and the shared base): optional, the
///     Equipment is chosen, the Equip cost is paid minus one Power.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kMask         = 60;    // Mask of Foresight (plain gear)
constexpr CardDefId kDiscipline   = 58;
constexpr CardDefId kSterak       = 379;   // [Equip] [G]
constexpr CardDefId kIrelia       = 380;   // Irelia, Fervent
constexpr CardDefId kSvellsongur  = 382;   // [Equip] [1][G]
constexpr CardDefId kClothArmor   = 387;   // [Equip] [B]
constexpr CardDefId kOrnn         = 407;   // Ornn, Forge God
constexpr CardDefId kJae          = 463;   // Jae Medarda
constexpr CardDefId kPoro778      = 778;   // Plundering Poro (UNL)
constexpr CardDefId kResonating   = 821;   // Resonating Strike
constexpr CardDefId kOtterpus     = 840;   // 2-energy 2-Might unit, no text
constexpr CardDefId kJayce        = 855;   // Jayce, Brilliant Inventor
constexpr CardDefId kCommandAtk   = 984;   // Command: Attack
constexpr CardDefId kClockwork    = 1089;  // Lady of Clockwork
constexpr CardDefId kTheBall      = 1090;

/// Answers int prompts from `answers` in order, object prompts with `want`
/// (else the first option); passes priority; ends the turn when it may.
/// Records every call and whether a Pending chain item was on the chain.
class ListsAgent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    GameObjectId want = kInvalidId;
    int calls = 0;
    bool saw_pending = false;
    std::vector<std::vector<GameObjectId>> object_prompts;
    Intent selectAction(const GameState& st, const std::vector<Intent>& legal) override {
        ++calls;
        for (const auto& ci : st.chain.items)
            if (ci.status == ChainItemStatus::Pending) saw_pending = true;
        if (legal.empty()) return Intent{};
        bool any_obj = false, any_val = false;
        for (const auto& i : legal) {
            if (i.type != IntentType::MakeChoice) continue;
            if (!i.chosen_objects.empty()) any_obj = true;
            else if (i.chosen_value.has_value()) any_val = true;
        }
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice && any_obj) {
            std::vector<GameObjectId> offered;
            for (const auto& i : legal)
                if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects.front());
            object_prompts.push_back(offered);
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            return f;
        }
        if (f.type == IntentType::MakeChoice && any_val) {
            if (next < answers.size()) {
                int a = answers[next++];
                for (const auto& i : legal) if (i.chosen_value == a) return i;
            }
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::EndTurn) return i;
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class OriannaListsAuditTest : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    ListsAgent p1_, p2_;
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
    GameObjectId setLegend(PlayerId p, CardDefId def_id) {
        auto id = s().createObject();
        auto& lo = s().getObject(id);
        lo.owner = lo.controller = p;
        lo.card_type = CardType::Legend;
        lo.card_def_id = def_id;
        lo.name = card_db.get(def_id).name;
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
        c.name = def.name;
        c.domains = def.domains;
        c.keywords = def.keywords;
        c.tags = def.tags;
        c.zone = ZoneType::Hand;
        s().player(p).hand.push_back(id);
        return id;
    }
    GameObjectId deckCard(PlayerId p, CardDefId def_id) {   // pushed on top
        auto id = s().createObject();
        auto& c = s().getObject(id);
        const auto& def = card_db.get(def_id);
        c.owner = c.controller = p;
        c.card_def_id = def_id;
        c.card_type = def.card_type;
        c.name = def.name;
        c.tags = def.tags;
        c.zone = ZoneType::MainDeck;
        s().player(p).main_deck.push_back(id);
        return id;
    }
    void play(GameObjectId card, std::vector<GameObjectId> targets = {}) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.targets = std::move(targets);
        engine_->testHook_executeIntent(i);
    }
    void startMainPhase() {
        events.emit(PhaseChangedEvent{TurnPhase::DrawPhase, TurnPhase::MainPhase, P1});
        engine_->testHook_runChain();
    }
    bool inHand(GameObjectId id) {
        const auto& h = s().player(P1).hand;
        return std::find(h.begin(), h.end(), id) != h.end();
    }
};

// ── CR 383.3.a.2: a declined "you may" leaves the chain ──────────────────

TEST_F(OriannaListsAuditTest, Clockwork_NoGear_TriggerLeavesTheChainWithoutAPriorityWindow) {
    makeEngine();
    setLegend(P1, kClockwork);
    s().player(P1).rune_pool.energy = 3;
    cardInHand(P1, kDiscipline);   // a reaction P1 could have wasted in the window
    startMainPhase();
    EXPECT_FALSE(s().chain.exists());
    EXPECT_EQ(p1_.calls, 0) << "nothing to choose: not performed, no priority round";
    EXPECT_EQ(p2_.calls, 0);
    EXPECT_EQ(s().player(P1).rune_pool.energy, 3);
}

TEST_F(OriannaListsAuditTest, Clockwork_Declined_TriggerLeavesTheChain) {
    makeEngine();
    setLegend(P1, kClockwork);
    auto gear = boardObject(P1, kMask);
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {0};   // "No"
    startMainPhase();
    EXPECT_FALSE(s().chain.exists());
    EXPECT_EQ(p1_.calls, 1) << "only the may prompt";
    EXPECT_EQ(p2_.calls, 0) << "the declined trigger never gave anyone priority";
    EXPECT_FALSE(s().getObject(gear).isUnit());
    EXPECT_EQ(s().player(P1).rune_pool.energy, 1);
}

TEST_F(OriannaListsAuditTest, Clockwork_Accepted_StillResolves) {
    makeEngine();
    setLegend(P1, kClockwork);
    auto gear = boardObject(P1, kMask);
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {1};
    p1_.want = gear;
    startMainPhase();
    EXPECT_TRUE(s().getObject(gear).isUnit());
    EXPECT_EQ(s().getObject(gear).current_might, 2) << "Might = its Energy cost";
    EXPECT_GE(p2_.calls, 1) << "an item that stays gets a priority round";
}

// ── Start-of-Main-Phase triggers resolve before the first main action ────

TEST_F(OriannaListsAuditTest, MainPhase_StartTriggerIsFinalizedBeforeAnyPriority) {
    makeEngine();
    s().turn.phase = TurnPhase::DrawPhase;
    setLegend(P1, kClockwork);
    auto gear = boardObject(P1, kMask);
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {1};
    p1_.want = gear;
    engine_->testHook_mainPhase();   // ends when P1 ends the turn
    EXPECT_FALSE(p1_.saw_pending) << "no decision above a not-yet-finalized trigger";
    EXPECT_FALSE(p2_.saw_pending);
    EXPECT_TRUE(s().getObject(gear).isUnit());
}

// ── Irelia, Fervent / Jae Medarda: "when you choose me" ──────────────────

TEST_F(OriannaListsAuditTest, Irelia_ChoosingHerGivesHerPlusOne) {
    makeEngine();
    auto irelia = boardObject(P1, kIrelia, 0);
    const int before = s().getObject(irelia).current_might;
    events.emit(ObjectStateChangedEvent{irelia, "chosen_by_controller"});
    engine_->testHook_runChain();
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(irelia).current_might, before + 1);
}

TEST_F(OriannaListsAuditTest, Irelia_ChoosingAnotherFriendlyUnitIsNotChoosingHer) {
    makeEngine();
    auto irelia = boardObject(P1, kIrelia, 0);
    auto other = boardObject(P1, kOtterpus, 0);
    const int before = s().getObject(irelia).current_might;
    events.emit(ObjectStateChangedEvent{other, "chosen_by_controller"});
    EXPECT_FALSE(s().chain.exists()) << "no trigger for another unit";
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(irelia).current_might, before);
}

TEST_F(OriannaListsAuditTest, Irelia_MyDisciplineOnHerTriggersHer) {
    makeEngine();
    auto irelia = boardObject(P1, kIrelia, 0);
    auto disc = cardInHand(P1, kDiscipline);
    deckCard(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 2;
    p1_.want = irelia;
    const int before = s().getObject(irelia).current_might;
    play(disc);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(irelia).current_might, before + 3) << "+2 Discipline, +1 Irelia";
}

TEST_F(OriannaListsAuditTest, JaeMedarda_TriggersOnlyWhenSheIsChosen) {
    makeEngine();
    auto jae = boardObject(P1, kJae, 0);
    auto other = boardObject(P1, kOtterpus, 0);
    const Card* c = card_registry.get(kJae);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->triggerCondition(s(), jae, P1, TriggerType::WhenYouChooseAFriendlyUnit, jae));
    EXPECT_FALSE(c->triggerCondition(s(), jae, P1, TriggerType::WhenYouChooseAFriendlyUnit, other));
}

// ── Command: Attack ───────────────────────────────────────────────────────

TEST_F(OriannaListsAuditTest, CommandAttack_MovingTheBallFiresItsMoveTrigger) {
    makeEngine();
    auto ball = boardObject(P1, kTheBall, 0);
    auto enemy = boardObject(P2, kOtterpus, 1);
    auto cmd = cardInHand(P1, kCommandAtk);
    s().player(P1).rune_pool.energy = 1;
    s().player(P1).rune_pool.universal_power = 1;
    // Command: Attack: mode 2 = move a [Deploy] gear to BF#1; The Ball's
    // move trigger: mode 1 = deal 2 to enemy units here.
    p1_.answers = {2, 1};
    p1_.want = ball;
    play(cmd);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(ball).battlefieldId(), std::optional<BattlefieldId>{1});
    EXPECT_FALSE(s().getObject(enemy).location.has_value()) << "2 damage killed the 2-Might unit";
}

TEST_F(OriannaListsAuditTest, CommandAttack_LookRevealsTheGearOfMyChoice) {
    makeEngine();
    auto cmd = cardInHand(P1, kCommandAtk);
    deckCard(P1, kOtterpus);
    auto mask = deckCard(P1, kMask);
    auto ball = deckCard(P1, kTheBall);   // top
    s().player(P1).rune_pool.energy = 1;
    s().player(P1).rune_pool.universal_power = 1;
    p1_.answers = {0};
    p1_.want = mask;   // not the top-most gear
    play(cmd);
    EXPECT_TRUE(inHand(mask));
    EXPECT_FALSE(inHand(ball));
    ASSERT_FALSE(p1_.object_prompts.empty());
    EXPECT_EQ(p1_.object_prompts.back().size(), 2u) << "both gears offered, not the unit";
}

// ── Resonating Strike ─────────────────────────────────────────────────────

TEST_F(OriannaListsAuditTest, ResonatingStrike_OnlyUnitsThatCanReachAControlledBattlefield) {
    makeEngine();
    s().battlefields[0].controller = P1;
    auto there = boardObject(P1, kOtterpus, 0);
    auto home = boardObject(P1, kOtterpus);
    const Card* c = card_registry.get(kResonating);
    ASSERT_NE(c, nullptr);
    auto legal = c->enumerateLegalTargets(s(), P1);
    EXPECT_EQ(legal, std::vector<GameObjectId>{home})
        << "the unit at my only battlefield has nowhere to go";
    (void)there;
}

TEST_F(OriannaListsAuditTest, ResonatingStrike_MovesThePlayTimeUnitAndPumpsIt) {
    makeEngine();
    s().battlefields[0].controller = P1;
    s().battlefields[1].controller = P1;
    auto at0 = boardObject(P1, kOtterpus, 0);
    auto rs = cardInHand(P1, kResonating);
    s().player(P1).rune_pool.energy = 2;
    s().player(P1).rune_pool.universal_power = 1;
    p1_.want = at0;
    p1_.answers = {1};   // battlefield index 1 (BF#0 is where it already is)
    play(rs);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(at0).battlefieldId(), std::optional<BattlefieldId>{1});
    EXPECT_EQ(s().getObject(at0).current_might, 4);
}

// ── Jayce, Brilliant Inventor ─────────────────────────────────────────────

TEST_F(OriannaListsAuditTest, Jayce_GearHalfTriggersOnlyOnTheFirstNonTokenGear) {
    makeEngine();
    auto jayce = boardObject(P1, kJayce, 0);
    auto spell = cardInHand(P1, kDiscipline);
    auto gear = boardObject(P1, kMask);
    const Card* c = card_registry.get(kJayce);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->triggerCondition(s(), jayce, P1, TriggerType::WhenYouPlayACard, spell))
        << "a spell is not a gear: nothing goes on the chain";
    EXPECT_FALSE(c->triggerCondition(s(), jayce, P1, TriggerType::WhenYouPlayACard, jayce));
    EXPECT_TRUE(c->triggerCondition(s(), jayce, P1, TriggerType::WhenYouPlayACard, gear));
    s().getObject(jayce).card_counters["__jayce_first_gear_turn"] = s().turn.turn_number + 1;
    EXPECT_FALSE(c->triggerCondition(s(), jayce, P1, TriggerType::WhenYouPlayACard, gear))
        << "not the first gear this turn";
    EXPECT_TRUE(c->triggerCondition(s(), jayce, P1, TriggerType::WhenYouPlayMe, jayce));
}

// ── Gold gear token "exhausted" ───────────────────────────────────────────

TEST_F(OriannaListsAuditTest, PlunderingPoro778_GoldTokenEntersExhausted) {
    auto poro = addUnit(P1, kPoro778, 2, 0);
    EffectExecutor exec(state, events, card_db);
    fireTriggerAs(kPoro778, P1, poro, TriggerType::WhenIConquer, exec);
    int gold = 0;
    for (const auto& [id, o] : state.objects)
        if (o.isToken() && o.name == "Gold" && o.controller == P1) {
            ++gold;
            EXPECT_TRUE(o.is_exhausted) << "\"play a Gold gear token exhausted\"";
        }
    EXPECT_EQ(gold, 1);
}

// ── [Weaponmaster] (Ornn, Forge God) ──────────────────────────────────────

class WeaponmasterChoiceTest : public CardTestFixture {
protected:
    GameObjectId equipment(CardDefId def_id) {
        auto id = state.createObject();
        auto& g = state.getObject(id);
        const auto& def = card_db.get(def_id);
        g.owner = g.controller = P1;
        g.card_def_id = def_id;
        g.card_type = CardType::Gear;
        g.name = def.name;
        g.tags = def.tags;
        g.might_bonus = def.might_bonus;
        g.zone = ZoneType::Base;
        g.location = BaseLocation{P1};
        return id;
    }
};

TEST_F(WeaponmasterChoiceTest, Ornn_ChoosesWhichEquipment_AndOnlyAffordableOnes) {
    auto ornn = addUnit(P1, kOrnn, 4, 0);
    auto armor = equipment(kClothArmor);
    auto sterak = equipment(kSterak);
    auto svell = equipment(kSvellsongur);   // [1][G]: still [1] after "[A] less", no rune
    EffectExecutor exec(state, events, card_db, &card_registry);
    std::vector<GameObjectId> offered;
    driveResumableTrigger(kOrnn, P1, ornn, [&](const std::vector<Intent>& legal) {
        for (const auto& i : legal)
            if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects.front());
        for (const auto& i : legal)
            if (!i.chosen_objects.empty() && i.chosen_objects.front() == sterak) return i;
        return legal.front();
    }, exec, TriggerType::WhenYouPlayMe);
    EXPECT_EQ(offered, (std::vector<GameObjectId>{armor, sterak}));
    EXPECT_EQ(state.getObject(sterak).attached_to, std::optional<GameObjectId>{ornn});
    EXPECT_FALSE(state.getObject(armor).attached_to.has_value());
    EXPECT_FALSE(state.getObject(svell).attached_to.has_value());
}

TEST_F(WeaponmasterChoiceTest, Ornn_WeaponmasterIsOptional) {
    auto ornn = addUnit(P1, kOrnn, 4, 0);
    auto armor = equipment(kClothArmor);
    EffectExecutor exec(state, events, card_db, &card_registry);
    driveResumableTrigger(kOrnn, P1, ornn, [&](const std::vector<Intent>& legal) {
        for (const auto& i : legal) if (i.chosen_objects.empty()) return i;   // "No"
        return legal.front();
    }, exec, TriggerType::WhenYouPlayMe);
    EXPECT_FALSE(state.getObject(armor).attached_to.has_value());
}

TEST_F(WeaponmasterChoiceTest, Ornn_PaysTheEnergyPartOfTheEquipCost) {
    auto ornn = addUnit(P1, kOrnn, 4, 0);
    auto svell = equipment(kSvellsongur);
    auto rune = addRune(P1, Domain::Mind);
    EffectExecutor exec(state, events, card_db, &card_registry);
    driveResumableTrigger(kOrnn, P1, ornn, [&](const std::vector<Intent>& legal) {
        for (const auto& i : legal)
            if (!i.chosen_objects.empty() && i.chosen_objects.front() == svell) return i;
        return legal.front();
    }, exec, TriggerType::WhenYouPlayMe);
    EXPECT_EQ(state.getObject(svell).attached_to, std::optional<GameObjectId>{ornn});
    EXPECT_TRUE(state.getObject(rune).is_exhausted) << "[1][G] minus [A] = [1]";
}

}  // namespace
}  // namespace riftbound::test
