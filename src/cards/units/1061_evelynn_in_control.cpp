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

class EvelynnInControl : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "[Disarm] (When I attack, give an enemy unit here -1 [M] this turn.)
    //  When I hold, if you control a facedown card here, score 1 point."
    // [Disarm 1] (CR 831): the enemy unit here is chosen at finalization.
    // The hold ability's "if" is an intervening condition: checked when the
    // hold happens (triggerCondition) and again as it resolves.
    static constexpr int kDisarmValue = 1;
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenIAttack, TriggerType::WhenIHold};
    }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType trigger, GameObjectId /*subject*/) const override {
        if (trigger != TriggerType::WhenIHold) return true;
        return controlsFacedownHere(state, source, controller);
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::WhenIHold) {
            if (finalizingChoices(ctx)) return;   // no choices (CR 383.3)
            if (!controlsFacedownHere(ctx.state, ctx.source, ctx.controller)) return;
            const int got = ctx.executor.scorePoints(ctx.controller, 1);
            ctx.events.logTrace("EVELYNN, IN CONTROL: held with a facedown card here -> scored " +
                                std::to_string(got));
            return;
        }
        if (!ctx.state.objectExists(ctx.source)) { if (finalizingChoices(ctx)) return; return; }
        auto candidates = disarmTargets(ctx, ctx.source);
        std::sort(candidates.begin(), candidates.end());
        GameObjectId victim = pickTarget(ctx, "Evelynn, In Control: [Disarm] an enemy unit here",
                                         candidates);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        if (!ctx.state.getObject(victim).location.has_value()) return;
        applyDisarm(ctx, victim, kDisarmValue);
    }
private:
    static bool controlsFacedownHere(const GameState& state, GameObjectId source, PlayerId controller) {
        if (!state.objectExists(source)) return false;
        const auto here = state.getObject(source).battlefieldId();
        if (!here.has_value()) return false;
        for (const auto& bf : state.battlefields) {
            if (bf.id != *here) continue;
            for (auto cid : bf.facedown)
                if (state.objectExists(cid) && state.getObject(cid).controller == controller) return true;
        }
        return false;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1061;
        d.def_id = R"RB(rad-109-167)RB";
        d.name = R"RB(Evelynn, In Control)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-109/167)RB";
        d.collector_number = 109;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Evelynn)RB"};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([Disarm] (When I attack, give an enemy unit here -1 [M] this turn.)
When I hold, if you control a facedown card here, score 1 point.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/8c17158e6153eddb4b56abd8a09cbbd20600b049-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1061(CardRegistry& r) {
    r.registerCard(1061, std::make_unique<EvelynnInControl>());
}

}  // namespace riftbound
