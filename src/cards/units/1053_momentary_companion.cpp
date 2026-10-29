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

class MomentaryCompanion : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // [Hidden] is the engine keyword.
    // "When you play me, if it's not your turn, give me [Temporary]."
    // An intervening "if": checked as I'm played (no trigger on your own
    // turn) and again as the ability resolves. [Temporary] is given for good
    // (not "this turn"), as Fading Memories does: the Beginning-Phase sweep
    // kills me at the start of my controller's next turn.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    bool triggerCondition(const GameState& state, GameObjectId /*source*/, PlayerId controller,
                          TriggerType /*trigger*/, GameObjectId /*subject*/) const override {
        return state.turn.turn_player != controller;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.state.turn.turn_player == ctx.controller) return;
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& self = ctx.state.getObject(ctx.source);
        if (!self.location.has_value()) return;
        self.keywords.set(Keyword::Temporary);
        ctx.events.logTrace("MOMENTARY COMPANION: played on an opponent's turn -> [Temporary]");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1053;
        d.def_id = R"RB(rad-095-167)RB";
        d.name = R"RB(Momentary Companion)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-095/167)RB";
        d.collector_number = 95;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.energy_cost = 4;
        d.might = 4;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Hidden);
        d.ability_text = R"RB([Hidden] (Hide now for [A] to react with later for [0].)
When you play me, if it's not your turn, give me [Temporary]. (Kill me at the start of my controller's Beginning Phase, before scoring.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/aea67209470e47ec876a11ae9b3eacda00df7be1-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1053(CardRegistry& r) {
    r.registerCard(1053, std::make_unique<MomentaryCompanion>());
}

}  // namespace riftbound
