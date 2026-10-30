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
    // A fresh file is RBV3: the RBV2 record layout plus a policy block
    // (empty here: no policy target was set).
    const auto path = (std::filesystem::temp_directory_path() / "riftbound_value_test.rbv3").string();
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
    ASSERT_GE(b.size(), 12u);
    EXPECT_EQ(std::memcmp(b.data(), "RBV3", 4), 0);
    int32_t dim; std::memcpy(&dim, &b[4], 4);
    EXPECT_EQ(dim, kStateFeatureDim);
    int32_t vocab; std::memcpy(&vocab, &b[8], 4);
    EXPECT_GT(vocab, 1200);
    size_t pos = 12;
    auto rd8 = [&]() { return static_cast<int8_t>(b[pos++]); };
    auto rd32 = [&]() { int32_t v; std::memcpy(&v, &b[pos], 4); pos += 4; return v; };
    auto rdu32 = [&]() { uint32_t v; std::memcpy(&v, &b[pos], 4); pos += 4; return v; };
    auto rd16 = [&]() { uint16_t v; std::memcpy(&v, &b[pos], 2); pos += 2; return v; };
    auto policy_block = [&]() { EXPECT_EQ(rd8(), 0); EXPECT_EQ(rd8(), 0); EXPECT_EQ(rd16(), 0); EXPECT_EQ(rd16(), 0); };
    // Record 1: P1 lost 5-8.
    EXPECT_EQ(rd8(), 0); EXPECT_EQ(rd8(), 1); EXPECT_EQ(rd8(), 5); EXPECT_EQ(rd8(), 8); EXPECT_EQ(rd8(), 8);
    EXPECT_EQ(rdu32(), 0xABCDEF01u); EXPECT_EQ(rd32(), 3); EXPECT_EQ(rd32(), 17);
    ASSERT_EQ(rd16(), 3);
    EXPECT_EQ(rd16(), 2); float v; std::memcpy(&v, &b[pos], 4); pos += 4; EXPECT_FLOAT_EQ(v, 1.0f);
    EXPECT_EQ(rd16(), 470); std::memcpy(&v, &b[pos], 4); pos += 4; EXPECT_FLOAT_EQ(v, 2.0f);
    EXPECT_EQ(rd16(), 6000); std::memcpy(&v, &b[pos], 4); pos += 4; EXPECT_FLOAT_EQ(v, -0.5f);
    policy_block();
    // Record 2: P2 won 8-5 (own score first).
    EXPECT_EQ(rd8(), 1); EXPECT_EQ(rd8(), 2); EXPECT_EQ(rd8(), 8); EXPECT_EQ(rd8(), 5); EXPECT_EQ(rd8(), 8);
    EXPECT_EQ(rdu32(), 0xABCDEF01u); EXPECT_EQ(rd32(), 4); EXPECT_EQ(rd32(), 18);
    ASSERT_EQ(rd16(), 1); pos += 6;
    policy_block();
    // Record 3: a draw.
    EXPECT_EQ(rd8(), 2); EXPECT_EQ(rd8(), 1); EXPECT_EQ(rd8(), 4); EXPECT_EQ(rd8(), 4); EXPECT_EQ(rd8(), 8);
    EXPECT_EQ(rdu32(), 7u);
    // The reader agrees.
    const ValueFile vf = readValueRecords(path);
    EXPECT_EQ(vf.version, 3);
    ASSERT_EQ(vf.records.size(), 3u);
    EXPECT_EQ(vf.records[2].label, 2);
    EXPECT_TRUE(vf.records[0].policy.empty());
    std::remove(path.c_str());
}

TEST(ValueData, PolicyRecordsRoundTripThroughTheReader) {
    const auto path = (std::filesystem::temp_directory_path() / "riftbound_policy_test.rbv3").string();
    std::remove(path.c_str());
    // Three options; the first two alias to one slot (a unit playable to two
    // locations would not, but two targets of one spell do).
    std::vector<int> slots = {1200, 1200, 7};
    ValueSample a;
    a.who = PlayerId::Player1; a.turn = 2; a.decision_index = 9;
    a.features = {{3, 1.0f}, {600, 2.0f}};
    setPolicyTarget(a, slots, {{0, 0.25}, {1, 0.25}, {2, 0.5}}, 30);
    a.flags |= kPolicyFlagCleanBoundary;
    ASSERT_EQ(a.source, kPolicySearch);
    ASSERT_EQ(a.policy.size(), 2u);
    EXPECT_EQ(a.policy[0].first, 1200); EXPECT_FLOAT_EQ(a.policy[0].second, 0.5f);
    EXPECT_EQ(a.policy[1].first, 7);    EXPECT_FLOAT_EQ(a.policy[1].second, 0.5f);
    ValueSample h;                              // a human one-hot
    h.who = PlayerId::Player2; h.turn = 3; h.decision_index = 12;
    h.features = {{10, 1.0f}};
    setHumanTarget(h, slots, 2);
    ASSERT_EQ(h.source, kPolicyHuman);
    ValueSample none;                           // an agent that did not search
    none.who = PlayerId::Player1; none.turn = 4; none.decision_index = 15;
    none.features = {{11, 1.0f}};
    setPolicyTarget(none, slots, {}, 0);
    EXPECT_EQ(none.source, kPolicyNone);
    EXPECT_TRUE(none.policy.empty());

    appendValueRecords(path, kStateFeatureDim, {a, h, none}, PlayerId::Player1, 8, 3, 8, 42u);
    appendValueRecords(path, kStateFeatureDim, {h}, PlayerId::Player2, 1, 8, 8, 43u);   // appends in v3

    const ValueFile f = readValueRecords(path);
    EXPECT_EQ(f.version, 3);
    EXPECT_EQ(f.dim, kStateFeatureDim);
    EXPECT_GT(f.vocab, 1200);
    ASSERT_EQ(f.records.size(), 4u);
    const auto& r0 = f.records[0];
    EXPECT_EQ(r0.label, 1); EXPECT_EQ(r0.perspective, 1); EXPECT_EQ(r0.own_score, 8); EXPECT_EQ(r0.opp_score, 3);
    EXPECT_EQ(r0.game_id, 42u); EXPECT_EQ(r0.turn, 2); EXPECT_EQ(r0.decision_index, 9);
    ASSERT_EQ(r0.features.size(), 2u); EXPECT_EQ(r0.features[1].first, 600);
    EXPECT_EQ(r0.source, kPolicySearch); EXPECT_EQ(r0.flags, kPolicyFlagCleanBoundary); EXPECT_EQ(r0.sims, 30);
    ASSERT_EQ(r0.policy.size(), 2u);
    EXPECT_EQ(r0.policy[0].first, 1200); EXPECT_FLOAT_EQ(r0.policy[0].second, 0.5f);
    const auto& r1 = f.records[1];
    EXPECT_EQ(r1.label, 0); EXPECT_EQ(r1.perspective, 2); EXPECT_EQ(r1.own_score, 3);
    EXPECT_EQ(r1.source, kPolicyHuman);
    ASSERT_EQ(r1.policy.size(), 2u);
    EXPECT_FLOAT_EQ(r1.policy[0].second, 0.0f); EXPECT_EQ(r1.policy[1].first, 7); EXPECT_FLOAT_EQ(r1.policy[1].second, 1.0f);
    const auto& r2 = f.records[2];
    EXPECT_EQ(r2.source, kPolicyNone); EXPECT_TRUE(r2.policy.empty());
    EXPECT_EQ(f.records[3].label, 1); EXPECT_EQ(f.records[3].game_id, 43u);
    std::remove(path.c_str());

    // An existing v2 file keeps its format (the policy block is dropped).
    const auto path2 = (std::filesystem::temp_directory_path() / "riftbound_policy_test.rbv2").string();
    std::remove(path2.c_str());
    { std::ofstream o(path2, std::ios::binary); o.write("RBV2", 4); int32_t d = kStateFeatureDim; o.write(reinterpret_cast<const char*>(&d), 4); }
    appendValueRecords(path2, kStateFeatureDim, {a}, PlayerId::Player1, 8, 3, 8, 1u);
    const ValueFile f2 = readValueRecords(path2);
    EXPECT_EQ(f2.version, 2);
    ASSERT_EQ(f2.records.size(), 1u);
    EXPECT_TRUE(f2.records[0].policy.empty());
    EXPECT_EQ(f2.records[0].features.size(), 2u);
    std::remove(path2.c_str());
}

}  // namespace
}  // namespace riftbound::ml
