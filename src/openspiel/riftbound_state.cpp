#include "riftbound_state.h"
#include "riftbound_game.h"

#include "action_vocab.h"

#include "engine/determinize.h"
#include "ml/cfr_util.h"
#include "ml/feature_extractor.h"

static_assert(::riftbound::openspiel::kNumCardDefIds == ::riftbound::ml::kCardVocabSize,
              "action vocab and state-feature card id spaces must match");

#include "open_spiel/spiel_utils.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <cstdio>
#include <cstring>
#include <random>
#include <unordered_set>
#include <utility>

namespace riftbound::openspiel {

namespace {

::open_spiel::Player playerIdToOpenSpiel(::riftbound::PlayerId pid) {
    if (pid == ::riftbound::PlayerId::Player1) return 0;
    if (pid == ::riftbound::PlayerId::Player2) return 1;
    return ::open_spiel::kTerminalPlayerId;
}

} // namespace

RiftboundState::RiftboundState(std::shared_ptr<const ::open_spiel::Game> game)
    : RiftboundState(game,
                      static_cast<const RiftboundGame*>(game.get())->seed() != 0
                        ? static_cast<const RiftboundGame*>(game.get())->seed()
                        : std::random_device{}()) {}

RiftboundState::RiftboundState(std::shared_ptr<const ::open_spiel::Game> game,
                                uint64_t engine_seed)
    : ::open_spiel::State(game),
      rb_game_(*static_cast<const RiftboundGame*>(game.get())),
      event_bus_(std::make_unique<::riftbound::EventBus>()),
      engine_(std::make_unique<::riftbound::GameEngine>(
          rb_game_.cardDb(), *event_bus_, rb_game_.cardRegistry())),
      is_lazy_(false),
      engine_seed_(engine_seed) {
    // Engine spawns its own worker thread internally and runs until the
    // first decision (or termination). Returns the first StepResult,
    // which we'll consult via engine_->currentStep().
    engine_->beginGame(rb_game_.deck1(), rb_game_.deck2(), engine_seed_);
}

RiftboundState::RiftboundState(std::shared_ptr<const ::open_spiel::Game> game,
                                uint64_t engine_seed,
                                ::riftbound::GameState snap_state,
                                ::riftbound::StepResult snap_step,
                                ::riftbound::GameResult snap_result,
                                SnapshotTag)
    : ::open_spiel::State(game),
      rb_game_(*static_cast<const RiftboundGame*>(game.get())),
      is_lazy_(true),
      lazy_state_(std::move(snap_state)),
      lazy_step_(std::move(snap_step)),
      lazy_result_(std::move(snap_result)),
      engine_seed_(engine_seed) {
    // No event bus, no engine, no worker thread. Snapshot fields hold
    // the answers. ensureLive() will materialize the engine on the
    // first mutating call.
}

void RiftboundState::ensureLive() const {
    if (!is_lazy_) return;

    // Phase 6c: Use resumeFromSnapshot to skip the replay path when the
    // snapshot is at a "clean" decision boundary — Neutral / Open state
    // with no in-flight chain, no mid-combat BF, no cost-payment cursor.
    // For these states, the fresh engine can call mainPhaseLoop and the
    // side-effectful re-entries (processContestedBattlefields, etc.) are
    // idempotent against the snapshot.
    //
    // Falls back to the legacy replay path for snapshots taken mid-chain
    // / mid-showdown / mid-combat / mid-cost-payment, where the resume
    // would race against side effects already captured in the snapshot
    // and produce a divergent state. Those are rarer than open-state
    // main-phase clones (which dominate MCTS expansion).
    //
    // The trade-off: open-state clones get the ~33x speedup from
    // resumeFromSnapshot; in-flight clones pay the legacy 5ms replay.
    // This is the safest landing — full correctness, partial wins. The
    // full memcpy refactor (no replay anywhere) requires extracting the
    // entry-side-effects from EVERY phase function + ChainManager
    // processFEPR, which is multi-day work; queued in CLAUDE.md.
    // The same predicate the engine's own searcher uses (engine/determinize.h).
    const bool snapshot_is_clean_boundary = ::riftbound::search::isResumableSnapshot(lazy_state_);
    bool snapshot_is_resumable = snapshot_is_clean_boundary;

    // Move state off the lazy fields before flipping. Snapshot still
    // holds action_history (needed only by the replay fallback).
    ::riftbound::GameState moved_state = std::move(lazy_state_);

    // Build the engine. Order matters: event_bus_ must outlive engine_.
    event_bus_ = std::make_unique<::riftbound::EventBus>();
    engine_ = std::make_unique<::riftbound::GameEngine>(
        rb_game_.cardDb(), *event_bus_, rb_game_.cardRegistry());

    is_lazy_ = false;
    lazy_state_ = ::riftbound::GameState{};
    lazy_step_ = ::riftbound::StepResult{};
    lazy_result_ = ::riftbound::GameResult{};

    if (snapshot_is_resumable) {
        // Fast path — skip beginGame + replay entirely. resumeFromSnapshot
        // initialises subsystems on the new EventBus and spawns the worker
        // thread which dispatches based on state_.turn.phase.
        engine_->resumeFromSnapshot(std::move(moved_state),
                                    world_seed_ != 0 ? world_seed_ : engine_seed_);
    } else {
        // Legacy replay path for the few clones that snapshot during
        // Setup/Mulligan. Re-extract action history from the moved
        // state and replay.
        std::vector<int64_t> history_to_replay =
            std::move(moved_state.action_history);
        engine_->beginGame(rb_game_.deck1(), rb_game_.deck2(), engine_seed_);
        // A determinized world: the replay first rebuilds the real game up to
        // the moment the world was sampled, then the sampled identities and
        // RNG take over (see ResampleFromInfostate).
        auto applyWorld = [this]() {
            if (identity_overrides_.empty()) return;
            applyIdentityOverrides(engine_->mutableState());
            if (world_seed_ != 0) engine_->reseedRng(world_seed_);
        };
        size_t replayed = 0;
        if (override_history_len_ == 0) applyWorld();
        for (auto action_id : history_to_replay) {
            if (engine_->isStepDone()) break;
            if (static_cast<int>(replayed) == override_history_len_ && replayed > 0) applyWorld();
            ++replayed;
            const auto& legal = engine_->currentStep().legal;
            const auto& state = engine_->state();
            int legal_index = -1;
            for (size_t i = 0; i < legal.size(); ++i) {
                if (encodeAction(legal[i], state) == action_id) {
                    legal_index = static_cast<int>(i);
                    break;
                }
            }
            if (legal_index < 0) break;
            engine_->mutableState().action_history.push_back(action_id);
            engine_->applyChoice(legal_index);
        }
        if (override_history_len_ > 0 && static_cast<int>(replayed) == override_history_len_ &&
            !engine_->isStepDone())
            applyWorld();
    }
}

::open_spiel::Player RiftboundState::CurrentPlayer() const {
    if (is_lazy_) {
        if (lazy_step_.kind == ::riftbound::StepKind::Done) {
            return ::open_spiel::kTerminalPlayerId;
        }
        return playerIdToOpenSpiel(lazy_step_.perspective);
    }
    if (engine_->isStepDone()) return ::open_spiel::kTerminalPlayerId;
    return playerIdToOpenSpiel(engine_->currentStep().perspective);
}

std::vector<::open_spiel::Action> RiftboundState::LegalActions() const {
    const ::riftbound::GameState* state = nullptr;
    const std::vector<::riftbound::Intent>* legal = nullptr;
    if (is_lazy_) {
        if (lazy_step_.kind == ::riftbound::StepKind::Done) return {};
        state = &lazy_state_;
        legal = &lazy_step_.legal;
    } else {
        if (engine_->isStepDone()) return {};
        // currentStep() returns by value — bind to a static thread-local
        // would be racy. We can call .legal on a fresh temporary safely
        // by keeping the temporary alive for the duration of this block.
        // Easier: take the current step into a local.
        auto step = engine_->currentStep();
        const auto& s = engine_->state();
        std::vector<::open_spiel::Action> out;
        out.reserve(step.legal.size());
        std::unordered_set<int64_t> seen;
        seen.reserve(step.legal.size());
        for (const auto& intent : step.legal) {
            int64_t id = encodeAction(intent, s);
            if (seen.insert(id).second) {
                out.push_back(static_cast<::open_spiel::Action>(id));
            }
        }
        return out;
    }
    std::vector<::open_spiel::Action> out;
    out.reserve(legal->size());
    std::unordered_set<int64_t> seen;
    seen.reserve(legal->size());
    for (const auto& intent : *legal) {
        int64_t id = encodeAction(intent, *state);
        if (seen.insert(id).second) {
            out.push_back(static_cast<::open_spiel::Action>(id));
        }
    }
    return out;
}

std::string RiftboundState::ActionToString(::open_spiel::Player,
                                            ::open_spiel::Action a) const {
    if (is_lazy_) {
        if (lazy_step_.kind == ::riftbound::StepKind::Done) return "<terminal>";
        if (const auto* intent = decodeAction(a, lazy_step_.legal, lazy_state_)) {
            return intent->describe();
        }
        return "<unknown action_id>";
    }
    if (engine_->isStepDone()) return "<terminal>";
    auto step = engine_->currentStep();
    if (const auto* intent = decodeAction(a, step.legal, engine_->state())) {
        return intent->describe();
    }
    return "<unknown action_id>";
}

std::string RiftboundState::ToString() const {
    if (is_lazy_) {
        if (lazy_step_.kind == ::riftbound::StepKind::Done) {
            return std::string("<terminal: ")
                + lazy_result_.termination_reason + ">";
        }
        return std::string("Riftbound state @ turn ")
            + std::to_string(lazy_state_.turn.turn_number);
    }
    if (engine_->isStepDone()) {
        return std::string("<terminal: ")
            + engine_->stepResult().termination_reason + ">";
    }
    return std::string("Riftbound state @ turn ")
        + std::to_string(engine_->state().turn.turn_number);
}

bool RiftboundState::IsTerminal() const {
    // Hard cap on action count. MCTS's RandomRolloutEvaluator runs
    // rollouts to terminal with no internal length bound — if the
    // engine reaches a state that can't progress under random play
    // (mutual stalemate, forced multi-step sequences with k=1),
    // rollouts otherwise run unbounded inside MCTSBot::MCTSearch.
    // Cap matches the outer-game max_decisions_per_game so rollouts
    // and outer games agree on game length. Returns() returns {0,0}
    // (draw) when forcibly terminated — the right MCTS signal for
    // "unknown outcome / out of budget."
    if (MoveNumber() >= 600) return true;
    if (is_lazy_) return lazy_step_.kind == ::riftbound::StepKind::Done;
    return engine_->isStepDone();
}

std::vector<double> RiftboundState::Returns() const {
    const ::riftbound::GameResult* result = nullptr;
    if (is_lazy_) {
        if (lazy_step_.kind != ::riftbound::StepKind::Done) return {0.0, 0.0};
        result = &lazy_result_;
    } else {
        if (!engine_->isStepDone()) return {0.0, 0.0};
        result = &engine_->stepResult();
    }
    if (result->winner == ::riftbound::PlayerId::Player1) return {1.0, -1.0};
    if (result->winner == ::riftbound::PlayerId::Player2) return {-1.0, 1.0};
    return {0.0, 0.0};
}

uint64_t RiftboundState::InfoSetId(::open_spiel::Player player) const {
    // Thin wrapper: extract masked features + delegate hashing to the
    // pure ml::computeInfoSetId so the hash function itself is unit-
    // testable without a full game.
    ::riftbound::PlayerId rb_player = (player == 0)
        ? ::riftbound::PlayerId::Player1
        : ::riftbound::PlayerId::Player2;
    const ::riftbound::GameState& gs =
        is_lazy_ ? lazy_state_ : engine_->state();
    auto feats = ::riftbound::ml::extractStateFeatures(
        gs, rb_player, rb_game_.cardDb());
    return ::riftbound::ml::computeInfoSetId(feats, player);
}

std::string RiftboundState::ObservationString(::open_spiel::Player player) const {
    // Compact hex of the InfoSetId — ISMCTSBot keys node lookups on
    // this string. 16 hex chars = 64-bit hash, tight map keys.
    uint64_t h = InfoSetId(player);
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016lx", static_cast<unsigned long>(h));
    return std::string(buf);
}

void RiftboundState::ObservationTensor(::open_spiel::Player player,
                                        absl::Span<float> values) const {
    // extractStateFeatures already implements information-set masking:
    //   - opponent hand identities are NOT serialized (is_self gate)
    //   - opponent main_deck identities are NEVER emitted (only the SELF
    //     deck/trash/banishment + opponent's PUBLIC trash/banishment are
    //     written as multi-hot zone vectors)
    //   - facedown cards are featurized by count and age only — never
    //     their card_def_id contents
    // So perspective masking is purely a function of `perspective`.
    ::riftbound::PlayerId rb_player = (player == 0)
        ? ::riftbound::PlayerId::Player1
        : ::riftbound::PlayerId::Player2;
    const ::riftbound::GameState& gs =
        is_lazy_ ? lazy_state_ : engine_->state();
    auto feats = ::riftbound::ml::extractStateFeatures(
        gs, rb_player, rb_game_.cardDb());
    const size_t n = std::min(values.size(), feats.size());
    for (size_t i = 0; i < n; ++i) values[i] = feats[i];
    for (size_t i = n; i < values.size(); ++i) values[i] = 0.0f;
}

std::unique_ptr<::open_spiel::State> RiftboundState::Clone() const {
    // Lazy clone: snapshot the source's GameState + StepResult + GameResult
    // into the new state's lazy fields without spawning an engine thread.
    // Most MCTS-style clones (rollout heads, search-tree leaves the algorithm
    // never descends into) are never advanced — those pay only the deep
    // copy cost. The first DoApplyAction on the clone (or any call to
    // eventBus() / setOnDecisionCallback) triggers ensureLive(), which
    // builds the engine and replays action_history.
    //
    // Clone-of-clone is also lazy: if the source is itself a lazy clone,
    // we just copy the lazy_ fields directly — no engine ever spawns.
    std::unique_ptr<RiftboundState> out;
    if (is_lazy_) {
        out.reset(new RiftboundState(
            game_, engine_seed_,
            lazy_state_, lazy_step_, lazy_result_, SnapshotTag{}));
    } else {
        auto snapshot_state  = engine_->state();          // copy
        auto snapshot_step   = engine_->currentStep();    // by value already
        auto snapshot_result = engine_->stepResult();     // copy
        out.reset(new RiftboundState(
            game_, engine_seed_,
            std::move(snapshot_state),
            std::move(snapshot_step),
            std::move(snapshot_result),
            SnapshotTag{}));
    }
    out->identity_overrides_ = identity_overrides_;
    out->override_history_len_ = override_history_len_;
    out->world_seed_ = world_seed_;
    return out;
}

const ::riftbound::CardDB& RiftboundState::cardDb() const { return rb_game_.cardDb(); }

void RiftboundState::applyIdentityOverrides(::riftbound::GameState& gs) const {
    ::riftbound::search::applyIdentityOverrides(gs, rb_game_.cardDb(), identity_overrides_);
}

std::unique_ptr<::open_spiel::State> RiftboundState::ResampleFromInfostate(
    int player_id, std::function<double()> rng) const {
    using ::riftbound::GameObjectId;
    using ::riftbound::CardDefId;
    // The sampling itself lives in engine/determinize.cpp, shared with the
    // engine's own searcher (agents/ismcts_search).
    auto base = Clone();
    auto* out = static_cast<RiftboundState*>(base.get());
    if (out->IsTerminal() || !out->is_lazy_) return base;
    ::riftbound::GameState& gs = out->lazy_state_;
    const auto& db = rb_game_.cardDb();
    const auto observer = player_id == 0 ? ::riftbound::PlayerId::Player1
                                         : ::riftbound::PlayerId::Player2;

    const auto assign = ::riftbound::search::sampleHiddenIdentities(gs, observer, db, rng);

    // Compose with an earlier determinization of the same state, if any.
    std::map<GameObjectId, CardDefId> merged(out->identity_overrides_.begin(),
                                             out->identity_overrides_.end());
    for (const auto& [oid, def] : assign) merged[oid] = def;
    out->identity_overrides_.assign(merged.begin(), merged.end());
    out->override_history_len_ = static_cast<int>(gs.action_history.size());
    out->world_seed_ = 1 + static_cast<uint64_t>(rng() * 9.0e15);
    out->applyIdentityOverrides(gs);

    // The sampled world must look the same to the observer. If it does not,
    // something the observer can see was re-dealt: report the differing
    // observation features once and fall back to the unchanged state.
    if (out->ObservationString(player_id) != ObservationString(player_id)) {
        static std::atomic<int> reported{0};
        if (reported.fetch_add(1) < 3) {
            const auto a = ::riftbound::ml::extractStateFeatures(engineState(), observer, db);
            const auto b = ::riftbound::ml::extractStateFeatures(gs, observer, db);
            std::fprintf(stderr, "[determinize] observation changed at decision %d; features:",
                         static_cast<int>(gs.decision_index));
            for (size_t q = 0; q < std::min(a.size(), b.size()); ++q)
                if (a[q] != b[q]) std::fprintf(stderr, " %zu:%g->%g", q, a[q], b[q]);
            std::fprintf(stderr, "\n");
        }
        return Clone();
    }
    return base;
}

void RiftboundState::DoApplyAction(::open_spiel::Action action) {
    ensureLive();

    // Defensive no-op on terminal: there's a benign race where the
    // OpenSpiel-side IsTerminal() check returns false but the engine
    // finishes by the time DoApplyAction runs (a previous waitForDecision
    // returned with at_decision_=true, but the engine can subsequently
    // process more selectAction calls internally — e.g. empty-legal-action
    // sites — and reach markDone before we get here).
    if (engine_->isStepDone()) {
        return;
    }

    // Translate bit-packed action_id into the engine-side legal-list
    // index. The engine's StepDriver returns paused_legal_[index] back
    // to the engine; we find the first index whose Intent encodes to
    // the same id.
    const auto& legal = engine_->currentStep().legal;
    const auto& state = engine_->state();
    int legal_index = -1;
    for (size_t i = 0; i < legal.size(); ++i) {
        if (encodeAction(legal[i], state) == action) {
            legal_index = static_cast<int>(i);
            break;
        }
    }
    if (legal_index < 0) {
        // Defensive: unknown action id. Fall back to first legal so the
        // engine doesn't deadlock. This branch shouldn't fire for action
        // ids produced by LegalActions() on this state.
        legal_index = 0;
    }

    engine_->mutableState().action_history.push_back(static_cast<int64_t>(action));
    engine_->applyChoice(legal_index);
}

std::unique_ptr<RiftboundState> RiftboundState::makeFromSnapshot(
    std::shared_ptr<const ::open_spiel::Game> game,
    uint64_t engine_seed,
    const ::riftbound::GameState& snap_state,
    ::riftbound::StepResult        snap_step,
    ::riftbound::GameResult        snap_result) {
    // The single deep copy lives here — snap_state crosses ownership
    // boundaries into the lazy state. Downstream this is a move-only
    // chain into the engine's resumeFromSnapshot.
    return std::unique_ptr<RiftboundState>(new RiftboundState(
        std::move(game), engine_seed,
        ::riftbound::GameState(snap_state),
        std::move(snap_step), std::move(snap_result),
        SnapshotTag{}));
}

} // namespace riftbound::openspiel
