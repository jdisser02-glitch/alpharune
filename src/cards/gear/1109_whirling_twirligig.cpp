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

// RECONSTRUCTED TEXT (not in cards/card_index.json; confidence: medium).
// Gathered from web-search summaries (WebFetch to card sites was blocked);
// ability_text written in the repo's symbol convention with standard reminder
// text. Rarity conflict: TCGplayer and most retailers say Epic, the LoL wiki
// says Uncommon (Epic used). Where the Recruit / Mech tokens are played is not
// stated by any source — played to your base here (the default for "play a
// token"). Tags not found (possibly none or 'Device'; none used). Artist:
// Caravan Studio per the LoL wiki (not set: no artist/image for these).
class WhirlingTwirligig : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeActivate; }

    // 0: "[E]: Play a Gold gear token exhausted."
    // 1: "Kill a friendly gear, [E]: Play a 1 [M] Recruit unit token."
    // 2: "Kill a friendly unit, [E]: Play a 3 [M] Mech unit token."
    // Ability 2's unit rides in the intent's targets and is killed by the
    // engine as the cost (ActivationCost::kill_friendly_unit). The engine has
    // no "kill a friendly gear" cost, so ability 1 picks the gear and kills it
    // as the ability is finalized (costDueNow — before anyone can respond).
    // This gear itself may be the gear killed, as with Gold's "Kill this, [E]".
    // The tokens enter exhausted, at your base.
    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility gold;
        gold.cost.exhaust = true;
        ActivatedAbility recruit;
        recruit.cost.exhaust = true;
        ActivatedAbility mech;
        mech.cost.exhaust = true;
        mech.cost.kill_friendly_unit = true;
        mech.targets = TargetRequirements{.count = 1, .must_be_unit = true, .must_be_friendly = true};
        return {gold, recruit, mech};
    }
    bool canActivateAbilityAt(const GameState& state, PlayerId controller,
                              GameObjectId source, int ability_index) const override {
        if (!Card::canActivateAbilityAt(state, controller, source, ability_index)) return false;
        if (ability_index == 1) return !friendlyGear(state, controller).empty();
        return true;
    }
    /// Ability 2's cost: a friendly unit on the board to kill.
    std::vector<GameObjectId> enumerateLegalTargets(
        const GameState& state, PlayerId controller, int ability_index) const override {
        std::vector<GameObjectId> out;
        if (ability_index != 2) return out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == controller && obj.location.has_value())
                out.push_back(id);
        return out;
    }
    void onActivate(CardContext& ctx, int ability_index,
                    const std::vector<GameObjectId>& /*targets*/) override {
        const LocationId base{BaseLocation{ctx.controller}};
        if (ability_index == 0) {
            if (finalizingChoices(ctx)) return;   // no choices
            createGoldExhausted(ctx);
            ctx.events.logTrace("WHIRLING TWIRLIGIG: Gold gear token (exhausted)");
            return;
        }
        if (ability_index == 1) {
            bool paid = costAlreadyPaid(ctx);
            GameObjectId gear = kInvalidId;
            if (!paid)
                gear = pickTarget(ctx, "Whirling Twirligig: kill a friendly gear",
                                  friendlyGear(ctx.state, ctx.controller));
            if (!paid && gear != kInvalidId && ctx.state.objectExists(gear) &&
                ctx.state.getObject(gear).location.has_value() && costDueNow(ctx)) {
                const std::string name = ctx.state.getObject(gear).name;
                ctx.executor.killObject(gear);
                paid = true;
                ctx.events.logTrace("WHIRLING TWIRLIGIG: killed " + name + " (cost)");
            }
            if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
            if (!paid) return;                    // waiting for the agent / cost not paid
            ctx.executor.createToken(ctx.controller, CardType::Unit, "Recruit", 1, {"Recruit"},
                                     KeywordSet{}, base, /*enter_ready=*/false);
            ctx.events.logTrace("WHIRLING TWIRLIGIG: 1 [M] Recruit token to base");
            return;
        }
        if (finalizingChoices(ctx)) return;       // the unit was killed by the engine
        ctx.executor.createToken(ctx.controller, CardType::Unit, "Mech", 3, {"Mech"},
                                 KeywordSet{}, base, /*enter_ready=*/false);
        ctx.events.logTrace("WHIRLING TWIRLIGIG: 3 [M] Mech token to base");
    }
private:
    static std::vector<GameObjectId> friendlyGear(const GameState& state, PlayerId player) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isGear() && obj.controller == player && obj.location.has_value())
                out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1109;
        d.def_id = R"RB(rad-022-167)RB";
        d.name = R"RB(Whirling Twirligig)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-022/167)RB";
        d.collector_number = 22;
        d.card_type = CardType::Gear;
        d.domains = {Domain::Fury};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB([E]: Play a Gold gear token exhausted. (It has "[Reaction][>] Kill this, [E]: [Add] [A].")
Kill a friendly gear, [E]: Play a 1 [M] Recruit unit token.
Kill a friendly unit, [E]: Play a 3 [M] Mech unit token.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1109(CardRegistry& r) {
    r.registerCard(1109, std::make_unique<WhirlingTwirligig>());
}

}  // namespace riftbound
