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

class ParallelConvergence : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "As an additional cost to play this, return a friendly unit to its
    //  owner's hand. Deal 5 to a unit at a battlefield."
    //
    // DEVIATION: the engine has no mandatory "return a friendly unit"
    // additional cost for spells. As with Ki Burst's exhaust cost, the card
    // models it itself:
    //   * it is playable only while its controller has a unit on the board and
    //     there is a unit at a battlefield to choose (hasLegalTargets);
    //   * its choices are made at finalization (kFinalizeResolve): first the
    //     unit to deal 5 to (CR 355: targets, then costs), then the friendly
    //     unit to return, which is returned there (costDueNow). This is after
    //     the Energy/Power payment rather than alongside it, and the returned
    //     unit is not "chosen" by the spell.
    // If the cost still has to be paid at resolution (no finalize pass ran)
    // and can't be, the spell does nothing. Returning the chosen unit itself
    // is allowed; the damage then has no legal target (mistarget).
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    static constexpr int kPayerSlot = 6;    // resume_data index of the returned unit
    static constexpr int kTakePayer = 21;   // resume points after pickTarget's 6/7/8
    static constexpr int kPayerChosen = 22;
    static constexpr int kDone = 30;

    static std::vector<GameObjectId> friendlyUnits(const GameState& state, PlayerId p) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == p && obj.location.has_value()) out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }
    static std::vector<GameObjectId> unitsAtBattlefields(const GameState& state, PlayerId p) {
        auto out = chooseableUnits(state, p, [](const GameObject& o) { return o.isAtBattlefield(); });
        std::sort(out.begin(), out.end());
        return out;
    }

    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        return !friendlyUnits(state, controller).empty() &&
               !unitsAtBattlefields(state, controller).empty();
    }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.chain.resuming.has_value()) return;
        auto& ri = *ctx.state.chain.resuming;
        if (ri.resume_point == kDone) return;

        // ── The target: a unit at a battlefield ──
        const GameObjectId victim = pickTarget(ctx, "Parallel Convergence: deal 5",
                                               unitsAtBattlefields(ctx.state, ctx.controller));
        if (ri.resume_point == 7) return;   // waiting for the agent

        // ── The additional cost: which friendly unit to return ──
        if (ri.resume_point == 8) {
            const auto payers = friendlyUnits(ctx.state, ctx.controller);
            if (payers.empty()) {
                ctx.events.logTrace("PARALLEL CONVERGENCE: no friendly unit to return");
                ri.resume_point = kDone;
                return;
            }
            std::vector<Intent> opts;
            for (auto u : payers) opts.push_back(objChoice(ctx.controller, u));
            ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                       "Parallel Convergence: return a friendly unit (additional cost)");
            ri.resume_point = kTakePayer;
            return;
        }
        if (ri.resume_point == kTakePayer) {
            const auto payers = friendlyUnits(ctx.state, ctx.controller);
            GameObjectId payer = takenObject(ctx);
            if (std::find(payers.begin(), payers.end(), payer) == payers.end())
                payer = payers.empty() ? kInvalidId : payers.front();
            while (ri.resume_data.size() <= static_cast<size_t>(kPayerSlot))
                ri.resume_data.push_back(0);
            ri.resume_data[kPayerSlot] = static_cast<int32_t>(payer);
            ri.resume_point = kPayerChosen;
        }

        // ── Pay the additional cost ──
        if (costDueNow(ctx)) {
            const GameObjectId payer = ri.resume_data.size() > static_cast<size_t>(kPayerSlot)
                ? static_cast<GameObjectId>(ri.resume_data[kPayerSlot]) : kInvalidId;
            const auto payers = friendlyUnits(ctx.state, ctx.controller);
            if (std::find(payers.begin(), payers.end(), payer) == payers.end()) {
                ctx.events.logTrace("PARALLEL CONVERGENCE: the additional cost can't be paid");
                ri.resume_point = kDone;
                return;
            }
            const std::string name = ctx.state.getObject(payer).name;
            ctx.executor.bounceToHand(payer);
            ctx.events.logTrace("PARALLEL CONVERGENCE: returned " + name +
                                " to hand as the additional cost");
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 355)

        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        ctx.executor.dealDamage(victim, 5, ctx.source);
        if (ctx.state.objectExists(victim) && ctx.state.getObject(victim).hasLethalDamage())
            ctx.executor.killObject(victim);
        ctx.events.logTrace("PARALLEL CONVERGENCE: dealt 5");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1019;
        d.def_id = R"RB(rad-051-167)RB";
        d.name = R"RB(Parallel Convergence)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-051/167)RB";
        d.collector_number = 51;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Mind};
        d.energy_cost = 2;
        d.power_cost = 1;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(As an additional cost to play this, return a friendly unit to its owner's hand.
Deal 5 to a unit at a battlefield.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/55e382346c6fa890936a1cf14abaf57b8ee0b8f2-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1019(CardRegistry& r) {
    r.registerCard(1019, std::make_unique<ParallelConvergence>());
}

}  // namespace riftbound
