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

class SanctumConservator : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play a card with Energy cost [7] or more, ready me."
    // WhenYouPlayACard carries the played card as triggering_subject (a
    // permanent as it is played, a spell as it resolves). Its printed Energy
    // cost is read from the CardDB as the ability resolves (triggerCondition
    // has no CardDB to ask, so a cheaper card still puts a no-op ability on
    // the chain).
    static constexpr int kMinEnergy = 7;
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayACard; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        if (!ctx.state.getObject(ctx.source).location.has_value()) return;   // left the board
        const GameObjectId played = ctx.state.chain.resuming.has_value()
            ? ctx.state.chain.resuming->triggering_subject : kInvalidId;
        if (played == kInvalidId || !ctx.state.objectExists(played)) return;
        const CardDefId def_id = ctx.state.getObject(played).card_def_id;
        if (def_id == kInvalidId) return;   // a token without a card definition
        if (ctx.executor.cardDB().get(def_id).energy_cost < kMinEnergy) return;
        ctx.executor.readyObject(ctx.source);
        ctx.events.logTrace("SANCTUM CONSERVATOR: played a [7]+ card -> ready me");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1007;
        d.def_id = R"RB(rad-030-167)RB";
        d.name = R"RB(Sanctum Conservator)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-030/167)RB";
        d.collector_number = 30;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Calm};
        d.energy_cost = 5;
        d.might = 5;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When you play a card with Energy cost [7] or more, ready me.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/776973101613a15a7d0a2fc90587d19098ecc894-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1007(CardRegistry& r) {
    r.registerCard(1007, std::make_unique<SanctumConservator>());
}

}  // namespace riftbound
