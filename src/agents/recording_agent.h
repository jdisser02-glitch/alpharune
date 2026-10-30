#pragma once
/// @file recording_agent.h
/// AgentInterface decorator that records the index of every choice an agent
/// makes, in the order the engine asks — the exact sequence a StepDriver
/// replays through `applyChoice`. GameRunner wraps both seats with one shared
/// history and hands it to the agents (AgentInterface::setDecisionHistory) so
/// a search agent can rebuild in-flight decisions by replaying the game.

#include "agents/agent_interface.h"

#include <cstdint>
#include <vector>

namespace riftbound {

class RecordingAgent : public AgentInterface {
public:
    RecordingAgent(AgentInterface& inner, std::vector<int32_t>& history)
        : inner_(inner), history_(history) {}

    Intent selectAction(const GameState& state, const std::vector<Intent>& legal) override {
        Intent chosen = inner_.selectAction(state, legal);
        int32_t idx = 0;   // an empty list becomes [concede] in the step driver: index 0
        for (size_t i = 0; i < legal.size(); ++i)
            if (legal[i] == chosen) { idx = static_cast<int32_t>(i); break; }
        history_.push_back(idx);
        return chosen;
    }
    std::vector<std::pair<int, double>> lastSearch() const override { return inner_.lastSearch(); }
    int lastSearchSims() const override { return inner_.lastSearchSims(); }
    void setDecisionHistory(const std::vector<int32_t>* h) override { inner_.setDecisionHistory(h); }

private:
    AgentInterface& inner_;
    std::vector<int32_t>& history_;
};

} // namespace riftbound
