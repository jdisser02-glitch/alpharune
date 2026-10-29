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

class HeraldOfTheEmperor : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play me, choose a friendly unit. Its base Might becomes 5 this
    //  turn." The unit (possibly me) is chosen as the ability is finalized.
    // As with Dragon Form, "base Might becomes 5 this turn" is a this-turn
    // Might delta of (5 - base Might): buffs, gear and auras still stack on
    // top, and it expires with the other "this turn" Might.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId picked = pickTarget(ctx, "Herald of the Emperor: base Might becomes 5",
            chooseableUnits(ctx.state, ctx.controller,
                            [&](const GameObject& o) { return o.controller == ctx.controller; }));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (picked == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the agent
        if (picked == kInvalidId || !ctx.state.objectExists(picked)) return;
        auto& unit = ctx.state.getObject(picked);
        if (!unit.location.has_value()) return;
        unit.temp_might_bonus += kNewBase - unit.base_might;
        unit.recomputeMight();
        ctx.events.logTrace("HERALD OF THE EMPEROR: " + unit.name +
                            " base Might is 5 this turn -> " +
                            std::to_string(unit.current_might) + "M");
    }
private:
    static constexpr int kNewBase = 5;
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1067;
        d.def_id = R"RB(rad-118-167)RB";
        d.name = R"RB(Herald of the Emperor)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-118/167)RB";
        d.collector_number = 118;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Order};
        d.energy_cost = 5;
        d.might = 3;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When you play me, choose a friendly unit. Its base Might becomes 5 this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/a168856002994f7ac67150d991e65152e6434ab6-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1067(CardRegistry& r) {
    r.registerCard(1067, std::make_unique<HeraldOfTheEmperor>());
}

}  // namespace riftbound
