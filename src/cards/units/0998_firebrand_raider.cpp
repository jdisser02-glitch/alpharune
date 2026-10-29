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

class FirebrandRaider : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "I enter ready."
    using Card::entersReadyOnPlay;
    bool entersReadyOnPlay() const override { return true; }

    // "Other units enter ready." Three routes, since the engine has no global
    // enter-ready hook:
    //  * units Firebrand's controller plays: grantsEnterReadyTo (asked of the
    //    playing player's own board);
    //  * units the opponent plays: a WhenOpponentPlaysAUnit trigger that clears
    //    the unit's exhaustion (APPROXIMATION: it becomes ready when that
    //    trigger resolves, just after it enters, not as it enters);
    //  * token units: PlayerState::tokens_enter_ready for both players
    //    (applyPassiveAura).
    // Not covered: non-token units put onto the board by an effect
    // (EffectExecutor::playIgnoringCost) for Firebrand's own controller.
    bool grantsEnterReadyTo(const GameState& /*state*/, GameObjectId self,
                            const GameObject& played) const override {
        return played.isUnit() && played.id != self;
    }

    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayMe, TriggerType::WhenOpponentPlaysAUnit};
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::WhenOpponentPlaysAUnit) {
            if (!ctx.state.objectExists(ctx.source)) return;
            const auto it = ctx.state.getObject(ctx.source).card_counters.find("__opp_played_unit_id");
            if (it == ctx.state.getObject(ctx.source).card_counters.end()) return;
            const GameObjectId unit = static_cast<GameObjectId>(it->second);
            if (!ctx.state.objectExists(unit)) return;
            auto& u = ctx.state.getObject(unit);
            if (!u.isUnit() || !u.location.has_value() || !u.is_exhausted) return;
            u.is_exhausted = false;   // enters ready: not a "ready" action, so no ready event
            ctx.events.logTrace("FIREBRAND RAIDER: " + u.name + " enters ready");
            return;
        }
        // "When you play me, ready all enemy units."
        std::vector<GameObjectId> enemies;
        for (const auto& [id, obj] : ctx.state.objects)
            if (obj.isUnit() && obj.location.has_value() && obj.controller != ctx.controller &&
                obj.controller != PlayerId::None)
                enemies.push_back(id);
        for (auto id : enemies) ctx.executor.readyObject(id);
        ctx.events.logTrace("FIREBRAND RAIDER: readied " + std::to_string(enemies.size()) +
                            " enemy units");
    }

    void onPlay(CardContext& ctx) override {
        // A fresh arrival was not in play during this turn's Awaken Phase.
        if (ctx.state.objectExists(ctx.source))
            ctx.state.getObject(ctx.source).card_counters.erase(kLastPass);
    }

    // "Units don't ready during the Awaken Phase."
    // APPROXIMATION: the engine's Awaken Phase readies the turn player's
    // objects directly, with no hook a card can veto. Instead, every aura pass
    // records each on-board unit's exhausted status (per turn); on the first
    // pass after an Awaken Phase that Firebrand was in play for (it was in play
    // at the last pass of the previous turn), units of the turn player that
    // were exhausted at the end of the previous turn and are now ready are
    // exhausted again. Side effects of the engine's readying still happen
    // ("became_ready" triggers), and a unit readied by an effect between the
    // Awaken Phase and the next cleanup would be exhausted again too.
    void applyPassiveAura(GameState& state, PlayerId /*controller*/) const override {
        state.player(PlayerId::Player1).tokens_enter_ready = true;
        state.player(PlayerId::Player2).tokens_enter_ready = true;

        const int turn = state.turn.turn_number;
        if (state.turn.phase == TurnPhase::AwakenPhase) return;   // look after it
        bool lock = false;
        for (auto& [id, o] : state.objects) {
            if (o.card_def_id != cardDefId() || !o.location.has_value()) continue;
            auto lp = o.card_counters.find(kLastPass);
            if (lp == o.card_counters.end() || lp->second != turn) {
                const bool continuous = lp != o.card_counters.end() && lp->second == turn - 1;
                o.card_counters[kLastPass] = turn;
                o.card_counters[kHeldTurn] = continuous ? turn : -1;
            }
            if (o.card_counters[kHeldTurn] == turn) lock = true;
        }
        for (auto& [id, u] : state.objects) {
            if (!u.isUnit()) continue;
            if (!u.location.has_value()) {
                u.card_counters.erase(kSeenTurn);
                u.card_counters.erase(kSeenExhausted);
                continue;
            }
            const auto st = u.card_counters.find(kSeenTurn);
            const int seen_turn = st == u.card_counters.end() ? turn : st->second;
            const auto se = u.card_counters.find(kSeenExhausted);
            const bool seen_exhausted = se != u.card_counters.end() && se->second == 1;
            if (lock && seen_turn < turn && seen_exhausted && !u.is_exhausted &&
                u.controller == state.turn.turn_player)
                u.is_exhausted = true;   // it did not ready during the Awaken Phase
            u.card_counters[kSeenTurn] = turn;
            u.card_counters[kSeenExhausted] = u.is_exhausted ? 1 : 0;
        }
    }
private:
    static constexpr const char* kLastPass = "__fbr_last_pass_turn";
    static constexpr const char* kHeldTurn = "__fbr_held_turn";
    static constexpr const char* kSeenTurn = "__fbr_seen_turn";
    static constexpr const char* kSeenExhausted = "__fbr_seen_exhausted";

    const CardDef def_ = [] {
        CardDef d;
        d.id = 998;
        d.def_id = R"RB(rad-019-167)RB";
        d.name = R"RB(Firebrand Raider)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-019/167)RB";
        d.collector_number = 19;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Fury};
        d.energy_cost = 6;
        d.power_cost = 1;
        d.might = 6;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(I enter ready.
When you play me, ready all enemy units.
Other units enter ready.
Units don't ready during the Awaken Phase.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/922ef5a266378fa8ebd7e75bf83435bacd1a75d8-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_998(CardRegistry& r) {
    r.registerCard(998, std::make_unique<FirebrandRaider>());
}

}  // namespace riftbound
