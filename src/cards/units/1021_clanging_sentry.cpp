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

class ClangingSentry : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "Your Mechs have [Disarm]." Granted [Disarm] (CR 831) is "When I attack,
    // give an enemy unit here -1 [M] this turn"; each further grant adds to
    // the Disarm value and the keyword triggers once with the total
    // (831.1.d.1, 831.2: two Sentries give Disarm 2).
    //
    // DEVIATION (engine gap): the engine cannot grant a triggered ability to
    // other units, and no trigger tells a card that ANOTHER friendly unit
    // attacks — except WhenAUnitAttacksOrDefendsAlone, which fires on the
    // side's cards when exactly one unit attacks (or defends) at a
    // battlefield, with that unit as the subject. The Sentry rides that
    // trigger, so a friendly Mech gets its Disarm when it attacks ALONE; a
    // Mech attacking alongside other units gets none. Only one Sentry fires
    // (the lowest object id among those its controller has on the board) and
    // it applies the summed value: one per Sentry its controller controls as
    // the ability resolves. A Mech's own printed Disarm (its own ability)
    // still triggers separately rather than being summed with the granted one.
    // Reads "Mech" as the unit tag. Clanging Sentry itself has no Mech tag in
    // the card data, so it does not get Disarm from its own text.
    TriggerType triggerType() const override { return TriggerType::WhenAUnitAttacksOrDefendsAlone; }

    static int sentriesControlled(const GameState& state, PlayerId p, CardDefId me) {
        int n = 0;
        for (const auto& [id, obj] : state.objects)
            if (obj.card_def_id == me && obj.controller == p && obj.isUnit() && obj.location.has_value())
                ++n;
        return n;
    }

    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType /*trigger*/, GameObjectId subject) const override {
        if (subject == kInvalidId || !state.objectExists(subject)) return false;
        const auto& unit = state.getObject(subject);
        if (!unit.isUnit() || unit.controller != controller || !unit.isAtBattlefield()) return false;
        if (unit.combat_designation != CombatDesignation::Attacker) return false;
        if (!hasTag(unit, "Mech")) return false;
        // One trigger for all Sentries: the lowest-id one fires.
        for (const auto& [id, obj] : state.objects)
            if (id < source && obj.card_def_id == cardDefId() && obj.controller == controller &&
                obj.isUnit() && obj.location.has_value())
                return false;
        return true;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId mech = ctx.state.chain.resuming.has_value()
            ? ctx.state.chain.resuming->triggering_subject : kInvalidId;
        const auto enemies = disarmTargets(ctx, mech);
        const GameObjectId victim = pickTarget(ctx, "Clanging Sentry: Mech [Disarm]", enemies);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (victim == kInvalidId) return;     // waiting for the agent / no enemy there
        const int value = std::max(1, sentriesControlled(ctx.state, ctx.controller, cardDefId()));
        applyDisarm(ctx, victim, value);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1021;
        d.def_id = R"RB(rad-055-167)RB";
        d.name = R"RB(Clanging Sentry)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-055/167)RB";
        d.collector_number = 55;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Mind};
        d.energy_cost = 3;
        d.might = 2;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(Your Mechs have [Disarm]. (When each of us attacks, give an enemy unit there -1 [M] this turn.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/5d4c22ef863e041887a00d34b2dae1f3ce658326-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1021(CardRegistry& r) {
    r.registerCard(1021, std::make_unique<ClangingSentry>());
}

}  // namespace riftbound
