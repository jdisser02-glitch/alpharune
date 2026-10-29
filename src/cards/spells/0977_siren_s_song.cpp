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

class SirensSong : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "Move an enemy unit that moved this turn."
    // The unit is chosen as the spell is played (needsPlayTimeTarget, via
    // pickTarget 6/7/8); where it moves — its controller's base or another
    // battlefield — is chosen as the spell resolves (resume points 20/21).
    // "Moved this turn" reads GameObject::moves_this_turn.
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true, .must_be_enemy = true};
    }
    bool needsPlayTimeTarget() const override { return true; }

    std::vector<GameObjectId> enumerateLegalTargets(const GameState& state,
                                                    PlayerId controller) const override {
        auto out = chooseableUnits(state, controller, [&](const GameObject& o) {
            return o.controller != controller && o.moves_this_turn > 0 &&
                   !o.cant_be_moved_by_enemy && !moveDests(state, o).empty();
        });
        std::sort(out.begin(), out.end());
        return out;
    }
    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        return !enumerateLegalTargets(state, controller).empty();
    }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId u = pickTarget(ctx, "Siren's Song: move an enemy unit that moved this turn",
                                    enumerateLegalTargets(ctx.state, ctx.controller));
        if (u == kInvalidId || !ctx.state.objectExists(u)) return;   // waiting, or no target
        const auto dests = moveDests(ctx.state, ctx.state.getObject(u));
        if (!ctx.state.chain.resuming.has_value()) {   // direct invocation
            if (!dests.empty()) moveTo(ctx, u, dests.front());
            return;
        }
        auto& ri = *ctx.state.chain.resuming;
        if (ri.resume_point == 8) {
            if (dests.empty()) { ri.resume_point = 21; return; }
            std::vector<Intent> opts;
            for (int d : dests) opts.push_back(valChoice(ctx.controller, d, destLabel(d)));
            ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                       "Siren's Song: move it where?");
            ri.resume_point = 20;
            return;
        }
        if (ri.resume_point == 20) {
            ri.resume_point = 21;
            if (dests.empty()) return;
            int d = takenValue(ctx, dests.front());
            if (std::find(dests.begin(), dests.end(), d) == dests.end()) d = dests.front();
            moveTo(ctx, u, d);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 977;
        d.def_id = R"RB(rad-098-167)RB";
        d.name = R"RB(Siren's Song)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-098/167)RB";
        d.collector_number = 98;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Chaos};
        d.energy_cost = 2;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Reaction);
        d.ability_text = R"RB([Reaction] (Play any time, even before spells and abilities resolve.)
Move an enemy unit that moved this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/742967b3e85f51b242fc04b8db4c4e3cd7b88672-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_977(CardRegistry& r) {
    r.registerCard(977, std::make_unique<SirensSong>());
}

}  // namespace riftbound
