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

class MordekaiserDominating : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play me or when I attack, you may discard 1 to kill a unit or
    //  gear." The "you may", the unit or gear to kill and the card discarded
    //  (the cost) are all decided as the ability is finalized (CR 383.3).
    // Resume points: 0-2 confirmOptional, 6-8 pickTarget, then kAwaitDiscard
    // (the discard prompt) and kPaid.
    static constexpr int kAwaitDiscard = 20;
    static constexpr int kPaid = 21;

    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayMe, TriggerType::WhenIAttack};
    }
    static std::vector<GameObjectId> killable(const CardContext& ctx) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : ctx.state.objects) {
            if (!obj.isUnit() && !obj.isGear()) continue;
            if (!obj.location.has_value()) continue;
            if (obj.controller != ctx.controller && obj.untargetable_by_enemy) continue;
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto& ps = ctx.state.player(ctx.controller);
        auto can_do = [&]() {
            return (costAlreadyPaid(ctx) || !ps.hand.empty()) && !killable(ctx).empty();
        };
        int go = confirmOptional(ctx, "Mordekaiser, Dominating: discard 1 to kill a unit or gear?",
                                 can_do);
        if (go == -1) return;                 // waiting for the agent
        GameObjectId victim = kInvalidId;
        if (go == 1) {
            victim = pickTarget(ctx, "Mordekaiser, Dominating: kill", killable(ctx));
            if (ctx.state.chain.resuming.has_value() &&
                ctx.state.chain.resuming->resume_point == 7)
                return;                       // waiting for the target
        }
        if (go == 1 && victim != kInvalidId && costDueNow(ctx)) {
            // The cost: discard 1 (the agent picks the card).
            if (!ctx.state.chain.resuming.has_value()) {
                if (ps.hand.empty()) return;
                ctx.executor.applyDiscard(ctx.controller, ps.hand.front());
            } else {
                auto& ri = *ctx.state.chain.resuming;
                if (ri.resume_point < kAwaitDiscard) {
                    if (ps.hand.empty()) return;  // can't pay: nothing is killed
                    std::vector<Intent> options;
                    for (auto cid : ps.hand) options.push_back(objChoice(ctx.controller, cid));
                    ctx.executor.requestChoice(ctx.controller, std::move(options),
                                               "Mordekaiser, Dominating: discard 1");
                    ri.resume_point = kAwaitDiscard;
                    return;
                }
                if (ri.resume_point == kAwaitDiscard) {
                    GameObjectId card = takenObject(ctx);
                    if (std::find(ps.hand.begin(), ps.hand.end(), card) == ps.hand.end())
                        card = ps.hand.empty() ? kInvalidId : ps.hand.front();
                    if (card == kInvalidId) return;
                    ctx.executor.applyDiscard(ctx.controller, card);
                    ri.resume_point = kPaid;
                }
            }
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (go != 1 || victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        if (!ctx.state.getObject(victim).location.has_value()) return;
        const std::string vname = ctx.state.getObject(victim).name;
        ctx.executor.killObject(victim);
        ctx.events.logTrace("MORDEKAISER, DOMINATING: discarded 1 -> killed " + vname);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1081;
        d.def_id = R"RB(rad-137-167)RB";
        d.name = R"RB(Mordekaiser, Dominating)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-137/167)RB";
        d.collector_number = 137;
        d.artist = R"RB(JunHuan)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Order};
        d.tags = {R"RB(Noxus)RB", R"RB(Mordekaiser)RB"};
        d.energy_cost = 9;
        d.power_cost = 2;
        d.might = 8;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(When you play me or when I attack, you may discard 1 to kill a unit or gear.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/43068166258356908b364566abcfc57123e96caa-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1081(CardRegistry& r) {
    r.registerCard(1081, std::make_unique<MordekaiserDominating>());
}

}  // namespace riftbound
