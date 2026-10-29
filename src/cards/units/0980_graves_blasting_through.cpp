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

class GravesBlastingThrough : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When I attack or defend, deal damage equal to my Might to all enemy
    //  units here." Nothing is chosen ("all"), so untargetable units are hit
    // too. Damage is dealt to every unit first, then the lethally damaged
    // ones are killed (no mutation while collecting).
    TriggerType triggerType() const override { return TriggerType::WhenIAttackOrDefend; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        const auto& self = ctx.state.getObject(ctx.source);
        auto my_bf = self.battlefieldId();
        if (!my_bf) return;
        const int dmg = self.current_might;
        if (dmg <= 0) return;
        std::vector<GameObjectId> enemies;
        for (const auto& [id, obj] : ctx.state.objects) {
            if (!obj.isUnit() || obj.controller == ctx.controller) continue;
            auto bf = obj.battlefieldId();
            if (bf && *bf == *my_bf) enemies.push_back(id);
        }
        for (auto id : enemies) ctx.executor.dealDamage(id, dmg, ctx.source);
        for (auto id : enemies)
            if (ctx.state.objectExists(id) && ctx.state.getObject(id).location.has_value() &&
                ctx.state.getObject(id).hasLethalDamage())
                ctx.executor.killObject(id);
        ctx.events.logTrace("GRAVES, BLASTING THROUGH: dealt " + std::to_string(dmg) +
                            " (=Might) to " + std::to_string(enemies.size()) +
                            " enemy unit(s) here");
    }

    // "I don't deal combat damage."
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        for (auto& [id, obj] : state.objects) {
            if (obj.card_def_id != cardDefId() || obj.controller != controller) continue;
            if (!obj.location.has_value()) continue;
            GameObject::AuraEffect ae;
            ae.source = id;
            ae.suppress_combat_damage = true;
            obj.aura_effects.push_back(ae);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 980;
        d.def_id = R"RB(rad-085-167)RB";
        d.name = R"RB(Graves, Blasting Through)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-085/167)RB";
        d.collector_number = 85;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Bilgewater)RB", R"RB(Graves)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When I attack or defend, deal damage equal to my Might to all enemy units here.
I don't deal combat damage.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/5bc0671f9f78630e2932052bb9f7f42ef2742c86-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_980(CardRegistry& r) {
    r.registerCard(980, std::make_unique<GravesBlastingThrough>());
}

}  // namespace riftbound
