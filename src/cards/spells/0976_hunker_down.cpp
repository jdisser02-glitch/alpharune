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

class HunkerDown : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "Ready a friendly unit. It can't move this turn. Draw 1."
    // The unit is chosen as the spell is played (needsPlayTimeTarget). If it
    // is no longer a legal target at resolution that instruction is ignored
    // and the draw still happens.
    //
    // DEVIATION: "It can't move this turn" is not enforced — the engine has
    // no per-unit "can't move this turn" restriction (only cant_move_to_base,
    // which is permanent and covers one direction). The card records the
    // restriction on the unit as card_counters[kCantMoveKey] = turn number + 1
    // so an engine-side check can honour it later; nothing reads it today.
    static constexpr const char* kCantMoveKey = "__cant_move_turn";

    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true, .must_be_friendly = true};
    }
    bool needsPlayTimeTarget() const override { return true; }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId unit = pickTarget(ctx, "Hunker Down: ready a friendly unit",
                                       enumerateLegalTargets(ctx.state, ctx.controller));
        if (unit == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (unit != kInvalidId && ctx.state.objectExists(unit)) {
            ctx.executor.readyObject(unit);
            ctx.state.getObject(unit).card_counters[kCantMoveKey] = ctx.state.turn.turn_number + 1;
        }
        ctx.executor.drawCards(ctx.controller, 1);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 976;
        d.def_id = R"RB(rad-034-167)RB";
        d.name = R"RB(Hunker Down)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-034/167)RB";
        d.collector_number = 34;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Calm};
        d.energy_cost = 1;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Reaction);
        d.ability_text = R"RB([Reaction] (Play any time, even before spells and abilities resolve.)
Ready a friendly unit. It can't move this turn. Draw 1.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/111c518590b17919e5b851ef6c713c7eb7504ae2-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_976(CardRegistry& r) {
    r.registerCard(976, std::make_unique<HunkerDown>());
}

}  // namespace riftbound
