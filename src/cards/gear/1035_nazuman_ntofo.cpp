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

class NazumanNtofo : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    // Effect text: "[Shield 2] (+2 [M] while I'm a defender.)"
    int equippedShield() const override { return 2; }
    KeywordSet equippedKeywords() const override { KeywordSet k; k.set(Keyword::Shield); return k; }

    // "Equip [1]": pay [1], attach to a unit you control. Offered only when
    // the [1] can be paid (one equip intent per friendly unit).
    bool hasEquipAbility() const override { return true; }
    std::optional<std::vector<std::vector<GameObjectId>>> equipOptions(
        const GameState& state, PlayerId player, GameObjectId /*gear*/) const override {
        std::vector<std::vector<GameObjectId>> out;
        if (payableEnergy(state, player) < 1) return out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == player && obj.location.has_value())
                out.push_back({id});
        return out;
    }
    bool onEquip(CardContext& ctx, GameObjectId unit) override {
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return false;
        const auto& u = ctx.state.getObject(unit);
        if (!u.isUnit() || u.controller != ctx.controller || !u.location.has_value()) return false;
        if (!payEnergy(ctx, ctx.controller, 1)) return false;
        attachFree(ctx, ctx.source, unit);
        return true;
    }
private:
    // The card index lists no tags; the card has [Equip], an effect text and a
    // Might bonus, i.e. it is an Equipment (CR 150), so the tag is added.
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1035;
        d.def_id = R"RB(rad-071-167)RB";
        d.name = R"RB(Nazuman Ntofo)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-071/167)RB";
        d.collector_number = 71;
        d.card_type = CardType::Gear;
        d.domains = {Domain::Order};
        d.tags = {R"RB(Equipment)RB"};
        d.energy_cost = 1;
        d.might_bonus = 0;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Equip);
        d.ability_text = R"RB(Equip [1] ([1]: Attach this to a unit you control.))RB";
        d.effect_text = R"RB([Shield 2] (+2 [M] while I'm a defender.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/9d518a6db3ae174096b09cf80cece2997f685ede-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1035(CardRegistry& r) {
    r.registerCard(1035, std::make_unique<NazumanNtofo>());
}

}  // namespace riftbound
