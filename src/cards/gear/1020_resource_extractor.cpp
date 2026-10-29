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

class ResourceExtractor : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }

    // "At the end of your turn, if you control this battlefield, play a Gold
    //  gear token exhausted." "This battlefield" is the one this gear is at
    //  ([Deploy] keeps it at one). An intervening "if": checked when the turn
    //  ends (triggerCondition) and again as the ability resolves.
    TriggerType triggerType() const override { return TriggerType::AtEndOfTurn; }

    static bool controlsMyBattlefield(const GameState& state, GameObjectId self, PlayerId p) {
        if (!state.objectExists(self)) return false;
        const auto bf = state.getObject(self).battlefieldId();
        if (!bf.has_value()) return false;
        for (const auto& b : state.battlefields)
            if (b.id == *bf) return b.controller == std::optional<PlayerId>{p};
        return false;
    }

    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType /*trigger*/, GameObjectId /*subject*/) const override {
        return controlsMyBattlefield(state, source, controller);
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!controlsMyBattlefield(ctx.state, ctx.source, ctx.controller)) {
            ctx.events.logTrace("RESOURCE EXTRACTOR: no longer controls this battlefield");
            return;
        }
        createGoldExhausted(ctx);
        ctx.events.logTrace("RESOURCE EXTRACTOR: controls this battlefield -> Gold token (exhausted)");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1020;
        d.def_id = R"RB(rad-052-167)RB";
        d.name = R"RB(Resource Extractor)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-052/167)RB";
        d.collector_number = 52;
        d.artist = R"RB(Fish Art Studio)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 1;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
At the end of your turn, if you control this battlefield, play a Gold gear token exhausted. (It has "[Reaction][>] Kill this, [E]: [Add] [A]."))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/d5cc2f85febe2260e51b88bddfeab6ce700c85dd-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1020(CardRegistry& r) {
    r.registerCard(1020, std::make_unique<ResourceExtractor>());
}

}  // namespace riftbound
