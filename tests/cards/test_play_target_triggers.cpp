/// @file test_play_target_triggers.cpp
/// "When you play me, <do something to a chosen unit>" on units. A unit's
/// play carries no targets into its trigger (addPermanent drops them and
/// fireTrigger adds the ability with none), so these cards choose their unit
/// as the ability is finalized (Card::pickTarget / pickTargetPair).

#include "tests/cards/card_test_fixture.h"

#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "engine/effect_executor.h"

#include <gtest/gtest.h>

#include <vector>

namespace riftbound::test {
namespace {

constexpr CardDefId kZauniteBouncer  = 188;
constexpr CardDefId kHarnessedDragon = 234;
constexpr CardDefId kDangerousDuo    = 16;
constexpr CardDefId kBeastBelow      = 453;
constexpr CardDefId kFieldMusicians  = 813;
constexpr CardDefId kPunchingPoro    = 794;

class PlayTargetTriggerTest : public CardTestFixture {
protected:
    std::vector<GameObjectId> offered;   // objects offered by the last object prompt
    std::function<Intent(const std::vector<Intent>&)> pick(GameObjectId want) {
        return [this, want](const std::vector<Intent>& legal) {
            offered.clear();
            for (const auto& i : legal)
                if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects[0]);
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects[0] == want) return i;
            return legal.front();
        };
    }
    bool wasOffered(GameObjectId id) const {
        return std::find(offered.begin(), offered.end(), id) != offered.end();
    }
    void play(CardDefId def, GameObjectId self, EffectExecutor& exec,
              std::function<Intent(const std::vector<Intent>&)> picker) {
        driveResumableTrigger(def, P1, self, std::move(picker), exec, TriggerType::WhenYouPlayMe);
    }
};

TEST_F(PlayTargetTriggerTest, ZauniteBouncer_ReturnsTheChosenUnitAtABattlefield) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto self = addUnit(P1, kZauniteBouncer, 0, 0);
    auto friendly_bf = addUnit(P1, kPunchingPoro, 0, 1);
    auto enemy_bf = addUnit(P2, kPunchingPoro, 0, 0);
    auto enemy_base = addUnit(P2, kPunchingPoro, 0, -1);

    play(kZauniteBouncer, self, exec, pick(enemy_bf));

    EXPECT_TRUE(wasOffered(friendly_bf));
    EXPECT_FALSE(wasOffered(self)) << "another unit";
    EXPECT_FALSE(wasOffered(enemy_base)) << "at a battlefield";
    EXPECT_TRUE(inHand(P2, enemy_bf));
    EXPECT_FALSE(state.getObject(enemy_bf).location.has_value());
    EXPECT_TRUE(state.getObject(self).isAtBattlefield());
}

TEST_F(PlayTargetTriggerTest, HarnessedDragon_KillsTheChosenEnemyUnit) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto self = addUnit(P1, kHarnessedDragon, 0, 0);
    auto friendly = addUnit(P1, kPunchingPoro, 0, 1);
    auto enemy_a = addUnit(P2, kPunchingPoro, 0, 0);
    auto enemy_b = addUnit(P2, kPunchingPoro, 0, -1);

    play(kHarnessedDragon, self, exec, pick(enemy_b));

    EXPECT_FALSE(wasOffered(friendly)) << "an enemy unit";
    EXPECT_FALSE(wasOffered(self));
    EXPECT_FALSE(state.getObject(enemy_b).location.has_value()) << "killed";
    EXPECT_TRUE(inTrash(P2, enemy_b));
    EXPECT_TRUE(state.getObject(enemy_a).location.has_value());
}

TEST_F(PlayTargetTriggerTest, DangerousDuo_WithLegionGivesTheChosenUnitPlusTwo) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto self = addUnit(P1, kDangerousDuo, 0, 0);
    auto buddy = addUnit(P1, kPunchingPoro, 0, 1);
    const int before = state.getObject(buddy).current_might;

    state.player(P1).cards_played_this_turn = 1;   // only the Duo: no Legion
    play(kDangerousDuo, self, exec, pick(buddy));
    EXPECT_TRUE(offered.empty()) << "nothing is asked without Legion";
    EXPECT_EQ(state.getObject(buddy).current_might, before);

    state.player(P1).cards_played_this_turn = 2;   // another card was played
    play(kDangerousDuo, self, exec, pick(buddy));
    EXPECT_TRUE(wasOffered(self)) << "a unit: the Duo itself may be chosen";
    EXPECT_EQ(state.getObject(buddy).current_might, before + 2);
}

TEST_F(PlayTargetTriggerTest, BeastBelow_ReturnsAnotherFriendlyAndAnEnemyUnit) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto self = addUnit(P1, kBeastBelow, 0, 0);
    auto friendly = addUnit(P1, kPunchingPoro, 0, 1);
    auto friendly2 = addUnit(P1, kPunchingPoro, 0, -1);
    auto enemy = addUnit(P2, kPunchingPoro, 0, 0);
    auto enemy2 = addUnit(P2, kPunchingPoro, 0, -1);

    bool self_offered = false;
    int prompt = 0;
    play(kBeastBelow, self, exec, [&](const std::vector<Intent>& legal) {
        for (const auto& i : legal)
            if (!i.chosen_objects.empty() && i.chosen_objects[0] == self) self_offered = true;
        const GameObjectId want = (prompt++ == 0) ? friendly : enemy;
        for (const auto& i : legal)
            if (!i.chosen_objects.empty() && i.chosen_objects[0] == want) return i;
        return legal.front();
    });

    EXPECT_EQ(prompt, 2);
    EXPECT_FALSE(self_offered) << "ANOTHER friendly unit";
    EXPECT_TRUE(inHand(P1, friendly));
    EXPECT_TRUE(inHand(P2, enemy));
    EXPECT_TRUE(state.getObject(friendly2).location.has_value());
    EXPECT_TRUE(state.getObject(enemy2).location.has_value());
    EXPECT_TRUE(state.getObject(self).isAtBattlefield());
}

TEST_F(PlayTargetTriggerTest, BeastBelow_WithNoOtherFriendlyUnitStillReturnsAnEnemy) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto self = addUnit(P1, kBeastBelow, 0, 0);
    auto enemy = addUnit(P2, kPunchingPoro, 0, 0);

    play(kBeastBelow, self, exec, pick(enemy));

    EXPECT_TRUE(inHand(P2, enemy));
    EXPECT_TRUE(state.getObject(self).isAtBattlefield());
}

TEST_F(PlayTargetTriggerTest, FieldMusicians_GivesTheChosenUnitPlusThree) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto self = addUnit(P1, kFieldMusicians, 0, 0);
    auto buddy = addUnit(P1, kPunchingPoro, 0, 1);
    const int before = state.getObject(buddy).current_might;

    play(kFieldMusicians, self, exec, pick(buddy));

    EXPECT_EQ(state.getObject(buddy).current_might, before + 3);
    EXPECT_EQ(state.getObject(self).current_might, 3);
}

}  // namespace
}  // namespace riftbound::test
