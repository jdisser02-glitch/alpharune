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

class TheGoodStuff : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }

    // [Deploy] is engine-handled through deploys(): played only to a
    // battlefield (any battlefield), not recalled in cleanups, killed when an
    // opponent holds here.
    // "When you play this, discard 1, then draw 3."
    // "When you hold here, return this to its owner's hand."
    //
    // ENGINE-GAP: TriggerManager::onScore fires "When you conquer here" on the
    // scorer's unattached gear at the scored battlefield, but "When you hold
    // here" only on battlefield cards. The card registers WhenYouHoldHere (so
    // it works as printed once the engine dispatches it to gear) and, as a
    // fallback, AtStartOfMain: if its controller scored its battlefield
    // earlier this turn — before the Main Phase that can only be a hold — it
    // returns then. So the return happens at the start of the Main Phase
    // rather than in the Scoring step. (Once the gear is back in hand the
    // fallback cannot fire, so the two never both return it.)
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayThis, TriggerType::WhenYouHoldHere,
                TriggerType::AtStartOfMain};
    }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType trigger, GameObjectId /*subject*/) const override {
        if (trigger != TriggerType::AtStartOfMain) return true;
        return heldHereThisTurn(state, source, controller);
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::WhenYouPlayThis) {
            discardOneThenAct(ctx, "The Good Stuff: discard 1",
                              [](CardContext& c, GameObjectId /*discarded*/) {
                                  c.executor.drawCards(c.controller, 3);
                                  c.events.logTrace("THE GOOD STUFF: discarded 1, drew 3");
                              });
            return;
        }
        if (ctx.firing_trigger == TriggerType::AtStartOfMain &&
            !heldHereThisTurn(ctx.state, ctx.source, ctx.controller))
            return;
        if (!ctx.state.objectExists(ctx.source)) return;
        if (!ctx.state.getObject(ctx.source).location.has_value()) return;
        ctx.executor.bounceToHand(ctx.source);
        ctx.events.logTrace("THE GOOD STUFF: you held here -> returned to its owner's hand");
    }
private:
    static bool heldHereThisTurn(const GameState& state, GameObjectId source, PlayerId controller) {
        if (!state.objectExists(source)) return false;
        const auto here = state.getObject(source).battlefieldId();
        if (!here.has_value()) return false;
        return state.player(controller).battlefields_scored_this_turn.count(*here) > 0;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1059;
        d.def_id = R"RB(rad-106-167)RB";
        d.name = R"RB(The Good Stuff)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-106/167)RB";
        d.collector_number = 106;
        d.card_type = CardType::Gear;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
When you play this, discard 1, then draw 3.
When you hold here, return this to its owner's hand.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/e7dcc9ad059b471373f0093453a0ca7537d6aa0f-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1059(CardRegistry& r) {
    r.registerCard(1059, std::make_unique<TheGoodStuff>());
}

}  // namespace riftbound
