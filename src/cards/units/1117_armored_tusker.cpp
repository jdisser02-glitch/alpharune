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

// RECONSTRUCTED TEXT (not in cards/card_index.json; confidence: high).
// Gathered from web-search summaries (WebFetch to card sites was blocked);
// ability_text written in the repo's symbol convention with standard reminder
// text. No image_url / artist.
class ArmoredTusker : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // [Shield 3] and [Tank] are engine keywords.
    // "When I move, you may deal damage equal to my [Shield] to a unit at a
    //  battlefield." The "you may" and the unit are chosen as the ability is
    // finalized; the amount is my Shield value as it resolves: printed and
    // "this turn" Shield (shield_value) plus Shield granted by continuous
    // effects (a granted plain [Shield] counts 1).
    TriggerType triggerType() const override { return TriggerType::WhenIMove; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        int go = confirmOptional(ctx, "Armored Tusker: deal damage equal to my [Shield]?",
                                 [&]() { return !unitsAtBattlefields(ctx).empty(); });
        GameObjectId victim = kInvalidId;
        if (go == 1) victim = pickTarget(ctx, "Armored Tusker: unit at a battlefield",
                                         unitsAtBattlefields(ctx));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (go != 1) return;
        if (victim == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the target
        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        if (!ctx.state.objectExists(ctx.source)) return;
        const int amount = shieldOf(ctx.state.getObject(ctx.source));
        if (amount <= 0) return;
        ctx.executor.dealDamage(victim, amount, ctx.source);
        ctx.events.logTrace("ARMORED TUSKER: dealt " + std::to_string(amount) + " to " +
                            ctx.state.getObject(victim).name);
    }
private:
    static int shieldOf(const GameObject& o) {
        int v = o.shield_value;
        for (const auto& ae : o.aura_effects)
            if (ae.keyword == Keyword::Shield) v += ae.keyword_value > 0 ? ae.keyword_value : 1;
        return v;
    }
    static std::vector<GameObjectId> unitsAtBattlefields(const CardContext& ctx) {
        auto out = chooseableUnits(ctx.state, ctx.controller,
                                   [](const GameObject& o) { return o.isAtBattlefield(); });
        std::sort(out.begin(), out.end());
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1117;
        d.def_id = R"RB(rad-078-167)RB";
        d.name = R"RB(Armored Tusker)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-078/167)RB";
        d.collector_number = 78;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Noxus)RB"};
        d.energy_cost = 6;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Uncommon;
        d.shield_value = 3;
        d.keywords.set(Keyword::Shield);
        d.keywords.set(Keyword::Tank);
        d.ability_text = R"RB([Shield 3] (+3 [M] while I'm a defender.)
[Tank] (I must be assigned combat damage first.)
When I move, you may deal damage equal to my [Shield] to a unit at a battlefield.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1117(CardRegistry& r) {
    r.registerCard(1117, std::make_unique<ArmoredTusker>());
}

}  // namespace riftbound
