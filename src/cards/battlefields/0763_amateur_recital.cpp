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

class AmateurRecital : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "When you hold here, you may move a unit at a battlefield to its base."
    // Whether to, and which unit (either player's, at any battlefield), are
    // decided as the trigger is finalized (CR 383.3). A triggered ability's
    // chain item carries no chosen targets, so the previous version, which
    // read the unit from `targets`, never moved anything.
    static std::vector<GameObjectId> candidates(const CardContext& ctx) {
        return chooseableUnitsSorted(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.isAtBattlefield() &&
                   !(o.controller != ctx.controller && o.cant_be_moved_by_enemy);
        });
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const int go = confirmOptional(ctx, "Amateur Recital: move a unit at a battlefield to its base?",
                                       [&]() { return !candidates(ctx).empty(); });
        if (go < 1) return;   // waiting on the agent, declined or nothing to move
        const GameObjectId t = pickTarget(ctx, "Amateur Recital: move which unit to its base",
                                          candidates(ctx));
        if (waitingForTarget(ctx, t)) return;
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;
        ctx.executor.moveToBase(t);
        ctx.events.logTrace("AMATEUR RECITAL: moved " + ctx.state.getObject(t).name + " to its base");
    }
    TriggerType triggerType() const override { return TriggerType::WhenYouHoldHere; }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 763;
        d.def_id = R"RB(unl-207-219)RB";
        d.name = R"RB(Amateur Recital)RB";
        d.set_code = R"RB(UNL)RB";
        d.set_name = R"RB(Unleashed)RB";
        d.public_code = R"RB(UNL-207/219)RB";
        d.collector_number = 207;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you hold here, you may move a unit at a battlefield to its base.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/9372a1574aa077e8b02cc6f00a1add060ae3b520-1039x744.png?accountingTag=RB)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_763(CardRegistry& r) {
    r.registerCard(763, std::make_unique<AmateurRecital>());
}

} // namespace riftbound
