#include "cards/card.h"
#include "cards/card_helpers.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace riftbound {
namespace {

class AstralHeron : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    // "When you play your first card each turn, if I'm at a battlefield, the
    //  next card you play this turn costs [2][A][A] less." (Vendetta erratum,
    //  rules/patch-notes/vendetta-rules-faq-and-clarifications.md; CR example
    //  under 1943 of rules/core-rules.md.) The discount ends with the turn.
    // "First card each turn" is read at resolution as
    // PlayerState::cards_played_this_turn == 1 (the triggering play itself).
    // The [A][A] half is CostModifier::power_reduction, which the engine's
    // cost path subtracts from the card's Power cost (clamped at 0).
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayACard; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        if (!ctx.state.getObject(ctx.source).isAtBattlefield()) return;
        auto& ps = ctx.state.player(ctx.controller);
        if (ps.cards_played_this_turn != 1) return;
        PlayerState::CostModifier m;
        m.source = ctx.source;
        m.energy_reduction = 2;
        m.power_reduction = 2;
        m.next_card_only = true;
        m.this_turn_only = true;    // erratum: "the next card you play THIS TURN" (it carried over to the
                                    // opponent's turn before: Jeremy's game of 8 Nov, free Discipline and Rebuke)
        m.affects_friendly_only = true;
        ps.cost_modifiers.push_back(m);
        ctx.events.logTrace("ASTRAL HERON: first card this turn -> next card costs [2][A][A] less");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 831;
        d.def_id = R"RB(ven-044-166)RB";
        d.name = R"RB(Astral Heron)RB";
        d.set_code = R"RB(VEN)RB";
        d.set_name = R"RB(Vendetta)RB";
        d.public_code = R"RB(VEN-044/166)RB";
        d.collector_number = 44;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Bird)RB", R"RB(Mount Targon)RB"};
        d.energy_cost = 7;
        d.might = 7;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(When you play your first card each turn, if I'm at a battlefield, the next card you play this turn costs [2][A][A] less.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b1ce87efc9a1a7e2438c53e034a7f92caaa67ed6-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_831(CardRegistry& r) {
    r.registerCard(831, std::make_unique<AstralHeron>());
}

}  // namespace riftbound
