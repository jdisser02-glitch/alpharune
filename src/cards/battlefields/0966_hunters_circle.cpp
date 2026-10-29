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

class HuntersCircle : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }
    // "For each player, the first unit or gear they play here on their turn
    //  costs [1] less." BattlefieldState::last_play_here_turn records the turn
    // each player last played a unit or gear here.
    int playCostReductionHere(const GameState& state, const BattlefieldState& here,
                              PlayerId player, const GameObject& played) const override {
        if (!played.isUnit() && !played.isGear()) return 0;
        if (state.turn.turn_player != player) return 0;
        const int p = player == PlayerId::Player1 ? 0 : 1;
        return here.last_play_here_turn[p] == state.turn.turn_number ? 0 : 1;
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 966;
        d.def_id = R"RB(rad-163-167)RB";
        d.name = R"RB(Hunters' Circle)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-163/167)RB";
        d.collector_number = 163;
        d.artist = R"RB(Fish Art Studio)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(For each player, the first unit or gear they play here on their turn costs [1] less.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/e7e25d90506d805f1996e7f751cb5b6baa8ec305-1039x744.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_966(CardRegistry& r) {
    r.registerCard(966, std::make_unique<HuntersCircle>());
}

}  // namespace riftbound
