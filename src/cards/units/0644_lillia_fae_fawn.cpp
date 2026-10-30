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

class LilliaFaeFawn : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    // "When I move from a location, play a 3 [M] Sprite unit token with
    // [Temporary] there." "There" is the location I moved FROM, which the
    // trigger manager leaves in `__moved_from_bf` (a battlefield id, or -1
    // for a base). The token is not "ready" (unlike the Bashful Bloom legend's
    // Sprite), so it enters exhausted (CR 143.4). Recorded games agree: after
    // "moved Lillia to <battlefield>" the client logs "played Sprite token to
    // base" and "exhausted Sprite".
    TriggerType triggerType() const override { return TriggerType::WhenIMove; }
    void onTrigger(CardContext& ctx,
                   const std::vector<GameObjectId>& /*targets*/) override {
        LocationId loc = BaseLocation{ctx.controller};
        if (ctx.state.objectExists(ctx.source)) {
            const auto& self = ctx.state.getObject(ctx.source);
            auto it = self.card_counters.find("__moved_from_bf");
            if (it != self.card_counters.end() && it->second >= 0) {
                const auto from_bf = static_cast<BattlefieldId>(it->second);
                for (const auto& bf : ctx.state.battlefields)
                    if (bf.id == from_bf) { loc = BattlefieldLocation{from_bf}; break; }
            }
        }
        KeywordSet kw; kw.set(Keyword::Temporary);
        ctx.executor.createToken(ctx.controller, CardType::Unit, "Sprite",
                                 /*might=*/3, /*tags=*/{"Fae"}, kw, loc,
                                 /*enter_ready=*/false);
        ctx.events.logTrace("LILLIA: plays a 3M Sprite token (Temporary) where "
                            "she moved from");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 644;
        d.def_id = R"RB(unl-082-219)RB";
        d.name = R"RB(Lillia, Fae Fawn)RB";
        d.set_code = R"RB(UNL)RB";
        d.set_name = R"RB(Unleashed)RB";
        d.public_code = R"RB(UNL-082/219)RB";
        d.collector_number = 82;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Fae)RB", R"RB(Lillia)RB", R"RB(Ionia)RB"};
        d.energy_cost = 3;
        d.might = 3;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Accelerate);
        d.keywords.set(Keyword::Temporary);
        d.ability_text = R"RB([Accelerate] (You may pay [1][B] as an additional cost to have me enter ready.)
When I move from a location, play a 3 [M] Sprite unit token with [Temporary] there. (Kill it at the start of its controller's Beginning Phase, before scoring.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/65d0af5361b335a7e65c7aa03a099fc0de3431e6-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_644(CardRegistry& r) {
    r.registerCard(644, std::make_unique<LilliaFaeFawn>());
}

} // namespace riftbound
