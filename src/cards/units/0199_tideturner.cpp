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

class Tideturner : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true,
                                   .must_be_friendly = true, .optional = true};
    }
    // Units you control somewhere other than where I am (CR 355.4.a).
    std::vector<GameObjectId> enumerateLegalTargets(
        const GameState& state, PlayerId controller) const override {
        std::vector<GameObjectId> out;
        for (auto& [id, obj] : state.objects) {
            if (!obj.isUnit() || obj.controller != controller) continue;
            if (!obj.location.has_value()) continue;
            out.push_back(id);
        }
        return out;
    }
    std::vector<GameObjectId> swappable(const CardContext& ctx) const {
        std::vector<GameObjectId> out;
        if (!ctx.state.objectExists(ctx.source)) return out;
        const auto& self = ctx.state.getObject(ctx.source);
        if (!self.location.has_value()) return out;
        for (auto id : enumerateLegalTargets(ctx.state, ctx.controller)) {
            if (id == ctx.source) continue;
            if (*ctx.state.getObject(id).location == *self.location) continue;
            out.push_back(id);
        }
        return out;
    }
    // "When you play me, you may choose a unit you control at another
    //  location. Move me to its location and it to my original location."
    // A unit's play carries no targets into its trigger, so the unit is chosen
    // here (at finalization, CR 383.3) through pickTarget.
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& targets) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto still_legal = [&]() { return !swappable(ctx).empty(); };
        int conf = confirmOptional(ctx, "Tideturner: swap locations with a unit?", still_legal);
        GameObjectId other = kInvalidId;
        if (conf == 1) {
            other = !targets.empty() ? targets[0]
                  : pickTarget(ctx, "Tideturner: unit to swap with", swappable(ctx));
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (conf != 1) return;
        if (other == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the agent
        if (other == kInvalidId || !ctx.state.objectExists(other)) return;
        const auto& self = ctx.state.getObject(ctx.source);
        const auto& tgt = ctx.state.getObject(other);
        if (!self.location.has_value() || !tgt.location.has_value()) return;
        if (*self.location == *tgt.location) return;
        auto my_loc  = self.location;
        auto tgt_loc = tgt.location;
        moveToLocation(ctx.executor, ctx.source, tgt_loc);
        moveToLocation(ctx.executor, other,      my_loc);
        ctx.events.logTrace("TIDETURNER: swapped locations with " + tgt.name);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 199;
        d.def_id = R"RB(ogn-199-298)RB";
        d.name = R"RB(Tideturner)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-199/298)RB";
        d.collector_number = 199;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Bilgewater)RB"};
        d.energy_cost = 2;
        d.might = 2;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Hidden);
        d.ability_text = R"RB([Hidden] (Hide now for [A] to react with later for [0].)
When you play me, you may choose a unit you control at another location. Move me to its location and it to my original location.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/bad355dba8b32d1fba33dc3924cad6a34b61b5af-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_199(CardRegistry& r) {
    r.registerCard(199, std::make_unique<Tideturner>());
}

} // namespace riftbound
