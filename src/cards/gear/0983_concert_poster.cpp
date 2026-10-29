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

class ConcertPoster : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play this or when you conquer here, you may pay [1] to play a
    //  1 [M] Recruit unit token to your base."
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayThis, TriggerType::WhenYouConquerHere};
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto can_pay = [&]() { return payableEnergy(ctx.state, ctx.controller) >= 1; };
        if (!costAlreadyPaid(ctx) && !can_pay()) { if (finalizingChoices(ctx)) return; return; }
        int go = confirmOptional(ctx, "Concert Poster: pay [1] for a Recruit?", can_pay);
        if (go == 1 && costDueNow(ctx) && !payEnergy(ctx, ctx.controller, 1)) return;
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (go != 1) return;
        ctx.executor.createToken(ctx.controller, CardType::Unit, "Recruit", 1, {"Recruit"},
                                 KeywordSet{}, LocationId{BaseLocation{ctx.controller}}, false);
        ctx.events.logTrace("CONCERT POSTER: paid [1] -> 1 [M] Recruit to base");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 983;
        d.def_id = R"RB(rad-116-167)RB";
        d.name = R"RB(Concert Poster)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-116/167)RB";
        d.collector_number = 116;
        d.card_type = CardType::Gear;
        d.domains = {Domain::Order};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 1;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
When you play this or when you conquer here, you may pay [1] to play a 1 [M] Recruit unit token to your base.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/0e80322dac6065e3ee726f5f9a2650a88195a44c-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_983(CardRegistry& r) {
    r.registerCard(983, std::make_unique<ConcertPoster>());
}

}  // namespace riftbound
