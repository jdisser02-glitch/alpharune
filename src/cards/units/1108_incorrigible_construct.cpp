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
// text. Text quoted consistently by two sources. Power cost not shown by any
// source (assumed none, unverified). No image_url / artist.
class IncorrigibleConstruct : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "[Show Off] a Mech. (As you play me, you may reveal a Mech from hand or
    //  pick another friendly Mech.) I enter ready if you showed off."
    // [Show Off] (CR 832) costs only information, which the engine does not
    // model, so I show off whenever I can (see canShowOff in card_helpers.h):
    // a Mech card in my controller's hand or another friendly Mech on the
    // board. Checked with me already at my destination, as the engine asks.
    using UnitCard::entersReadyOnPlay;
    bool entersReadyOnPlay(const GameState& state, PlayerId controller,
                           const GameObject& self) const override {
        for (auto cid : state.player(controller).hand)
            if (cid != self.id && state.objectExists(cid) && hasTag(state.getObject(cid), "Mech"))
                return true;
        for (const auto& [id, obj] : state.objects)
            if (id != self.id && obj.controller == controller && obj.location.has_value() &&
                hasTag(obj, "Mech"))
                return true;
        return false;
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1108;
        d.def_id = R"RB(rad-011-167)RB";
        d.name = R"RB(Incorrigible Construct)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-011/167)RB";
        d.collector_number = 11;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Fury};
        d.tags = {R"RB(Piltover)RB", R"RB(Mech)RB"};
        d.energy_cost = 4;
        d.might = 4;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB([Show Off] a Mech. (As you play me, you may reveal a Mech from hand or pick another friendly Mech.)
I enter ready if you showed off.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1108(CardRegistry& r) {
    r.registerCard(1108, std::make_unique<IncorrigibleConstruct>());
}

}  // namespace riftbound
