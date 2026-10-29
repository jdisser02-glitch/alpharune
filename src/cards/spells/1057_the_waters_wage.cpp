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

class TheWatersWage : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // [Hidden] is engine-handled (keyword).
    // "Play a Gold gear token exhausted. If it's your turn and you played this
    //  from face down, play three ready Gold gear tokens instead."
    // A play from face down is stamped card_counters["__played_from_facedown"]
    // = 1 by the engine; it is consumed here so a later play of this card from
    // hand does not see a stale marker. "Your turn" is checked as it resolves.
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        bool from_facedown = false;
        if (ctx.state.objectExists(ctx.source)) {
            auto& flag = ctx.state.getObject(ctx.source).card_counters["__played_from_facedown"];
            from_facedown = flag == 1;
            flag = 0;
        }
        if (from_facedown && ctx.state.turn.turn_player == ctx.controller) {
            for (int i = 0; i < 3; ++i) {
                auto tok = ctx.executor.createToken(ctx.controller, CardType::Gear, "Gold", 0,
                                                    {"Gold"}, KeywordSet{},
                                                    LocationId{BaseLocation{ctx.controller}},
                                                    /*enter_ready=*/true);
                if (ctx.state.objectExists(tok)) {
                    auto& gold = ctx.state.getObject(tok);
                    gold.card_def_id = kGoldGearCardDefId;
                    gold.is_exhausted = false;
                }
            }
            ctx.events.logTrace("THE WATERS' WAGE: played from face down on your turn -> 3 ready Gold");
            return;
        }
        createGoldExhausted(ctx);
        ctx.events.logTrace("THE WATERS' WAGE: 1 exhausted Gold");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1057;
        d.def_id = R"RB(rad-100-167)RB";
        d.name = R"RB(The Waters' Wage)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-100/167)RB";
        d.collector_number = 100;
        d.artist = R"RB(Polar Engine Studio)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Chaos};
        d.energy_cost = 1;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Hidden);
        d.ability_text = R"RB([Hidden] (Hide now for [A] to react with later for [0].)
Play a Gold gear token exhausted. If it's your turn and you played this from face down, play three ready Gold gear tokens instead. (Gold tokens have "[Reaction][>] Kill this, [E]: [Add] [A]."))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/0552932562db1dd48f14c74779999a645d62cc0d-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1057(CardRegistry& r) {
    r.registerCard(1057, std::make_unique<TheWatersWage>());
}

}  // namespace riftbound
