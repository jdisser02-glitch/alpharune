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

class StarryEyedSongstress : public LegendCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeActivate; }

    // "As an additional cost to play a spell, you may exhaust me and three
    //  friendly units to reduce the cost of that spell by [2]."
    //
    // APPROXIMATION (ENGINE-GAP): the spell-play path has no hook for an
    // optional additional cost offered by ANOTHER card (only the spell's own
    // Card::optionalAdditionalCost). Modelled as a [Reaction] activated ability
    // of the legend: "[E], exhaust three friendly units: the next spell you
    // play this turn costs [2] less" (a next_spell_only CostModifier). The
    // three units are chosen and exhausted as the ability is put on the chain
    // (a cost). Offered only while you have a spell in hand and no discount
    // from it is still waiting; an unused discount is dropped at the next aura
    // pass after the turn it was made in.
    static constexpr const char* kTurnKey = "__songstress_discount_turn";   // on the legend

    bool hasActivatedAbility() const override { return true; }
    bool isReactionAbility() const override { return true; }
    ActivationCost getActivationCost() const override { return {.exhaust = true}; }
    bool canActivateAbility(const GameState& state, PlayerId controller) const override {
        if (readyFriendlyUnits(state, controller, {}).size() < 3) return false;
        const auto& ps = state.player(controller);
        bool spell_in_hand = false;
        for (auto cid : ps.hand)
            if (state.objectExists(cid) && state.getObject(cid).isSpell()) { spell_in_hand = true; break; }
        if (!spell_in_hand) return false;
        for (const auto& m : ps.cost_modifiers)
            if (m.source == ps.legend_zone && m.next_spell_only) return false;   // one waiting already
        return true;
    }
    void onActivate(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!costAlreadyPaid(ctx)) {
            const GameObjectId a = pickTarget(ctx, "Starry-Eyed Songstress: exhaust a friendly unit (1/3)",
                                              readyFriendlyUnits(ctx.state, ctx.controller, {}));
            auto& ri_opt = ctx.state.chain.resuming;
            if (a == kInvalidId && ri_opt.has_value() && ri_opt->resume_point == 7) return;   // waiting
            if (a == kInvalidId) { if (finalizingChoices(ctx)) return; return; }               // can't pay
            auto [b, c] = pickTargetPair(
                ctx, "Starry-Eyed Songstress: exhaust two more friendly units (2-3/3)",
                readyFriendlyUnits(ctx.state, ctx.controller, {a}),
                [&](GameObjectId pb) { return readyFriendlyUnits(ctx.state, ctx.controller, {a, pb}); });
            if (ri_opt.has_value() && (ri_opt->resume_point == 10 || ri_opt->resume_point == 12))
                return;   // waiting
            if (b == kInvalidId || c == kInvalidId) { if (finalizingChoices(ctx)) return; return; }
            if (costDueNow(ctx)) {
                for (auto u : {a, b, c}) ctx.executor.exhaustObject(u);
                ctx.events.logTrace("STARRY-EYED SONGSTRESS: exhausted three friendly units (cost)");
            }
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)

        auto& ps = ctx.state.player(ctx.controller);
        PlayerState::CostModifier m;
        m.source = ctx.source;
        m.energy_reduction = 2;
        m.next_spell_only = true;
        m.this_turn_only = true;
        m.affects_friendly_only = true;
        ps.cost_modifiers.push_back(m);
        if (ctx.state.objectExists(ctx.source))
            ctx.state.getObject(ctx.source).card_counters[kTurnKey] = ctx.state.turn.turn_number + 1;
        ctx.events.logTrace("STARRY-EYED SONGSTRESS: the next spell this turn costs [2] less");
    }

    // Drop a discount left over from an earlier turn (CostModifier's
    // this_turn_only is not enforced by the engine).
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        auto& ps = state.player(controller);
        if (ps.legend_zone == kInvalidId || !state.objectExists(ps.legend_zone)) return;
        auto& legend = state.getObject(ps.legend_zone);
        if (legend.card_def_id != cardDefId()) return;
        auto it = legend.card_counters.find(kTurnKey);
        const bool current = it != legend.card_counters.end() &&
                             it->second == state.turn.turn_number + 1;
        if (current) return;
        const GameObjectId me = ps.legend_zone;
        ps.cost_modifiers.erase(
            std::remove_if(ps.cost_modifiers.begin(), ps.cost_modifiers.end(),
                           [&](const PlayerState::CostModifier& mod) {
                               return mod.source == me && mod.next_spell_only;
                           }),
            ps.cost_modifiers.end());
    }
private:
    static std::vector<GameObjectId> readyFriendlyUnits(const GameState& state, PlayerId me,
                                                        const std::vector<GameObjectId>& exclude) {
        std::vector<GameObjectId> out;
        for (const auto& [id, o] : state.objects) {
            if (!o.isUnit() || o.controller != me || !o.location.has_value() || o.is_exhausted) continue;
            if (std::find(exclude.begin(), exclude.end(), id) != exclude.end()) continue;
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1093;
        d.def_id = R"RB(rad-151-167)RB";
        d.name = R"RB(Starry-Eyed Songstress)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-151/167)RB";
        d.collector_number = 151;
        d.artist = R"RB(Naifan Zhang)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Mind, Domain::Order};
        d.tags = {R"RB(Seraphine)RB"};
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(As an additional cost to play a spell, you may exhaust me and three friendly units to reduce the cost of that spell by [2].)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/399262205b15aa3b6306eb1d80a308791dcbe2ca-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1093(CardRegistry& r) {
    r.registerCard(1093, std::make_unique<StarryEyedSongstress>());
}

}  // namespace riftbound
