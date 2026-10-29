#include "ml/value_net.h"
#include "ml/feature_extractor.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <istream>
#include <streambuf>
#include <map>
#include <mutex>
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
}

double ValueNet::winProbability(const std::vector<float>& x) const {
    // Standardize once; most features are zero, but zero standardizes to
    // -mean*inv_std, so the dense pass is simplest and still cheap (~400k MACs).
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

void appendValueRecords(const std::string& path, int dim,
                        const std::vector<std::pair<PlayerId, std::vector<float>>>& samples,
                        const std::vector<int>& turns, PlayerId winner) {
    static std::mutex mu;
    std::lock_guard<std::mutex> lock(mu);
    const bool fresh = !std::ifstream(path).good();
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (fresh) {
        f.write("RBVD", 4);
        int32_t d = dim;
        f.write(reinterpret_cast<const char*>(&d), 4);
    }
    for (size_t k = 0; k < samples.size(); ++k) {
        const auto& [who, x] = samples[k];
        int8_t label = winner == who ? 1 : 0;
        int32_t turn = k < turns.size() ? turns[k] : 0;
        std::vector<std::pair<uint16_t, float>> nz;
        for (size_t i = 0; i < x.size() && i < 65535; ++i)
            if (x[i] != 0.0f) nz.emplace_back(static_cast<uint16_t>(i), x[i]);
        uint16_t nnz = static_cast<uint16_t>(std::min<size_t>(nz.size(), 65535));
        f.write(reinterpret_cast<const char*>(&label), 1);
        f.write(reinterpret_cast<const char*>(&turn), 4);
        f.write(reinterpret_cast<const char*>(&nnz), 2);
        for (size_t j = 0; j < nnz; ++j) {
            f.write(reinterpret_cast<const char*>(&nz[j].first), 2);
            f.write(reinterpret_cast<const char*>(&nz[j].second), 4);
        }
    }
}

}  // namespace riftbound::ml
