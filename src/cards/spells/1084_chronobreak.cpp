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

class Chronobreak : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "As an additional cost to play this, pay any amount of Power.
    //  Banish that many units you control, then for each, its owner plays that
    //  unit back to the same location or to their base, ignoring its cost.
    //  (You choose the order.)"
    //
    // The amount X is chosen as the spell is finalized (pickXAmount, offered
    // up to the Power you can pay and the number of units you control —
    // paying more could do nothing) and paid then as Power of any domain
    // (EffectExecutor::payAnyPower: floating Power first, then runes
    // recycled, exhausted ones first — the runes are not an agent choice).
    // As it resolves you pick the units one at a time; the pick order is the
    // order they are played back. All are banished first; then each card's
    // owner chooses its location (the battlefield it was at, or their base;
    // a unit that was in a base goes to its owner's base). Tokens cease to
    // exist when banished and are not played back.
    //
    // Resume points: 0-2 pickXAmount (resume_data[0] = X), then kPick /
    // kAwaitPick (choose units), kPlayBack / kAwaitDest (play them back),
    // kDone. resume_data[6] = units picked, [7] = units played back,
    // [8 + 2i] / [9 + 2i] = i-th unit and where it was (-1 = a base).
    static constexpr int kPick = 20;
    static constexpr int kAwaitPick = 21;
    static constexpr int kPlayBack = 22;
    static constexpr int kAwaitDest = 23;
    static constexpr int kDone = 24;
    static constexpr size_t kPicked = 6, kPlayed = 7, kList = 8;

    static std::vector<GameObjectId> friendlyUnits(const CardContext& ctx) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : ctx.state.objects)
            if (obj.isUnit() && obj.controller == ctx.controller && obj.location.has_value())
                out.push_back(id);
        std::sort(out.begin(), out.end());
        return out;
    }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const int units_now = static_cast<int>(friendlyUnits(ctx).size());
        const int max_x = std::min(ctx.executor.payableAnyPower(ctx.controller), units_now);
        int x = pickXAmount(ctx, "Chronobreak: pay any amount of Power", 0, std::max(0, max_x));
        if (x < 0) return;                    // waiting for the agent
        if (x > 0 && costDueNow(ctx) && !ctx.executor.payAnyPower(ctx.controller, x)) x = 0;
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (x <= 0) return;

        if (!ctx.state.chain.resuming.has_value()) {
            // Direct invocation (no chain): the first X units, back where they were.
            auto units = friendlyUnits(ctx);
            if (static_cast<int>(units.size()) > x) units.resize(x);
            std::vector<int> origins;
            for (auto u : units) origins.push_back(originOf(ctx.state.getObject(u)));
            for (auto u : units) ctx.executor.banishObject(u);
            for (size_t i = 0; i < units.size(); ++i)
                playBack(ctx, units[i], origins[i] >= 0 ? 1 + origins[i] : 0);
            return;
        }
        auto& ri = *ctx.state.chain.resuming;
        auto& d = ri.resume_data;
        if (d.size() < kList) d.resize(kList, 0);
        if (ri.resume_point < kPick) {
            ri.resume_point = kPick;
            d[kPicked] = 0;
            d[kPlayed] = 0;
        }
        auto picked_unit = [&](int i) { return static_cast<GameObjectId>(d[kList + 2 * i]); };
        auto candidates = [&]() {
            std::vector<GameObjectId> out;
            for (auto u : friendlyUnits(ctx)) {
                bool taken = false;
                for (int i = 0; i < d[kPicked]; ++i) if (picked_unit(i) == u) taken = true;
                if (!taken) out.push_back(u);
            }
            return out;
        };

        if (ri.resume_point == kAwaitPick) {
            auto cands = candidates();
            GameObjectId u = takenObject(ctx);
            if (std::find(cands.begin(), cands.end(), u) == cands.end())
                u = cands.empty() ? kInvalidId : cands.front();
            if (u != kInvalidId) {
                d.push_back(static_cast<int32_t>(u));
                d.push_back(originOf(ctx.state.getObject(u)));
                d[kPicked]++;
            }
            ri.resume_point = kPick;
        }
        if (ri.resume_point == kPick) {
            auto cands = candidates();
            if (d[kPicked] < x && !cands.empty()) {
                std::vector<Intent> options;
                for (auto u : cands) options.push_back(objChoice(ctx.controller, u));
                ctx.executor.requestChoice(ctx.controller, std::move(options),
                    "Chronobreak: banish a unit you control (" +
                    std::to_string(d[kPicked] + 1) + " of " + std::to_string(x) + ")");
                ri.resume_point = kAwaitPick;
                return;
            }
            for (int i = 0; i < d[kPicked]; ++i) {
                const GameObjectId u = picked_unit(i);
                if (ctx.state.objectExists(u) && ctx.state.getObject(u).location.has_value())
                    ctx.executor.banishObject(u);
            }
            ctx.events.logTrace("CHRONOBREAK: banished " + std::to_string(d[kPicked]) + " unit(s)");
            d[kPlayed] = 0;
            ri.resume_point = kPlayBack;
        }
        if (ri.resume_point == kAwaitDest) {
            const int i = d[kPlayed];
            const int origin = d[kList + 2 * i + 1];
            int dest = takenValue(ctx, 0);
            if (dest != 0 && dest != 1 + origin) dest = 0;
            playBack(ctx, picked_unit(i), dest);
            d[kPlayed]++;
            ri.resume_point = kPlayBack;
        }
        if (ri.resume_point == kPlayBack) {
            while (d[kPlayed] < d[kPicked]) {
                const int i = d[kPlayed];
                const GameObjectId u = picked_unit(i);
                const int origin = d[kList + 2 * i + 1];
                if (!isBanishedCard(ctx.state, u)) { d[kPlayed]++; continue; }
                if (origin >= 0 && origin < static_cast<int>(ctx.state.battlefields.size())) {
                    const PlayerId owner = ctx.state.getObject(u).owner;
                    const std::string uname = ctx.state.getObject(u).name;
                    ctx.executor.requestChoice(owner,
                        {valChoice(owner, 1 + origin, uname + " back to " + destLabel(1 + origin)),
                         valChoice(owner, 0, uname + " to base")},
                        "Chronobreak: where " + uname + " is played back");
                    ri.resume_point = kAwaitDest;
                    return;
                }
                playBack(ctx, u, 0);
                d[kPlayed]++;
            }
            ri.resume_point = kDone;
        }
    }
private:
    /// Where a unit is: its battlefield id, or -1 for a base.
    static int originOf(const GameObject& u) {
        const auto bf = u.battlefieldId();
        return bf.has_value() ? static_cast<int>(*bf) : -1;
    }
    static bool isBanishedCard(const GameState& state, GameObjectId u) {
        if (!state.objectExists(u)) return false;
        const auto& o = state.getObject(u);
        if (o.isToken() || o.zone != ZoneType::Banishment) return false;
        const auto& bz = state.player(o.owner).banishment;
        return std::find(bz.begin(), bz.end(), u) != bz.end();
    }
    /// Its owner plays `u` from banishment, ignoring its cost: dest 0 = the
    /// owner's base, 1 + i = battlefield i.
    static void playBack(CardContext& ctx, GameObjectId u, int dest) {
        if (!isBanishedCard(ctx.state, u)) return;
        auto& obj = ctx.state.getObject(u);
        const PlayerId owner = obj.owner;
        auto& bz = ctx.state.player(owner).banishment;
        bz.erase(std::remove(bz.begin(), bz.end(), u), bz.end());
        const LocationId where = dest <= 0
            ? LocationId{BaseLocation{owner}}
            : LocationId{BattlefieldLocation{static_cast<BattlefieldId>(dest - 1)}};
        const std::string uname = obj.name;
        ctx.executor.playIgnoringCost(owner, u, where);
        ctx.events.logTrace("CHRONOBREAK: " + uname + " played back to " + destLabel(dest) +
                            ", ignoring its cost");
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1084;
        d.def_id = R"RB(rad-140-167)RB";
        d.name = R"RB(Chronobreak)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-140/167)RB";
        d.collector_number = 140;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Spell;
        d.super_type = SuperType::Signature;
        d.domains = {Domain::Fury, Domain::Mind};
        d.tags = {R"RB(Ekko)RB"};
        d.energy_cost = 2;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(As an additional cost to play this, pay any amount of Power.
Banish that many units you control, then for each, its owner plays that unit back to the same location or to their base, ignoring its cost. (You choose the order.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/af2123a99f3e3e7e6c9b101c5fc2c9b0ff56e096-744x1040.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1084(CardRegistry& r) {
    r.registerCard(1084, std::make_unique<Chronobreak>());
}

}  // namespace riftbound
