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

class PackedAmphitheater : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }

    // "Champion units here have +1 [M]." (Any player's.)
    void applyPassiveAura(GameState& state, PlayerId /*controller*/) const override {
        for (const auto& bf : state.battlefields) {
            if (!state.objectExists(bf.card_object_id)) continue;
            if (state.getObject(bf.card_object_id).card_def_id != cardDefId()) continue;
            for (auto& [uid, u] : state.objects) {
                if (!u.isUnit() || !u.isChampion()) continue;
                if (u.battlefieldId() != std::optional<BattlefieldId>{bf.id}) continue;
                bool already = false;   // the card is asked once per copy in play
                for (const auto& ae : u.aura_effects)
                    if (ae.source == bf.card_object_id && ae.might_bonus == 1) already = true;
                if (!already) auraMight(u, bf.card_object_id, 1);
            }
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1105;
        d.def_id = R"RB(rad-164-167)RB";
        d.name = R"RB(Packed Amphitheater)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-164/167)RB";
        d.collector_number = 164;
        d.artist = R"RB(Grafit Studio)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(Champion units here have +1 [M].)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/f4b396780dd7a5aa1be43f763265f308ee0fe665-1039x744.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1105(CardRegistry& r) {
    r.registerCard(1105, std::make_unique<PackedAmphitheater>());
}

}  // namespace riftbound
