/// Tests for ml::PolicyNet (the RBPN two-head network) and its use as the
/// pmcts prior (agents/ismcts_search.h).

#include "agents/ismcts_search.h"
#include "cards/card_registry.h"
#include "core/card_db.h"
#include "core/events.h"
#include "engine/determinize.h"
#include "engine/game_engine.h"
#include "ml/feature_extractor.h"
#include "ml/policy_net.h"
#include "openspiel/action_vocab.h"
#include "rules/deck_validator.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace riftbound {
namespace {

std::string repoRoot() {
    if (const char* root = std::getenv("RIFTBOUND_ROOT")) return root;
    return "..";
}

/// A random RBPN v1 file in memory with the engine's real input / vocab sizes.
std::vector<unsigned char> randomPolicyNetBytes(int dim, int hidden, int vocab, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    std::vector<unsigned char> out;
    auto put = [&](const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        out.insert(out.end(), b, b + n);
    };
    put("RBPN", 4);
    int32_t hdr[4] = {1, dim, hidden, vocab};
    put(hdr, sizeof hdr);
    auto floats = [&](size_t n, float scale, float shift) {
        for (size_t i = 0; i < n; ++i) { float v = nd(rng) * scale + shift; put(&v, 4); }
    };
    floats(dim, 0.3f, 0.2f);                                  // mean
    floats(dim, 0.5f, 1.5f);                                  // inv_std
    floats(static_cast<size_t>(dim) * hidden, 0.05f, 0.0f);   // W1
    floats(hidden, 0.1f, 0.0f);                               // b1
    floats(hidden, 0.3f, 0.0f);                               // w2
    floats(1, 0.1f, 0.0f);                                    // b2
    floats(hidden, 0.3f, 0.0f);                               // wm
    floats(1, 0.1f, 0.0f);                                    // bm
    floats(static_cast<size_t>(vocab) * hidden, 0.2f, 0.0f);  // Wp
    floats(vocab, 0.2f, 0.0f);                                // bp
    return out;
}

TEST(PolicyNet, MatchesTheNumpyReferenceOnTheSavedTinyNet) {
    const std::string prefix = repoRoot() + "/tests/data/tiny_policy";
    std::ifstream jf(prefix + ".json");
    ASSERT_TRUE(jf.good()) << "missing " << prefix << ".json (python3 lab/learn/rbpn.py --make-fixture tests/data/tiny_policy)";
    const nlohmann::json ref = nlohmann::json::parse(jf);
    ml::PolicyNet net(prefix + ".rbpn");
    EXPECT_EQ(net.inputDim(), ref["in_dim"].get<int>());
    EXPECT_EQ(net.hiddenDim(), ref["hidden"].get<int>());
    EXPECT_EQ(net.vocabSize(), ref["vocab"].get<int>());
    ASSERT_GE(ref["cases"].size(), 3u);
    for (const auto& c : ref["cases"]) {
        std::vector<std::pair<uint16_t, float>> sparse;
        std::vector<float> dense(static_cast<size_t>(net.inputDim()), 0.0f);
        for (const auto& fv : c["features"]) {
            sparse.emplace_back(static_cast<uint16_t>(fv[0].get<int>()), fv[1].get<float>());
            dense[static_cast<size_t>(fv[0].get<int>())] = fv[1].get<float>();
        }
        const std::vector<int> slots = c["slots"].get<std::vector<int>>();
        const auto out = net.evaluate(sparse, slots);
        EXPECT_NEAR(out.win, c["win"].get<double>(), 1e-5);
        EXPECT_NEAR(out.margin, c["margin"].get<double>(), 1e-5);
        ASSERT_EQ(out.policy.size(), slots.size());
        for (size_t k = 0; k < slots.size(); ++k)
            EXPECT_NEAR(out.policy[k], c["policy"][k].get<double>(), 1e-5) << "slot " << slots[k];
        // Dense and sparse hidden layers agree.
        const auto hd = net.hidden(dense);
        const auto hs = net.hidden(sparse);
        ASSERT_EQ(hd.size(), hs.size());
        for (size_t h = 0; h < hd.size(); ++h) EXPECT_NEAR(hd[h], hs[h], 1e-4);
    }
}

TEST(PolicyNet, RejectsBadFiles) {
    std::vector<unsigned char> bytes = randomPolicyNetBytes(20, 4, 10, 1);
    bytes[0] = 'X';
    EXPECT_THROW(ml::PolicyNet(bytes.data(), bytes.size(), "bad-magic"), std::runtime_error);
    bytes = randomPolicyNetBytes(20, 4, 10, 1);
    bytes.resize(bytes.size() - 8);
    EXPECT_THROW(ml::PolicyNet(bytes.data(), bytes.size(), "truncated"), std::runtime_error);
}

TEST(PolicyNet, AnOldSizeNetIsPaddedAndScoresOldSlotsAsBefore) {
    // A net written before the MakeChoiceOpponent block: its policy head has
    // kVocabSizeBeforeOpponentPicks rows. It loads at the full size, every old
    // slot's logit is the file's, and an enemy pick scores as the same card's
    // friendly pick (the slot it was encoded to before).
    const int dim = 20, hidden = 4, old_vocab = openspiel::kVocabSizeBeforeOpponentPicks;
    const auto bytes = randomPolicyNetBytes(dim, hidden, old_vocab, 5);
    ml::PolicyNet net(bytes.data(), bytes.size(), "old-size");
    EXPECT_EQ(net.vocabSize(), openspiel::kVocabSize);
    EXPECT_EQ(net.fileVocabSize(), old_vocab);

    // The file's own policy rows, read straight from the bytes.
    const size_t wp_at = 20 + 4 * (2 * dim + static_cast<size_t>(dim) * hidden + 3 * hidden + 2);
    auto f32 = [&](size_t at) { float v; std::memcpy(&v, &bytes[at], 4); return v; };
    auto fileLogit = [&](const std::vector<float>& hid, int slot) {
        double z = f32(wp_at + 4 * (static_cast<size_t>(old_vocab) * hidden + slot));
        for (int h = 0; h < hidden; ++h) z += f32(wp_at + 4 * (static_cast<size_t>(slot) * hidden + h)) * hid[h];
        return z;
    };

    const std::vector<std::pair<uint16_t, float>> x = {{0, 1.0f}, {3, 2.0f}, {11, -1.5f}, {19, 0.5f}};
    const auto hid = net.hidden(x);
    const int mc = openspiel::verbOffset(openspiel::ActionVerb::MakeChoice);
    const int opp = openspiel::verbOffset(openspiel::ActionVerb::MakeChoiceOpponent);
    const std::vector<int> old_slots = {1, 154, mc, mc + 1, mc + 300, mc + 1200, old_vocab - 1};
    const auto logits = net.logitsFromHidden(hid, old_slots);
    for (size_t k = 0; k < old_slots.size(); ++k)
        EXPECT_NEAR(logits[k], fileLogit(hid, old_slots[k]), 1e-5) << "slot " << old_slots[k];
    // Softmax over old slots only is the file's softmax.
    const auto p = net.policyFromHidden(hid, old_slots);
    double mx = -1e300, z = 0;
    for (int s : old_slots) mx = std::max(mx, fileLogit(hid, s));
    for (int s : old_slots) z += std::exp(fileLogit(hid, s) - mx);
    for (size_t k = 0; k < old_slots.size(); ++k)
        EXPECT_NEAR(p[k], std::exp(fileLogit(hid, old_slots[k]) - mx) / z, 1e-6);
    // New enemy rows copy the friendly row of the same card (bias included).
    const auto pair = net.logitsFromHidden(hid, {mc + 300, opp + 299, mc + 1, opp, mc + 1200, opp + 1199});
    EXPECT_DOUBLE_EQ(pair[0], pair[1]);
    EXPECT_DOUBLE_EQ(pair[2], pair[3]);
    EXPECT_DOUBLE_EQ(pair[4], pair[5]);
    EXPECT_TRUE(std::isnan(net.logitsFromHidden(hid, {openspiel::kVocabSize})[0]));
}

TEST(PolicyNet, SmallAndFullSizeNetsAreNotPadded) {
    const auto tiny = randomPolicyNetBytes(20, 4, 10, 2);
    ml::PolicyNet a(tiny.data(), tiny.size(), "tiny");
    EXPECT_EQ(a.vocabSize(), 10);
    const auto full = randomPolicyNetBytes(20, 4, openspiel::kVocabSize, 3);
    ml::PolicyNet b(full.data(), full.size(), "full");
    EXPECT_EQ(b.vocabSize(), openspiel::kVocabSize);
    EXPECT_EQ(b.fileVocabSize(), openspiel::kVocabSize);
}

class PolicyPriorTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        registry_ = new CardRegistry();
        registry_->loadAll();
        db_ = new CardDB();
        db_->buildFromClasses(*registry_);
        deck_ = new DeckSubmission(DeckValidator::loadFromDeckList(repoRoot() + "/decks/riven_sacramento.txt", *db_));
    }
    static void TearDownTestSuite() {
        delete deck_; deck_ = nullptr;
        delete db_; db_ = nullptr;
        delete registry_; registry_ = nullptr;
    }

    /// Random play until a resumable decision with at least `min_options`
    /// options whose actor holds no priority inside a chain.
    bool reachDecision(GameEngine& engine, uint64_t seed, int min_options, int max_decisions = 400) {
        std::mt19937_64 rng(seed * 31 + 7);
        StepResult sr = engine.beginGame(*deck_, *deck_, seed);
        for (int n = 0; n < max_decisions && sr.kind == StepKind::NeedDecision; ++n) {
            const GameState& s = engine.state();
            if (static_cast<int>(sr.legal.size()) >= min_options && !s.cost_cursor.has_value() &&
                s.turn.phase != TurnPhase::Mulligan && s.turn.phase != TurnPhase::Setup &&
                search::isResumableSnapshot(s))
                return true;
            int idx = static_cast<int>(rng() % sr.legal.size());
            if (sr.legal[idx].type == IntentType::Concede) idx = 0;
            sr = engine.applyChoice(idx);
        }
        return false;
    }

    static CardRegistry* registry_;
    static CardDB* db_;
    static DeckSubmission* deck_;
};
CardRegistry* PolicyPriorTest::registry_ = nullptr;
CardDB* PolicyPriorTest::db_ = nullptr;
DeckSubmission* PolicyPriorTest::deck_ = nullptr;

TEST_F(PolicyPriorTest, ThePriorReachesTheSearcherAndSteersRootVisits) {
    EventBus bus;
    GameEngine engine(*db_, bus, *registry_);
    ASSERT_TRUE(reachDecision(engine, 11, 3));
    const GameState& state = engine.state();
    const std::vector<Intent> legal = engine.currentStep().legal;
    const PlayerId me = search::decidingPlayerOf(state, legal);

    // Favour the last option that is not a concession overwhelmingly.
    int favoured = static_cast<int>(legal.size()) - 1;
    while (favoured > 0 && legal[static_cast<size_t>(favoured)].type == IntentType::Concede) --favoured;
    const int favoured_slot = openspiel::encodeAction(legal[static_cast<size_t>(favoured)], state);

    SearchOptions opts;
    opts.worlds = 2;
    opts.determinize = false;
    search::IsMctsSearch s(*db_, *registry_, *deck_, *deck_, 11, 40, opts);
    EXPECT_FALSE(s.hasPrior());
    int calls = 0, saw_favoured = 0;
    s.setPrior([&](const GameState& gs, const std::vector<Intent>& opts_legal) {
        ++calls;
        std::vector<double> w(opts_legal.size(), 1e-4);
        bool saw = false;
        for (size_t i = 0; i < opts_legal.size(); ++i)
            if (openspiel::encodeAction(opts_legal[i], gs) == favoured_slot) { w[i] = 1.0; saw = true; }
        if (saw) ++saw_favoured;
        return w;
    });
    EXPECT_TRUE(s.hasPrior());
    const auto res = s.search(state, legal, me, 5);
    EXPECT_GT(calls, 0);
    EXPECT_GE(saw_favoured, 1) << "the prior is asked at the root, where the favoured option is legal";
    ASSERT_EQ(res.visits.size(), legal.size());
    int total = 0, best = -1;
    for (size_t i = 0; i < legal.size(); ++i) {
        total += res.visits[i];
        if (best < 0 || res.visits[i] > res.visits[static_cast<size_t>(best)]) best = static_cast<int>(i);
    }
    ASSERT_GT(total, 0);
    EXPECT_EQ(openspiel::encodeAction(legal[static_cast<size_t>(best)], state), favoured_slot)
        << "the prior-favoured option should collect the most root visits";
    EXPECT_GT(res.visits[static_cast<size_t>(best)] * 2, total);
}

TEST_F(PolicyPriorTest, EvalPolicyLoadsBothHeadsAndSearches) {
    const auto path = (std::filesystem::temp_directory_path() / "riftbound_test_policy.rbpn").string();
    {
        const auto bytes = randomPolicyNetBytes(ml::kStateFeatureDim, 8, openspiel::kVocabSize, 77);
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                    static_cast<std::streamsize>(bytes.size()));
    }
    EventBus bus;
    GameEngine engine(*db_, bus, *registry_);
    ASSERT_TRUE(reachDecision(engine, 13, 2));
    const GameState& state = engine.state();
    const std::vector<Intent> legal = engine.currentStep().legal;
    const PlayerId me = search::decidingPlayerOf(state, legal);

    SearchOptions opts;
    opts.eval = "policy:" + path;
    opts.worlds = 2;
    search::IsMctsSearch s(*db_, *registry_, *deck_, *deck_, 13, 24, opts);
    ASSERT_NE(s.policyNet(), nullptr);
    EXPECT_TRUE(s.hasPrior()) << "prior=auto installs the policy head";
    EXPECT_EQ(s.policyNet()->vocabSize(), openspiel::kVocabSize);
    // The prior is a proper distribution over the legal options from the actor's view.
    const auto p = s.policyNet()->prior(state, legal, me, *db_);
    ASSERT_EQ(p.size(), legal.size());
    double sum = 0.0;
    for (double v : p) { EXPECT_GE(v, 0.0); sum += v; }
    EXPECT_NEAR(sum, 1.0, 1e-6);
    // The value head evaluates leaves.
    const double v = s.evaluate(state, me);
    EXPECT_GE(v, -1.0);
    EXPECT_LE(v, 1.0);
    EXPECT_NEAR(v, 2.0 * s.policyNet()->winProbability(state, me, *db_) - 1.0, 1e-9);
    const auto res = s.search(state, legal, me, 3);
    EXPECT_GT(res.simulations, 0);
    EXPECT_GE(res.chosen, 0);

    // Gumbel sequential halving at the root uses the same prior and budget.
    opts.root = "gumbel";
    opts.gumbel_m = 4;
    search::IsMctsSearch sg(*db_, *registry_, *deck_, *deck_, 13, 24, opts);
    const auto rg = sg.search(state, legal, me, 3);
    EXPECT_GT(rg.simulations, 0);
    ASSERT_GE(rg.chosen, 0);
    ASSERT_LT(rg.chosen, static_cast<int>(legal.size()));
    EXPECT_GT(rg.visits[static_cast<size_t>(rg.chosen)], 0);
    int visited = 0;
    for (int n : rg.visits) visited += n > 0;
    EXPECT_LE(visited, 4) << "at most gumbel_m root candidates are searched";
    opts.root = "puct";

    // prior=uniform keeps the value head but drops the prior.
    opts.prior = "uniform";
    search::IsMctsSearch s2(*db_, *registry_, *deck_, *deck_, 13, 8, opts);
    EXPECT_FALSE(s2.hasPrior());
    EXPECT_NE(s2.policyNet(), nullptr);
    // prior=net without a policy file is an error.
    SearchOptions bad;
    bad.prior = "net";
    EXPECT_THROW(search::IsMctsSearch(*db_, *registry_, *deck_, *deck_, 13, 8, bad), std::runtime_error);
    std::remove(path.c_str());
}

}  // namespace
}  // namespace riftbound
