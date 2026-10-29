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

class IridescentAvian : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "[E]: [Buff] a unit. If it's already buffed, give it +2 [M] this turn
    //  instead." The unit is chosen as the ability is put on the chain
    // (needs_activation_time_target: GameEngine::chooseTargetsAtFinalize asks
    // enumerateLegalTargets, pays any Deflect); resolution reads it back
    // through pickTarget. "Already buffed" is checked as it resolves.
    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility ab;
        ab.cost.exhaust = true;
        ab.targets = TargetRequirements{.count = 1, .must_be_unit = true};
        ab.needs_activation_time_target = true;
        return {ab};
    }
    using Card::enumerateLegalTargets;
    std::vector<GameObjectId> enumerateLegalTargets(
        const GameState& state, PlayerId controller, int /*ability_index*/) const override {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects) {
            if (!obj.isUnit() || !obj.location.has_value()) continue;
            if (obj.controller != controller) {
                if (obj.untargetable_by_enemy) continue;
                // CR 809: an opponent must be able to pay Deflect to choose it
                // (only when offering a choice; a resolving item already paid).
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
        GameObjectId t = pickTarget(ctx, "Iridescent Avian: buff a unit", legal);
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;   // waiting / mistarget
        auto& u = ctx.state.getObject(t);
        if (!u.location.has_value()) return;
        if (u.buff_count > 0) {
            ctx.executor.giveTemporaryMight(t, 2);
            ctx.events.logTrace("IRIDESCENT AVIAN: " + u.name + " already buffed -> +2 [M] this turn");
        } else {
            ctx.executor.buffUnit(t);
            ctx.events.logTrace("IRIDESCENT AVIAN: [Buff] " + u.name);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1005;
        d.def_id = R"RB(rad-027-167)RB";
        d.name = R"RB(Iridescent Avian)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-027/167)RB";
        d.collector_number = 27;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Calm};
        d.energy_cost = 3;
        d.might = 3;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB([E]: [Buff] a unit. If it's already buffed, give it +2 [M] this turn instead. (To buff a unit, give it a +1 [M] buff if it doesn't have one.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/73fde9189b222313dcf8c2abe79b38082c76961e-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1005(CardRegistry& r) {
    r.registerCard(1005, std::make_unique<IridescentAvian>());
}

}  // namespace riftbound
