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

class Brawl : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    // [Action] is the keyword (engine timing).
    // "Give a unit [Assault] this turn equal to its Might." The unit is chosen
    // as the spell is played (the engine's finalize-time target choice); its
    // Might is read as the spell resolves.
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true};
    }
    bool needsPlayTimeTarget() const override { return true; }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId t = pickTarget(ctx, "Brawl", enumerateLegalTargets(ctx.state, ctx.controller));
        if (t == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;
        const int might = ctx.state.getObject(t).current_might;
        if (might <= 0) return;
        ctx.executor.giveTemporaryKeyword(t, Keyword::Assault, might);
        ctx.events.logTrace("BRAWL: " + ctx.state.getObject(t).name + " gets [Assault " +
                            std::to_string(might) + "] this turn");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 987;
        d.def_id = R"RB(rad-004-167)RB";
        d.name = R"RB(Brawl)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-004/167)RB";
        d.collector_number = 4;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Chaos};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
Give a unit [Assault] this turn equal to its Might. (+1 [M] for each instance of Assault while it's an attacker.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/38fd13c7522bb47dcc5ce1596aeb6a638087ee09-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_987(CardRegistry& r) {
    r.registerCard(987, std::make_unique<Brawl>());
}

}  // namespace riftbound
