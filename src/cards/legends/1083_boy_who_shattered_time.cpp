#include "cards/card.h"
#include "cards/card_helpers.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace riftbound {
namespace {

class BoyWhoShatteredTime : public LegendCard {
public:
    const CardDef& def() const override { return def_; }

    // "[Empower] — [1], [E] (Use only if not Empowered.)"
    EmpowerSpec empowerSpec() const override {
        auto s = empowerCost(1);
        s.cost.exhaust = true;
        return s;
    }

    // "Disempower me, [E]: Reduce the cost of the next unit you play this turn
    //  by [2][A] and give it "Kill me at end of turn.""
    // The reduction is a one-shot cost modifier (2 Energy and 1 Power of any
    // domain, next unit only, this turn). The unit that uses it is found when
    // it is played: WhenYouPlayACard fires on the legend with the played card
    // as subject, and triggers only once the modifier has been consumed.
    //
    // APPROXIMATION: the engine cannot grant a triggered ability to another
    // object, so "Kill me at end of turn" is carried by this legend: the unit
    // is remembered (tracked_objects, stamped "__ekko_kill_eot" with the turn)
    // and the legend's own AtEndOfTurn trigger kills it. Being marked happens
    // through a (choice-free) triggered ability rather than as the unit is
    // played.
    bool hasActivatedAbility() const override { return true; }
    ActivationCost getActivationCost() const override {
        ActivationCost c;
        c.exhaust = true;
        c.disempower_self = true;
        return c;
    }
    void onActivate(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        PlayerState::CostModifier m;
        m.source = ctx.source;
        m.energy_reduction = 2;
        m.power_reduction = 1;
        m.next_unit_only = true;
        m.this_turn_only = true;
        ctx.state.player(ctx.controller).cost_modifiers.push_back(m);
        if (ctx.state.objectExists(ctx.source))
            ctx.state.getObject(ctx.source).card_counters["__ekko_armed_turn"] = stamp(ctx.state);
        ctx.events.logTrace("BOY WHO SHATTERED TIME: next unit this turn costs [2][A] less");
    }

    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayACard, TriggerType::AtEndOfTurn};
    }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType trigger, GameObjectId subject) const override {
        if (!state.objectExists(source)) return false;
        const auto& self = state.getObject(source);
        if (trigger == TriggerType::WhenYouPlayACard) {
            if (counter(self, "__ekko_armed_turn") != stamp(state)) return false;
            if (subject == kInvalidId || !state.objectExists(subject) ||
                !state.getObject(subject).isUnit())
                return false;
            for (const auto& m : state.player(controller).cost_modifiers)
                if (m.source == source && m.next_unit_only) return false;  // not used yet
            return true;
        }
        if (trigger == TriggerType::AtEndOfTurn) {
            for (auto u : self.tracked_objects)
                if (state.objectExists(u) &&
                    counter(state.getObject(u), "__ekko_kill_eot") == stamp(state))
                    return true;
            return false;
        }
        return true;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& self = ctx.state.getObject(ctx.source);
        if (ctx.firing_trigger == TriggerType::AtEndOfTurn) {
            std::vector<GameObjectId> doomed;
            for (auto u : self.tracked_objects)
                if (ctx.state.objectExists(u) &&
                    counter(ctx.state.getObject(u), "__ekko_kill_eot") == stamp(ctx.state))
                    doomed.push_back(u);
            self.tracked_objects.clear();
            for (auto u : doomed) {
                auto& unit = ctx.state.getObject(u);
                unit.card_counters.erase("__ekko_kill_eot");
                if (!unit.location.has_value()) continue;
                ctx.events.logTrace("BOY WHO SHATTERED TIME: " + unit.name + " dies at end of turn");
                ctx.executor.killObject(u);
            }
            return;
        }
        // WhenYouPlayACard: the unit that got the reduction.
        const GameObjectId unit = ctx.state.chain.resuming.has_value()
            ? ctx.state.chain.resuming->triggering_subject : kInvalidId;
        self.card_counters["__ekko_armed_turn"] = 0;
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        ctx.state.getObject(unit).card_counters["__ekko_kill_eot"] = stamp(ctx.state);
        self.tracked_objects.push_back(unit);
        ctx.events.logTrace("BOY WHO SHATTERED TIME: " + ctx.state.getObject(unit).name +
                            " has \"Kill me at end of turn.\"");
    }
private:
    static int stamp(const GameState& state) { return state.turn.turn_number + 1; }
    static int counter(const GameObject& o, const char* key) {
        auto it = o.card_counters.find(key);
        return it == o.card_counters.end() ? 0 : it->second;
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1083;
        d.def_id = R"RB(rad-139-167)RB";
        d.name = R"RB(Boy Who Shattered Time)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-139/167)RB";
        d.collector_number = 139;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Fury, Domain::Mind};
        d.tags = {R"RB(Ekko)RB"};
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Empower);
        d.ability_text = R"RB([Empower] — [1], [E] (Use only if not Empowered.)
Disempower me, [E]: Reduce the cost of the next unit you play this turn by [2][A] and give it "Kill me at end of turn.")RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/8ca3ef446631784ce1d261e30f6a163843ffcb2b-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1083(CardRegistry& r) {
    r.registerCard(1083, std::make_unique<BoyWhoShatteredTime>());
}

}  // namespace riftbound
