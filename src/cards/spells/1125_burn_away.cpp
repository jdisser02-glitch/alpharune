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
// medium). Notes: the effect order and the Burn reminder wording are
// reconstructed; no timing keyword ([Action]/[Reaction]) was found in any
// source (none set); who burns ("you") is assumed.
class BurnAway : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "Banish a unit with 3 [M] or less or a gear. [Burn 2]."
    // The unit / gear is chosen as the spell is played (needsPlayTimeTarget:
    // the engine asks at finalization and charges Deflect). If it is no longer
    // legal at resolution (e.g. its Might rose above 3) it is not banished,
    // but the Burn still happens (CR 359.3.e.5).
    bool needsPlayTimeTarget() const override { return true; }
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1};
    }
    std::vector<GameObjectId> enumerateLegalTargets(const GameState& state,
                                                    PlayerId controller) const override {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects) {
            if (!obj.location.has_value()) continue;
            const bool small_unit = obj.isUnit() && obj.current_might <= 3;
            if (!small_unit && !obj.isGear()) continue;
            if (obj.controller != controller && obj.untargetable_by_enemy) continue;
            // CR 809: Deflect must be payable when the choice is offered (as in
            // Card::enumerateLegalTargets).
            if (!state.chain.resuming.has_value() && obj.controller != controller &&
                deflectCostFor(state, controller, id) > payableAnyPower(state, controller))
                continue;
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& targets) override {
        GameObjectId t = kInvalidId;
        if (!targets.empty()) {
            t = targets[0];
            const auto legal = enumerateLegalTargets(ctx.state, ctx.controller);
            if (std::find(legal.begin(), legal.end(), t) == legal.end()) t = kInvalidId;
        } else {
            t = pickTarget(ctx, "Burn Away: banish", enumerateLegalTargets(ctx.state, ctx.controller));
            if (t == kInvalidId && ctx.state.chain.resuming.has_value() &&
                ctx.state.chain.resuming->resume_point == 7)
                return;   // waiting for the agent
        }
        if (t != kInvalidId && ctx.state.objectExists(t) && ctx.state.getObject(t).location.has_value()) {
            const std::string name = ctx.state.getObject(t).name;
            ctx.executor.banishObject(t);
            ctx.events.logTrace("BURN AWAY: banished " + name);
        }
        ctx.executor.burn(ctx.controller, 2);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1125;
        d.def_id = R"RB(rad-124-167)RB";
        d.name = R"RB(Burn Away)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-124/167)RB";
        d.collector_number = 124;
        d.card_type = CardType::Spell;
        d.domains = {Domain::Order};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(Banish a unit with 3 [M] or less or a gear.
[Burn 2]. (Put the top 2 cards of your Main Deck into your trash.))RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1125(CardRegistry& r) {
    r.registerCard(1125, std::make_unique<BurnAway>());
}

}  // namespace riftbound
