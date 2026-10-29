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

class JarvanIVRoyalHeir : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // [Assault] and [Tank] are keywords (engine-handled).
    // "When I attack, you may move a unit you control to my battlefield."
    // The "you may" and the unit are chosen as the ability is finalized; the
    // move happens if I'm still at a battlefield and the unit isn't already
    // there. The cleanup after the move gives it the Attacker designation
    // (CR 466.2.c.3.a).
    TriggerType triggerType() const override { return TriggerType::WhenIAttack; }

    static std::vector<GameObjectId> movable(const CardContext& ctx) {
        std::vector<GameObjectId> out;
        if (!ctx.state.objectExists(ctx.source)) return out;
        const auto here = ctx.state.getObject(ctx.source).battlefieldId();
        if (!here.has_value()) return out;
        for (const auto& [id, obj] : ctx.state.objects) {
            if (id == ctx.source || !obj.isUnit() || obj.controller != ctx.controller) continue;
            if (!obj.location.has_value() || obj.battlefieldId() == here) continue;
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        int go = confirmOptional(ctx, "Jarvan IV: move a unit you control to my battlefield?",
                                 [&]() { return !movable(ctx).empty(); });
        if (go == -1) return;                 // waiting for the agent
        GameObjectId unit = kInvalidId;
        if (go == 1) unit = pickTarget(ctx, "Jarvan IV: unit to move", movable(ctx));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (go != 1 || unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        if (!ctx.state.objectExists(ctx.source)) return;
        const auto here = ctx.state.getObject(ctx.source).battlefieldId();
        const auto& u = ctx.state.getObject(unit);
        if (!here.has_value() || !u.location.has_value() || u.battlefieldId() == here) return;
        ctx.executor.moveToBattlefield(unit, *here);
        ctx.events.logTrace("JARVAN IV, ROYAL HEIR: moved " + u.name + " to my battlefield");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1079;
        d.def_id = R"RB(rad-133-167)RB";
        d.name = R"RB(Jarvan IV, Royal Heir)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-133/167)RB";
        d.collector_number = 133;
        d.artist = R"RB(黯荧岛Dark Glow)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Order};
        d.tags = {R"RB(Demacia)RB", R"RB(Jarvan IV)RB"};
        d.energy_cost = 7;
        d.power_cost = 1;
        d.might = 7;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Assault);
        d.assault_value = 1;
        d.keywords.set(Keyword::Tank);
        d.ability_text = R"RB([Assault] (+1 [M] while I'm attacking.)
[Tank] (I must be assigned combat damage first.)
When I attack, you may move a unit you control to my battlefield.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/967de7aa18d5833896825bfd7d78b234e088ae73-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1079(CardRegistry& r) {
    r.registerCard(1079, std::make_unique<JarvanIVRoyalHeir>());
}

}  // namespace riftbound
