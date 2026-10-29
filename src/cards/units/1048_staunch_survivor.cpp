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

// "[Assault 2], [Shield 2]" — engine keywords with values; no other text.
class StaunchSurvivor : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1048;
        d.def_id = R"RB(rad-088-167)RB";
        d.name = R"RB(Staunch Survivor)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-088/167)RB";
        d.collector_number = 88;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.energy_cost = 2;
        d.power_cost = 1;
        d.might = 2;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Assault);
        d.assault_value = 2;
        d.keywords.set(Keyword::Shield);
        d.shield_value = 2;
        d.ability_text = R"RB([Assault 2], [Shield 2] (+2 [M] while I'm an attacker or defender.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/91c4670531d7773cae9e7b4bfd5ac4e259602162-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1048(CardRegistry& r) {
    r.registerCard(1048, std::make_unique<StaunchSurvivor>());
}

}  // namespace riftbound
