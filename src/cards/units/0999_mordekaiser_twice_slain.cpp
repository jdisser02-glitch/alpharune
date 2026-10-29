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

class MordekaiserTwiceSlain : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // [Accelerate] and [Assault 3] are keywords (engine-handled).
    //
    // "Once each turn, if I would die, you may kill a friendly unit to heal me,
    //  exhaust me, and recall me instead."
    // APPROXIMATION: the engine asks death replacements only of OTHER objects
    // (Card::applyReplacement is never consulted for the dying unit itself),
    // but both kill paths honour GameObject::death_replacement_recall_pending
    // ("heal it, exhaust it, and recall it instead"). So:
    //  * applyPassiveAura arms that flag while the replacement is still
    //    available this turn and there is another friendly unit to kill;
    //  * when the flag is consumed (I would have died and was recalled
    //    instead), the recall's move fires WhenIMove; that trigger pays the
    //    cost by killing a friendly unit the controller picks.
    // Deviations: the "you may" is always taken while it is affordable, and the
    // friendly unit is killed just after the replacement instead of before it.
    // Arming shares the flag with Tactical Retreat's one-shot replacement.
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        const int turn = state.turn.turn_number;
        for (auto& [id, self] : state.objects) {
            if (self.card_def_id != cardDefId() || self.controller != controller) continue;
            if (!self.isUnit() || !self.location.has_value()) continue;
            auto& c = self.card_counters;
            if (counter(self, kUsedTurn) == turn) continue;           // used this turn
            if (counter(self, kArmedTurn) == turn && !self.death_replacement_recall_pending) {
                c[kUsedTurn] = turn;                                    // it was consumed
                continue;
            }
            bool other_unit = false;
            for (const auto& [oid, o] : state.objects)
                if (oid != id && o.isUnit() && o.controller == controller && o.location.has_value()) {
                    other_unit = true;
                    break;
                }
            if (other_unit) {
                self.death_replacement_recall_pending = true;
                c[kArmedTurn] = turn;
            } else if (counter(self, kArmedTurn) != -1) {
                self.death_replacement_recall_pending = false;          // can't pay: disarm
                c[kArmedTurn] = -1;
            }
        }
    }

    // The recall that replaced my death: armed this turn, flag now consumed,
    // not yet accounted for.
    TriggerType triggerType() const override { return TriggerType::WhenIMove; }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId /*controller*/,
                          TriggerType /*trigger*/, GameObjectId /*subject*/) const override {
        if (!state.objectExists(source)) return false;
        const auto& self = state.getObject(source);
        const int turn = state.turn.turn_number;
        return counter(self, kArmedTurn) == turn && !self.death_replacement_recall_pending &&
               counter(self, kPaidTurn) != turn;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        const int turn = ctx.state.turn.turn_number;
        auto& self = ctx.state.getObject(ctx.source);
        self.card_counters[kUsedTurn] = turn;
        self.card_counters[kPaidTurn] = turn;
        const auto friends = chooseableUnits(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller == ctx.controller && o.id != ctx.source;
        });
        GameObjectId victim = pickTarget(ctx, "Mordekaiser, Twice Slain: kill a friendly unit", friends);
        if (victim == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        ctx.executor.killObject(victim);
        ctx.events.logTrace("MORDEKAISER, TWICE SLAIN: killed " + ctx.state.getObject(victim).name +
                            " to survive (healed, exhausted, recalled)");
    }
private:
    static constexpr const char* kArmedTurn = "__mk_armed_turn";
    static constexpr const char* kUsedTurn = "__mk_used_turn";
    static constexpr const char* kPaidTurn = "__mk_paid_turn";
    static int counter(const GameObject& o, const char* key) {
        auto it = o.card_counters.find(key);
        return it == o.card_counters.end() ? -1 : it->second;
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 999;
        d.def_id = R"RB(rad-020-167)RB";
        d.name = R"RB(Mordekaiser, Twice Slain)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-020/167)RB";
        d.collector_number = 20;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Fury};
        d.tags = {R"RB(Noxus)RB", R"RB(Mordekaiser)RB"};
        d.energy_cost = 7;
        d.power_cost = 1;
        d.might = 5;
        d.rarity = Rarity::Rare;
        d.assault_value = 3;
        d.keywords.set(Keyword::Accelerate);
        d.keywords.set(Keyword::Assault);
        d.ability_text = R"RB([Accelerate] (You may pay [1][R] as an additional cost to have me enter ready.)
[Assault 3] (+3 [M] while I'm an attacker.)
Once each turn, if I would die, you may kill a friendly unit to heal me, exhaust me, and recall me instead.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/66db9510de566ac43157ecdcc96f33b5f553a38d-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_999(CardRegistry& r) {
    r.registerCard(999, std::make_unique<MordekaiserTwiceSlain>());
}

}  // namespace riftbound
