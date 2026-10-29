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

class DetonationAmplifier : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // Two triggered abilities:
    //  * "When you play this, you may kill another friendly unit or gear to
    //    draw 1." The kill is the cost of the draw: the yes/no and the victim
    //    are chosen, and the victim killed, as the ability is finalized.
    //  * "Friendly [Deathknell] effects here trigger an additional time."
    //    APPROXIMATION: the engine's Deathknell doubling (Karthus) is per
    //    player, not per battlefield, so this card watches friendly unit deaths
    //    (WhenAFriendlyUnitDies). When the unit had [Deathknell] and died here,
    //    its death ability is put on the chain once more (as Karthus does,
    //    through the trigger queue). The extra copy goes on the chain when this
    //    watcher resolves rather than at the same instant as the original.
    //    Gear Deathknell is not covered (the engine has no gear death event).
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayThis, TriggerType::WhenAFriendlyUnitDies};
    }

    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId /*controller*/,
                          TriggerType trigger, GameObjectId subject) const override {
        if (trigger != TriggerType::WhenAFriendlyUnitDies) return true;
        if (!state.objectExists(source) || !state.objectExists(subject)) return false;
        const auto& self = state.getObject(source);
        const auto& dead = state.getObject(subject);
        if (!self.location.has_value() || !dead.last_location.has_value()) return false;
        if (*dead.last_location != *self.location) return false;          // died here
        const bool deathknell = dead.last_known.valid
            ? dead.last_known.keywords.has(Keyword::Deathknell)
            : dead.hasKeyword(Keyword::Deathknell);
        return deathknell;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::WhenAFriendlyUnitDies) {
            if (finalizingChoices(ctx)) return;   // no choices (CR 383.3)
            const GameObjectId dead = ctx.state.chain.resuming.has_value()
                ? ctx.state.chain.resuming->triggering_subject : kInvalidId;
            if (dead == kInvalidId || !ctx.state.objectExists(dead)) return;
            const auto& d = ctx.state.getObject(dead);
            ctx.executor.queueTriggeredAbility(dead, d.controller, TriggerType::WhenIDie);
            ctx.events.logTrace("DETONATION AMPLIFIER: " + d.name +
                                "'s Deathknell triggers an additional time");
            return;
        }

        // When you play this: you may kill another friendly unit or gear to draw 1.
        auto victims = [&]() {
            std::vector<GameObjectId> out;
            for (const auto& [id, obj] : ctx.state.objects) {
                if (id == ctx.source || obj.controller != ctx.controller) continue;
                if (!obj.location.has_value() || !(obj.isUnit() || obj.isGear())) continue;
                out.push_back(id);
            }
            return out;
        };
        const bool paid = costAlreadyPaid(ctx);
        if (!paid && victims().empty()) { if (finalizingChoices(ctx)) return; return; }
        int go = confirmOptional(ctx, "Detonation Amplifier: kill another friendly unit or gear to draw 1?",
                                 [&]() { return costAlreadyPaid(ctx) || !victims().empty(); });
        GameObjectId victim = kInvalidId;
        if (go == 1 && !paid) victim = pickTarget(ctx, "Detonation Amplifier: kill", victims());
        bool killed = false;
        if (go == 1 && victim != kInvalidId && costDueNow(ctx) && ctx.state.objectExists(victim)) {
            ctx.executor.killObject(victim);      // the cost
            killed = true;
        }
        if (finalizingChoices(ctx)) return;       // choices and costs end here (CR 383.3)
        if (go != 1) return;
        if (!killed && !costAlreadyPaid(ctx)) return;   // waiting for the pick, or no victim
        ctx.executor.drawCards(ctx.controller, 1);
        ctx.events.logTrace("DETONATION AMPLIFIER: killed a friendly object -> draw 1");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 996;
        d.def_id = R"RB(rad-017-167)RB";
        d.name = R"RB(Detonation Amplifier)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-017/167)RB";
        d.collector_number = 17;
        d.artist = R"RB(Fish Art Studio)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
When you play this, you may kill another friendly unit or gear to draw 1.
Friendly [Deathknell] effects here trigger an additional time.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/0cd07e77e69a53b78486b07dd7610183c953d581-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_996(CardRegistry& r) {
    r.registerCard(996, std::make_unique<DetonationAmplifier>());
}

}  // namespace riftbound
