#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "cards/card_helpers.h"

namespace riftbound {
namespace {

// RECONSTRUCTED TEXT (not in cards/card_index.json; confidence: high).
// Gathered from web-search summaries (WebFetch to card sites was blocked);
// ability_text written in the repo's symbol convention with standard reminder
// text. The LoL wiki lists cost 2 Energy with no Power symbols, so power cost
// 0. No image_url / artist.
class StrigianOracle : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When I become exhausted, [Predict]." (Predict 1: look at the top card
    // of your Main Deck; you may recycle it.)
    TriggerType triggerType() const override { return TriggerType::WhenIBecomeExhausted; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        ctx.executor.predict(ctx.controller, 1);
        ctx.events.logTrace("STRIGIAN ORACLE: [Predict]");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1113;
        d.def_id = R"RB(rad-053-167)RB";
        d.name = R"RB(Strigian Oracle)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-053/167)RB";
        d.collector_number = 53;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Freljord)RB", R"RB(Bird)RB"};
        d.energy_cost = 2;
        d.might = 2;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When I become exhausted, [Predict]. (Look at the top card of your Main Deck. You may recycle it.))RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1113(CardRegistry& r) {
    r.registerCard(1113, std::make_unique<StrigianOracle>());
}

}  // namespace riftbound
