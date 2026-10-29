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

// RECONSTRUCTED TEXT: this card is not in cards/card_index.json. Its data and
// ability text were reconstructed from web-search summaries (confidence:
// medium). Notes: effect_text (granted to the equipped unit, repo
// convention) = [Tank]. might_bonus = 1 comes from a single source (Hextech
// Analytics "Might: 1") — low confidence. riftbound.zone lists the domain as
// Colorless; every other source says Order.
class ProtectorsShield : public GearCard {
public:
    const CardDef& def() const override { return def_; }

    // Effect text: "[Tank] (I must be assigned combat damage first.)"
    KeywordSet equippedKeywords() const override { KeywordSet k; k.set(Keyword::Tank); return k; }

    // [Quick-Draw] is the engine keyword (with [Reaction]): played as a
    // Reaction targeting a friendly unit and attached to it on resolution.
    // "[Equip] [1]": pay [1], attach to a unit you control. Offered only when
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
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1124;
        d.def_id = R"RB(rad-123-167)RB";
        d.name = R"RB(Protector's Shield)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-123/167)RB";
        d.collector_number = 123;
        d.card_type = CardType::Gear;
        d.domains = {Domain::Order};
        d.tags = {R"RB(Equipment)RB"};
        d.energy_cost = 2;
        d.power_cost = 1;
        d.might_bonus = 1;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Equip);
        d.keywords.set(Keyword::QuickDraw);
        d.keywords.set(Keyword::Reaction);
        d.ability_text = R"RB([Quick-Draw] (This has [Reaction]. When you play it, attach it to a unit you control.)
[Equip] [1] ([1]: Attach this to a unit you control.))RB";
        d.effect_text = R"RB([Tank] (I must be assigned combat damage first.))RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1124(CardRegistry& r) {
    r.registerCard(1124, std::make_unique<ProtectorsShield>());
}

}  // namespace riftbound
