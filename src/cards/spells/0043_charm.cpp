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

class Charm : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true,
                                   .must_be_enemy = true};
    }
    bool needsPlayTimeTarget() const override { return true; }
    // "Move an enemy unit." Enemy units an enemy effect may choose and move,
    // with somewhere to go.
    std::vector<GameObjectId> enumerateLegalTargets(const GameState& state,
                                                    PlayerId controller) const override {
        auto out = chooseableUnits(state, controller, [&](const GameObject& o) {
            return o.controller != controller && !o.cant_be_moved_by_enemy &&
                   !moveDests(state, o).empty();
        });
        std::sort(out.begin(), out.end());
        return out;
    }
    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        return !enumerateLegalTargets(state, controller).empty();
    }
    // The unit is chosen as the spell is played (pickTarget, resume points
    // 6-8); Charm's controller then chooses the Move's destination (CR 355.4:
    // its base or a battlefield other than where it is — e.g. pull a unit out
    // of its base into a fight) at resume points 9 (asked) / 10 (done), with
    // the unit kept in resume_data[3]. Same shape as Ride the Wind.
    static constexpr int kAskDestination = 9;
    static constexpr int kDone = 10;
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& targets) override {
        auto& ri_opt = ctx.state.chain.resuming;
        if (!ri_opt.has_value()) {
            // Direct invocation (no chain): first target, first destination.
            GameObjectId picked = !targets.empty() ? targets[0]
                : pickTarget(ctx, "Charm", enumerateLegalTargets(ctx.state, ctx.controller));
            if (picked == kInvalidId || !ctx.state.objectExists(picked)) return;
            auto dests = moveDests(ctx.state, ctx.state.getObject(picked));
            if (!dests.empty()) moveTo(ctx, picked, dests.front());
            return;
        }
        auto& ri = *ri_opt;
        if (ri.resume_point < kAskDestination) {
            GameObjectId picked = kInvalidId;
            if (!targets.empty()) {
                picked = targets[0];
            } else {
                picked = pickTarget(ctx, "Charm",
                                    enumerateLegalTargets(ctx.state, ctx.controller));
                if (picked == kInvalidId && ri.resume_point == 7) return;   // waiting
            }
            if (picked == kInvalidId || !ctx.state.objectExists(picked)) {
                ri.resume_point = kDone;
                return;
            }
            auto dests = moveDests(ctx.state, ctx.state.getObject(picked));
            if (dests.empty()) { ri.resume_point = kDone; return; }
            while (ri.resume_data.size() < 4) ri.resume_data.push_back(0);
            ri.resume_data[3] = static_cast<int32_t>(picked);
            std::vector<Intent> opts;
            for (int d : dests) opts.push_back(valChoice(ctx.controller, d, destLabel(d)));
            ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                       "Charm: move " + ctx.state.getObject(picked).name +
                                       " where?");
            ri.resume_point = kAskDestination;
            return;
        }
        if (ri.resume_point != kAskDestination) return;
        ri.resume_point = kDone;
        if (ri.resume_data.size() < 4) return;
        const auto picked = static_cast<GameObjectId>(ri.resume_data[3]);
        if (!ctx.state.objectExists(picked)) return;
        auto dests = moveDests(ctx.state, ctx.state.getObject(picked));
        if (dests.empty()) return;
        int d = takenValue(ctx, dests.front());
        if (std::find(dests.begin(), dests.end(), d) == dests.end()) d = dests.front();
        moveTo(ctx, picked, d);
        ctx.events.logTrace("CHARM: moved enemy " + ctx.state.getObject(picked).name +
                            " to " + destLabel(d));
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 43;
        d.def_id = R"RB(ogn-043-298)RB";
        d.name = R"RB(Charm)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-043/298)RB";
        d.collector_number = 43;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Calm};
        d.energy_cost = 1;
        d.power_cost = 1;
        d.ability_text = R"RB(Move an enemy unit.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/62ae564505db8fcba70605eac2083ac2d4397b5a-744x1039.png?accountingTag=RB)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_43(CardRegistry& r) {
    r.registerCard(43, std::make_unique<Charm>());
}

} // namespace riftbound
