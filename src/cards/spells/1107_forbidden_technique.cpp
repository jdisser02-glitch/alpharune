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

// RECONSTRUCTED TEXT (not in cards/card_index.json; confidence: medium).
// Gathered from web-search summaries (WebFetch to card sites was blocked);
// ability_text written in the repo's symbol convention. The text is the
// official errata NEW TEXT (repo errata/radiance-errata.txt); the official
// trash cost [1][C][C] uses Riot's Fury symbol [C], converted here to [R].
// Power cost: no source showed one (retailers list only Energy 2) — taken as
// 0, unverified. No timing keyword found. No image_url / artist.
class ForbiddenTechnique : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "Ready a unit." The unit is chosen as the spell is played
    // (needsPlayTimeTarget); if it is no longer legal at resolution the spell
    // does nothing.
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true};
    }
    bool needsPlayTimeTarget() const override { return true; }

    // "When you play a card with Energy cost [7] or more, you may play this
    //  from your trash for [1][R][R]. Then banish this."
    // APPROXIMATION (engine gap): no trigger reaches a card in the trash on
    // "when you play a card" (TriggerManager only fires WhenYouPlayACard on
    // cards on the board / in the Legend Zone), and nothing records the Energy
    // cost of the cards a player played this turn, so the condition can't be
    // checked. The replay is modeled with the engine's [Flow] machinery: while
    // this is in its owner's trash it may be played for [1][R][R] at normal
    // spell timing and is banished as it leaves the chain. Deviations: the
    // replay is not limited to right after you play a 7+ Energy card (it is
    // available any time you could play a spell), and effects that modify
    // Flow costs (Stargazer) also apply to it.
    FlowCost flowCost() const override { return flow(1, 2, Domain::Fury); }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId unit = pickTarget(ctx, "Forbidden Technique: ready a unit",
                                       enumerateLegalTargets(ctx.state, ctx.controller));
        if (unit == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        ctx.executor.readyObject(unit);
        ctx.events.logTrace("FORBIDDEN TECHNIQUE: readied " + ctx.state.getObject(unit).name);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1107;
        d.def_id = R"RB(rad-010-167)RB";
        d.name = R"RB(Forbidden Technique)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-010/167)RB";
        d.collector_number = 10;
        d.card_type = CardType::Spell;
        d.domains = {Domain::Fury};
        d.energy_cost = 2;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(Ready a unit.
When you play a card with Energy cost [7] or more, you may play this from your trash for [1][R][R]. Then banish this.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1107(CardRegistry& r) {
    r.registerCard(1107, std::make_unique<ForbiddenTechnique>());
}

}  // namespace riftbound
