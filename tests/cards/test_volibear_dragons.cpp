// Volibear Dragons (Ritualo's deck profile), Jeremy's Play games of 8 Nov.
#include "tests/cards/card_test_fixture.h"
#include <gtest/gtest.h>

using namespace riftbound;
using namespace riftbound::test;

namespace {
constexpr CardDefId kRelentlessStorm = 295;   // legend: "When you play a [Mighty] unit, you may exhaust me to channel 1 rune exhausted."
constexpr CardDefId kBilgewaterTracker = 973; // 3 [M]
constexpr CardDefId kGentleGemdragon = 666;   // 8 [M]
}

class VolibearDragonsTest : public CardTestFixture {};

// The unit just played decides: a 3-might Bilgewater Tracker does not fire Relentless Storm, even with a Mighty
// Gentle Gemdragon already in play (the card used to look for any Mighty friendly unit).
TEST_F(VolibearDragonsTest, RelentlessStorm_OnlyTheMightyUnitJustPlayedFiresIt) {
    auto legend = state.createObject();
    auto gem = addUnit(P1, kGentleGemdragon);
    auto tracker = addUnit(P1, kBilgewaterTracker);
    const Card* c = card_registry.get(kRelentlessStorm);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->triggerCondition(state, legend, P1, TriggerType::WhenYouPlayAUnit, tracker));
    EXPECT_TRUE(c->triggerCondition(state, legend, P1, TriggerType::WhenYouPlayAUnit, gem));
    EXPECT_FALSE(c->triggerCondition(state, legend, P1, TriggerType::WhenYouPlayAUnit, kInvalidId));
}
