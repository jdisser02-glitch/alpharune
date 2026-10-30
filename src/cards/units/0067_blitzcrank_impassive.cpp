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

class BlitzcrankImpassive : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayMe, TriggerType::WhenIHold};
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;

        // WhenIHold (delayed-fire path leaves firing_trigger=None, so treat
        // anything that isn't WhenYouPlayMe as the hold branch only when the
        // unit is actually holding a battlefield — but here both triggers are
        // event-driven and carry firing_trigger, so branch on it directly).
        if (ctx.firing_trigger == TriggerType::WhenIHold) {
            if (finalizingChoices(ctx)) return;   // no choices
            ctx.executor.bounceToHand(ctx.source);
            ctx.events.logTrace("BLITZCRANK: returned to owner's hand on hold");
            return;
        }

        // "When you play me to a battlefield, you may move an enemy unit to
        //  here." The player chooses which enemy unit (one at another location
        //  that an enemy effect may choose and move), at finalization.
        const auto my_bf = ctx.state.getObject(ctx.source).battlefieldId();
        if (!my_bf) return;                   // not at a battlefield: nothing to do
        const BattlefieldId here = *my_bf;
        auto pullable = [&]() {
            auto out = chooseableUnits(ctx.state, ctx.controller, [&](const GameObject& o) {
                if (o.controller == ctx.controller || o.cant_be_moved_by_enemy) return false;
                const auto obf = o.battlefieldId();
                return !(obf && *obf == here);
            });
            std::sort(out.begin(), out.end());
            return out;
        };
        int conf = confirmOptional(ctx, "Blitzcrank: move an enemy unit to my battlefield?",
                                   [&]() { return !pullable().empty(); });
        GameObjectId target = kInvalidId;
        if (conf == 1)
            target = pickTarget(ctx, "Blitzcrank: move which enemy unit here", pullable());
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (conf != 1) return;                // waiting for the agent, or declined
        if (target == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the target
        if (target == kInvalidId || !ctx.state.objectExists(target)) return;
        std::string name = ctx.state.getObject(target).name;
        ctx.executor.moveToBattlefield(target, here);
        ctx.events.logTrace("BLITZCRANK: pulled " + name + " to BF#" +
                             std::to_string(static_cast<int>(here)));
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 67;
        d.def_id = R"RB(ogn-067-298)RB";
        d.name = R"RB(Blitzcrank, Impassive)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-067/298)RB";
        d.collector_number = 67;
        d.artist = R"RB(League Splash Team)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Mech)RB", R"RB(Blitzcrank)RB", R"RB(Zaun)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.might = 5;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Tank);
        d.ability_text = R"RB([Tank] (I must be assigned combat damage first.)
When you play me to a battlefield, you may move an enemy unit to here.
When I hold, return me to my owner's hand.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/654dcc4aef0a0b5a0c6e928d7aae397a52c3ab17-744x1039.png?accountingTag=RB)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_67(CardRegistry& r) {
    r.registerCard(67, std::make_unique<BlitzcrankImpassive>());
}

} // namespace riftbound
