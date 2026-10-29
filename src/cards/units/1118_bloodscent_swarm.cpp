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

// RECONSTRUCTED TEXT: this card is not in cards/card_index.json. Its data and
// ability text were reconstructed from web-search summaries (confidence:
// high). Notes: gathered from web-search summaries of retailer / wiki pages
// (WebFetch to card sites was blocked); ability_text written in the repo's
// symbol convention. No image_url / artist known.
class BloodscentSwarm : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "I cost [2] less for each battlefield you've conquered this turn."
    // A battlefield counts when the engine's last-conquered stamp
    // (BattlefieldState::conquered_on_turn / conquered_by_player, set in
    // scoreConquer) says `player` conquered it this turn. Energy only; the
    // engine clamps the cost at 0.
    int selfCostReduction(const GameState& state, PlayerId player) const override {
        return 2 * conqueredThisTurn(state, player);
    }
    static int conqueredThisTurn(const GameState& state, PlayerId player) {
        int n = 0;
        for (const auto& bf : state.battlefields)
            if (bf.conquered_on_turn == state.turn.turn_number && bf.conquered_by_player == player) ++n;
        return n;
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1118;
        d.def_id = R"RB(rad-080-167)RB";
        d.name = R"RB(Bloodscent Swarm)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-080/167)RB";
        d.collector_number = 80;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Shurima)RB"};
        d.energy_cost = 8;
        d.power_cost = 2;
        d.might = 8;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(I cost [2] less for each battlefield you've conquered this turn.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1118(CardRegistry& r) {
    r.registerCard(1118, std::make_unique<BloodscentSwarm>());
}

}  // namespace riftbound
