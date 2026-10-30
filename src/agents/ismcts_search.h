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
///     GameEngine::resumeFromSnapshot, which re-enters the engine at the
///     decision (GameState::decision_site) — also in flight: chain priority,
///     a resolving card's choice, showdown focus, combat damage, mulligan.
///     Only a decision inside a card effect or cost payment is rebuilt by
///     replaying the game. An in-flight resume is checked against the real
///     decision (same options, same view for the acting player).
///   • PUCT selection with a prior hook (uniform by default; the policy head
///     of an ml::PolicyNet with `eval=policy:<file>`) and the ISMCTS
///     availability correction: a child's exploration term counts the
///     simulations in which it was legal. `root=gumbel` replaces PUCT at the
///     root by Gumbel-top-k sampling plus sequential halving over the prior's
///     candidates (Danihelka et al. 2022), for few-simulation searches.
///   • Leaf evaluation by ml::ValueNet (`eval=net:<file>`), the value head of
///     a two-head ml::PolicyNet (`eval=policy:<file>`, whose policy head is
///     then the PUCT prior), or the score / board heuristics, always from the
///     acting player's masked view. With `settle=N` a leaf in the middle of a
///     chain, showdown or combat is first played on by a default policy
///     (passes, else first options) until it is quiet (isQuietDecision).
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

namespace riftbound::ml { class ValueNet; class PolicyNet; }

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
    bool resumed_inflight = false;                ///< the root is an in-flight decision resumed in place (not a clean main-phase boundary)
    int replay_rejected = 0;                      ///< rebuilt or resumed worlds that did not match the real decision (options, acting player's view)
    int settled = 0;                              ///< leaves played on before evaluation (SearchOptions::settle)
    int settle_steps = 0;                         ///< decisions the settle policy took over those leaves
    int settle_capped = 0;                        ///< settled leaves still not quiet when the step cap ran out
};

/// A decision the search may evaluate as it stands: the turn player's
/// main-phase action (GameState::decision_site.kind == MainPhase) with nothing
/// on the chain, no cost being paid and no showdown or combat staged or under
/// way; also a mulligan, and a state whose site was never recorded (None).
/// Chain priority, a resolving card's choice, showdown focus, combat damage
/// and decisions inside a card effect or cost payment are not quiet.
bool isQuietDecision(const GameState& state);

/// The settle policy's pick among `legal`: pass priority or pass focus when
/// offered, otherwise the first option (combat damage assignment, cost
/// payment, a card's choice).
int settleChoice(const std::vector<Intent>& legal);

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

    /// Replace the prior (default: uniform; the action-type heuristic when
    /// SearchOptions::prior == "heuristic"; the policy head when eval is
    /// "policy:<file>" and prior is "auto" or "net").
    void setPrior(PriorFn prior);
    /// True when a prior other than uniform is installed.
    bool hasPrior() const { return static_cast<bool>(prior_); }
    /// The two-head network behind eval=policy:<file>, if any.
    const ml::PolicyNet* policyNet() const { return pnet_.get(); }
    /// Use an already loaded value net as the leaf evaluation (the browser
    /// engine has no file to name in SearchOptions::eval).
    void setValueNet(std::shared_ptr<const ml::ValueNet> net) { net_ = std::move(net); }
    /// Replace the leaf evaluation of a non-terminal state by `fn` (value
    /// for the player given, in [-1, 1]); an empty function restores
    /// SearchOptions::eval. For custom heuristics and tests.
    void setEvaluator(std::function<double(const GameState&, PlayerId)> fn) { eval_fn_ = std::move(fn); }

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
    std::function<double(const GameState&, PlayerId)> eval_fn_;
    std::shared_ptr<const ml::ValueNet> net_;
    std::shared_ptr<const ml::PolicyNet> pnet_;
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
    long long resumed_inflight = 0;  ///< in-flight decisions (chain, showdown, combat, mulligan) resumed in place
    long long replay_rejected = 0;///< rebuilt or resumed worlds discarded (did not match the real decision)
    double seconds = 0.0;         ///< wall time inside the search
    double seconds_replayed = 0.0;///< part of `seconds` spent on decisions rebuilt by replay
    long long simulations_replayed = 0;  ///< simulations run on rebuilt decisions
    long long settled = 0;        ///< leaves played on before evaluation (settle=N)
    long long settle_steps = 0;   ///< decisions the settle policy took
    long long settle_capped = 0;  ///< settled leaves that hit the step cap before turning quiet
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
    int lastSearchSims() const override { return last_.simulations; }
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
