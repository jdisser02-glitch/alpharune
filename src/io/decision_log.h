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
std::string describeIntent(const Intent& intent, const GameState& state);

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

private:
    nlohmann::json frames_ = nlohmann::json::array();
};

}  // namespace riftbound
