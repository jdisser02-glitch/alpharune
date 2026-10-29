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

class RiptideMage : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // [Accelerate] is the keyword (engine-paid [1][B] at play).
    // "When you play me, give an enemy unit -3 [M] this turn." The enemy unit
    // is chosen as the ability is finalized.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto enemies = chooseableUnits(ctx.state, ctx.controller,
            [&](const GameObject& o) { return o.controller != ctx.controller; });
        std::sort(enemies.begin(), enemies.end());
        const GameObjectId victim = pickTarget(ctx, "Riptide Mage: give an enemy unit -3 [M]", enemies);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        ctx.executor.giveTemporaryMight(victim, -3);
        ctx.events.logTrace("RIPTIDE MAGE: " + ctx.state.getObject(victim).name + " -3 [M] this turn");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1024;
        d.def_id = R"RB(rad-058-167)RB";
        d.name = R"RB(Riptide Mage)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-058/167)RB";
        d.collector_number = 58;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Mind};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.might = 3;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Accelerate);
        d.ability_text = R"RB([Accelerate] (You may pay [1][B] as an additional cost to have me enter ready.)
When you play me, give an enemy unit -3 [M] this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/4e3c24c1c315656772d0bfd8d100bc7893e94994-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1024(CardRegistry& r) {
    r.registerCard(1024, std::make_unique<RiptideMage>());
}

}  // namespace riftbound
