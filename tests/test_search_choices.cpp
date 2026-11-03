/// The search at a resolving card's optional draw — Lightning Rush's "draw
/// one of the top 3 cards" and Trifarian Captain's "you may reveal ... and
/// draw it" (Kennen deck) — reached by heuristic-weighted random play and
/// searched as the browser's strongest bot does (sims 150, worlds 8,
/// settle 60).
///
/// The search tests use a leaf evaluation that counts cards in hand, so they
/// do not depend on a trained net. The Diagnose* tests print what the current
/// nets of the learning ladder (lab/learn/ladder.json) make of the same
/// decisions; they skip when the nets are not there.

#include "agents/ismcts_search.h"
#include "cards/card_registry.h"
#include "core/card_db.h"
#include "core/events.h"
#include "engine/determinize.h"
#include "engine/game_engine.h"
#include "ml/policy_net.h"
#include "ml/value_net.h"
#include "rules/deck_validator.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace riftbound {
namespace {

std::string repoRoot() {
    if (const char* root = std::getenv("RIFTBOUND_ROOT")) return root;
    return "..";
}

constexpr CardDefId kTrifarianCaptain = 974;
constexpr CardDefId kLightningRush = 943;
const char* kKennen = "/lab/decks/meta/sacramento_showdown_2026-10-24/06_dylan_sumner_kennen.txt";

/// The current net of the learning ladder with extension `ext`, "" when absent.
std::string currentNet(const std::string& ext) {
    std::ifstream f(repoRoot() + "/lab/learn/ladder.json");
    if (!f) return "";
    try {
        const auto j = nlohmann::json::parse(f);
        std::string rel = j.value("current_net", "");
        if (rel.empty()) return "";
        rel = rel.substr(0, rel.find_last_of('.')) + ext;
        const std::string path = repoRoot() + "/" + rel;
        return std::filesystem::exists(path) ? path : "";
    } catch (...) {
        return "";
    }
}

/// Cards in hand, own minus the opponent's: a leaf value that wants to draw.
double handEval(const GameState& s, PlayerId me) {
    return 0.1 * (static_cast<double>(s.player(me).hand.size()) - static_cast<double>(s.player(opponent(me)).hand.size()));
}

/// Neither player close to winning, past the opening.
bool balanced(const GameState& s) {
    const int a = s.players[0].score, b = s.players[1].score;
    return s.turn.turn_number >= 3 && a <= 5 && b <= 5 && std::abs(a - b) <= 2;
}

struct Position {
    GameState state;
    std::vector<Intent> legal;
    std::vector<int32_t> history;
    uint64_t seed = 0;
    PlayerId me = PlayerId::None;
};

bool isYesNo(const std::vector<Intent>& legal) {
    return legal.size() == 2 && legal[0].type == IntentType::MakeChoice && legal[1].type == IntentType::MakeChoice &&
           legal[0].chosen_value.has_value() && legal[1].chosen_value.has_value();
}

bool isRushPick(const std::vector<Intent>& legal) {
    int objs = 0, none = 0;
    for (const auto& in : legal) {
        if (in.type != IntentType::MakeChoice) return false;
        if (!in.chosen_objects.empty()) ++objs;
        else if (in.chosen_value.has_value()) ++none;
    }
    return objs >= 2 && none == 1;
}

int valueIndex(const std::vector<Intent>& legal, int v) {
    for (size_t i = 0; i < legal.size(); ++i)
        if (legal[i].chosen_objects.empty() && legal[i].chosen_value == v) return static_cast<int>(i);
    return -1;
}

class SearchChoicesTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        registry_ = new CardRegistry();
        registry_->loadAll();
        db_ = new CardDB();
        db_->buildFromClasses(*registry_);
        const std::string path = repoRoot() + kKennen;
        if (std::filesystem::exists(path)) deck_ = new DeckSubmission(DeckValidator::loadFromDeckList(path, *db_));
    }
    static void TearDownTestSuite() {
        delete deck_; deck_ = nullptr;
        delete db_; db_ = nullptr;
        delete registry_; registry_ = nullptr;
    }

    /// Up to `max` decisions of a resolving `card`'s choice whose options
    /// satisfy `want`, in balanced positions the search resumes in place;
    /// one per game, Kennen mirror, play weighted by the action-type prior.
    template <typename Pred>
    std::vector<Position> find(CardDefId card, Pred want, size_t max, int seeds = 400) const {
        std::vector<Position> out;
        for (uint64_t seed = 1; seed <= static_cast<uint64_t>(seeds) && out.size() < max; ++seed) {
            EventBus bus;
            GameEngine engine(*db_, bus, *registry_);
            std::mt19937_64 rng(seed * 131 + 3);
            std::vector<int32_t> history;
            StepResult sr = engine.beginGame(*deck_, *deck_, seed);
            for (int n = 0; n < 1500 && sr.kind == StepKind::NeedDecision; ++n) {
                const GameState& s = engine.state();
                if (s.chain.resuming.has_value() && s.chain.resuming->card_def_id == card &&
                    s.decision_site.kind == DecisionSiteKind::ChainChoice && search::isResumableSnapshot(s) &&
                    balanced(s) && want(sr.legal)) {
                    out.push_back(Position{s, sr.legal, history, seed, search::decidingPlayerOf(s, sr.legal)});
                    break;
                }
                auto w = search::IsMctsSearch::heuristicPrior(s, sr.legal);
                for (size_t i = 0; i < w.size(); ++i)
                    if (sr.legal[i].type == IntentType::Concede) w[i] = 0;
                std::discrete_distribution<int> pick(w.begin(), w.end());
                const int idx = pick(rng);
                history.push_back(idx);
                sr = engine.applyChoice(idx);
            }
        }
        return out;
    }

    search::SearchResult run(const Position& p, SearchOptions opts, bool hand_eval = false) const {
        search::IsMctsSearch s(*db_, *registry_, *deck_, *deck_, p.seed, 150, opts);
        if (hand_eval) s.setEvaluator(handEval);
        return s.search(p.state, p.legal, p.me, 2026, &p.history);
    }

    static SearchOptions browserOptions() {
        SearchOptions o;
        o.worlds = 8;
        o.settle = 60;
        o.prior = "heuristic";
        return o;
    }

    std::string table(const Position& p, const search::SearchResult& r) const {
        std::string t;
        for (size_t i = 0; i < p.legal.size(); ++i) {
            const auto& in = p.legal[i];
            std::string what;
            if (!in.chosen_objects.empty() && p.state.objectExists(in.chosen_objects[0]))
                what = p.state.getObject(in.chosen_objects[0]).name;
            else if (in.chosen_value) what = !in.choice_label.empty() ? in.choice_label : *in.chosen_value ? "yes" : "no";
            if (what.size() > 28) what = what.substr(0, 28);
            char buf[160];
            std::snprintf(buf, sizeof buf, "\n    [%zu] %-28s visits %4d  value %+.3f", i, what.c_str(), r.visits[i], r.values[i]);
            t += buf;
        }
        return t;
    }
    static std::string head(const search::SearchResult& r) {
        return "sims " + std::to_string(r.simulations) + ", visits on the options " +
               std::to_string(std::accumulate(r.visits.begin(), r.visits.end(), 0)) +
               ", failed " + std::to_string(r.failed) + ", rejected " + std::to_string(r.replay_rejected);
    }

    static CardRegistry* registry_;
    static CardDB* db_;
    static DeckSubmission* deck_;
};

CardRegistry* SearchChoicesTest::registry_ = nullptr;
CardDB* SearchChoicesTest::db_ = nullptr;
DeckSubmission* SearchChoicesTest::deck_ = nullptr;

TEST_F(SearchChoicesTest, ARedealKeepsTheCardsAResolvingCardShowsItsChooser) {
    // Lightning Rush shows its controller the top 3 cards of its deck; the
    // worlds the search samples must keep those three where they are (the
    // rest of the deck is still re-dealt).
    if (!deck_) GTEST_SKIP() << "no Kennen deck";
    const auto found = find(kLightningRush, isRushPick, 1);
    ASSERT_EQ(found.size(), 1u) << "no Lightning Rush choice reached";
    const Position& p = found[0];
    search::IsMctsSearch s(*db_, *registry_, *deck_, *deck_, p.seed, 10, browserOptions());
    const auto worlds = s.sampleWorlds(p.state, p.me, 16, 7);
    int deck_moved = 0;
    for (const auto& w : worlds) {
        for (const auto& in : p.legal)
            for (auto oid : in.chosen_objects)
                EXPECT_EQ(w.getObject(oid).card_def_id, p.state.getObject(oid).card_def_id) << p.state.getObject(oid).name;
        for (auto oid : p.state.player(p.me).main_deck)
            deck_moved += w.getObject(oid).card_def_id != p.state.getObject(oid).card_def_id;
    }
    EXPECT_GT(deck_moved, 0) << "the rest of the deck is no longer re-dealt";
}

TEST_F(SearchChoicesTest, LightningRushPickReachesTheCardsItShows) {
    // The tree keys a card option by the card's identity. Before the cards
    // shown were kept in every world, each world had other cards behind the
    // three options: most visits to "draw this card" landed on keys no real
    // option has and were dropped, while "Draw none" (an int answer) kept
    // all of its own — the search leaned to declining the draw.
    if (!deck_) GTEST_SKIP() << "no Kennen deck";
    const auto found = find(kLightningRush, isRushPick, 4);
    ASSERT_GE(found.size(), 2u) << "no Lightning Rush choice reached";
    for (const auto& p : found) {
        const auto r = run(p, browserOptions(), /*hand_eval=*/true);
        std::cout << "[choices] Lightning Rush (seed " << p.seed << "), cards-in-hand eval: " << head(r) << table(p, r) << "\n";
        EXPECT_EQ(r.failed, 0);
        EXPECT_EQ(std::accumulate(r.visits.begin(), r.visits.end(), 0), r.simulations) << table(p, r);
        EXPECT_NE(r.chosen, valueIndex(p.legal, 0)) << "declined the draw" << table(p, r);
    }
}

TEST_F(SearchChoicesTest, TrifarianCaptainTakesTheFreeDrawWhenCardsCount) {
    if (!deck_) GTEST_SKIP() << "no Kennen deck";
    const auto found = find(kTrifarianCaptain, isYesNo, 4);
    ASSERT_GE(found.size(), 2u) << "no Trifarian Captain choice reached";
    for (const auto& p : found) {
        const auto r = run(p, browserOptions(), /*hand_eval=*/true);
        std::cout << "[choices] Trifarian Captain (seed " << p.seed << "), cards-in-hand eval: " << head(r) << table(p, r) << "\n";
        EXPECT_EQ(r.failed, 0);
        EXPECT_EQ(r.chosen, valueIndex(p.legal, 1)) << table(p, r);
    }
}

TEST_F(SearchChoicesTest, FizzFreeSpellIsAskedOnceAtFinalizationAndSearchedByReplay) {
    // Fizz, Trickster decides its "you may play a spell from your trash" when
    // the trigger is finalized (CR 383.3): a decision inside the engine that
    // the search rebuilds by replaying the game (with the exact decision
    // history the browser and the lab pass). The answer is stored, not asked
    // again when the trigger resolves.
    if (!deck_) GTEST_SKIP() << "no Kennen deck";
    auto isFizz = [](const std::vector<Intent>& legal) {
        return isYesNo(legal) && legal[1].choice_label.find("Fizz") != std::string::npos;
    };
    int checked = 0;
    for (uint64_t seed = 1; seed <= 400 && checked < 2; ++seed) {
        EventBus bus;
        GameEngine engine(*db_, bus, *registry_);
        std::mt19937_64 rng(seed * 131 + 3);
        std::vector<int32_t> history;
        StepResult sr = engine.beginGame(*deck_, *deck_, seed);
        for (int n = 0; n < 1500 && sr.kind == StepKind::NeedDecision; ++n) {
            if (isFizz(sr.legal)) {
                const GameState& s = engine.state();
                EXPECT_FALSE(search::isResumableSnapshot(s));   // a finalization: rebuilt by replay
                Position p{s, sr.legal, history, seed, search::decidingPlayerOf(s, sr.legal)};
                const auto r = run(p, browserOptions(), /*hand_eval=*/true);
                std::cout << "[choices] Fizz (seed " << seed << "), cards-in-hand eval: " << head(r) << table(p, r) << "\n";
                EXPECT_EQ(r.failed, 0);
                EXPECT_EQ(r.replay_rejected, 0);
                EXPECT_EQ(std::accumulate(r.visits.begin(), r.visits.end(), 0), r.simulations);
                // Answer yes: no second Fizz prompt before the game moves on.
                sr = engine.applyChoice(valueIndex(sr.legal, 1));
                int again = 0;
                for (int k = 0; k < 40 && sr.kind == StepKind::NeedDecision; ++k) {
                    again += isFizz(sr.legal);
                    sr = engine.applyChoice(search::settleChoice(sr.legal));
                }
                EXPECT_EQ(again, 0) << "Fizz asked again";
                ++checked;
                break;
            }
            auto w = search::IsMctsSearch::heuristicPrior(engine.state(), sr.legal);
            for (size_t i = 0; i < w.size(); ++i)
                if (sr.legal[i].type == IntentType::Concede) w[i] = 0;
            std::discrete_distribution<int> pick(w.begin(), w.end());
            const int idx = pick(rng);
            history.push_back(idx);
            sr = engine.applyChoice(idx);
        }
    }
    if (checked == 0) GTEST_SKIP() << "no Fizz choice reached";
}

/// What the current nets make of the same decisions (prints; no assertion).
TEST_F(SearchChoicesTest, DiagnoseNetsAtOptionalDraws) {
    if (!deck_) GTEST_SKIP() << "no Kennen deck";
    const std::string vnet = currentNet(".rbvn"), pnet = currentNet(".rbpn");
    if (vnet.empty() && pnet.empty()) GTEST_SKIP() << "no current net";
    auto diagnose = [&](const char* name, const std::vector<Position>& found) {
        for (const auto& p : found) {
            std::cout << "[choices] " << name << " (seed " << p.seed << ", turn " << p.state.turn.turn_number
                      << ", score " << p.state.players[0].score << "-" << p.state.players[1].score << ")";
            if (!vnet.empty()) {
                SearchOptions o = browserOptions();
                o.eval = "net:" + vnet;
                const auto r = run(p, o);
                std::cout << "\n  value net + heuristic prior (the browser bot): " << head(r) << table(p, r);
            }
            if (!pnet.empty()) {
                SearchOptions o = browserOptions();
                o.eval = "policy:" + pnet;
                o.prior = "auto";
                const auto r = run(p, o);
                std::cout << "\n  policy net (value head + prior, as training): " << head(r) << table(p, r);
            }
            std::cout << "\n";
        }
    };
    diagnose("Trifarian Captain", find(kTrifarianCaptain, isYesNo, 6));
    diagnose("Lightning Rush", find(kLightningRush, isRushPick, 4));
}

/// What one more card in hand is worth to the value net: balanced quiet
/// main-phase positions, the acting player's top card moved to its hand.
TEST_F(SearchChoicesTest, DiagnoseNetValueOfACardInHand) {
    if (!deck_) GTEST_SKIP() << "no Kennen deck";
    const std::string vpath = currentNet(".rbvn");
    if (vpath.empty()) GTEST_SKIP() << "no current net";
    const auto vnet = ml::ValueNet::shared(vpath);
    std::vector<double> d_all;
    std::vector<std::vector<double>> d_by_hand(11);
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        EventBus bus;
        GameEngine engine(*db_, bus, *registry_);
        std::mt19937_64 rng(seed * 977 + 1);
        StepResult sr = engine.beginGame(*deck_, *deck_, seed);
        for (int k = 0; k < 1500 && sr.kind == StepKind::NeedDecision; ++k) {
            const GameState& s = engine.state();
            if (balanced(s) && search::isQuietDecision(s) && rng() % 6 == 0) {
                const PlayerId me = search::decidingPlayerOf(s, sr.legal);
                GameState more = s;
                auto& ps = more.player(me);
                if (!ps.main_deck.empty() && ps.hand.size() < 10) {
                    const size_t before = ps.hand.size();
                    const GameObjectId top = ps.main_deck.back();
                    ps.main_deck.pop_back();
                    more.getObject(top).zone = ZoneType::Hand;
                    ps.hand.push_back(top);
                    const double d = vnet->winProbability(more, me, *db_) - vnet->winProbability(s, me, *db_);
                    d_all.push_back(d);
                    d_by_hand[std::min<size_t>(before, 10)].push_back(d);
                }
            }
            auto w = search::IsMctsSearch::heuristicPrior(s, sr.legal);
            for (size_t i = 0; i < w.size(); ++i)
                if (sr.legal[i].type == IntentType::Concede) w[i] = 0;
            std::discrete_distribution<int> pick(w.begin(), w.end());
            sr = engine.applyChoice(pick(rng));
        }
    }
    ASSERT_GT(d_all.size(), 30u);
    auto mean = [](const std::vector<double>& v) { return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size(); };
    const auto neg = std::count_if(d_all.begin(), d_all.end(), [](double d) { return d < 0; });
    std::cout << "[choices] one more card in hand, " << d_all.size() << " balanced positions: value net dP(win) mean "
              << mean(d_all) << ", lower in " << neg << " (" << 100.0 * neg / d_all.size() << "%)\n";
    for (size_t h = 0; h < d_by_hand.size(); ++h)
        if (!d_by_hand[h].empty())
            std::cout << "    hand " << h << " -> " << h + 1 << ": " << d_by_hand[h].size() << " positions, mean dP " << mean(d_by_hand[h]) << "\n";
}

}  // namespace
}  // namespace riftbound

TEST(ValueTemperature, IdentityAtOneAndSpreadsSaturatedValuesAbove) {
    using riftbound::search::temperValue;
    EXPECT_DOUBLE_EQ(temperValue(-0.97, 1.0), -0.97);
    EXPECT_DOUBLE_EQ(temperValue(0.3, 0.0), 0.3) << "a non-positive temperature is off";
    const double a = temperValue(-0.99, 3.0), b = temperValue(-0.95, 3.0);
    EXPECT_GT(b - a, 0.10) << "-0.99 and -0.95 are told apart: " << a << " vs " << b;
    EXPECT_LT(a, b) << "the order is kept";
    EXPECT_NEAR(temperValue(0.0, 3.0), 0.0, 1e-12);
    EXPECT_NEAR(temperValue(0.5, 3.0), -temperValue(-0.5, 3.0), 1e-12) << "symmetric";
    EXPECT_LT(std::abs(temperValue(1.0, 3.0)), 1.0) << "clamped: no infinity";
}
