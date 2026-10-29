/// @file test_radiance_b09.cpp
/// Radiance batch 09: Steadfast Outrider (+2 [M] beside 2 friendly units/gear),
/// Battlefield Armorer (pay [1]: a dead gear returns to hand), Demacian
/// Illuminators (one buff per batch of friendly deaths), Heimerdinger,
/// Academy Instructor (unit tokens become 3 [M] Mechs), Jarvan IV, Royal Heir
/// (attack: move a unit to his battlefield), Dockside Lock-Up (banish a unit
/// here; on hold, kill this to play it), Mordekaiser, Dominating (discard 1 to
/// kill), Seraphine, Not Alone (Recruits + Might per exhausted unit), Boy Who
/// Shattered Time (Ekko legend: Empower, then a [2][A] discount with a kill at
/// end of turn) and Chronobreak (pay X Power: banish and replay X units).

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

#include <deque>

namespace riftbound::test {
namespace {

constexpr CardDefId kOutrider      = 1075;
constexpr CardDefId kArmorer       = 1076;
constexpr CardDefId kIlluminators  = 1077;
constexpr CardDefId kHeimerdinger  = 1078;
constexpr CardDefId kJarvan        = 1079;
constexpr CardDefId kLockUp        = 1080;
constexpr CardDefId kMordekaiser   = 1081;
constexpr CardDefId kSeraphine     = 1082;
constexpr CardDefId kEkko          = 1083;
constexpr CardDefId kChronobreak   = 1084;
constexpr CardDefId kOtterpus      = 840;  // 2-energy 2-Might unit, no play or exhaust text
constexpr CardDefId kConcertPoster = 983;  // [Deploy] gear, no death text

/// Answers int prompts (yes/no, X, modes, locations) from a queue, picks the
/// next object from `wants` (else `want`) when offered objects, passes
/// priority otherwise. Records every prompt.
class B09Agent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    GameObjectId want = kInvalidId;
    std::deque<GameObjectId> wants;
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
            GameObjectId pick = want;
            if (!wants.empty()) { pick = wants.front(); wants.pop_front(); }
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == pick) return i;
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class RadianceB09Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B09Agent p1_, p2_;
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
        if (def_id != kInvalidId) {
            lo.name = card_db.get(def_id).name;
            lo.keywords = card_db.get(def_id).keywords;
        }
        lo.zone = ZoneType::LegendZone;
        s().player(p).legend_zone = id;
        return id;
    }
    void stamp(GameObject& c, PlayerId p, CardDefId def_id) {
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
        c.might_bonus = def.might_bonus;
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
    void play(GameObjectId card, std::optional<LocationId> where = std::nullopt) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
    }
    void activate(GameObjectId source, int ability_index) {
        Intent i;
        i.type = IntentType::ActivateAbility;
        i.player = P1;
        i.ability_source = source;
        i.ability_index = ability_index;
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
    std::vector<int> abilitiesOffered(GameObjectId source) {
        std::vector<int> out;
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::ActivateAbility && a.ability_source == source)
                out.push_back(a.ability_index);
        return out;
    }
    bool inZoneList(const std::vector<GameObjectId>& v, GameObjectId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
};

// ─── RAD-128 Steadfast Outrider ─────────────────────────────────────────────

TEST_F(RadianceB09Test, Outrider_PlusTwoBesideTwoFriendlyUnitsOrGear) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto outrider = boardObject(P1, kOutrider, 0);
    boardObject(P1, kOtterpus, 0);
    boardObject(P1, kConcertPoster, 0);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(outrider).current_might, 5) << "a unit and a gear: 3 + 2";
    EXPECT_TRUE(card_db.get(kOutrider).keywords.has(Keyword::Tank));
}

TEST_F(RadianceB09Test, Outrider_NoBonusWithOneFriendlyOrWithEnemies) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto outrider = boardObject(P1, kOutrider, 0);
    boardObject(P1, kOtterpus, 0);
    boardObject(P2, kOtterpus, 0);
    boardObject(P2, kOtterpus, 0);
    boardObject(P1, kOtterpus, 1);   // another battlefield
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(outrider).current_might, 3);
}

// ─── RAD-130 Battlefield Armorer ────────────────────────────────────────────

TEST_F(RadianceB09Test, Armorer_PayOneToReturnAGearThatDiedAtABattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kArmorer);
    auto gear = boardObject(P1, kConcertPoster, 0);
    engine_->testHook_cleanup();                       // the Armorer sees the gear
    engine_->testHook_executor().killObject(gear);
    engine_->testHook_cleanup();                       // ... and that it died
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {1};
    engine_->testHook_runChain();
    ASSERT_EQ(p1_.int_prompts.size(), 1u);
    EXPECT_EQ(s().getObject(gear).zone, ZoneType::Hand);
    EXPECT_TRUE(inZoneList(s().player(P1).hand, gear));
    EXPECT_FALSE(inZoneList(s().player(P1).trash, gear));
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0) << "paid [1]";
    EXPECT_TRUE(s().chain.items.empty());
}

TEST_F(RadianceB09Test, Armorer_DeclineOrGearDyingInBaseLeavesItInTrash) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kArmorer);
    auto at_bf = boardObject(P1, kConcertPoster, 0);
    auto in_base = boardObject(P1, kConcertPoster);
    engine_->testHook_cleanup();
    engine_->testHook_executor().killObject(in_base);
    engine_->testHook_cleanup();
    s().player(P1).rune_pool.energy = 1;
    engine_->testHook_runChain();
    EXPECT_TRUE(p1_.int_prompts.empty()) << "a gear in a base is not at a battlefield";
    EXPECT_EQ(s().getObject(in_base).zone, ZoneType::Trash);

    engine_->testHook_executor().killObject(at_bf);
    engine_->testHook_cleanup();
    p1_.answers = {0};   // decline
    engine_->testHook_runChain();
    EXPECT_EQ(p1_.int_prompts.size(), 1u);
    EXPECT_EQ(s().getObject(at_bf).zone, ZoneType::Trash);
    EXPECT_EQ(s().player(P1).rune_pool.energy, 1);
}

// ─── RAD-131 Demacian Illuminators ──────────────────────────────────────────

TEST_F(RadianceB09Test, Illuminators_UnitsDyingTogetherGiveOneBuff) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kIlluminators);
    auto a = boardObject(P1, kOtterpus);
    auto b = boardObject(P1, kOtterpus);
    auto keep = boardObject(P1, kOtterpus);
    s().getObject(a).damage_marked = 2;
    s().getObject(b).damage_marked = 2;
    p1_.want = keep;
    engine_->testHook_cleanup();          // both die in one lethal-damage check
    engine_->testHook_runChain();
    EXPECT_FALSE(s().getObject(a).location.has_value());
    EXPECT_FALSE(s().getObject(b).location.has_value());
    EXPECT_EQ(p1_.obj_prompts.size(), 1u) << "one trigger for the batch";
    EXPECT_EQ(s().getObject(keep).buff_count, 1);

    // A later, separate death triggers again.
    auto c = boardObject(P1, kOtterpus);
    auto other = boardObject(P1, kOtterpus);
    p1_.want = other;
    engine_->testHook_executor().killObject(c);
    engine_->testHook_runChain();
    EXPECT_EQ(p1_.obj_prompts.size(), 2u);
    EXPECT_EQ(s().getObject(other).buff_count, 1);
}

// ─── RAD-132 Heimerdinger, Academy Instructor ───────────────────────────────

TEST_F(RadianceB09Test, Heimerdinger_UnitTokenBecomesAMechWhileHeIsAtABattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kHeimerdinger, 0);
    engine_->testHook_cleanup();
    auto tok = engine_->testHook_executor().createToken(
        P1, CardType::Unit, "Recruit", 1, {"Recruit"}, KeywordSet{},
        LocationId{BaseLocation{P1}}, false);
    engine_->testHook_cleanup();
    const auto& t = s().getObject(tok);
    EXPECT_EQ(t.name, "Mech");
    EXPECT_EQ(t.current_might, 3);
    EXPECT_TRUE(t.isAtBase()) << "the same location";
    EXPECT_TRUE(t.isToken());
}

TEST_F(RadianceB09Test, Heimerdinger_NoReplacementWhileInBase) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kHeimerdinger);
    engine_->testHook_cleanup();
    auto tok = engine_->testHook_executor().createToken(
        P1, CardType::Unit, "Recruit", 1, {"Recruit"}, KeywordSet{},
        LocationId{BaseLocation{P1}}, false);
    auto enemy_tok = engine_->testHook_executor().createToken(
        P2, CardType::Unit, "Recruit", 1, {"Recruit"}, KeywordSet{},
        LocationId{BaseLocation{P2}}, false);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(tok).name, "Recruit");
    EXPECT_EQ(s().getObject(enemy_tok).name, "Recruit");
}

// ─── RAD-133 Jarvan IV, Royal Heir ──────────────────────────────────────────

TEST_F(RadianceB09Test, Jarvan_AttackMovesAFriendlyUnitToHisBattlefield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto jarvan = boardObject(P1, kJarvan, 0);
    auto ally = boardObject(P1, kOtterpus);
    p1_.answers = {1};
    p1_.want = ally;
    engine_->testHook_executor().queueTriggeredAbility(jarvan, P1, TriggerType::WhenIAttack);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(ally).battlefieldId(), std::optional<BattlefieldId>{0});
    const auto& def = card_db.get(kJarvan);
    EXPECT_TRUE(def.keywords.has(Keyword::Assault));
    EXPECT_EQ(def.assault_value, 1);
    EXPECT_TRUE(def.keywords.has(Keyword::Tank));
}

TEST_F(RadianceB09Test, Jarvan_MayDecline) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto jarvan = boardObject(P1, kJarvan, 0);
    auto ally = boardObject(P1, kOtterpus);
    p1_.answers = {0};
    engine_->testHook_executor().queueTriggeredAbility(jarvan, P1, TriggerType::WhenIAttack);
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(ally).isAtBase());
}

// ─── RAD-136 Dockside Lock-Up ───────────────────────────────────────────────

TEST_F(RadianceB09Test, LockUp_BanishesAUnitHereThenHoldPlaysItForYou) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto lockup = cardInHand(P1, kLockUp);
    s().player(P1).rune_pool.energy = 6;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 2;
    p1_.want = enemy;
    play(lockup, LocationId{BattlefieldLocation{0}});
    engine_->testHook_runChain();
    ASSERT_EQ(s().getObject(lockup).battlefieldId(), std::optional<BattlefieldId>{0});
    EXPECT_EQ(s().getObject(enemy).zone, ZoneType::Banishment);
    EXPECT_TRUE(inZoneList(s().player(P2).banishment, enemy));

    // No hold here yet: nothing at the start of the Main Phase.
    events.emit(PhaseChangedEvent{TurnPhase::DrawPhase, TurnPhase::MainPhase, P1});
    engine_->testHook_runChain();
    EXPECT_TRUE(p1_.int_prompts.empty());

    // Held here this turn: kill this to play the banished card.
    s().player(P1).battlefields_scored_this_turn.insert(0);
    p1_.answers = {1, 0};   // yes; play it to base
    events.emit(PhaseChangedEvent{TurnPhase::DrawPhase, TurnPhase::MainPhase, P1});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(lockup).zone, ZoneType::Trash) << "the cost: kill this";
    const auto& u = s().getObject(enemy);
    EXPECT_TRUE(u.isAtBase());
    EXPECT_EQ(u.controller, P1) << "you play it";
    EXPECT_EQ(u.owner, P2);
    EXPECT_FALSE(inZoneList(s().player(P2).banishment, enemy));
}

TEST_F(RadianceB09Test, LockUp_HasDeploy) {
    makeEngine();
    const auto* c = card_registry.get(kLockUp);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->deploys());
}

// ─── RAD-137 Mordekaiser, Dominating ────────────────────────────────────────

TEST_F(RadianceB09Test, Mordekaiser_PlayDiscardOneToKillAUnit) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto fodder = cardInHand(P1, kOtterpus);
    auto morde = cardInHand(P1, kMordekaiser);
    s().player(P1).rune_pool.energy = 9;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 2;
    p1_.answers = {1};
    p1_.wants = {enemy, fodder};
    play(morde, LocationId{BaseLocation{P1}});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(enemy).zone, ZoneType::Trash);
    EXPECT_EQ(s().getObject(fodder).zone, ZoneType::Trash) << "the discard";
    EXPECT_TRUE(s().player(P1).hand.empty());
}

TEST_F(RadianceB09Test, Mordekaiser_AttackNotOfferedWithAnEmptyHand) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto morde = boardObject(P1, kMordekaiser, 0);
    auto enemy = boardObject(P2, kOtterpus, 0);
    engine_->testHook_executor().queueTriggeredAbility(morde, P1, TriggerType::WhenIAttack);
    engine_->testHook_runChain();
    EXPECT_TRUE(p1_.int_prompts.empty());
    EXPECT_TRUE(s().getObject(enemy).location.has_value());
}

// ─── RAD-138 Seraphine, Not Alone ───────────────────────────────────────────

TEST_F(RadianceB09Test, Seraphine_RecruitsOnPlayAndOnExhaustAndCountsExhaustedUnits) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto sera = cardInHand(P1, kSeraphine);
    s().player(P1).rune_pool.energy = 5;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 1;
    play(sera, LocationId{BaseLocation{P1}});
    engine_->testHook_runChain();
    engine_->testHook_cleanup();
    auto recruits = [&] {
        int n = 0;
        for (auto& [id, o] : s().objects)
            if (o.isUnit() && o.isToken() && o.name == "Recruit" && o.controller == P1 &&
                o.location.has_value()) ++n;
        return n;
    };
    EXPECT_EQ(recruits(), 1);
    EXPECT_EQ(s().getObject(sera).current_might, 2) << "1 + the exhausted Recruit";

    s().getObject(sera).is_exhausted = false;
    engine_->testHook_executor().exhaustObject(sera);
    engine_->testHook_runChain();
    engine_->testHook_cleanup();
    EXPECT_EQ(recruits(), 2);
    EXPECT_EQ(s().getObject(sera).current_might, 3);

    boardObject(P1, kOtterpus);   // a ready unit doesn't count
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(sera).current_might, 3);
}

// ─── RAD-139 Boy Who Shattered Time (Ekko) ──────────────────────────────────

TEST_F(RadianceB09Test, Ekko_EmpowerCostsOneAndExhausts) {
    makeEngine();
    auto ekko = setLegend(P1, kEkko); setLegend(P2);
    s().player(P1).rune_pool.energy = 1;
    auto offered = abilitiesOffered(ekko);
    EXPECT_EQ(offered, (std::vector<int>{1})) << "only [Empower]: not Empowered yet";
    activate(ekko, 1);
    EXPECT_TRUE(s().getObject(ekko).isEmpowered());
    EXPECT_TRUE(s().getObject(ekko).is_exhausted);
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
    EXPECT_EQ(card_db.get(kEkko).tags, (std::vector<std::string>{"Ekko"}));
}

TEST_F(RadianceB09Test, Ekko_DisempowerForADiscountAndTheUnitDiesAtEndOfTurn) {
    makeEngine();
    auto ekko = setLegend(P1, kEkko); setLegend(P2);
    s().getObject(ekko).empowered = 1;
    auto offered = abilitiesOffered(ekko);
    EXPECT_EQ(offered, (std::vector<int>{0})) << "Empowered: only the disempower ability";
    activate(ekko, 0);
    EXPECT_FALSE(s().getObject(ekko).isEmpowered());
    EXPECT_TRUE(s().getObject(ekko).is_exhausted);
    ASSERT_EQ(s().player(P1).cost_modifiers.size(), 1u);

    // A 2-Energy unit costs nothing now.
    auto unit = cardInHand(P1, kOtterpus);
    EXPECT_TRUE(engine_->testHook_canAfford(P1, unit));
    play(unit, LocationId{BaseLocation{P1}});
    engine_->testHook_runChain();
    ASSERT_TRUE(s().getObject(unit).isAtBase());
    EXPECT_TRUE(s().player(P1).cost_modifiers.empty()) << "used up";

    // The next unit pays full price and is not doomed.
    auto second = boardObject(P1, kOtterpus);
    events.emit(PhaseChangedEvent{TurnPhase::MainPhase, TurnPhase::EndingStep, P1});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(unit).zone, ZoneType::Trash) << "Kill me at end of turn";
    EXPECT_TRUE(s().getObject(second).location.has_value());
}

// ─── RAD-140 Chronobreak ────────────────────────────────────────────────────

TEST_F(RadianceB09Test, Chronobreak_PayTwoPowerToReplayTwoUnits) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto a = boardObject(P1, kOtterpus, 0);
    auto b = boardObject(P1, kOtterpus);
    s().getObject(a).damage_marked = 1;
    rune(P1, Domain::Calm, /*exhausted=*/true);
    rune(P1, Domain::Body, /*exhausted=*/true);
    s().player(P1).rune_pool.energy = 2;
    auto spell = cardInHand(P1, kChronobreak);
    p1_.answers = {2, 1};      // X = 2; A back to its battlefield
    p1_.wants = {a, b};
    play(spell);
    engine_->testHook_runChain();
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(runesOnBoard(P1), 0) << "2 Power paid by recycling both runes";
    EXPECT_EQ(s().getObject(a).battlefieldId(), std::optional<BattlefieldId>{0});
    EXPECT_EQ(s().getObject(a).damage_marked, 0) << "a new object: no damage";
    EXPECT_TRUE(s().getObject(a).is_exhausted);
    EXPECT_TRUE(s().getObject(b).isAtBase());
    EXPECT_TRUE(s().player(P1).banishment.empty());
}

TEST_F(RadianceB09Test, Chronobreak_ZeroPowerDoesNothing) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto a = boardObject(P1, kOtterpus, 0);
    s().player(P1).rune_pool.energy = 2;
    auto spell = cardInHand(P1, kChronobreak);
    play(spell);
    engine_->testHook_runChain();
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0})) << "no Power to pay";
    EXPECT_EQ(s().getObject(a).battlefieldId(), std::optional<BattlefieldId>{0});
    EXPECT_EQ(s().getObject(spell).zone, ZoneType::Trash);
}

}  // namespace
}  // namespace riftbound::test
