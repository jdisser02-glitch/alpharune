#pragma once
/// @file greedy_bot.h
/// One-ply search bot for the WebAssembly build (no OpenSpiel).
///
/// For every legal option at the current decision the bot clones the game
/// (GameState copy + a fresh GameEngine resumed from that snapshot, or —
/// when the snapshot is mid-chain / mid-combat / mid-cost-payment, where
/// resumeFromSnapshot is not safe — a replay of the decision history from
/// beginGame), applies the option, runs forward with cheap default answers
/// until the board is quiet again (no chain, no combat, no cost cursor) and
/// scores the position from the bot's point of view: the value net when one
/// is loaded, else score difference plus on-board might.
///
/// Hidden information: before cloning, the state is determinized — the
/// opponent's hand and Main Deck are shuffled together and re-dealt, and the
/// unknown deck orders are shuffled — so the bot never plans against the
/// opponent's actual hand. Scores are averaged over a few determinizations.

#include "cards/card_registry.h"
#include "core/card_db.h"
#include "engine/game_engine.h"
#include "ml/value_net.h"
#include "rules/deck_validator.h"

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace riftbound::wasm {

struct GreedyBotConfig {
    int determinizations = 3;   // worlds sampled per decision
    int rollout_steps = 40;     // max default answers while settling a clone
    double budget_ms = 1500;    // stop sampling further worlds past this
    int wide_cutoff = 24;       // more options than this: one world only
    bool random = false;        // "random" spec: uniform choice, no search

    /// Parse "greedy", "greedy:det=3,budget=1500,steps=40", "random".
    /// Unknown keys are ignored; throws on a malformed number.
    static GreedyBotConfig parse(const std::string& spec);
};

class GreedyBot {
public:
    GreedyBot(const CardDB& db, const CardRegistry& registry,
              const DeckSubmission& deck1, const DeckSubmission& deck2,
              uint64_t game_seed, GreedyBotConfig config, uint64_t bot_seed);

    void setValueNet(std::shared_ptr<const ml::ValueNet> net) { net_ = std::move(net); }

    /// Pick a legal index for `bot` at the current decision of `engine`.
    /// `history` = the legal indices applied to `engine` so far (used by the
    /// replay fallback). Never throws for a well-formed decision; an
    /// internal failure falls back to option 0.
    int choose(const GameEngine& engine, PlayerId bot, const std::vector<int>& history);

    /// Mean value per option from the last search: (legal index, score in
    /// [0,1] from the bot's view). Empty when the last choice did not search.
    const std::vector<std::pair<int, double>>& lastSearch() const { return last_search_; }
    double lastMs() const { return last_ms_; }
    int lastWorlds() const { return last_worlds_; }
    /// True when the last choice went through the replay fallback.
    bool lastReplayed() const { return last_replayed_; }

private:
    using Overrides = std::vector<std::pair<GameObjectId, CardDefId>>;

    double evaluate(const GameState& state, PlayerId bot) const;
    Overrides determinize(const GameState& state, PlayerId bot);
    void applyOverrides(GameState& state, const Overrides& ov) const;
    static bool cleanBoundary(const GameState& state);
    static bool inFlight(const GameState& state);

    const CardDB& db_;
    const CardRegistry& registry_;
    const DeckSubmission& deck1_;
    const DeckSubmission& deck2_;
    uint64_t game_seed_;
    GreedyBotConfig config_;
    std::mt19937_64 rng_;
    std::shared_ptr<const ml::ValueNet> net_;

    std::vector<std::pair<int, double>> last_search_;
    double last_ms_ = 0;
    int last_worlds_ = 0;
    bool last_replayed_ = false;
};

}  // namespace riftbound::wasm
