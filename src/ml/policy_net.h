#pragma once
/// @file policy_net.h
/// Two-head network over the masked state features (ml::extractStateFeatures):
/// a shared ReLU hidden layer feeding a win-probability head, a score-margin
/// head and a policy head over the dense action vocabulary
/// (openspiel/action_vocab.h). Trained by lab/learn/train_policy.py from
/// RBV3 records; used by the pmcts searcher as `eval=policy:<file>` (value
/// head as the leaf evaluation, policy head as the PUCT prior).
///
/// File format "RBPN" (little-endian): magic, int32 version (1), int32 in_dim,
/// int32 hidden, int32 vocab, then float32 arrays mean[in_dim],
/// inv_std[in_dim], w1[hidden * in_dim] (one row per hidden unit), b1[hidden],
/// w2[hidden], b2[1] (win logit), wm[hidden], bm[1] (margin, tanh),
/// wp[vocab * hidden] (one row per vocab slot), bp[vocab] (policy logits).
///
/// Inference is sparse on both ends: the hidden layer accumulates only the
/// non-zero inputs (standardisation of the zeros folded into the bias, as
/// ValueNet does) and the policy head computes logits only for the legal
/// slots, soft-maxed over them.

#include "core/card_db.h"
#include "core/game_state.h"
#include "core/intent.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace riftbound::ml {

class PolicyNet {
public:
    /// Loads a network file; throws std::runtime_error on a bad file.
    explicit PolicyNet(const std::string& path);
    /// Same format from a memory buffer (`label` names it in errors).
    PolicyNet(const unsigned char* data, size_t len, const std::string& label = "<memory>");

    struct Output {
        double win = 0.5;              ///< P(perspective player wins)
        double margin = 0.0;           ///< predicted (own - opp) / victory, in [-1, 1]
        std::vector<double> policy;    ///< one probability per requested slot (softmax over them)
    };

    /// Hidden activations from sparse features.
    std::vector<float> hidden(const std::vector<std::pair<uint16_t, float>>& features) const;
    std::vector<float> hidden(const std::vector<float>& features) const;

    double winFromHidden(const std::vector<float>& hid) const;
    double marginFromHidden(const std::vector<float>& hid) const;
    /// Softmax over `slots` of their policy logits (duplicates allowed; each
    /// occurrence gets the full slot probability). Slots out of range get
    /// probability 0 (logit -inf).
    std::vector<double> policyFromHidden(const std::vector<float>& hid, const std::vector<int>& slots) const;
    /// Raw policy logits of `slots` (NaN for a slot out of range).
    std::vector<double> logitsFromHidden(const std::vector<float>& hid, const std::vector<int>& slots) const;

    /// All heads at once from sparse features and the legal slots.
    Output evaluate(const std::vector<std::pair<uint16_t, float>>& features, const std::vector<int>& slots) const;

    /// Win probability for `perspective` from its own (masked) view.
    double winProbability(const GameState& state, PlayerId perspective, const CardDB& db) const;

    /// Prior over `legal` for the search: the policy head's probability of
    /// each option's vocab slot from the deciding player's view; options
    /// sharing a slot split its probability evenly (the search pools them
    /// again). `temperature` > 1 flattens, < 1 sharpens the logits.
    std::vector<double> prior(const GameState& state, const std::vector<Intent>& legal,
                              PlayerId perspective, const CardDB& db, double temperature = 1.0) const;

    int inputDim() const { return in_dim_; }
    int hiddenDim() const { return hidden_; }
    int vocabSize() const { return vocab_; }

    /// Process-wide cache: one immutable network per path (thread-safe).
    static std::shared_ptr<const PolicyNet> shared(const std::string& path);

private:
    void load(std::istream& in, const std::string& label);

    int in_dim_ = 0, hidden_ = 0, vocab_ = 0;
    std::vector<float> mean_, inv_std_, w1_, b1_, w2_, wm_, wp_, bp_;
    float b2_ = 0, bm_ = 0;
    std::vector<float> w1t_, b1_eff_;   // sparse first layer (see ValueNet)
};

}  // namespace riftbound::ml
