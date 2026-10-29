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

class BannerOfConquest : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool usesTextAuras() const override { return false; }  // aura is in code below
    // [Deploy] (CR 830): the engine restricts its play to battlefields, keeps
    // it there through cleanups and kills it when an opponent holds here.
    bool deploys() const override { return true; }

    // "Friendly units here have [Assault]." (+1 [M] while attacking; auraKeyword
    // carries the conditional Might.)
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        forEachSelfOnce(*this, state, controller, [&](GameObjectId sid, GameObject& self) {
            if (self.attached_to.has_value()) return;
            const auto here = self.battlefieldId();
            if (!here) return;
            for (auto& [id, u] : state.objects) {
                if (!u.isUnit() || u.controller != self.controller) continue;
                if (u.battlefieldId() != here) continue;
                auraKeyword(u, sid, Keyword::Assault, 1);
            }
        });
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 986;
        d.def_id = R"RB(rad-002-167)RB";
        d.name = R"RB(Banner of Conquest)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-002/167)RB";
        d.collector_number = 2;
        d.artist = R"RB(Fish Art Studio)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Fury};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 1;
        d.power_cost = 1;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
Friendly units here have [Assault]. (+1 [M] while they're attackers.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/1e2a4e0a8cf533c82e9c3f4d507dd7eb63ece943-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_986(CardRegistry& r) {
    r.registerCard(986, std::make_unique<BannerOfConquest>());
}

}  // namespace riftbound
