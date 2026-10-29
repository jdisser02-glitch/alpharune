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

class DemacianStandard : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }   // [Deploy] (CR 830)
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When one or more friendly units attack here, give one of them +2 [M]
    //  this turn.
    //  When you hold here, you may return this to its owner's hand."
    //
    // ENGINE GAPS, approximated with the events that do reach a gear standing
    // at a battlefield:
    //  * Attack: the engine announces "a friendly unit attacks alone" to the
    //    attacking player's board (WhenAUnitAttacksOrDefendsAlone, the lone
    //    unit as subject); there is no "friendly units attack here" event for
    //    a gear. So the +2 [M] is given when exactly one friendly unit attacks
    //    here (then "one of them" is that unit). An attack here by two or more
    //    friendly units gives nothing until the engine announces combat starts
    //    to [Deploy] gear (as it does conquers, WhenYouConquerHere).
    //  * Hold: "When you hold here" reaches gear at the scored battlefield.
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenAUnitAttacksOrDefendsAlone, TriggerType::WhenYouHoldHere};
    }

    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType trigger, GameObjectId subject) const override {
        if (!state.objectExists(source)) return false;
        const auto& self = state.getObject(source);
        if (self.attached_to.has_value()) return false;
        const auto here = self.battlefieldId();
        if (!here.has_value()) return false;
        if (trigger == TriggerType::WhenAUnitAttacksOrDefendsAlone) {
            if (!state.objectExists(subject)) return false;
            const auto& u = state.getObject(subject);
            return u.isUnit() && u.controller == controller &&
                   u.combat_designation == CombatDesignation::Attacker && u.battlefieldId() == here;
        }
        if (trigger == TriggerType::WhenYouHoldHere) return true;
        return false;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::WhenAUnitAttacksOrDefendsAlone) {
            if (finalizingChoices(ctx)) return;   // no choices: the lone attacker
            const GameObjectId unit = ctx.state.chain.resuming.has_value()
                ? ctx.state.chain.resuming->triggering_subject : kInvalidId;
            if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;
            const auto& u = ctx.state.getObject(unit);
            if (!u.isUnit() || !u.location.has_value()) return;
            ctx.executor.giveTemporaryMight(unit, 2);
            ctx.events.logTrace("DEMACIAN STANDARD: " + u.name + " attacks here -> +2 [M] this turn");
            return;
        }
        if (ctx.firing_trigger == TriggerType::WhenYouHoldHere) {
            auto here_ok = [&]() {
                return ctx.state.objectExists(ctx.source) &&
                       ctx.state.getObject(ctx.source).location.has_value();
            };
            int go = confirmOptional(ctx, "Demacian Standard: return this to its owner's hand?", here_ok);
            if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
            if (go != 1 || !here_ok()) return;
            ctx.executor.bounceToHand(ctx.source);
            ctx.events.logTrace("DEMACIAN STANDARD: held here -> returned to its owner's hand");
            return;
        }
        if (finalizingChoices(ctx)) return;
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1098;
        d.def_id = R"RB(rad-156-167)RB";
        d.name = R"RB(Demacian Standard)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-156/167)RB";
        d.collector_number = 156;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Gear;
        d.super_type = SuperType::Signature;
        d.domains = {Domain::Body, Domain::Order};
        d.tags = {R"RB(Device)RB", R"RB(Jarvan IV)RB"};
        d.energy_cost = 2;
        d.power_cost = 1;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
When one or more friendly units attack here, give one of them +2 [M] this turn.
When you hold here, you may return this to its owner's hand.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/315b0f7901a83df38b9765f12b1edfdc7de566a3-744x1040.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1098(CardRegistry& r) {
    r.registerCard(1098, std::make_unique<DemacianStandard>());
}

}  // namespace riftbound
