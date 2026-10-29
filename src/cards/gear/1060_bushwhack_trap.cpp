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

class BushwhackTrap : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }

    // Facedown capacity is reset to 1 on every aura pass (recalculateAuras);
    // Bandle Tree (273) raises its own battlefield to 2. This aura can run
    // once per trap and in any order with Bandle Tree's, so it writes the
    // whole capacity rather than adding to it: 1, +1 on Bandle Tree, +1 per
    // trap whose controller controls the battlefield.
    static constexpr CardDefId kBandleTreeId_1060 = 273;
    void applyPassiveAura(GameState& state, PlayerId /*controller*/) const override {
        for (auto& bf : state.battlefields) {
            if (!state.objectExists(bf.card_object_id)) continue;
            int n = 0;
            for (const auto& [id, obj] : state.objects) {
                if (obj.card_def_id != cardDefId() || !obj.isGear() || !obj.location.has_value()) continue;
                if (obj.battlefieldId() != std::optional<BattlefieldId>{bf.id}) continue;
                if (bf.controller == std::optional<PlayerId>{obj.controller}) ++n;
            }
            if (n == 0) continue;
            const bool bandle = state.getObject(bf.card_object_id).card_def_id == kBandleTreeId_1060;
            bf.facedown_max_occupancy = std::max(bf.facedown_max_occupancy, 1 + (bandle ? 1 : 0) + n);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1060;
        d.def_id = R"RB(rad-108-167)RB";
        d.name = R"RB(Bushwhack Trap)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-108/167)RB";
        d.collector_number = 108;
        d.artist = R"RB(Fish Art Studio)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Fury};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 1;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
While you or an ally control this battlefield, that player may hide an additional card here.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/120762e23a599ee14d3f88856fcf707154338fb0-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1060(CardRegistry& r) {
    r.registerCard(1060, std::make_unique<BushwhackTrap>());
}

}  // namespace riftbound
