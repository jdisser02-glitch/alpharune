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

// RECONSTRUCTED TEXT: this card is not in cards/card_index.json. Its data and
// ability text were reconstructed from web-search summaries (confidence:
// medium). Notes: Power-cost cap — riftbound.gg shows the rainbow rune symbol
// (= [A]), retailers say "1 Rune", the LoL wiki says "1 Wild Power" (same
// meaning: Power cost 1 or less). "up to 3" vs "up to three" and the comma
// placement are unverified. power_any_domain = false refers to the card's
// own 2 Order Power cost.
class RememberTheFallen : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "Play up to 3 units and/or gear from your trash, each with Energy cost
    //  no more than [3] and Power cost no more than [A], ignoring their costs."
    //
    // Resolved as up to three sequential picks among the qualifying units and
    // gear in your trash (printed costs), each with a "Done" option. A unit
    // goes to your base or a battlefield you control (CR 355.2.a), asked when
    // there is more than one place; a [Deploy] gear to any battlefield; other
    // gear to your base. Each card is played through
    // EffectExecutor::playIgnoringCost (its "as you play me" and play triggers
    // apply).
    static constexpr int kAwaitPick = 400;   // clear of the Card::pick* resume points
    static constexpr int kAwaitDest = 401;
    static constexpr int kDone = 402;
    static constexpr size_t kPlayedSlot = 5;   // resume_data slots (0..4 belong to the helpers)
    static constexpr size_t kPickedSlot = 6;
    static constexpr int kMaxPlays = 3;

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto& ri_opt = ctx.state.chain.resuming;
        if (!ri_opt.has_value()) {
            // No chain to suspend on: play the first qualifying cards to their
            // default places.
            for (int n = 0; n < kMaxPlays; ++n) {
                const auto el = eligible(ctx);
                if (el.empty()) break;
                const auto dests = destinations(ctx, el.front());
                if (dests.empty()) break;
                playOne(ctx, el.front(), dests.front());
            }
            return;
        }
        auto& ri = *ri_opt;
        if (ri.resume_data.size() <= kPickedSlot) ri.resume_data.resize(kPickedSlot + 1, 0);

        switch (ri.resume_point) {
        case kDone:
            return;
        case kAwaitDest: {
            const GameObjectId picked = ri.resume_data[kPickedSlot];
            const auto dests = destinations(ctx, picked);
            int d = takenValue(ctx, dests.empty() ? -1 : dests.front());
            if (std::find(dests.begin(), dests.end(), d) == dests.end())
                d = dests.empty() ? -1 : dests.front();
            if (d >= 0 && stillEligible(ctx, picked)) {
                playOne(ctx, picked, d);
                ++ri.resume_data[kPlayedSlot];
            }
            break;
        }
        case kAwaitPick: {
            const GameObjectId picked = takenObject(ctx);
            if (picked == kInvalidId || !stillEligible(ctx, picked)) {
                ri.resume_point = kDone;   // "Done"
                return;
            }
            const auto dests = destinations(ctx, picked);
            if (dests.size() > 1) {
                ri.resume_data[kPickedSlot] = picked;
                std::vector<Intent> opts;
                for (int d : dests) opts.push_back(valChoice(ctx.controller, d, destLabel(d)));
                ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                           "Remember the Fallen: play " +
                                               ctx.state.getObject(picked).name + " to");
                ri.resume_point = kAwaitDest;
                return;
            }
            if (!dests.empty()) {
                playOne(ctx, picked, dests.front());
                ++ri.resume_data[kPlayedSlot];
            }
            break;
        }
        default:
            ri.resume_data[kPlayedSlot] = 0;
            break;
        }

        if (ri.resume_data[kPlayedSlot] >= kMaxPlays) { ri.resume_point = kDone; return; }
        const auto el = eligible(ctx);
        if (el.empty()) { ri.resume_point = kDone; return; }
        std::vector<Intent> opts;
        for (auto id : el) opts.push_back(objChoice(ctx.controller, id));
        opts.push_back(valChoice(ctx.controller, -1, "Done"));
        ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                   "Remember the Fallen: play a unit or gear from your trash");
        ri.resume_point = kAwaitPick;
    }
private:
    static bool qualifies(const CardContext& ctx, const GameObject& obj) {
        if (!obj.isUnit() && !obj.isGear()) return false;
        if (obj.card_def_id == kInvalidId) return false;
        const auto& def = ctx.executor.cardDB().get(obj.card_def_id);
        return def.energy_cost <= 3 && def.power_cost <= 1;
    }
    static std::vector<GameObjectId> eligible(const CardContext& ctx) {
        std::vector<GameObjectId> out;
        for (auto cid : ctx.state.player(ctx.controller).trash) {
            if (!ctx.state.objectExists(cid)) continue;
            const auto& obj = ctx.state.getObject(cid);
            if (obj.zone == ZoneType::Trash && qualifies(ctx, obj) &&
                std::find(out.begin(), out.end(), cid) == out.end())
                out.push_back(cid);
        }
        return out;
    }
    static bool stillEligible(const CardContext& ctx, GameObjectId id) {
        const auto el = eligible(ctx);
        return std::find(el.begin(), el.end(), id) != el.end();
    }
    /// Where `card` may be played: 0 = your base, 1 + i = battlefield i.
    static std::vector<int> destinations(const CardContext& ctx, GameObjectId card) {
        std::vector<int> out;
        if (!ctx.state.objectExists(card)) return out;
        const auto& obj = ctx.state.getObject(card);
        const PlayerId me = ctx.controller;
        if (obj.isGear()) {
            const Card* c = (ctx.registry && obj.card_def_id != kInvalidId)
                ? ctx.registry->get(obj.card_def_id) : nullptr;
            if (c && c->deploys()) {
                for (const auto& bf : ctx.state.battlefields) out.push_back(1 + static_cast<int>(bf.id));
            } else {
                out.push_back(0);
            }
            return out;
        }
        out.push_back(0);
        if (ctx.state.player(me).units_play_base_only) return out;
        for (const auto& bf : ctx.state.battlefields)
            if (bf.controller == std::optional<PlayerId>{me} && !bf.blocks_unit_play)
                out.push_back(1 + static_cast<int>(bf.id));
        return out;
    }
    static void playOne(CardContext& ctx, GameObjectId card, int dest) {
        auto& trash = ctx.state.player(ctx.controller).trash;
        trash.erase(std::remove(trash.begin(), trash.end(), card), trash.end());
        const LocationId where = dest <= 0
            ? LocationId{BaseLocation{ctx.controller}}
            : LocationId{BattlefieldLocation{static_cast<BattlefieldId>(dest - 1)}};
        const std::string name = ctx.state.getObject(card).name;
        ctx.executor.playIgnoringCost(ctx.controller, card, where);
        ctx.events.logTrace("REMEMBER THE FALLEN: played " + name + " from trash to " + destLabel(dest));
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1127;
        d.def_id = R"RB(rad-135-167)RB";
        d.name = R"RB(Remember the Fallen)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-135/167)RB";
        d.collector_number = 135;
        d.card_type = CardType::Spell;
        d.domains = {Domain::Order};
        d.energy_cost = 8;
        d.power_cost = 2;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(Play up to 3 units and/or gear from your trash, each with Energy cost no more than [3] and Power cost no more than [A], ignoring their costs.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1127(CardRegistry& r) {
    r.registerCard(1127, std::make_unique<RememberTheFallen>());
}

}  // namespace riftbound
