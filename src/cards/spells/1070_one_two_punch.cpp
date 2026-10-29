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

class OneTwoPunch : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "[Buff] a friendly unit and [Stun] an enemy unit."
    // Two targets chosen as the spell is played (pickTargetPair): A = a
    // friendly unit, B = an enemy unit (anywhere). [Buff] gives a +1 [M] buff
    // only if it has none (CR 427.1.b). Playable with a friendly unit to
    // choose; with no enemy unit the stun part is skipped.
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 2, .must_be_unit = true};
    }
    bool needsPlayTimeTargetPair() const override { return true; }
    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.location.has_value() && obj.controller == controller)
                return true;
        return false;
    }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto friendly = chooseableUnits(ctx.state, ctx.controller,
            [&](const GameObject& o) { return o.controller == ctx.controller; });
        auto enemy_fn = [&](GameObjectId /*picked_a*/) {
            return chooseableUnits(ctx.state, ctx.controller,
                [&](const GameObject& o) { return o.controller != ctx.controller; });
        };
        auto [ally, enemy] = pickTargetPair(ctx, "One-Two Punch", friendly, enemy_fn);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if ((ally == kInvalidId || enemy == kInvalidId) && ctx.state.chain.resuming.has_value() &&
            (ctx.state.chain.resuming->resume_point == 10 ||
             ctx.state.chain.resuming->resume_point == 12))
            return;                           // waiting for the agent
        if (ally != kInvalidId && ctx.state.objectExists(ally) &&
            ctx.state.getObject(ally).location.has_value() &&
            ctx.state.getObject(ally).buff_count == 0)
            ctx.executor.buffUnit(ally);
        if (enemy != kInvalidId && ctx.state.objectExists(enemy) &&
            ctx.state.getObject(enemy).location.has_value())
            ctx.executor.stunUnitBy(enemy, ctx.source);
        ctx.events.logTrace("ONE-TWO PUNCH: buff + stun");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1070;
        d.def_id = R"RB(rad-121-167)RB";
        d.name = R"RB(One-Two Punch)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-121/167)RB";
        d.collector_number = 121;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.energy_cost = 3;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
[Buff] a friendly unit and [Stun] an enemy unit. (Give the friendly unit a +1 [M] buff if it doesn't have one. The enemy unit doesn't deal combat damage this turn.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/1f246286c658d44dc4e7d51c158b2f822da39825-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1070(CardRegistry& r) {
    r.registerCard(1070, std::make_unique<OneTwoPunch>());
}

}  // namespace riftbound
