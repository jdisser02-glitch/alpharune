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
///
/// Inference is sparse: the state vector is ~1.3% non-zero (≈86 of 6,688
/// features), so the standardisation of the zeros is folded into the
/// first-layer bias at load time and the hidden pre-activations accumulate
/// only the non-zero inputs (≈5k MACs instead of ≈430k).

#include "core/card_db.h"
#include "core/game_state.h"

#include <cstddef>
#include <cstdint>
#include <utility>
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
    /// Same, from already-extracted features (sparse accumulation).
    double winProbability(const std::vector<float>& features) const;
    /// Same, from the non-zero features only.
    double winProbabilitySparse(const std::vector<std::pair<uint16_t, float>>& features) const;
    /// Reference dense pass (tests and benchmarks).
    double winProbabilityDense(const std::vector<float>& features) const;

    int inputDim() const { return in_dim_; }

    /// Process-wide cache: one immutable network per path (thread-safe).
    static std::shared_ptr<const ValueNet> shared(const std::string& path);

private:
    void load(std::istream& in, const std::string& label);

    int in_dim_ = 0, hidden_ = 0;
    std::vector<float> mean_, inv_std_, w1_, b1_, w2_;
    float b2_ = 0;
    // Sparse inference: w1 transposed (one contiguous row of `hidden_`
    // weights per input) and b1 with the zero-input standardisation folded in.
    std::vector<float> w1t_, b1_eff_;
};

/// Where a record's policy target comes from (RBV3 `source` byte).
enum PolicySource : uint8_t {
    kPolicyNone = 0,     ///< no policy block (the agent did not search)
    kPolicySearch = 1,   ///< visit shares of a search agent (pmcts / ismcts)
    kPolicyHuman = 2,    ///< a human's recorded move (one-hot over the legal slots)
};

/// RBV3 record flags.
enum PolicyFlags : uint8_t {
    kPolicyFlagCleanBoundary = 1,   ///< decision at a clean boundary (no chain / showdown in flight)
    kPolicyFlagFullSims = 2,        ///< searched with the full simulation budget
};

/// One self-play training record: the perspective player's masked state
/// features at a decision, kept sparse (index, value) until written, plus an
/// optional policy block: the vocab slot of every legal option (merged when
/// options alias to one slot) with the target share of each.
struct ValueSample {
    PlayerId who = PlayerId::None;
    int turn = 0;
    int decision_index = 0;
    std::vector<std::pair<uint16_t, float>> features;
    uint8_t source = kPolicyNone;
    uint8_t flags = 0;
    uint16_t sims = 0;                                   ///< simulations behind a search target
    std::vector<std::pair<uint16_t, float>> policy;      ///< (vocab slot, target), all legal slots
};

/// Non-zero (index, value) pairs of a feature vector (indices < 65536).
std::vector<std::pair<uint16_t, float>> sparsify(const std::vector<float>& x);

/// Policy block of a sample from the legal options' vocab slots and a search's
/// visit shares (index into `slots`, share; empty = no search). Aliased slots
/// are merged (shares summed); every legal slot is kept, with 0 when
/// unvisited, so the trainer knows the legal mask. Sets source/sims/flags.
void setPolicyTarget(ValueSample& sample, const std::vector<int>& slots,
                     const std::vector<std::pair<int, double>>& shares, int sims);
/// Human one-hot: the slot of option `chosen` gets 1.
void setHumanTarget(ValueSample& sample, const std::vector<int>& slots, int chosen);

/// Appends self-play training records. A fresh file is written as "RBV3"
/// (version 3): header "RBV3", int32 dim, int32 vocab size. Per record the
/// RBV2 fields — int8 label (1 the perspective player won, 0 lost, 2 draw),
/// int8 perspective (1 = P1, 2 = P2), int8 own final score, int8 opponent
/// final score, int8 victory score, uint32 game id, int32 turn, int32 decision
/// index, uint16 nnz, nnz x (uint16 index, float32 value) — followed by the
/// policy block: uint8 source (PolicySource), uint8 flags, uint16 sims, uint16
/// n_legal, n_legal x (uint16 vocab slot, float32 target). Appending to an
/// existing "RBV2" file keeps that format (the policy block is dropped).
/// lab/learn/train_value.py and train_policy.py read RBVD / RBV2 / RBV3.
void appendValueRecords(const std::string& path, int dim, const std::vector<ValueSample>& samples,
                        PlayerId winner, int score_p1, int score_p2, int victory_score,
                        uint32_t game_id);

/// One record as read back from a data file.
struct ValueRecord {
    int8_t label = 0, perspective = 0, own_score = 0, opp_score = 0, victory_score = 0;
    uint32_t game_id = 0;
    int32_t turn = 0, decision_index = 0;
    std::vector<std::pair<uint16_t, float>> features;
    uint8_t source = kPolicyNone, flags = 0;
    uint16_t sims = 0;
    std::vector<std::pair<uint16_t, float>> policy;
};

struct ValueFile {
    int version = 0;     ///< 1 (RBVD), 2 (RBV2) or 3 (RBV3)
    int dim = 0;
    int vocab = 0;       ///< RBV3 only
    std::vector<ValueRecord> records;
};

/// Reads a RBVD / RBV2 / RBV3 file (tests, tools). Throws on a bad file.
ValueFile readValueRecords(const std::string& path);

}  // namespace riftbound::ml
