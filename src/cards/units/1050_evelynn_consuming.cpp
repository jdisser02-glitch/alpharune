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

class EvelynnConsuming : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // [Ambush] is the engine keyword.
    // "When you play a card from face down, ready me and give me +1 [M] this
    //  turn."
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayFromFacedown; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source) ||
            !ctx.state.getObject(ctx.source).location.has_value())
            return;
        ctx.executor.readyObject(ctx.source);
        ctx.executor.giveTemporaryMight(ctx.source, 1);
        ctx.events.logTrace("EVELYNN, CONSUMING: played from face down -> ready, +1 [M] this turn");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1050;
        d.def_id = R"RB(rad-090-167)RB";
        d.name = R"RB(Evelynn, Consuming)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-090/167)RB";
        d.collector_number = 90;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Evelynn)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.might = 5;
        d.rarity = Rarity::Epic;
        d.keywords.set(Keyword::Ambush);
        d.ability_text = R"RB([Ambush] (You may play me as a [Reaction] to a battlefield where you have units.)
When you play a card from face down, ready me and give me +1 [M] this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/26b83de26622fb5163d2fe321ff29e2bf16ac027-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1050(CardRegistry& r) {
    r.registerCard(1050, std::make_unique<EvelynnConsuming>());
}

}  // namespace riftbound
