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

class Supernova : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "Deal 10 to a unit. Draw 2."
    // The unit is chosen as the spell is played (needsPlayTimeTarget: the
    // engine records it at finalization). If it is no longer a legal target
    // as the spell resolves, that instruction is ignored and the draw still
    // happens.
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true};
    }
    bool needsPlayTimeTarget() const override { return true; }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId t = pickTarget(ctx, "Supernova: deal 10 to a unit",
                                    enumerateLegalTargets(ctx.state, ctx.controller));
        if (t == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (t != kInvalidId && ctx.state.objectExists(t) &&
            ctx.state.getObject(t).location.has_value()) {
            ctx.executor.dealDamage(t, 10, ctx.source);
            if (ctx.state.objectExists(t) && ctx.state.getObject(t).location.has_value() &&
                ctx.state.getObject(t).hasLethalDamage())
                ctx.executor.killObject(t);
        }
        ctx.executor.drawCards(ctx.controller, 2);
        ctx.events.logTrace("SUPERNOVA: deal 10, draw 2");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1025;
        d.def_id = R"RB(rad-059-167)RB";
        d.name = R"RB(Supernova)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-059/167)RB";
        d.collector_number = 59;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Order};
        d.energy_cost = 9;
        d.power_cost = 1;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(Deal 10 to a unit. Draw 2.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/bea8e402f99b0afb5af1b19c0ee8ea34052e736f-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1025(CardRegistry& r) {
    r.registerCard(1025, std::make_unique<Supernova>());
}

}  // namespace riftbound
