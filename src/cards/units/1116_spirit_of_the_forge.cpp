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

// RECONSTRUCTED TEXT (not in cards/card_index.json; confidence: medium).
// Gathered from web-search summaries (WebFetch to card sites was blocked);
// ability_text written in the repo's symbol convention with standard reminder
// text. Stats agree across sources; the ability wording is reconstructed from
// paraphrases (optional additional cost: return a friendly gear; if paid, I
// enter ready and gain Might equal to the returned gear's Energy cost this
// turn) — exact phrasing unverified. Modeled on Skystrike Squadron (RAD-075)
// wording. Also an Organized Play promo. No image_url / artist.
class SpiritOfTheForge : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "You may return a friendly gear to its owner's hand as an additional
    //  cost to play me. If you do, I enter ready and get +[M] equal to that
    //  gear's Energy cost this turn."
    // APPROXIMATION (engine gap): OptionalAdditionalCost models only Energy /
    // Power / discard, not returning a permanent to hand. As with Skystrike
    // Squadron, the choice is settled by my play ability, which is finalized
    // as I enter the board (no response window in between): you may return a
    // friendly gear to its owner's hand (paid at finalization); if you did,
    // I'm readied and get +[M] equal to the gear's printed Energy cost this
    // turn as the ability resolves. "Enter ready" is modeled as readying me,
    // so effects that watch a unit becoming ready see it.
    static constexpr int kCostSlot = 8;   // resume_data slot: 1 + the returned gear's Energy cost

    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const bool already_paid = costAlreadyPaid(ctx);
        int go = confirmOptional(ctx, "Spirit of the Forge: return a friendly gear to hand?",
                                 [&]() { return already_paid || !friendlyGear(ctx.state, ctx.controller).empty(); });
        GameObjectId gear = kInvalidId;
        if (go == 1 && !already_paid)
            gear = pickTarget(ctx, "Spirit of the Forge: gear to return",
                              friendlyGear(ctx.state, ctx.controller));
        bool paid_now = false;
        if (go == 1 && !already_paid && gear != kInvalidId && ctx.state.objectExists(gear) &&
            ctx.state.getObject(gear).location.has_value() && costDueNow(ctx)) {
            const int cost = energyCostOf(ctx, gear);
            if (ctx.state.chain.resuming.has_value()) {
                auto& rd = ctx.state.chain.resuming->resume_data;
                while (static_cast<int>(rd.size()) <= kCostSlot) rd.push_back(0);
                rd[kCostSlot] = 1 + cost;
            }
            returnGear(ctx, gear);
            paid_now = true;
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (!paid_now && !already_paid) return;   // declined / waiting / not paid
        int bonus = 0;
        if (ctx.state.chain.resuming.has_value()) {
            const auto& rd = ctx.state.chain.resuming->resume_data;
            if (static_cast<int>(rd.size()) > kCostSlot && rd[kCostSlot] > 0) bonus = rd[kCostSlot] - 1;
        }
        if (!ctx.state.objectExists(ctx.source) || !ctx.state.getObject(ctx.source).location.has_value())
            return;
        ctx.executor.readyObject(ctx.source);
        if (bonus > 0) ctx.executor.giveTemporaryMight(ctx.source, bonus);
        ctx.events.logTrace("SPIRIT OF THE FORGE: returned a gear -> ready, +" +
                            std::to_string(bonus) + " [M] this turn");
    }
private:
    static std::vector<GameObjectId> friendlyGear(const GameState& state, PlayerId player) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isGear() && obj.controller == player && obj.location.has_value())
                out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }
    /// The gear's printed Energy cost (0 for a token without a card).
    static int energyCostOf(const CardContext& ctx, GameObjectId gear) {
        const auto& g = ctx.state.getObject(gear);
        if (g.card_def_id == kInvalidId || !ctx.registry) return 0;
        const Card* c = ctx.registry->get(g.card_def_id);
        return c ? c->def().energy_cost : 0;
    }
    static void returnGear(CardContext& ctx, GameObjectId gear) {
        auto& g = ctx.state.getObject(gear);
        const std::string name = g.name;
        if (g.attached_to.has_value()) ctx.executor.unattachGear(gear);
        ctx.executor.bounceToHand(gear);
        ctx.events.logTrace("SPIRIT OF THE FORGE: returned " + name + " to its owner's hand");
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1116;
        d.def_id = R"RB(rad-066-167)RB";
        d.name = R"RB(Spirit of the Forge)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-066/167)RB";
        d.collector_number = 66;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Freljord)RB", R"RB(Spirit)RB"};
        d.energy_cost = 6;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(You may return a friendly gear to its owner's hand as an additional cost to play me. If you do, I enter ready and get +[M] equal to that gear's Energy cost this turn.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1116(CardRegistry& r) {
    r.registerCard(1116, std::make_unique<SpiritOfTheForge>());
}

}  // namespace riftbound
