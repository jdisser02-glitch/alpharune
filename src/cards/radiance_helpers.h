#pragma once
/// @file radiance_helpers.h
/// Radiance mechanics used by more than one card:
///   - the Bomb gear token ("It has [Deploy]. When it dies, deal 2 to an enemy
///     there and kill your other Bombs there.") — Amateur Demolitionist, Ziggs
///     (x2), Satchel Charge, Hexplosives Expert, Hexplosive Minefield;
///   - killing a gear so that its "When this dies" ability triggers;
///   - "Banish the top card of your Main Deck. Until your next Ending Phase,
///     you may play that card." — Runeblade Anvil, Akali, Brash.
/// Staged as its own header for a clean merge; it belongs in card_helpers.h.
#include "cards/card_helpers.h"
#include "core/intent.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace riftbound {

// ── Gear deaths ─────────────────────────────────────────────────────────────
// ENGINE-GAP: TriggerManager fires WhenIDie only from UnitDiedEvent. A gear
// that dies (EffectExecutor::killObject's gear branch, the [Deploy] hold kill
// in GameEngine::scoreHold) emits only LeftBoardEvent, which nothing
// dispatches, so a gear's "When this dies" never triggers on its own. Card
// effects that kill a gear go through killWithDeathTrigger, which queues the
// gear's WhenIDie ability itself (unless the engine already did) and stamps
// the gear "__died_turn" so "if a friendly gear died this turn" can see it.
// Gear killed by the engine or by cards that call killObject directly still
// die silently.

/// "__died_turn" holds turn_number + 1 (0 = never stamped).
inline bool stampedDiedThisTurn(const GameState& state, const GameObject& obj) {
    auto it = obj.card_counters.find("__died_turn");
    return it != obj.card_counters.end() && it->second == state.turn.turn_number + 1;
}

/// Kill `id` (a unit or gear on the board) so that its "When I die / When this
/// dies" ability triggers even when it is a gear.
inline void killWithDeathTrigger(CardContext& ctx, GameObjectId id) {
    if (!ctx.state.objectExists(id) || !ctx.state.getObject(id).location.has_value()) return;
    const bool gear = ctx.state.getObject(id).isGear();
    const PlayerId controller = ctx.state.getObject(id).controller;
    auto queued = [&]() {
        int n = 0;
        for (const auto& ci : ctx.state.chain.items)
            if (ci.source == id && ci.fired_trigger == TriggerType::WhenIDie) ++n;
        return n;
    };
    const int before = queued();
    ctx.executor.killObject(id);
    if (!gear || !ctx.state.objectExists(id)) return;
    auto& dead = ctx.state.getObject(id);
    if (dead.location.has_value()) return;   // a replacement kept it on the board
    dead.card_counters["__died_turn"] = ctx.state.turn.turn_number + 1;
    if (queued() > before) return;           // the engine triggered it already
    const Card* c = (ctx.registry && dead.card_def_id != kInvalidId)
        ? ctx.registry->get(dead.card_def_id) : nullptr;
    if (c && c->firesOn(TriggerType::WhenIDie) && ctx.executor.canQueueTriggers()) {
        ctx.events.logTrace("GEAR DIES: " + dead.name + " -> its death ability triggers");
        ctx.executor.queueTriggeredAbility(id, controller, TriggerType::WhenIDie);
    }
}

// ── Bomb gear token ─────────────────────────────────────────────────────────
// PROPOSED card id for the Bomb token (no official token number is known; the
// class is gear/1199_bomb.cpp). Renumber here, in that file's name and in its
// register function if the id is wanted for something else.
constexpr CardDefId kBombTokenCardDefId = 1199;
void register_card_1199(CardRegistry& r);

inline bool isBomb(const GameObject& obj) {
    return obj.isGear() &&
           (obj.card_def_id == kBombTokenCardDefId || (obj.isToken() && obj.name == "Bomb"));
}

/// Put a Bomb gear token onto battlefield `bf` for `controller`. Gear enter
/// ready (CR 149.1); [Deploy] comes from the bound card (not recalled in
/// cleanups, killed when an opponent holds there).
inline GameObjectId playBombToken(CardContext& ctx, PlayerId controller, BattlefieldId bf) {
    auto id = ctx.executor.createToken(controller, CardType::Gear, "Bomb", 0, {}, KeywordSet{},
                                       LocationId{BattlefieldLocation{bf}}, /*enter_ready=*/true);
    if (ctx.state.objectExists(id)) ctx.state.getObject(id).card_def_id = kBombTokenCardDefId;
    ctx.events.logTrace("BOMB: played a Bomb gear token to BF#" + std::to_string(bf));
    return id;
}

/// "Play a Bomb gear token to a battlefield": the controller picks the
/// battlefield ([Deploy]: any battlefield). Uses Card::pickMode (resume points
/// 3/4/5 + resume_data[1]). Returns the battlefield id, -1 while waiting for
/// the agent, -2 if there is no battlefield.
inline int pickBombBattlefield(Card& card, CardContext& ctx, const std::string& label) {
    const auto& bfs = ctx.state.battlefields;
    const int n = static_cast<int>(std::min<size_t>(bfs.size(), 32));
    if (n == 0) return -2;
    std::vector<std::string> labels;
    for (int i = 0; i < n; ++i) labels.push_back("Bomb to BF#" + std::to_string(bfs[i].id));
    const uint32_t legal = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
    const int m = card.pickMode(ctx, label, n, labels, legal);
    if (m < 0) return m;
    return static_cast<int>(bfs[m].id);
}

// ── "Until your next Ending Phase, you may play that card" ──────────────────
// ENGINE-GAP: the engine has no standing permission to play a card from
// banishment. Modeled with the engine's granted-ability path:
//   - the banished card carries card_counters[key] = (last turn it may be
//     played) + 1;
//   - the controller's legend gets a granted, cost-free activated ability
//     (GameObject::temp_granted_abilities -> {granter, ability_index}) whose
//     resolution (playFromBanishment) picks such a card and plays it through
//     EffectExecutor::playPayingCost, paying its costs;
//   - "this turn" grants are cleared at every end of turn, so when the card is
//     banished on an opponent's turn a delayed AtStartOfMain ability (source =
//     the legend, card = the granter) re-grants it at the start of the
//     controller's next Main Phase; the granter's onTrigger handles that
//     firing (ctx.firing_trigger == AtStartOfMain) with refreshBanishPlay.
// Approximations: the permission lasts through the controller's whole next
// turn (to its end, not to the start of its Ending Phase); the play happens at
// the timing of a plain activated ability (the controller's Main Phase, Open
// State), so [Action]/[Reaction] spells lose their showdown / reaction timing;
// without a legend object there is nobody to grant the ability to and the
// card stays banished.

/// Turn number through which "until your next Ending Phase" lasts.
inline int untilNextEndingTurn(const GameState& state, PlayerId player) {
    return state.turn.turn_player == player ? state.turn.turn_number : state.turn.turn_number + 1;
}

inline bool banishPlayAllowed(const GameState& state, PlayerId player, GameObjectId id,
                              const std::string& key) {
    if (!state.objectExists(id)) return false;
    const auto& obj = state.getObject(id);
    if (obj.owner != player || obj.zone != ZoneType::Banishment) return false;
    auto it = obj.card_counters.find(key);
    return it != obj.card_counters.end() && it->second > 0 &&
           state.turn.turn_number <= it->second - 1;
}

/// Cards in `player`'s banishment they may currently play under `key`.
inline std::vector<GameObjectId> banishPlayCards(const GameState& state, PlayerId player,
                                                 const std::string& key) {
    std::vector<GameObjectId> out;
    for (auto cid : state.player(player).banishment)
        if (banishPlayAllowed(state, player, cid, key)) out.push_back(cid);
    return out;
}

inline void grantBanishPlay(CardContext& ctx, CardDefId granter, int ability_index) {
    auto& ps = ctx.state.player(ctx.controller);
    if (ps.legend_zone == kInvalidId || !ctx.state.objectExists(ps.legend_zone)) {
        ctx.events.logTrace("BANISH-PLAY: no legend to carry the play permission (ENGINE-GAP)");
        return;
    }
    auto& legend = ctx.state.getObject(ps.legend_zone);
    auto has = [&](const std::vector<GameObject::GrantedAbilityRef>& v) {
        for (const auto& r : v)
            if (r.source_def_id == granter && r.ability_index == ability_index) return true;
        return false;
    };
    GameObject::GrantedAbilityRef ref;
    ref.source_def_id = granter;
    ref.ability_index = ability_index;
    if (!has(legend.temp_granted_abilities)) legend.temp_granted_abilities.push_back(ref);
    if (!has(legend.granted_abilities)) legend.granted_abilities.push_back(ref);
}

inline void revokeBanishPlay(CardContext& ctx, CardDefId granter, int ability_index) {
    auto& ps = ctx.state.player(ctx.controller);
    if (ps.legend_zone == kInvalidId || !ctx.state.objectExists(ps.legend_zone)) return;
    auto& legend = ctx.state.getObject(ps.legend_zone);
    auto drop = [&](std::vector<GameObject::GrantedAbilityRef>& v) {
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const GameObject::GrantedAbilityRef& r) {
                                   return r.source_def_id == granter && r.ability_index == ability_index;
                               }),
                v.end());
    };
    drop(legend.temp_granted_abilities);
    drop(legend.granted_abilities);
}

/// The granter's AtStartOfMain (delayed) firing: re-grant while a card banished
/// under `key` may still be played.
inline void refreshBanishPlay(CardContext& ctx, const std::string& key, CardDefId granter,
                              int ability_index) {
    if (!banishPlayCards(ctx.state, ctx.controller, key).empty())
        grantBanishPlay(ctx, granter, ability_index);
}

/// "Banish the top card of your Main Deck. Until your next Ending Phase, you
/// may play that card." Returns the banished card (kInvalidId if the deck was
/// empty).
inline GameObjectId banishTopToPlayLater(CardContext& ctx, const std::string& key,
                                         CardDefId granter, int ability_index) {
    auto& ps = ctx.state.player(ctx.controller);
    if (ps.main_deck.empty()) return kInvalidId;
    const GameObjectId top = ps.main_deck.back();
    ctx.executor.banishObject(top);
    if (!ctx.state.objectExists(top) || ctx.state.getObject(top).zone != ZoneType::Banishment)
        return kInvalidId;
    const int until = untilNextEndingTurn(ctx.state, ctx.controller);
    ctx.state.getObject(top).card_counters[key] = until + 1;
    grantBanishPlay(ctx, granter, ability_index);
    if (until > ctx.state.turn.turn_number && ps.legend_zone != kInvalidId &&
        ctx.state.objectExists(ps.legend_zone)) {
        bool scheduled = false;
        for (const auto& da : ctx.state.delayed_abilities)
            if (!da.fired && da.card_def_id == granter && da.controller == ctx.controller &&
                da.trigger == TriggerType::AtStartOfMain)
                scheduled = true;
        if (!scheduled) {
            DelayedAbility da;
            da.source = ps.legend_zone;
            da.card_def_id = granter;
            da.controller = ctx.controller;
            da.trigger = TriggerType::AtStartOfMain;
            da.expires_on_turn = until;
            ctx.state.delayed_abilities.push_back(da);
        }
    }
    ctx.events.logTrace("BANISH-PLAY: banished " + ctx.state.getObject(top).name +
                        " from the top of the Main Deck; playable through turn " +
                        std::to_string(until));
    return top;
}

/// Resume point of playFromBanishment's location prompt (clear of the 0..14
/// range the Card::pick* / confirmOptional helpers use).
constexpr int kBanishPlayLocAwait = 200;
constexpr int kBanishPlayDone = 201;

/// Resolution of the granted ability: pick a card banished under `key`, choose
/// where a permanent goes (base or a battlefield you control for a unit; any
/// battlefield for a [Deploy] gear; base for other gear) and play it, paying
/// its costs (CR 356.1.b.2). If the costs aren't paid it stays banished and
/// keeps its permission.
inline void playFromBanishment(Card& card, CardContext& ctx, const std::string& key,
                               CardDefId granter, int ability_index) {
    auto& state = ctx.state;
    const PlayerId me = ctx.controller;
    const auto allowed = banishPlayCards(state, me, key);
    std::vector<GameObjectId> payable;
    for (auto cid : allowed)
        if (ctx.executor.canPlayPayingCost(me, cid)) payable.push_back(cid);
    const GameObjectId t = card.pickTarget(ctx, "play a banished card", payable);
    auto& ri_opt = state.chain.resuming;
    if (t == kInvalidId) {
        if (ri_opt.has_value() && ri_opt->resume_point == 7) return;   // waiting for the agent
        if (allowed.empty()) revokeBanishPlay(ctx, granter, ability_index);
        return;
    }
    if (ri_opt.has_value() && ri_opt->resume_point == kBanishPlayDone) return;
    if (!banishPlayAllowed(state, me, t, key)) return;

    std::optional<LocationId> where;
    if (!state.getObject(t).isSpell()) {
        const auto& obj = state.getObject(t);
        const Card* c = (ctx.registry && obj.card_def_id != kInvalidId)
            ? ctx.registry->get(obj.card_def_id) : nullptr;
        const bool deploy = obj.isGear() && c && c->deploys();
        std::vector<int> dests;   // 0 = base, 1 + i = battlefield i
        if (!deploy) dests.push_back(0);
        for (const auto& bf : state.battlefields)
            if (deploy || (obj.isUnit() && bf.controller == std::optional<PlayerId>{me}))
                dests.push_back(1 + static_cast<int>(bf.id));
        if (dests.empty()) return;
        int d = dests.front();
        if (ri_opt.has_value()) {
            auto& ri = *ri_opt;
            if (ri.resume_point != kBanishPlayLocAwait) {
                std::vector<Intent> opts;
                for (int x : dests) opts.push_back(valChoice(me, x, destLabel(x)));
                ctx.executor.requestChoice(me, std::move(opts), "play " + obj.name + " to");
                ri.resume_point = kBanishPlayLocAwait;
                return;
            }
            d = takenValue(ctx, dests.front());
            if (std::find(dests.begin(), dests.end(), d) == dests.end()) d = dests.front();
        }
        where = d <= 0 ? LocationId{BaseLocation{me}}
                       : LocationId{BattlefieldLocation{static_cast<BattlefieldId>(d - 1)}};
    }
    if (ri_opt.has_value()) ri_opt->resume_point = kBanishPlayDone;

    auto& ps = state.player(me);
    auto it = std::find(ps.banishment.begin(), ps.banishment.end(), t);
    if (it != ps.banishment.end()) ps.banishment.erase(it);
    const int saved = state.getObject(t).card_counters[key];
    state.getObject(t).card_counters.erase(key);
    const std::string name = state.getObject(t).name;
    if (!ctx.executor.playPayingCost(me, t, where)) {
        if (state.objectExists(t)) {
            auto& back = state.getObject(t);
            back.zone = ZoneType::Banishment;
            back.location = std::nullopt;
            back.card_counters[key] = saved;
        }
        ps.banishment.push_back(t);
        ctx.events.logTrace("BANISH-PLAY: costs of " + name + " not paid; it stays banished");
        return;
    }
    ctx.events.logTrace("BANISH-PLAY: played " + name + " from banishment");
    if (banishPlayCards(state, me, key).empty()) revokeBanishPlay(ctx, granter, ability_index);
}

}  // namespace riftbound
