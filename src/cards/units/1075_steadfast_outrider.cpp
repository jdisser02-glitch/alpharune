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

class SteadfastOutrider : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // [Tank] is the keyword (engine-handled).
    // "While there are 2 or more other friendly units and/or gear at my
    //  battlefield, I have +2 [M]." Counts every other unit or gear the
    //  controller has at my battlefield (attached Equipment included — it is
    //  at the battlefield too). Nothing while I'm in my base.
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        forEachSelfOnce(*this, state, controller, [&](GameObjectId sid, GameObject& self) {
            const auto here = self.battlefieldId();
            if (!here.has_value()) return;
            int others = 0;
            for (const auto& [id, obj] : state.objects) {
                if (id == sid || obj.controller != controller) continue;
                if (!obj.isUnit() && !obj.isGear()) continue;
                if (obj.battlefieldId() != here) continue;
                ++others;
            }
            if (others >= 2) auraMight(self, sid, 2);
        });
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1075;
        d.def_id = R"RB(rad-128-167)RB";
        d.name = R"RB(Steadfast Outrider)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-128/167)RB";
        d.collector_number = 128;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Order};
        d.energy_cost = 4;
        d.might = 3;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Tank);
        d.ability_text = R"RB([Tank] (I must be assigned combat damage first.)
While there are 2 or more other friendly units and/or gear at my battlefield, I have +2 [M].)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/efa8d58e65b1b1f804d37bb51bb67ea52e0b3205-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1075(CardRegistry& r) {
    r.registerCard(1075, std::make_unique<SteadfastOutrider>());
}

}  // namespace riftbound
