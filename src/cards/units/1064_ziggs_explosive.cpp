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

class ZiggsExplosive : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger | kFinalizeActivate; }

    // "[1], [E]: Play a Bomb gear token to a battlefield. (It has [Deploy].
    //  When it dies, deal 2 to an enemy there and kill your other Bombs
    //  there.)"
    // The battlefield is chosen as the ability is finalized (pickBombBattlefield,
    // radiance_helpers.h); the Bomb token class is gear/1199_bomb.cpp.
    bool hasActivatedAbility() const override { return true; }
    ActivationCost getActivationCost() const override {
        return ActivationCost{.exhaust = true, .energy = 1};
    }
    void onActivate(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const int bf = pickBombBattlefield(*this, ctx, "Ziggs, Explosive: Bomb to which battlefield");
        if (finalizingChoices(ctx)) return;   // choices end here (CR 355)
        if (bf < 0) return;
        playBombToken(ctx, ctx.controller, static_cast<BattlefieldId>(bf));
    }

    // "When I defend, you may kill a friendly gear to give me +2 [M] this
    //  turn." Killing the gear is the cost of the "you may": the gear is
    //  chosen and killed as the ability is finalized (killWithDeathTrigger, so
    //  a Bomb's or other gear's death ability triggers), and Ziggs gets +2 [M]
    //  this turn as it resolves.
    TriggerType triggerType() const override { return TriggerType::WhenIDefend; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto gear = friendlyGear(ctx);
        if (!costAlreadyPaid(ctx) && gear.empty()) { if (finalizingChoices(ctx)) return; return; }
        int go = confirmOptional(ctx, "Ziggs, Explosive: kill a friendly gear for +2 [M]?",
                                 [&]() { return costAlreadyPaid(ctx) || !friendlyGear(ctx).empty(); });
        GameObjectId victim = kInvalidId;
        if (go == 1) victim = pickTarget(ctx, "Ziggs, Explosive: gear to kill", gear);
        bool paid = false;
        if (go == 1 && victim != kInvalidId && costDueNow(ctx) && ctx.state.objectExists(victim) &&
            ctx.state.getObject(victim).location.has_value()) {
            ctx.events.logTrace("ZIGGS, EXPLOSIVE: kills " + ctx.state.getObject(victim).name +
                                " (cost)");
            killWithDeathTrigger(ctx, victim);
            paid = true;
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (go != 1) return;
        if (!paid && !costAlreadyPaid(ctx)) return;   // the gear was not killed
        if (!ctx.state.objectExists(ctx.source) || !ctx.state.getObject(ctx.source).location.has_value())
            return;
        ctx.executor.giveTemporaryMight(ctx.source, 2);
        ctx.events.logTrace("ZIGGS, EXPLOSIVE: +2 [M] this turn");
    }
private:
    static std::vector<GameObjectId> friendlyGear(const CardContext& ctx) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : ctx.state.objects)
            if (obj.isGear() && obj.controller == ctx.controller && obj.location.has_value())
                out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1064;
        d.def_id = R"RB(rad-112-167)RB";
        d.name = R"RB(Ziggs, Explosive)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-112/167)RB";
        d.collector_number = 112;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Bandle City)RB", R"RB(Ziggs)RB"};
        d.energy_cost = 4;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([1], [E]: Play a Bomb gear token to a battlefield. (It has [Deploy]. When it dies, deal 2 to an enemy there and kill your other Bombs there.)
When I defend, you may kill a friendly gear to give me +2 [M] this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/f0c1f1f3b69b31a69f9637ece841b5ee1029cdd3-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1064(CardRegistry& r) {
    r.registerCard(1064, std::make_unique<ZiggsExplosive>());
}

}  // namespace riftbound
