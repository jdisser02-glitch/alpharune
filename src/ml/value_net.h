#pragma once
/// @file value_net.h
/// Learned position evaluator: a one-hidden-layer network over the masked
/// state features (ml::extractStateFeatures) that predicts the probability the
/// perspective player wins. Trained by lab/learn/train_value.py from
/// self-play records (GameConfig::value_data_path); used by the search agents
/// as `eval=net:<file>`.
///
/// File format (little-endian): "RBVN" magic, int32 version (1), int32
/// in_dim, int32 hidden, then float32 arrays mean[in_dim], inv_std[in_dim],
/// w1[hidden * in_dim] (row-major, one row per hidden unit), b1[hidden],
/// w2[hidden], b2[1].

#include "core/card_db.h"
#include "core/game_state.h"

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace riftbound::ml {

class ValueNet {
public:
    /// Loads a network file; throws std::runtime_error on a bad file.
    explicit ValueNet(const std::string& path);
    /// Same format from a memory buffer (`label` names it in errors).
    ValueNet(const unsigned char* data, size_t len, const std::string& label = "<memory>");

    /// Win probability for `perspective` from its own (masked) view.
    double winProbability(const GameState& state, PlayerId perspective, const CardDB& db) const;
    /// Same, from already-extracted features.
    double winProbability(const std::vector<float>& features) const;

    int inputDim() const { return in_dim_; }

    /// Process-wide cache: one immutable network per path (thread-safe).
    static std::shared_ptr<const ValueNet> shared(const std::string& path);

private:
    void load(std::istream& in, const std::string& label);

    int in_dim_ = 0, hidden_ = 0;
    std::vector<float> mean_, inv_std_, w1_, b1_, w2_;
    float b2_ = 0;
};

/// Appends self-play training records: for each sampled decision, the
/// perspective player's masked features (sparse) and whether that player
/// won. Format per record: int8 label (1 win, 0 loss), int32 turn,
/// uint16 nnz, then nnz x (uint16 index, float32 value). File header:
/// "RBVD", int32 dim.
void appendValueRecords(const std::string& path, int dim,
                        const std::vector<std::pair<PlayerId, std::vector<float>>>& samples,
                        const std::vector<int>& turns, PlayerId winner);

}  // namespace riftbound::ml
