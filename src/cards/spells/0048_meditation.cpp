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

class Meditation : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "As an additional cost to play this, you may exhaust a friendly unit.
    //  If you do, draw 2. Otherwise, draw 1."
    // The player decides whether to pay and WHICH ready friendly unit to
    // exhaust (it matters: exhausting Riven, Broken Blade or Reckless
    // Innovator triggers them). Like Ki Burst, the choices and the payment are
    // made at finalization (kFinalizeResolve), through exhaustObject so "When
    // I become exhausted" triggers fire. The exhausted unit is a cost, not a
    // choice of the spell, so it is not recorded in the item's chosen objects.
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    static constexpr int kAskPayer = 20;     // the payer prompt is published
    static constexpr int kPayerKnown = 21;   // the payer is in resume_data[kPayerSlot]
    static constexpr int kPayerSlot = 6;
    static constexpr int kPaidSlot = 7;      // 1 once the unit was exhausted

    static std::vector<GameObjectId> readyFriendlyUnits(const GameState& state, PlayerId p) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == p && obj.location.has_value() &&
                !obj.is_exhausted)
                out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }

    void onResolve(CardContext& ctx,
                   const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.chain.resuming.has_value()) {
            // Direct invocation (no chain): pay with the first ready unit.
            const auto payers = readyFriendlyUnits(ctx.state, ctx.controller);
            if (!payers.empty()) ctx.executor.exhaustObject(payers.front());
            ctx.executor.drawCards(ctx.controller, payers.empty() ? 1 : 2);
            return;
        }
        auto& ri = *ctx.state.chain.resuming;
        int conf = confirmOptional(ctx,
            "Meditation: exhaust a friendly unit to draw 2 (else draw 1)?",
            [&]() {
                return costAlreadyPaid(ctx) ||
                       !readyFriendlyUnits(ctx.state, ctx.controller).empty();
            });
        if (conf == -1) return;   // waiting for the agent

        bool paid = false;
        if (conf == 1) {
            while (ri.resume_data.size() <= static_cast<size_t>(kPaidSlot))
                ri.resume_data.push_back(0);
            if (ri.resume_point < kAskPayer) {
                const auto payers = readyFriendlyUnits(ctx.state, ctx.controller);
                if (payers.empty()) {
                    ri.resume_data[kPayerSlot] = static_cast<int32_t>(kInvalidId);
                    ri.resume_point = kPayerKnown;
                } else {
                    std::vector<Intent> opts;
                    for (auto u : payers) opts.push_back(objChoice(ctx.controller, u));
                    ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                               "Meditation: exhaust which friendly unit");
                    ri.resume_point = kAskPayer;
                    return;
                }
            } else if (ri.resume_point == kAskPayer) {
                const auto payers = readyFriendlyUnits(ctx.state, ctx.controller);
                GameObjectId payer = takenObject(ctx);
                if (std::find(payers.begin(), payers.end(), payer) == payers.end())
                    payer = payers.empty() ? kInvalidId : payers.front();
                ri.resume_data[kPayerSlot] = static_cast<int32_t>(payer);
                ri.resume_point = kPayerKnown;
            }
            // ── Pay the additional cost ──
            if (costDueNow(ctx)) {
                const auto payer = static_cast<GameObjectId>(ri.resume_data[kPayerSlot]);
                const auto payers = readyFriendlyUnits(ctx.state, ctx.controller);
                if (std::find(payers.begin(), payers.end(), payer) != payers.end()) {
                    ctx.executor.exhaustObject(payer);
                    ri.resume_data[kPaidSlot] = 1;
                    ctx.events.logTrace("MEDITATION: exhausted " +
                                        ctx.state.getObject(payer).name +
                                        " as the additional cost");
                }
            }
            paid = ri.resume_data[kPaidSlot] == 1;
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 355)

        ctx.executor.drawCards(ctx.controller, paid ? 2 : 1);
        ctx.events.logTrace(paid ? "MEDITATION: exhausted a friendly unit -> draw 2"
                                 : "MEDITATION: draw 1");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 48;
        d.def_id = R"RB(ogn-048-298)RB";
        d.name = R"RB(Meditation)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-048/298)RB";
        d.collector_number = 48;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Calm};
        d.energy_cost = 2;
        d.keywords.set(Keyword::Reaction);
        d.ability_text = R"RB([Reaction] (Play any time, even before spells and abilities resolve.)
As an additional cost to play this, you may exhaust a friendly unit. If you do, draw 2. Otherwise, draw 1.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/7103336ab0d7f7bebdaa4155e4258d8d9beb06de-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_48(CardRegistry& r) {
    r.registerCard(48, std::make_unique<Meditation>());
}

} // namespace riftbound
