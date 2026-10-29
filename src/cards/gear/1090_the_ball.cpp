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

class TheBall : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // [Ganking] is a printed keyword; it only matters while this is a unit
    // (Lady of Clockwork), when the engine's unit-move rules read it.
    // "When you play or move this, choose one — Draw 1. / Deal 2 to enemy
    //  units here." The mode is chosen at finalization; "here" is where this
    // is as the ability resolves. (It can only move while it is a unit.)
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayThis, TriggerType::WhenIMove};
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const int mode = pickMode(ctx, "The Ball", 2, {"Draw 1", "Deal 2 to enemy units here"});
        if (mode == -1) return;               // waiting for the agent
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (mode == 0) {
            ctx.executor.drawCards(ctx.controller, 1);
            ctx.events.logTrace("THE BALL: draw 1");
            return;
        }
        if (mode != 1 || !ctx.state.objectExists(ctx.source)) return;
        const auto& self = ctx.state.getObject(ctx.source);
        if (!self.location.has_value()) return;
        const LocationId here = *self.location;
        // AoE: collect, deal all the damage, then kill.
        std::vector<GameObjectId> hit;
        for (const auto& [id, o] : ctx.state.objects)
            if (o.isUnit() && o.controller != ctx.controller && o.location.has_value() &&
                *o.location == here)
                hit.push_back(id);
        for (auto id : hit) ctx.executor.dealDamage(id, 2, ctx.source);
        for (auto id : hit)
            if (ctx.state.objectExists(id) && ctx.state.getObject(id).location.has_value() &&
                ctx.state.getObject(id).hasLethalDamage())
                ctx.executor.killObject(id);
        ctx.events.logTrace("THE BALL: 2 to " + std::to_string(hit.size()) + " enemy unit(s) here");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1090;
        d.def_id = R"RB(rad-146-167)RB";
        d.name = R"RB(The Ball)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-146/167)RB";
        d.collector_number = 146;
        d.card_type = CardType::Gear;
        d.super_type = SuperType::Signature;
        d.domains = {Domain::Calm, Domain::Mind};
        d.tags = {R"RB(Device)RB", R"RB(Orianna)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.rarity = Rarity::Epic;
        d.keywords.set(Keyword::Ganking);
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
[Ganking] (While this is a unit, it can move from battlefield to battlefield.)
When you play or move this, choose one —
Draw 1.Deal 2 to enemy units here.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/4b11f36bd5ce2b08677959664cda3906a04d234c-744x1040.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1090(CardRegistry& r) {
    r.registerCard(1090, std::make_unique<TheBall>());
}

}  // namespace riftbound
