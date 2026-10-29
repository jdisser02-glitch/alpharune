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

class ImprovisedProjectile : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "[Action] Choose a friendly gear. Deal damage equal to its Energy cost
    //  to a unit at a battlefield." Both are chosen as the spell is played
    //  (pickTargetPair at finalization): the gear first, then the unit. The
    //  amount is the gear's printed Energy cost (a token's is 0) as the spell
    //  resolves. If the gear is no longer a legal choice by then (it left the
    //  board or changed control) the amount can't be determined and no damage
    //  is dealt.
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 2};
    }
    bool needsPlayTimeTargetPair() const override { return true; }

    static std::vector<GameObjectId> friendlyGear(const GameState& state, PlayerId p) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isGear() && obj.controller == p && obj.location.has_value()) out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }
    static std::vector<GameObjectId> unitsAtBattlefields(const GameState& state, PlayerId p) {
        auto out = chooseableUnits(state, p, [](const GameObject& o) { return o.isAtBattlefield(); });
        std::sort(out.begin(), out.end());
        return out;
    }

    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        return !friendlyGear(state, controller).empty() &&
               !unitsAtBattlefields(state, controller).empty();
    }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto [gear, unit] = pickTargetPair(
            ctx, "Improvised Projectile",
            friendlyGear(ctx.state, ctx.controller),
            [&](GameObjectId /*picked_gear*/) { return unitsAtBattlefields(ctx.state, ctx.controller); });
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        const bool suspending = (gear == kInvalidId || unit == kInvalidId) &&
                                ctx.state.chain.resuming.has_value() &&
                                (ctx.state.chain.resuming->resume_point == 10 ||
                                 ctx.state.chain.resuming->resume_point == 12);
        if (suspending) return;
        if (gear == kInvalidId || unit == kInvalidId) return;
        if (!ctx.state.objectExists(gear) || !ctx.state.objectExists(unit)) return;
        const auto& g = ctx.state.getObject(gear);
        const int amount = g.card_def_id != kInvalidId
            ? ctx.executor.cardDB().get(g.card_def_id).energy_cost : 0;
        ctx.events.logTrace("IMPROVISED PROJECTILE: " + g.name + " costs [" +
                            std::to_string(amount) + "] -> deal " + std::to_string(amount) +
                            " to " + ctx.state.getObject(unit).name);
        if (amount <= 0) return;
        ctx.executor.dealDamage(unit, amount, ctx.source);
        if (ctx.state.objectExists(unit) && ctx.state.getObject(unit).hasLethalDamage())
            ctx.executor.killObject(unit);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1022;
        d.def_id = R"RB(rad-056-167)RB";
        d.name = R"RB(Improvised Projectile)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-056/167)RB";
        d.collector_number = 56;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Mind};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
Choose a friendly gear. Deal damage equal to its Energy cost to a unit at a battlefield.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/ecbb970f955b8e12369921f30ad08cab04e59a3f-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1022(CardRegistry& r) {
    r.registerCard(1022, std::make_unique<ImprovisedProjectile>());
}

}  // namespace riftbound
