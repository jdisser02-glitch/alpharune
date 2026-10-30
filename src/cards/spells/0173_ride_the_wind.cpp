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

class RideTheWind : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true,
                                   .must_be_friendly = true};
    }
    bool needsPlayTimeTarget() const override { return true; }
    // "Move a friendly unit and ready it." The unit is chosen as the spell is
    // played (pickTarget, resume points 6-8); its controller then chooses the
    // Move's destination (CR 355.4: base or any battlefield other than where
    // it is) at resume points 9 (asked) / 10 (done), with the unit kept in
    // resume_data[3].
    static constexpr int kAskDestination = 9;
    static constexpr int kDone = 10;
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& targets) override {
        auto& ri_opt = ctx.state.chain.resuming;
        if (!ri_opt.has_value()) {
            // Direct invocation (no chain): first target, moved to base.
            GameObjectId picked = !targets.empty() ? targets[0]
                : pickTarget(ctx, "Ride the Wind", enumerateLegalTargets(ctx.state, ctx.controller));
            if (picked == kInvalidId || !ctx.state.objectExists(picked)) return;
            auto dests = moveDests(ctx.state, ctx.state.getObject(picked));
            if (!dests.empty()) moveTo(ctx, picked, dests.front());
            ctx.executor.readyObject(picked);
            return;
        }
        auto& ri = *ri_opt;
        if (ri.resume_point < kAskDestination) {
            GameObjectId picked = kInvalidId;
            if (!targets.empty()) {
                picked = targets[0];
            } else {
                picked = pickTarget(ctx, "Ride the Wind",
                                    enumerateLegalTargets(ctx.state, ctx.controller));
                if (picked == kInvalidId && ri.resume_point == 7) return;   // waiting
            }
            if (picked == kInvalidId || !ctx.state.objectExists(picked)) {
                ri.resume_point = kDone;
                return;
            }
            auto dests = moveDests(ctx.state, ctx.state.getObject(picked));
            if (dests.empty()) {   // nowhere to move: it is still readied
                ri.resume_point = kDone;
                ctx.executor.readyObject(picked);
                return;
            }
            while (ri.resume_data.size() < 4) ri.resume_data.push_back(0);
            ri.resume_data[3] = static_cast<int32_t>(picked);
            std::vector<Intent> opts;
            for (int d : dests) opts.push_back(valChoice(ctx.controller, d, destLabel(d)));
            ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                       "Ride the Wind: move " +
                                       ctx.state.getObject(picked).name + " where?");
            ri.resume_point = kAskDestination;
            return;
        }
        if (ri.resume_point != kAskDestination) return;
        ri.resume_point = kDone;
        if (ri.resume_data.size() < 4) return;
        const auto picked = static_cast<GameObjectId>(ri.resume_data[3]);
        if (!ctx.state.objectExists(picked)) return;
        auto dests = moveDests(ctx.state, ctx.state.getObject(picked));
        if (dests.empty()) { ctx.executor.readyObject(picked); return; }
        int d = takenValue(ctx, dests.front());
        if (std::find(dests.begin(), dests.end(), d) == dests.end()) d = dests.front();
        moveTo(ctx, picked, d);
        ctx.executor.readyObject(picked);
        ctx.events.logTrace("RIDE THE WIND: moved " + ctx.state.getObject(picked).name +
                            " to " + destLabel(d) + " and readied it");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 173;
        d.def_id = R"RB(ogn-173-298)RB";
        d.name = R"RB(Ride the Wind)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-173/298)RB";
        d.collector_number = 173;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Chaos};
        d.energy_cost = 2;
        d.power_cost = 1;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
Move a friendly unit and ready it.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/864d9f8992db7f9a9e795d9438d941ca564ddde7-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_173(CardRegistry& r) {
    r.registerCard(173, std::make_unique<RideTheWind>());
}

} // namespace riftbound
