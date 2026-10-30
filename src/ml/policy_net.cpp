#include "ml/policy_net.h"

#include "ml/feature_extractor.h"
#include "openspiel/action_vocab.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <istream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <streambuf>
#include <unordered_map>

namespace riftbound::ml {

namespace {
template <typename T>
void readInto(std::istream& f, T* out, size_t n, const std::string& path) {
    f.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n * sizeof(T)));
    if (!f) throw std::runtime_error("policy net: truncated file " + path);
}

struct MemoryBuf : std::streambuf {
    MemoryBuf(const unsigned char* data, size_t len) {
        char* p = const_cast<char*>(reinterpret_cast<const char*>(data));
        setg(p, p, p + len);
    }
};
}  // namespace

PolicyNet::PolicyNet(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("policy net: cannot open " + path);
    load(f, path);
}

PolicyNet::PolicyNet(const unsigned char* data, size_t len, const std::string& label) {
    MemoryBuf buf(data, len);
    std::istream f(&buf);
    load(f, label);
}

void PolicyNet::load(std::istream& f, const std::string& path) {
    char magic[4];
    readInto(f, magic, 4, path);
    if (std::memcmp(magic, "RBPN", 4) != 0) throw std::runtime_error("policy net: bad magic in " + path);
    int32_t hdr[4];
    readInto(f, hdr, 4, path);
    if (hdr[0] != 1) throw std::runtime_error("policy net: unsupported version in " + path);
    in_dim_ = hdr[1];
    hidden_ = hdr[2];
    vocab_ = hdr[3];
    if (in_dim_ <= 0 || hidden_ <= 0 || vocab_ <= 0 || in_dim_ > (1 << 20) || hidden_ > (1 << 16) || vocab_ > (1 << 20))
        throw std::runtime_error("policy net: bad dimensions in " + path);
    mean_.resize(in_dim_);
    inv_std_.resize(in_dim_);
    w1_.resize(static_cast<size_t>(hidden_) * in_dim_);
    b1_.resize(hidden_);
    w2_.resize(hidden_);
    wm_.resize(hidden_);
    wp_.resize(static_cast<size_t>(vocab_) * hidden_);
    bp_.resize(vocab_);
    readInto(f, mean_.data(), mean_.size(), path);
    readInto(f, inv_std_.data(), inv_std_.size(), path);
    readInto(f, w1_.data(), w1_.size(), path);
    readInto(f, b1_.data(), b1_.size(), path);
    readInto(f, w2_.data(), w2_.size(), path);
    readInto(f, &b2_, 1, path);
    readInto(f, wm_.data(), wm_.size(), path);
    readInto(f, &bm_, 1, path);
    readInto(f, wp_.data(), wp_.size(), path);
    readInto(f, bp_.data(), bp_.size(), path);

    // Fold the standardisation of zero inputs into the bias (see ValueNet::load).
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

std::vector<float> PolicyNet::hidden(const std::vector<std::pair<uint16_t, float>>& x) const {
    std::vector<float> a(b1_eff_);
    for (const auto& [i, v] : x) {
        if (i >= in_dim_ || v == 0.0f) continue;
        const float* col = &w1t_[static_cast<size_t>(i) * hidden_];
        for (int h = 0; h < hidden_; ++h) a[h] += col[h] * v;
    }
    for (auto& v : a) if (v < 0) v = 0;
    return a;
}

std::vector<float> PolicyNet::hidden(const std::vector<float>& x) const {
    std::vector<float> a(b1_eff_);
    const size_t n = std::min<size_t>(x.size(), static_cast<size_t>(in_dim_));
    for (size_t i = 0; i < n; ++i) {
        const float v = x[i];
        if (v == 0.0f) continue;
        const float* col = &w1t_[i * hidden_];
        for (int h = 0; h < hidden_; ++h) a[h] += col[h] * v;
    }
    for (auto& v : a) if (v < 0) v = 0;
    return a;
}

double PolicyNet::winFromHidden(const std::vector<float>& hid) const {
    double out = b2_;
    for (int h = 0; h < hidden_; ++h) out += w2_[h] * hid[h];
    return 1.0 / (1.0 + std::exp(-out));
}

double PolicyNet::marginFromHidden(const std::vector<float>& hid) const {
    double out = bm_;
    for (int h = 0; h < hidden_; ++h) out += wm_[h] * hid[h];
    return std::tanh(out);
}

std::vector<double> PolicyNet::logitsFromHidden(const std::vector<float>& hid, const std::vector<int>& slots) const {
    std::vector<double> out(slots.size(), std::numeric_limits<double>::quiet_NaN());
    for (size_t k = 0; k < slots.size(); ++k) {
        const int s = slots[k];
        if (s < 0 || s >= vocab_) continue;
        const float* row = &wp_[static_cast<size_t>(s) * hidden_];
        double z = bp_[s];
        for (int h = 0; h < hidden_; ++h) z += row[h] * hid[h];
        out[k] = z;
    }
    return out;
}

std::vector<double> PolicyNet::policyFromHidden(const std::vector<float>& hid, const std::vector<int>& slots) const {
    // Softmax over the distinct slots; every occurrence of a slot reports it.
    std::vector<double> logits = logitsFromHidden(hid, slots);
    std::unordered_map<int, double> by_slot;
    double mx = -std::numeric_limits<double>::infinity();
    for (size_t k = 0; k < slots.size(); ++k) {
        if (std::isnan(logits[k])) continue;
        by_slot[slots[k]] = logits[k];
        mx = std::max(mx, logits[k]);
    }
    std::vector<double> out(slots.size(), 0.0);
    if (by_slot.empty()) return out;
    double z = 0.0;
    for (auto& [s, l] : by_slot) { l = std::exp(l - mx); z += l; }
    for (size_t k = 0; k < slots.size(); ++k) {
        auto it = by_slot.find(slots[k]);
        if (it != by_slot.end()) out[k] = it->second / z;
    }
    return out;
}

PolicyNet::Output PolicyNet::evaluate(const std::vector<std::pair<uint16_t, float>>& features,
                                      const std::vector<int>& slots) const {
    const auto hid = hidden(features);
    Output o;
    o.win = winFromHidden(hid);
    o.margin = marginFromHidden(hid);
    o.policy = policyFromHidden(hid, slots);
    return o;
}

double PolicyNet::winProbability(const GameState& state, PlayerId perspective, const CardDB& db) const {
    return winFromHidden(hidden(extractStateFeatures(state, perspective, db)));
}

std::vector<double> PolicyNet::prior(const GameState& state, const std::vector<Intent>& legal,
                                     PlayerId perspective, const CardDB& db, double temperature) const {
    const auto hid = hidden(extractStateFeatures(state, perspective, db));
    const std::vector<int> slots = ::riftbound::openspiel::encodeActions(legal, state);
    std::vector<double> logits = logitsFromHidden(hid, slots);
    const double inv_t = temperature > 0 ? 1.0 / temperature : 1.0;
    std::unordered_map<int, double> by_slot;
    std::unordered_map<int, int> count;
    double mx = -std::numeric_limits<double>::infinity();
    for (size_t k = 0; k < slots.size(); ++k) {
        count[slots[k]] += 1;
        if (std::isnan(logits[k])) continue;
        by_slot[slots[k]] = logits[k] * inv_t;
        mx = std::max(mx, logits[k] * inv_t);
    }
    std::vector<double> out(legal.size(), 0.0);
    if (by_slot.empty()) { std::fill(out.begin(), out.end(), 1.0); return out; }
    double z = 0.0;
    for (auto& [s, l] : by_slot) { l = std::exp(l - mx); z += l; }
    for (size_t k = 0; k < slots.size(); ++k) {
        auto it = by_slot.find(slots[k]);
        // A slot the net cannot score keeps a small floor so it stays reachable.
        out[k] = it == by_slot.end() ? 1e-3 : it->second / z / count[slots[k]];
    }
    return out;
}

std::shared_ptr<const PolicyNet> PolicyNet::shared(const std::string& path) {
    static std::mutex mu;
    static std::map<std::string, std::shared_ptr<const PolicyNet>> cache;
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    auto net = std::make_shared<const PolicyNet>(path);
    cache[path] = net;
    return net;
}

}  // namespace riftbound::ml
