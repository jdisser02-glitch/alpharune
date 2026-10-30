#pragma once
/// @file search_options.h
/// Search settings shared by the search agents, parsed from an agent spec
/// such as "ismcts:sims=200,eval=board,c=1.4" or
/// "pmcts:sims=100,eval=net:lab/learn/nets/net_004.rbvn,threads=4,worlds=8"
/// (parseSearchSpecOptions below).
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
    /// Settle a leaf before evaluating it: at most this many decisions
    /// (0 = off). A leaf that is not a quiet decision — a main-phase action
    /// with nothing on the chain and no showdown or combat under way
    /// (search::isQuietDecision) — is played on with a cheap default policy
    /// (pass priority / focus when offered, else the first option: combat
    /// damage, cost payment, a card's choice) until it is quiet or the game
    /// ends, so a move that starts a combat is valued after the combat, not
    /// in the middle of its showdown. Used by pmcts and by the OpenSpiel
    /// ismcts agent's leaf evaluator.
    int settle = 0;

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

/// Apply one `key=value` of an agent spec to `opts`. Returns false when `key`
/// is not a search option (sims= and anything else is the caller's).
inline bool applySearchOption(SearchOptions& opts, const std::string& key, const std::string& val) {
    if (key == "eval") opts.eval = val;
    else if (key == "c") opts.uct_c = std::stod(val);
    else if (key == "det") opts.determinize = val != "0";
    else if (key == "final") opts.final_policy = val;
    else if (key == "settle") opts.settle = std::stoi(val);
    else if (key == "threads") opts.threads = std::stoi(val);
    else if (key == "worlds") opts.worlds = std::stoi(val);
    else if (key == "prior") opts.prior = val;
    else if (key == "prior_temp") opts.prior_temp = std::stod(val);
    else if (key == "root") opts.root = val;
    else if (key == "gumbel_m") opts.gumbel_m = std::stoi(val);
    else return false;
    return true;
}

/// Parse the options of an agent spec, the part after "kind:": comma-
/// separated `key=value` pairs such as "sims=50,eval=board,settle=60". Search
/// options go to `opts`, `sims=` to `*sims` (when given); other keys and
/// entries without '=' are ignored.
inline void parseSearchSpecOptions(const std::string& rest, SearchOptions& opts, int* sims = nullptr) {
    size_t i = 0;
    while (i < rest.size()) {
        const auto eq = rest.find('=', i);
        auto comma = rest.find(',', i);
        if (comma == std::string::npos) comma = rest.size();
        if (eq != std::string::npos && eq < comma) {
            const std::string key = rest.substr(i, eq - i);
            const std::string val = rest.substr(eq + 1, comma - eq - 1);
            if (key == "sims") {
                if (sims) *sims = std::stoi(val);
            } else {
                applySearchOption(opts, key, val);
            }
        }
        i = comma + 1;
    }
}

} // namespace riftbound
