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

// RECONSTRUCTED TEXT (not in cards/card_index.json; confidence: high).
// Gathered from web-search summaries (WebFetch to card sites was blocked);
// ability_text written in the repo's symbol convention with standard reminder
// text. GameRant reveal and retailers agree; [Disarm] (no number). Also
// printed as an Organized Play promo foil. Power cost not shown (assumed
// none). No image_url / artist.
class TrinketPeddler : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "[Disarm]" (CR 831): "When I attack, give an enemy unit here -1 [M]
    //  this turn." The enemy unit is chosen as the ability is finalized.
    TriggerType triggerType() const override { return TriggerType::WhenIAttack; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto enemies = disarmTargets(ctx, ctx.source);
        std::sort(enemies.begin(), enemies.end());
        const GameObjectId victim = pickTarget(ctx, "Trinket Peddler: [Disarm]", enemies);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        applyDisarm(ctx, victim, 1);          // no-op while waiting / without a target
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1114;
        d.def_id = R"RB(rad-054-167)RB";
        d.name = R"RB(Trinket Peddler)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-054/167)RB";
        d.collector_number = 54;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Bandle City)RB", R"RB(Yordle)RB"};
        d.energy_cost = 3;
        d.might = 3;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB([Disarm] (When I attack, give an enemy unit here -1 [M] this turn.))RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1114(CardRegistry& r) {
    r.registerCard(1114, std::make_unique<TrinketPeddler>());
}

}  // namespace riftbound
