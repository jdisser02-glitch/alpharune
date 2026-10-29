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

class PiltovanGuidebot : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play a card, you may have my base Might become equal to that
    //  card's Energy cost this turn."
    // The played card is the trigger's subject; its Energy cost is its printed
    // one (a token's is 0). As with Dragon Form, "base Might becomes N this
    // turn" is a this-turn Might delta of (N - printed base Might), so every
    // other modifier still stacks on top and it expires with the other "this
    // turn" Might. A later use the same turn replaces the earlier one: the
    // delta applied this turn is remembered and swapped out.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayACard; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto on_board = [&]() {
            return ctx.state.objectExists(ctx.source) &&
                   ctx.state.getObject(ctx.source).location.has_value();
        };
        if (!on_board()) { if (finalizingChoices(ctx)) return; return; }
        const int go = confirmOptional(ctx, "Piltovan Guidebot: set my base Might to that card's Energy cost?",
                                       on_board);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (go != 1 || !on_board()) return;

        const GameObjectId played = ctx.state.chain.resuming.has_value()
            ? ctx.state.chain.resuming->triggering_subject : kInvalidId;
        int cost = 0;
        if (played != kInvalidId && ctx.state.objectExists(played)) {
            const auto def_id = ctx.state.getObject(played).card_def_id;
            if (def_id != kInvalidId) cost = ctx.executor.cardDB().get(def_id).energy_cost;
        }
        auto& me = ctx.state.getObject(ctx.source);
        const int turn = ctx.state.turn.turn_number;
        int previous = 0;
        if (auto it = me.card_counters.find("__guidebot_turn");
            it != me.card_counters.end() && it->second == turn)
            previous = me.card_counters["__guidebot_delta"];
        const int delta = cost - me.base_might;
        me.temp_might_bonus += delta - previous;
        me.card_counters["__guidebot_turn"] = turn;
        me.card_counters["__guidebot_delta"] = delta;
        me.recomputeMight();
        ctx.events.logTrace("PILTOVAN GUIDEBOT: base Might is " + std::to_string(cost) +
                            " this turn -> " + std::to_string(me.current_might) + "M");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1023;
        d.def_id = R"RB(rad-057-167)RB";
        d.name = R"RB(Piltovan Guidebot)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-057/167)RB";
        d.collector_number = 57;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Mind};
        d.energy_cost = 4;
        d.might = 3;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you play a card, you may have my base Might become equal to that card's Energy cost this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/9558ced7593f5a26911eb6e1393aae3289842e20-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1023(CardRegistry& r) {
    r.registerCard(1023, std::make_unique<PiltovanGuidebot>());
}

}  // namespace riftbound
