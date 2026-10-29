/// @file test_text_aura_double_count.cpp
/// Cards that implement their "units have ..." clauses in Card code opt out of
/// GameEngine::recalculateAuras' ability-text matcher (Card::usesTextAuras),
/// so each continuous effect is applied exactly once and with the card's own
/// scoping (friendly only, "other", "here", Level gates).

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kCaptainFarron   = 15;
constexpr CardDefId kTaricProtector  = 74;
constexpr CardDefId kMegaMech        = 88;   // 8-Might Mech, no text
constexpr CardDefId kGarenCommander  = 311;
constexpr CardDefId kRumbleHotheaded = 349;
constexpr CardDefId kAllay           = 603;
constexpr CardDefId kWujuMaster      = 784;
constexpr CardDefId kRageAmplifier   = 805;
constexpr CardDefId kOtterpus        = 840;  // 2-Might unit, no relevant text
constexpr CardDefId kDarius          = 243;  // Darius, Executioner
constexpr CardDefId kLeona           = 79;   // Leona, Zealot
constexpr CardDefId kBannerOfConquest = 986;
constexpr CardDefId kEphemeralWard   = 1011;

class TextAuraPassAgent : public AgentInterface {
public:
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return legal.front();
    }
};

class TextAuraDoubleCountTest : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    TextAuraPassAgent p1_, p2_;
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
    /// A combat at battlefield `bf` (cleanup sets the designations, CR 323.2).
    void combatAt(int bf, PlayerId attacker, PlayerId defender) {
        auto& b = s().battlefields[bf];
        b.combat_in_progress = true;
        b.attacker = attacker;
        b.defender = defender;
    }
    int might(GameObjectId id) { return s().getObject(id).current_might; }
    bool has(GameObjectId id, Keyword kw) { return s().getObject(id).hasKeyword(kw); }
};

TEST_F(TextAuraDoubleCountTest, RageAmplifier_GivesExactlyOneMight) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kRageAmplifier);
    auto mine = boardObject(P1, kOtterpus);
    auto theirs = boardObject(P2, kOtterpus);
    engine_->testHook_cleanup();
    EXPECT_EQ(might(mine), 3) << "2 + 1, not + 2";
    EXPECT_EQ(might(theirs), 2);
}

TEST_F(TextAuraDoubleCountTest, BannerOfConquest_NoAssaultForEnemyHere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kBannerOfConquest, 0);
    auto mine = boardObject(P1, kOtterpus, 0);
    auto theirs = boardObject(P2, kOtterpus, 0);
    engine_->testHook_cleanup();
    EXPECT_TRUE(has(mine, Keyword::Assault));
    EXPECT_FALSE(has(theirs, Keyword::Assault)) << "friendly units only";
    combatAt(0, P1, P2);
    engine_->testHook_cleanup();
    EXPECT_EQ(might(mine), 3) << "2 + [Assault] while attacking";
    EXPECT_EQ(might(theirs), 2);
}

TEST_F(TextAuraDoubleCountTest, EphemeralWard_GivesExactlyTwoMight) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kEphemeralWard);
    auto mine = boardObject(P1, kOtterpus, 1);
    auto theirs = boardObject(P2, kOtterpus, 1);
    engine_->testHook_cleanup();
    EXPECT_EQ(might(mine), 4);
    EXPECT_EQ(might(theirs), 2);
}

TEST_F(TextAuraDoubleCountTest, WujuMaster_BuffOnlyFromLevelSix) {
    makeEngine();
    setLegend(P1, kWujuMaster); setLegend(P2);
    auto mine = boardObject(P1, kOtterpus);
    s().player(P1).xp = 5;
    engine_->testHook_cleanup();
    EXPECT_EQ(might(mine), 2) << "no buff below Level 6";
    s().player(P1).xp = 6;
    engine_->testHook_cleanup();
    EXPECT_EQ(might(mine), 3) << "exactly +1 at Level 6";
}

TEST_F(TextAuraDoubleCountTest, CaptainFarron_OneAssaultBonusFriendlyOnly) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto farron = boardObject(P1, kCaptainFarron, 0);
    auto mine = boardObject(P1, kOtterpus, 0);
    auto theirs = boardObject(P2, kOtterpus, 0);
    combatAt(0, P1, P2);
    engine_->testHook_cleanup();
    EXPECT_EQ(might(mine), 3) << "2 + one [Assault]";
    // Farron has [Assault] printed; "other" means no aura grant on himself.
    for (const auto& ae : s().getObject(farron).aura_effects)
        EXPECT_NE(ae.keyword, Keyword::Assault) << "\"other\" units only";
    EXPECT_FALSE(has(theirs, Keyword::Assault));
}

TEST_F(TextAuraDoubleCountTest, GarenCommander_OnlyUnitsHere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kGarenCommander, 0);
    auto here = boardObject(P1, kOtterpus, 0);
    auto at_base = boardObject(P1, kOtterpus);
    engine_->testHook_cleanup();
    EXPECT_EQ(might(here), 3);
    EXPECT_EQ(might(at_base), 2) << "\"here\" only";
}

TEST_F(TextAuraDoubleCountTest, TaricProtector_ShieldFriendlyOnlyWithMight) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kTaricProtector, 0);
    auto mine = boardObject(P1, kOtterpus, 0);
    auto theirs = boardObject(P2, kOtterpus, 0);
    combatAt(0, P2, P1);
    engine_->testHook_cleanup();
    EXPECT_TRUE(has(mine, Keyword::Shield));
    EXPECT_EQ(might(mine), 3) << "2 + [Shield] while defending";
    EXPECT_FALSE(has(theirs, Keyword::Shield));
}

TEST_F(TextAuraDoubleCountTest, Rumble_MechsGetAssaultMight) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kRumbleHotheaded);
    auto mech = boardObject(P1, kMegaMech, 0);
    combatAt(0, P1, P2);
    engine_->testHook_cleanup();
    EXPECT_TRUE(has(mech, Keyword::Assault));
    EXPECT_EQ(might(mech), 9) << "8 + [Assault] while attacking";
}

TEST_F(TextAuraDoubleCountTest, Allay_NoDeflectForEnemyHere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kAllay, 0);
    auto mine = boardObject(P1, kOtterpus, 0);
    auto theirs = boardObject(P2, kOtterpus, 0);
    engine_->testHook_cleanup();
    EXPECT_TRUE(has(mine, Keyword::Deflect));
    EXPECT_FALSE(has(theirs, Keyword::Deflect));
}

TEST_F(TextAuraDoubleCountTest, Darius_OtherFriendlyUnitsHereOnly) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kDarius, 0);
    auto here = boardObject(P1, kOtterpus, 0);
    auto away = boardObject(P1, kOtterpus, 1);
    auto enemy = boardObject(P2, kOtterpus, 0);
    engine_->testHook_cleanup();
    EXPECT_EQ(might(here), 3);
    EXPECT_EQ(might(away), 2) << "\"here\" only";
    EXPECT_EQ(might(enemy), 2);
}

TEST_F(TextAuraDoubleCountTest, Leona_OnlyStunnedEnemiesHereShrink) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto leona = boardObject(P1, kLeona, 0);
    auto friend_here = boardObject(P1, kOtterpus, 0);
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto stunned = boardObject(P2, kOtterpus, 0);
    s().getObject(stunned).is_stunned = true;
    const int leona_might = s().getObject(leona).base_might;
    engine_->testHook_cleanup();
    EXPECT_EQ(might(leona), leona_might);
    EXPECT_EQ(might(friend_here), 2);
    EXPECT_EQ(might(enemy), 2) << "not stunned";
    EXPECT_EQ(might(stunned), 1) << "-8, to a minimum of 1";
}

}  // namespace
}  // namespace riftbound::test
