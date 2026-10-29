/// Tests for ml::ValueNet's sparse inference and the value-data record format.

#include "ml/value_net.h"
#include "ml/feature_extractor.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

namespace riftbound::ml {
namespace {

/// A random RBVN v1 file in memory.
std::vector<unsigned char> randomNetBytes(int dim, int hidden, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    std::vector<unsigned char> out;
    auto put = [&](const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        out.insert(out.end(), b, b + n);
    };
    put("RBVN", 4);
    int32_t hdr[3] = {1, dim, hidden};
    put(hdr, sizeof hdr);
    auto floats = [&](size_t n, float scale, float shift) {
        for (size_t i = 0; i < n; ++i) { float v = nd(rng) * scale + shift; put(&v, 4); }
    };
    floats(dim, 0.3f, 0.2f);                 // mean
    floats(dim, 0.5f, 1.5f);                 // inv_std (some near zero)
    floats(static_cast<size_t>(dim) * hidden, 0.1f, 0.0f);   // w1
    floats(hidden, 0.1f, 0.0f);              // b1
    floats(hidden, 0.3f, 0.0f);              // w2
    floats(1, 0.1f, 0.0f);                   // b2
    return out;
}

TEST(ValueNet, SparseInferenceMatchesTheDensePass) {
    const int dim = 300, hidden = 16;
    auto bytes = randomNetBytes(dim, hidden, 5);
    ValueNet net(bytes.data(), bytes.size(), "test");
    EXPECT_EQ(net.inputDim(), dim);
    std::mt19937_64 rng(9);
    for (int trial = 0; trial < 50; ++trial) {
        std::vector<float> x(dim, 0.0f);
        const int nnz = 1 + static_cast<int>(rng() % 20);
        for (int k = 0; k < nnz; ++k) x[rng() % dim] = static_cast<float>(1 + rng() % 5);
        const double dense = net.winProbabilityDense(x);
        const double sparse = net.winProbability(x);
        const double sparse2 = net.winProbabilitySparse(sparsify(x));
        EXPECT_NEAR(dense, sparse, 1e-5);
        EXPECT_NEAR(dense, sparse2, 1e-5);
        EXPECT_GT(dense, 0.0);
        EXPECT_LT(dense, 1.0);
    }
    // All-zero input: only the folded bias contributes.
    std::vector<float> zero(dim, 0.0f);
    EXPECT_NEAR(net.winProbabilityDense(zero), net.winProbability(zero), 1e-5);
}

TEST(ValueData, RecordsRoundTripWithResultAndScores) {
    const auto path = (std::filesystem::temp_directory_path() / "riftbound_value_test.rbv2").string();
    std::remove(path.c_str());
    std::vector<ValueSample> samples(2);
    samples[0].who = PlayerId::Player1; samples[0].turn = 3; samples[0].decision_index = 17;
    samples[0].features = {{2, 1.0f}, {470, 2.0f}, {6000, -0.5f}};
    samples[1].who = PlayerId::Player2; samples[1].turn = 4; samples[1].decision_index = 18;
    samples[1].features = {{5, 3.0f}};
    appendValueRecords(path, kStateFeatureDim, samples, PlayerId::Player2, 5, 8, 8, 0xABCDEF01u);
    appendValueRecords(path, kStateFeatureDim, {samples[0]}, PlayerId::None, 4, 4, 8, 7u);

    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_GE(b.size(), 8u);
    EXPECT_EQ(std::memcmp(b.data(), "RBV2", 4), 0);
    int32_t dim; std::memcpy(&dim, &b[4], 4);
    EXPECT_EQ(dim, kStateFeatureDim);
    size_t pos = 8;
    auto rd8 = [&]() { return static_cast<int8_t>(b[pos++]); };
    auto rd32 = [&]() { int32_t v; std::memcpy(&v, &b[pos], 4); pos += 4; return v; };
    auto rdu32 = [&]() { uint32_t v; std::memcpy(&v, &b[pos], 4); pos += 4; return v; };
    auto rd16 = [&]() { uint16_t v; std::memcpy(&v, &b[pos], 2); pos += 2; return v; };
    // Record 1: P1 lost 5-8.
    EXPECT_EQ(rd8(), 0); EXPECT_EQ(rd8(), 1); EXPECT_EQ(rd8(), 5); EXPECT_EQ(rd8(), 8); EXPECT_EQ(rd8(), 8);
    EXPECT_EQ(rdu32(), 0xABCDEF01u); EXPECT_EQ(rd32(), 3); EXPECT_EQ(rd32(), 17);
    ASSERT_EQ(rd16(), 3);
    EXPECT_EQ(rd16(), 2); float v; std::memcpy(&v, &b[pos], 4); pos += 4; EXPECT_FLOAT_EQ(v, 1.0f);
    EXPECT_EQ(rd16(), 470); std::memcpy(&v, &b[pos], 4); pos += 4; EXPECT_FLOAT_EQ(v, 2.0f);
    EXPECT_EQ(rd16(), 6000); std::memcpy(&v, &b[pos], 4); pos += 4; EXPECT_FLOAT_EQ(v, -0.5f);
    // Record 2: P2 won 8-5 (own score first).
    EXPECT_EQ(rd8(), 1); EXPECT_EQ(rd8(), 2); EXPECT_EQ(rd8(), 8); EXPECT_EQ(rd8(), 5); EXPECT_EQ(rd8(), 8);
    EXPECT_EQ(rdu32(), 0xABCDEF01u); EXPECT_EQ(rd32(), 4); EXPECT_EQ(rd32(), 18);
    ASSERT_EQ(rd16(), 1); pos += 6;
    // Record 3: a draw.
    EXPECT_EQ(rd8(), 2); EXPECT_EQ(rd8(), 1); EXPECT_EQ(rd8(), 4); EXPECT_EQ(rd8(), 4); EXPECT_EQ(rd8(), 8);
    EXPECT_EQ(rdu32(), 7u);
    std::remove(path.c_str());
}

}  // namespace
}  // namespace riftbound::ml
