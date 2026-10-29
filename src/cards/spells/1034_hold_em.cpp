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

class HoldEm : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "[Action] This costs [2] less if you've played a unit or gear to a
    //  battlefield this turn. Give a unit +3 [M] this turn."
    // [Action] is the engine keyword. "Played to a battlefield this turn" is
    // read from BattlefieldState::last_play_here_turn, which the engine stamps
    // for every unit or gear a player plays to a battlefield. The unit is
    // chosen as the spell is played (needsPlayTimeTarget).
    int selfCostReduction(const GameState& state, PlayerId player) const override {
        const int p = player == PlayerId::Player1 ? 0 : 1;
        for (const auto& bf : state.battlefields)
            if (bf.last_play_here_turn[p] == state.turn.turn_number) return 2;
        return 0;
    }

    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true};
    }
    bool needsPlayTimeTarget() const override { return true; }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId t = pickTarget(ctx, "Hold 'Em: give a unit +3 [M] this turn",
                                    enumerateLegalTargets(ctx.state, ctx.controller));
        if (t == kInvalidId) return;   // waiting for the agent / no longer a legal target
        if (!ctx.state.objectExists(t) || !ctx.state.getObject(t).location.has_value()) return;
        ctx.executor.giveTemporaryMight(t, 3);
        ctx.events.logTrace("HOLD 'EM: +3 [M] to " + ctx.state.getObject(t).name);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1034;
        d.def_id = R"RB(rad-070-167)RB";
        d.name = R"RB(Hold 'Em)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-070/167)RB";
        d.collector_number = 70;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Body};
        d.energy_cost = 3;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
This costs [2] less if you've played a unit or gear to a battlefield this turn.
Give a unit +3 [M] this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/0d9af22d38d32b0ec2a6584d25abe201730f9e11-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1034(CardRegistry& r) {
    r.registerCard(1034, std::make_unique<HoldEm>());
}

}  // namespace riftbound
