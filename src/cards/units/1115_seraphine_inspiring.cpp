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

// RECONSTRUCTED TEXT (not in cards/card_index.json; confidence: high).
// Gathered from web-search summaries (WebFetch to card sites was blocked);
// ability_text written in the repo's symbol convention with standard reminder
// text. Champion unit (the Seraphine legend is RAD-151). Showcase alt art
// RAD-065a. No image_url / artist.
class SeraphineInspiring : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "[Disarm 2]" (CR 831): "When I attack, give an enemy unit here -2 [M]
    //  this turn." The enemy unit is chosen as the ability is finalized.
    //
    // "Once each turn, when you exhaust three or more friendly units at the
    //  same time, you may draw 1."
    // APPROXIMATION (engine gap): the engine announces each unit becoming
    // exhausted separately and has no "several units at the same time" event.
    // So I trigger on every friendly unit becoming exhausted (myself included)
    // until the ability has been used this turn, and the exhaustions whose
    // abilities are put on the chain together (still waiting to be finalized
    // as one batch — e.g. all the units of one standard move, or the payers
    // of one cost) count as "at the same time": as the first of them is
    // finalized it counts itself plus the ones still waiting; with three or
    // more it is the real ability (asks the "you may" and draws as it
    // resolves) and the others do nothing. Who exhausted the units is not
    // checked (the engine does not record it), so an opponent's effect that
    // exhausts three of your units at once also counts. Each friendly
    // exhaustion before the ability is used this turn puts a (usually inert)
    // ability on the chain.
    static constexpr const char* kUsedKey = "__seraphine_inspiring_used_turn";
    static constexpr int kQualifySlot = 8;   // resume_data slot: 1 = the real ability, 2 = inert

    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenIAttack, TriggerType::WhenIBecomeExhausted,
                TriggerType::WhenAnotherFriendlyUnitBecomesExhausted};
    }
    static bool isExhaustTrigger(TriggerType t) {
        return t == TriggerType::WhenIBecomeExhausted ||
               t == TriggerType::WhenAnotherFriendlyUnitBecomesExhausted;
    }
    static bool usedThisTurn(const GameState& state, GameObjectId source) {
        if (!state.objectExists(source)) return true;
        const auto& self = state.getObject(source);
        auto it = self.card_counters.find(kUsedKey);
        return it != self.card_counters.end() && it->second == state.turn.turn_number + 1;
    }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId /*controller*/,
                          TriggerType trigger, GameObjectId /*subject*/) const override {
        if (isExhaustTrigger(trigger)) return !usedThisTurn(state, source);
        return true;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::WhenIAttack) {
            auto enemies = disarmTargets(ctx, ctx.source);
            std::sort(enemies.begin(), enemies.end());
            const GameObjectId victim = pickTarget(ctx, "Seraphine, Inspiring: [Disarm 2]", enemies);
            if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
            applyDisarm(ctx, victim, 2);          // no-op while waiting / without a target
            return;
        }
        if (!ctx.state.chain.resuming.has_value()) return;
        auto& ri = *ctx.state.chain.resuming;
        while (static_cast<int>(ri.resume_data.size()) <= kQualifySlot) ri.resume_data.push_back(0);
        if (ri.resume_data[kQualifySlot] == 0) {
            // First pass (normally the finalize pass): is this the ability of
            // a batch of three or more simultaneous exhaustions?
            ri.resume_data[kQualifySlot] = qualifies(ctx, ri.id) ? 1 : 2;
        }
        int go = 0;
        if (ri.resume_data[kQualifySlot] == 1)
            go = confirmOptional(ctx, "Seraphine, Inspiring: draw 1?", [] { return true; });
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (ri.resume_data[kQualifySlot] != 1 || go < 0) return;   // inert / waiting
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& self = ctx.state.getObject(ctx.source);
        if (self.card_counters[kUsedKey] == ctx.state.turn.turn_number + 1) return;
        self.card_counters[kUsedKey] = ctx.state.turn.turn_number + 1;   // once each turn
        if (go != 1) return;
        ctx.executor.drawCards(ctx.controller, 1);
        ctx.events.logTrace("SERAPHINE, INSPIRING: exhausted 3+ friendly units at once -> draw 1");
    }
private:
    bool qualifies(const CardContext& ctx, ChainItemId me) const {
        if (usedThisTurn(ctx.state, ctx.source)) return false;
        int batch = 1;   // this exhaustion
        for (const auto& ci : ctx.state.chain.items) {
            if (ci.id == me || ci.source != ctx.source || ci.is_activated_ability ||
                !isExhaustTrigger(ci.fired_trigger))
                continue;
            // Another of my exhaust abilities already is this turn's ability.
            if (static_cast<int>(ci.resume_data.size()) > kQualifySlot &&
                ci.resume_data[kQualifySlot] == 1)
                return false;
            if (ci.status == ChainItemStatus::Pending) ++batch;
        }
        return batch >= 3;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1115;
        d.def_id = R"RB(rad-065-167)RB";
        d.name = R"RB(Seraphine, Inspiring)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-065/167)RB";
        d.collector_number = 65;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Piltover)RB", R"RB(Seraphine)RB"};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.might = 3;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([Disarm 2] (When I attack, give an enemy unit here -2 [M] this turn.)
Once each turn, when you exhaust three or more friendly units at the same time, you may draw 1.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1115(CardRegistry& r) {
    r.registerCard(1115, std::make_unique<SeraphineInspiring>());
}

}  // namespace riftbound
