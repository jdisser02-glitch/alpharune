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
    /// Might at battlefields, battlefields held, cards and runes),
    /// "net:<file>" (a learned ml::ValueNet) or "policy:<file>" (the value
    /// head of a two-head ml::PolicyNet, whose policy head also becomes the
    /// pmcts prior unless `prior` says otherwise).
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
    /// Prior over root/child actions: "auto" (the policy head when eval is
    /// "policy:<file>", else uniform), "uniform", "heuristic" (the action-type
    /// weights the perfect-information MctsAgent uses) or "net" (the policy
    /// head; requires eval=policy:<file>).
    std::string prior = "auto";
    /// Softmax temperature applied to the policy head's logits when it is
    /// the prior (> 1 flattens).
    double prior_temp = 1.0;
    /// Root action selection: "puct" (PUCT at every node) or "gumbel"
    /// (Gumbel-top-k sampling of `gumbel_m` root candidates from the prior,
    /// then sequential halving over them with the simulation budget —
    /// Danihelka et al. 2022 — which turns few simulations into a sound
    /// policy improvement when a policy prior is available; PUCT below the
    /// root).
    std::string root = "puct";
    int gumbel_m = 16;
};

} // namespace riftbound
