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

class PrideOfNazumah : public LegendCard {
public:
    const CardDef& def() const override { return def_; }

    // "[1], [E]: Give a friendly unit "I have [Assault] equal to my [Shield]"
    //  this turn."
    // The unit is chosen as the ability is put on the chain (activation-time
    // target). Resolution marks it for this turn (card_counters, turn + 1, plus
    // the granting player); the grant is a continuous effect, so it is applied
    // in every aura pass by this legend: the unit gets [Assault X] where X is
    // its current [Shield] (printed, "this turn" and aura-granted instances).
    // APPROXIMATION: Shield granted by attached Equipment is folded in by the
    // engine after the legends' aura pass, so it is not counted.
    static constexpr const char* kTurnKey = "__pride_assault_eq_shield_turn";
    static constexpr const char* kByKey = "__pride_assault_eq_shield_by";

    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility ab;
        ab.cost.exhaust = true;
        ab.cost.energy = 1;
        ab.targets = TargetRequirements{.count = 1, .must_be_unit = true, .must_be_friendly = true};
        ab.needs_activation_time_target = true;
        return {ab};
    }
    std::vector<GameObjectId> enumerateLegalTargets(const GameState& state, PlayerId controller,
                                                    int /*ability_index*/) const override {
        return friendlyUnits(state, controller);
    }
    void onActivate(CardContext& ctx, int /*ability_index*/,
                    const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId t = pickTarget(ctx, "Pride of Nazumah: Assault equal to Shield",
                                          friendlyUnits(ctx.state, ctx.controller));
        if (t == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;
        auto& u = ctx.state.getObject(t);
        u.card_counters[kTurnKey] = ctx.state.turn.turn_number + 1;
        u.card_counters[kByKey] = playerIndex(ctx.controller) + 1;
        ctx.events.logTrace("PRIDE OF NAZUMAH: " + u.name +
                            " has [Assault] equal to its [Shield] this turn");
    }

    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        auto& ps = state.player(controller);
        if (ps.legend_zone == kInvalidId || !state.objectExists(ps.legend_zone)) return;
        if (state.getObject(ps.legend_zone).card_def_id != cardDefId()) return;
        const GameObjectId src = ps.legend_zone;
        const int turn_tag = state.turn.turn_number + 1;
        const int by = playerIndex(controller) + 1;
        for (auto& [id, u] : state.objects) {
            if (!u.isUnit() || !u.location.has_value()) continue;
            auto t = u.card_counters.find(kTurnKey);
            if (t == u.card_counters.end() || t->second != turn_tag) continue;
            auto b = u.card_counters.find(kByKey);
            if (b == u.card_counters.end() || b->second != by) continue;
            const int shield = shieldOf(u);
            if (shield > 0) auraKeyword(u, src, Keyword::Assault, shield);
        }
    }
private:
    static std::vector<GameObjectId> friendlyUnits(const GameState& state, PlayerId me) {
        return chooseableUnits(state, me, [&](const GameObject& o) { return o.controller == me; });
    }
    /// Total [Shield]: own value (printed + "this turn") plus each aura-granted
    /// instance (one without a value is [Shield 1]).
    static int shieldOf(const GameObject& u) {
        int v = std::max(0, u.shield_value);
        for (const auto& ae : u.aura_effects)
            if (ae.keyword == Keyword::Shield) v += std::max(1, ae.keyword_value);
        if (v == 0 && u.hasKeyword(Keyword::Shield)) v = 1;
        return v;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1091;
        d.def_id = R"RB(rad-147-167)RB";
        d.name = R"RB(Pride of Nazumah)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-147/167)RB";
        d.collector_number = 147;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Calm, Domain::Body};
        d.tags = {R"RB(K'Sante)RB"};
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([1], [E]: Give a friendly unit "I have [Assault] equal to my [Shield]" this turn. (+1 [M] for each instance of Assault while it's an attacker.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/190a27bf844fa21105824efc19de4e71c3137df6-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1091(CardRegistry& r) {
    r.registerCard(1091, std::make_unique<PrideOfNazumah>());
}

}  // namespace riftbound
