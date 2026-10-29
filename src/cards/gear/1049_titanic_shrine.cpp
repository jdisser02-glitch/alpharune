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

class TitanicShrine : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play a unit with Might greater than each other unit on the
    //  board, exhaust me to draw 1."
    // WhenYouPlayACard carries the played card as the subject (the engine's
    // WhenYouPlayAUnit does not). The condition — a unit whose Might is
    // strictly greater than every other unit on the board (both players') —
    // is checked as the unit is played; an exhausted Shrine can't pay its
    // cost, so it does not trigger then.
    // "Exhaust me" is the ability's cost (CR 383.3.b), paid as it is
    // finalized; the controller may decline to pay it (the ability then
    // does nothing).
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayACard; }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId /*controller*/,
                          TriggerType /*trigger*/, GameObjectId subject) const override {
        if (!state.objectExists(source) || state.getObject(source).is_exhausted) return false;
        if (subject == kInvalidId || !state.objectExists(subject)) return false;
        const auto& played = state.getObject(subject);
        // The unit is still on its way to the board (on the chain) when this fires.
        if (!played.isUnit()) return false;
        for (const auto& [id, obj] : state.objects) {
            if (id == subject || !obj.isUnit() || !obj.location.has_value()) continue;
            if (obj.current_might >= played.current_might) return false;
        }
        return true;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto ready = [&]() {
            return ctx.state.objectExists(ctx.source) &&
                   ctx.state.getObject(ctx.source).location.has_value() &&
                   !ctx.state.getObject(ctx.source).is_exhausted;
        };
        int go = confirmOptional(ctx, "Titanic Shrine: exhaust me to draw 1?",
                                 [&]() { return costAlreadyPaid(ctx) || ready(); });
        if (go == 1 && costDueNow(ctx)) {
            if (!ready()) return;             // can't pay the cost
            ctx.executor.exhaustObject(ctx.source);
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (go != 1) return;
        ctx.executor.drawCards(ctx.controller, 1);
        ctx.events.logTrace("TITANIC SHRINE: exhausted -> draw 1");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1049;
        d.def_id = R"RB(rad-089-167)RB";
        d.name = R"RB(Titanic Shrine)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-089/167)RB";
        d.collector_number = 89;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 3;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When you play a unit with Might greater than each other unit on the board, exhaust me to draw 1.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/7d027f4e30df4f7141799709f9fb785835adefed-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1049(CardRegistry& r) {
    r.registerCard(1049, std::make_unique<TitanicShrine>());
}

}  // namespace riftbound
