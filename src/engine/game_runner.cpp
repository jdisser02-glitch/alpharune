#include "game_runner.h"
#include "agents/random_agent.h"
#include "agents/recording_agent.h"
#include "io/decision_log.h"
#include "ml/feature_extractor.h"
#include "ml/value_net.h"
#include "openspiel/action_vocab.h"
#include "io/replay_writer.h"
#include "io/state_renderer.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace riftbound {

GameRunner::GameRunner(const CardDB& card_db,
                       const CardRegistry& card_registry,
                       const DeckSubmission& deck1,
                       const DeckSubmission& deck2,
                       const GameConfig& config,
                       AggregateResults& results)
    : card_db_(card_db), card_registry_(card_registry),
      deck1_(deck1), deck2_(deck2), config_(config), results_(results) {}

GameResult GameRunner::run() {
    // Per-game resources (all stack/heap local — thread-safe)
    EventBus events;
    GameEngine engine(card_db_, events, card_registry_);
    StateRenderer renderer(card_db_);
    renderer.show_hand = config_.show_hand;

    // Compute seed. Truncated to 31 bits so any MCTS-style agent that
    // builds an OpenSpiel state internally (whose `seed` GameParameter
    // is int-typed) sees the same value the engine uses here. Without
    // the mask, a 64-bit nondeterministic seed would be silently
    // truncated only on the agent side and the cloned state's engine
    // would diverge from the real one. See src/main.cpp for full notes.
    uint64_t game_seed = (config_.base_seed == 0
        ? std::random_device{}()
        : config_.base_seed + config_.game_index) & 0x7FFFFFFFu;

    // Generate UUID
    std::string game_uuid;
    {
        std::mt19937_64 uuid_rng(std::random_device{}());
        std::uniform_int_distribution<uint64_t> dist;
        uint64_t a = dist(uuid_rng), b = dist(uuid_rng);
        std::ostringstream uu;
        uu << std::hex << std::setfill('0');
        uu << std::setw(8) << (a >> 32) << "-";
        uu << std::setw(4) << ((a >> 16) & 0xFFFF) << "-";
        uu << std::setw(4) << (0x4000 | ((a) & 0x0FFF)) << "-";
        uu << std::setw(4) << (0x8000 | ((b >> 48) & 0x3FFF)) << "-";
        uu << std::setw(12) << (b & 0xFFFFFFFFFFFFULL);
        game_uuid = uu.str();
    }

    // Set up debug/trace logging
    std::ostringstream log_buffer; // thread-local buffer
    if (config_.debug_mode) {
        // RIFTBOUND_LOG_STREAM=1 also writes every line to stderr as it
        // happens (tagged with the game index), so a game that never ends
        // can still be read; the buffered copy is printed at the end as usual.
        static const bool stream = std::getenv("RIFTBOUND_LOG_STREAM") != nullptr;
        events.on_log.connect([&log_buffer, trace = config_.trace_mode,
                               game = config_.game_index](const LogEvent& e) {
            if (e.level == LogLevel::Trace && !trace) return;
            const char* prefix = "[TRC]";
            if (e.level == LogLevel::Debug) prefix = "[DBG]";
            if (e.level == LogLevel::Info) prefix = "[INF]";
            if (e.level == LogLevel::Warning) prefix = "[WRN]";
            log_buffer << "  " << prefix << " " << e.message << "\n";
            if (stream) {
                std::string line = "g" + std::to_string(game) + " " + prefix + " " + e.message + "\n";
                std::fputs(line.c_str(), stderr);
            }
        });
    }

    // Set up HTML replay writer
    std::unique_ptr<ReplayWriter> replay;
    if (config_.do_render) {
        std::string replay_path = "replay_game" + std::to_string(config_.game_index + 1) + ".html";
        replay = std::make_unique<ReplayWriter>(card_db_, replay_path);
        replay->setGameHeader("Game " + std::to_string(config_.game_index + 1) +
            " | UUID: " + game_uuid +
            " | Seed: " + std::to_string(game_seed));

        events.on_log.connect([&replay](const LogEvent& e) {
            if (!replay) return;
            const char* prefix = "[TRC]";
            if (e.level == LogLevel::Debug) prefix = "[DBG]";
            if (e.level == LogLevel::Info) prefix = "[INF]";
            if (e.level == LogLevel::Warning) prefix = "[WRN]";
            replay->addTraceLine(std::string(prefix) + " " + e.message);
        });
        // Inject phase-seam snapshots at every PhaseChangedEvent so the
        // replay has discrete visual stops at each phase boundary.
        events.on_phase_changed.connect(
            [&replay, &renderer, &engine](const PhaseChangedEvent& e) {
                if (!replay) return;
                replay->recordPhaseSeam(engine.state(),
                                         e.old_phase, e.new_phase, renderer);
            });
    }

    // Agent factory. If config_.agent_factory is supplied, defer to it —
    // that's how the riftbound binary plugs in MCTS / IsMCTS (their
    // constructors live in the executable's TU because they pull in
    // OpenSpiel, which riftbound_core itself does not link). Otherwise
    // fall back to the built-in random-only factory so older callers
    // (tests, batch_runner with default config) keep working.
    auto makeAgent = [&](int seat_idx, const std::string& spec)
        -> std::unique_ptr<AgentInterface> {
        if (config_.agent_factory) {
            // Factory receives game_seed verbatim — it picks how to derive
            // any per-seat sub-seeds for the constructed agent.
            return config_.agent_factory(seat_idx, game_seed);
        }
        // Fallback: per-seat derived seed so the two RandomAgents don't
        // draw identical action sequences.
        uint64_t derived = game_seed * 2 + static_cast<uint64_t>(seat_idx);
        if (spec == "random") {
            return std::make_unique<RandomAgent>(derived);
        }
        throw std::runtime_error(
            "GameRunner: agent spec '" + spec +
            "' requires an agent_factory in GameConfig (riftbound_core only "
            "ships RandomAgent; mcts/ismcts/human live in the binary's TU).");
    };

    auto agent1 = makeAgent(0, config_.agent1_spec);
    auto agent2 = makeAgent(1, config_.agent2_spec);

    // Every decision's chosen legal index in engine order, for agents that
    // rebuild in-flight decisions by replay (AgentInterface::setDecisionHistory).
    std::vector<int32_t> choice_history;
    choice_history.reserve(512);
    agent1->setDecisionHistory(&choice_history);
    agent2->setDecisionHistory(&choice_history);
    RecordingAgent seat1(*agent1, choice_history), seat2(*agent2, choice_history);

    // Game review record (JSON) for the first json_replay_games games.
    std::unique_ptr<DecisionLog> review;
    std::vector<std::string> review_lines;
    if (!config_.json_replay_dir.empty() && config_.game_index < config_.json_replay_games) {
        review = std::make_unique<DecisionLog>();
        events.on_log.connect([&review_lines](const LogEvent& e) {
            if (e.level == LogLevel::Debug || e.level == LogLevel::Info ||
                (e.level == LogLevel::Trace && review_lines.size() < 200))
                review_lines.push_back(e.message);
        });
    }

    // Self-play value records: every non-trivial decision of both players
    // (or a reservoir sample of `value_samples_per_game` of them).
    std::vector<ml::ValueSample> value_samples;
    std::mt19937_64 value_rng(game_seed * 7919 + 17);
    int value_seen = 0;
    const bool want_values = !config_.value_data_path.empty();

    // Decision callback feeds the HTML replay writer when --render is on.
    engine.on_decision = [&](const GameState& state,
                              const std::vector<Intent>& actions,
                              const Intent& chosen) {
        if (replay) {
            replay->recordDecision(state, actions, chosen, renderer);
        }
        if (want_values && actions.size() > 1 && chosen.player != PlayerId::None &&
            !state.cost_cursor.has_value()) {
            ++value_seen;
            const int k = config_.value_samples_per_game;
            int slot = static_cast<int>(value_samples.size());
            if (k > 0 && slot >= k) {
                slot = static_cast<int>(value_rng() % static_cast<uint64_t>(value_seen));
                if (slot >= k) slot = -1;
            }
            if (slot >= 0) {
                ml::ValueSample sample;
                sample.who = chosen.player;
                sample.turn = state.turn.turn_number;
                sample.decision_index = state.decision_index;
                sample.features = ml::sparsify(ml::extractStateFeatures(state, chosen.player, card_db_));
                // Policy target (RBV3): the legal options' vocab slots with the
                // acting agent's visit shares; cheap (one encodeAction per
                // option) and empty for agents that do not search.
                const AgentInterface& actor = chosen.player == PlayerId::Player2 ? *agent2 : *agent1;
                const auto shares = actor.lastSearch();
                const std::vector<int> slots = ::riftbound::openspiel::encodeActions(actions, state);
                ml::setPolicyTarget(sample, slots, shares, actor.lastSearchSims());
                // Clean boundary: nothing in flight (no chain, showdown or combat).
                bool clean = !state.chain.exists() && state.turn.oc_state == OpenClosedState::Open &&
                             state.turn.ns_state == NeutralShowdownState::Neutral &&
                             state.turn.phase != TurnPhase::Mulligan && state.turn.phase != TurnPhase::Setup;
                for (const auto& bf : state.battlefields)
                    if (bf.combat_in_progress || bf.showdown_in_progress || bf.combat_staged || bf.showdown_staged) clean = false;
                if (clean) sample.flags |= ml::kPolicyFlagCleanBoundary;
                if (slot == static_cast<int>(value_samples.size())) value_samples.push_back(std::move(sample));
                else value_samples[static_cast<size_t>(slot)] = std::move(sample);
            }
        }
        if (review) {
            const AgentInterface& who = chosen.player == PlayerId::Player2 ? *agent2 : *agent1;
            review->record(state, actions, chosen, who.lastSearch(), std::move(review_lines));
            review_lines.clear();
        }
    };

    auto result = engine.runGame(deck1_, deck2_, seat1, seat2, game_seed);

    if (want_values && result.winner != PlayerId::None) {
        // Game id: the seed and index identify the game within a batch and
        // let the trainer hold out whole games.
        const uint32_t game_id = static_cast<uint32_t>((game_seed * 0x9E3779B1ull) ^
                                                       static_cast<uint64_t>(config_.game_index) * 0x85EBCA77ull);
        ml::appendValueRecords(config_.value_data_path, ml::kStateFeatureDim, value_samples,
                               result.winner, result.final_scores[0], result.final_scores[1],
                               engine.state().mode.victory_score, game_id);
    }

    if (!config_.json_replay_dir.empty()) {
        nlohmann::json header = {
            {"game", config_.game_index + 1}, {"seed", game_seed}, {"uuid", game_uuid},
            {"deck1", config_.deck1_label}, {"deck2", config_.deck2_label},
            {"agent1", config_.agent1_spec}, {"agent2", config_.agent2_spec},
            {"winner", result.winner == PlayerId::Player1 ? 1 : result.winner == PlayerId::Player2 ? 2 : 0},
            {"scores", {result.final_scores[0], result.final_scores[1]}},
            {"turns", result.total_turns}, {"decisions", result.total_decisions},
            {"reason", result.termination_reason}};
        std::filesystem::create_directories(config_.json_replay_dir);
        if (review)
            review->finish(header, config_.json_replay_dir + "/game_" +
                                   std::to_string(config_.game_index + 1) + ".json");
        std::lock_guard<std::mutex> lock(results_.console_mutex);
        std::ofstream(config_.json_replay_dir + "/games.ndjson", std::ios::app)
            << header.dump() << "\n";
    }

    // Finalize outputs
    if (replay) {
        replay->addTraceLine("[TRC] GAME_OVER: " + result.termination_reason);
        replay->writeHtml();
    }

    // Update aggregate results (atomic)
    if (result.winner == PlayerId::Player1)
        results_.p1_wins.fetch_add(1);
    else if (result.winner == PlayerId::Player2)
        results_.p2_wins.fetch_add(1);
    else
        results_.draws.fetch_add(1);
    results_.total_turns.fetch_add(result.total_turns);
    results_.total_decisions.fetch_add(result.total_decisions);
    int completed = results_.games_completed.fetch_add(1) + 1;

    // Console output (under mutex)
    {
        std::lock_guard<std::mutex> lock(results_.console_mutex);

        // Per-game result (only for small batches)
        if (config_.total_games <= 20) {
            std::cout << "Game " << (config_.game_index + 1)
                      << " [seed=" << game_seed << "]: "
                      << toString(result.winner) << " wins"
                      << " (" << result.final_scores[0] << "-"
                      << result.final_scores[1] << ")"
                      << " in " << result.total_turns << " turns"
                      << " (" << result.total_decisions << " decisions)"
                      << " — " << result.termination_reason << "\n";
        }

        // Progress for batches > 20 games. Tick on whichever fires first:
        //   - every 100 games (good signal at very high gps)
        //   - at least once per second of wall time
        //   - at completion
        if (config_.total_games > 20) {
            auto now = std::chrono::steady_clock::now();
            double since_last = std::chrono::duration<double>(
                now - results_.last_progress_time).count();
            bool fire = (completed % 100 == 0)
                     || (completed == results_.total_games)
                     || (since_last >= 1.0);
            if (fire) {
                results_.last_progress_time = now;
                double elapsed = std::chrono::duration<double>(
                    now - results_.start_time).count();
                double gps = elapsed > 0 ? completed / elapsed : 0;
                int eta_sec = gps > 0
                    ? static_cast<int>((results_.total_games - completed) / gps)
                    : 0;
                std::cout << "\r  Progress: " << completed << "/"
                          << results_.total_games << " games"
                          << "  (" << std::fixed << std::setprecision(1)
                          << gps << " games/sec"
                          << ", ETA " << eta_sec / 60 << "m"
                          << eta_sec % 60 << "s)"
                          << "    " << std::flush;
            }
        }
    }

    return result;
}

} // namespace riftbound
