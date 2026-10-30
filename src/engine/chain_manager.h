#pragma once
/// @file chain_manager.h
/// Chain resolution manager — implements the FEPR loop.
///
/// FEPR = Finalize → Execute → Pass → Resolve
/// This is the core mechanic enabling spells, reactions, triggered abilities,
/// and activated abilities. Items stack on the chain and resolve LIFO.
///
/// Rules references: CR 335-340, 309, 312, 337.1.c, 354, 359

#include "core/card_db.h"
#include "core/events.h"
#include "core/game_state.h"
#include "core/intent.h"

#include <functional>
#include <vector>

namespace riftbound {

class EffectExecutor;

/// Callback to query an agent for an action.
/// Given a player and their legal actions, returns their chosen action.
using AgentQuery = std::function<Intent(PlayerId, const std::vector<Intent>&)>;

/// Where a fresh engine re-enters processFEPR when it resumes a snapshot
/// taken at a chain decision (GameState::decision_site).
enum class ChainResume : uint8_t {
    None,      // a new chain run
    Priority,  // at the ChainPriority decision: the priority holder is asked again
    Choice,    // at the ChainChoice decision: the resolving card's choice is asked again
};

class ChainManager {
public:
    ChainManager(GameState& state, EventBus& events, const CardDB& card_db);

    /// Add a spell to the chain as a Pending Item.
    /// Returns the chain item ID.
    ChainItemId addSpell(GameObjectId spell_obj, PlayerId controller,
                         const std::vector<GameObjectId>& targets);

    /// Add a permanent (unit/gear) to the chain as a Pending Item.
    /// Permanents resolve immediately on finalize (CR 337.1.c).
    ChainItemId addPermanent(GameObjectId card_obj, PlayerId controller,
                             const std::vector<GameObjectId>& targets = {});

    /// Add a triggered/activated ability to the chain. Source stays on
    /// board (not moved to chain zone). `is_activated` distinguishes
    /// "player chose to activate (e.g. [E]:)" from "event fired" — set
    /// it true from the ActivateAbility intent handler in the engine,
    /// false (default) from TriggerManager. Drives whether stepResolve
    /// dispatches through Card::onActivate or Card::onTrigger.
    ChainItemId addAbility(GameObjectId source, PlayerId controller,
                           CardDefId def_id,
                           const std::vector<GameObjectId>& targets = {},
                           bool is_activated = false,
                           int ability_index = 0);

    /// Callback to generate legal closed-state actions for a player.
    /// Injected from GameEngine so targeting and affordability are handled.
    using ClosedActionsGen = std::function<std::vector<Intent>(PlayerId)>;

    /// Run the FEPR loop until the chain is empty.
    /// query_agent is called whenever a player with priority must act.
    /// resolve_permanent is called when a permanent finalizes (bypasses chain).
    /// resolve_spell is called when a spell resolves.
    /// gen_closed_actions generates legal actions during Closed State.
    /// `resume` re-enters the loop at the decision a snapshot was taken at
    /// (its cursors are in GameState::decision_site) instead of starting a
    /// new round. Before every agent query the loop records which decision
    /// it is asking in `decision_site.kind`.
    void processFEPR(
        AgentQuery query_agent,
        std::function<void(const ChainItem&)> resolve_permanent,
        std::function<void(const ChainItem&)> resolve_spell,
        ClosedActionsGen gen_closed_actions = nullptr,
        ChainResume resume = ChainResume::None
    );

    /// Check if the chain currently exists.
    bool chainExists() const { return state_.chain.exists(); }

    /// Set affordability check callback (injected from GameEngine).
    using AffordCheck = std::function<bool(PlayerId, GameObjectId)>;
    void setAffordCheck(AffordCheck check) { can_afford_ = std::move(check); }

    /// Set cost payment callback (injected from GameEngine).
    using PayCost = std::function<bool(PlayerId, GameObjectId)>;
    void setPayCost(PayCost pay) { pay_cost_ = std::move(pay); }

    /// Activating an ability while the chain is open ([Reaction] abilities:
    /// a rune's Add, Gold, legends). Pays the cost and adds the item; returns
    /// true if an item was added.
    /// Called for each item as it is finalized, before anyone gets priority:
    /// the engine makes the choices the rules place at finalization (CR 355 —
    /// targets are chosen as the card or ability is played, and Deflect is
    /// paid then).
    using FinalizeChoices = std::function<void(ChainItem&)>;
    void setFinalizeChoices(FinalizeChoices fn) { finalize_choices_ = std::move(fn); }
    using ActivateAbility = std::function<bool(const Intent&)>;
    void setActivateAbility(ActivateAbility fn) { activate_ability_ = std::move(fn); }

    /// Inject the EffectExecutor so stepResolve can detect mid-resolution
    /// pending choices published by Card::onResolve / onTrigger via
    /// `requestChoice`. Required before processFEPR runs.
    void setEffectExecutor(EffectExecutor* exec) { executor_ = exec; }

    /// Generate legal actions for a player with priority in Closed State.
    std::vector<Intent> generateClosedStateActions(PlayerId player) const;

private:
    GameState& state_;
    EventBus& events_;
    const CardDB& card_db_;
    AffordCheck can_afford_;
    PayCost pay_cost_;
    ActivateAbility activate_ability_;
    FinalizeChoices finalize_choices_;
    EffectExecutor* executor_ = nullptr;

    /// Step 1: Finalize all pending items in order.
    /// Returns true if any items were finalized.
    bool stepFinalize(
        std::function<void(const ChainItem&)> resolve_permanent
    );

    /// Steps 2-3: Execute (player acts) and Pass (check all passed).
    /// Returns true if an item was added (restart from Finalize).
    /// `resume`: continue the round the snapshot was taken in (the priority
    /// holder and the passes so far are in the state) instead of a new one.
    bool stepExecuteAndPass(AgentQuery query_agent,
                            ClosedActionsGen gen_closed_actions,
                            bool resume = false);

    /// Step 4: Resolve the top item on the chain. Drives a resumable loop:
    /// while the resolving Card publishes a pending choice via
    /// `EffectExecutor::requestChoice`, query the agent + record the
    /// choice + re-invoke resolve_spell so the Card's next switch case
    /// runs. Single-shot resolution (the common case) loops once.
    /// `resume`: the item is already in `chain.resuming` and the card's
    /// pending choice (GameState::decision_site) is asked first.
    void stepResolve(AgentQuery query_agent,
                     std::function<void(const ChainItem&)> resolve_spell,
                     bool resume = false);

    /// Nesting depth of processFEPR. A decision asked while another chain run
    /// is on the stack is recorded as DecisionSiteKind::Other: its progress is
    /// in C++ frames a resumed engine cannot rebuild.
    int depth_ = 0;
};

} // namespace riftbound
