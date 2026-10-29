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

class KiBurst : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "As an additional cost to play this, exhaust a friendly unit.
    //  [Stun] a unit."
    //
    // DEVIATION: the engine has no mandatory "exhaust a friendly unit"
    // additional cost for spells (ActivationCost::exhaust_friendly_unit exists
    // only for activated abilities). The card models it itself:
    //   * it is playable only while its controller has a ready friendly unit
    //     on the board (hasLegalTargets);
    //   * its choices are made at finalization (kFinalizeResolve): the unit to
    //     exhaust first, then the unit to stun, and the exhaust is paid there
    //     (costDueNow), through EffectExecutor::exhaustObject so "becomes
    //     exhausted" triggers fire. This is after the Energy payment rather
    //     than alongside it, and the unit exhausted is not "chosen" by the
    //     spell (it is not recorded in the chain item's chosen objects).
    // If the cost still has to be paid at resolution (no finalize pass ran)
    // and can't be, the spell does nothing.
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    static constexpr int kPayerSlot = 6;   // resume_data index of the exhausted unit

    static std::vector<GameObjectId> readyFriendlyUnits(const GameState& state, PlayerId p) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == p && obj.location.has_value() &&
                !obj.is_exhausted)
                out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }

    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        return !readyFriendlyUnits(state, controller).empty();
    }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.chain.resuming.has_value()) return;
        auto& ri = *ctx.state.chain.resuming;
        constexpr int kDone = 30;
        if (ri.resume_point == kDone) return;

        // ── The additional cost: which friendly unit to exhaust ──
        if (ri.resume_point == 0) {
            const auto payers = readyFriendlyUnits(ctx.state, ctx.controller);
            if (payers.empty()) {
                ctx.events.logTrace("KI BURST: no ready friendly unit to exhaust");
                ri.resume_point = kDone;
                return;
            }
            std::vector<Intent> opts;
            for (auto u : payers) opts.push_back(objChoice(ctx.controller, u));
            ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                       "Ki Burst: exhaust a friendly unit (additional cost)");
            ri.resume_point = 1;
            return;
        }
        if (ri.resume_point == 1) {
            const auto payers = readyFriendlyUnits(ctx.state, ctx.controller);
            GameObjectId payer = takenObject(ctx);
            if (std::find(payers.begin(), payers.end(), payer) == payers.end())
                payer = payers.empty() ? kInvalidId : payers.front();
            while (ri.resume_data.size() <= static_cast<size_t>(kPayerSlot))
                ri.resume_data.push_back(0);
            ri.resume_data[kPayerSlot] = static_cast<int32_t>(payer);
            ri.resume_point = 6;   // hand over to pickTarget (6/7/8)
        }

        // ── The target: a unit to stun ──
        const auto stunnable = chooseableUnits(ctx.state, ctx.controller,
                                               [](const GameObject&) { return true; });
        GameObjectId victim = pickTarget(ctx, "Ki Burst: stun a unit", stunnable);
        if (victim == kInvalidId && ri.resume_point == 7) return;   // waiting for the agent

        // ── Pay the additional cost ──
        if (costDueNow(ctx)) {
            const GameObjectId payer = ri.resume_data.size() > static_cast<size_t>(kPayerSlot)
                ? static_cast<GameObjectId>(ri.resume_data[kPayerSlot]) : kInvalidId;
            const auto payers = readyFriendlyUnits(ctx.state, ctx.controller);
            if (std::find(payers.begin(), payers.end(), payer) == payers.end()) {
                ctx.events.logTrace("KI BURST: the additional cost can't be paid");
                return;
            }
            ctx.executor.exhaustObject(payer);
            ctx.events.logTrace("KI BURST: exhausted " + ctx.state.getObject(payer).name +
                                " as the additional cost");
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 355)

        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        ctx.executor.stunUnitBy(victim, ctx.source);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 972;
        d.def_id = R"RB(rad-028-167)RB";
        d.name = R"RB(Ki Burst)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-028/167)RB";
        d.collector_number = 28;
        d.card_type = CardType::Spell;
        d.domains = {Domain::Calm};
        d.energy_cost = 1;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
As an additional cost to play this, exhaust a friendly unit.
[Stun] a unit. (It doesn't deal combat damage this turn.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/33af916b50007c01b33e78aef3dc99b757edc3f2-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_972(CardRegistry& r) {
    r.registerCard(972, std::make_unique<KiBurst>());
}

}  // namespace riftbound
