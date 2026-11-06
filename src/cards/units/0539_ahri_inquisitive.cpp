#include "cards/card.h"
#include "cards/card_helpers.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace riftbound {
namespace {

class AhriInquisitive_539 : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "When I attack or defend, give an enemy unit here -2 [M] this turn, to a minimum of 1 [M]."
    // The unit is chosen as the trigger is finalized (CR 383.3). A triggered
    // ability's chain item carries no chosen targets, so the previous
    // version, which read the unit from `targets`, never did anything.
    static std::vector<GameObjectId> candidates(const CardContext& ctx) {
        const auto here = hereOf(ctx.state, ctx.source);
        if (!here.has_value()) return {};
        return chooseableUnitsSorted(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller != ctx.controller && o.battlefieldId() == here;
        });
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId t = pickTarget(ctx, "Ahri, Inquisitive: give an enemy unit here -2 [M]", candidates(ctx));
        if (waitingForTarget(ctx, t)) return;
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;
        ctx.executor.giveTemporaryMight(t, -2, /*minimum=*/1);
    }
    TriggerType triggerType() const override { return TriggerType::WhenIAttackOrDefend; }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 539;
        d.def_id = R"RB(sfd-227-221)RB";
        d.name = R"RB(Ahri, Inquisitive)RB";
        d.set_code = R"RB(SFD)RB";
        d.set_name = R"RB(Spiritforged)RB";
        d.public_code = R"RB(SFD-227/221)RB";
        d.collector_number = 227;
        d.artist = R"RB(Shawn Tan)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Ahri)RB"};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.might = 3;
        d.rarity = Rarity::Showcase;
        d.ability_text = R"RB(When I attack or defend, give an enemy unit here -2 [M] this turn, to a minimum of 1 [M].)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/d4c332e90c97e5e70b996598cd4a0f731b77fdef-744x1039.png?accountingTag=RB)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_539(CardRegistry& r) {
    r.registerCard(539, std::make_unique<AhriInquisitive_539>());
}

} // namespace riftbound
