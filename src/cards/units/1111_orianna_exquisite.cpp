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

// RECONSTRUCTED TEXT (not in cards/card_index.json; confidence: medium).
// Gathered from web-search summaries (WebFetch to card sites was blocked);
// ability_text written in the repo's symbol convention with standard reminder
// text. Champion unit (the Orianna legend is RAD-145). Showcase alt art
// RAD-042a. Power cost not shown by any source (assumed none, unverified).
// No image_url / artist.
class OriannaExquisite : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // [Shield] is the engine keyword (Shield 1).
    // "I don't deal combat damage unless there's a friendly gear here."
    // A self aura recomputed every cleanup: while no gear my controller
    // controls shares my location (a gear attached to a unit here counts —
    // attached gear is at its unit's location), I suppress my combat damage.
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        forEachSelfOnce(*this, state, controller, [&](GameObjectId sid, GameObject& self) {
            if (!self.location.has_value()) return;
            for (const auto& [gid, g] : state.objects)
                if (g.isGear() && g.controller == controller && g.location.has_value() &&
                    *g.location == *self.location)
                    return;   // a friendly gear here: I deal combat damage
            GameObject::AuraEffect ae;
            ae.source = sid;
            ae.suppress_combat_damage = true;
            self.aura_effects.push_back(ae);
        });
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1111;
        d.def_id = R"RB(rad-042-167)RB";
        d.name = R"RB(Orianna, Exquisite)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-042/167)RB";
        d.collector_number = 42;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Orianna)RB", R"RB(Piltover)RB"};
        d.energy_cost = 3;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.shield_value = 1;
        d.keywords.set(Keyword::Shield);
        d.ability_text = R"RB([Shield] (+1 [M] while I'm a defender.)
I don't deal combat damage unless there's a friendly gear here.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1111(CardRegistry& r) {
    r.registerCard(1111, std::make_unique<OriannaExquisite>());
}

}  // namespace riftbound
