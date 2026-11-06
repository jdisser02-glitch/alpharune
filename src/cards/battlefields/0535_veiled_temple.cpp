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

// "When you conquer here, you may ready a friendly gear. If it's an Equipment,
//  you may detach it."

class VeiledTemple : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // The friendly gear (a unit gear counts) is chosen as the trigger is
    // finalized (CR 383.3). Readying it never hurts, so the "you may" of the
    // ready is always taken; whether to detach an attached Equipment is asked
    // as the ability resolves, after the ready (resume points kAskDetach /
    // kDone, past pickTarget's 6-8).
    // (A triggered ability's chain item carries no chosen targets, so the
    // previous version, which read the gear from `targets`, never did
    // anything.)
    static constexpr int kAskDetach = 20;
    static constexpr int kDone = 21;
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId gear = pickTarget(ctx, "Veiled Temple: ready a friendly gear",
                                             friendlyGear(ctx.state, ctx.controller));
        if (gear == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (gear == kInvalidId || !ctx.state.objectExists(gear)) return;
        if (!ctx.state.chain.resuming.has_value()) {   // direct invocation: ready only
            ctx.executor.readyObject(gear);
            return;
        }
        auto& ri = *ctx.state.chain.resuming;
        if (ri.resume_point < kAskDetach) {
            ctx.executor.readyObject(gear);
            ctx.events.logTrace("VEILED TEMPLE: readied " + ctx.state.getObject(gear).name);
            const auto& g = ctx.state.getObject(gear);
            if (!isEquipment(g) || !g.attached_to.has_value()) { ri.resume_point = kDone; return; }
            ctx.executor.requestChoice(ctx.controller,
                {valChoice(ctx.controller, 0, "Keep it attached"),
                 valChoice(ctx.controller, 1, "Detach it")},
                "Veiled Temple: detach the Equipment?");
            ri.resume_point = kAskDetach;
            return;
        }
        if (ri.resume_point == kAskDetach) {
            ri.resume_point = kDone;
            if (takenValue(ctx, 0) != 1) return;
            const auto& g = ctx.state.getObject(gear);
            if (!g.attached_to.has_value()) return;
            ctx.executor.unattachGear(gear);
            ctx.events.logTrace("VEILED TEMPLE: detached " + g.name);
        }
    }
    TriggerType triggerType() const override { return TriggerType::WhenYouConquerHere; }
    static std::vector<GameObjectId> friendlyGear(const GameState& state, PlayerId me) {
        std::vector<GameObjectId> out;
        for (const auto& [id, o] : state.objects)
            if (o.isAnyGear() && o.controller == me && o.location.has_value()) out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 535;
        d.def_id = R"RB(sfd-221-221)RB";
        d.name = R"RB(Veiled Temple)RB";
        d.set_code = R"RB(SFD)RB";
        d.set_name = R"RB(Spiritforged)RB";
        d.public_code = R"RB(SFD-221/221)RB";
        d.collector_number = 221;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you conquer here, you may ready a friendly gear. If it's an Equipment, you may detach it.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/6f2b38874a09b3e3df3fe584ea77e84aa5423e37-1039x744.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_535(CardRegistry& r) {
    r.registerCard(535, std::make_unique<VeiledTemple>());
}

} // namespace riftbound
