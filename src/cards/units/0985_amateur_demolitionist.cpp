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
#include "cards/radiance_helpers.h"

namespace riftbound {
namespace {

class AmateurDemolitionist : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play me, play a Bomb gear token to a battlefield." The Bomb
    // ([Deploy]) may go to any battlefield; the controller picks it as the
    // ability resolves (a destination, not a target).
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const int bf = pickBombBattlefield(*this, ctx, "Amateur Demolitionist: Bomb to");
        if (bf < 0) return;   // waiting for the agent / no battlefield
        playBombToken(ctx, ctx.controller, static_cast<BattlefieldId>(bf));
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 985;
        d.def_id = R"RB(rad-001-167)RB";
        d.name = R"RB(Amateur Demolitionist)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-001/167)RB";
        d.collector_number = 1;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.energy_cost = 2;
        d.might = 1;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When you play me, play a Bomb gear token to a battlefield. (It has [Deploy]. When it dies, deal 2 to an enemy there and kill your other Bombs there.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/3dca5e04ff8dff983993f87d5a82a7fd56d7bc9c-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

// The Bomb token this card creates is registered with it, so it exists even
// before gear/1199_bomb.cpp's register function is listed in cards_init.cpp
// (registering it twice is harmless: CardRegistry::registerCard overwrites).
void register_card_985(CardRegistry& r) {
    r.registerCard(985, std::make_unique<AmateurDemolitionist>());
    register_card_1199(r);
}

}  // namespace riftbound
