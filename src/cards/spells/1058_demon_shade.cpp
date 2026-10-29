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

class DemonShade : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // [Hidden] is engine-handled (keyword).
    // "Choose a friendly unit, then choose one —
    //  • Move it to an open battlefield.
    //  • Give it [Ganking] this turn if there are no open battlefields."
    // An open battlefield is one no player controls. One pickMode covers the
    // mode and the destination: option 0 is the [Ganking] mode, option 1 + i
    // moves the unit to battlefield i (offered only while battlefield i is
    // open and a friendly unit is elsewhere). The unit is picked after the
    // mode (the helpers' resume slots compose in that order), among the
    // friendly units the chosen mode can act on. All choices are made at
    // finalization. The [Ganking] mode can always be chosen; its condition is
    // checked as the spell resolves. A move whose destination is no longer
    // open at resolution does nothing.
    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == controller && obj.location.has_value()) return true;
        return false;
    }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto& bfs = ctx.state.battlefields;
        const int n = 1 + static_cast<int>(std::min<size_t>(bfs.size(), 31));
        uint32_t legal = friendlyUnits(ctx, std::nullopt).empty() ? 0u : 1u;
        std::vector<std::string> labels = {"Give it [Ganking] this turn (if no open battlefield)"};
        for (int i = 1; i < n; ++i) {
            const auto& bf = bfs[i - 1];
            labels.push_back("Move it to open BF#" + std::to_string(static_cast<int>(bf.id)));
            if (!bf.controller.has_value() && !friendlyUnits(ctx, bf.id).empty()) legal |= (1u << i);
        }
        const int mode = pickMode(ctx, "Demon Shade", n, labels, legal);
        GameObjectId unit = kInvalidId;
        if (mode >= 0 && mode < n) {
            std::optional<BattlefieldId> dest;
            if (mode > 0) dest = bfs[mode - 1].id;
            unit = pickTarget(ctx, "Demon Shade: friendly unit", friendlyUnits(ctx, dest));
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 355)
        if (mode < 0 || mode >= n) return;
        if (unit == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the unit
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        if (!ctx.state.getObject(unit).location.has_value()) return;
        if (mode == 0) {
            for (const auto& bf : ctx.state.battlefields)
                if (!bf.controller.has_value()) {
                    ctx.events.logTrace("DEMON SHADE: an open battlefield exists -> no [Ganking]");
                    return;
                }
            ctx.executor.giveTemporaryKeyword(unit, Keyword::Ganking, 0);
            ctx.events.logTrace("DEMON SHADE: " + ctx.state.getObject(unit).name +
                                " gets [Ganking] this turn");
            return;
        }
        const auto& dest = ctx.state.battlefields[mode - 1];
        if (dest.controller.has_value()) {
            ctx.events.logTrace("DEMON SHADE: BF#" + std::to_string(static_cast<int>(dest.id)) +
                                " is no longer open -> no move");
            return;
        }
        ctx.executor.moveToBattlefield(unit, dest.id);
        ctx.events.logTrace("DEMON SHADE: moved " + ctx.state.getObject(unit).name +
                            " to open BF#" + std::to_string(static_cast<int>(dest.id)));
    }
private:
    /// Friendly units on the board, excluding any already at `not_at`.
    static std::vector<GameObjectId> friendlyUnits(const CardContext& ctx,
                                                   std::optional<BattlefieldId> not_at) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : ctx.state.objects) {
            if (!obj.isUnit() || obj.controller != ctx.controller || !obj.location.has_value()) continue;
            if (not_at.has_value() && obj.battlefieldId() == not_at) continue;
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1058;
        d.def_id = R"RB(rad-102-167)RB";
        d.name = R"RB(Demon Shade)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-102/167)RB";
        d.collector_number = 102;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Fury};
        d.energy_cost = 3;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Hidden);
        d.ability_text = R"RB([Hidden] (Hide now for [A] to react with later for [0].)
Choose a friendly unit, then choose one —
Move it to an open battlefield.Give it [Ganking] this turn if there are no open battlefields. (It can move from battlefield to battlefield.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/e9dd313886624aafc7e0c7f9e1689225c042b484-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1058(CardRegistry& r) {
    r.registerCard(1058, std::make_unique<DemonShade>());
}

}  // namespace riftbound
