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

class SneakyZeebles : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    // "When I conquer, [Add] [A]."
    TriggerType triggerType() const override { return TriggerType::WhenIConquer; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        ctx.executor.addFloatingUniversalPower(ctx.controller, 1);
        ctx.events.logTrace("SNEAKY ZEEBLES: conquered -> [Add] [A]");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1040;
        d.def_id = R"RB(rad-076-167)RB";
        d.name = R"RB(Sneaky Zeebles)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-076/167)RB";
        d.collector_number = 76;
        d.artist = R"RB(Dao Trong Le)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.energy_cost = 2;
        d.might = 1;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When I conquer, [Add] [A]. (Abilities that add resources can't be reacted to.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/c04963c90ab2b338883c980ac947b8c9d80a7427-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1040(CardRegistry& r) {
    r.registerCard(1040, std::make_unique<SneakyZeebles>());
}

}  // namespace riftbound
