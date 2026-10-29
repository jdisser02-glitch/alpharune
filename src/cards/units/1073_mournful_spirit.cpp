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

class MournfulSpirit : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "[Deathknell][>] You may kill a unit or gear here."
    // "Here" is where I died (GameObject::last_location: a battlefield or a
    // base). The "you may" and the unit / gear are chosen as the ability is
    // finalized; enemy objects that can't be chosen by my spells and
    // abilities are left out, and [Deflect] is paid through pickTarget.
    TriggerType triggerType() const override { return TriggerType::WhenIDie; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        int go = confirmOptional(ctx, "Mournful Spirit: kill a unit or gear here?",
                                 [&]() { return !killable(ctx).empty(); });
        GameObjectId victim = kInvalidId;
        if (go == 1) victim = pickTarget(ctx, "Mournful Spirit: unit or gear to kill", killable(ctx));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (go != 1) return;
        if (victim == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the agent
        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        const auto here = hereOf(ctx);
        const auto& v = ctx.state.getObject(victim);
        if (!here.has_value() || !v.location.has_value() || *v.location != *here) return;
        ctx.events.logTrace("MOURNFUL SPIRIT: deathknell -> killed " + v.name);
        ctx.executor.killObject(victim);
    }
private:
    static std::optional<LocationId> hereOf(const CardContext& ctx) {
        if (!ctx.state.objectExists(ctx.source)) return std::nullopt;
        const auto& self = ctx.state.getObject(ctx.source);
        return self.location.has_value() ? self.location : self.last_location;
    }
    static std::vector<GameObjectId> killable(const CardContext& ctx) {
        std::vector<GameObjectId> out;
        const auto here = hereOf(ctx);
        if (!here.has_value()) return out;
        for (const auto& [id, obj] : ctx.state.objects) {
            if (id == ctx.source || !(obj.isUnit() || obj.isGear())) continue;
            if (!obj.location.has_value() || *obj.location != *here) continue;
            if (obj.controller != ctx.controller && obj.untargetable_by_enemy) continue;
            out.push_back(id);
        }
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1073;
        d.def_id = R"RB(rad-126-167)RB";
        d.name = R"RB(Mournful Spirit)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-126/167)RB";
        d.collector_number = 126;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Order};
        d.tags = {R"RB(Spirit)RB"};
        d.energy_cost = 4;
        d.might = 1;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Deathknell);
        d.ability_text = R"RB([Deathknell][>] You may kill a unit or gear here. (When I die, get the effect.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/6d8500ac7efc5382985f566212d88ac8f41c135d-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1073(CardRegistry& r) {
    r.registerCard(1073, std::make_unique<MournfulSpirit>());
}

}  // namespace riftbound
