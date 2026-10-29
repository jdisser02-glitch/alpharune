#include "agents/ismcts_search.h"

#include "core/events.h"
#include "core/game_object.h"
#include "engine/determinize.h"
#include "engine/game_engine.h"
#include "ml/feature_extractor.h"
#include "ml/value_net.h"
#include "openspiel/action_vocab.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <thread>
#include <unordered_map>
#include <variant>

namespace riftbound::search {

namespace {

constexpr int kMaxDepth = 200;   // decisions below the root before a forced evaluation

/// Root-perspective terminal value.
double terminalValue(const GameResult& r, PlayerId root) {
    if (r.winner == PlayerId::None) return 0.0;
    return r.winner == root ? 1.0 : -1.0;
}

/// "board": the score difference still dominates; the rest breaks ties
/// between positions with the same score — Might at the battlefields, who
/// holds more battlefields, cards and runes in reserve. Same weights as the
/// OpenSpiel evaluator so eval=board means the same thing for both agents.
double boardValue(const GameState& s, PlayerId me, double score_term) {
    const PlayerId opp = opponent(me);
    double might[2] = {0, 0}, held[2] = {0, 0}, runes[2] = {0, 0};
    for (const auto& [id, o] : s.objects) {
        if (!o.location.has_value()) continue;
        const int p = o.controller == me ? 0 : 1;
        if (o.isUnit() && o.isAtBattlefield()) might[p] += o.current_might;
        else if (o.isRune()) runes[p] += 1;
    }
    for (const auto& bf : s.battlefields)
        if (bf.controller.has_value()) held[*bf.controller == me ? 0 : 1] += 1;
    const double nbf = std::max<size_t>(1, s.battlefields.size());
    const double hand = static_cast<double>(s.player(me).hand.size()) -
                        static_cast<double>(s.player(opp).hand.size());
    return 0.55 * score_term
         + 0.20 * std::tanh((might[0] - might[1]) / 8.0)
         + 0.15 * (held[0] - held[1]) / nbf
         + 0.05 * std::tanh(hand / 4.0)
         + 0.05 * std::tanh((runes[0] - runes[1]) / 4.0);
}

/// Key of a move in the tree: its encoded action slot (openspiel/action_vocab.h)
/// refined by the play location, which the Play slot does not carry — playing
/// a unit at base and at a battlefield are different moves to the search even
/// though they are one policy slot today.
int moveKey(const Intent& in, const GameState& gs) {
    int loc = 0;
    if (in.play_location.has_value())
        if (const auto* bf = std::get_if<BattlefieldLocation>(&*in.play_location))
            loc = 1 + (bf->id != kInvalidId && static_cast<int>(bf->id) < 14 ? static_cast<int>(bf->id) : 0);
    return ::riftbound::openspiel::encodeAction(in, gs) * 16 + loc;
}

struct Node;

struct Edge {
    int slot = -1;        // moveKey of the action
    double prior = 0.0;   // unnormalised prior weight
    int visits = 0;
    int avail = 0;        // simulations in which this edge was legal at this node
    double sum = 0.0;     // root-perspective value sum
    std::unique_ptr<Node> child;
};

struct Node {
    bool expanded = false;
    int visits = 0;
    double sum = 0.0;
    std::vector<Edge> edges;

    Edge* find(int slot) {
        for (auto& e : edges) if (e.slot == slot) return &e;
        return nullptr;
    }
};

struct World {
    GameState state;        // hidden identities already re-dealt
    uint64_t seed = 1;      // engine RNG for this world
    IdentityOverrides overrides;   // for the replay path
};

/// Statistics of one root edge after merging threads.
struct RootStat {
    int visits = 0;
    double sum = 0.0;
};

}  // namespace

namespace {
std::atomic<long long> g_decisions{0}, g_sims{0}, g_failed{0}, g_replayed{0}, g_rejected{0};
std::atomic<long long> g_micros{0}, g_micros_replayed{0}, g_sims_replayed{0};
}  // namespace

GlobalSearchStats globalSearchStats() {
    GlobalSearchStats st;
    st.decisions = g_decisions.load();
    st.simulations = g_sims.load();
    st.failed = g_failed.load();
    st.replayed = g_replayed.load();
    st.replay_rejected = g_rejected.load();
    st.seconds = g_micros.load() / 1e6;
    st.seconds_replayed = g_micros_replayed.load() / 1e6;
    st.simulations_replayed = g_sims_replayed.load();
    return st;
}

PlayerId decidingPlayerOf(const GameState& state, const std::vector<Intent>& legal) {
    for (const auto& in : legal)
        if (in.player == PlayerId::Player1 || in.player == PlayerId::Player2) return in.player;
    if (state.turn.priority_holder.has_value()) return *state.turn.priority_holder;
    if (state.turn.focus_holder.has_value()) return *state.turn.focus_holder;
    return state.turn.turn_player;
}

IsMctsSearch::IsMctsSearch(const CardDB& db, const CardRegistry& registry,
                           DeckSubmission deck1, DeckSubmission deck2,
                           uint64_t engine_seed, int sims, SearchOptions opts)
    : db_(db), registry_(registry),
      deck1_(std::move(deck1)), deck2_(std::move(deck2)),
      engine_seed_(engine_seed), sims_(std::max(1, sims)), opts_(std::move(opts)) {
    if (opts_.eval.rfind("net:", 0) == 0) net_ = ml::ValueNet::shared(opts_.eval.substr(4));
    if (opts_.prior == "heuristic") prior_ = &IsMctsSearch::heuristicPrior;
}

IsMctsSearch::~IsMctsSearch() = default;

void IsMctsSearch::setPrior(PriorFn prior) { prior_ = std::move(prior); }

double IsMctsSearch::evaluate(const GameState& s, PlayerId me) const {
    if (s.game_over) {
        if (s.winner == PlayerId::None) return 0.0;
        return s.winner == me ? 1.0 : -1.0;
    }
    if (net_) return 2.0 * net_->winProbability(s, me, db_) - 1.0;
    const double victory = std::max(1, s.mode.victory_score);
    double v = static_cast<double>(s.player(me).score - s.player(opponent(me)).score) / victory;
    if (opts_.eval == "board") v = boardValue(s, me, v);
    return std::clamp(v, -1.0, 1.0);
}

std::vector<double> IsMctsSearch::heuristicPrior(const GameState&, const std::vector<Intent>& legal) {
    std::vector<double> w(legal.size(), 1.0);
    for (size_t i = 0; i < legal.size(); ++i) {
        const auto& it = legal[i];
        switch (it.type) {
            case IntentType::PlayCard:
            case IntentType::PlayReaction:
            case IntentType::PlayActionCard:
            case IntentType::AssignCombatDamage:
                w[i] = 4.0; break;
            case IntentType::StandardMove:
                w[i] = (it.move_destination.has_value() &&
                        std::holds_alternative<BattlefieldLocation>(*it.move_destination)) ? 3.0 : 0.6;
                break;
            case IntentType::ActivateAbility:
            case IntentType::ActivateReactionAbility:
            case IntentType::ActivateActionAbility:
                w[i] = 2.5; break;
            case IntentType::MakeChoice:
                w[i] = 1.5; break;
            case IntentType::EndTurn:
            case IntentType::PassPriority:
            case IntentType::PassFocus:
                w[i] = 0.3; break;
            case IntentType::Concede:
                w[i] = 0.01; break;
            default:
                w[i] = 1.0; break;
        }
    }
    return w;
}

std::vector<GameState> IsMctsSearch::sampleWorlds(const GameState& state, PlayerId perspective,
                                                  int n, uint64_t seed) const {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    auto draw = [&]() { return uni(rng); };
    std::vector<GameState> out;
    out.reserve(static_cast<size_t>(std::max(0, n)));
    for (int k = 0; k < n; ++k) {
        GameState ws = state;
        if (opts_.determinize) applyIdentityOverrides(ws, db_, sampleHiddenIdentities(ws, perspective, db_, draw));
        out.push_back(std::move(ws));
    }
    return out;
}

namespace {

/// One thread's search: its own worlds and tree. `run()` fills `root`.
class Worker {
public:
    Worker(const IsMctsSearch& search, const CardDB& db, const CardRegistry& registry,
           const DeckSubmission& d1, const DeckSubmission& d2, uint64_t engine_seed,
           const GameState& state, PlayerId root_player, const PriorFn& prior,
           int sims, int n_worlds, double c_puct, bool determinize, uint64_t seed,
           const std::vector<int32_t>* history)
        : search_(search), db_(db), registry_(registry), deck1_(d1), deck2_(d2),
          engine_seed_(engine_seed), state_(state), root_player_(root_player), prior_(prior),
          sims_(sims), c_puct_(c_puct), determinize_(determinize), rng_(seed), history_(history) {
        resumable_ = isResumableSnapshot(state_);
        can_replay_ = !deck1_.main_deck.empty() && !deck2_.main_deck.empty();
        root_.expanded = true;
        sampleWorlds(std::max(1, n_worlds));
        if (!resumable_ && can_replay_) root_view_ = ml::extractStateFeatures(state_, root_player_, db_);
    }

    void run() {
        static const bool dbg = std::getenv("RIFTBOUND_PMCTS_DEBUG") != nullptr;
        if (!resumable_ && !can_replay_) return;
        for (int s = 0; s < sims_; ++s) {
            try {
                simulate(worlds_[static_cast<size_t>(s) % worlds_.size()]);
            } catch (const std::exception& e) {
                ++failed_;
                if (dbg) std::fprintf(stderr, "[pmcts] decision %d: simulation threw: %s\n",
                                      state_.decision_index, e.what());
            } catch (...) {
                ++failed_;
                if (dbg) std::fprintf(stderr, "[pmcts] decision %d: simulation threw (unknown)\n", state_.decision_index);
            }
        }
    }

    Node& root() { return root_; }
    void setRootLegalSize(size_t n) { state_legal_size_ = n; }
    int completed() const { return completed_; }
    int failed() const { return failed_; }
    int rejected() const { return rejected_; }
    int worlds() const { return static_cast<int>(worlds_.size()); }
    bool resumable() const { return resumable_; }

private:
    void sampleWorlds(int n) {
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        auto draw = [&]() { return uni(rng_); };
        for (int k = 0; k < n; ++k) {
            World w;
            w.state = state_;
            if (determinize_) {
                w.overrides = sampleHiddenIdentities(w.state, root_player_, db_, draw);
                applyIdentityOverrides(w.state, db_, w.overrides);
            }
            w.seed = 1 + (rng_() >> 1);
            worlds_.push_back(std::move(w));
        }
    }

    /// A fresh engine at the world's decision point, or nullptr.
    std::unique_ptr<GameEngine> enter(const World& w, EventBus& bus) {
        auto engine = std::make_unique<GameEngine>(db_, bus, registry_);
        if (resumable_) {
            engine->resumeFromSnapshot(GameState(w.state), w.seed);
            return engine;
        }
        // In-flight decision: rebuild the real game from the start. With the
        // driver's decision history the replay is exact (legal indices);
        // without it, encoded action slots are replayed, which is lossy (one
        // slot may stand for several options) — either way the result is
        // checked against the acting player's view below. Then the sampled
        // identities and RNG take over (as RiftboundState::ensureLive).
        static const bool dbg = std::getenv("RIFTBOUND_PMCTS_DEBUG") != nullptr;
        StepResult sr = engine->beginGame(deck1_, deck2_, engine_seed_);
        size_t replayed = 0;
        if (history_) {
            for (int32_t idx : *history_) {
                if (sr.kind != StepKind::NeedDecision || idx < 0 || idx >= static_cast<int32_t>(sr.legal.size())) {
                    if (dbg) std::fprintf(stderr, "[pmcts] decision %d: index replay failed at %zu of %zu (kind %d, idx %d, %zu options)\n",
                                          state_.decision_index, replayed, history_->size(), static_cast<int>(sr.kind),
                                          static_cast<int>(idx), sr.legal.size());
                    return nullptr;
                }
                engine->mutableState().action_history.push_back(
                    ::riftbound::openspiel::encodeAction(sr.legal[static_cast<size_t>(idx)], engine->state()));
                sr = engine->applyChoice(idx);
                ++replayed;
            }
        } else {
            for (auto action_id : w.state.action_history) {
                if (sr.kind != StepKind::NeedDecision) return nullptr;
                const GameState& s = engine->state();
                int idx = -1;
                for (size_t i = 0; i < sr.legal.size(); ++i)
                    if (::riftbound::openspiel::encodeAction(sr.legal[i], s) == action_id) { idx = static_cast<int>(i); break; }
                if (idx < 0) {
                    if (dbg) std::fprintf(stderr, "[pmcts] decision %d: slot replay failed at %zu of %zu\n",
                                          state_.decision_index, replayed, w.state.action_history.size());
                    return nullptr;
                }
                engine->mutableState().action_history.push_back(action_id);
                sr = engine->applyChoice(idx);
                ++replayed;
            }
        }
        if (sr.kind != StepKind::NeedDecision || engine->state().decision_index != state_.decision_index ||
            sr.legal.size() != state_legal_size_) {
            if (dbg) std::fprintf(stderr, "[pmcts] decision %d: replay landed on decision %d with %zu options (want %zu)\n",
                                  state_.decision_index, engine->state().decision_index, sr.legal.size(), state_legal_size_);
            return nullptr;
        }
        applyIdentityOverrides(engine->mutableState(), db_, w.overrides);
        engine->reseedRng(w.seed);
        // The rebuilt world must look exactly like the real decision to the
        // acting player; otherwise the replay drifted (a god-mode edit, a
        // lossy slot) and searching it would not be honest.
        if (ml::extractStateFeatures(engine->state(), root_player_, db_) != root_view_) {
            ++rejected_;
            if (dbg) std::fprintf(stderr, "[pmcts] decision %d: rebuilt world differs from the acting player's view\n",
                                  state_.decision_index);
            return nullptr;
        }
        return engine;
    }

    void simulate(const World& w) {
        EventBus bus;
        auto engine = enter(w, bus);
        if (!engine) { ++failed_; return; }
        if (!resumable_ && dbg_once_++ == 0 && std::getenv("RIFTBOUND_PMCTS_DEBUG"))
            std::fprintf(stderr, "[pmcts] decision %d: in-flight decision rebuilt by replay\n", state_.decision_index);

        Node* node = &root_;
        std::vector<Edge*> path;
        double value = 0.0;
        int depth = 0;
        StepResult step = engine->currentStep();
        while (true) {
            if (step.kind != StepKind::NeedDecision) {
                value = terminalValue(engine->stepResult(), root_player_);
                break;
            }
            const GameState& gs = engine->state();
            // Nothing to choose, or which rune pays a cost (every option leaves
            // the same resources except the rune's domain): take the first.
            if (step.legal.size() <= 1 || gs.cost_cursor.has_value()) {
                step = engine->applyChoice(0);
                continue;
            }
            if (!node->expanded) {
                node->expanded = true;
                value = search_.evaluate(gs, root_player_);
                break;
            }
            if (depth >= kMaxDepth) {
                value = search_.evaluate(gs, root_player_);
                break;
            }
            const PlayerId actor = decidingPlayerOf(gs, step.legal);
            const int idx = select(*node, gs, step.legal, actor, path);
            node = path.back()->child.get();
            step = engine->applyChoice(idx);
            ++depth;
        }
        // Back up the root-perspective value along the path.
        root_.visits++;
        root_.sum += value;
        for (Edge* e : path) {
            e->visits++;
            e->sum += value;
            if (e->child) { e->child->visits++; e->child->sum += value; }
        }
        ++completed_;
    }

    /// PUCT over the options legal in this world; returns the legal index to
    /// apply and pushes the chosen edge on `path`.
    int select(Node& node, const GameState& gs, const std::vector<Intent>& legal,
               PlayerId actor, std::vector<Edge*>& path) {
        // Slot per option; the first option with a slot represents it.
        std::vector<int> slots(legal.size());
        std::vector<int> first;        // legal indices that own a slot
        first.reserve(legal.size());
        {
            std::unordered_map<int, int> seen;
            seen.reserve(legal.size() * 2);
            for (size_t i = 0; i < legal.size(); ++i) {
                slots[i] = moveKey(legal[i], gs);
                if (seen.emplace(slots[i], static_cast<int>(i)).second) first.push_back(static_cast<int>(i));
            }
        }
        // Edges for options this node has not seen yet, with their prior.
        bool need_prior = false;
        for (int i : first) if (!node.find(slots[i])) { need_prior = true; break; }
        if (need_prior) {
            std::vector<double> pw;
            if (prior_) pw = prior_(gs, legal);
            if (pw.size() != legal.size()) pw.assign(legal.size(), 1.0);
            for (size_t i = 0; i < legal.size(); ++i) {
                Edge* e = node.find(slots[i]);
                if (!e) {
                    node.edges.push_back(Edge{});
                    e = &node.edges.back();
                    e->slot = slots[i];
                    e->prior = 0.0;
                    // Options sharing a slot pool their prior weight.
                    for (size_t j = 0; j < legal.size(); ++j)
                        if (slots[j] == slots[i]) e->prior += std::max(0.0, pw[j]);
                }
            }
        }
        const double sign = actor == root_player_ ? 1.0 : -1.0;
        double prior_total = 0.0;
        for (int i : first) prior_total += node.find(slots[i])->prior;
        const double fpu = node.visits > 0 ? sign * node.sum / node.visits : 0.0;
        double best = -1e300;
        int best_idx = first.front();
        Edge* best_edge = nullptr;
        for (int i : first) {
            Edge* e = node.find(slots[i]);
            e->avail++;
            const double p = prior_total > 0 ? e->prior / prior_total : 1.0 / static_cast<double>(first.size());
            const double q = e->visits > 0 ? sign * e->sum / e->visits : fpu;
            const double u = c_puct_ * p * std::sqrt(static_cast<double>(e->avail)) / (1.0 + e->visits);
            double score = q + u;
            // Deterministic tie-break from the thread RNG.
            score += 1e-9 * std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
            if (score > best) { best = score; best_idx = i; best_edge = e; }
        }
        if (!best_edge->child) best_edge->child = std::make_unique<Node>();
        path.push_back(best_edge);
        return best_idx;
    }

    const IsMctsSearch& search_;
    const CardDB& db_;
    const CardRegistry& registry_;
    const DeckSubmission& deck1_;
    const DeckSubmission& deck2_;
    uint64_t engine_seed_;
    const GameState& state_;
    PlayerId root_player_;
    const PriorFn& prior_;
    int sims_;
    double c_puct_;
    bool determinize_;
    std::mt19937_64 rng_;
    const std::vector<int32_t>* history_ = nullptr;
    bool resumable_ = false;
    bool can_replay_ = false;
    std::vector<float> root_view_;      // the acting player's masked view (replay check)
    size_t state_legal_size_ = 0;
    std::vector<World> worlds_;
    Node root_;
    int completed_ = 0;
    int failed_ = 0;
    int rejected_ = 0;
    int dbg_once_ = 0;
};

}  // namespace

SearchResult IsMctsSearch::search(const GameState& state, const std::vector<Intent>& legal,
                                  PlayerId perspective, uint64_t seed,
                                  const std::vector<int32_t>* history) const {
    SearchResult out;
    out.visits.assign(legal.size(), 0);
    if (legal.empty()) return out;
    out.chosen = 0;
    if (legal.size() == 1) { out.shares = {{0, 1.0}}; out.visits[0] = 1; return out; }

    const int threads = std::max(1, opts_.threads);
    const double c = opts_.uct_c > 0 ? opts_.uct_c : 1.5;
    const int per_thread = std::max(1, (sims_ + threads - 1) / threads);
    const int n_worlds = opts_.worlds > 0 ? std::min(opts_.worlds, per_thread) : per_thread;

    std::vector<std::unique_ptr<Worker>> workers;
    workers.reserve(static_cast<size_t>(threads));
    for (int t = 0; t < threads; ++t) {
        const uint64_t s = seed ^ (0x9E3779B97F4A7C15ull * static_cast<uint64_t>(t + 1));
        workers.push_back(std::make_unique<Worker>(*this, db_, registry_, deck1_, deck2_, engine_seed_,
                                                   state, perspective, prior_, per_thread, n_worlds,
                                                   c, opts_.determinize, s, history));
        workers.back()->setRootLegalSize(legal.size());
    }
    if (threads == 1) {
        workers[0]->run();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(static_cast<size_t>(threads));
        for (auto& w : workers) pool.emplace_back([&w] { w->run(); });
        for (auto& th : pool) th.join();
    }

    // Merge root statistics by slot.
    std::map<int, RootStat> stats;
    double root_sum = 0.0;
    int root_visits = 0;
    for (auto& w : workers) {
        out.simulations += w->completed();
        out.failed += w->failed();
        out.replay_rejected += w->rejected();
        out.worlds += w->worlds();
        out.resumed = w->resumable();
        root_sum += w->root().sum;
        root_visits += w->root().visits;
        for (const auto& e : w->root().edges) {
            auto& st = stats[e.slot];
            st.visits += e.visits;
            st.sum += e.sum;
        }
    }
    out.root_value = root_visits > 0 ? root_sum / root_visits : 0.0;

    // Visits per legal option: the first option with a move key carries it.
    std::unordered_map<int, int> first_by_slot;
    std::vector<int> slot_of(legal.size());
    int total = 0;
    for (size_t i = 0; i < legal.size(); ++i) {
        slot_of[i] = moveKey(legal[i], state);
        if (!first_by_slot.emplace(slot_of[i], static_cast<int>(i)).second) continue;
        auto it = stats.find(slot_of[i]);
        if (it == stats.end()) continue;
        out.visits[i] = it->second.visits;
        total += it->second.visits;
    }
    if (total <= 0) return out;   // nothing searched: chosen stays 0, shares empty
    for (size_t i = 0; i < legal.size(); ++i)
        if (out.visits[i] > 0) out.shares.emplace_back(static_cast<int>(i), out.visits[i] / static_cast<double>(total));

    // Most visited; ties go to the higher mean value.
    int best = -1;
    double best_key = -1e300;
    for (size_t i = 0; i < legal.size(); ++i) {
        if (out.visits[i] <= 0) continue;
        const auto& st = stats[slot_of[i]];
        const double key = out.visits[i] + 0.5 * (st.sum / st.visits);
        if (key > best_key) { best_key = key; best = static_cast<int>(i); }
    }
    out.chosen = best < 0 ? 0 : best;
    return out;
}

// ─── PuctAgent ─────────────────────────────────────────────────────────────

PuctAgent::PuctAgent(const CardDB& db, const CardRegistry& registry,
                     DeckSubmission deck1, DeckSubmission deck2,
                     uint64_t engine_seed, uint64_t search_seed, int sims, SearchOptions opts)
    : search_(db, registry, std::move(deck1), std::move(deck2), engine_seed, sims, opts),
      rng_(search_seed ^ 0xD1B54A32D192ED03ull),
      final_max_(opts.final_policy != "sample") {}

Intent PuctAgent::selectAction(const GameState& state, const std::vector<Intent>& legal) {
    last_ = SearchResult{};
    if (legal.empty()) return Intent::concede(state.turn.turn_player);
    if (legal.size() == 1) return legal[0];
    // Which rune pays a cost is not worth a search (see Worker::simulate).
    if (state.cost_cursor.has_value()) return legal[0];

    const auto t0 = std::chrono::steady_clock::now();
    const PlayerId me = decidingPlayerOf(state, legal);
    last_ = search_.search(state, legal, me, rng_(), history_);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    total_seconds_ += secs;
    total_sims_ += last_.simulations;
    g_decisions.fetch_add(1);
    g_sims.fetch_add(last_.simulations);
    g_failed.fetch_add(last_.failed);
    g_replayed.fetch_add(last_.resumed ? 0 : 1);
    g_rejected.fetch_add(last_.replay_rejected);
    g_micros.fetch_add(static_cast<long long>(secs * 1e6));
    if (!last_.resumed) {
        g_micros_replayed.fetch_add(static_cast<long long>(secs * 1e6));
        g_sims_replayed.fetch_add(last_.simulations);
    }
    if (last_.shares.empty()) return legal[0];
    int pick = last_.chosen;
    if (!final_max_) {
        double r = std::uniform_real_distribution<double>(0.0, 1.0)(rng_), acc = 0.0;
        for (const auto& [i, share] : last_.shares) { acc += share; pick = i; if (acc >= r) break; }
    }
    if (pick < 0 || pick >= static_cast<int>(legal.size())) pick = 0;
    return legal[static_cast<size_t>(pick)];
}

}  // namespace riftbound::search
