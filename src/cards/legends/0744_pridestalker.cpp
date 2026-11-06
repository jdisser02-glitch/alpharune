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

class Pridestalker : public LegendCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "When you play a unit, give a unit +1 [M] this turn."
    // The unit is chosen as the trigger is finalized (CR 383.3). A triggered
    // ability's chain item carries no chosen targets, so the previous
    // version, which read the unit from `targets`, never did anything.
    static std::vector<GameObjectId> candidates(const CardContext& ctx) {
        return chooseableUnitsSorted(ctx.state, ctx.controller, [](const GameObject&) { return true; });
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId t = pickTarget(ctx, "Pridestalker: give a unit +1 [M]", candidates(ctx));
        if (waitingForTarget(ctx, t)) return;
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;
        ctx.executor.giveTemporaryMight(t, 1);
    }
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayAUnit; }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 744;
        d.def_id = R"RB(unl-183-219)RB";
        d.name = R"RB(Pridestalker)RB";
        d.set_code = R"RB(UNL)RB";
        d.set_name = R"RB(Unleashed)RB";
        d.public_code = R"RB(UNL-183/219)RB";
        d.collector_number = 183;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Fury, Domain::Body};
        d.tags = {R"RB(Rengar)RB"};
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When you play a unit, give a unit +1 [M] this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b3d3085f62aee993b9f5b80d4659a88439da83be-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_744(CardRegistry& r) {
    r.registerCard(744, std::make_unique<Pridestalker>());
}

} // namespace riftbound
