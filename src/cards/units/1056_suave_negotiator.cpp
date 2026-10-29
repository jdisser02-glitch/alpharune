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

class SuaveNegotiator : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play me, choose an opponent. You and that player each draw 1."
    // In 1v1 the only opponent is chosen without a prompt.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const PlayerId them = opponent(ctx.controller);
        ctx.executor.drawCards(ctx.controller, 1);
        ctx.executor.drawCards(them, 1);
        ctx.events.logTrace("SUAVE NEGOTIATOR: you and the chosen opponent each draw 1");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1056;
        d.def_id = R"RB(rad-099-167)RB";
        d.name = R"RB(Suave Negotiator)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-099/167)RB";
        d.collector_number = 99;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.energy_cost = 3;
        d.might = 3;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When you play me, choose an opponent. You and that player each draw 1.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/20e2752ddf0af274d26a3c25c737ccea2c353e39-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1056(CardRegistry& r) {
    r.registerCard(1056, std::make_unique<SuaveNegotiator>());
}

}  // namespace riftbound
