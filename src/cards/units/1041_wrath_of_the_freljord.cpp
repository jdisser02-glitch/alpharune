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

class WrathOfTheFreljord : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    // "I enter ready." ([Shield 3] is the engine keyword.)
    bool entersReadyOnPlay() const override { return true; }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1041;
        d.def_id = R"RB(rad-077-167)RB";
        d.name = R"RB(Wrath of the Freljord)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-077/167)RB";
        d.collector_number = 77;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.energy_cost = 9;
        d.might = 7;
        d.rarity = Rarity::Common;
        d.shield_value = 3;
        d.keywords.set(Keyword::Shield);
        d.ability_text = R"RB(I enter ready.
[Shield 3] (+3 [M] while I'm a defender.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/531b923f3437f497b858bf186b46b4ac88460da6-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1041(CardRegistry& r) {
    r.registerCard(1041, std::make_unique<WrathOfTheFreljord>());
}

}  // namespace riftbound
