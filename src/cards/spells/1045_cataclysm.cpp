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

class Cataclysm : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "Opponents can't play spells this turn. Move a friendly unit to an
    //  occupied enemy battlefield."
    // An occupied enemy battlefield is one where an opponent has units (CR
    // 170.11.a). The battlefield (pickMode, offered only where some friendly
    // unit not already there could go) and then the unit (pickTarget) are
    // chosen as the spell is finalized. On resolution the lockout always
    // applies; the move happens if the battlefield still has enemy units and
    // the unit is still a legal choice (else it is a mistarget).
    // ENGINE GAP: PlayerState::cant_play_spells_this_turn is honoured by the
    // hand / trash spell generators but not by the play-from-facedown
    // ([Hidden]) generator, so an opponent can still play a hidden spell.
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto& bfs = ctx.state.battlefields;
        const int n = static_cast<int>(std::min<size_t>(bfs.size(), 32));
        uint32_t legal = 0;
        std::vector<std::string> labels;
        for (int i = 0; i < n; ++i) {
            labels.push_back("Move to BF#" + std::to_string(static_cast<int>(bfs[i].id)));
            if (enemyOccupied(ctx, bfs[i].id) && !movers(ctx, bfs[i].id).empty())
                legal |= (1u << i);
        }
        const int mode = pickMode(ctx, "Cataclysm: occupied enemy battlefield", n, labels, legal);
        GameObjectId unit = kInvalidId;
        if (mode >= 0 && mode < n)
            unit = pickTarget(ctx, "Cataclysm: friendly unit to move", movers(ctx, bfs[mode].id));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 355)
        if (mode == -1) return;               // waiting for the agent
        if (mode >= 0 && unit == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the target

        const PlayerId opp = opponent(ctx.controller);
        ctx.state.player(opp).cant_play_spells_this_turn = true;
        ctx.events.logTrace(std::string("CATACLYSM: ") + toString(opp) +
                            " can't play spells this turn");

        if (mode < 0 || mode >= n || unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        const BattlefieldId dest = bfs[mode].id;
        if (!enemyOccupied(ctx, dest)) return;   // no longer an occupied enemy battlefield
        ctx.executor.moveToBattlefield(unit, dest);
        ctx.events.logTrace("CATACLYSM: moved " + ctx.state.getObject(unit).name + " to BF#" +
                            std::to_string(static_cast<int>(dest)));
    }
private:
    static bool enemyOccupied(const CardContext& ctx, BattlefieldId bf) {
        for (const auto& [id, obj] : ctx.state.objects)
            if (obj.isUnit() && obj.controller != ctx.controller && obj.battlefieldId() == bf)
                return true;
        return false;
    }
    static std::vector<GameObjectId> movers(const CardContext& ctx, BattlefieldId dest) {
        return chooseableUnits(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller == ctx.controller && o.battlefieldId() != std::optional<BattlefieldId>{dest};
        });
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1045;
        d.def_id = R"RB(rad-084-167)RB";
        d.name = R"RB(Cataclysm)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-084/167)RB";
        d.collector_number = 84;
        d.artist = R"RB(Polar Engine Studio)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Body};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(Opponents can't play spells this turn.
Move a friendly unit to an occupied enemy battlefield.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/dc551ece6cf007c76a3a16feba5f26fb7dd4ed65-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1045(CardRegistry& r) {
    r.registerCard(1045, std::make_unique<Cataclysm>());
}

}  // namespace riftbound
