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

class IronRevenant : public LegendCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeActivate; }

    // "[Reaction][>] Banish a unit or gear from your trash, [E]: [Add] [A].
    //  Spend this Power only to play units or gear."
    // [E] is paid by the engine; the card to banish is chosen and banished as
    // the ability is put on the chain (a cost, CR 383.3), then [A] is added.
    // APPROXIMATION (ENGINE-GAP): the rune pool has no Power restricted to
    // units/gear (only RunePool::unit_energy, for Energy), so the [A] is plain
    // universal Power and may pay for anything.
    bool hasActivatedAbility() const override { return true; }
    bool isReactionAbility() const override { return true; }
    ActivationCost getActivationCost() const override { return {.exhaust = true}; }
    bool canActivateAbility(const GameState& state, PlayerId controller) const override {
        return !banishable(state, controller).empty();
    }
    void onActivate(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId paid = kInvalidId;
        if (!costAlreadyPaid(ctx)) {
            paid = pickTarget(ctx, "Iron Revenant: banish a unit or gear from your trash",
                              banishable(ctx.state, ctx.controller));
            if (paid == kInvalidId && ctx.state.chain.resuming.has_value() &&
                ctx.state.chain.resuming->resume_point == 7)
                return;   // waiting for the agent
            if (paid == kInvalidId) { if (finalizingChoices(ctx)) return; return; }   // can't pay
            if (costDueNow(ctx)) {
                ctx.executor.banishObject(paid);
                ctx.events.logTrace("IRON REVENANT: banished " + ctx.state.getObject(paid).name +
                                    " from the trash (cost)");
            }
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        ctx.executor.addFloatingUniversalPower(ctx.controller, 1);
        ctx.events.logTrace("IRON REVENANT: [Add] [A] (unit/gear-only restriction not enforced)");
    }
private:
    static std::vector<GameObjectId> banishable(const GameState& state, PlayerId controller) {
        std::vector<GameObjectId> out;
        for (auto cid : state.player(controller).trash) {
            if (!state.objectExists(cid)) continue;
            const auto& o = state.getObject(cid);
            if (o.zone == ZoneType::Trash && (o.isUnit() || o.isGear())) out.push_back(cid);
        }
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1087;
        d.def_id = R"RB(rad-143-167)RB";
        d.name = R"RB(Iron Revenant)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-143/167)RB";
        d.collector_number = 143;
        d.artist = R"RB(TUTU)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Fury, Domain::Order};
        d.tags = {R"RB(Mordekaiser)RB"};
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Reaction);
        d.ability_text = R"RB([Reaction][>] Banish a unit or gear from your trash, [E]: [Add] [A]. Spend this Power only to play units or gear. (Abilities that add resources can't be reacted to.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/90ec3319518771820909592eee963d34dab037bc-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1087(CardRegistry& r) {
    r.registerCard(1087, std::make_unique<IronRevenant>());
}

}  // namespace riftbound
