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

class PillagedArmory : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play this or when you conquer here, [Buff] a friendly unit."
    // The unit is chosen as the ability is finalized; [Buff] gives it a +1 [M]
    // buff only if it doesn't have one.
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayThis, TriggerType::WhenYouConquerHere};
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId unit = pickTarget(ctx, "Pillaged Armory: buff a friendly unit",
                                       friendlyUnits(ctx));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (unit == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the agent
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        auto& u = ctx.state.getObject(unit);
        if (u.buff_count > 0) {
            ctx.events.logTrace("PILLAGED ARMORY: " + u.name + " already has a buff");
            return;
        }
        ctx.executor.buffUnit(unit);
        ctx.events.logTrace("PILLAGED ARMORY: buffed " + u.name);
    }
private:
    static std::vector<GameObjectId> friendlyUnits(const CardContext& ctx) {
        return chooseableUnits(ctx.state, ctx.controller,
                               [&](const GameObject& o) { return o.controller == ctx.controller; });
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1037;
        d.def_id = R"RB(rad-073-167)RB";
        d.name = R"RB(Pillaged Armory)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-073/167)RB";
        d.collector_number = 73;
        d.card_type = CardType::Gear;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 1;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
When you play this or when you conquer here, [Buff] a friendly unit. (Give it a +1 [M] buff if it doesn't have one.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/374248eb65ca1e90a434652073dbbd3a12a11eda-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1037(CardRegistry& r) {
    r.registerCard(1037, std::make_unique<PillagedArmory>());
}

}  // namespace riftbound
