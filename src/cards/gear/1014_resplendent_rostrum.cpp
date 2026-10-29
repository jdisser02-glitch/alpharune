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

class ResplendentRostrum : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "[E]: Draw 1 and channel 1 rune exhausted."
    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility ab;
        ab.cost.exhaust = true;
        return {ab};
    }
    void onActivate(CardContext& ctx, int /*ability_index*/,
                    const std::vector<GameObjectId>& /*targets*/) override {
        ctx.executor.drawCards(ctx.controller, 1);
        ctx.executor.channelRunes(ctx.controller, 1, /*enter_exhausted=*/true);
        ctx.events.logTrace("RESPLENDENT ROSTRUM: draw 1, channel 1 rune exhausted");
    }

    // "When I become exhausted, give a friendly unit +3 [M] this turn."
    // The unit is chosen at finalization.
    TriggerType triggerType() const override { return TriggerType::WhenIBecomeExhausted; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto friendly = chooseableUnits(ctx.state, ctx.controller,
            [&](const GameObject& o) { return o.controller == ctx.controller; });
        std::sort(friendly.begin(), friendly.end());
        GameObjectId t = pickTarget(ctx, "Resplendent Rostrum: give a friendly unit +3 [M]", friendly);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;
        if (!ctx.state.getObject(t).location.has_value()) return;
        ctx.executor.giveTemporaryMight(t, 3);
        ctx.events.logTrace("RESPLENDENT ROSTRUM: exhausted -> " + ctx.state.getObject(t).name +
                            " +3 [M] this turn");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1014;
        d.def_id = R"RB(rad-046-167)RB";
        d.name = R"RB(Resplendent Rostrum)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-046/167)RB";
        d.collector_number = 46;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Calm};
        d.energy_cost = 8;
        d.power_cost = 1;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB([E]: Draw 1 and channel 1 rune exhausted.
When I become exhausted, give a friendly unit +3 [M] this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/df1c12e65cc3c63c96fb6c96b2071795f37f30a6-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1014(CardRegistry& r) {
    r.registerCard(1014, std::make_unique<ResplendentRostrum>());
}

}  // namespace riftbound
