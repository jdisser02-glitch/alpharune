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

// RECONSTRUCTED TEXT: this card is not in cards/card_index.json. Its data and
// ability text were reconstructed from web-search summaries (confidence:
// medium). Notes: the payoff adds one any-domain Power (riftbound.gg shows the
// rainbow rune symbol; retailers say "1 Rune"); the Show Off reminder wording
// is reconstructed. Ruling: a facedown card can't be shown off for it. Power
// cost not shown in any source (null, taken as 0).
class GutterGuardian : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "[Show Off] a card with [Hidden]. (As you play me, you may reveal a card
    //  with [Hidden] from hand or pick another friendly one.)
    //  When you play me, if you showed off, [Add] [A]."
    //
    // [Show Off] (CR 832) costs only information, which the engine does not
    // model, so the player shows off whenever a card with [Hidden] is in hand
    // or among their other face-up game objects (a facedown card can't be
    // shown off — ruling). Decided as I am played (onPlay) and recorded on me
    // for the play trigger. [Hidden] itself is the engine keyword.
    static constexpr const char* kShowedOff = "__gutter_guardian_showed_off";

    void onPlay(CardContext& ctx) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        ctx.state.getObject(ctx.source).card_counters[kShowedOff] = canShowOffHidden(ctx) ? 1 : 0;
    }

    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        bool showed = false;
        if (ctx.state.objectExists(ctx.source)) {
            auto& self = ctx.state.getObject(ctx.source);
            auto it = self.card_counters.find(kShowedOff);
            showed = it != self.card_counters.end() ? it->second != 0 : canShowOffHidden(ctx);
            self.card_counters.erase(kShowedOff);
        }
        if (!showed) {
            ctx.events.logTrace("GUTTER GUARDIAN: no [Hidden] card shown off");
            return;
        }
        ctx.executor.addFloatingUniversalPower(ctx.controller, 1);
        ctx.events.logTrace("GUTTER GUARDIAN: showed off a [Hidden] card -> [Add] [A]");
    }
private:
    static bool canShowOffHidden(const CardContext& ctx) {
        return canShowOff(ctx, ctx.source, [](const GameObject& o) {
            return !o.is_hidden && o.zone != ZoneType::FacedownZone && o.hasKeyword(Keyword::Hidden);
        });
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1121;
        d.def_id = R"RB(rad-103-167)RB";
        d.name = R"RB(Gutter Guardian)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-103/167)RB";
        d.collector_number = 103;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Zaun)RB"};
        d.energy_cost = 3;
        d.power_cost = 0;   // unknown in every source (null)
        d.might = 3;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Hidden);
        d.ability_text = R"RB([Hidden] (Hide now for [A] to react with later for [0].)
[Show Off] a card with [Hidden]. (As you play me, you may reveal a card with [Hidden] from hand or pick another friendly one.)
When you play me, if you showed off, [Add] [A].)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1121(CardRegistry& r) {
    r.registerCard(1121, std::make_unique<GutterGuardian>());
}

}  // namespace riftbound
