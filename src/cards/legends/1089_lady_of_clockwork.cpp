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
    // APPROXIMATION (ENGINE-GAP): a GameObject has a single card_type, so a
    // "unit gear" is modelled by turning the gear's card_type into Unit (base
    // Might = its printed Energy cost; 0 for a token) for as long as the
    // effect lasts. While it is a unit it moves, fights, takes damage and dies
    // like one, but it no longer answers isGear() (effects that look for "a
    // gear" do not see it). An attached Equipment that is chosen is unattached
    // first (a unit can't be attached). The gear and its original Might are
    // remembered on the legend; choosing another gear turns the previous one
    // back into a plain gear, and when the unit gear leaves the board the
    // aura pass turns it back so it is a gear again in every other zone.
    static constexpr const char* kGearKey = "__clockwork_gear";      // on the legend
    static constexpr const char* kOrigMightKey = "__clockwork_orig_might";  // on the gear

    TriggerType triggerType() const override { return TriggerType::AtStartOfMain; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto can_pay = [&]() {
            return payableEnergy(ctx.state, ctx.controller) >= 1 &&
                   !friendlyGear(ctx.state, ctx.controller).empty();
        };
        if (!costAlreadyPaid(ctx) && !can_pay()) { if (finalizingChoices(ctx)) return; return; }
        const int go = confirmOptional(ctx, "Lady of Clockwork: pay [1] to choose a friendly gear?",
                                       [&]() { return costAlreadyPaid(ctx) || can_pay(); });
        if (go != 1) { if (finalizingChoices(ctx)) return; return; }   // waiting, or declined
        const GameObjectId gear = pickTarget(ctx, "Lady of Clockwork: gear to become a unit",
                                             friendlyGear(ctx.state, ctx.controller));
        if (gear == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (gear != kInvalidId && costDueNow(ctx) && !payEnergy(ctx, ctx.controller, 1)) return;
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (gear == kInvalidId || !ctx.state.objectExists(gear)) return;
        auto& g = ctx.state.getObject(gear);
        if (!g.location.has_value() || !g.isGear() || g.controller != ctx.controller) return;

        // "until you choose another gear with me": the previous one reverts.
        if (ctx.state.objectExists(ctx.source)) {
            auto& cc = ctx.state.getObject(ctx.source).card_counters;
            auto it = cc.find(kGearKey);
            if (it != cc.end()) {
                const GameObjectId prev = static_cast<GameObjectId>(it->second);
                if (prev != gear) revert(ctx.state, prev);
            }
        }
        if (g.attached_to.has_value()) ctx.executor.unattachGear(gear);
        auto& u = ctx.state.getObject(gear);
        int energy = 0;
        if (u.card_def_id != kInvalidId) energy = ctx.executor.cardDB().get(u.card_def_id).energy_cost;
        u.card_counters[kOrigMightKey] = u.base_might;
        u.card_type = CardType::Unit;
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
        if (state.objectExists(gid) && state.getObject(gid).location.has_value()) return;
        revert(state, gid);
        legend.card_counters.erase(kGearKey);
    }
private:
    static std::vector<GameObjectId> friendlyGear(const GameState& state, PlayerId me) {
        std::vector<GameObjectId> out;
        for (const auto& [id, o] : state.objects)
            if (o.isGear() && o.controller == me && o.location.has_value()) out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }
    static void revert(GameState& state, GameObjectId gid) {
        if (!state.objectExists(gid)) return;
        auto& g = state.getObject(gid);
        auto it = g.card_counters.find(kOrigMightKey);
        if (it == g.card_counters.end()) return;   // not (or no longer) a unit gear
        g.card_type = CardType::Gear;
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
