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

class BlackMarket : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }

    // "[A][A]: Draw 1. Each player may use this ability once on their turn
    //  (even if they don't control this battlefield)."
    // A battlefield card is not on the board, so the engine never offers its
    // own activated abilities. The ability goes through the aura-granted
    // ability pipeline instead (as Forge of the Fluft does): while it is the
    // turn player's turn and they have not used it yet this turn, the turn
    // player's legend carries it. Using it pays [A][A] and draws 1. The engine
    // records each use on the bearer ("__ability_used_turn_<index>" = turn + 1,
    // set as the ability is played, so a countered use still counts).
    //
    // Indices 3 and 2 are used (one per Black Market in play, in battlefield
    // order) so the granted ability does not share its action-vocab slot or
    // its once-per-turn record with the legend's own abilities 0 and 1.
    static constexpr int kSlots[2] = {3, 2};

    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility a;
        a.cost.power = 2;
        a.cost.power_any_domain = true;
        return {a, a, a, a};
    }

    void onActivate(CardContext& ctx, int /*ability_index*/,
                    const std::vector<GameObjectId>& /*targets*/) override {
        ctx.executor.drawCards(ctx.controller, 1);
        ctx.events.logTrace("BLACK MARKET: paid [A][A] -> draw 1");
    }

    void applyPassiveAura(GameState& state, PlayerId /*controller*/) const override {
        const PlayerId p = state.turn.turn_player;
        if (p != PlayerId::Player1 && p != PlayerId::Player2) return;
        const auto legend = state.player(p).legend_zone;
        if (legend == kInvalidId || !state.objectExists(legend)) return;
        auto& bearer = state.getObject(legend);
        int k = 0;
        for (const auto& bf : state.battlefields) {
            if (!state.objectExists(bf.card_object_id)) continue;
            if (state.getObject(bf.card_object_id).card_def_id != cardDefId()) continue;
            if (k >= 2) break;
            const int idx = kSlots[k++];
            // Several Black Markets each grant their own index on every aura
            // pass; only grant this one once.
            bool granted = false;
            for (const auto& g : bearer.granted_abilities)
                if (g.source_def_id == cardDefId() && g.ability_index == idx) granted = true;
            if (granted) continue;
            auto used = bearer.card_counters.find("__ability_used_turn_" + std::to_string(idx));
            if (used != bearer.card_counters.end() && used->second == state.turn.turn_number + 1)
                continue;   // used this turn
            bearer.granted_abilities.push_back({cardDefId(), idx});
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1100;
        d.def_id = R"RB(rad-158-167)RB";
        d.name = R"RB(Black Market)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-158/167)RB";
        d.collector_number = 158;
        d.artist = R"RB(Fish Art Studio)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB([A][A]: Draw 1. Each player may use this ability once on their turn (even if they don't control this battlefield).)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/9a02ae108f32a8b88be47a94b5f0d39b5a06a6a1-1039x744.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1100(CardRegistry& r) {
    r.registerCard(1100, std::make_unique<BlackMarket>());
}

}  // namespace riftbound
