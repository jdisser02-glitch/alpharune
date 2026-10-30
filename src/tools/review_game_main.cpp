/// @file review_game_main.cpp
/// riftbound_review: replay a game recorded by the dashboard's Play tab (the
/// exact index of every decision) and review the human's moves with the
/// strong bot, the way lab/tools/human_review.py reviews RiftLite games.
///
/// For each decision of the human that had a real choice, the bot is asked
/// what it would do at the same state (with the exact history, so its worlds
/// are rebuilt faithfully). With --rollouts N, every disagreement is also
/// settled on the board: from that state the human's move and the bot's move
/// are each played out N times with a fast bot on both seats, and the win
/// rate of each line is recorded ("from here, the bot's line wins 62 %").
///
/// Output: <out>/game_1.json (frames in the replay format, with human_choice /
/// bot_choice / agree / lines), <out>/summary.json, <out>/deck_p1.txt and
/// deck_p2.txt. lab/tools/review_games.py drives it and indexes the result.
#include "agents/ismcts_search.h"
#include "agents/search_options.h"
#include "cards/card_registry.h"
#include "core/card_db.h"
#include "core/events.h"
#include "engine/determinize.h"
#include "engine/game_engine.h"
#include "io/decision_log.h"
#include "rules/deck_validator.h"

#include <boost/program_options.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace po = boost::program_options;
namespace fs = std::filesystem;
using namespace riftbound;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    std::stringstream ss; ss << in.rdbuf(); return ss.str();
}

/// The kind of a move for the agreement breakdown (the review of human games
/// uses the same words).
std::string kindOf(const Intent& i) {
    switch (i.type) {
        case IntentType::PlayCard: case IntentType::PlayActionCard: case IntentType::PlayReaction: return "play";
        case IntentType::StandardMove: return "move";
        case IntentType::EndTurn: return "end_turn";
        case IntentType::PassFocus: return "pass_focus";
        case IntentType::PassPriority: return "pass";
        case IntentType::ActivateAbility: case IntentType::ActivateActionAbility: case IntentType::ActivateReactionAbility: return "activate";
        case IntentType::MulliganDecision: return "mulligan";
        case IntentType::ChooseBattlefield: return "battlefield";
        default: { std::string s = toString(i.type); for (auto& c : s) c = static_cast<char>(std::tolower(c)); return s; }
    }
}

int indexOf(const std::vector<Intent>& legal, const Intent& x) {
    for (size_t i = 0; i < legal.size(); ++i) if (legal[i] == x) return static_cast<int>(i);
    return -1;
}

struct Rollout { int games = 0, wins = 0; };

/// Play `line` from `st` to the end `n` times, the fast bot on both seats.
/// Wins are counted for `me`. Snapshots the engine cannot resume give 0 games.
Rollout rollout(const CardDB& db, const CardRegistry& reg, const GameState& st, int line, PlayerId me,
                search::PuctAgent& fast, int n, uint64_t seed) {
    Rollout r;
    if (!search::isResumableSnapshot(st)) return r;
    for (int g = 0; g < n; ++g) {
        EventBus bus;
        GameEngine e(db, bus, reg);
        StepResult s;
        try {
            s = e.resumeFromSnapshot(GameState(st), seed + static_cast<uint64_t>(g) * 7919u);
            if (s.kind != StepKind::NeedDecision || line < 0 || line >= static_cast<int>(s.legal.size())) continue;
            s = e.applyChoice(line);
            int guard = 0;
            while (s.kind == StepKind::NeedDecision && ++guard < 20000) {
                const Intent c = fast.selectAction(e.state(), s.legal);
                int idx = indexOf(s.legal, c); if (idx < 0) idx = 0;
                s = e.applyChoice(idx);
            }
        } catch (const std::exception& ex) {
            std::cerr << "  rollout error: " << ex.what() << "\n";
            continue;
        }
        if (!e.state().game_over) continue;
        r.games++;
        if (e.state().winner == me) r.wins++;
    }
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    po::options_description desc("riftbound_review");
    desc.add_options()
        ("help,h", "this help")
        ("game", po::value<std::string>()->required(), "the saved game (JSON from the Play tab's database record)")
        ("out", po::value<std::string>()->required(), "output directory")
        ("sims", po::value<int>()->default_value(300), "simulations per move for the reviewing bot")
        ("eval", po::value<std::string>()->default_value("policy:lab/learn/nets/net_022.rbpn"), "net for the reviewing bot (net:<rbvn> or policy:<rbpn>)")
        ("worlds", po::value<int>()->default_value(8), "determinizations per decision")
        ("rollouts", po::value<int>()->default_value(0), "games per line at each disagreement (0 = none)")
        ("rollout-sims", po::value<int>()->default_value(24), "simulations per move inside the rollouts");
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help")) { std::cout << desc << "\n"; return 0; }
        po::notify(vm);
    } catch (const std::exception& e) { std::cerr << e.what() << "\n" << desc << "\n"; return 2; }

    const auto t_start = std::chrono::steady_clock::now();
    const json doc = json::parse(slurp(vm["game"].as<std::string>()));
    const fs::path out = vm["out"].as<std::string>();
    fs::create_directories(out);

    CardRegistry registry; registry.loadAll();
    CardDB db; db.buildFromClasses(registry);

    const int seat = doc.value("seat", 1);
    const PlayerId me = seat == 2 ? PlayerId::Player2 : PlayerId::Player1;
    const std::string me_path = doc.value("me", ""), opp_path = doc.value("opponent", "");
    const std::string d1_path = seat == 1 ? me_path : opp_path, d2_path = seat == 1 ? opp_path : me_path;
    const std::string d1_txt = slurp(d1_path), d2_txt = slurp(d2_path);
    fs::copy_file(d1_path, out / "deck_p1.txt", fs::copy_options::overwrite_existing);
    fs::copy_file(d2_path, out / "deck_p2.txt", fs::copy_options::overwrite_existing);
    std::istringstream in1(d1_txt), in2(d2_txt);
    const DeckSubmission deck1 = DeckValidator::parseDeckList(in1, "deck1", db);
    const DeckSubmission deck2 = DeckValidator::parseDeckList(in2, "deck2", db);
    uint64_t seed = doc.value("seed", 0ull);
    seed = (seed == 0 ? 0x5eed1234u : seed) & 0x7FFFFFFFu;   // as rb_new_game masks it

    SearchOptions opts; opts.eval = vm["eval"].as<std::string>(); opts.worlds = vm["worlds"].as<int>(); opts.threads = 1;
    const int sims = vm["sims"].as<int>();
    search::PuctAgent bot(db, registry, deck1, deck2, seed, seed * 31u + 7u, sims, opts);
    SearchOptions fopts = opts; fopts.worlds = 4;
    search::PuctAgent fast(db, registry, deck1, deck2, seed, seed * 17u + 3u, vm["rollout-sims"].as<int>(), fopts);
    const int rollouts = vm["rollouts"].as<int>();
    const std::string bot_spec = "pmcts:sims=" + std::to_string(sims) + ",eval=" + opts.eval + ",worlds=" + std::to_string(opts.worlds);

    EventBus bus;
    std::vector<std::string> lines;
    bus.on_log.connect([&lines](const LogEvent& e) {
        if (e.level == LogLevel::Debug || e.level == LogLevel::Info || e.level == LogLevel::Trace) lines.push_back(e.message);
    });
    GameEngine engine(db, bus, registry);
    DecisionLog log;
    std::vector<int32_t> history;
    const json& decisions = doc.at("decisions");
    std::string stop_reason = "game over";
    int compared = 0, agreed = 0, trivial = 0, human_decisions = 0;
    std::map<std::string, std::pair<int, int>> by_kind;
    json disagreements = json::array();
    double bot_seconds = 0;

    StepResult step = engine.beginGame(deck1, deck2, seed);
    size_t k = 0;
    while (step.kind == StepKind::NeedDecision) {
        if (k >= decisions.size()) { stop_reason = "record ended before the game"; break; }
        const json& d = decisions[k];
        const int idx = d.value("chosen", -1);
        const auto& legal = step.legal;
        if (idx < 0 || idx >= static_cast<int>(legal.size())) {
            stop_reason = "desync at decision " + std::to_string(k) + ": chosen " + std::to_string(idx) + " of " + std::to_string(legal.size()) + " options";
            break;
        }
        const GameState& st = engine.state();
        // The record says who decided (the browser engine's decision_is_human);
        // the engine's own reading is the fallback for records without it.
        const bool human = d.contains("player") && d["player"].is_number()
            ? d["player"].get<int>() == seat : search::decidingPlayerOf(st, legal) == me;
        const bool real = legal.size() > 1 && !st.cost_cursor.has_value();
        std::vector<std::pair<int, double>> shares;
        int bot_choice = -1;
        if (human) {
            ++human_decisions;
            if (!real) ++trivial;
            else {
                const auto t0 = std::chrono::steady_clock::now();
                bot.setDecisionHistory(&history);
                const Intent c = bot.selectAction(st, legal);
                bot_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                bot_choice = indexOf(legal, c);
                shares = bot.lastSearch();
            }
        } else if (d.contains("options") && d["options"].is_array()) {
            for (const auto& o : d["options"])
                if (o.is_array() && o.size() == 2 && o[0].is_number() && o[1].is_number())
                    shares.emplace_back(o[0].get<int>(), o[1].get<double>());
        }
        std::vector<std::string> taken; taken.swap(lines);
        log.record(st, legal, legal[idx], shares, std::move(taken));
        json extra = {{"human_known", true}, {"human_choice", idx}, {"clean_boundary", true},
                      {"kind", kindOf(legal[idx])}, {"who", human ? "me" : "bot"}};
        if (human && real) {
            const bool agree = bot_choice == idx;
            ++compared; if (agree) ++agreed;
            auto& bk = by_kind[kindOf(legal[idx])]; bk.first++; if (agree) bk.second++;
            extra["bot_choice"] = bot_choice; extra["agree"] = agree;
            if (!agree) {
                json dis = {{"decision", st.decision_index}, {"turn", st.turn.turn_number}, {"human", idx}, {"bot", bot_choice},
                            {"human_text", describeIntent(legal[idx], st, &db)}, {"bot_text", describeIntent(legal[bot_choice], st, &db)},
                            {"kind", kindOf(legal[idx])}};
                if (rollouts > 0) {
                    const Rollout h = rollout(db, registry, st, idx, me, fast, rollouts, seed * 101u + k);
                    const Rollout b = rollout(db, registry, st, bot_choice, me, fast, rollouts, seed * 103u + k);
                    auto rateOf = [](const Rollout& r) { return r.games ? json(double(r.wins) / r.games) : json(nullptr); };
                    json ln = {{"games", rollouts},
                               {"human", {{"games", h.games}, {"wins", h.wins}, {"rate", rateOf(h)}}},
                               {"bot", {{"games", b.games}, {"wins", b.wins}, {"rate", rateOf(b)}}}};
                    extra["lines"] = ln; dis["lines"] = ln;
                    std::cerr << "  decision " << st.decision_index << " (turn " << st.turn.turn_number << "): you " << describeIntent(legal[idx], st, &db)
                              << " [" << h.wins << "/" << h.games << "]  bot " << describeIntent(legal[bot_choice], st, &db) << " [" << b.wins << "/" << b.games << "]\n";
                }
                disagreements.push_back(dis);
            }
        }
        log.annotateLast(extra);
        history.push_back(idx);
        step = engine.applyChoice(idx);
        ++k;
    }
    const GameState& fin = engine.state();
    const int winner = fin.winner == PlayerId::Player1 ? 1 : fin.winner == PlayerId::Player2 ? 2 : 0;
    const std::vector<std::string> players = seat == 1 ? std::vector<std::string>{"You", "Bot"} : std::vector<std::string>{"Bot", "You"};
    json header = {{"agent1", seat == 1 ? "human:You" : "bot:" + doc.value("bot_spec", "")},
                   {"agent2", seat == 2 ? "human:You" : "bot:" + doc.value("bot_spec", "")},
                   {"bot", bot_spec}, {"capture_player", "You"}, {"capture_seat", seat}, {"compare_both", false},
                   {"decisions", fin.decision_index}, {"deck1", (out / "deck_p1.txt").string()}, {"deck2", (out / "deck_p2.txt").string()},
                   {"game", 1}, {"guided", true}, {"players", players}, {"reason", fin.game_over_reason},
                   {"scores", {fin.players[0].score, fin.players[1].score}}, {"seed", seed}, {"source", "play"},
                   {"source_game", doc.value("id", "")}, {"turns", fin.turn.turn_number}, {"uuid", ""}, {"winner", winner},
                   {"play_level", doc.value("level", "")}, {"play_bot_spec", doc.value("bot_spec", "")}};
    log.finish(header, (out / "game_1.json").string());

    json bk = json::object();
    for (const auto& [kind, v] : by_kind) bk[kind] = {{"n", v.first}, {"agree", v.second}, {"rate", v.first ? double(v.second) / v.first : 0.0}};
    const double rate = compared ? double(agreed) / compared : 0.0;
    json summary = {{"agreement", {{"n", compared}, {"agree", agreed}, {"rate", rate}, {"by_kind", bk},
                                   {"clean_boundary", {{"n", compared}, {"agree", agreed}, {"rate", rate}}}}},
                    {"bot", bot_spec}, {"capture_player", "You"}, {"compare_both", false},
                    {"decisions", fin.decision_index}, {"decisions_compared", compared}, {"decisions_human_known", human_decisions},
                    {"decisions_trivial", trivial}, {"desync_causes", json::object()}, {"desync_list", json::array()}, {"desyncs", 0},
                    {"disagreements", disagreements}, {"first_player", fin.turn.starting_player == PlayerId::Player1 ? players[0] : players[1]},
                    {"legends", {{"P1", db.get(deck1.legend).name}, {"P2", db.get(deck2.legend).name}}},
                    {"players", players}, {"result", {{"engine_scores", {fin.players[0].score, fin.players[1].score}}, {"engine_winner", winner},
                                                      {"log_scores", {fin.players[0].score, fin.players[1].score}}, {"reason", fin.game_over_reason},
                                                      {"turns", fin.turn.turn_number}}},
                    {"seed", seed}, {"source", "play"}, {"stop_reason", stop_reason}, {"rollouts", rollouts},
                    {"timing", {{"bot_seconds", bot_seconds},
                                {"seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count()}}}};
    std::ofstream(out / "summary.json") << summary.dump(1) << "\n";
    std::cout << "reviewed " << k << " decisions, " << compared << " compared, " << agreed << " agree ("
              << static_cast<int>(100 * rate + 0.5) << "%), " << disagreements.size() << " disagreements; " << stop_reason << "\n";
    return 0;
}
