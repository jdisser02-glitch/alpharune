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
#include "cards/radiance_helpers.h"

namespace riftbound {
namespace {

class HexplosivesExpert : public LegendCard {
public:
    const CardDef& def() const override { return def_; }

    // "[1][A], [E]: Play a Bomb gear token to a battlefield."
    // The Bomb has [Deploy], so any battlefield; the battlefield is picked as
    // the ability resolves (where a token is played is not a target).
    bool hasActivatedAbility() const override { return true; }
    ActivationCost getActivationCost() const override {
        ActivationCost c;
        c.exhaust = true;
        c.energy = 1;
        c.power = 1;
        c.power_any_domain = true;
        return c;
    }
    bool canActivateAbility(const GameState& state, PlayerId /*controller*/) const override {
        return !state.battlefields.empty();
    }
    void onActivate(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const int bf = pickBombBattlefield(*this, ctx, "Hexplosives Expert: Bomb to which battlefield");
        if (bf < 0) return;   // -1 waiting for the agent, -2 no battlefield
        playBombToken(ctx, ctx.controller, static_cast<BattlefieldId>(bf));
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1085;
        d.def_id = R"RB(rad-141-167)RB";
        d.name = R"RB(Hexplosives Expert)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-141/167)RB";
        d.collector_number = 141;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Fury, Domain::Chaos};
        d.tags = {R"RB(Yordle)RB", R"RB(Ziggs)RB"};
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([1][A], [E]: Play a Bomb gear token to a battlefield. (It has [Deploy]. When it dies, deal 2 to an enemy there and kill your other Bombs there.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/69899f4d5e05ff0a060b9f38894cd77f9e17d195-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1085(CardRegistry& r) {
    r.registerCard(1085, std::make_unique<HexplosivesExpert>());
}

}  // namespace riftbound
