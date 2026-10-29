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

class AkaliBrash : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "The first time I move each turn, [Add] [1][A].
    //  The second time I move each turn, banish the top card of your Main
    //  Deck. Until your next Ending Phase, you may play that card."
    // Moves are counted here, per turn, as the WhenIMove abilities resolve
    // (GameObject::moves_this_turn misses moves made by effects). The play
    // permission is the legend-granted ability of radiance_helpers.h (ability
    // index 0 below); AtStartOfMain is its delayed re-grant.
    TriggerType triggerType() const override { return TriggerType::WhenIMove; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::AtStartOfMain) {
            refreshBanishPlay(ctx, kKey, def_.id, kPlayBanished);
            return;
        }
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& me = ctx.state.getObject(ctx.source);
        const int turn = ctx.state.turn.turn_number + 1;
        if (me.card_counters["__akali_brash_move_turn"] != turn) {
            me.card_counters["__akali_brash_move_turn"] = turn;
            me.card_counters["__akali_brash_moves"] = 0;
        }
        const int nth = ++me.card_counters["__akali_brash_moves"];
        if (nth == 1) {
            ctx.executor.addFloatingEnergy(ctx.controller, 1);
            ctx.executor.addFloatingUniversalPower(ctx.controller, 1);
            ctx.events.logTrace("AKALI, BRASH: first move this turn -> [Add] [1][A]");
        } else if (nth == 2) {
            ctx.events.logTrace("AKALI, BRASH: second move this turn -> banish the top card");
            banishTopToPlayLater(ctx, kKey, def_.id, kPlayBanished);
        }
    }

    // The play-from-banishment permission, granted to the legend only.
    std::vector<ActivatedAbility> activatedAbilities() const override { return {ActivatedAbility{}}; }
    bool canActivateAbilityAt(const GameState&, PlayerId, GameObjectId, int) const override {
        return false;
    }
    void onActivate(CardContext& ctx, int ability_index,
                    const std::vector<GameObjectId>& /*targets*/) override {
        if (ability_index == kPlayBanished) playFromBanishment(*this, ctx, kKey, def_.id, kPlayBanished);
    }
private:
    static constexpr int kPlayBanished = 0;
    static constexpr const char* kKey = "__akali_brash_play_until";
    const CardDef def_ = [] {
        CardDef d;
        d.id = 994;
        d.def_id = R"RB(rad-015-167)RB";
        d.name = R"RB(Akali, Brash)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-015/167)RB";
        d.collector_number = 15;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Fury};
        d.tags = {R"RB(Ionia)RB", R"RB(Akali)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.might = 5;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(The first time I move each turn, [Add] [1][A].
The second time I move each turn, banish the top card of your Main Deck. Until your next Ending Phase, you may play that card. (You still pay its costs.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/20997ddaef2cd714c36e5473c156faf331b80b7b-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_994(CardRegistry& r) {
    r.registerCard(994, std::make_unique<AkaliBrash>());
}

}  // namespace riftbound
