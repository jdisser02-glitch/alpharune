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

        GameObjectId unit = pickTarget(ctx, "Last Caress: unit to play", candidates(ctx));
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;   // waiting / none

        const LocationId dest = where == 0
            ? LocationId{BaseLocation{ctx.controller}}
            : LocationId{BattlefieldLocation{bfs[where - 1].id}};
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
        } else {
            if (!ctx.executor.canPlayPayingCost(ctx.controller, unit)) return;
            auto& hand = ctx.state.player(ctx.controller).hand;
            auto it = std::find(hand.begin(), hand.end(), unit);
            if (it == hand.end()) return;
            hand.erase(it);
            if (!ctx.executor.playPayingCost(ctx.controller, unit, dest)) {
                hand.push_back(unit);   // not paid: it stays in hand
                return;
            }
        }
        if (!ctx.state.objectExists(unit) || !ctx.state.getObject(unit).location.has_value()) return;
        if (ctx.state.getObject(unit).buff_count == 0) ctx.executor.buffUnit(unit);
        ctx.events.logTrace("LAST CARESS: played " + uname + " to " + labels[where] + " and buffed it");
    }

private:
    std::vector<GameObjectId> candidates(const CardContext& ctx) const {
        const auto& st = ctx.state;
        const bool main_timing = st.turn.turn_player == ctx.controller &&
                                 st.turn.ns_state == NeutralShowdownState::Neutral &&
                                 st.chain.items.empty();
        std::vector<GameObjectId> out;
        for (auto cid : st.player(ctx.controller).hand) {
            if (!st.objectExists(cid)) continue;
            const auto& c = st.getObject(cid);
            if (!c.isUnit() || c.card_def_id == kInvalidId) continue;
            if (!main_timing && !c.keywords.has(Keyword::Reaction) && !c.keywords.has(Keyword::Ambush))
                continue;
            if (!ctx.executor.canPlayPayingCost(ctx.controller, cid)) continue;
            out.push_back(cid);
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
