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

class DisposalExpert : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "I enter ready if you control no other units." Checked with me already
    // at my destination: any other unit the controller has on the board
    // makes me enter exhausted as usual.
    bool entersReadyOnPlay(const GameState& state, PlayerId controller,
                           const GameObject& self) const override {
        for (const auto& [id, obj] : state.objects)
            if (id != self.id && obj.isUnit() && obj.controller == controller &&
                obj.location.has_value())
                return false;
        return true;
    }

    // "[Disarm 2] (When I attack, give an enemy unit here -2 [M] this turn.)"
    // [Disarm] (CR 831) is not an engine keyword: a WhenIAttack trigger whose
    // enemy unit is chosen at finalization.
    TriggerType triggerType() const override { return TriggerType::WhenIAttack; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId victim = pickTarget(ctx, "Disposal Expert: [Disarm 2]", disarmTargets(ctx, ctx.source));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (victim == kInvalidId) return;     // no enemy unit here / waiting for the agent
        applyDisarm(ctx, victim, 2);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1052;
        d.def_id = R"RB(rad-093-167)RB";
        d.name = R"RB(Disposal Expert)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-093/167)RB";
        d.collector_number = 93;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Fury};
        d.energy_cost = 4;
        d.might = 3;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(I enter ready if you control no other units.
[Disarm 2] (When I attack, give an enemy unit here -2 [M] this turn.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/79ec89753be773e7ea93b2116ea29d21c81fcef9-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1052(CardRegistry& r) {
    r.registerCard(1052, std::make_unique<DisposalExpert>());
}

}  // namespace riftbound
