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
// ability_text written in the repo's symbol convention with standard reminder
// text. Text matches the official reveal tweet and retailer listings. The
// reveal tweet said '4 Power', which appears to be the Energy cost; no source
// showed a Power cost (assumed none, unverified). Tag 'Sentinel' reported by a
// retailer summary — verify. No image_url / artist.
class StoicFerryman : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // [Shield 3] is the engine keyword (shield_value).
    // "When you play me to a battlefield, you may move another unit you
    //  control here to your base." Triggers only if I was played to a
    // battlefield; the "you may" and the unit are chosen as the ability is
    // finalized, and the unit must still be here as it resolves.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId /*controller*/,
                          TriggerType /*trigger*/, GameObjectId /*subject*/) const override {
        return state.objectExists(source) && state.getObject(source).isAtBattlefield();
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        int go = confirmOptional(ctx, "Stoic Ferryman: move another unit here to your base?",
                                 [&]() { return !othersHere(ctx).empty(); });
        GameObjectId unit = kInvalidId;
        if (go == 1) unit = pickTarget(ctx, "Stoic Ferryman: unit to move to base", othersHere(ctx));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (go != 1) return;
        if (unit == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the target
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        ctx.executor.moveToBase(unit);
        ctx.events.logTrace("STOIC FERRYMAN: moved " + ctx.state.getObject(unit).name + " to base");
    }
private:
    /// Other units I control at my battlefield.
    static std::vector<GameObjectId> othersHere(const CardContext& ctx) {
        std::vector<GameObjectId> out;
        if (!ctx.state.objectExists(ctx.source)) return out;
        const auto here = ctx.state.getObject(ctx.source).battlefieldId();
        if (!here.has_value()) return out;
        for (const auto& [id, obj] : ctx.state.objects)
            if (id != ctx.source && obj.isUnit() && obj.controller == ctx.controller &&
                obj.battlefieldId() == here)
                out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1110;
        d.def_id = R"RB(rad-031-167)RB";
        d.name = R"RB(Stoic Ferryman)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-031/167)RB";
        d.collector_number = 31;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Bilgewater)RB", R"RB(Sentinel)RB"};
        d.energy_cost = 4;
        d.might = 2;
        d.rarity = Rarity::Common;
        d.shield_value = 3;
        d.keywords.set(Keyword::Shield);
        d.ability_text = R"RB([Shield 3] (+3 [M] while I'm a defender.)
When you play me to a battlefield, you may move another unit you control here to your base.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1110(CardRegistry& r) {
    r.registerCard(1110, std::make_unique<StoicFerryman>());
}

}  // namespace riftbound
