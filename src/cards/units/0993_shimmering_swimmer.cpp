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
#include "cards/radiance_helpers.h"

namespace riftbound {
namespace {

class ShimmeringSwimmer : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "You may kill a friendly unit or gear as an additional cost to play me.
    //  When you play me, if you paid the additional cost, deal 2 to a unit."
    // APPROXIMATION: the engine's optional additional costs
    // (Card::OptionalAdditionalCost) cover only Energy / Power / discards, so
    // the kill is offered and paid as the play trigger is finalized, right
    // after I enter the board, instead of while I am being played: "you may"
    // kill (confirmOptional), which unit or gear (pair pick A, never me), and
    // the unit to deal 2 to (pair pick B, never the killed one). A gear killed
    // this way goes through killWithDeathTrigger so its death ability triggers.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto fodder = sacrificeable(ctx);
        const int go = confirmOptional(ctx, "Shimmering Swimmer: kill a friendly unit or gear?",
                                       [&]() { return costAlreadyPaid(ctx) || !fodder.empty(); });
        bool paid = go == 1 && costAlreadyPaid(ctx);
        GameObjectId victim = kInvalidId;
        if (go == 1) {
            auto [killed, hit] = pickTargetPair(ctx, "Shimmering Swimmer", fodder,
                [&](GameObjectId a) {
                    return chooseableUnits(ctx.state, ctx.controller,
                                           [&](const GameObject& o) { return o.id != a; });
                });
            victim = hit;
            const bool waiting = ctx.state.chain.resuming.has_value() &&
                (ctx.state.chain.resuming->resume_point == 10 ||
                 ctx.state.chain.resuming->resume_point == 12);
            if (!waiting && killed != kInvalidId && costDueNow(ctx)) {
                ctx.events.logTrace("SHIMMERING SWIMMER: additional cost -> kill " +
                                    ctx.state.getObject(killed).name);
                killWithDeathTrigger(ctx, killed);
                paid = true;
            }
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (!paid || victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        ctx.executor.dealDamage(victim, 2, ctx.source);
        ctx.events.logTrace("SHIMMERING SWIMMER: 2 damage to " + ctx.state.getObject(victim).name);
    }
private:
    std::vector<GameObjectId> sacrificeable(const CardContext& ctx) const {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : ctx.state.objects) {
            if (id == ctx.source || obj.controller != ctx.controller) continue;
            if (!obj.location.has_value() || !(obj.isUnit() || obj.isGear())) continue;
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 993;
        d.def_id = R"RB(rad-014-167)RB";
        d.name = R"RB(Shimmering Swimmer)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-014/167)RB";
        d.collector_number = 14;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Fury};
        d.energy_cost = 3;
        d.might = 3;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(You may kill a friendly unit or gear as an additional cost to play me.
When you play me, if you paid the additional cost, deal 2 to a unit.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/02ef2a5c93cf60048bb932a709d3a5b17b93bb1d-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_993(CardRegistry& r) {
    r.registerCard(993, std::make_unique<ShimmeringSwimmer>());
}

}  // namespace riftbound
