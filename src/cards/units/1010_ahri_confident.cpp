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

class AhriConfident : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "[Disarm] (When I attack, give an enemy unit here -1 [M] this turn.)
    //  When you reduce the Might of an enemy unit here by 1 or more, give me
    //  +1 [M] this turn."
    //
    // [Disarm 1] (CR 831): the enemy unit is chosen at finalization, then
    // given -1 [M] this turn.
    //
    // APPROXIMATION: the engine announces no "a player reduced a unit's Might"
    // event (giveTemporaryMight emits only a generic "buffed" state change
    // with no responsible player), so the second ability is modelled only for
    // Ahri's own [Disarm]: when it actually lowers the chosen enemy's Might
    // (not replaced, not already at 0), she gets +1 [M] this turn as part of
    // the same resolution. Might reductions from your other spells and
    // abilities don't trigger it.
    static constexpr int kDisarmValue = 1;
    TriggerType triggerType() const override { return TriggerType::WhenIAttack; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto candidates = disarmTargets(ctx, ctx.source);
        std::sort(candidates.begin(), candidates.end());
        GameObjectId victim = pickTarget(ctx, "Ahri, Confident: [Disarm] an enemy unit here", candidates);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        auto& v = ctx.state.getObject(victim);
        if (!v.location.has_value()) return;
        const auto here = ctx.state.getObject(ctx.source).battlefieldId();
        const int before = v.current_might;
        applyDisarm(ctx, victim, kDisarmValue);
        if (!ctx.state.objectExists(victim)) return;
        const auto& after = ctx.state.getObject(victim);
        const bool still_here = here.has_value() && after.battlefieldId() == here;
        if (still_here && after.current_might < before &&
            ctx.state.getObject(ctx.source).location.has_value()) {
            ctx.executor.giveTemporaryMight(ctx.source, 1);
            ctx.events.logTrace("AHRI, CONFIDENT: reduced an enemy unit's Might here -> +1 [M] this turn");
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1010;
        d.def_id = R"RB(rad-038-167)RB";
        d.name = R"RB(Ahri, Confident)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-038/167)RB";
        d.collector_number = 38;
        d.artist = R"RB(Naifan Zhang)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Ahri)RB", R"RB(Ionia)RB"};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([Disarm] (When I attack, give an enemy unit here -1 [M] this turn.)
When you reduce the Might of an enemy unit here by 1 or more, give me +1 [M] this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/cf3c6a5963ad27976960977b0a51ea185f551696-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1010(CardRegistry& r) {
    r.registerCard(1010, std::make_unique<AhriConfident>());
}

}  // namespace riftbound
