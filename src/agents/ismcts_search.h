#pragma once
/// @file ismcts_search.h
/// The engine's own information-set Monte-Carlo tree search (agent spec
/// `pmcts`). Replaces OpenSpiel's ISMCTSBot for self-play and the lab:
///
///   • Honest. The acting player never sees hidden information: every world
///     the search runs in is re-dealt from that player's information set by
///     search::sampleHiddenIdentities (the same code the OpenSpiel path uses in
///     RiftboundState::ResampleFromInfostate).
///   • Root determinization reuse. Instead of one resample per simulation,
///     each thread samples `worlds` determinizations once at the root and
///     cycles through them; a simulation costs one GameState copy plus
///     GameEngine::resumeFromSnapshot (or an action_history replay when the
///     decision is in flight: chain priority, showdown, combat, mulligan).
///   • PUCT selection with a prior hook (uniform by default, a place for a
///     policy network) and the ISMCTS availability correction: a child's
///     exploration term counts the simulations in which it was legal.
///   • Leaf evaluation by ml::ValueNet (`eval=net:<file>`), or the score /
///     board heuristics, always from the acting player's masked view.
///   • Root parallelism: `threads` searchers with their own worlds and trees
///     (Chaslot et al. 2008); root visits are summed. Visit shares are exported
///     through AgentInterface::lastSearch for review and policy targets.
///
/// Tree nodes are keyed by the action path from the root in encoded slots
/// (openspiel/action_vocab.h), so a node pools the statistics of every
/// sampled world that reaches it — the single-observer ISMCTS tree.

#include "agents/agent_interface.h"
#include "agents/search_options.h"
#include "cards/card_registry.h"
#include "core/card_db.h"
#include "core/game_state.h"
#include "core/intent.h"
#include "rules/deck_validator.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace riftbound::ml { class ValueNet; }

namespace riftbound::search {

/// Prior over the legal options at a decision: one non-negative weight per
/// entry of `legal` (normalised by the search). The default is uniform; a
/// policy network plugs in here.
using PriorFn = std::function<std::vector<double>(const GameState&, const std::vector<Intent>&)>;

struct SearchResult {
    int chosen = -1;                              ///< index into the root's legal list
    std::vector<std::pair<int, double>> shares;   ///< (legal index, visit share), AgentInterface::lastSearch
    std::vector<int> visits;                      ///< root visits per legal index (0 for options sharing a slot)
    double root_value = 0.0;                      ///< mean backed-up value for the acting player, [-1, 1]
    int simulations = 0;                          ///< simulations that reached a value
    int failed = 0;                               ///< simulations abandoned on an engine error
    int worlds = 0;                               ///< determinizations sampled (all threads)
    bool resumed = true;                          ///< false: the root was rebuilt by replaying the game
    int replay_rejected = 0;                      ///< rebuilt worlds that did not match the acting player's view
};

class IsMctsSearch {
public:
    /// `deck1` / `deck2` and `engine_seed` MUST match the live game: a decision
    /// that cannot be resumed from its snapshot (search::isResumableSnapshot)
    /// is rebuilt from a fresh beginGame by replaying the decision history
    /// (`history`: the chosen legal index of every decision so far, exact) or,
    /// without one, `GameState::action_history` (encoded slots, lossy). Each
    /// rebuilt world is checked against the acting player's masked view and
    /// discarded when it differs. With empty decks such decisions are not
    /// searched.
    IsMctsSearch(const CardDB& db, const CardRegistry& registry,
                 DeckSubmission deck1, DeckSubmission deck2,
                 uint64_t engine_seed, int sims, SearchOptions opts = {});
    ~IsMctsSearch();

    /// Replace the prior (default: uniform, or the action-type heuristic when
    /// SearchOptions::prior == "heuristic").
    void setPrior(PriorFn prior);

    /// Run the search for `perspective`, who must decide among `legal` at
    /// `state`. `seed` drives world sampling and tie-breaks: the same inputs
    /// and seed give the same result (per thread; thread merging is
    /// order-independent).
    SearchResult search(const GameState& state, const std::vector<Intent>& legal,
                        PlayerId perspective, uint64_t seed,
                        const std::vector<int32_t>* history = nullptr) const;

    /// Leaf value for `perspective` in [-1, 1] under SearchOptions::eval.
    double evaluate(const GameState& state, PlayerId perspective) const;

    /// Sample `n` determinizations of `state` from `perspective`'s view, as
    /// the search does at the root (exposed for tests).
    std::vector<GameState> sampleWorlds(const GameState& state, PlayerId perspective,
                                        int n, uint64_t seed) const;

    const SearchOptions& options() const { return opts_; }
    int sims() const { return sims_; }

    /// Action-type weights of the perfect-information MctsAgent's prior
    /// (plays, moves to battlefields and combat first; passes last).
    static std::vector<double> heuristicPrior(const GameState& state, const std::vector<Intent>& legal);

private:
    const CardDB& db_;
    const CardRegistry& registry_;
    DeckSubmission deck1_, deck2_;
    uint64_t engine_seed_;
    int sims_;
    SearchOptions opts_;
    PriorFn prior_;
    std::shared_ptr<const ml::ValueNet> net_;
};

/// The player who makes a decision: the one the options name, else the
/// priority / focus / turn player.
PlayerId decidingPlayerOf(const GameState& state, const std::vector<Intent>& legal);

/// Process-wide totals over every PuctAgent decision (throughput reports).
struct GlobalSearchStats {
    long long decisions = 0;      ///< searched decisions
    long long simulations = 0;    ///< simulations that reached a value
    long long failed = 0;         ///< simulations abandoned on an engine error
    long long replayed = 0;       ///< decisions rebuilt by replay
    long long replay_rejected = 0;///< rebuilt worlds discarded (did not match the acting player's view)
    double seconds = 0.0;         ///< wall time inside the search
    double seconds_replayed = 0.0;///< part of `seconds` spent on decisions rebuilt by replay
    long long simulations_replayed = 0;  ///< simulations run on rebuilt decisions
};
GlobalSearchStats globalSearchStats();

/// AgentInterface over IsMctsSearch — the `pmcts` agent.
class PuctAgent : public AgentInterface {
public:
    PuctAgent(const CardDB& db, const CardRegistry& registry,
              DeckSubmission deck1, DeckSubmission deck2,
              uint64_t engine_seed, uint64_t search_seed, int sims, SearchOptions opts = {});

    Intent selectAction(const GameState& state, const std::vector<Intent>& legal_actions) override;
    std::vector<std::pair<int, double>> lastSearch() const override { return last_.shares; }
    void setDecisionHistory(const std::vector<int32_t>* history) override { history_ = history; }

    const SearchResult& lastResult() const { return last_; }
    IsMctsSearch& searcher() { return search_; }
    /// Totals over this agent's decisions (for throughput reports).
    long long totalSimulations() const { return total_sims_; }
    double totalSearchSeconds() const { return total_seconds_; }

private:
    IsMctsSearch search_;
    const std::vector<int32_t>* history_ = nullptr;
    std::mt19937_64 rng_;
    bool final_max_ = true;
    SearchResult last_;
    long long total_sims_ = 0;
    double total_seconds_ = 0.0;
};

}  // namespace riftbound::search
