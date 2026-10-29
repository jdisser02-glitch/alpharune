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

class MysteriousChronomancer : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play me, [Predict 2]."
    // "[Disarm 2]" (CR 831): "When I attack, give an enemy unit here -2 [M]
    //  this turn." The enemy unit is chosen as the ability is finalized.
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayMe, TriggerType::WhenIAttack};
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::WhenIAttack) {
            const auto enemies = disarmTargets(ctx, ctx.source);
            const GameObjectId victim = pickTarget(ctx, "Mysterious Chronomancer: [Disarm 2]", enemies);
            if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
            applyDisarm(ctx, victim, 2);          // no-op while waiting / without a target
            return;
        }
        if (finalizingChoices(ctx)) return;       // no choices
        ctx.executor.predict(ctx.controller, 2);
        ctx.events.logTrace("MYSTERIOUS CHRONOMANCER: [Predict 2]");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1018;
        d.def_id = R"RB(rad-050-167)RB";
        d.name = R"RB(Mysterious Chronomancer)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-050/167)RB";
        d.collector_number = 50;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Mind};
        d.energy_cost = 5;
        d.might = 4;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When you play me, [Predict 2]. (Look at the top 2 cards of your Main Deck. Recycle any of them and put the rest back in any order.)
[Disarm 2] (When I attack, give an enemy unit here -2 [M] this turn.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/72828b0fe019b2e03069510f781dfa8f5759ac12-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1018(CardRegistry& r) {
    r.registerCard(1018, std::make_unique<MysteriousChronomancer>());
}

}  // namespace riftbound
