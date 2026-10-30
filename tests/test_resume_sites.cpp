/// Resuming a fresh engine at any decision (GameEngine::resumeFromSnapshot
/// driven by GameState::decision_site). For every site the engine records,
/// a decision reached by random play is snapshotted, resumed in a fresh
/// engine, and checked to be the same decision: the same options, the same
/// state digest, and the same states after the same choices.

#include "agents/ismcts_search.h"
#include "cards/card_registry.h"
#include "core/card_db.h"
#include "core/events.h"
#include "engine/determinize.h"
#include "engine/game_engine.h"
#include "ml/feature_extractor.h"
#include "rules/deck_validator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <variant>
#include <vector>

namespace riftbound {
namespace {

std::string repoRoot() {
    if (const char* root = std::getenv("RIFTBOUND_ROOT")) return root;
    return "..";
}

const char* kindName(DecisionSiteKind k) {
    switch (k) {
        case DecisionSiteKind::None: return "None";
        case DecisionSiteKind::MainPhase: return "MainPhase";
        case DecisionSiteKind::Mulligan: return "Mulligan";
        case DecisionSiteKind::ChainPriority: return "ChainPriority";
        case DecisionSiteKind::ChainChoice: return "ChainChoice";
        case DecisionSiteKind::ShowdownFocus: return "ShowdownFocus";
        case DecisionSiteKind::CombatDamage: return "CombatDamage";
        case DecisionSiteKind::Other: return "Other";
    }
    return "?";
}

/// A digest of everything in the state that play can change.
uint64_t digest(const GameState& s) {
    uint64_t h = 0xCBF29CE484222325ULL;
    auto mix = [&](uint64_t x) { h ^= x; h *= 0x100000001B3ULL; };
    auto mixi = [&](auto x) { mix(static_cast<uint64_t>(static_cast<int64_t>(x))); };
    auto mixv = [&](const auto& v) { mix(v.size()); for (const auto& x : v) mixi(x); };
    auto mixs = [&](const std::string& str) { mix(str.size()); for (unsigned char c : str) mix(c); };
    auto mixloc = [&](const std::optional<LocationId>& loc) {
        if (!loc.has_value()) { mix(7); return; }
        if (const auto* bf = std::get_if<BattlefieldLocation>(&*loc)) { mix(1000); mixi(bf->id); }
        else { mix(2000); mixi(std::get<BaseLocation>(*loc).player); }
    };
    std::vector<GameObjectId> ids;
    for (const auto& [id, o] : s.objects) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (auto id : ids) {
        const auto& o = s.getObject(id);
        mixi(id); mixi(o.card_def_id); mixi(o.owner); mixi(o.controller); mixs(o.name);
        mixi(o.zone); mixloc(o.location); mixi(o.is_exhausted); mixi(o.is_stunned); mixi(o.is_hidden);
        mixi(o.hidden_at); mixi(o.base_might); mixi(o.current_might); mixi(o.damage_marked);
        mixi(o.empowered); mixi(o.buff_count); mixi(o.temp_buff_count); mixi(o.combat_designation);
        mixi(o.attached_to.value_or(kInvalidId)); mixv(o.attachments); mixi(o.is_token);
        mixi(o.prevent_damage_remaining); mixi(o.damage_taken_this_turn); mixi(o.moves_this_turn);
        std::map<std::string, int> counters(o.card_counters.begin(), o.card_counters.end());
        for (const auto& [k, v] : counters) { mixs(k); mixi(v); }
    }
    for (auto pid : {PlayerId::Player1, PlayerId::Player2}) {
        const auto& p = s.player(pid);
        mixi(p.score); mixi(p.xp); mixi(p.turns_taken); mixi(p.rune_pool.energy);
        mixi(p.rune_pool.universal_power);
        for (int d = 0; d < static_cast<int>(Domain::Count); ++d) mixi(p.rune_pool.power[d]);
        mixv(p.hand); mixv(p.main_deck); mixv(p.rune_deck); mixv(p.trash); mixv(p.banishment);
        mixi(p.legend_zone); mixi(p.cards_played_this_turn);
    }
    for (const auto& bf : s.battlefields) {
        mixi(bf.id); mixi(bf.controller.value_or(PlayerId::None)); mixi(bf.is_contested); mixi(bf.contested_by);
        mixi(bf.showdown_staged); mixi(bf.combat_staged); mixi(bf.showdown_in_progress); mixi(bf.combat_in_progress);
        mixi(bf.attacker.value_or(PlayerId::None)); mixi(bf.defender.value_or(PlayerId::None)); mixi(bf.combat_phase);
        mixv(bf.facedown);
    }
    const auto& t = s.turn;
    mixi(t.turn_player); mixi(t.phase); mixi(t.ns_state); mixi(t.oc_state);
    mixi(t.priority_holder.value_or(PlayerId::None)); mixi(t.focus_holder.value_or(PlayerId::None));
    mixi(t.turn_number); mixi(t.turn_decisions_p1); mixi(t.turn_decisions_p2);
    for (auto p : t.players_passed_priority) mixi(p);
    for (auto p : t.players_passed_focus) mixi(p);
    auto mixitem = [&](const ChainItem& it) {
        mixi(it.id); mixi(it.status); mixi(it.source); mixi(it.controller); mixi(it.resume_point);
        mixv(it.resume_data); mixv(it.targets); mixv(it.chosen_objects); mixi(it.may_answer);
    };
    mix(s.chain.items.size());
    for (const auto& it : s.chain.items) mixitem(it);
    mixi(s.chain.resuming.has_value());
    if (s.chain.resuming.has_value()) mixitem(*s.chain.resuming);
    mix(s.delayed_abilities.size());
    mixi(s.decision_index); mixi(s.game_over); mixi(s.winner); mixi(s.cost_cursor.has_value());
    mixv(s.action_history);
    const auto& d = s.decision_site;
    mixi(d.kind); mixi(d.decision_index); mixi(d.mulligan_index); mixi(d.scoring_bf_index);
    mixi(d.main_stage); mixi(d.main_action_count); mixi(d.main_bf_index);
    mixi(d.showdown_bf); mixi(d.showdown_focus); mixi(d.showdown_action_count); mixi(d.showdown_in_intent);
    mixi(d.combat_bf); mixi(d.combat_stage); mixi(d.combat_showdown_already_closed);
    mixi(d.chain_iterations); mixi(d.chain_passes); mixi(d.chain_choice_iter);
    return h;
}

std::string describeAll(const std::vector<Intent>& legal) {
    std::string out;
    for (size_t i = 0; i < legal.size(); ++i) out += "\n  [" + std::to_string(i) + "] " + legal[i].describe();
    return out;
}

bool isPassive(const Intent& in) {
    return in.type == IntentType::PassPriority || in.type == IntentType::PassFocus ||
           in.type == IntentType::EndTurn || in.type == IntentType::Concede;
}

/// A random choice among `legal` that is not a concession.
int randomChoice(std::mt19937_64& rng, const std::vector<Intent>& legal) {
    int idx = static_cast<int>(rng() % legal.size());
    if (legal[static_cast<size_t>(idx)].type == IntentType::Concede) {
        for (size_t i = 0; i < legal.size(); ++i)
            if (legal[i].type != IntentType::Concede) return static_cast<int>(i);
    }
    return idx;
}

bool stamped(const GameState& s) {
    return s.decision_site.kind != DecisionSiteKind::None &&
           s.decision_site.decision_index == s.decision_index;
}

class ResumeSitesTest : public ::testing::Test {
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

    /// Play randomly from the start of game `seed` until a decision of `kind`
    /// with at least `min_options` options. Returns false if none came up.
    bool playUntil(GameEngine& engine, uint64_t seed, DecisionSiteKind kind, size_t min_options,
                   int max_decisions = 700) {
        std::mt19937_64 rng(seed * 31 + 7);
        StepResult sr = engine.beginGame(*deck_, *deck_, seed);
        for (int n = 0; n < max_decisions && sr.kind == StepKind::NeedDecision; ++n) {
            const auto& s = engine.state();
            if (s.decision_site.kind == kind && stamped(s) && sr.legal.size() >= min_options) return true;
            sr = engine.applyChoice(randomChoice(rng, sr.legal));
        }
        return false;
    }

    /// Resume `snapshot` in a fresh engine: the same decision must be asked.
    /// Then both engines, reseeded alike, play the same choices for a while and
    /// must stay identical. `engine` is the original, paused at the decision.
    void checkResume(GameEngine& engine, DecisionSiteKind kind, uint64_t seed) {
        const GameState snapshot = engine.state();
        const std::vector<Intent> legal = engine.currentStep().legal;
        ASSERT_TRUE(search::isResumableSnapshot(snapshot)) << kindName(kind);

        EventBus bus;
        GameEngine fresh(*db_, bus, *registry_);
        StepResult sr = fresh.resumeFromSnapshot(GameState(snapshot), 777);
        ASSERT_EQ(sr.kind, StepKind::NeedDecision) << kindName(kind);
        EXPECT_EQ(fresh.state().decision_index, snapshot.decision_index) << kindName(kind);
        EXPECT_TRUE(sr.legal == legal) << kindName(kind) << "\noriginal:" << describeAll(legal)
                                       << "\nresumed:" << describeAll(sr.legal);
        EXPECT_EQ(digest(fresh.state()), digest(snapshot)) << kindName(kind) << ": state differs after resume";

        // The same choices from the same RNG lead to the same states.
        engine.reseedRng(seed * 977 + 1);
        fresh.reseedRng(seed * 977 + 1);
        std::mt19937_64 rng(seed * 13 + 5);
        StepResult a = engine.currentStep();
        StepResult b = sr;
        for (int step = 0; step < 40 && a.kind == StepKind::NeedDecision; ++step) {
            ASSERT_TRUE(a.legal == b.legal) << kindName(kind) << " step " << step
                                            << "\noriginal:" << describeAll(a.legal)
                                            << "\nresumed:" << describeAll(b.legal);
            // The first choice is never a pass when something else is offered.
            int idx = randomChoice(rng, a.legal);
            if (step == 0 && isPassive(a.legal[static_cast<size_t>(idx)]))
                for (size_t i = 0; i < a.legal.size(); ++i)
                    if (!isPassive(a.legal[i])) { idx = static_cast<int>(i); break; }
            a = engine.applyChoice(idx);
            b = fresh.applyChoice(idx);
            ASSERT_EQ(a.kind, b.kind) << kindName(kind) << " step " << step;
            ASSERT_EQ(digest(engine.state()), digest(fresh.state()))
                << kindName(kind) << ": states differ " << (step + 1) << " choices after the resume";
        }
        if (a.kind == StepKind::Done) {
            EXPECT_EQ(engine.stepResult().winner, fresh.stepResult().winner);
        }
    }

    /// Find a decision of `kind` over a range of seeds and check its resume.
    void checkKind(DecisionSiteKind kind, size_t min_options, int want = 3) {
        int found = 0;
        for (uint64_t seed = 1; seed <= 80 && found < want; ++seed) {
            EventBus bus;
            GameEngine engine(*db_, bus, *registry_);
            if (!playUntil(engine, seed, kind, min_options)) continue;
            ++found;
            checkResume(engine, kind, seed);
            if (HasFatalFailure()) return;
        }
        EXPECT_GE(found, 1) << "no " << kindName(kind) << " decision with >= " << min_options
                            << " options in 80 random games";
    }

    static CardRegistry* registry_;
    static CardDB* db_;
    static DeckSubmission* deck_;
};

CardRegistry* ResumeSitesTest::registry_ = nullptr;
CardDB* ResumeSitesTest::db_ = nullptr;
DeckSubmission* ResumeSitesTest::deck_ = nullptr;

TEST_F(ResumeSitesTest, MainPhaseAction) { checkKind(DecisionSiteKind::MainPhase, 2); }
TEST_F(ResumeSitesTest, Mulligan) { checkKind(DecisionSiteKind::Mulligan, 2); }
TEST_F(ResumeSitesTest, ChainPriority) { checkKind(DecisionSiteKind::ChainPriority, 2); }
TEST_F(ResumeSitesTest, ChainChoice) { checkKind(DecisionSiteKind::ChainChoice, 2); }
TEST_F(ResumeSitesTest, ShowdownFocus) { checkKind(DecisionSiteKind::ShowdownFocus, 2); }
TEST_F(ResumeSitesTest, CombatDamage) { checkKind(DecisionSiteKind::CombatDamage, 2); }

// Every decision of a game records its site; every site but "Other" resumes to
// the same decision. (The whole game, not only decisions with several options.)
TEST_F(ResumeSitesTest, EveryDecisionOfAGameIsRecordedAndResumes) {
    std::map<std::string, int> seen;
    int checked = 0;
    for (uint64_t seed = 101; seed <= 103; ++seed) {
        EventBus bus;
        GameEngine engine(*db_, bus, *registry_);
        std::mt19937_64 rng(seed);
        StepResult sr = engine.beginGame(*deck_, *deck_, seed);
        for (int n = 0; n < 1500 && sr.kind == StepKind::NeedDecision; ++n) {
            const GameState& s = engine.state();
            ASSERT_TRUE(stamped(s)) << "decision " << s.decision_index << " has no site record";
            const auto kind = s.decision_site.kind;
            ++seen[kindName(kind)];
            if (kind == DecisionSiteKind::Other) {
                EXPECT_FALSE(search::isResumableSnapshot(s));
            } else {
                std::string why;
                for (const auto& bf : s.battlefields)
                    why += " bf" + std::to_string(bf.id) + "(" + (bf.showdown_staged ? "S" : "") +
                           (bf.combat_staged ? "C" : "") + (bf.showdown_in_progress ? "s" : "") +
                           (bf.combat_in_progress ? "c" : "") + ")";
                ASSERT_TRUE(search::isResumableSnapshot(s))
                    << kindName(kind) << " at decision " << s.decision_index << " is not resumable: phase "
                    << toString(s.turn.phase) << " oc " << static_cast<int>(s.turn.oc_state) << " ns "
                    << static_cast<int>(s.turn.ns_state) << " chain " << s.chain.items.size()
                    << " resuming " << s.chain.resuming.has_value() << " cost " << s.cost_cursor.has_value() << why;
                EventBus bus2;
                GameEngine fresh(*db_, bus2, *registry_);
                StepResult r = fresh.resumeFromSnapshot(GameState(s), 5);
                ASSERT_EQ(r.kind, StepKind::NeedDecision) << kindName(kind) << " at decision " << s.decision_index;
                EXPECT_TRUE(r.legal == sr.legal) << kindName(kind) << " at decision " << s.decision_index
                                                 << "\noriginal:" << describeAll(sr.legal)
                                                 << "\nresumed:" << describeAll(r.legal);
                EXPECT_EQ(digest(fresh.state()), digest(s)) << kindName(kind) << " at decision " << s.decision_index;
                ++checked;
            }
            sr = engine.applyChoice(randomChoice(rng, sr.legal));
        }
    }
    std::string table;
    for (const auto& [k, n] : seen) table += k + "=" + std::to_string(n) + " ";
    EXPECT_GT(checked, 100) << table;
    EXPECT_GT(seen["ChainPriority"], 0) << table;
    EXPECT_GT(seen["ShowdownFocus"], 0) << table;
    EXPECT_GT(seen["CombatDamage"], 0) << table;
}

// The searcher resumes an in-flight decision in place, and every resumed
// world passes its check against the real decision.
TEST_F(ResumeSitesTest, SearchResumesInFlightDecisions) {
    int searched = 0;
    for (auto kind : {DecisionSiteKind::ChainPriority, DecisionSiteKind::ShowdownFocus,
                      DecisionSiteKind::CombatDamage, DecisionSiteKind::ChainChoice}) {
        for (uint64_t seed = 7; seed <= 60; ++seed) {
            EventBus bus;
            GameEngine engine(*db_, bus, *registry_);
            if (!playUntil(engine, seed, kind, 2)) continue;
            const GameState& s = engine.state();
            const auto legal = engine.currentStep().legal;
            const PlayerId me = search::decidingPlayerOf(s, legal);
            SearchOptions opts;
            opts.eval = "board";
            opts.worlds = 4;
            opts.threads = 1;
            search::IsMctsSearch search(*db_, *registry_, *deck_, *deck_, seed, 24, opts);
            const auto r = search.search(s, legal, me, 99);
            EXPECT_TRUE(r.resumed) << kindName(kind);
            EXPECT_TRUE(r.resumed_inflight) << kindName(kind);
            EXPECT_EQ(r.replay_rejected, 0) << kindName(kind);
            EXPECT_EQ(r.failed, 0) << kindName(kind);
            EXPECT_EQ(r.simulations, 24) << kindName(kind);
            ++searched;
            break;
        }
    }
    EXPECT_GE(searched, 3);
}

}  // namespace
}  // namespace riftbound

namespace riftbound {
namespace {

// At every in-flight decision of random games (with the lab benchmark's two
// decks when present), every sampled determinization resumes to the real
// decision: same step, same options, same masked view for the acting player.
// This is the check the searcher applies to each resumed world; a failure here
// means rejected worlds (or a determinizer that re-deals something visible).
TEST_F(ResumeSitesTest, DeterminizedWorldsResumeToTheSameDecision) {
    std::map<std::string, int> checked, legal_diff, view_diff, step_diff;
    std::map<std::string, std::map<size_t, int>> diff_idx;
    std::string examples;
    // The second deck of the lab's pmcts benchmark, when the lab is present.
    DeckSubmission deck2 = *deck_;
    try {
        deck2 = DeckValidator::loadFromDeckList(repoRoot() + "/lab/decks/meta/rro25_2026-10-17/01_simple_kennen.txt", *db_);
    } catch (...) {}
    for (uint64_t seed = 301; seed <= 310; ++seed) {
        EventBus bus;
        GameEngine engine(*db_, bus, *registry_);
        std::mt19937_64 rng(seed);
        StepResult sr = engine.beginGame(*deck_, deck2, seed);
        for (int n = 0; n < 1500 && sr.kind == StepKind::NeedDecision; ++n) {
            const GameState& s = engine.state();
            if (sr.legal.size() > 1 && search::isResumableSnapshot(s) && !search::isCleanMainPhaseSnapshot(s)) {
                const std::string k = kindName(s.decision_site.kind);
                const PlayerId me = search::decidingPlayerOf(s, sr.legal);
                search::IsMctsSearch srch(*db_, *registry_, *deck_, deck2, seed, 4);
                const auto view = ml::extractStateFeatures(s, me, *db_);
                for (const auto& w : srch.sampleWorlds(s, me, 3, seed + n)) {
                    ++checked[k];
                    EventBus bus2;
                    GameEngine fresh(*db_, bus2, *registry_);
                    StepResult r = fresh.resumeFromSnapshot(GameState(w), 9);
                    if (r.kind != StepKind::NeedDecision || fresh.state().decision_index != s.decision_index) { ++step_diff[k]; continue; }
                    if (!(r.legal == sr.legal)) {
                        ++legal_diff[k];
                        if (examples.size() < 4000)
                            examples += "\n" + k + " decision " + std::to_string(s.decision_index) + " options differ:\noriginal:" +
                                        describeAll(sr.legal) + "\nresumed:" + describeAll(r.legal);
                    }
                    const auto v2 = ml::extractStateFeatures(fresh.state(), me, *db_);
                    const auto v1 = ml::extractStateFeatures(w, me, *db_);
                    if (v2 != view) {
                        ++view_diff[k];
                        // Which object is both in a trash and in a hidden zone vector?
                        if (examples.size() < 6000) {
                            for (auto pid : {PlayerId::Player1, PlayerId::Player2}) {
                                const auto& ps = s.player(pid);
                                auto in = [](const std::vector<GameObjectId>& v, GameObjectId o) {
                                    return std::find(v.begin(), v.end(), o) != v.end(); };
                                for (auto o : ps.trash) {
                                    for (auto q : {PlayerId::Player1, PlayerId::Player2}) {
                                        const auto& qs = s.player(q);
                                        std::string where;
                                        if (in(qs.hand, o)) where += " hand";
                                        if (in(qs.main_deck, o)) where += " main_deck";
                                        if (in(qs.rune_deck, o)) where += " rune_deck";
                                        if (in(qs.banishment, o)) where += " banishment";
                                        if (!where.empty())
                                            examples += "\n  trash object " + std::to_string(o) + " (" + s.getObject(o).name +
                                                        ", zone " + std::to_string(static_cast<int>(s.getObject(o).zone)) +
                                                        ") of " + toString(pid) + " also in " + toString(q) + ":" + where;
                                    }
                                }
                            }
                        }
                        for (size_t q = 0; q < view.size(); ++q) if (v2[q] != view[q]) ++diff_idx[k][q];
                        bool world_itself = v1 != view;
                        if (examples.size() < 6000)
                            examples += "\n" + k + " decision " + std::to_string(s.decision_index) +
                                        (world_itself ? " view differs already in the sampled world" : " view differs only after resume");
                    }
                }
            }
            sr = engine.applyChoice(randomChoice(rng, sr.legal));
        }
    }
    std::string table;
    for (const auto& [k, n] : checked) {
        table += "\n" + k + ": worlds " + std::to_string(n) + " step_diff " + std::to_string(step_diff[k]) +
                 " legal_diff " + std::to_string(legal_diff[k]) + " view_diff " + std::to_string(view_diff[k]) + " idx:";
        int shown = 0;
        for (const auto& [q, c] : diff_idx[k]) { if (shown++ > 25) { table += " ..."; break; } table += " " + std::to_string(q) + "x" + std::to_string(c); }
    }
    int bad = 0;
    for (const auto& [k, n] : legal_diff) bad += n;
    for (const auto& [k, n] : view_diff) bad += n;
    for (const auto& [k, n] : step_diff) bad += n;
    EXPECT_EQ(bad, 0) << table << examples;
}

}  // namespace
}  // namespace riftbound
