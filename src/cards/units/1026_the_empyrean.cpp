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

class TheEmpyrean : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "[Show Off] a card. (As you play me, you may reveal a card from hand or
    //  pick another friendly one.)
    //  When you play me, if you showed off a card, [Predict] equal to its
    //  Power cost, then draw 2."
    //
    // [Show Off] (CR 832) costs only information, which the engine does not
    // model, so the player always shows off when they can, and shows off the
    // card with the highest Power cost (a bigger Predict is never worse: any
    // looked-at card may be kept). It is decided as I am played (onPlay — "As
    // you play me") among the cards in hand and the friendly non-token cards
    // on the board, and recorded on me for the play trigger.
    static constexpr const char* kShownPower = "__empyrean_shown_power";

    void onPlay(CardContext& ctx) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        ctx.state.getObject(ctx.source).card_counters[kShownPower] = bestShownPower(ctx);
    }

    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        int shown = -1;
        if (ctx.state.objectExists(ctx.source)) {
            auto& self = ctx.state.getObject(ctx.source);
            auto it = self.card_counters.find(kShownPower);
            shown = it != self.card_counters.end() ? it->second : bestShownPower(ctx);
            self.card_counters.erase(kShownPower);
        } else {
            shown = bestShownPower(ctx);
        }
        if (shown < 0) {
            ctx.events.logTrace("THE EMPYREAN: nothing to show off");
            return;
        }
        if (shown > 0) ctx.executor.predict(ctx.controller, shown);
        ctx.executor.drawCards(ctx.controller, 2);
        ctx.events.logTrace("THE EMPYREAN: showed off Power " + std::to_string(shown) +
                            " -> Predict " + std::to_string(shown) + ", draw 2");
    }
private:
    /// Highest Power cost among the cards I could show off, -1 if none.
    static int bestShownPower(const CardContext& ctx) {
        const auto& db = ctx.executor.cardDB();
        return bestShowOff(ctx, ctx.source,
            [](const GameObject& o) { return !o.isToken() && o.card_def_id != kInvalidId; },
            [&](const GameObject& o) { return db.get(o.card_def_id).power_cost; });
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1026;
        d.def_id = R"RB(rad-060-167)RB";
        d.name = R"RB(The Empyrean)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-060/167)RB";
        d.collector_number = 60;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Dragon)RB"};
        d.energy_cost = 8;
        d.might = 8;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB([Show Off] a card. (As you play me, you may reveal a card from hand or pick another friendly one.)
When you play me, if you showed off a card, [Predict] equal to its Power cost, then draw 2. (Look at that many cards from your Main Deck. Recycle any of them and put the rest back in any order.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b7cd2e475834863bf4e66994118a835ba4377d16-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1026(CardRegistry& r) {
    r.registerCard(1026, std::make_unique<TheEmpyrean>());
}

}  // namespace riftbound
