#pragma once
/// @file search_options.h
/// Search settings shared by the search agents, parsed from an agent spec
/// such as "ismcts:sims=200,eval=board,c=1.4" or
/// "pmcts:sims=100,eval=net:lab/learn/nets/net_004.rbvn,threads=4,worlds=8".
/// Lives apart from mcts_agent.h so the engine library's own searcher
/// (agents/ismcts_search.h) can use it without the OpenSpiel adapters.

#include <string>

namespace riftbound {

struct SearchOptions {
    /// Leaf evaluation: "score" (score difference only), "board" (score,
    /// Might at battlefields, battlefields held, cards and runes) or
    /// "net:<file>" (a learned ml::ValueNet).
    std::string eval = "score";
    /// Exploration constant; <= 0 keeps the agent's default.
    double uct_c = 0.0;
    /// Sample hidden information per simulation (honest search).
    /// false = search the real state (sees the opponent's hand and deck order).
    bool determinize = true;
    /// Final move: "max" (most visited) or "sample" (visit-proportional).
    std::string final_policy = "max";

    // ── pmcts (agents/ismcts_search.h) only ──
    /// Root-parallel search threads; each owns its worlds and tree, visits
    /// are merged at the root. <= 0 means one.
    int threads = 1;
    /// Determinizations sampled per thread at the root and reused round-robin
    /// across its simulations. <= 0 means one world per simulation.
    int worlds = 8;
    /// Prior over root/child actions: "uniform" or "heuristic" (the
    /// action-type weights the perfect-information MctsAgent uses).
    std::string prior = "uniform";
};

} // namespace riftbound
