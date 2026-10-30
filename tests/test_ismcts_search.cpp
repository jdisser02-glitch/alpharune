/// Tests for the engine's own information-set search (agents/ismcts_search.h)
/// and the shared determinizer (engine/determinize.h).

#include "agents/ismcts_search.h"
#include "agents/random_agent.h"
#include "agents/recording_agent.h"
#include "cards/card_registry.h"
#include "core/card_db.h"
#include "core/events.h"
#include "engine/determinize.h"
#include "engine/game_engine.h"
#include "ml/feature_extractor.h"
#include "openspiel/action_vocab.h"
#include "rules/deck_validator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <variant>

namespace riftbound {
namespace {

std::string repoRoot() {
    if (const char* root = std::getenv("RIFTBOUND_ROOT")) return root;
    return "..";
}

bool isPass(const Intent& in) {
    return in.type == IntentType::PassPriority || in.type == IntentType::PassFocus ||
           in.type == IntentType::EndTurn || in.type == IntentType::DeclineOptionalTrigger ||
           in.type == IntentType::DeclineTriggeredCost;
}

/// Index of a passive option in `legal` (pass / end turn), else 0.
int passiveIndex(const std::vector<Intent>& legal) {
    for (size_t i = 0; i < legal.size(); ++i)
        if (isPass(legal[i])) return static_cast<int>(i);
    return 0;
}

class IsMctsSearchTest : public ::testing::Test {
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

    /// Play randomly from the start until `want(state, legal)` holds at a
    /// decision, or the game ends / `max_decisions` pass. Returns false when no
    /// such decision was reached.
    template <typename Pred>
    bool playUntil(GameEngine& engine, uint64_t seed, Pred want, int max_decisions = 400) {
        std::mt19937_64 rng(seed * 31 + 7);
        StepResult sr = engine.beginGame(*deck_, *deck_, seed);
        for (int n = 0; n < max_decisions && sr.kind == StepKind::NeedDecision; ++n) {
            if (want(engine.state(), sr.legal)) return true;
            int idx = static_cast<int>(rng() % sr.legal.size());
            if (sr.legal[idx].type == IntentType::Concede) idx = passiveIndex(sr.legal);
            sr = engine.applyChoice(idx);
        }
        return false;
    }

    static CardRegistry* registry_;
    static CardDB* db_;
    static DeckSubmission* deck_;
};

CardRegistry* IsMctsSearchTest::registry_ = nullptr;
CardDB* IsMctsSearchTest::db_ = nullptr;
DeckSubmission* IsMctsSearchTest::deck_ = nullptr;

std::multiset<CardDefId> idsOf(const GameState& s, const std::vector<GameObjectId>& objs) {
    std::multiset<CardDefId> out;
    for (auto o : objs) out.insert(s.getObject(o).card_def_id);
    return out;
}

// ─── Determinization ────────────────────────────────────────────────────────

TEST_F(IsMctsSearchTest, DeterminizationHidesTheOpponentsHandFromTheObserver) {
    EventBus bus;
    GameEngine engine(*db_, bus, *registry_);
    // A main-phase decision where the opponent holds at least three cards.
    const bool found = playUntil(engine, 5, [](const GameState& s, const std::vector<Intent>& legal) {
        const PlayerId me = search::decidingPlayerOf(s, legal);
        return s.turn.turn_number >= 3 && search::isResumableSnapshot(s) &&
               s.player(opponent(me)).hand.size() >= 3;
    });
    ASSERT_TRUE(found);
    const GameState& real = engine.state();
    const auto legal = engine.currentStep().legal;
    const PlayerId me = search::decidingPlayerOf(real, legal);
    const PlayerId opp = opponent(me);

    search::IsMctsSearch s(*db_, *registry_, *deck_, *deck_, 5, 10);
    const int n = 300;
    auto worlds = s.sampleWorlds(real, me, n, 99);
    ASSERT_EQ(worlds.size(), static_cast<size_t>(n));

    const auto my_view = ml::extractStateFeatures(real, me, *db_);
    const auto& opp_hand = real.player(opp).hand;
    const auto real_hand = idsOf(real, opp_hand);
    // Pool the opponent's hidden cards are drawn from: hand + Main Deck + facedown.
    std::vector<GameObjectId> pool_objs(opp_hand.begin(), opp_hand.end());
    for (auto o : real.player(opp).main_deck) pool_objs.push_back(o);
    for (const auto& bf : real.battlefields)
        for (auto o : bf.facedown) if (real.getObject(o).controller == opp) pool_objs.push_back(o);
    const auto real_pool = idsOf(real, pool_objs);

    int exact = 0, slot_matches = 0;
    std::set<std::multiset<CardDefId>> distinct;
    for (const auto& w : worlds) {
        // Same objects everywhere, only identities change.
        ASSERT_EQ(w.objects.size(), real.objects.size());
        // What the observer can see is unchanged: its own hand, and the whole masked view.
        EXPECT_EQ(idsOf(w, w.player(me).hand), idsOf(real, real.player(me).hand));
        EXPECT_EQ(ml::extractStateFeatures(w, me, *db_), my_view);
        // Conservation: the hidden pool is a permutation of the real one.
        EXPECT_EQ(idsOf(w, pool_objs), real_pool);
        const auto hand = idsOf(w, opp_hand);
        distinct.insert(hand);
        if (hand == real_hand) ++exact;
        for (auto o : opp_hand) slot_matches += w.getObject(o).card_def_id == real.getObject(o).card_def_id;
    }
    // Honesty: the sampled hands vary, and the real hand recurs no more often
    // than chance allows.
    EXPECT_GE(distinct.size(), 10u);
    EXPECT_LT(exact, n / 5) << "the real hand came back in " << exact << " of " << n << " worlds";
    // Per slot, the chance of holding the real card is its share of the pool.
    double expected = 0.0;
    for (auto o : opp_hand) {
        const auto id = real.getObject(o).card_def_id;
        expected += static_cast<double>(real_pool.count(id)) / static_cast<double>(real_pool.size());
    }
    const double observed = slot_matches / static_cast<double>(n);
    EXPECT_NEAR(observed, expected, 0.35 + 0.25 * expected) << "slot matches per world: observed " << observed
                                                            << ", chance " << expected;
}

// ─── Search ─────────────────────────────────────────────────────────────────

TEST_F(IsMctsSearchTest, SearchIsDeterministicForASeedAndExportsShares) {
    EventBus bus;
    GameEngine engine(*db_, bus, *registry_);
    ASSERT_TRUE(playUntil(engine, 11, [](const GameState& s, const std::vector<Intent>& legal) {
        return s.turn.turn_number >= 2 && search::isResumableSnapshot(s) && legal.size() >= 4;
    }));
    const GameState& state = engine.state();
    const auto legal = engine.currentStep().legal;
    const PlayerId me = search::decidingPlayerOf(state, legal);

    SearchOptions opts;
    opts.eval = "board";
    opts.worlds = 3;
    search::IsMctsSearch s(*db_, *registry_, *deck_, *deck_, 11, 24, opts);
    const auto a = s.search(state, legal, me, 1234);
    const auto b = s.search(state, legal, me, 1234);
    EXPECT_EQ(a.visits, b.visits);
    EXPECT_EQ(a.chosen, b.chosen);
    EXPECT_EQ(a.simulations + a.failed, 24);
    EXPECT_EQ(a.failed, 0);
    EXPECT_EQ(a.worlds, 3);
    EXPECT_TRUE(a.resumed);
    ASSERT_GE(a.chosen, 0);
    ASSERT_LT(a.chosen, static_cast<int>(legal.size()));
    double total = 0;
    for (const auto& [i, share] : a.shares) { total += share; EXPECT_GT(a.visits[static_cast<size_t>(i)], 0); }
    EXPECT_NEAR(total, 1.0, 1e-9);
    int sum = 0;
    for (int v : a.visits) sum += v;
    EXPECT_EQ(sum, a.simulations);
    // Visits concentrate on the most visited option, which is the choice.
    EXPECT_EQ(*std::max_element(a.visits.begin(), a.visits.end()), a.visits[static_cast<size_t>(a.chosen)]);
    EXPECT_GE(a.root_value, -1.0);
    EXPECT_LE(a.root_value, 1.0);

    // Root parallelism: the same budget over two threads still sums to the budget.
    opts.threads = 2;
    search::IsMctsSearch s2(*db_, *registry_, *deck_, *deck_, 11, 24, opts);
    const auto c = s2.search(state, legal, me, 1234);
    EXPECT_EQ(c.simulations + c.failed, 24);
    EXPECT_EQ(c.worlds, 6);
}

TEST_F(IsMctsSearchTest, PriorHookSteersTheSearch) {
    EventBus bus;
    GameEngine engine(*db_, bus, *registry_);
    ASSERT_TRUE(playUntil(engine, 21, [](const GameState& s, const std::vector<Intent>& legal) {
        return s.turn.turn_number >= 2 && search::isResumableSnapshot(s) && legal.size() >= 4;
    }));
    const GameState& state = engine.state();
    const auto legal = engine.currentStep().legal;
    const PlayerId me = search::decidingPlayerOf(state, legal);

    // Favour, by prior alone, the last option whose encoded slot is its own
    // (options sharing a slot pool their visits on the first of them), with a
    // large exploration constant; eval=score is flat this early.
    size_t fav = legal.size() - 1;
    for (size_t i = legal.size(); i-- > 0;) {
        const int slot = openspiel::encodeAction(legal[i], state);
        int n = 0;
        for (const auto& in : legal) n += openspiel::encodeAction(in, state) == slot;
        if (n == 1) { fav = i; break; }
    }
    SearchOptions opts;
    opts.uct_c = 10.0;
    opts.worlds = 2;
    search::IsMctsSearch s(*db_, *registry_, *deck_, *deck_, 21, 40, opts);
    s.setPrior([fav](const GameState&, const std::vector<Intent>& l) {
        std::vector<double> w(l.size(), 1e-3);
        if (fav < l.size()) w[fav] = 1.0;
        return w;
    });
    const auto r = s.search(state, legal, me, 7);
    int total = 0;
    for (int v : r.visits) total += v;
    ASSERT_GT(total, 0);
    EXPECT_EQ(r.failed, 0);
    EXPECT_GT(r.visits[fav], 0.7 * total) << "favoured option got " << r.visits[fav] << " of " << total;
    EXPECT_EQ(r.chosen, static_cast<int>(fav));
}

TEST_F(IsMctsSearchTest, FindsTheWinningMove) {
    // A main-phase decision where the acting player has a ready unit at base
    // and may move it to a battlefield nobody occupies. The copy is edited so
    // that conquering it is the winning point (score 7 of 8, every other
    // battlefield already scored this turn: CR 466.1.b).
    GameState pos;
    std::vector<Intent> legal;
    PlayerId me = PlayerId::None;
    std::set<int> winning;
    bool built = false;
    for (uint64_t seed = 31; seed < 60 && !built; ++seed) {
        EventBus bus;
        GameEngine engine(*db_, bus, *registry_);
        std::optional<BattlefieldId> target;
        const bool found = playUntil(engine, seed, [&](const GameState& s, const std::vector<Intent>& l) {
            if (!search::isResumableSnapshot(s) || s.turn.turn_number < 2) return false;
            const PlayerId p = search::decidingPlayerOf(s, l);
            for (const auto& in : l) {
                if (in.type != IntentType::StandardMove || !in.move_destination.has_value()) continue;
                const auto* bf = std::get_if<BattlefieldLocation>(&*in.move_destination);
                if (!bf) continue;
                if (!s.unitsAt(BattlefieldLocation{bf->id}, PlayerId::Player1).empty() ||
                    !s.unitsAt(BattlefieldLocation{bf->id}, PlayerId::Player2).empty()) continue;
                bool mine = false;
                for (const auto& bfs : s.battlefields)
                    if (bfs.id == bf->id && bfs.controller.has_value() && *bfs.controller == p) mine = true;
                if (mine) continue;
                target = bf->id;
                return true;
            }
            return false;
        });
        if (!found) continue;
        me = search::decidingPlayerOf(engine.state(), engine.currentStep().legal);
        GameState copy = engine.state();
        // The opponent cannot answer: their hand goes to the bottom of their deck.
        auto& opp_ps = copy.player(opponent(me));
        for (auto oid : opp_ps.hand) {
            copy.getObject(oid).zone = ZoneType::MainDeck;
            opp_ps.main_deck.push_back(oid);
        }
        opp_ps.hand.clear();
        copy.player(me).score = copy.mode.victory_score - 1;
        copy.player(me).battlefields_scored_this_turn.clear();
        for (const auto& bf : copy.battlefields)
            if (bf.id != *target) copy.player(me).battlefields_scored_this_turn.insert(bf.id);
        // The legal list of the edited position, from a fresh engine.
        EventBus bus2;
        GameEngine probe(*db_, bus2, *registry_);
        StepResult sr = probe.resumeFromSnapshot(GameState(copy), seed);
        if (sr.kind != StepKind::NeedDecision) continue;
        legal = sr.legal;
        // Which options win on the spot when everyone else just passes?
        winning.clear();
        for (size_t i = 0; i < legal.size(); ++i) {
            EventBus bus3;
            GameEngine sim(*db_, bus3, *registry_);
            StepResult t = sim.resumeFromSnapshot(GameState(copy), seed);
            t = sim.applyChoice(static_cast<int>(i));
            for (int n = 0; n < 12 && t.kind == StepKind::NeedDecision; ++n)
                t = sim.applyChoice(passiveIndex(t.legal));
            if (sim.isStepDone() && sim.stepResult().winner == me) winning.insert(static_cast<int>(i));
        }
        if (winning.empty() || winning.size() == legal.size()) continue;
        pos = std::move(copy);
        built = true;
    }
    ASSERT_TRUE(built) << "no suitable position found";
    ASSERT_GE(legal.size(), 2u);

    SearchOptions opts;
    opts.eval = "score";
    opts.worlds = 4;
    opts.threads = 2;
    opts.uct_c = 1.0;
    search::IsMctsSearch s(*db_, *registry_, *deck_, *deck_, 1, 400, opts);
    const auto r = s.search(pos, legal, me, 2024);
    ASSERT_GE(r.chosen, 0);
    std::string table;
    for (size_t i = 0; i < legal.size(); ++i)
        table += "\n  [" + std::to_string(i) + "] " + legal[i].describe() + " visits " + std::to_string(r.visits[i]) +
                 (winning.count(static_cast<int>(i)) ? " (wins)" : "");
    EXPECT_EQ(r.failed, 0) << table;
    EXPECT_TRUE(winning.count(r.chosen)) << "chose " << legal[static_cast<size_t>(r.chosen)].describe()
                                         << " (visits " << r.visits[static_cast<size_t>(r.chosen)] << " of "
                                         << r.simulations << ", root value " << r.root_value << ")" << table;
    EXPECT_GT(r.root_value, 0.5);
}

TEST_F(IsMctsSearchTest, PuctAgentPlaysAWholeGame) {
    EventBus bus;
    GameEngine engine(*db_, bus, *registry_);
    SearchOptions opts;
    opts.eval = "board";
    opts.worlds = 2;
    search::PuctAgent bot(*db_, *registry_, *deck_, *deck_, 3, 42, 4, opts);
    RandomAgent rnd(99);
    std::vector<int32_t> history;
    bot.setDecisionHistory(&history);
    RecordingAgent seat1(bot, history), seat2(rnd, history);
    int searched = 0, inflight = 0, rejected = 0;
    engine.on_decision = [&](const GameState& s, const std::vector<Intent>& legal, const Intent& chosen) {
        if (chosen.player != PlayerId::Player1 || legal.size() < 2) return;
        if (!bot.lastSearch().empty()) {
            ++searched;
            if (!bot.lastResult().resumed) ++inflight;
            rejected += bot.lastResult().replay_rejected;
            double total = 0;
            for (const auto& [i, share] : bot.lastSearch()) {
                EXPECT_GE(i, 0);
                EXPECT_LT(i, static_cast<int>(legal.size()));
                total += share;
            }
            EXPECT_NEAR(total, 1.0, 1e-9);
        }
        (void)s;
    };
    const GameResult res = engine.runGame(*deck_, *deck_, seat1, seat2, 3);
    EXPECT_TRUE(res.winner == PlayerId::Player1 || res.winner == PlayerId::Player2 ||
                res.winner == PlayerId::None);
    EXPECT_GT(res.total_decisions, 0);
    EXPECT_GT(searched, 0);
    EXPECT_GT(bot.totalSimulations(), 0);
    // In-flight decisions (chain, showdown, combat) are searched through the
    // exact replay of the recorded choices, and the rebuilt worlds pass the
    // acting player's-view check.
    EXPECT_GT(inflight, 0);
    EXPECT_EQ(rejected, 0);
}

// ─── Settling leaves (SearchOptions::settle) ────────────────────────────────

TEST(SearchOptionsTest, SpecParserReadsSettle) {
    SearchOptions opts;
    EXPECT_EQ(opts.settle, 0);   // off unless asked for
    int sims = 0;
    parseSearchSpecOptions("sims=50,eval=policy:lab/learn/nets/net_034.rbpn,worlds=8,settle=60", opts, &sims);
    EXPECT_EQ(sims, 50);
    EXPECT_EQ(opts.settle, 60);
    EXPECT_EQ(opts.worlds, 8);
    EXPECT_EQ(opts.eval, "policy:lab/learn/nets/net_034.rbpn");

    // Order does not matter, unknown keys and bare words are skipped, and
    // settle=0 turns it back off.
    SearchOptions o2;
    parseSearchSpecOptions("settle=12,bogus=3,det=0,root=gumbel,gumbel_m=4,noeq", o2);
    EXPECT_EQ(o2.settle, 12);
    EXPECT_FALSE(o2.determinize);
    EXPECT_EQ(o2.root, "gumbel");
    EXPECT_EQ(o2.gumbel_m, 4);
    EXPECT_TRUE(applySearchOption(o2, "settle", "0"));
    EXPECT_EQ(o2.settle, 0);
    EXPECT_FALSE(applySearchOption(o2, "sims", "10"));
}

TEST(SearchOptionsTest, SettlePolicyPassesElseTakesTheFirstOption) {
    Intent play;
    play.type = IntentType::PlayCard;
    Intent pass_focus;
    pass_focus.type = IntentType::PassFocus;
    Intent pass_priority;
    pass_priority.type = IntentType::PassPriority;
    Intent choice;
    choice.type = IntentType::MakeChoice;
    EXPECT_EQ(search::settleChoice({play, pass_focus}), 1);
    EXPECT_EQ(search::settleChoice({play, play, pass_priority}), 2);
    EXPECT_EQ(search::settleChoice({choice, play}), 0);
}

TEST_F(IsMctsSearchTest, QuietDecisionsAreMainPhaseActionsWithNothingInFlight) {
    // Random games: every decision's site is stamped for that decision, a
    // quiet main-phase decision is a clean boundary (no chain, showdown or
    // combat), every in-flight site is not quiet, and the settle policy takes
    // a resumable in-flight decision to a quiet one in a few steps.
    int quiet = 0, loud = 0, settled = 0, max_steps = 0, total_steps = 0, mainphase_loud = 0;
    std::map<int, int> loud_by_kind;
    for (uint64_t seed : {3u, 4u, 5u}) {
        EventBus bus;
        GameEngine engine(*db_, bus, *registry_);
        std::mt19937_64 rng(seed * 131 + 1);
        StepResult sr = engine.beginGame(*deck_, *deck_, seed);
        for (int n = 0; n < 700 && sr.kind == StepKind::NeedDecision; ++n) {
            const GameState& s = engine.state();
            const auto kind = s.decision_site.kind;
            ASSERT_EQ(s.decision_site.decision_index, s.decision_index)
                << "decision " << s.decision_index << " has a stale site (kind " << static_cast<int>(kind) << ")";
            const bool q = search::isQuietDecision(s);
            if (q) {
                ++quiet;
                if (kind == DecisionSiteKind::MainPhase) EXPECT_TRUE(search::isCleanMainPhaseSnapshot(s));
            } else {
                ++loud;
                ++loud_by_kind[static_cast<int>(kind)];
                if (kind == DecisionSiteKind::MainPhase) ++mainphase_loud;
                EXPECT_NE(kind, DecisionSiteKind::None);
                EXPECT_NE(kind, DecisionSiteKind::Mulligan);
            }
            if (kind == DecisionSiteKind::ChainPriority || kind == DecisionSiteKind::ChainChoice ||
                kind == DecisionSiteKind::ShowdownFocus || kind == DecisionSiteKind::CombatDamage ||
                kind == DecisionSiteKind::Other)
                EXPECT_FALSE(q);
            if (!q && search::isResumableSnapshot(s)) {
                EventBus bus2;
                GameEngine copy(*db_, bus2, *registry_);
                StepResult t = copy.resumeFromSnapshot(GameState(s), seed + 1000);
                int steps = 0;
                while (t.kind == StepKind::NeedDecision && !search::isQuietDecision(copy.state()) && steps < 300) {
                    t = copy.applyChoice(search::settleChoice(t.legal));
                    ++steps;
                }
                EXPECT_TRUE(t.kind != StepKind::NeedDecision || search::isQuietDecision(copy.state()))
                    << "settling decision " << s.decision_index << " did not turn quiet in 300 steps";
                ++settled;
                max_steps = std::max(max_steps, steps);
                total_steps += steps;
            }
            int idx = static_cast<int>(rng() % sr.legal.size());
            if (sr.legal[static_cast<size_t>(idx)].type == IntentType::Concede) idx = passiveIndex(sr.legal);
            sr = engine.applyChoice(idx);
        }
    }
    EXPECT_GT(quiet, 0);
    EXPECT_GT(loud, 0);
    EXPECT_GT(settled, 0);
    std::string kinds;
    for (const auto& [k, c] : loud_by_kind) kinds += " kind" + std::to_string(k) + "=" + std::to_string(c);
    std::cout << "[settle] quiet " << quiet << ", not quiet " << loud << " (" << kinds
              << "; main-phase sites with a chain open " << mainphase_loud << "), settled " << settled
              << " in-flight snapshots in " << (settled ? total_steps / static_cast<double>(settled) : 0.0)
              << " steps on average, at most " << max_steps << "\n";
}

TEST_F(IsMctsSearchTest, SettleSeesThatASuicideAttackLosesTheUnits) {
    // The acting player's only ready units are two 1-Might units at base; every
    // battlefield is held by the opponent with two 10-Might units. Moving in
    // starts a combat whose damage assignments are real decisions, so without
    // settling the first evaluation of a move lands mid-combat with the units
    // still alive and the move looks free. The leaf evaluation counts units on
    // the board (and score), so with settling the attack shows its cost and
    // End turn (the opponent then scores its holds) is preferred.
    auto onBoard = [](const GameState& s, GameObjectId id) {
        if (!s.objectExists(id)) return false;
        const auto& o = s.getObject(id);
        return o.location.has_value() && (o.zone == ZoneType::Base || o.zone == ZoneType::BattlefieldZone);
    };
    // The acting player's units on the board and the score difference: a unit
    // is worth more than the two points the opponent's holds bring it. (The
    // opponent's units are left out so that what it deploys on its own turn
    // after End turn does not blur the comparison.)
    auto material = [onBoard](const GameState& s, PlayerId me) {
        int units = 0;
        for (const auto& [id, o] : s.objects)
            if (o.isUnit() && o.controller == me && onBoard(s, id)) units += 1;
        const int score = s.player(me).score - s.player(opponent(me)).score;
        return 0.08 * score + 0.25 * units;
    };

    GameState pos;
    std::vector<Intent> legal;
    PlayerId me = PlayerId::None;
    std::vector<int> suicide;
    int end_turn = -1;
    uint64_t pos_seed = 0;
    for (uint64_t seed = 61; seed < 120 && pos_seed == 0; ++seed) {
        EventBus bus;
        GameEngine engine(*db_, bus, *registry_);
        if (!playUntil(engine, seed, [](const GameState& s, const std::vector<Intent>&) {
                return s.turn.turn_number >= 3 && s.decision_site.kind == DecisionSiteKind::MainPhase &&
                       search::isQuietDecision(s) && search::isResumableSnapshot(s);
            }))
            continue;
        me = search::decidingPlayerOf(engine.state(), engine.currentStep().legal);
        const PlayerId opp = opponent(me);
        GameState copy = engine.state();
        // Nobody has anything to play: hands go to the decks.
        for (PlayerId p : {me, opp}) {
            auto& ps = copy.player(p);
            for (auto oid : ps.hand) {
                copy.getObject(oid).zone = ZoneType::MainDeck;
                copy.getObject(oid).location.reset();
                ps.main_deck.push_back(oid);
            }
            ps.hand.clear();
        }
        auto takeUnits = [&](PlayerId p, size_t n) {
            std::vector<GameObjectId> out;
            auto& deck = copy.player(p).main_deck;
            for (auto it = deck.begin(); it != deck.end() && out.size() < n;) {
                if (db_->get(copy.getObject(*it).card_def_id).card_type == CardType::Unit) {
                    out.push_back(*it);
                    it = deck.erase(it);
                } else {
                    ++it;
                }
            }
            return out;
        };
        const auto movers = takeUnits(me, 2);
        const auto guards = takeUnits(opp, 2 * copy.battlefields.size());
        if (movers.size() < 2 || guards.size() < 2 * copy.battlefields.size()) continue;
        // Ending the turn must not hand the opponent the game through its holds.
        if (copy.player(opp).score + static_cast<int>(copy.battlefields.size()) >= copy.mode.victory_score) continue;
        // Everything the acting player has on the board is exhausted and off
        // the battlefields; the two movers are ready at base with 1 Might.
        for (auto& [id, o] : copy.objects) {
            if (o.controller != me) continue;
            if (o.zone == ZoneType::BattlefieldZone && o.isUnit()) {
                o.zone = ZoneType::Base;
                o.location = BaseLocation{me};
            }
            if (o.zone == ZoneType::Base || o.zone == ZoneType::BattlefieldZone || o.zone == ZoneType::LegendZone)
                o.is_exhausted = true;
        }
        auto place = [&](GameObjectId id, std::optional<BattlefieldId> bf, int might) {
            auto& o = copy.getObject(id);
            if (bf) {
                o.zone = ZoneType::BattlefieldZone;
                o.location = BattlefieldLocation{*bf};
            } else {
                o.zone = ZoneType::Base;
                o.location = BaseLocation{o.owner};
            }
            o.controller = o.owner;
            o.is_exhausted = false;
            o.damage_marked = 0;
            o.base_might = might;
            o.current_might = might;
        };
        for (auto id : movers) place(id, std::nullopt, 1);
        for (size_t b = 0; b < copy.battlefields.size(); ++b) {
            auto& bf = copy.battlefields[b];
            place(guards[2 * b], bf.id, 10);
            place(guards[2 * b + 1], bf.id, 10);
            bf.controller = opp;
            bf.is_contested = false;
            bf.contested_by = PlayerId::None;
        }
        if (!search::isResumableSnapshot(copy)) continue;
        EventBus bus2;
        GameEngine probe(*db_, bus2, *registry_);
        StepResult sr = probe.resumeFromSnapshot(GameState(copy), seed);
        if (sr.kind != StepKind::NeedDecision) continue;
        legal = sr.legal;
        suicide.clear();
        end_turn = -1;
        for (size_t i = 0; i < legal.size(); ++i) {
            if (legal[i].type == IntentType::EndTurn) end_turn = static_cast<int>(i);
            if (legal[i].type == IntentType::StandardMove && legal[i].move_destination.has_value() &&
                std::holds_alternative<BattlefieldLocation>(*legal[i].move_destination))
                suicide.push_back(static_cast<int>(i));
        }
        if (end_turn < 0 || suicide.empty()) continue;
        // Every move into a battlefield loses the units that move, and the
        // combat asks at least one real decision before it is over.
        bool fits = true;
        for (int i : suicide) {
            EventBus bus3;
            GameEngine sim(*db_, bus3, *registry_);
            StepResult t = sim.resumeFromSnapshot(GameState(copy), seed);
            t = sim.applyChoice(i);
            int multi = 0, steps = 0;
            while (t.kind == StepKind::NeedDecision && !search::isQuietDecision(sim.state()) && steps < 100) {
                if (t.legal.size() > 1) ++multi;
                t = sim.applyChoice(search::settleChoice(t.legal));
                ++steps;
            }
            bool lost = t.kind == StepKind::NeedDecision && search::isQuietDecision(sim.state());
            for (auto u : legal[static_cast<size_t>(i)].units_to_move) lost = lost && !onBoard(sim.state(), u);
            if (!lost || multi == 0) { fits = false; break; }
        }
        if (!fits) continue;
        pos = std::move(copy);
        pos_seed = seed;
    }
    ASSERT_NE(pos_seed, 0u) << "no suitable position found";

    // Measured from the position searched, so nothing saturates.
    const double base = material(pos, me);
    auto run = [&](int settle) {
        SearchOptions opts;
        opts.worlds = 4;
        opts.uct_c = 0.5;
        opts.settle = settle;
        search::IsMctsSearch s(*db_, *registry_, *deck_, *deck_, pos_seed, 80, opts);
        s.setEvaluator([&](const GameState& st, PlayerId p) { return material(st, p) - base; });
        return s.search(pos, legal, me, 2026);
    };
    const auto off = run(0);
    const auto on = run(60);
    auto share = [&](const search::SearchResult& r) {
        int total = 0, moves = 0;
        for (size_t i = 0; i < r.visits.size(); ++i) total += r.visits[i];
        for (int i : suicide) moves += r.visits[static_cast<size_t>(i)];
        return total > 0 ? moves / static_cast<double>(total) : 0.0;
    };
    std::string table;
    for (size_t i = 0; i < legal.size(); ++i) {
        const GameObjectId src = legal[i].ability_source != kInvalidId ? legal[i].ability_source : legal[i].card;
        table += "\n  [" + std::to_string(i) + "] " + legal[i].describe() +
                 (src != kInvalidId && pos.objectExists(src) ? " (" + pos.getObject(src).name + ")" : std::string()) +
                 "  off " + std::to_string(off.visits[i]) + "  on " + std::to_string(on.visits[i]);
    }
    EXPECT_EQ(off.failed, 0) << table;
    EXPECT_EQ(on.failed, 0) << table;
    EXPECT_EQ(off.settled, 0);
    EXPECT_GT(on.settled, 0);
    EXPECT_EQ(on.settle_capped, 0);
    // Settling moves the visits away from the attacks...
    EXPECT_LT(share(on), share(off)) << table;
    // ... End turn now beats every attack, and no attack is chosen.
    for (int i : suicide)
        EXPECT_GT(on.visits[static_cast<size_t>(end_turn)], on.visits[static_cast<size_t>(i)]) << table;
    EXPECT_EQ(std::count(suicide.begin(), suicide.end(), on.chosen), 0) << table;
    std::cout << "[settle] suicide attacks: " << 100.0 * share(off) << "% of visits without settling, "
              << 100.0 * share(on) << "% with (" << on.settled << " leaves settled, "
              << on.settle_steps / std::max(1.0, static_cast<double>(on.settled)) << " steps each)" << table << "\n";
}

}  // namespace
}  // namespace riftbound
