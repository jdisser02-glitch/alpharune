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

class PortablePortal : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool usesTextAuras() const override { return false; }  // aura is in code below
    bool deploys() const override { return true; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play this, you may pay [O] to ready a unit here."
    // [O] = one Body Power. The "you may", the unit (any unit at this
    // battlefield) and the payment are settled as the ability is finalized;
    // the unit is readied on resolution (a mistarget if it left).
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayThis; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto units_here = [&]() { return unitsHere(ctx); };
        auto can_pay = [&]() { return payableOnePower(ctx.state, ctx.controller, Domain::Body); };
        int go = confirmOptional(ctx, "Portable Portal: pay [O] to ready a unit here?", [&]() {
            return !units_here().empty() && (costAlreadyPaid(ctx) || can_pay());
        });
        GameObjectId unit = kInvalidId;
        if (go == 1) unit = pickTarget(ctx, "Portable Portal: unit to ready", units_here());
        if (go == 1 && unit != kInvalidId && costDueNow(ctx) &&
            !payOnePower(ctx, ctx.controller, Domain::Body))
            return;                           // can't pay: nothing happens
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (go != 1 || unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        ctx.executor.readyObject(unit);
        ctx.events.logTrace("PORTABLE PORTAL: paid [O] -> readied " + ctx.state.getObject(unit).name);
    }

    // "Friendly units here have [Ganking]."
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        forEachSelfOnce(*this, state, controller, [&](GameObjectId sid, GameObject& self) {
            const auto here = self.battlefieldId();
            if (!here.has_value() || self.attached_to.has_value()) return;
            for (auto& [id, obj] : state.objects) {
                if (!obj.isUnit() || obj.controller != controller) continue;
                if (obj.battlefieldId() != here) continue;
                auraKeyword(obj, sid, Keyword::Ganking);
            }
        });
    }
private:
    static std::vector<GameObjectId> unitsHere(const CardContext& ctx) {
        if (!ctx.state.objectExists(ctx.source)) return {};
        const auto here = ctx.state.getObject(ctx.source).battlefieldId();
        if (!here.has_value()) return {};
        return chooseableUnits(ctx.state, ctx.controller,
                               [&](const GameObject& o) { return o.battlefieldId() == here; });
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1047;
        d.def_id = R"RB(rad-087-167)RB";
        d.name = R"RB(Portable Portal)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-087/167)RB";
        d.collector_number = 87;
        d.card_type = CardType::Gear;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 2;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
When you play this, you may pay [O] to ready a unit here.
Friendly units here have [Ganking]. (They can move from battlefield to battlefield.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/dba5c879aae5c614198eb8408e0104a1b0618102-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1047(CardRegistry& r) {
    r.registerCard(1047, std::make_unique<PortablePortal>());
}

}  // namespace riftbound
