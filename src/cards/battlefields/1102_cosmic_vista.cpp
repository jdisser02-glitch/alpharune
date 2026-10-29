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

class CosmicVista : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you hold here, if your Rune Deck is empty, you score 1 point."
    // An intervening "if": checked as the hold happens and again as the
    // ability resolves.
    TriggerType triggerType() const override { return TriggerType::WhenYouHoldHere; }

    bool triggerCondition(const GameState& state, GameObjectId /*source*/, PlayerId controller,
                          TriggerType /*trigger*/, GameObjectId /*subject*/) const override {
        return state.player(controller).rune_deck.empty();
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (finalizingChoices(ctx)) return;   // no choices
        if (!ctx.state.player(ctx.controller).rune_deck.empty()) {
            ctx.events.logTrace("COSMIC VISTA: rune deck no longer empty");
            return;
        }
        ctx.executor.scorePoints(ctx.controller, 1);
        ctx.events.logTrace("COSMIC VISTA: held here with an empty rune deck -> +1 point");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1102;
        d.def_id = R"RB(rad-160-167)RB";
        d.name = R"RB(Cosmic Vista)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-160/167)RB";
        d.collector_number = 160;
        d.artist = R"RB(Fish Art Studio)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you hold here, if your Rune Deck is empty, you score 1 point.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/0efb6db0a41350e9cfc116b235eeb92bb05ed5bf-1039x744.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1102(CardRegistry& r) {
    r.registerCard(1102, std::make_unique<CosmicVista>());
}

}  // namespace riftbound
