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

class RunebladeAnvil : public GearCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play this or when this leaves the board, banish the top card
    //  of your Main Deck. Until your next Ending Phase, you may play that card.
    //  (You still pay its costs.)"
    // "Leaves the board" is the WhenIDie trigger: a gear's death triggers when
    // it is killed through killWithDeathTrigger (see radiance_helpers.h) — as
    // this card's own "Kill this" does — and deathAbilityOnLeavingBoard()
    // fires it on a bounce, banish or recycle too.
    // The play permission is the legend-granted ability of radiance_helpers.h
    // (ability index kPlayBanished below); AtStartOfMain is its delayed
    // re-grant, never a trigger of the Anvil itself.
    bool deathAbilityOnLeavingBoard() const override { return true; }
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayThis, TriggerType::WhenIDie};
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::AtStartOfMain) {
            refreshBanishPlay(ctx, kKey, def_.id, kPlayBanished);
            return;
        }
        banishTopToPlayLater(ctx, kKey, def_.id, kPlayBanished);
    }

    // 0: "[2][P], [E]: Kill this."
    // 1: the play-from-banishment permission, granted to the legend only.
    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility kill;
        kill.cost.exhaust = true;
        kill.cost.energy = 2;
        kill.cost.power = 1;
        kill.cost.power_domain = Domain::Chaos;
        ActivatedAbility play;   // no cost: the card's own costs are paid as it is played
        return {kill, play};
    }
    bool canActivateAbilityAt(const GameState& state, PlayerId controller, GameObjectId source,
                              int ability_index) const override {
        if (ability_index != kKillThis) return false;   // the granted one isn't the Anvil's own
        return Card::canActivateAbilityAt(state, controller, source, ability_index);
    }
    void onActivate(CardContext& ctx, int ability_index,
                    const std::vector<GameObjectId>& /*targets*/) override {
        if (ability_index == kPlayBanished) {
            playFromBanishment(*this, ctx, kKey, def_.id, kPlayBanished);
            return;
        }
        if (!ctx.state.objectExists(ctx.source)) return;
        ctx.events.logTrace("RUNEBLADE ANVIL: [2][P], [E] -> kill this");
        killWithDeathTrigger(ctx, ctx.source);
    }
private:
    static constexpr int kKillThis = 0;
    static constexpr int kPlayBanished = 1;
    static constexpr const char* kKey = "__runeblade_anvil_play_until";
    const CardDef def_ = [] {
        CardDef d;
        d.id = 992;
        d.def_id = R"RB(rad-012-167)RB";
        d.name = R"RB(Runeblade Anvil)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-012/167)RB";
        d.collector_number = 12;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Chaos};
        d.energy_cost = 1;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you play this or when this leaves the board, banish the top card of your Main Deck. Until your next Ending Phase, you may play that card. (You still pay its costs.)
[2][P], [E]: Kill this.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/11e2ddea70ec4dd8e562681a1ab1dfbc2b2dca0f-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_992(CardRegistry& r) {
    r.registerCard(992, std::make_unique<RunebladeAnvil>());
}

}  // namespace riftbound
