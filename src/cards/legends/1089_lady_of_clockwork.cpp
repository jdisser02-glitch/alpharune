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

class LadyOfClockwork : public LegendCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "At the start of your Main Phase, you may pay [1] to choose a friendly
    //  gear. It becomes a unit gear with Might equal to its Energy cost until
    //  you choose another gear with me."
    //
    // A GameObject has a single card_type, so a unit gear is a gear whose
    // card_type becomes Unit (base Might = its printed Energy cost; 0 for a
    // token) with GameObject::unit_gear set: it moves, fights, takes damage
    // and dies like a unit, and isAnyGear() still answers "a gear" for
    // effects that look for or count gear (Orianna, Exquisite; Plaza
    // Guardian; this legend). An attached Equipment that is chosen detaches
    // as it becomes a unit (CR 143.5.b). The "you may", the [1] and the gear
    // are decided as the trigger is finalized (CR 383.3), and the previous
    // unit gear stops being a unit right then: choosing another gear is what
    // ends it (Radiance rules update, 2026-10-16: "there is a small gap
    // between finalizing the trigger ... and the triggered ability
    // resolving ... During that gap, neither gear is a unit"). Equipment
    // attached to it stays attached when it stops being a unit. When the
    // unit gear leaves the board, the aura pass turns it back into a plain
    // gear so it is a gear in every other zone.
    static constexpr const char* kGearKey = "__clockwork_gear";      // on the legend
    static constexpr const char* kOrigMightKey = "__clockwork_orig_might";  // on the gear

    TriggerType triggerType() const override { return TriggerType::AtStartOfMain; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId current = currentUnitGear(ctx.state, ctx.source);
        auto can_pay = [&]() {
            return payableEnergy(ctx.state, ctx.controller) >= 1 &&
                   !choosable(ctx.state, ctx.controller, current).empty();
        };
        if (!costAlreadyPaid(ctx) && !can_pay()) { if (finalizingChoices(ctx)) return; return; }
        const int go = confirmOptional(ctx, "Lady of Clockwork: pay [1] to choose a friendly gear?",
                                       [&]() { return costAlreadyPaid(ctx) || can_pay(); });
        if (go != 1) { if (finalizingChoices(ctx)) return; return; }   // waiting, or declined
        const GameObjectId gear = pickTarget(ctx, "Lady of Clockwork: gear to become a unit",
                                             choosable(ctx.state, ctx.controller, current));
        if (gear == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (gear != kInvalidId && costDueNow(ctx)) {
            if (!payEnergy(ctx, ctx.controller, 1)) return;
            // Choosing another gear ends the previous one's effect now.
            if (current != kInvalidId && current != gear) endUnitGear(ctx, current);
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (gear == kInvalidId || !ctx.state.objectExists(gear)) return;
        auto& g = ctx.state.getObject(gear);
        if (!g.location.has_value() || !g.isGear() || g.controller != ctx.controller) return;

        if (g.attached_to.has_value()) ctx.executor.unattachGear(gear);   // CR 143.5.b
        auto& u = ctx.state.getObject(gear);
        int energy = 0;
        if (u.card_def_id != kInvalidId) energy = ctx.executor.cardDB().get(u.card_def_id).energy_cost;
        u.card_counters[kOrigMightKey] = u.base_might;
        u.card_type = CardType::Unit;
        u.unit_gear = true;
        u.base_might = energy;
        u.recomputeMight();
        if (ctx.state.objectExists(ctx.source))
            ctx.state.getObject(ctx.source).card_counters[kGearKey] = static_cast<int>(gear);
        ctx.events.emit(ObjectStateChangedEvent{gear, "became_unit"});
        ctx.events.logTrace("LADY OF CLOCKWORK: " + u.name + " becomes a unit gear with " +
                            std::to_string(energy) + " [M]");
    }

    // The unit gear stops being a unit once it leaves the board.
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        auto& ps = state.player(controller);
        if (ps.legend_zone == kInvalidId || !state.objectExists(ps.legend_zone)) return;
        auto& legend = state.getObject(ps.legend_zone);
        if (legend.card_def_id != cardDefId()) return;
        auto it = legend.card_counters.find(kGearKey);
        if (it == legend.card_counters.end()) return;
        const GameObjectId gid = static_cast<GameObjectId>(it->second);
        if (state.objectExists(gid) && state.getObject(gid).location.has_value() &&
            state.getObject(gid).card_counters.count(kOrigMightKey))
            return;
        revert(state, gid);
        legend.card_counters.erase(kGearKey);
    }
private:
    /// The gear this legend has made a unit gear, if it still is one.
    static GameObjectId currentUnitGear(const GameState& state, GameObjectId legend) {
        if (!state.objectExists(legend)) return kInvalidId;
        const auto& cc = state.getObject(legend).card_counters;
        auto it = cc.find(kGearKey);
        if (it == cc.end()) return kInvalidId;
        const auto gid = static_cast<GameObjectId>(it->second);
        if (!state.objectExists(gid) || !state.getObject(gid).card_counters.count(kOrigMightKey))
            return kInvalidId;
        return gid;
    }
    /// Friendly gear on the board that can be chosen. The current unit gear is
    /// a gear too, but choosing it again would change nothing, so it is left
    /// out of the options.
    static std::vector<GameObjectId> choosable(const GameState& state, PlayerId me,
                                               GameObjectId current) {
        std::vector<GameObjectId> out;
        for (const auto& [id, o] : state.objects)
            if (o.isAnyGear() && o.controller == me && o.location.has_value() && id != current)
                out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }
    void endUnitGear(CardContext& ctx, GameObjectId gid) {
        revert(ctx.state, gid);
        if (ctx.state.objectExists(ctx.source)) ctx.state.getObject(ctx.source).card_counters.erase(kGearKey);
        if (ctx.state.objectExists(gid))
            ctx.events.logTrace("LADY OF CLOCKWORK: " + ctx.state.getObject(gid).name +
                                " is no longer a unit");
    }
    static void revert(GameState& state, GameObjectId gid) {
        if (!state.objectExists(gid)) return;
        auto& g = state.getObject(gid);
        auto it = g.card_counters.find(kOrigMightKey);
        if (it == g.card_counters.end()) return;   // not (or no longer) a unit gear
        g.card_type = CardType::Gear;
        g.unit_gear = false;
        g.base_might = it->second;
        g.card_counters.erase(it);
        g.damage_marked = 0;
        g.combat_designation = CombatDesignation::None;
        g.recomputeMight();
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1089;
        d.def_id = R"RB(rad-145-167)RB";
        d.name = R"RB(Lady of Clockwork)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-145/167)RB";
        d.collector_number = 145;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Calm, Domain::Mind};
        d.tags = {R"RB(Orianna)RB"};
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(At the start of your Main Phase, you may pay [1] to choose a friendly gear. It becomes a unit gear with Might equal to its Energy cost until you choose another gear with me.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/d1f940425d4d913d8f56c017254343bc898f4892-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1089(CardRegistry& r) {
    r.registerCard(1089, std::make_unique<LadyOfClockwork>());
}

}  // namespace riftbound
