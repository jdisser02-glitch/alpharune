#include "cards/card.h"
#include "cards/card_helpers.h"
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

class ReaverSRow : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "When you defend here, you may move a friendly unit here to base."
    // Whether to, and which unit, are decided as the trigger is finalized
    // (CR 383.3); a triggered ability's chain item carries no chosen targets.
    static std::vector<GameObjectId> unitsHere(const CardContext& ctx) {
        const auto here = hereOf(ctx.state, ctx.source);
        if (!here.has_value()) return {};
        return chooseableUnitsSorted(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller == ctx.controller && o.battlefieldId() == here;
        });
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const int go = confirmOptional(ctx, "Reaver's Row: move a friendly unit here to base?",
                                       [&]() { return !unitsHere(ctx).empty(); });
        if (go < 1) return;   // waiting on the agent, declined or no unit here
        const GameObjectId unit = pickTarget(ctx, "Reaver's Row: move which unit to base", unitsHere(ctx));
        if (waitingForTarget(ctx, unit)) return;
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        ctx.executor.moveToBase(unit);
    }
    TriggerType triggerType() const override { return TriggerType::WhenYouDefendHere; }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 280;
        d.def_id = R"RB(ogn-285-298)RB";
        d.name = R"RB(Reaver's Row)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-285/298)RB";
        d.collector_number = 285;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you defend here, you may move a friendly unit here to base.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/7d99fe84c9b815463198b5dbad694759940cac12-1038x744.png)RB";
        d.banned = true;  // tournament ban (first ban list; was missing here)
        return d;
    }();
};

}  // anonymous namespace

void register_card_280(CardRegistry& r) {
    r.registerCard(280, std::make_unique<ReaverSRow>());
}

} // namespace riftbound
