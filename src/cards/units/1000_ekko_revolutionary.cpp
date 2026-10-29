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

class EkkoRevolutionary : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "I enter ready." [Assault] is a keyword (engine-handled).
    using Card::entersReadyOnPlay;
    bool entersReadyOnPlay() const override { return true; }

    // "[Action][>] Discard 1, [1][R]: Banish me, then play me to your base,
    //  ignoring my cost."
    bool hasActivatedAbility() const override { return true; }
    bool isActionAbility() const override { return true; }
    ActivationCost getActivationCost() const override {
        ActivationCost c;
        c.discard = true;
        c.discard_count = 1;
        c.energy = 1;
        c.power = 1;
        c.power_domain = Domain::Fury;
        return c;
    }
    void onActivate(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        if (!ctx.state.getObject(ctx.source).location.has_value()) return;   // left the board
        ctx.executor.banishObject(ctx.source);
        auto& me = ctx.state.getObject(ctx.source);
        // playIgnoringCost re-zones it: drop the banishment entry.
        auto& bz = ctx.state.player(me.owner).banishment;
        bz.erase(std::remove(bz.begin(), bz.end(), ctx.source), bz.end());
        // A card that left the board comes back as a new object: no buffs,
        // no "this turn" Might, no combat status.
        me.combat_designation = CombatDesignation::None;
        me.is_stunned = false;
        me.buff_count = 0;
        me.temp_buff_count = 0;
        me.temp_might_bonus = 0;
        ctx.executor.playIgnoringCost(ctx.controller, ctx.source,
                                      LocationId{BaseLocation{ctx.controller}});
        // playIgnoringCost enters units exhausted; "I enter ready".
        auto& back = ctx.state.getObject(ctx.source);
        back.is_exhausted = false;
        back.recomputeMight();
        ctx.events.emit(ObjectStateChangedEvent{ctx.source, "played_not_from_hand"});
        ctx.events.logTrace("EKKO, REVOLUTIONARY: banished and played to base, ready");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1000;
        d.def_id = R"RB(rad-021-167)RB";
        d.name = R"RB(Ekko, Revolutionary)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-021/167)RB";
        d.collector_number = 21;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Fury};
        d.tags = {R"RB(Ekko)RB", R"RB(Zaun)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Epic;
        d.assault_value = 1;
        d.keywords.set(Keyword::Assault);
        d.ability_text = R"RB(I enter ready.
[Assault] (+1 [M] while I'm an attacker.)
[Action][>] Discard 1, [1][R]: Banish me, then play me to your base, ignoring my cost.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/d13964c46506f9f36430dfe91ee02d258a304908-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1000(CardRegistry& r) {
    r.registerCard(1000, std::make_unique<EkkoRevolutionary>());
}

}  // namespace riftbound
