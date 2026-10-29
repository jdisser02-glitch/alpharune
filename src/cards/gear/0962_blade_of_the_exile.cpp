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

class BladeOfTheExile : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    KeywordSet equippedKeywords() const override { KeywordSet k; k.set(Keyword::Ganking); return k; }

    // "[Equip] — [A] or exhaust a friendly unit." One equip option per way to
    // pay: {unit} pays [A]; {unit, other} exhausts `other` (any ready friendly
    // unit, the equipped unit included).
    bool hasEquipAbility() const override { return true; }
    std::optional<std::vector<std::vector<GameObjectId>>> equipOptions(
        const GameState& state, PlayerId player, GameObjectId /*gear*/) const override {
        std::vector<GameObjectId> units, ready;
        for (const auto& [id, obj] : state.objects) {
            if (!obj.isUnit() || obj.controller != player || !obj.location.has_value()) continue;
            units.push_back(id);
            if (!obj.is_exhausted) ready.push_back(id);
        }
        const bool can_pay_power = payableOnePower(state, player, std::nullopt);
        std::vector<std::vector<GameObjectId>> out;
        for (auto u : units) {
            if (can_pay_power) out.push_back({u});
            for (auto r : ready) out.push_back({u, r});
        }
        return out;
    }
    bool onEquipWith(CardContext& ctx, const std::vector<GameObjectId>& targets) override {
        if (targets.empty() || !ctx.state.objectExists(targets[0])) return false;
        if (targets.size() >= 2) {
            GameObjectId cost_unit = targets[1];
            if (!ctx.state.objectExists(cost_unit)) return false;
            const auto& cu = ctx.state.getObject(cost_unit);
            if (!cu.isUnit() || cu.controller != ctx.controller || cu.is_exhausted ||
                !cu.location.has_value()) return false;
            ctx.events.logTrace("  EQUIP_COST: exhausted " + cu.name);
            ctx.executor.exhaustObject(cost_unit);
        } else if (!payOnePower(ctx, ctx.controller, std::nullopt)) {
            return false;
        }
        attachFree(ctx, ctx.source, targets[0]);
        return true;
    }
    bool onEquip(CardContext& ctx, GameObjectId unit) override {
        return onEquipWith(ctx, std::vector<GameObjectId>{unit});
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 962;
        d.def_id = R"RB(rad-150-167)RB";
        d.name = R"RB(Blade of the Exile)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-150/167)RB";
        d.collector_number = 150;
        d.artist = R"RB(Polar Engine Studio)RB";
        d.card_type = CardType::Gear;
        d.super_type = SuperType::Signature;
        d.domains = {Domain::Calm, Domain::Chaos};
        d.tags = {R"RB(Equipment)RB", R"RB(Riven)RB"};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.might_bonus = 3;
        d.rarity = Rarity::Epic;
        d.keywords.set(Keyword::Equip);
        d.ability_text = R"RB([Equip] — [A] or exhaust a friendly unit. (Pay the cost: Attach this to a unit you control.))RB";
        d.effect_text = R"RB([Ganking] (I can move from battlefield to battlefield.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/04aaded01b23641e496e39f51882ccfe89715492-744x1040.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_962(CardRegistry& r) {
    r.registerCard(962, std::make_unique<BladeOfTheExile>());
}

}  // namespace riftbound
