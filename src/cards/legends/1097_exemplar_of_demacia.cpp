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

class ExemplarOfDemacia : public LegendCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play a gear with [Deploy], you may exhaust me and pay [A] to
    //  move a friendly unit to that battlefield."
    // "When you play a card" reaches the Legend Zone with the played card as
    // the subject. As the play is announced the gear has not been placed yet
    // and the trigger condition has no registry to ask Card::deploys(), so the
    // ability triggers for any gear whose costs could be met and checks
    // [Deploy] (and the battlefield it went to) as it is finalized; for a gear
    // without [Deploy] it does nothing. The yes/no, the unit and the costs
    // (exhaust me, [A]) are decided when the ability is finalized.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayACard; }

    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType /*trigger*/, GameObjectId subject) const override {
        if (!state.objectExists(subject) || !state.getObject(subject).isGear()) return false;
        if (!state.objectExists(source) || state.getObject(source).is_exhausted) return false;
        if (payableAnyPower(state, controller) < 1) return false;
        for (const auto& [id, o] : state.objects)
            if (o.isUnit() && o.controller == controller && o.location.has_value()) return true;
        return false;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId gear = ctx.state.chain.resuming.has_value()
            ? ctx.state.chain.resuming->triggering_subject : kInvalidId;
        const auto dest = deployedAt(ctx, gear);
        if (!dest.has_value()) { if (finalizingChoices(ctx)) return; return; }

        auto units = [&]() {
            std::vector<GameObjectId> out;
            for (const auto& [id, o] : ctx.state.objects) {
                if (!o.isUnit() || o.controller != ctx.controller || !o.location.has_value()) continue;
                if (o.battlefieldId() == dest) continue;   // already there
                out.push_back(id);
            }
            return out;
        };
        auto can_pay = [&]() {
            if (costAlreadyPaid(ctx)) return true;
            return ctx.state.objectExists(ctx.source) && !ctx.state.getObject(ctx.source).is_exhausted &&
                   ctx.executor.payableAnyPower(ctx.controller) >= 1;
        };
        if (!costAlreadyPaid(ctx) && (!can_pay() || units().empty())) {
            if (finalizingChoices(ctx)) return;
            return;
        }
        int go = confirmOptional(ctx, "Exemplar of Demacia: exhaust me and pay [A] to move a unit there?",
                                 [&]() { return can_pay() && (costAlreadyPaid(ctx) || !units().empty()); });
        GameObjectId unit = kInvalidId;
        if (go == 1) unit = pickTarget(ctx, "Exemplar of Demacia: unit to move", units());
        bool paid = false;
        if (go == 1 && unit != kInvalidId && !costAlreadyPaid(ctx) && can_pay() && costDueNow(ctx)) {
            ctx.executor.exhaustObject(ctx.source);
            ctx.executor.payAnyPower(ctx.controller, 1);
            paid = true;
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (go != 1 || unit == kInvalidId) return;
        if (!paid && !costAlreadyPaid(ctx)) return;
        if (!ctx.state.objectExists(unit)) return;
        const auto& u = ctx.state.getObject(unit);
        if (!u.isUnit() || u.controller != ctx.controller || !u.location.has_value()) return;
        if (u.battlefieldId() == dest) return;
        ctx.executor.moveToBattlefield(unit, *dest);
        ctx.events.logTrace("EXEMPLAR OF DEMACIA: moved " + ctx.state.getObject(unit).name +
                            " to the [Deploy] gear's battlefield");
    }

private:
    // The battlefield a just-played [Deploy] gear went to.
    static std::optional<BattlefieldId> deployedAt(const CardContext& ctx, GameObjectId gear) {
        if (gear == kInvalidId || !ctx.state.objectExists(gear)) return std::nullopt;
        const auto& g = ctx.state.getObject(gear);
        if (!g.isGear()) return std::nullopt;
        const Card* c = (ctx.registry && g.card_def_id != kInvalidId) ? ctx.registry->get(g.card_def_id)
                                                                      : nullptr;
        if (!c || !c->deploys()) return std::nullopt;
        return g.battlefieldId();
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1097;
        d.def_id = R"RB(rad-155-167)RB";
        d.name = R"RB(Exemplar of Demacia)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-155/167)RB";
        d.collector_number = 155;
        d.artist = R"RB(Grafit Studio)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Body, Domain::Order};
        d.tags = {R"RB(Jarvan IV)RB"};
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When you play a gear with [Deploy], you may exhaust me and pay [A] to move a friendly unit to that battlefield.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/e85c005f938e44655f926bb406a47359ae51ecc1-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1097(CardRegistry& r) {
    r.registerCard(1097, std::make_unique<ExemplarOfDemacia>());
}

}  // namespace riftbound
