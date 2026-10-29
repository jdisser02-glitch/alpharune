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

class MeticulousRestorationist : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play a card with Energy cost [7] or more, you may play me from
    //  your trash for [Y]."
    // A trash ability on WhenYouPlayACard (the played card is the chain item's
    // triggering_subject; its printed Energy cost is read from the CardDB).
    // The "you may" and the [Y] are decided / paid as the ability is
    // finalized; at resolution, if I'm still in the trash, I'm played from it
    // (the [Y] replaces my cost) to my owner's base.
    //
    // ENGINE GAP: TriggerManager dispatches WhenYouPlayACard only to cards on
    // the board and in the Legend Zone, never to cards in a trash, so this
    // ability does not trigger in a real game until the engine's
    // WhenYouPlayACard sweeps (onCardPlayed + onSpellResolved) also include
    // the player's trash for cards that fire on it (as onUnitDied already does
    // for WhenYouKillAUnitWithASpell). The behaviour below is complete once it
    // is dispatched (tests queue it through EffectExecutor::queueTriggeredAbility).
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayACard; }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType /*trigger*/, GameObjectId subject) const override {
        if (!state.objectExists(source)) return false;
        const auto& self = state.getObject(source);
        return self.zone == ZoneType::Trash && self.owner == controller &&
               subject != kInvalidId && subject != source;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!qualifies(ctx)) return;   // a cheaper card (no choice was made)
        auto in_trash = [&]() {
            return ctx.state.objectExists(ctx.source) &&
                   ctx.state.getObject(ctx.source).zone == ZoneType::Trash;
        };
        int go = confirmOptional(ctx, "Meticulous Restorationist: play me from your trash for [Y]?",
            [&]() {
                return in_trash() &&
                       (costAlreadyPaid(ctx) ||
                        payableOnePower(ctx.state, ctx.controller, Domain::Order));
            });
        if (go == 1 && costDueNow(ctx) && !payOnePower(ctx, ctx.controller, Domain::Order))
            return;                           // can't pay: not played
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (go != 1 || !in_trash()) return;
        auto& trash = ctx.state.player(ctx.state.getObject(ctx.source).owner).trash;
        auto it = std::find(trash.begin(), trash.end(), ctx.source);
        if (it != trash.end()) trash.erase(it);
        ctx.executor.playIgnoringCost(ctx.controller, ctx.source, std::nullopt);
        ctx.events.logTrace("METICULOUS RESTORATIONIST: paid [Y] -> played from trash");
    }
private:
    static constexpr int kMinEnergy = 7;
    static bool qualifies(CardContext& ctx) {
        const GameObjectId played = ctx.state.chain.resuming.has_value()
            ? ctx.state.chain.resuming->triggering_subject : kInvalidId;
        if (played == kInvalidId || !ctx.state.objectExists(played)) return false;
        const CardDefId def_id = ctx.state.getObject(played).card_def_id;
        if (def_id == kInvalidId) return false;   // a token without a card definition
        return ctx.executor.cardDB().get(def_id).energy_cost >= kMinEnergy;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1069;
        d.def_id = R"RB(rad-120-167)RB";
        d.name = R"RB(Meticulous Restorationist)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-120/167)RB";
        d.collector_number = 120;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Order};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 2;
        d.might = 2;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When you play a card with Energy cost [7] or more, you may play me from your trash for [Y].)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/ee50fbf5e902e7109a3d54b417edc440b5a37198-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1069(CardRegistry& r) {
    r.registerCard(1069, std::make_unique<MeticulousRestorationist>());
}

}  // namespace riftbound
