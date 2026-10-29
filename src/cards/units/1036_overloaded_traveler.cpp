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

class OverloadedTraveler : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "While there's a friendly gear with [Deploy] at my battlefield, I have
    //  +1 [M]."
    // applyPassiveAura has no CardRegistry to ask Card::deploys(), so the gear
    // is recognised by where it is: an unattached non-unit gear at a
    // battlefield. Cleanup recalls every other such gear to base before the
    // aura pass runs (recallMisplacedObjects precedes recalculateAuras, CR
    // 323.6 / 830.1.b.3), so after cleanup only [Deploy] gear remain there.
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        forEachSelfOnce(*this, state, controller, [&](GameObjectId sid, GameObject& self) {
            const auto here = self.battlefieldId();
            if (!here.has_value()) return;
            for (const auto& [id, obj] : state.objects) {
                if (!obj.isGear() || obj.controller != controller) continue;
                if (obj.attached_to.has_value() || obj.battlefieldId() != here) continue;
                auraMight(self, sid, 1);
                return;
            }
        });
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1036;
        d.def_id = R"RB(rad-072-167)RB";
        d.name = R"RB(Overloaded Traveler)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-072/167)RB";
        d.collector_number = 72;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.energy_cost = 2;
        d.might = 2;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(While there's a friendly gear with [Deploy] at my battlefield, I have +1 [M].)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/6db2839d8702b15ab62413028c0321c75e87801c-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1036(CardRegistry& r) {
    r.registerCard(1036, std::make_unique<OverloadedTraveler>());
}

}  // namespace riftbound
