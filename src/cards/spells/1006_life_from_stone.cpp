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

class LifeFromStone : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "As an additional cost to play this, exhaust a friendly unit.
    //  Draw 1 and channel 1 rune exhausted."
    //
    // DEVIATION (same model as Ki Burst): the engine has no mandatory "exhaust
    // a friendly unit" additional cost for spells, so the card models it:
    //   * playable only while its controller has a ready friendly unit on the
    //     board (hasLegalTargets);
    //   * the unit to exhaust is chosen at finalization (kFinalizeResolve) and
    //     exhausted there (costDueNow) through EffectExecutor::exhaustObject so
    //     "becomes exhausted" triggers fire. This is after the Energy payment
    //     rather than alongside it.
    // If no finalize pass ran and the cost can't be paid at resolution, the
    // spell does nothing.
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    static constexpr int kPayerSlot = 6;   // resume_data index of the exhausted unit
    static constexpr int kChosen = 2;      // resume_point: payer chosen
    static constexpr int kDone = 30;       // resume_point: nothing (more) to do

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
        if (ri.resume_point == kDone) return;

        // ── The additional cost: which friendly unit to exhaust ──
        if (ri.resume_point == 0) {
            const auto payers = readyFriendlyUnits(ctx.state, ctx.controller);
            if (payers.empty()) {
                ctx.events.logTrace("LIFE FROM STONE: no ready friendly unit to exhaust");
                ri.resume_point = kDone;
                return;
            }
            std::vector<Intent> opts;
            for (auto u : payers) opts.push_back(objChoice(ctx.controller, u));
            ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                       "Life from Stone: exhaust a friendly unit (additional cost)");
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
            ri.resume_point = kChosen;
        }

        // ── Pay the additional cost ──
        if (costDueNow(ctx)) {
            const GameObjectId payer = ri.resume_data.size() > static_cast<size_t>(kPayerSlot)
                ? static_cast<GameObjectId>(ri.resume_data[kPayerSlot]) : kInvalidId;
            const auto payers = readyFriendlyUnits(ctx.state, ctx.controller);
            if (std::find(payers.begin(), payers.end(), payer) == payers.end()) {
                ctx.events.logTrace("LIFE FROM STONE: the additional cost can't be paid");
                ri.resume_point = kDone;
                return;
            }
            ctx.executor.exhaustObject(payer);
            ctx.events.logTrace("LIFE FROM STONE: exhausted " + ctx.state.getObject(payer).name +
                                " as the additional cost");
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 355)

        ctx.executor.drawCards(ctx.controller, 1);
        ctx.executor.channelRunes(ctx.controller, 1, /*enter_exhausted=*/true);
        ri.resume_point = kDone;
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1006;
        d.def_id = R"RB(rad-029-167)RB";
        d.name = R"RB(Life from Stone)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-029/167)RB";
        d.collector_number = 29;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Calm};
        d.energy_cost = 1;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Reaction);
        d.ability_text = R"RB([Reaction] (Play any time, even before spells and abilities resolve.)
As an additional cost to play this, exhaust a friendly unit.
Draw 1 and channel 1 rune exhausted.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/6a849c0425f0fecb8d00e1b9494e975c4dbbba75-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1006(CardRegistry& r) {
    r.registerCard(1006, std::make_unique<LifeFromStone>());
}

}  // namespace riftbound
