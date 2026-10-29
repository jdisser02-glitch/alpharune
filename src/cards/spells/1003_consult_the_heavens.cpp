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

class ConsultTheHeavens : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "[Reaction]. Give a unit [Shield 2] this turn. [Predict]."
    // The unit is chosen as the spell is played (needsPlayTimeTarget). If it
    // is no longer a legal target at resolution that instruction is ignored
    // and the Predict still happens.
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true};
    }
    bool needsPlayTimeTarget() const override { return true; }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId unit = pickTarget(ctx, "Consult the Heavens: give a unit [Shield 2]",
                                       enumerateLegalTargets(ctx.state, ctx.controller));
        if (unit == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (unit != kInvalidId && ctx.state.objectExists(unit))
            ctx.executor.giveTemporaryKeyword(unit, Keyword::Shield, 2);
        ctx.executor.predict(ctx.controller, 1);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1003;
        d.def_id = R"RB(rad-025-167)RB";
        d.name = R"RB(Consult the Heavens)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-025/167)RB";
        d.collector_number = 25;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Calm};
        d.energy_cost = 1;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Reaction);
        d.keywords.set(Keyword::Predict);
        d.ability_text = R"RB([Reaction] (Play any time, even before spells and abilities resolve.)
Give a unit [Shield 2] this turn. (+2 [M] while it's a defender.)
[Predict]. (Look at the top card of your Main Deck. You may recycle it.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/120b20ee26d221cea31f510090cef037e74c023f-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1003(CardRegistry& r) {
    r.registerCard(1003, std::make_unique<ConsultTheHeavens>());
}

}  // namespace riftbound
