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

class SirenOfTheShallows : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // [Ganking] is engine-handled (keyword).
    // "[Disarm] (When I attack, give an enemy unit here -1 [M] this turn.)"
    // [Disarm 1] (CR 831): the enemy unit here is chosen at finalization and
    // given -1 [M] this turn as the ability resolves (a mistarget if it has
    // left this battlefield by then).
    static constexpr int kDisarmValue = 1;
    TriggerType triggerType() const override { return TriggerType::WhenIAttack; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) { if (finalizingChoices(ctx)) return; return; }
        auto candidates = disarmTargets(ctx, ctx.source);
        std::sort(candidates.begin(), candidates.end());
        GameObjectId victim = pickTarget(ctx, "Siren of the Shallows: [Disarm] an enemy unit here",
                                         candidates);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        if (!ctx.state.getObject(victim).location.has_value()) return;
        applyDisarm(ctx, victim, kDisarmValue);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1055;
        d.def_id = R"RB(rad-097-167)RB";
        d.name = R"RB(Siren of the Shallows)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-097/167)RB";
        d.collector_number = 97;
        d.artist = R"RB(Wild Blue Studios)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.energy_cost = 2;
        d.might = 1;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Ganking);
        d.ability_text = R"RB([Disarm] (When I attack, give an enemy unit here -1 [M] this turn.)
[Ganking] (I can move from battlefield to battlefield.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/e666029dcebc507cc80435eee17db435305d1361-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1055(CardRegistry& r) {
    r.registerCard(1055, std::make_unique<SirenOfTheShallows>());
}

}  // namespace riftbound
