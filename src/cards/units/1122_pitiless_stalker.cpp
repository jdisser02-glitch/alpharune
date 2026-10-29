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

// RECONSTRUCTED TEXT: this card is not in cards/card_index.json. Its data and
// ability text were reconstructed from web-search summaries (confidence:
// high). Notes: gathered from web-search summaries of retailer / wiki pages;
// ability_text written in the repo's symbol convention. In the Evelynn
// Showdown precon.
class PitilessStalker : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "[E]: [Stun] an enemy unit." The enemy unit is chosen as the ability is
    // activated (needs_activation_time_target: the engine asks at
    // finalization and charges Deflect); pickTarget returns that choice at
    // resolution, or nothing if it is no longer legal.
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true, .must_be_enemy = true};
    }
    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility ab;
        ab.cost.exhaust = true;
        ab.targets = getTargetRequirements();
        ab.needs_activation_time_target = true;
        return {ab};
    }
    void onActivate(CardContext& ctx, int /*ability_index*/,
                    const std::vector<GameObjectId>& targets) override {
        GameObjectId victim = kInvalidId;
        if (!targets.empty()) {
            victim = targets[0];
        } else {
            victim = pickTarget(ctx, "Pitiless Stalker: stun",
                                enumerateLegalTargets(ctx.state, ctx.controller));
            if (victim == kInvalidId && ctx.state.chain.resuming.has_value() &&
                ctx.state.chain.resuming->resume_point == 7)
                return;   // waiting for the agent
        }
        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        const auto& v = ctx.state.getObject(victim);
        if (!v.isUnit() || !v.location.has_value() || v.controller == ctx.controller) return;
        ctx.executor.stunUnitBy(victim, ctx.source);
        ctx.events.logTrace("PITILESS STALKER: stunned " + ctx.state.getObject(victim).name);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1122;
        d.def_id = R"RB(rad-104-167)RB";
        d.name = R"RB(Pitiless Stalker)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-104/167)RB";
        d.collector_number = 104;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Shadow Isles)RB", R"RB(Cat)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.might = 5;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB([E]: [Stun] an enemy unit. (It doesn't deal combat damage this turn.))RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1122(CardRegistry& r) {
    r.registerCard(1122, std::make_unique<PitilessStalker>());
}

}  // namespace riftbound
