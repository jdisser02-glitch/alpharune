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

class SkystrikeSquadron : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "You may return a friendly gear to its owner's hand as an additional
    //  cost to play me. If you do, I cost [2] less." ([Deflect] is the engine
    //  keyword.)
    // APPROXIMATION: the engine has no optional additional cost that returns a
    // permanent to hand (OptionalAdditionalCost models Energy / Power / discard
    // only). So the [2] discount is taken whenever you control a gear
    // (selfCostReduction, which the affordability check and the payment both
    // read), and as I'm played the player settles it at once: return a
    // friendly gear to its owner's hand, or pay the [2] after all. Both are
    // done when the play ability is finalized (no response window in
    // between). This matches the printed card except in rare edge cases: the
    // only gear leaving the board while I'm on the chain keeps the discount
    // for free, and when other reductions already bring my cost to 0 the
    // "pay [2] instead" option still charges [2].
    int selfCostReduction(const GameState& state, PlayerId player) const override {
        return friendlyGear(state, player).empty() ? 0 : 2;
    }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const bool paid = costAlreadyPaid(ctx);
        uint32_t legal = 0;
        if (paid || !friendlyGear(ctx.state, ctx.controller).empty()) legal |= 1u;
        if (paid || payableEnergy(ctx.state, ctx.controller) >= 2) legal |= 2u;
        // No gear: the discount was not taken, nothing is owed.
        if (!paid && (legal & 1u) == 0) { if (finalizingChoices(ctx)) return; return; }
        int mode = pickMode(ctx, "Skystrike Squadron: additional cost", 2,
                            {"Return a friendly gear to its owner's hand", "Pay [2] instead"},
                            legal);
        GameObjectId gear = kInvalidId;
        if (mode == 0)
            gear = pickTarget(ctx, "Skystrike Squadron: gear to return",
                              friendlyGear(ctx.state, ctx.controller));
        if (mode == 0 && gear != kInvalidId && ctx.state.objectExists(gear) && costDueNow(ctx))
            returnGear(ctx, gear);
        if (mode == 1 && costDueNow(ctx) && payEnergy(ctx, ctx.controller, 2))
            ctx.events.logTrace("SKYSTRIKE SQUADRON: paid [2] instead of returning a gear");
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
    }
private:
    static std::vector<GameObjectId> friendlyGear(const GameState& state, PlayerId player) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isGear() && obj.controller == player && obj.location.has_value())
                out.push_back(id);
        return out;
    }
    static void returnGear(CardContext& ctx, GameObjectId gear) {
        auto& g = ctx.state.getObject(gear);
        const std::string name = g.name;
        if (g.attached_to.has_value()) ctx.executor.unattachGear(gear);
        ctx.executor.bounceToHand(gear);
        ctx.events.logTrace("SKYSTRIKE SQUADRON: returned " + name + " to its owner's hand");
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1039;
        d.def_id = R"RB(rad-075-167)RB";
        d.name = R"RB(Skystrike Squadron)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-075/167)RB";
        d.collector_number = 75;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.energy_cost = 5;
        d.might = 4;
        d.rarity = Rarity::Common;
        d.deflect_value = 1;
        d.keywords.set(Keyword::Deflect);
        d.ability_text = R"RB(You may return a friendly gear to its owner's hand as an additional cost to play me. If you do, I cost [2] less.
[Deflect] (Opponents must pay [A] to choose me with a spell or ability.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/694a362579d3f2e9805cac0da4814ea911423033-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1039(CardRegistry& r) {
    r.registerCard(1039, std::make_unique<SkystrikeSquadron>());
}

}  // namespace riftbound
