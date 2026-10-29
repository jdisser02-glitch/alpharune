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

class InvigoratingBloom : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play this, give a friendly unit +3 [M] this turn."
    // The unit is chosen when the ability is finalized (CR 383.3).
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayThis; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto friendly = chooseableUnits(ctx.state, ctx.controller,
            [&](const GameObject& o) { return o.controller == ctx.controller; });
        GameObjectId tgt = pickTarget(ctx, "Invigorating Bloom: give a friendly unit +3 [M]",
                                      friendly);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (tgt == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the agent
        if (tgt == kInvalidId || !ctx.state.objectExists(tgt)) return;
        ctx.executor.giveTemporaryMight(tgt, 3);
        ctx.events.logTrace("INVIGORATING BLOOM: +3 [M] this turn to " +
                            ctx.state.getObject(tgt).name);
    }

    // "Exhaust a friendly [Mighty] unit, [E]: Draw 1."
    // The unit exhausted as the cost is the intent's target (paid by the
    // engine, ActivationCost::exhaust_friendly_unit).
    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility ab;
        ab.cost.exhaust = true;
        ab.cost.exhaust_friendly_unit = true;
        ab.targets = TargetRequirements{.count = 1, .must_be_unit = true, .must_be_friendly = true};
        return {ab};
    }
    /// Units that can pay the cost: ready, Mighty (5+ [M]) friendly units on the board.
    std::vector<GameObjectId> enumerateLegalTargets(
        const GameState& state, PlayerId controller, int /*ability_index*/) const override {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == controller && obj.location.has_value() &&
                !obj.is_exhausted && isMighty(obj))
                out.push_back(id);
        return out;
    }
    void onActivate(CardContext& ctx, int /*ability_index*/,
                    const std::vector<GameObjectId>& /*targets*/) override {
        ctx.executor.drawCards(ctx.controller, 1);
        ctx.events.logTrace("INVIGORATING BLOOM: exhausted a Mighty unit -> draw 1");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 979;
        d.def_id = R"RB(rad-035-167)RB";
        d.name = R"RB(Invigorating Bloom)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-035/167)RB";
        d.collector_number = 35;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Calm};
        d.energy_cost = 3;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you play this, give a friendly unit +3 [M] this turn.
Exhaust a friendly [Mighty] unit, [E]: Draw 1. (A unit is Mighty while it has 5+ [M].))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/66d4436ef5d7a7a81c60ee9b1b3ebae06162b7d2-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_979(CardRegistry& r) {
    r.registerCard(979, std::make_unique<InvigoratingBloom>());
}

}  // namespace riftbound
