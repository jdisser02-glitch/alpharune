#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace riftbound {
namespace {

class NeekoBlendingIn : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "(Neutral cards can go in decks of any Domain.)
    //  If Neeko is in your deck, choose 3 different legends in addition to your
    //  starting legend. (Different means none match names.)
    //  When you play me, choose one of those legends and add it to your Legend
    //  Zone while I'm on the board."
    //
    // ENGINE GAP (not implemented): deck construction has no "choose 3 extra
    // legends" step (decks name one legend), and a player's Legend Zone holds
    // exactly one legend (PlayerState::legend_zone), which is the only one the
    // legend trigger sweeps consult. With no extra legends chosen there is
    // nothing for "When you play me" to add, so Neeko plays as a 4 [M]
    // neutral champion with no play effect. Wiring needs: a deck-level list of
    // the 3 extra legends, a multi-legend Legend Zone (every legend sweep and
    // legend aura iterating it), and removal of the added legend when Neeko
    // leaves the board.
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1106;
        d.def_id = R"RB(rad-167-167)RB";
        d.name = R"RB(Neeko, Blending In)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-167/167)RB";
        d.collector_number = 167;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.tags = {R"RB(Ixtal)RB", R"RB(Neeko)RB"};
        d.energy_cost = 6;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB((Neutral cards can go in decks of any Domain.)
If Neeko is in your deck, choose 3 different legends in addition to your starting legend. (Different means none match names.)
When you play me, choose one of those legends and add it to your Legend Zone while I'm on the board.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/607da6f534ce1b960ad02586ed4cd47c996e2c42-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1106(CardRegistry& r) {
    r.registerCard(1106, std::make_unique<NeekoBlendingIn>());
}

}  // namespace riftbound
