#include "cards/card.h"
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

class RelentlessStorm : public LegendCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayAUnit; }
    // "When you play a [Mighty] unit": the unit just played must be Mighty (5+ [M]) as it is played. It used to
    // check whether ANY friendly unit in play was Mighty, so a 3-might Bilgewater Tracker next to a Gentle
    // Gemdragon fired it (Jeremy's game, 8 Nov). The engine hands the played unit as the trigger's subject.
    bool triggerCondition(const GameState& state, GameObjectId /*source*/, PlayerId /*controller*/,
                          TriggerType /*trigger*/, GameObjectId subject) const override {
        if (subject == kInvalidId || !state.objectExists(subject)) return false;
        const auto& u = state.getObject(subject);
        // The play event fires before the next aura pass: a unit that has just arrived may not have its current
        // Might computed yet, so its printed Might stands in.
        const int might = u.current_might > 0 ? u.current_might : u.base_might;
        return u.isUnit() && might >= 5;
    }
    void onTrigger(CardContext& ctx,
                   const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;

        auto still_legal = [&]() -> bool {
            return ctx.state.objectExists(ctx.source) &&
                   !ctx.state.getObject(ctx.source).is_exhausted;
        };
        int conf = confirmOptional(ctx,
            "Relentless Storm: exhaust me to channel 1 rune exhausted?",
            still_legal);
        if (conf == -1) return;  // waiting on agent
        if (conf == 0) return;   // declined / already exhausted
        if (!costAlreadyPaid(ctx) && !still_legal()) return;
        if (costDueNow(ctx)) ctx.state.getObject(ctx.source).is_exhausted = true;
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        ctx.executor.channelRunes(ctx.controller, 1, /*enter_exhausted=*/true);
        ctx.events.logTrace("RELENTLESS STORM: played Mighty unit -> exhaust me, "
                            "channel 1 rune exhausted");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 295;
        d.def_id = R"RB(ogn-300-298)RB";
        d.name = R"RB(Relentless Storm)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-300/298)RB";
        d.collector_number = 300;
        d.artist = R"RB(Alex Flores)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Fury, Domain::Body};
        d.tags = {R"RB(Volibear)RB"};
        d.rarity = Rarity::Showcase;
        d.ability_text = R"RB(When you play a [Mighty] unit, you may exhaust me to channel 1 rune exhausted. (A unit is Mighty while it has 5+ [M].))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/34aa11c88735be28266dbc61486a557454fd6b4c-1488x2078.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_295(CardRegistry& r) {
    r.registerCard(295, std::make_unique<RelentlessStorm>());
}

} // namespace riftbound
