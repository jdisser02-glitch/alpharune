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

class WatchfulCurator : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "Your gear have [Deflect]." A continuous effect: each pass, every gear
    // its controller has on the board (attached or not) gets an aura-granted
    // [Deflect] (deflectCostFor counts each aura Deflect at value 1). Each
    // Curator grants its own instance; granted Deflect values are summed
    // (CR 809.2).
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        forEachSelfOnce(*this, state, controller, [&](GameObjectId sid, GameObject& /*self*/) {
            for (auto& [gid, g] : state.objects) {
                if (!g.isGear() || g.controller != controller || !g.location.has_value()) continue;
                auraKeyword(g, sid, Keyword::Deflect, 1);
            }
        });
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1009;
        d.def_id = R"RB(rad-037-167)RB";
        d.name = R"RB(Watchful Curator)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-037/167)RB";
        d.collector_number = 37;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Calm};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(Your gear have [Deflect]. (Opponents must pay [A] to choose each with a spell or ability.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/5081f81341bb02dfd26c970c09651fcf64a18a04-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1009(CardRegistry& r) {
    r.registerCard(1009, std::make_unique<WatchfulCurator>());
}

}  // namespace riftbound
