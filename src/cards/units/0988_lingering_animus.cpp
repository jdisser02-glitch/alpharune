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

class LingeringAnimus : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "I enter ready if a friendly unit or gear died this turn."
    // APPROXIMATION (no per-player death tracking in the engine):
    //  - a friendly gear counts when it was killed this turn through
    //    killWithDeathTrigger (radiance_helpers.h stamps "__died_turn");
    //  - a friendly unit counts when TurnState::any_unit_died_this_turn is set
    //    (some unit, of either player, died this turn) and one of my units
    //    has died at some point (it carries a last-known snapshot and is in a
    //    trash / banishment). So an enemy death this turn plus a friendly death
    //    on an earlier turn also enables it.
    bool entersReadyOnPlay(const GameState& state, PlayerId controller) const override {
        for (const auto& [id, obj] : state.objects) {
            if (obj.controller != controller || obj.location.has_value()) continue;
            if ((obj.isGear() || obj.isUnit()) && stampedDiedThisTurn(state, obj)) return true;
        }
        if (!state.turn.any_unit_died_this_turn) return false;
        for (const auto& [id, obj] : state.objects) {
            if (!obj.isUnit() || obj.controller != controller || obj.location.has_value()) continue;
            if (!obj.last_known.valid) continue;
            if (obj.zone == ZoneType::Trash || obj.zone == ZoneType::Banishment) return true;
        }
        return false;
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 988;
        d.def_id = R"RB(rad-005-167)RB";
        d.name = R"RB(Lingering Animus)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-005/167)RB";
        d.collector_number = 5;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Fury};
        d.energy_cost = 2;
        d.might = 2;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(I enter ready if a friendly unit or gear died this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/5453008d27b3bb4f00941cb5a0d9ca52e4eb0245-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_988(CardRegistry& r) {
    r.registerCard(988, std::make_unique<LingeringAnimus>());
}

}  // namespace riftbound
