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

class RecklessInnovator : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // [Accelerate] is the keyword (engine-paid [1][P] at play).
    // "When I become exhausted, choose one — Play a Gold gear token exhausted.
    //  / Pay [P] to draw 1." The draw mode is offered only when [P] can be paid;
    //  the [P] is paid as the ability is finalized.
    TriggerType triggerType() const override { return TriggerType::WhenIBecomeExhausted; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        uint32_t legal = 1u;
        if (costAlreadyPaid(ctx) || payableOnePower(ctx.state, ctx.controller, Domain::Chaos))
            legal |= 2u;
        int mode = pickMode(ctx, "Reckless Innovator", 2,
                            {"Play a Gold gear token exhausted", "Pay [P] to draw 1"}, legal);
        if (mode == 1 && costDueNow(ctx) && !payOnePower(ctx, ctx.controller, Domain::Chaos))
            return;                           // can't pay: no draw (the cost stays owed)
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (mode == 0) {
            createGoldExhausted(ctx);
            ctx.events.logTrace("RECKLESS INNOVATOR: Gold token (exhausted)");
        } else if (mode == 1) {
            ctx.executor.drawCards(ctx.controller, 1);
            ctx.events.logTrace("RECKLESS INNOVATOR: paid [P] -> draw 1");
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 965;
        d.def_id = R"RB(rad-105-167)RB";
        d.name = R"RB(Reckless Innovator)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-105/167)RB";
        d.collector_number = 105;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.energy_cost = 3;
        d.might = 3;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Accelerate);
        d.ability_text = R"RB([Accelerate] (You may pay [1][P] as an additional cost to have me enter ready.)
When I become exhausted, choose one —
Play a Gold gear token exhausted. (It has "[Reaction][>] Kill this, [E]: [Add] [A].")Pay [P] to draw 1.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b7ff5684309bcd346e6b3534e0d1f38ec1a38726-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_965(CardRegistry& r) {
    r.registerCard(965, std::make_unique<RecklessInnovator>());
}

}  // namespace riftbound
