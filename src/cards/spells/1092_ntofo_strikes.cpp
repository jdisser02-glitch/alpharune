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

class NtofoStrikes : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "Give a friendly unit [Shield 2] this turn, then deal damage equal to its
    //  [Shield] to an enemy unit."
    // Both units are chosen at finalization (friendly first, then enemy). The
    // damage is the friendly unit's total [Shield] after the +2 (printed,
    // "this turn" and aura-granted instances).
    bool needsPlayTimeTargetPair() const override { return true; }
    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        return !friends(state, controller).empty();
    }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto [ally, enemy] = pickTargetPair(
            ctx, "Ntofo Strikes: a friendly unit, then an enemy unit",
            friends(ctx.state, ctx.controller),
            [&](GameObjectId) { return enemies(ctx.state, ctx.controller); });
        const bool waiting = ctx.state.chain.resuming.has_value() &&
                             (ctx.state.chain.resuming->resume_point == 10 ||
                              ctx.state.chain.resuming->resume_point == 12);
        if (waiting) return;
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (ally == kInvalidId || !ctx.state.objectExists(ally) ||
            !ctx.state.getObject(ally).location.has_value())
            return;   // "its [Shield]" needs the friendly unit
        ctx.executor.giveTemporaryKeyword(ally, Keyword::Shield, 2);
        const int dmg = shieldOf(ctx.state.getObject(ally));
        if (enemy == kInvalidId || !ctx.state.objectExists(enemy) ||
            !ctx.state.getObject(enemy).location.has_value())
            return;
        ctx.executor.dealDamage(enemy, dmg, ctx.source);
        ctx.events.logTrace("NTOFO STRIKES: " + std::to_string(dmg) + " to " +
                            ctx.state.getObject(enemy).name);
        if (ctx.state.objectExists(enemy) && ctx.state.getObject(enemy).location.has_value() &&
            ctx.state.getObject(enemy).hasLethalDamage())
            ctx.executor.killObject(enemy);
    }
private:
    static std::vector<GameObjectId> enemies(const GameState& state, PlayerId me) {
        return chooseableUnits(state, me, [&](const GameObject& o) { return o.controller != me; });
    }
    static std::vector<GameObjectId> friends(const GameState& state, PlayerId me) {
        return chooseableUnits(state, me, [&](const GameObject& o) { return o.controller == me; });
    }
    static int shieldOf(const GameObject& u) {
        int v = std::max(0, u.shield_value);
        for (const auto& ae : u.aura_effects)
            if (ae.keyword == Keyword::Shield) v += std::max(1, ae.keyword_value);
        if (v == 0 && u.hasKeyword(Keyword::Shield)) v = 1;
        return v;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1092;
        d.def_id = R"RB(rad-148-167)RB";
        d.name = R"RB(Ntofo Strikes)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-148/167)RB";
        d.collector_number = 148;
        d.artist = R"RB(Polar Engine Studio)RB";
        d.card_type = CardType::Spell;
        d.super_type = SuperType::Signature;
        d.domains = {Domain::Calm, Domain::Body};
        d.tags = {R"RB(K'Sante)RB"};
        d.energy_cost = 2;
        d.power_cost = 1;
        d.rarity = Rarity::Epic;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
Give a friendly unit [Shield 2] this turn, then deal damage equal to its [Shield] to an enemy unit. (A unit with Shield has +1 [M] for each instance of Shield while it's a defender.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/ab5aa7d29bd49efe2c9da3b62a8065295c189991-744x1040.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1092(CardRegistry& r) {
    r.registerCard(1092, std::make_unique<NtofoStrikes>());
}

}  // namespace riftbound
