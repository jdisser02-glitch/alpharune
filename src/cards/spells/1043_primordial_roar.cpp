#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "cards/card_helpers.h"

namespace riftbound {
namespace {

class PrimordialRoar : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "[Show Off] a unit. If you showed off a unit, deal damage equal to that
    //  unit's Might to a unit."
    // Showing off costs only information, which the engine does not model, so
    // the player shows off whenever a unit can be revealed from hand or picked
    // among friendly units, and shows off the one with the most Might. The
    // Might is read when the spell is finalized (CR 832.1.d.1) and kept in
    // resume_data[kShownMightSlot] (stored + 1; 0 = nothing shown off), so
    // later changes to the unit don't matter. The damaged unit is chosen then
    // too.
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto& ri = ctx.state.chain.resuming;
        int shown = -1;
        if (!ri.has_value() || ri->finalizing || !ri->choices_finalized) {
            shown = bestShowOff(ctx, ctx.source,
                                [](const GameObject& o) { return o.isUnit(); },
                                [&](const GameObject& o) { return mightOf(ctx, o); });
            if (ri.has_value()) {
                while (ri->resume_data.size() <= kShownMightSlot) ri->resume_data.push_back(0);
                ri->resume_data[kShownMightSlot] = shown < 0 ? 0 : shown + 1;
            }
        } else if (ri->resume_data.size() > kShownMightSlot) {
            shown = ri->resume_data[kShownMightSlot] - 1;
        }
        GameObjectId target = kInvalidId;
        if (shown >= 0)
            target = pickTarget(ctx, "Primordial Roar: unit to deal " + std::to_string(shown) + " to",
                                chooseableUnits(ctx.state, ctx.controller,
                                                [](const GameObject&) { return true; }));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (shown < 0) {
            ctx.events.logTrace("PRIMORDIAL ROAR: no unit to show off");
            return;
        }
        if (target == kInvalidId && ri.has_value() && ri->resume_point == 7)
            return;                           // waiting for the agent
        if (target == kInvalidId || !ctx.state.objectExists(target)) return;
        ctx.events.logTrace("PRIMORDIAL ROAR: showed off " + std::to_string(shown) +
                            " [M] -> deal " + std::to_string(shown) + " to " +
                            ctx.state.getObject(target).name);
        if (shown <= 0) return;
        ctx.executor.dealDamage(target, shown, ctx.source);
        if (ctx.state.objectExists(target) && ctx.state.getObject(target).location.has_value() &&
            ctx.state.getObject(target).hasLethalDamage())
            ctx.executor.killObject(target);
    }
private:
    static constexpr size_t kShownMightSlot = 6;   // past the helpers' slots 0-4 and the engine's 5
    // A unit on the board shows its current Might; a card in hand its printed Might.
    static int mightOf(const CardContext& ctx, const GameObject& o) {
        if (o.location.has_value()) return o.current_might;
        int m = o.base_might;
        if (o.card_def_id != kInvalidId) {
            try { m = ctx.executor.cardDB().get(o.card_def_id).might; }
            catch (const std::out_of_range&) {}
        }
        return m;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1043;
        d.def_id = R"RB(rad-082-167)RB";
        d.name = R"RB(Primordial Roar)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-082/167)RB";
        d.collector_number = 82;
        d.artist = R"RB(Polar Engine Studio)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Body};
        d.energy_cost = 4;
        d.power_cost = 2;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB([Show Off] a unit. (As you play this, you may reveal a unit from hand or pick a friendly unit.)
If you showed off a unit, deal damage equal to that unit's Might to a unit.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/945633b0c2ed7f303245c917ffbcdb80dcfb524e-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1043(CardRegistry& r) {
    r.registerCard(1043, std::make_unique<PrimordialRoar>());
}

}  // namespace riftbound
