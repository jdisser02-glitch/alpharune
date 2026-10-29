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

class HearteningSoul : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play me, ready a friendly legend." (A player has one legend.)
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId legend = ctx.state.player(ctx.controller).legend_zone;
        if (legend == kInvalidId || !ctx.state.objectExists(legend)) return;
        ctx.executor.readyObject(legend);
        ctx.events.logTrace("HEARTENING SOUL: ready " + ctx.state.getObject(legend).name);
    }

    // "Friendly legends have '[E]: Give a unit +3 [M] this turn.'"
    // Through the aura-granted-ability pipeline (as Forge of the Fluft):
    // applyPassiveAura grants this card's ability 0 to its controller's
    // legend, the legend pays [E], and resolution routes back to onActivate
    // with the legend as ctx.source. The unit is chosen as the ability is put
    // on the chain (needs_activation_time_target). Heartening Soul itself
    // does not have the ability (canActivateAbilityAt refuses non-legends).
    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility ab;
        ab.cost.exhaust = true;
        ab.targets = TargetRequirements{.count = 1, .must_be_unit = true};
        ab.needs_activation_time_target = true;
        return {ab};
    }
    bool canActivateAbilityAt(const GameState& state, PlayerId /*controller*/,
                              GameObjectId source, int /*ability_index*/) const override {
        return state.objectExists(source) && state.getObject(source).isLegend();
    }
    using Card::enumerateLegalTargets;
    std::vector<GameObjectId> enumerateLegalTargets(
        const GameState& state, PlayerId controller, int /*ability_index*/) const override {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects) {
            if (!obj.isUnit() || !obj.location.has_value()) continue;
            if (obj.controller != controller) {
                if (obj.untargetable_by_enemy) continue;
                // CR 809: an opponent must be able to pay Deflect to choose it.
                if (!state.chain.resuming.has_value() &&
                    deflectCostFor(state, controller, id) > payableAnyPower(state, controller))
                    continue;
            }
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    void onActivate(CardContext& ctx, int /*ability_index*/,
                    const std::vector<GameObjectId>& /*targets*/) override {
        const auto legal = enumerateLegalTargets(ctx.state, ctx.controller, 0);
        GameObjectId t = pickTarget(ctx, "Heartening Soul (legend): give a unit +3 [M]", legal);
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;   // waiting / mistarget
        if (!ctx.state.getObject(t).location.has_value()) return;
        ctx.executor.giveTemporaryMight(t, 3);
        ctx.events.logTrace("HEARTENING SOUL (granted): " + ctx.state.getObject(t).name +
                            " +3 [M] this turn");
    }
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        if (controller == PlayerId::None) return;
        const GameObjectId legend = state.player(controller).legend_zone;
        if (legend == kInvalidId || !state.objectExists(legend)) return;
        auto& refs = state.getObject(legend).granted_abilities;
        // One grant however many Heartening Souls are in play: the legend
        // "has" the ability, it doesn't gain a second copy of it.
        for (const auto& ref : refs)
            if (ref.source_def_id == cardDefId() && ref.ability_index == 0) return;
        refs.push_back({cardDefId(), 0});
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1012;
        d.def_id = R"RB(rad-040-167)RB";
        d.name = R"RB(Heartening Soul)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-040/167)RB";
        d.collector_number = 40;
        d.artist = R"RB(Dao Trong Le)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Calm};
        d.energy_cost = 7;
        d.power_cost = 1;
        d.might = 7;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When you play me, ready a friendly legend.
Friendly legends have "[E]: Give a unit +3 [M] this turn.")RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/0a5f9ab3216462356f6ddf8f172284dca6be21e9-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1012(CardRegistry& r) {
    r.registerCard(1012, std::make_unique<HearteningSoul>());
}

}  // namespace riftbound
