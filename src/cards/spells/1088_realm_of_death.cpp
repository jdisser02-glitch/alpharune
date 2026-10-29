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

class RealmOfDeath : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "Give an enemy unit -2 [M] this turn and a friendly unit +2 [M] this
    //  turn. They deal damage equal to their Mights to each other."
    // Both units are chosen at finalization (enemy first, then friendly). The
    // Might changes apply first, then each deals damage equal to its Might to
    // the other simultaneously (Mights read before any damage is dealt).
    bool needsPlayTimeTargetPair() const override { return true; }
    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        return !enemies(state, controller).empty() && !friends(state, controller).empty();
    }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto [enemy, ally] = pickTargetPair(
            ctx, "Realm of Death: an enemy unit, then a friendly unit",
            enemies(ctx.state, ctx.controller),
            [&](GameObjectId) { return friends(ctx.state, ctx.controller); });
        const bool waiting = ctx.state.chain.resuming.has_value() &&
                             (ctx.state.chain.resuming->resume_point == 10 ||
                              ctx.state.chain.resuming->resume_point == 12);
        if (waiting) return;
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)

        const bool e_ok = enemy != kInvalidId && ctx.state.objectExists(enemy) &&
                          ctx.state.getObject(enemy).location.has_value();
        const bool a_ok = ally != kInvalidId && ctx.state.objectExists(ally) &&
                          ctx.state.getObject(ally).location.has_value();
        if (e_ok) ctx.executor.giveTemporaryMight(enemy, -2);
        if (a_ok) ctx.executor.giveTemporaryMight(ally, 2);
        // "They deal damage ... to each other" needs both of them.
        if (!e_ok || !a_ok) return;
        const int enemy_might = ctx.state.getObject(enemy).current_might;
        const int ally_might = ctx.state.getObject(ally).current_might;
        ctx.events.logTrace("REALM OF DEATH: " + ctx.state.getObject(ally).name + " (" +
                            std::to_string(ally_might) + "M) <-> " +
                            ctx.state.getObject(enemy).name + " (" +
                            std::to_string(enemy_might) + "M)");
        ctx.executor.dealDamage(enemy, ally_might, ctx.source);
        ctx.executor.dealDamage(ally, enemy_might, ctx.source);
        for (auto id : {enemy, ally})
            if (ctx.state.objectExists(id) && ctx.state.getObject(id).location.has_value() &&
                ctx.state.getObject(id).hasLethalDamage())
                ctx.executor.killObject(id);
    }
private:
    static std::vector<GameObjectId> enemies(const GameState& state, PlayerId me) {
        return chooseableUnits(state, me, [&](const GameObject& o) { return o.controller != me; });
    }
    static std::vector<GameObjectId> friends(const GameState& state, PlayerId me) {
        return chooseableUnits(state, me, [&](const GameObject& o) { return o.controller == me; });
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1088;
        d.def_id = R"RB(rad-144-167)RB";
        d.name = R"RB(Realm of Death)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-144/167)RB";
        d.collector_number = 144;
        d.card_type = CardType::Spell;
        d.super_type = SuperType::Signature;
        d.domains = {Domain::Fury, Domain::Order};
        d.tags = {R"RB(Mordekaiser)RB"};
        d.energy_cost = 2;
        d.power_cost = 1;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(Give an enemy unit -2 [M] this turn and a friendly unit +2 [M] this turn. They deal damage equal to their Mights to each other.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/fd3b4c6939b6a80b33956da7e3a9102eba9c32a8-744x1040.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1088(CardRegistry& r) {
    r.registerCard(1088, std::make_unique<RealmOfDeath>());
}

}  // namespace riftbound
