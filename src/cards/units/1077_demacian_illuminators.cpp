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

class DemacianIlluminators : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When one or more friendly units die, [Buff] a friendly unit."
    // The engine fires WhenAFriendlyUnitDies once per dying unit; units that
    // die together all die before the resulting abilities are finalized, so
    // "one or more" is one trigger per batch: while an ability of mine from
    // this event is still pending on the chain, further deaths don't trigger
    // again. The unit to buff is chosen as the ability is finalized.
    TriggerType triggerType() const override { return TriggerType::WhenAFriendlyUnitDies; }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId /*controller*/,
                          TriggerType trigger, GameObjectId /*subject*/) const override {
        if (trigger != TriggerType::WhenAFriendlyUnitDies) return true;
        for (const auto& ci : state.chain.items)
            if (ci.source == source && ci.status == ChainItemStatus::Pending &&
                ci.fired_trigger == TriggerType::WhenAFriendlyUnitDies)
                return false;   // the same batch of deaths already triggered me
        return true;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto friendly = chooseableUnits(ctx.state, ctx.controller,
            [&](const GameObject& o) { return o.controller == ctx.controller; });
        std::sort(friendly.begin(), friendly.end());
        GameObjectId picked = pickTarget(ctx, "Demacian Illuminators: [Buff] a friendly unit", friendly);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (picked == kInvalidId || !ctx.state.objectExists(picked)) return;
        if (!ctx.state.getObject(picked).location.has_value()) return;
        ctx.executor.buffUnit(picked);
        ctx.events.logTrace("DEMACIAN ILLUMINATORS: buffed " + ctx.state.getObject(picked).name);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1077;
        d.def_id = R"RB(rad-131-167)RB";
        d.name = R"RB(Demacian Illuminators)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-131/167)RB";
        d.collector_number = 131;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Order};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.might = 3;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When one or more friendly units die, [Buff] a friendly unit. (Give it a +1 [M] buff if it doesn't have one.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/127c2700ee4e2557dfd463ed84c45f4600ad9723-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1077(CardRegistry& r) {
    r.registerCard(1077, std::make_unique<DemacianIlluminators>());
}

}  // namespace riftbound
