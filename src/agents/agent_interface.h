#pragma once
/// @file agent_interface.h
/// Abstract interface for game-playing agents.
///
/// An agent receives the current game state and a list of legal actions,
/// and returns the action it chooses. This interface is implemented by:
///   - RandomAgent (uniform random selection)
///   - Future: scripted agents, MCTS, neural network policies

#include "core/game_state.h"
#include "core/intent.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace riftbound {

class AgentInterface {
public:
    virtual ~AgentInterface() = default;

    /// Select an action from the list of legal actions given the current state.
    virtual Intent selectAction(
        const GameState& state,
        const std::vector<Intent>& legal_actions
    ) = 0;

    /// After selectAction: how the agent's search split over the options it
    /// was given, as (index into legal_actions, share of the search). Empty
    /// for agents that do not search. Used for game review.
    virtual std::vector<std::pair<int, double>> lastSearch() const { return {}; }

    /// After selectAction: simulations behind lastSearch() (0 = no search).
    /// Recorded with the policy targets (ml::ValueSample::sims).
    virtual int lastSearchSims() const { return 0; }

    /// The game driver may hand the agent the running record of every
    /// decision so far (both players): the index chosen in each decision's
    /// legal list, in order. A search agent replays it from the start of the
    /// game to rebuild decisions it cannot resume from a snapshot (chain
    /// priority, showdowns, combat). The vector outlives the agent's use of
    /// it and only grows. Default: ignored.
    virtual void setDecisionHistory(const std::vector<int32_t>* /*history*/) {}
};

} // namespace riftbound
