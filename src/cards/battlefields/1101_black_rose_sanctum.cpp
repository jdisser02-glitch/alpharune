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

class BlackRoseSanctum : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }

    // "For each player, the first time each turn a non-token unit they control
    //  dies here, they play a 1 [M] Recruit unit token to their base."
    // ENGINE GAP: TriggerManager::onUnitDied fires "a friendly unit dies" on
    // the dying unit's controller's on-board cards only — never on the card of
    // the battlefield it died at — so this does not fire today. Declared so it
    // works once a death at a battlefield is also announced to that
    // battlefield's card for the dying unit's controller, with the unit as the
    // subject (fireTrigger(bf.card_object_id, e.controller, 0,
    // WhenAFriendlyUnitDies, e.object)).
    TriggerType triggerType() const override { return TriggerType::WhenAFriendlyUnitDies; }

    static std::string usedKey(PlayerId p) {
        return p == PlayerId::Player1 ? "__brs_used_p1" : "__brs_used_p2";
    }

    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType /*trigger*/, GameObjectId subject) const override {
        if (!state.objectExists(source)) return false;
        const auto here = battlefieldOfCard(state, source);
        if (!here.has_value()) return false;
        if (subject != kInvalidId) {
            if (!state.objectExists(subject)) return false;
            const auto& dead = state.getObject(subject);
            if (dead.isToken()) return false;
            if (dead.last_location.has_value() &&
                *dead.last_location != LocationId{BattlefieldLocation{*here}}) return false;
        }
        // The first time each turn, for this player.
        const auto& self = state.getObject(source);
        auto it = self.card_counters.find(usedKey(controller));
        if (it != self.card_counters.end() && it->second == state.turn.turn_number) return false;
        for (const auto& ci : state.chain.items)
            if (ci.source == source && ci.controller == controller && ci.is_ability) return false;
        return true;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (finalizingChoices(ctx)) return;   // no choices
        if (ctx.state.objectExists(ctx.source))
            ctx.state.getObject(ctx.source).card_counters[usedKey(ctx.controller)] =
                ctx.state.turn.turn_number;
        ctx.executor.createToken(ctx.controller, CardType::Unit, "Recruit", 1, {"Recruit"},
                                 KeywordSet{}, LocationId{BaseLocation{ctx.controller}}, false);
        ctx.events.logTrace("BLACK ROSE SANCTUM: a unit died here -> 1 [M] Recruit to base");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1101;
        d.def_id = R"RB(rad-159-167)RB";
        d.name = R"RB(Black Rose Sanctum)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-159/167)RB";
        d.collector_number = 159;
        d.artist = R"RB(Grafit Studio)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(For each player, the first time each turn a non-token unit they control dies here, they play a 1 [M] Recruit unit token to their base.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/03ce18a231c7c919875b436b06bbe4e1a99c4831-1039x744.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1101(CardRegistry& r) {
    r.registerCard(1101, std::make_unique<BlackRoseSanctum>());
}

}  // namespace riftbound
