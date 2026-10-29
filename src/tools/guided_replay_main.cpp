/// @file guided_replay_main.cpp
/// `riftbound_guided`: replay a recorded human game (lab/human_games/*.json)
/// through the engine and ask a bot what it would have played at each of the
/// human's decisions. See tools/guided_replay.h.
///
///   ./build/riftbound_guided --game lab/human_games/<file>.json [--index 1]
///        [--agent ismcts:sims=40,eval=net:lab/learn/nets/net_004.rbvn]
///        [--out lab/human_replays/<stem>_g1] [--both] [--no-bot] [--verbose]

#include "agents/ismcts_search.h"
#include "agents/mcts_agent.h"
#include "agents/random_agent.h"
#include "cards/card_registry.h"
#include "core/card_db.h"
#include "tools/guided_replay.h"

#include <boost/program_options.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace po = boost::program_options;
namespace fs = std::filesystem;
using namespace riftbound;

namespace {

struct GuidedAgentSpec {
    std::string raw, kind;
    int sims = 0;
    SearchOptions search;
};

/// Same grammar as the riftbound CLI: kind[:key=value,...] with
/// sims=, eval=, c=, det=, final=, threads=, worlds=, prior=.
GuidedAgentSpec parseGuidedAgentSpec(const std::string& s) {
    GuidedAgentSpec out;
    out.raw = s;
    const auto colon = s.find(':');
    out.kind = colon == std::string::npos ? s : s.substr(0, colon);
    if (out.kind != "random" && out.kind != "mcts" && out.kind != "ismcts" && out.kind != "pmcts" && out.kind != "none")
        throw std::runtime_error("Unknown agent kind '" + out.kind + "' (random, mcts, ismcts, pmcts, none)");
    if (colon != std::string::npos) {
        const std::string rest = s.substr(colon + 1);
        size_t i = 0;
        while (i < rest.size()) {
            auto eq = rest.find('=', i);
            auto comma = rest.find(',', i);
            if (comma == std::string::npos) comma = rest.size();
            if (eq != std::string::npos && eq < comma) {
                const std::string key = rest.substr(i, eq - i), val = rest.substr(eq + 1, comma - eq - 1);
                if (key == "sims") out.sims = std::stoi(val);
                else if (key == "eval") out.search.eval = val;
                else if (key == "c") out.search.uct_c = std::stod(val);
                else if (key == "det") out.search.determinize = val != "0";
                else if (key == "final") out.search.final_policy = val;
                else if (key == "threads") out.search.threads = std::stoi(val);
                else if (key == "worlds") out.search.worlds = std::stoi(val);
                else if (key == "prior") out.search.prior = val;
            }
            i = comma + 1;
        }
    }
    if ((out.kind == "mcts" || out.kind == "ismcts" || out.kind == "pmcts") && out.sims <= 0)
        throw std::runtime_error("Agent '" + out.kind + "' requires sims=N");
    return out;
}

}  // namespace

int main(int argc, char* argv[]) {
    po::options_description desc("riftbound_guided — replay a human game and compare with the bot");
    desc.add_options()
        ("help,h", "show this help")
        ("game", po::value<std::string>()->required(), "human game file (lab/human_games/*.json)")
        ("index", po::value<int>()->default_value(1), "game number inside the file (BO3)")
        ("agent", po::value<std::string>()->default_value("ismcts:sims=40,eval=net:lab/learn/nets/net_004.rbvn"),
         "bot spec (random | mcts:sims=N | ismcts:sims=N,eval=...)")
        ("out", po::value<std::string>()->default_value(""), "output dir (default lab/human_replays/<stem>_g<N>)")
        ("root", po::value<std::string>()->default_value("."), "repo root (deck lists for filler)")
        ("both", po::bool_switch()->default_value(false), "compare the bot at both players' decisions")
        ("no-bot", po::bool_switch()->default_value(false), "replay only, no comparison")
        ("seed", po::value<uint64_t>()->default_value(1), "engine seed (coin toss is matched to the log)")
        ("max-desyncs", po::value<int>()->default_value(25), "stop the game after this many desyncs")
        ("max-decisions", po::value<int>()->default_value(400), "stop the game after this many decisions")
        ("verbose,v", po::bool_switch()->default_value(false), "print every decision");
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help")) { std::cout << desc << "\n"; return 0; }
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << "\n\n" << desc << "\n";
        return 1;
    }

    try {
        CardRegistry registry;
        registry.loadAll();
        CardDB db;
        db.buildFromClasses(registry);

        const std::string game_path = vm["game"].as<std::string>();
        const int index = vm["index"].as<int>();
        const std::string root = vm["root"].as<std::string>();
        auto game = guided::loadHumanGame(game_path, index);
        if (game.players.size() != 2) throw std::runtime_error("expected two players in " + game_path);

        std::string out_dir = vm["out"].as<std::string>();
        if (out_dir.empty())
            out_dir = (fs::path(root) / "lab" / "human_replays" / (game.stem + "_g" + std::to_string(index))).string();
        fs::create_directories(out_dir);

        const auto candidates = guided::findCandidateDeckLists(root);
        auto d1 = guided::inferDeck(game, game.players[0], db, candidates);
        auto d2 = guided::inferDeck(game, game.players[1], db, candidates);
        const std::string deck1_path = (fs::path(out_dir) / "deck_p1.txt").string();
        const std::string deck2_path = (fs::path(out_dir) / "deck_p2.txt").string();
        std::ofstream(deck1_path) << d1.deck_text;
        std::ofstream(deck2_path) << d2.deck_text;

        guided::GuidedOptions opts;
        opts.out_dir = out_dir;
        opts.compare_both = vm["both"].as<bool>();
        opts.seed = vm["seed"].as<uint64_t>();
        opts.max_desyncs = vm["max-desyncs"].as<int>();
        opts.max_decisions = vm["max-decisions"].as<int>();
        opts.verbose = vm["verbose"].as<bool>();
        std::error_code ec;
        opts.deck1_label = fs::relative(deck1_path, root, ec).string();
        opts.deck2_label = fs::relative(deck2_path, root, ec).string();
        if (ec) { opts.deck1_label = deck1_path; opts.deck2_label = deck2_path; }

        std::unique_ptr<AgentInterface> bot;
        const std::string agent_raw = vm["no-bot"].as<bool>() ? "none" : vm["agent"].as<std::string>();
        const auto spec = parseGuidedAgentSpec(agent_raw);
        opts.bot_spec = spec.raw;
        // The engine seed the driver will use: seedForFirstPlayer picks the
        // first seed at or after opts.seed whose coin toss matches the log.
        const PlayerId first = game.first_player == game.players[1] ? PlayerId::Player2 : PlayerId::Player1;
        const uint64_t engine_seed = guided::seedForFirstPlayer(opts.seed & 0x7FFFFFFFu, first);
        if (spec.kind == "random") bot = std::make_unique<RandomAgent>(engine_seed * 2 + 1);
        else if (spec.kind == "mcts")
            bot = std::make_unique<MctsAgent>(deck1_path, deck2_path, "", engine_seed, engine_seed * 2 + 1, spec.sims, spec.search);
        else if (spec.kind == "ismcts")
            bot = std::make_unique<IsMctsAgent>(deck1_path, deck2_path, "", engine_seed, engine_seed * 2 + 1, spec.sims, spec.search);
        else if (spec.kind == "pmcts")
            bot = std::make_unique<search::PuctAgent>(db, registry, d1.deck, d2.deck, engine_seed, engine_seed * 2 + 1,
                                                      spec.sims, spec.search);

        std::cout << game.stem << " game " << index << ": " << game.players[0] << " (" << d1.notes["legend"].get<std::string>()
                  << ") vs " << game.players[1] << " (" << d2.notes["legend"].get<std::string>() << "), "
                  << game.events.size() << " log lines, first player " << game.first_player
                  << ", bot " << spec.raw << "\n";

        auto res = guided::runGuidedReplay(game, d1, d2, db, registry, bot.get(), opts);
        const auto& s = res.summary;
        std::cout << std::fixed << std::setprecision(1)
                  << "  decisions " << s["decisions"].get<int>()
                  << " (human move known " << s["decisions_human_known"].get<int>()
                  << "), compared " << s["decisions_compared"].get<int>()
                  << ", agreement " << 100.0 * s["agreement"]["rate"].get<double>() << "%"
                  << " (clean boundary " << 100.0 * s["agreement"]["clean_boundary"]["rate"].get<double>()
                  << "% of " << s["agreement"]["clean_boundary"]["n"].get<int>() << ")"
                  << ", desyncs " << s["desyncs"].get<int>()
                  << ", silent passes " << s["silent_passes"].get<int>()
                  << ", hand fixes " << s["hand_fixes"]["pulled_from_deck"].get<int>() << "+"
                  << s["hand_fixes"]["created"].get<int>()
                  << ", stop: " << s["stop_reason"].get<std::string>()
                  << ", " << s["timing"]["seconds"].get<double>() << "s"
                  << " (bot " << s["timing"]["bot_seconds"].get<double>() << "s)\n"
                  << "  -> " << out_dir << "/game_1.json, summary.json\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "riftbound_guided: " << e.what() << "\n";
        return 1;
    }
}
