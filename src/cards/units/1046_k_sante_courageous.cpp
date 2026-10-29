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

class KSanteCourageous : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // [Accelerate] and [Shield 2] are engine keywords.
    // "When I hold, channel 1 rune." (enters ready, as channelled runes do)
    TriggerType triggerType() const override { return TriggerType::WhenIHold; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        ctx.executor.channelRunes(ctx.controller, 1);
        ctx.events.logTrace("K'SANTE, COURAGEOUS: hold -> channel 1 rune");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1046;
        d.def_id = R"RB(rad-086-167)RB";
        d.name = R"RB(K'Sante, Courageous)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-086/167)RB";
        d.collector_number = 86;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Shurima)RB", R"RB(K'Sante)RB"};
        d.energy_cost = 5;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Accelerate);
        d.keywords.set(Keyword::Shield);
        d.shield_value = 2;
        d.ability_text = R"RB([Accelerate] (You may pay [1][O] as an additional cost to have me enter ready.)
[Shield 2] (+2 [M] while I'm a defender.)
When I hold, channel 1 rune.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/2a5edfa252798770045f8cd7cc50a93fbbb690fa-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1046(CardRegistry& r) {
    r.registerCard(1046, std::make_unique<KSanteCourageous>());
}

}  // namespace riftbound
