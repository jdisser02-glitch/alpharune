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

class EkkoIngenious : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "As an additional cost to play me, return a friendly unit to its owner's
    //  hand. [Ganking]" ([Ganking] is the engine keyword.)
    //
    // DEVIATION: the engine has no mandatory "return a friendly unit"
    // additional cost (Card::optionalAdditionalCost is optional and covers
    // resources / discards only). The card models it itself:
    //   * I can be played only while my controller has a unit on the board to
    //     return (canBePlayed — consulted by every play generator and by
    //     effect-driven plays through canAfford);
    //   * the unit is chosen and returned when my play trigger is finalized —
    //     a WhenYouPlayMe ability whose choice and "cost" (costDueNow) happen
    //     before anyone can respond — so the return happens after the Energy
    //     payment and with me already on the board, not alongside the cost.
    //     I can't return myself.
    // If the trigger resolves without a finalize pass, the return happens at
    // resolution instead.
    bool canBePlayed(const GameState& state, PlayerId player) const override {
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == player && obj.location.has_value())
                return true;
        return false;
    }

    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (costAlreadyPaid(ctx)) return;   // returned at finalization: nothing left to do
        std::vector<GameObjectId> legal;
        for (const auto& [id, obj] : ctx.state.objects)
            if (id != ctx.source && obj.isUnit() && obj.controller == ctx.controller &&
                obj.location.has_value())
                legal.push_back(id);
        std::sort(legal.begin(), legal.end());
        if (legal.empty()) { if (finalizingChoices(ctx)) return; return; }
        GameObjectId unit = pickTarget(ctx, "Ekko, Ingenious: return a friendly unit (additional cost)",
                                       legal);
        const bool waiting = unit == kInvalidId && ctx.state.chain.resuming.has_value() &&
                             ctx.state.chain.resuming->resume_point == 7;
        if (!waiting && unit != kInvalidId && ctx.state.objectExists(unit) && costDueNow(ctx)) {
            ctx.executor.bounceToHand(unit);
            ctx.events.logTrace("EKKO, INGENIOUS: returned a friendly unit to its owner's hand (additional cost)");
        }
        if (finalizingChoices(ctx)) return;   // choices and the cost end here (CR 383.3)
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1027;
        d.def_id = R"RB(rad-061-167)RB";
        d.name = R"RB(Ekko, Ingenious)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-061/167)RB";
        d.collector_number = 61;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Ekko)RB", R"RB(Zaun)RB"};
        d.energy_cost = 3;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Ganking);
        d.ability_text = R"RB(As an additional cost to play me, return a friendly unit to its owner's hand.
[Ganking] (I can move from battlefield to battlefield.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/3129469742d7aa5c6921ebae841ccee1923bb279-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1027(CardRegistry& r) {
    r.registerCard(1027, std::make_unique<EkkoIngenious>());
}

}  // namespace riftbound
