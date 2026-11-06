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

class BlastCone : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayThis; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play this, you may move an enemy unit." Whether to, the enemy
    // unit and the Move's destination (CR 355.4: its base or a battlefield
    // other than where it is) are decided as the trigger is finalized (CR
    // 383.3). A triggered ability's chain item carries no chosen targets, so
    // the previous version never let its controller choose: it moved the
    // first enemy unit it found, always to its base.
    //
    // "When you move an enemy unit, you may exhaust this to [Stun] it" is
    // only approximated: it is offered for the unit this ability moved (the
    // engine does not track who is responsible for other moves), asked after
    // the move.
    //
    // Resume points: confirmOptional 0-2, pickTarget 6-8, then kAskDest
    // (destination asked) / kDestChosen (destination in resume_data[6]),
    // kAskStun (stun asked) / kDone.
    static constexpr int kAskDest = 20;
    static constexpr int kDestChosen = 21;
    static constexpr int kAskStun = 22;
    static constexpr int kDone = 23;
    static constexpr size_t kDestSlot = 6;

    static std::vector<GameObjectId> movable(const CardContext& ctx) {
        return chooseableUnitsSorted(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller != ctx.controller && !o.cant_be_moved_by_enemy &&
                   !moveDests(ctx.state, o).empty();
        });
    }
    static bool canStun(const CardContext& ctx, GameObjectId moved) {
        if (!ctx.state.objectExists(ctx.source) || !ctx.state.objectExists(moved)) return false;
        const auto& self = ctx.state.getObject(ctx.source);
        const auto& u = ctx.state.getObject(moved);
        return self.location.has_value() && !self.is_exhausted && u.location.has_value();
    }
    static void stun(CardContext& ctx, GameObjectId moved) {
        ctx.executor.exhaustObject(ctx.source);
        ctx.executor.stunUnitBy(moved, ctx.source);
        ctx.events.logTrace("BLAST CONE: exhausted to stun " + ctx.state.getObject(moved).name);
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& ri_opt = ctx.state.chain.resuming;
        if (!ri_opt.has_value()) {
            // Direct invocation (no chain): the first enemy unit, the first
            // destination (its base when it is at a battlefield), and stun it.
            const auto c = movable(ctx);
            if (c.empty()) return;
            const auto dests = moveDests(ctx.state, ctx.state.getObject(c.front()));
            moveTo(ctx, c.front(), dests.front());
            if (canStun(ctx, c.front())) stun(ctx, c.front());
            return;
        }
        auto& ri = *ri_opt;
        if (ri.resume_point == kDone) return;
        if (ri.resume_point == kAskStun) {
            ri.resume_point = kDone;
            const auto moved = static_cast<GameObjectId>(ri.resume_data[2]);
            if (takenValue(ctx, 0) == 1 && canStun(ctx, moved)) stun(ctx, moved);
            return;
        }

        const int go = confirmOptional(ctx, "Blast Cone: move an enemy unit?",
                                       [&]() { return !movable(ctx).empty(); });
        if (go < 1) return;   // waiting on the agent, declined or nothing to move
        const GameObjectId t = pickTarget(ctx, "Blast Cone: move which enemy unit", movable(ctx));
        if (waitingForTarget(ctx, t)) return;
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;   // none, or a mistarget
        if (ri.resume_point < kAskDest) {
            std::vector<Intent> opts;
            for (int d : moveDests(ctx.state, ctx.state.getObject(t)))
                opts.push_back(valChoice(ctx.controller, d, destLabel(d)));
            ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                       "Blast Cone: move " + ctx.state.getObject(t).name + " where?");
            ri.resume_point = kAskDest;
            return;
        }
        if (ri.resume_point == kAskDest) {
            const auto dests = moveDests(ctx.state, ctx.state.getObject(t));
            int d = takenValue(ctx, dests.empty() ? 0 : dests.front());
            if (std::find(dests.begin(), dests.end(), d) == dests.end() && !dests.empty()) d = dests.front();
            while (ri.resume_data.size() <= kDestSlot) ri.resume_data.push_back(0);
            ri.resume_data[kDestSlot] = d;
            ri.resume_point = kDestChosen;
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)

        // The destination must still be one the unit can move to.
        const int d = ri.resume_data.size() > kDestSlot ? ri.resume_data[kDestSlot] : 0;
        const auto dests = moveDests(ctx.state, ctx.state.getObject(t));
        ri.resume_point = kDone;
        if (std::find(dests.begin(), dests.end(), d) == dests.end()) {
            ctx.events.logTrace("BLAST CONE: the chosen destination is no longer valid");
            return;
        }
        moveTo(ctx, t, d);
        ctx.events.logTrace("BLAST CONE: moved " + ctx.state.getObject(t).name + " to " + destLabel(d));
        if (!canStun(ctx, t)) return;
        ctx.executor.requestChoice(ctx.controller,
            {valChoice(ctx.controller, 0, "Don't stun it"),
             valChoice(ctx.controller, 1, "Exhaust Blast Cone to stun it")},
            "Blast Cone: exhaust this to [Stun] the moved unit?");
        ri.resume_point = kAskStun;
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 695;
        d.def_id = R"RB(unl-133-219)RB";
        d.name = R"RB(Blast Cone)RB";
        d.set_code = R"RB(UNL)RB";
        d.set_name = R"RB(Unleashed)RB";
        d.public_code = R"RB(UNL-133/219)RB";
        d.collector_number = 133;
        d.artist = R"RB(黯荧岛Dark Glow)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Chaos};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you play this, you may move an enemy unit.
When you move an enemy unit, you may exhaust this to [Stun] it. (It doesn't deal combat damage this turn.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/093799f1ad34084f05011f3b326d4d32d96849e5-744x1039.png?accountingTag=RB)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_695(CardRegistry& r) {
    r.registerCard(695, std::make_unique<BlastCone>());
}

} // namespace riftbound
