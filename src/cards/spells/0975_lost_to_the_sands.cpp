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

class LostToTheSands : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "Choose one — Deal 4 to a unit at a battlefield. / Kill a gear."
    // Mode then target (pickMode 3/4/5, then pickTarget 6/7/8), both chosen
    // as the spell is finalized.
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    static std::vector<GameObjectId> unitsAtBattlefields(const GameState& state, PlayerId p) {
        auto out = chooseableUnits(state, p, [](const GameObject& o) { return o.isAtBattlefield(); });
        std::sort(out.begin(), out.end());
        return out;
    }
    static std::vector<GameObjectId> gearOnBoard(const GameState& state, PlayerId p) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects) {
            if (!obj.isGear() || !obj.location.has_value()) continue;
            if (obj.controller != p && obj.untargetable_by_enemy) continue;
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        return !unitsAtBattlefields(state, controller).empty() ||
               !gearOnBoard(state, controller).empty();
    }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto units = unitsAtBattlefields(ctx.state, ctx.controller);
        const auto gear = gearOnBoard(ctx.state, ctx.controller);
        uint32_t legal = 0;
        if (!units.empty()) legal |= 1u;
        if (!gear.empty()) legal |= 2u;
        int mode = pickMode(ctx, "Lost to the Sands", 2,
                            {"Deal 4 to a unit at a battlefield", "Kill a gear"}, legal);
        if (mode == -1) return;   // waiting for the agent
        GameObjectId t = kInvalidId;
        if (mode == 0 || mode == 1) {
            t = pickTarget(ctx, mode == 0 ? "Lost to the Sands: deal 4" : "Lost to the Sands: kill a gear",
                           mode == 0 ? units : gear);
            if (t == kInvalidId && ctx.state.chain.resuming.has_value() &&
                ctx.state.chain.resuming->resume_point == 7)
                return;   // waiting for the agent
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 355)

        if (t == kInvalidId || !ctx.state.objectExists(t)) return;
        if (mode == 0) {
            ctx.executor.dealDamage(t, 4, ctx.source);
            if (ctx.state.objectExists(t) && ctx.state.getObject(t).hasLethalDamage())
                ctx.executor.killObject(t);
        } else {
            ctx.executor.killObject(t);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 975;
        d.def_id = R"RB(rad-013-167)RB";
        d.name = R"RB(Lost to the Sands)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-013/167)RB";
        d.collector_number = 13;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Fury};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
Choose one —
Deal 4 to a unit at a battlefield.Kill a gear.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/92071a0f4d495f5f7acdf78d060133b98d1cebae-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_975(CardRegistry& r) {
    r.registerCard(975, std::make_unique<LostToTheSands>());
}

}  // namespace riftbound
