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

#include <cstdio>
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
