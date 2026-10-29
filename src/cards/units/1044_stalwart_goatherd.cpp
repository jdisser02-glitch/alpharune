#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "cards/card_helpers.h"

namespace riftbound {
namespace {

class StalwartGoatherd : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // ([Shield 2] is the engine keyword.)
    // "When you play me, you may kill a gear with Energy cost [4] or less."
    // Any gear on the board, friendly or enemy; the "you may" and the gear
    // are chosen as the ability is finalized.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        int go = confirmOptional(ctx, "Stalwart Goatherd: kill a gear with Energy cost [4] or less?",
                                 [&]() { return !killableGear(ctx).empty(); });
        GameObjectId gear = kInvalidId;
        if (go == 1) gear = pickTarget(ctx, "Stalwart Goatherd: gear to kill", killableGear(ctx));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (go != 1) return;
        if (gear == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the agent
        if (gear == kInvalidId || !ctx.state.objectExists(gear)) return;
        ctx.events.logTrace("STALWART GOATHERD: killed " + ctx.state.getObject(gear).name);
        ctx.executor.killObject(gear);
    }
private:
    static std::vector<GameObjectId> killableGear(const CardContext& ctx) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : ctx.state.objects) {
            if (!obj.isGear() || !obj.location.has_value()) continue;
            if (obj.controller != ctx.controller && obj.untargetable_by_enemy) continue;
            if (energyCost(ctx, obj) <= 4) out.push_back(id);
        }
        return out;
    }
    // Printed Energy cost; a token gear without a card costs 0.
    static int energyCost(const CardContext& ctx, const GameObject& obj) {
        if (obj.card_def_id == kInvalidId) return 0;
        try { return ctx.executor.cardDB().get(obj.card_def_id).energy_cost; }
        catch (const std::out_of_range&) { return 0; }
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1044;
        d.def_id = R"RB(rad-083-167)RB";
        d.name = R"RB(Stalwart Goatherd)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-083/167)RB";
        d.collector_number = 83;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.energy_cost = 4;
        d.might = 2;
        d.rarity = Rarity::Uncommon;
        d.shield_value = 2;
        d.keywords.set(Keyword::Shield);
        d.ability_text = R"RB([Shield 2] (+2 [M] while I'm a defender.)
When you play me, you may kill a gear with Energy cost [4] or less.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/bff85727b58017cbd3921fd73e9b363f5a0205e4-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1044(CardRegistry& r) {
    r.registerCard(1044, std::make_unique<StalwartGoatherd>());
}

}  // namespace riftbound
