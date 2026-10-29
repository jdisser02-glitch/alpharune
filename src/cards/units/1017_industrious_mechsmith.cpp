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

class IndustriousMechsmith : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play me, play a 3 [M] Mech unit token here." "Here" is where I
    // am as the ability resolves (my base or a battlefield). The token is a
    // domainless 3 [M] unit with the Mech tag and, like any unit played,
    // enters exhausted (CR 143.4).
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        const auto& self = ctx.state.getObject(ctx.source);
        if (!self.location.has_value()) return;   // left the board: no "here"
        const LocationId here = *self.location;
        ctx.executor.createToken(ctx.controller, CardType::Unit, "Mech", 3, {"Mech"},
                                 KeywordSet{}, here, /*enter_ready=*/false);
        ctx.events.logTrace("INDUSTRIOUS MECHSMITH: played a 3 [M] Mech token here");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1017;
        d.def_id = R"RB(rad-049-167)RB";
        d.name = R"RB(Industrious Mechsmith)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-049/167)RB";
        d.collector_number = 49;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {};
        d.energy_cost = 4;
        d.might = 1;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When you play me, play a 3 [M] Mech unit token here.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/c8f6b4191874f23b522566f8ebeeb6e5ee18eb7f-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1017(CardRegistry& r) {
    r.registerCard(1017, std::make_unique<IndustriousMechsmith>());
}

}  // namespace riftbound
