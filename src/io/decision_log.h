#pragma once
/// @file decision_log.h
/// Machine-readable game record for review: one frame per decision with the
/// board, the options offered, the option taken and (for search agents) how
/// much of the search went to each option. Written as JSON by GameRunner when
/// GameConfig::json_replay_dir is set; read by the lab dashboard.

#include "core/card_db.h"
#include "core/game_state.h"
#include "core/intent.h"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace riftbound {

/// Human-readable text for an option, with card names resolved from `state`.
/// One line of text for an intent ("Play X to Y", "Bring battlefield Z"). `db`
/// names cards that are not objects yet, such as the battlefields offered at
/// setup (Intent::chosen_battlefield is then an index into
/// PlayerState::battlefield_pool); without it those read "battlefield #N".
std::string describeIntent(const Intent& intent, const GameState& state, const CardDB* db = nullptr);

/// Board summary: scores, resources, hands (both, marked by owner), units and
/// gear per location, battlefields and their controllers, the chain.
nlohmann::json boardJson(const GameState& state);

class DecisionLog {
public:
    /// `search` = (option index, share of the search) for the chosen
    /// decision, empty when the agent does not search.
    void record(const GameState& state, const std::vector<Intent>& options,
                const Intent& chosen, const std::vector<std::pair<int, double>>& search,
                std::vector<std::string> log_lines);
    void finish(const nlohmann::json& header, const std::string& path) const;
    /// A last frame after the final decision: the end state (the points of
    /// the last scoring step, the winner) with the log since that decision,
    /// so a replay can be stepped to the end of the game.
    void finishGame(const GameState& state, std::vector<std::string> log_lines);

    /// Merge extra fields into the frame recorded last (the guided replay of
    /// a human game adds the human's and the bot's choice). No-op before the
    /// first record.
    void annotateLast(const nlohmann::json& extra);
    size_t size() const { return frames_.size(); }

private:
    nlohmann::json frames_ = nlohmann::json::array();
};

}  // namespace riftbound
