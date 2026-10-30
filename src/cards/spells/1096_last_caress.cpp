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

class LastCaress : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "[Reaction] The next unit you play this turn (even from face down) can be
    //  played to any battlefield. When you play it, [Buff] it."
    //
    // APPROXIMATION (engine gap): the engine has no "the next unit you play"
    // play-location grant (the unit play generator only knows a card's own
    // text, Ambush and board-wide auras) and nothing reaches a resolved spell
    // when a later unit is played. So the effect is folded into the
    // resolution: the controller may play a unit right now — from hand, paying
    // its cost, or one of their face-down cards — to their base or ANY
    // battlefield, and [Buff] it. Only units that could be played at this
    // moment are offered: any unit from hand on your own turn with nothing
    // else on the chain and no showdown under way, otherwise only units with
    // [Reaction] / [Ambush]; face-down cards from the turn after they were
    // hidden. Declining is the same as not playing a unit afterwards.
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        int go = confirmOptional(ctx, "Last Caress: play a unit to any battlefield and buff it?",
                                 [&]() { return !candidates(ctx).empty(); });
        if (go != 1) return;   // waiting, declined, or nothing to play

        // Where: 0 = base, 1 + i = battlefield i.
        const auto& bfs = ctx.state.battlefields;
        const int n = 1 + static_cast<int>(std::min<size_t>(bfs.size(), 31));
        std::vector<std::string> labels{"Base"};
        uint32_t legal = 1u;
        const bool base_only = ctx.state.player(ctx.controller).units_play_base_only;
        for (int i = 1; i < n; ++i) {
            const auto& bf = bfs[i - 1];
            labels.push_back(ctx.state.objectExists(bf.card_object_id)
                ? ctx.state.getObject(bf.card_object_id).name
                : "BF#" + std::to_string(static_cast<int>(bf.id)));
            if (!bf.blocks_unit_play && !base_only) legal |= (1u << i);
        }
        int where = pickMode(ctx, "Last Caress: where to play the unit", n, labels, legal);
        if (where < 0 || where >= n) return;   // waiting for the agent

        const LocationId dest = where == 0
            ? LocationId{BaseLocation{ctx.controller}}
            : LocationId{BattlefieldLocation{bfs[where - 1].id}};
        GameObjectId unit = pickTarget(ctx, "Last Caress: unit to play", candidates(ctx, where));
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;   // waiting / none

        auto& obj = ctx.state.getObject(unit);
        const std::string uname = obj.name;
        if (obj.is_hidden) {
            // From face down: ignoring its cost (CR 811), announced the way the
            // engine's face-down plays are.
            for (auto& bf : ctx.state.battlefields) {
                auto it = std::find(bf.facedown.begin(), bf.facedown.end(), unit);
                if (it != bf.facedown.end()) { bf.facedown.erase(it); break; }
            }
            obj.is_hidden = false;
            obj.hidden_at = kInvalidId;
            obj.card_counters["__played_from_facedown"] = 1;
            ctx.events.emit(PlayedFromFacedownEvent{unit, ctx.controller});
            ctx.executor.playIgnoringCost(ctx.controller, unit, dest);
            ctx.events.emit(ObjectStateChangedEvent{unit, "played_not_from_hand"});
        } else if (obj.zone == ZoneType::ChampionZone) {
            // The chosen champion, from the Champion Zone (a unit you play).
            EffectExecutor::EffectPlayCost cost;
            cost.energy_reduction = discountAt(ctx, where, unit);
            if (!ctx.executor.canPlayPayingCost(ctx.controller, unit, cost)) return;
            if (!ctx.executor.playPayingCost(ctx.controller, unit, dest, cost)) return;
            auto& ps = ctx.state.player(ctx.controller);
            if (ps.champion_zone == unit) ps.champion_zone = kInvalidId;
        } else {
            EffectExecutor::EffectPlayCost cost;
            cost.energy_reduction = discountAt(ctx, where, unit);
            if (!ctx.executor.canPlayPayingCost(ctx.controller, unit, cost)) return;
            auto& hand = ctx.state.player(ctx.controller).hand;
            auto it = std::find(hand.begin(), hand.end(), unit);
            if (it == hand.end()) return;
            hand.erase(it);
            if (!ctx.executor.playPayingCost(ctx.controller, unit, dest, cost)) {
                hand.push_back(unit);   // not paid: it stays in hand
                return;
            }
        }
        if (where > 0 && where - 1 < static_cast<int>(bfs.size())) {
            // "The first unit or gear a player plays here each turn" (Hunters'
            // Circle) counts this play.
            auto& bf = ctx.state.battlefields[static_cast<size_t>(where - 1)];
            bf.last_play_here_turn[ctx.controller == PlayerId::Player1 ? 0 : 1] = ctx.state.turn.turn_number;
        }
        if (!ctx.state.objectExists(unit) || !ctx.state.getObject(unit).location.has_value()) return;
        if (ctx.state.getObject(unit).buff_count == 0) ctx.executor.buffUnit(unit);
        ctx.events.logTrace("LAST CARESS: played " + uname + " to " + labels[where] + " and buffed it");
    }

private:
    // The battlefield's own play discount where the unit goes ("the first unit
    // or gear they play here on their turn costs [1] less", Hunters' Circle).
    static int discountAt(const CardContext& ctx, int where, GameObjectId unit) {
        const auto& bfs = ctx.state.battlefields;
        if (where <= 0 || where - 1 >= static_cast<int>(bfs.size()) || !ctx.registry) return 0;
        const auto& bf = bfs[static_cast<size_t>(where - 1)];
        if (!ctx.state.objectExists(bf.card_object_id) || !ctx.state.objectExists(unit)) return 0;
        const auto* bc = dynamic_cast<const BattlefieldCard*>(
            ctx.registry->get(ctx.state.getObject(bf.card_object_id).card_def_id));
        return bc ? bc->playCostReductionHere(ctx.state, bf, ctx.controller, ctx.state.getObject(unit)) : 0;
    }
    // Units that could be played now; `where` (0 base, 1 + i battlefield i)
    // prices them with that location's discount, -1 with the best one.
    std::vector<GameObjectId> candidates(const CardContext& ctx, int where = -1) const {
        const auto& st = ctx.state;
        const bool main_timing = st.turn.turn_player == ctx.controller &&
                                 st.turn.ns_state == NeutralShowdownState::Neutral &&
                                 st.chain.items.empty();
        auto affordable = [&](GameObjectId cid) {
            int lo = where < 0 ? 0 : where, hi = where < 0 ? static_cast<int>(st.battlefields.size()) : where;
            for (int w = lo; w <= hi; ++w) {
                EffectExecutor::EffectPlayCost cost;
                cost.energy_reduction = discountAt(ctx, w, cid);
                if (ctx.executor.canPlayPayingCost(ctx.controller, cid, cost)) return true;
            }
            return false;
        };
        std::vector<GameObjectId> out;
        for (auto cid : st.player(ctx.controller).hand) {
            if (!st.objectExists(cid)) continue;
            const auto& c = st.getObject(cid);
            if (!c.isUnit() || c.card_def_id == kInvalidId) continue;
            if (!main_timing && !c.keywords.has(Keyword::Reaction) && !c.keywords.has(Keyword::Ambush))
                continue;
            if (!affordable(cid)) continue;
            out.push_back(cid);
        }
        // The chosen champion in the Champion Zone is a unit you may play too.
        if (const auto champ = st.player(ctx.controller).champion_zone;
            champ != kInvalidId && st.objectExists(champ)) {
            const auto& c = st.getObject(champ);
            if (c.zone == ZoneType::ChampionZone && c.isUnit() && c.card_def_id != kInvalidId &&
                (main_timing || c.keywords.has(Keyword::Reaction) || c.keywords.has(Keyword::Ambush)) &&
                affordable(champ))
                out.push_back(champ);
        }
        for (const auto& [id, c] : st.objects) {
            if (!c.is_hidden || !c.isUnit() || c.controller != ctx.controller) continue;
            if (c.hidden_on_turn >= st.turn.turn_number) continue;   // hidden this turn
            out.push_back(id);
        }
        return out;
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1096;
        d.def_id = R"RB(rad-154-167)RB";
        d.name = R"RB(Last Caress)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-154/167)RB";
        d.collector_number = 154;
        d.artist = R"RB(Polar Engine Studio)RB";
        d.card_type = CardType::Spell;
        d.super_type = SuperType::Signature;
        d.domains = {Domain::Body, Domain::Chaos};
        d.tags = {R"RB(Evelynn)RB"};
        d.energy_cost = 2;
        d.rarity = Rarity::Epic;
        d.keywords.set(Keyword::Reaction);
        d.ability_text = R"RB([Reaction] (Play any time, even before spells and abilities resolve.)
The next unit you play this turn (even from face down) can be played to any battlefield. When you play it, [Buff] it. (Give it a +1 [M] buff if it doesn't have one.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/c34881b6fe2e63f6345845fe95cfe72eb9d4878c-744x1040.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1096(CardRegistry& r) {
    r.registerCard(1096, std::make_unique<LastCaress>());
}

}  // namespace riftbound
