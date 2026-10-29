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
#include "cards/radiance_helpers.h"   // Bomb gear token: pickBombBattlefield / playBombToken

namespace riftbound {
namespace {

class ZiggsShortFused : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // Two triggers: "When you play me" (the Bomb) and WhenIDie (ends the
    // Bonus Damage below as I leave).
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayMe, TriggerType::WhenIDie};
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::WhenIDie) {
            // I left the board: friendly gear (on the board or with a pending
            // death ability) loses my Bonus Damage now.
            setGearBonus(ctx.state, ctx.controller, /*on_board_only=*/false);
            ctx.events.logTrace("ZIGGS, SHORT-FUSED: left the board, gear Bonus Damage recomputed");
            return;
        }
        // "When you play me, play a Bomb gear token to a battlefield." The Bomb
        // has [Deploy], so any battlefield; the controller picks it.
        const int bf = pickBombBattlefield(*this, ctx, "Ziggs, Short-Fused: Bomb to which battlefield");
        if (bf < 0) return;   // -1 waiting for the agent, -2 no battlefield
        playBombToken(ctx, ctx.controller, static_cast<BattlefieldId>(bf));
        ctx.events.logTrace("ZIGGS, SHORT-FUSED: Bomb token to BF#" + std::to_string(bf));
    }

    // "Abilities of friendly gear deal 1 Bonus Damage."
    // APPROXIMATION: the engine has no source-type Bonus Damage hook, but
    // EffectExecutor::dealDamage adds the SOURCE object's spell_bonus_damage to
    // spell/ability damage, and a gear is the source only of its own abilities
    // (never of combat damage). So every aura pass sets each on-board gear's
    // spell_bonus_damage to the number of Ziggs, Short-Fused its controller has
    // on the board (instances sum, CR 714). A gear that dies keeps the value it
    // had, so its death ability (a Bomb's) still gets the bonus. When Ziggs
    // dies its WhenIDie trigger recomputes the value everywhere. Gap: if Ziggs
    // leaves the board any other way (returned to hand, banished, recycled)
    // the bonus lingers on friendly gear until another Ziggs arrives or dies.
    void applyPassiveAura(GameState& state, PlayerId /*controller*/) const override {
        setGearBonus(state, PlayerId::Player1, /*on_board_only=*/true);
        setGearBonus(state, PlayerId::Player2, /*on_board_only=*/true);
    }
private:
    static constexpr const char* kMark = "__ziggs_gear_bonus";

    static void setGearBonus(GameState& state, PlayerId controller, bool on_board_only) {
        int n = 0;
        for (const auto& [id, o] : state.objects)
            if (o.card_def_id == 1001 && o.isUnit() && o.controller == controller &&
                o.location.has_value())
                ++n;
        for (auto& [id, g] : state.objects) {
            if (!g.isGear() || g.controller != controller) continue;
            const auto it = g.card_counters.find(kMark);
            const bool marked = it != g.card_counters.end();
            if (g.location.has_value() || (!on_board_only && marked)) {
                if (n == 0 && !marked) continue;   // never touched by Ziggs
                g.spell_bonus_damage = n;
                g.card_counters[kMark] = n;
            }
        }
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1001;
        d.def_id = R"RB(rad-023-167)RB";
        d.name = R"RB(Ziggs, Short-Fused)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-023/167)RB";
        d.collector_number = 23;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Fury};
        d.tags = {R"RB(Bandle City)RB", R"RB(Ziggs)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.might = 5;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(When you play me, play a Bomb gear token to a battlefield. (It has [Deploy]. When it dies, deal 2 to an enemy there and kill your other Bombs there.)
Abilities of friendly gear deal 1 Bonus Damage.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/9df0d347bcfc0593cb724258ee07a1208c620848-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1001(CardRegistry& r) {
    r.registerCard(1001, std::make_unique<ZiggsShortFused>());
}

}  // namespace riftbound
