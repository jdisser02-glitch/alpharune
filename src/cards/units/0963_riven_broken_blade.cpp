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

class RivenBrokenBlade : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When I become exhausted, choose one that hasn't been chosen this turn —
    //  [Buff] me. / Spend my buff to [Stun] an enemy unit. / Spend my buff to
    //  ready me." Modes already chosen this turn live in a per-turn bitmask on
    //  the object. The two "spend my buff" modes are offered only while I have
    //  a buff to spend (and, for the stun, an enemy unit to choose).
    TriggerType triggerType() const override { return TriggerType::WhenIBecomeExhausted; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& self = ctx.state.getObject(ctx.source);
        const int turn = ctx.state.turn.turn_number;
        if (self.card_counters["__rbb_modes_turn"] != turn) {
            self.card_counters["__rbb_modes_turn"] = turn;
            self.card_counters["__rbb_modes_mask"] = 0;
        }
        const uint32_t chosen = static_cast<uint32_t>(self.card_counters["__rbb_modes_mask"]);
        const auto enemies = chooseableUnits(ctx.state, ctx.controller,
            [&](const GameObject& o) { return o.controller != ctx.controller; });
        uint32_t legal = 0;
        if (!(chosen & 1u)) legal |= 1u;
        if (!(chosen & 2u) && self.buff_count > 0 && !enemies.empty()) legal |= 2u;
        if (!(chosen & 4u) && self.buff_count > 0) legal |= 4u;
        int mode = pickMode(ctx, "Riven, Broken Blade", 3,
                            {"Buff me", "Spend my buff: stun an enemy unit",
                             "Spend my buff: ready me"},
                            legal);
        GameObjectId victim = kInvalidId;
        if (mode == 1) victim = pickTarget(ctx, "Riven, Broken Blade: stun", enemies);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (mode < 0) return;                 // waiting for the agent / no legal mode
        if (mode == 1 && victim == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the target

        auto& me = ctx.state.getObject(ctx.source);
        me.card_counters["__rbb_modes_mask"] = static_cast<int>(
            static_cast<uint32_t>(me.card_counters["__rbb_modes_mask"]) | (1u << mode));
        if (mode == 0) {
            ctx.executor.buffUnit(ctx.source);
        } else {
            if (me.buff_count <= 0) return;   // the buff is gone: nothing to spend
            --me.buff_count;
            me.recomputeMight();
            if (mode == 1) {
                if (victim != kInvalidId && ctx.state.objectExists(victim))
                    ctx.executor.stunUnitBy(victim, ctx.source);
            } else {
                ctx.executor.readyObject(ctx.source);
            }
        }
        ctx.events.logTrace("RIVEN, BROKEN BLADE: mode " + std::to_string(mode));
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 963;
        d.def_id = R"RB(rad-043-167)RB";
        d.name = R"RB(Riven, Broken Blade)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-043/167)RB";
        d.collector_number = 43;
        d.artist = R"RB(JunHuan)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Riven)RB"};
        d.energy_cost = 4;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When I become exhausted, choose one that hasn't been chosen this turn —
[Buff] me. (Give me a +1 [M] buff if I don't have one.)Spend my buff to [Stun] an enemy unit. (It doesn't deal combat damage this turn.)Spend my buff to ready me.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/0c9ac0da1c144f46a1e0c5423f51f9b8f6ba295b-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_963(CardRegistry& r) {
    r.registerCard(963, std::make_unique<RivenBrokenBlade>());
}

}  // namespace riftbound
