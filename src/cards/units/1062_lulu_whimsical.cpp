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

class LuluWhimsical : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When I move, you may have another unit's base Might become 4 this turn."
    // The "you may" and the unit (any other unit on the board, friendly or
    // enemy) are chosen at finalization. "Base Might becomes 4 this turn" is
    // modelled as Dragon Form does it: a this-turn Might delta of
    // (4 - base Might), so every other modifier still stacks on top and it
    // expires with the other "this turn" Might. GameObject::base_might itself
    // is left untouched because nothing would restore it at end of turn.
    TriggerType triggerType() const override { return TriggerType::WhenIMove; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto others = chooseableUnits(ctx.state, ctx.controller,
            [&](const GameObject& o) { return o.id != ctx.source; });
        std::sort(others.begin(), others.end());
        int go = confirmOptional(ctx, "Lulu, Whimsical: make another unit's base Might 4?",
                                 [&]() { return !others.empty(); });
        GameObjectId unit = kInvalidId;
        if (go == 1) unit = pickTarget(ctx, "Lulu, Whimsical: unit", others);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (go != 1) return;
        if (unit == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the unit
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        auto& u = ctx.state.getObject(unit);
        if (!u.location.has_value()) return;
        u.temp_might_bonus += 4 - u.base_might;
        u.recomputeMight();
        ctx.events.logTrace("LULU, WHIMSICAL: " + u.name + " base Might is 4 this turn -> " +
                            std::to_string(u.current_might) + "M");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1062;
        d.def_id = R"RB(rad-110-167)RB";
        d.name = R"RB(Lulu, Whimsical)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-110/167)RB";
        d.collector_number = 110;
        d.artist = R"RB(Polar Engine Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Yordle)RB", R"RB(Bandle City)RB", R"RB(Lulu)RB"};
        d.energy_cost = 3;
        d.might = 3;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When I move, you may have another unit's base Might become 4 this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/defd09480671a1e394725f21b6c3448ebec77913-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1062(CardRegistry& r) {
    r.registerCard(1062, std::make_unique<LuluWhimsical>());
}

}  // namespace riftbound
