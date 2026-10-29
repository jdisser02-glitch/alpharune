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
#include "cards/radiance_helpers.h"   // the Bomb gear token (playBombToken)

namespace riftbound {
namespace {

class SatchelCharge : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "[Action] Move a friendly unit, then play a Bomb gear token to a
    //  battlefield. (It has [Deploy]. When it dies, deal 2 to an enemy there
    //  and kill your other Bombs there.)"
    // The move is chosen as the spell is finalized: first the destination
    // (pickMode, moveDests encoding: 0 = base, 1 + i = battlefield i; offered
    // where some friendly unit not already there could go), then the unit
    // (pickTarget). On resolution the unit moves (a mistarget if it is no
    // longer a legal choice), then the controller picks the battlefield for
    // the Bomb (any battlefield — [Deploy]) and the token is played there.
    // With no friendly unit to move, only the Bomb is played.
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto& ri_opt = ctx.state.chain.resuming;
        const auto& bfs = ctx.state.battlefields;
        const int nb = static_cast<int>(std::min<size_t>(bfs.size(), 31));
        const int n = 1 + nb;
        uint32_t legal = 0;
        std::vector<std::string> labels;
        for (int d = 0; d < n; ++d) {
            labels.push_back("Move to " + destLabel(d));
            if (!movers(ctx, d).empty()) legal |= (1u << d);
        }
        const int dest = pickMode(ctx, "Satchel Charge: move a friendly unit to", n, labels, legal);
        GameObjectId unit = kInvalidId;
        if (dest >= 0 && dest < n)
            unit = pickTarget(ctx, "Satchel Charge: friendly unit to move", movers(ctx, dest));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 355)
        if (dest == -1) return;               // waiting for the agent
        if (dest >= 0 && unit == kInvalidId && ri_opt.has_value() && ri_opt->resume_point == 7)
            return;                           // waiting for the target

        const bool bomb_pending = ri_opt.has_value() && ri_opt->resume_point >= kBombAwait;
        if (!bomb_pending && dest >= 0 && dest < n && unit != kInvalidId &&
            ctx.state.objectExists(unit) && ctx.state.getObject(unit).location.has_value()) {
            moveTo(ctx, unit, dest);
            ctx.events.logTrace("SATCHEL CHARGE: moved " + ctx.state.getObject(unit).name +
                                " to " + destLabel(dest));
        }

        // "then play a Bomb gear token to a battlefield."
        if (bfs.empty()) return;
        if (!ri_opt.has_value()) { playBombToken(ctx, ctx.controller, bfs.front().id); return; }
        auto& ri = *ri_opt;
        if (ri.resume_point >= kBombDone) return;
        if (ri.resume_point != kBombAwait) {
            std::vector<Intent> opts;
            for (const auto& bf : bfs)
                opts.push_back(valChoice(ctx.controller, static_cast<int>(bf.id),
                                         "Bomb to BF#" + std::to_string(static_cast<int>(bf.id))));
            ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                       "Satchel Charge: battlefield for the Bomb");
            ri.resume_point = kBombAwait;
            return;
        }
        int b = takenValue(ctx, static_cast<int>(bfs.front().id));
        bool ok = false;
        for (const auto& bf : bfs) if (static_cast<int>(bf.id) == b) ok = true;
        if (!ok) b = static_cast<int>(bfs.front().id);
        ri.resume_point = kBombDone;
        playBombToken(ctx, ctx.controller, static_cast<BattlefieldId>(b));
    }
private:
    // Resume points of the Bomb battlefield prompt, clear of the 0..14 range
    // the Card::pick* / confirmOptional helpers use.
    static constexpr int kBombAwait = 310;
    static constexpr int kBombDone = 311;

    /// Friendly units that could be moved to `dest` (moveDests encoding).
    static std::vector<GameObjectId> movers(const CardContext& ctx, int dest) {
        return chooseableUnits(ctx.state, ctx.controller, [&](const GameObject& o) {
            if (o.controller != ctx.controller) return false;
            const auto ds = moveDests(ctx.state, o);
            return std::find(ds.begin(), ds.end(), dest) != ds.end();
        });
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1054;
        d.def_id = R"RB(rad-096-167)RB";
        d.name = R"RB(Satchel Charge)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-096/167)RB";
        d.collector_number = 96;
        d.artist = R"RB(Polar Engine Studio)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Chaos};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
Move a friendly unit, then play a Bomb gear token to a battlefield. (It has [Deploy]. When it dies, deal 2 to an enemy there and kill your other Bombs there.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b07c26438f600b87c92e4ed778a1c8b073b47f5c-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1054(CardRegistry& r) {
    r.registerCard(1054, std::make_unique<SatchelCharge>());
}

}  // namespace riftbound
