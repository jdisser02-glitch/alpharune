#include "ml/value_net.h"
#include "ml/feature_extractor.h"
#include "openspiel/action_vocab.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <istream>
#include <streambuf>
#include <map>
#include <mutex>
#include <iterator>
#include <stdexcept>

namespace riftbound::ml {

namespace {
template <typename T>
void readInto(std::istream& f, T* out, size_t n, const std::string& path) {
    f.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n * sizeof(T)));
    if (!f) throw std::runtime_error("value net: truncated file " + path);
}

/// Read-only streambuf over a caller-owned byte range.
struct MemoryBuf : std::streambuf {
    MemoryBuf(const unsigned char* data, size_t len) {
        char* p = const_cast<char*>(reinterpret_cast<const char*>(data));
        setg(p, p, p + len);
    }
};
}  // namespace

ValueNet::ValueNet(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("value net: cannot open " + path);
    load(f, path);
}

ValueNet::ValueNet(const unsigned char* data, size_t len, const std::string& label) {
    MemoryBuf buf(data, len);
    std::istream f(&buf);
    load(f, label);
}

void ValueNet::load(std::istream& f, const std::string& path) {
    char magic[4];
    readInto(f, magic, 4, path);
    if (std::memcmp(magic, "RBVN", 4) != 0) throw std::runtime_error("value net: bad magic in " + path);
    int32_t hdr[3];
    readInto(f, hdr, 3, path);
    if (hdr[0] != 1) throw std::runtime_error("value net: unsupported version in " + path);
    in_dim_ = hdr[1];
    hidden_ = hdr[2];
    if (in_dim_ <= 0 || hidden_ <= 0 || in_dim_ > (1 << 20) || hidden_ > (1 << 16))
        throw std::runtime_error("value net: bad dimensions in " + path);
    mean_.resize(in_dim_);
    inv_std_.resize(in_dim_);
    w1_.resize(static_cast<size_t>(hidden_) * in_dim_);
    b1_.resize(hidden_);
    w2_.resize(hidden_);
    readInto(f, mean_.data(), mean_.size(), path);
    readInto(f, inv_std_.data(), inv_std_.size(), path);
    readInto(f, w1_.data(), w1_.size(), path);
    readInto(f, b1_.data(), b1_.size(), path);
    readInto(f, w2_.data(), w2_.size(), path);
    readInto(f, &b2_, 1, path);

    // Fold the standardisation of zero inputs into the bias:
    //   a_h = b1_h + sum_i w1[h][i] * (x_i - mean_i) * inv_std_i
    //       = (b1_h - sum_i w1[h][i] * mean_i * inv_std_i) + sum_{x_i != 0} w1[h][i] * inv_std_i * x_i
    // and keep w1 * inv_std transposed so each non-zero input touches one
    // contiguous row of `hidden_` weights.
    w1t_.assign(static_cast<size_t>(in_dim_) * hidden_, 0.0f);
    b1_eff_.assign(hidden_, 0.0f);
    for (int h = 0; h < hidden_; ++h) {
        const float* row = &w1_[static_cast<size_t>(h) * in_dim_];
        double fold = b1_[h];
        for (int i = 0; i < in_dim_; ++i) {
            fold -= static_cast<double>(row[i]) * mean_[i] * inv_std_[i];
            w1t_[static_cast<size_t>(i) * hidden_ + h] = row[i] * inv_std_[i];
        }
        b1_eff_[h] = static_cast<float>(fold);
    }
}

double ValueNet::winProbabilityDense(const std::vector<float>& x) const {
    std::vector<float> z(in_dim_);
    const size_t n = std::min<size_t>(x.size(), static_cast<size_t>(in_dim_));
    for (size_t i = 0; i < static_cast<size_t>(in_dim_); ++i)
        z[i] = ((i < n ? x[i] : 0.0f) - mean_[i]) * inv_std_[i];
    double out = b2_;
    for (int h = 0; h < hidden_; ++h) {
        const float* row = &w1_[static_cast<size_t>(h) * in_dim_];
        double a = b1_[h];
        for (int i = 0; i < in_dim_; ++i) a += row[i] * z[i];
        if (a > 0) out += w2_[h] * a;
    }
    return 1.0 / (1.0 + std::exp(-out));
}

double ValueNet::winProbability(const std::vector<float>& x) const {
    float acc[1024];
    std::vector<float> heap;
    float* a = acc;
    if (hidden_ > 1024) { heap.resize(hidden_); a = heap.data(); }
    std::memcpy(a, b1_eff_.data(), sizeof(float) * hidden_);
    const size_t n = std::min<size_t>(x.size(), static_cast<size_t>(in_dim_));
    for (size_t i = 0; i < n; ++i) {
        const float v = x[i];
        if (v == 0.0f) continue;
        const float* col = &w1t_[i * hidden_];
        for (int h = 0; h < hidden_; ++h) a[h] += col[h] * v;
    }
    double out = b2_;
    for (int h = 0; h < hidden_; ++h) if (a[h] > 0) out += w2_[h] * a[h];
    return 1.0 / (1.0 + std::exp(-out));
}

double ValueNet::winProbabilitySparse(const std::vector<std::pair<uint16_t, float>>& x) const {
    float acc[1024];
    std::vector<float> heap;
    float* a = acc;
    if (hidden_ > 1024) { heap.resize(hidden_); a = heap.data(); }
    std::memcpy(a, b1_eff_.data(), sizeof(float) * hidden_);
    for (const auto& [i, v] : x) {
        if (i >= in_dim_ || v == 0.0f) continue;
        const float* col = &w1t_[static_cast<size_t>(i) * hidden_];
        for (int h = 0; h < hidden_; ++h) a[h] += col[h] * v;
    }
    double out = b2_;
    for (int h = 0; h < hidden_; ++h) if (a[h] > 0) out += w2_[h] * a[h];
    return 1.0 / (1.0 + std::exp(-out));
}

double ValueNet::winProbability(const GameState& state, PlayerId perspective,
                                const CardDB& db) const {
    return winProbability(extractStateFeatures(state, perspective, db));
}

std::shared_ptr<const ValueNet> ValueNet::shared(const std::string& path) {
    static std::mutex mu;
    static std::map<std::string, std::shared_ptr<const ValueNet>> cache;
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    auto net = std::make_shared<const ValueNet>(path);
    cache[path] = net;
    return net;
}

std::vector<std::pair<uint16_t, float>> sparsify(const std::vector<float>& x) {
    std::vector<std::pair<uint16_t, float>> nz;
    for (size_t i = 0; i < x.size() && i < 65535; ++i)
        if (x[i] != 0.0f) nz.emplace_back(static_cast<uint16_t>(i), x[i]);
    return nz;
}

namespace {
/// Distinct legal slots in legal order, each with target 0.
std::vector<std::pair<uint16_t, float>> legalSlots(const std::vector<int>& slots) {
    std::vector<std::pair<uint16_t, float>> out;
    for (int slot : slots) {
        if (slot < 0 || slot > 65535) continue;
        bool seen = false;
        for (const auto& [s, v] : out) if (s == slot) { seen = true; break; }
        if (!seen) out.emplace_back(static_cast<uint16_t>(slot), 0.0f);
    }
    return out;
}
}  // namespace

void setPolicyTarget(ValueSample& sample, const std::vector<int>& slots,
                     const std::vector<std::pair<int, double>>& shares, int sims) {
    sample.policy = legalSlots(slots);
    double total = 0.0;
    for (const auto& [idx, share] : shares) {
        if (idx < 0 || idx >= static_cast<int>(slots.size()) || share <= 0) continue;
        for (auto& [s, v] : sample.policy)
            if (s == slots[static_cast<size_t>(idx)]) { v += static_cast<float>(share); total += share; break; }
    }
    if (total > 0) for (auto& [s, v] : sample.policy) v = static_cast<float>(v / total);
    sample.source = total > 0 ? kPolicySearch : kPolicyNone;
    sample.sims = static_cast<uint16_t>(std::max(0, std::min(65535, sims)));
    if (total <= 0) sample.policy.clear();
}

void setHumanTarget(ValueSample& sample, const std::vector<int>& slots, int chosen) {
    sample.policy = legalSlots(slots);
    sample.source = kPolicyNone;
    sample.sims = 0;
    if (chosen >= 0 && chosen < static_cast<int>(slots.size()))
        for (auto& [s, v] : sample.policy)
            if (s == slots[static_cast<size_t>(chosen)]) { v = 1.0f; sample.source = kPolicyHuman; break; }
    if (sample.source != kPolicyHuman) sample.policy.clear();
}

void appendValueRecords(const std::string& path, int dim, const std::vector<ValueSample>& samples,
                        PlayerId winner, int score_p1, int score_p2, int victory_score,
                        uint32_t game_id) {
    static std::mutex mu;
    std::lock_guard<std::mutex> lock(mu);
    int version = 3;
    {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {0, 0, 0, 0};
        if (probe && probe.read(magic, 4)) {
            if (std::memcmp(magic, "RBV2", 4) == 0) version = 2;
            else if (std::memcmp(magic, "RBVD", 4) == 0)
                throw std::runtime_error("value data: cannot append to a v1 file " + path);
            else if (std::memcmp(magic, "RBV3", 4) != 0)
                throw std::runtime_error("value data: " + path + " is not a value-data file");
        } else {
            version = -3;   // fresh file: write the header
        }
    }
    std::ofstream f(path, std::ios::binary | std::ios::app);
    auto put = [&](const void* p, size_t n) { f.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n)); };
    if (version == -3) {
        version = 3;
        f.write("RBV3", 4);
        int32_t d = dim, vocab = ::riftbound::openspiel::kVocabSize;
        put(&d, 4);
        put(&vocab, 4);
    }
    auto clamp8 = [](int v) { return static_cast<int8_t>(std::max(-127, std::min(127, v))); };
    for (const auto& s : samples) {
        if (s.who != PlayerId::Player1 && s.who != PlayerId::Player2) continue;
        const int8_t label = winner == PlayerId::None ? 2 : (winner == s.who ? 1 : 0);
        const int8_t persp = s.who == PlayerId::Player1 ? 1 : 2;
        const int8_t own = clamp8(s.who == PlayerId::Player1 ? score_p1 : score_p2);
        const int8_t opp = clamp8(s.who == PlayerId::Player1 ? score_p2 : score_p1);
        const int8_t victory = clamp8(victory_score);
        const uint32_t gid = game_id;
        const int32_t turn = s.turn, decision = s.decision_index;
        const uint16_t nnz = static_cast<uint16_t>(std::min<size_t>(s.features.size(), 65535));
        put(&label, 1); put(&persp, 1); put(&own, 1); put(&opp, 1); put(&victory, 1);
        put(&gid, 4); put(&turn, 4); put(&decision, 4); put(&nnz, 2);
        for (size_t j = 0; j < nnz; ++j) { put(&s.features[j].first, 2); put(&s.features[j].second, 4); }
        if (version < 3) continue;
        const uint8_t source = s.policy.empty() ? static_cast<uint8_t>(kPolicyNone) : s.source;
        const uint16_t n_legal = static_cast<uint16_t>(std::min<size_t>(s.policy.size(), 65535));
        put(&source, 1); put(&s.flags, 1); put(&s.sims, 2); put(&n_legal, 2);
        for (size_t j = 0; j < n_legal; ++j) { put(&s.policy[j].first, 2); put(&s.policy[j].second, 4); }
    }
}

ValueFile readValueRecords(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("value data: cannot open " + path);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ValueFile out;
    if (b.size() < 8) throw std::runtime_error("value data: truncated header in " + path);
    if (std::memcmp(b.data(), "RBVD", 4) == 0) out.version = 1;
    else if (std::memcmp(b.data(), "RBV2", 4) == 0) out.version = 2;
    else if (std::memcmp(b.data(), "RBV3", 4) == 0) out.version = 3;
    else throw std::runtime_error("value data: bad magic in " + path);
    size_t pos = 4;
    auto need = [&](size_t n) { if (pos + n > b.size()) throw std::runtime_error("value data: truncated record in " + path); };
    auto rd = [&](void* dst, size_t n) { need(n); std::memcpy(dst, &b[pos], n); pos += n; };
    int32_t dim = 0;
    rd(&dim, 4);
    out.dim = dim;
    if (out.version == 3) { int32_t vocab = 0; rd(&vocab, 4); out.vocab = vocab; }
    while (pos < b.size()) {
        ValueRecord r;
        uint16_t nnz = 0;
        if (out.version == 1) {
            rd(&r.label, 1); rd(&r.turn, 4); rd(&nnz, 2);
        } else {
            rd(&r.label, 1); rd(&r.perspective, 1); rd(&r.own_score, 1); rd(&r.opp_score, 1); rd(&r.victory_score, 1);
            rd(&r.game_id, 4); rd(&r.turn, 4); rd(&r.decision_index, 4); rd(&nnz, 2);
        }
        r.features.resize(nnz);
        for (auto& [i, v] : r.features) { rd(&i, 2); rd(&v, 4); }
        if (out.version == 3) {
            uint16_t n_legal = 0;
            rd(&r.source, 1); rd(&r.flags, 1); rd(&r.sims, 2); rd(&n_legal, 2);
            r.policy.resize(n_legal);
            for (auto& [s, v] : r.policy) { rd(&s, 2); rd(&v, 4); }
        }
        out.records.push_back(std::move(r));
    }
    return out;
}

}  // namespace riftbound::ml
