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

class SmokeAndMirrors : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }
    // "Choose a unit you control and another unit you control at a different
    //  location. If at least one of them has [Temporary], move each to the
    //  other's location. Draw 1."
    // Both units are friendly: the engine's generic two-target play (one
    // friendly x one enemy unit) never offered this card, so it chooses its
    // own pair as it is played.
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 2, .must_be_unit = true,
                                   .must_be_friendly = true};
    }
    bool needsPlayTimeTargetPair() const override { return true; }
    static std::vector<GameObjectId> mine(const GameState& s, PlayerId me) {
        return chooseableUnits(s, me, [me](const GameObject& o) { return o.controller == me; });
    }
    /// My units at a different location than `first`.
    static std::vector<GameObjectId> elsewhere(const GameState& s, PlayerId me, GameObjectId first) {
        std::vector<GameObjectId> out;
        if (!s.objectExists(first)) return out;
        const auto here = s.getObject(first).location;
        for (auto id : mine(s, me))
            if (id != first && s.getObject(id).location != here) out.push_back(id);
        return out;
    }
    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        for (auto id : mine(state, controller))
            if (!elsewhere(state, controller, id).empty()) return true;
        return false;
    }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& targets) override {
        const PlayerId me = ctx.controller;
        GameObjectId a = kInvalidId, b = kInvalidId;
        bool waiting = false;
        if (targets.size() >= 2) {
            a = targets[0];                        // both chosen already
            b = targets[1];
            if (finalizingChoices(ctx)) return;
        } else if (targets.size() == 1) {
            // Played from facedown: the first unit had to be at the battlefield
            // the card was hidden at (the hidden-play generator hands it in);
            // the second, "at a different location", can be anywhere else
            // (CR 811.1.d.4 uses this very card as its example).
            a = targets[0];
            b = pickTarget(ctx, "Smoke and Mirrors: another unit you control at a different location",
                           elsewhere(ctx.state, me, a));
            if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
            waiting = b == kInvalidId && ctx.state.chain.resuming.has_value() &&
                      ctx.state.chain.resuming->resume_point == 7;
        } else {
            auto picked = pickTargetPair(ctx, "Smoke and Mirrors", mine(ctx.state, me),
                                         [&](GameObjectId first) { return elsewhere(ctx.state, me, first); });
            a = picked.first;
            b = picked.second;
            if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
            waiting = (a == kInvalidId || b == kInvalidId) && ctx.state.chain.resuming.has_value() &&
                      (ctx.state.chain.resuming->resume_point == 10 ||
                       ctx.state.chain.resuming->resume_point == 12);
        }
        if (waiting) return;                       // the agent is still choosing
        if (a == kInvalidId || b == kInvalidId) return;   // no pair could be chosen
        const bool has_a = a != kInvalidId && ctx.state.objectExists(a) &&
                           ctx.state.getObject(a).location.has_value();
        const bool has_b = b != kInvalidId && ctx.state.objectExists(b) &&
                           ctx.state.getObject(b).location.has_value();
        if (!has_a && !has_b) return;             // every target gone: the spell does nothing
        if (has_a && has_b) {
            const auto& ua = ctx.state.getObject(a);
            const auto& ub = ctx.state.getObject(b);
            const auto la = ua.location;
            const auto lb = ub.location;
            const bool has_temp = ua.hasKeyword(Keyword::Temporary) || ub.hasKeyword(Keyword::Temporary);
            if (has_temp && la != lb) {
                moveToLocation(ctx.executor, a, *lb);
                moveToLocation(ctx.executor, b, *la);
                ctx.events.logTrace("SMOKE AND MIRRORS: swapped two friendly units (Temporary gate met)");
            } else {
                ctx.events.logTrace("SMOKE AND MIRRORS: no swap (Temporary gate not met / same loc)");
            }
        }
        ctx.executor.drawCards(me, 1);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 645;
        d.def_id = R"RB(unl-083-219)RB";
        d.name = R"RB(Smoke and Mirrors)RB";
        d.set_code = R"RB(UNL)RB";
        d.set_name = R"RB(Unleashed)RB";
        d.public_code = R"RB(UNL-083/219)RB";
        d.collector_number = 83;
        d.artist = R"RB(Wild Blue Studios)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Mind};
        d.energy_cost = 2;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Action);
        d.keywords.set(Keyword::Hidden);
        d.ability_text = R"RB([Hidden] (Hide now for [A] to react with later for .)
[Action] (Play on your turn or in showdowns.)
Choose a unit you control and another unit you control at a different location. If at least one of them has [Temporary], move each to the other's location. Draw 1.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/38886634ee8646707d9c26020f977f14c934a4c0-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_645(CardRegistry& r) {
    r.registerCard(645, std::make_unique<SmokeAndMirrors>());
}

} // namespace riftbound
