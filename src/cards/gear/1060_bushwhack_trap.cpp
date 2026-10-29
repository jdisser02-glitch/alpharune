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

    // [Deploy] is engine-handled through deploys().
    // "While you or an ally control this battlefield, that player may hide an
    //  additional card here."
    // Raises the battlefield's facedown capacity by one per Bushwhack Trap
    // there whose controller controls the battlefield (only the battlefield's
    // controller may hide there, so the extra slot is theirs; 1v1 has no
    // allies). The amount this card family added is remembered on the
    // battlefield's card object so each aura pass replaces it rather than
    // adding again: "__bwt_added" (slots added) and "__bwt_written" (the
    // capacity last written). If the capacity was changed by anything else
    // since (an engine reset, another card), that value is taken as the base.
    //
    // ENGINE-GAP: recalculateAuras never resets
    // BattlefieldState::facedown_max_occupancy, and a card's passive aura runs
    // only while it is on the board. So the extra slot is withdrawn when the
    // controller loses the battlefield (the trap is still there to run), but
    // lingers after the last Bushwhack Trap leaves the board until another
    // one's aura runs. Adding `bf.facedown_max_occupancy = 1;` to the per-BF
    // reset in recalculateAuras fixes that, and this logic keeps working with
    // it (Bandle Tree re-raises its own each pass).
    void applyPassiveAura(GameState& state, PlayerId /*controller*/) const override {
        for (auto& bf : state.battlefields) {
            if (!state.objectExists(bf.card_object_id)) continue;
            int n = 0;
            for (const auto& [id, obj] : state.objects) {
                if (obj.card_def_id != cardDefId() || !obj.isGear() || !obj.location.has_value()) continue;
                if (obj.battlefieldId() != std::optional<BattlefieldId>{bf.id}) continue;
                if (bf.controller == std::optional<PlayerId>{obj.controller}) ++n;
            }
            auto& counters = state.getObject(bf.card_object_id).card_counters;
            auto wit = counters.find("__bwt_written");
            auto ait = counters.find("__bwt_added");
            const int written = wit != counters.end() ? wit->second : 0;
            const int added = ait != counters.end() ? ait->second : 0;
            if (n == 0 && added == 0) continue;
            const int base = (written > 0 && bf.facedown_max_occupancy == written)
                ? written - added : bf.facedown_max_occupancy;
            bf.facedown_max_occupancy = std::max(1, base + n);
            counters["__bwt_added"] = n;
            counters["__bwt_written"] = bf.facedown_max_occupancy;
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
