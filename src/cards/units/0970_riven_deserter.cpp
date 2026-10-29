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

class RivenDeserter : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When another friendly unit becomes exhausted, give me +1 [M] this turn."
    TriggerType triggerType() const override {
        return TriggerType::WhenAnotherFriendlyUnitBecomesExhausted;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        ctx.executor.giveTemporaryMight(ctx.source, 1);
    }

    // "Exhaust a friendly unit: Ready a unit. Use this ability only once each
    //  turn." The unit exhausted as the cost is the intent's target (paid by
    // the engine, ActivationCost::exhaust_friendly_unit); the unit to ready is
    // picked as the ability resolves.
    static constexpr const char* kUsedKey = "__ability_used_turn_0";   // written by the engine
    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility ab;
        ab.cost.exhaust_friendly_unit = true;
        ab.targets = TargetRequirements{.count = 1, .must_be_unit = true, .must_be_friendly = true};
        return {ab};
    }
    bool canActivateAbilityAt(const GameState& state, PlayerId controller,
                              GameObjectId source, int ability_index) const override {
        if (!Card::canActivateAbilityAt(state, controller, source, ability_index)) return false;
        if (!state.objectExists(source)) return false;
        const auto& self = state.getObject(source);
        auto it = self.card_counters.find(kUsedKey);
        return it == self.card_counters.end() || it->second != state.turn.turn_number + 1;
    }
    /// Units that can pay the cost: ready friendly units on the board.
    std::vector<GameObjectId> enumerateLegalTargets(
        const GameState& state, PlayerId controller, int /*ability_index*/) const override {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == controller && obj.location.has_value() &&
                !obj.is_exhausted)
                out.push_back(id);
        return out;
    }
    void onActivate(CardContext& ctx, int /*ability_index*/,
                    const std::vector<GameObjectId>& /*targets*/) override {
        const auto exhausted = chooseableUnits(ctx.state, ctx.controller,
            [](const GameObject& o) { return o.is_exhausted; });
        GameObjectId picked = pickTarget(ctx, "Riven, Deserter: ready a unit", exhausted);
        if (picked == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (picked == kInvalidId || !ctx.state.objectExists(picked)) return;
        ctx.executor.readyObject(picked);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 970;
        d.def_id = R"RB(rad-114-167)RB";
        d.name = R"RB(Riven, Deserter)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-114/167)RB";
        d.collector_number = 114;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Riven)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.might = 5;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(When another friendly unit becomes exhausted, give me +1 [M] this turn.
Exhaust a friendly unit: Ready a unit. Use this ability only once each turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/59c080321a697164c9eeb6f0ad297ff0d0b85808-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_970(CardRegistry& r) {
    r.registerCard(970, std::make_unique<RivenDeserter>());
}

}  // namespace riftbound
