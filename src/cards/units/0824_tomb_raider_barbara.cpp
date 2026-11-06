#include "cards/card.h"
#include "cards/card_helpers.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace riftbound {
namespace {

class TombRaiderBarbara : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "When you play me, if you control 7 or more runes, choose an enemy gear.
    //  If it's [Empowered], disempower it. Otherwise, kill it."
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    std::vector<GameObjectId> enemyGear(CardContext& ctx) const {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : ctx.state.objects) {
            if (!obj.isAnyGear() || obj.controller == ctx.controller) continue;
            if (!obj.location.has_value() || obj.untargetable_by_enemy) continue;
            out.push_back(id);
        }
        return out;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (runesControlled(ctx.state, ctx.controller) < 7) return;
        GameObjectId tgt = pickTarget(ctx, "Tomb-Raider Barbara: choose an enemy gear",
                                      enemyGear(ctx));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (tgt == kInvalidId || !ctx.state.objectExists(tgt)) return;
        auto& gear = ctx.state.getObject(tgt);
        if (!gear.isAnyGear() || !gear.location.has_value()) return;
        if (gear.isEmpowered()) {
            ctx.executor.disempower(tgt);
            ctx.events.logTrace("TOMB-RAIDER BARBARA: disempowered " + gear.name);
        } else {
            ctx.events.logTrace("TOMB-RAIDER BARBARA: killed " + gear.name);
            ctx.executor.killObject(tgt);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 824;
        d.def_id = R"RB(ven-037-166)RB";
        d.name = R"RB(Tomb-Raider Barbara)RB";
        d.set_code = R"RB(VEN)RB";
        d.set_name = R"RB(Vendetta)RB";
        d.public_code = R"RB(VEN-037/166)RB";
        d.collector_number = 37;
        d.artist = R"RB(Caravan Studio)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Shurima)RB"};
        d.energy_cost = 4;
        d.might = 4;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you play me, if you control 7 or more runes, choose an enemy gear. If it's [Empowered], disempower it. Otherwise, kill it.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/31d0c16ecaa0bd8522d13d6dd5d940be8111f118-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_824(CardRegistry& r) {
    r.registerCard(824, std::make_unique<TombRaiderBarbara>());
}

}  // namespace riftbound
