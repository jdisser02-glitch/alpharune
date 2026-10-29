/// @file rb_api.cpp
/// C API of the WebAssembly build: one game at a time, driven from
/// JavaScript (tools/wasm_test.js shows the calling sequence).
///
///   rb_version()                      build tag
///   rb_new_game(d1, d2, seat, seed, bot) decks in the .txt deck-list format;
///                                     seat 1/2 = the human's seat, 0 = bot
///                                     vs bot; bot = "greedy[:det=3,budget=
///                                     1500,steps=40]" or "random". 0 = ok.
///   rb_state_json(perspective)        board (decision_log boardJson frame +
///                                     zones), `legal` = {index,text} options,
///                                     `decision_player`; perspective 1/2
///                                     masks the other player's hand and
///                                     facedown cards, 0 shows everything.
///   rb_apply(index)                   the human's choice; engine runs to the
///                                     next decision. Never auto-plays bots.
///   rb_bot_move()                     the bot decides for whoever must act,
///                                     applies it, returns the legal index
///                                     (negative = error).
///   rb_bot_last_search_json()         {chosen, ms, worlds, options:[{index,
///                                     score}]} of that search.
///   rb_is_terminal() / rb_result_json() / rb_decision_player() /
///   rb_legal_count() / rb_load_value_net(bytes, len) / rb_end_game()
///
/// Every function catches C++ exceptions and reports them through
/// rb_last_error(); returned strings stay valid until the next call of the
/// same function.

#include "cards/card_registry.h"
#include "core/card_db.h"
#include "core/events.h"
#include "core/version.h"
#include "engine/game_engine.h"
#include "io/decision_log.h"
#include "ml/value_net.h"
#include "rules/deck_validator.h"
#include "wasm/greedy_bot.h"

#include <nlohmann/json.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define RB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define RB_EXPORT
#endif

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace riftbound;
using nlohmann::json;

enum RbError : int {
    kOk = 0,
    kBadArgument = 1,
    kDeckParse = 2,
    kDeckIllegal = 3,
    kNoGame = 4,
    kNotADecision = 5,
    kEngineError = 6,
    kBadNet = 7,
};

struct Cards {
    CardDB db;
    CardRegistry registry;
    Cards() {
        registry.loadAll();
        db.buildFromClasses(registry);
    }
};

Cards& cards() {
    static Cards c;   // built once per module
    return c;
}

struct Session {
    DeckSubmission deck1, deck2;
    uint64_t seed = 0;
    int human_seat = 0;  // 0 = bot vs bot
    std::unique_ptr<EventBus> bus;
    std::unique_ptr<GameEngine> engine;
    std::vector<int> history;             // legal indices applied so far
    std::unique_ptr<wasm::GreedyBot> bots[2];
    std::vector<std::string> log;         // engine log since the last move
    int last_bot_index = -1;
    PlayerId last_bot_player = PlayerId::None;
};

std::unique_ptr<Session> g_session;
std::shared_ptr<const ml::ValueNet> g_net;
std::string g_last_error;
std::string g_state_buf, g_result_buf, g_search_buf;

int fail(int code, const std::string& msg) {
    g_last_error = msg;
    return code;
}

PlayerId seatToPlayer(int seat) {
    return seat == 2 ? PlayerId::Player2 : PlayerId::Player1;
}
int playerToSeat(PlayerId p) {
    return p == PlayerId::Player1 ? 1 : p == PlayerId::Player2 ? 2 : 0;
}

PlayerId decisionPlayer(const StepResult& step) {
    if (step.kind != StepKind::NeedDecision) return PlayerId::None;
    for (const auto& it : step.legal)
        if (it.player != PlayerId::None) return it.player;
    return step.perspective;
}

json objBrief(const GameState& s, GameObjectId id) {
    if (id == kInvalidId || !s.objectExists(id)) return nullptr;
    const auto& o = s.getObject(id);
    json j = {{"id", o.id}, {"name", o.name}, {"card", o.card_def_id}};
    if (o.isUnit()) {
        j["might"] = o.current_might;
        if (o.damage_marked) j["damage"] = o.damage_marked;
    }
    if (o.is_exhausted) j["exhausted"] = true;
    return j;
}

/// Replace a facedown/hidden object's identity in a boardJson object list.
void maskObjects(json& arr) {
    for (auto& o : arr) {
        if (o.is_object() && o.value("facedown", false)) {
            o["name"] = nullptr;
            o["card"] = nullptr;
        }
    }
}

/// The review-frame board (decision_log.cpp's boardJson), extended with the
/// zones the page needs and masked for `perspective` (1, 2, or 0 = all).
json stateJson(const Session& ses, int perspective) {
    const GameState& s = ses.engine->state();
    json b = boardJson(s);
    const PlayerId viewer = perspective == 0 ? PlayerId::None : seatToPlayer(perspective);

    for (int k = 0; k < 2; ++k) {
        const PlayerId p = k == 0 ? PlayerId::Player1 : PlayerId::Player2;
        const auto& ps = s.player(p);
        json& pj = b["players"][k];
        const bool hidden = viewer != PlayerId::None && viewer != p;
        if (hidden) {
            for (auto& h : pj["hand"]) h = nullptr;
        } else {
            json ids = json::array();
            for (auto h : ps.hand) ids.push_back(objBrief(s, h));
            pj["hand_cards"] = ids;
        }
        json trash = json::array(), banish = json::array(), runes = json::array();
        for (auto id : ps.trash) trash.push_back(objBrief(s, id));
        for (auto id : ps.banishment) banish.push_back(objBrief(s, id));
        for (auto r : s.runesInBase(p)) {
            const auto& o = s.getObject(r);
            runes.push_back({{"id", o.id}, {"name", o.name}, {"card", o.card_def_id},
                             {"exhausted", o.is_exhausted}});
        }
        pj["trash_cards"] = trash;
        pj["banish_cards"] = banish;
        pj["runes"] = runes;
        int power = ps.rune_pool.universal_power;
        for (int d = 0; d < static_cast<int>(Domain::Count); ++d) power += ps.rune_pool.power[d];
        pj["power_pool"] = power;
        pj["champion"] = objBrief(s, ps.champion_zone);
        maskObjects(pj["base_units"]);
        maskObjects(pj["base_gear"]);
    }
    for (auto& bj : b["battlefields"]) {
        // Facedown cards: the controller sees them, the other player does not.
        if (viewer != PlayerId::Player1) maskObjects(bj["p1"]);
        if (viewer != PlayerId::Player2) maskObjects(bj["p2"]);
    }
    // Chain items: add the card id and whether the item is resolving.
    json chain = json::array();
    auto chainItem = [&](const ChainItem& it, bool resolving) {
        json j = {{"source", s.objectExists(it.source) ? json(s.getObject(it.source).name) : json("?")},
                  {"card", it.card_def_id},
                  {"player", playerToSeat(it.controller)}};
        if (resolving) j["resolving"] = true;
        return j;
    };
    for (const auto& it : s.chain.items) chain.push_back(chainItem(it, false));
    if (s.chain.resuming.has_value()) chain.push_back(chainItem(*s.chain.resuming, true));
    b["chain"] = chain;

    json out;
    out["board"] = std::move(b);
    out["n"] = s.decision_index;
    out["scores"] = {s.players[0].score, s.players[1].score};
    out["game_over"] = s.game_over;
    out["human_seat"] = ses.human_seat;
    out["perspective"] = perspective;

    const StepResult step = ses.engine->currentStep();
    const PlayerId dp = decisionPlayer(step);
    out["decision_player"] = playerToSeat(dp);
    out["decision_is_human"] = dp != PlayerId::None && playerToSeat(dp) == ses.human_seat;
    out["paying_cost"] = s.cost_cursor.has_value();
    json legal = json::array();
    if (step.kind == StepKind::NeedDecision) {
        for (size_t i = 0; i < step.legal.size(); ++i)
            legal.push_back({{"index", i}, {"text", describeIntent(step.legal[i], s)},
                             {"type", toString(step.legal[i].type)}});
    }
    out["legal"] = legal;
    out["log"] = ses.log;
    if (ses.last_bot_index >= 0) {
        out["last_bot_move"] = {{"player", playerToSeat(ses.last_bot_player)},
                                {"index", ses.last_bot_index}};
    }
    return out;
}

wasm::GreedyBot& botFor(Session& ses, PlayerId p) {
    auto& slot = ses.bots[p == PlayerId::Player2 ? 1 : 0];
    return *slot;
}

}  // namespace

extern "C" {

RB_EXPORT const char* rb_version() { return kVersionTag; }

RB_EXPORT const char* rb_last_error() { return g_last_error.c_str(); }

RB_EXPORT int rb_card_count() {
    try {
        return static_cast<int>(cards().db.size());
    } catch (const std::exception& e) {
        fail(kEngineError, e.what());
        return -1;
    }
}

RB_EXPORT int rb_new_game(const char* deck1_txt, const char* deck2_txt, int human_seat,
                          unsigned seed, const char* bot_spec) {
    g_last_error.clear();
    if (!deck1_txt || !deck2_txt) return fail(kBadArgument, "deck text is null");
    if (human_seat < 0 || human_seat > 2) return fail(kBadArgument, "human_seat must be 0, 1 or 2");
    try {
        auto& c = cards();
        auto ses = std::make_unique<Session>();
        {
            std::istringstream in1(deck1_txt), in2(deck2_txt);
            try {
                ses->deck1 = DeckValidator::parseDeckList(in1, "deck1", c.db);
                ses->deck2 = DeckValidator::parseDeckList(in2, "deck2", c.db);
            } catch (const std::exception& e) {
                return fail(kDeckParse, e.what());
            }
            DeckValidator validator(c.db);
            auto v1 = validator.validate(ses->deck1);
            auto v2 = validator.validate(ses->deck2);
            if (!v1.is_legal || !v2.is_legal) {
                std::string msg = "deck validation failed:";
                for (auto& e : v1.errors) msg += " [P1] " + e;
                for (auto& e : v2.errors) msg += " [P2] " + e;
                return fail(kDeckIllegal, msg);
            }
        }
        // 31-bit seed like the CLI (GameRunner masks the same way).
        ses->seed = (seed == 0 ? 0x5eed1234u : seed) & 0x7FFFFFFFu;
        ses->human_seat = human_seat;
        ses->bus = std::make_unique<EventBus>();
        ses->engine = std::make_unique<GameEngine>(c.db, *ses->bus, c.registry);
        Session* raw = ses.get();
        ses->bus->on_log.connect([raw](const LogEvent& e) {
            if (e.level == LogLevel::Debug || e.level == LogLevel::Info ||
                (e.level == LogLevel::Trace && raw->log.size() < 300))
                raw->log.push_back(e.message);
        });
        const auto cfg = wasm::GreedyBotConfig::parse(bot_spec ? bot_spec : "greedy");
        for (int k = 0; k < 2; ++k) {
            ses->bots[k] = std::make_unique<wasm::GreedyBot>(
                c.db, c.registry, ses->deck1, ses->deck2, ses->seed, cfg,
                ses->seed * 7919u + 17u + static_cast<uint64_t>(k));
            ses->bots[k]->setValueNet(g_net);
        }
        ses->engine->beginGame(ses->deck1, ses->deck2, ses->seed);
        g_session = std::move(ses);
        return kOk;
    } catch (const std::exception& e) {
        return fail(kEngineError, e.what());
    }
}

RB_EXPORT int rb_is_terminal() {
    if (!g_session) return 1;
    return g_session->engine->isStepDone() ? 1 : 0;
}

/// Seat (1/2) that must decide now, 0 when the game is over or not started.
RB_EXPORT int rb_decision_player() {
    if (!g_session || g_session->engine->isStepDone()) return 0;
    return playerToSeat(decisionPlayer(g_session->engine->currentStep()));
}

RB_EXPORT int rb_legal_count() {
    if (!g_session || g_session->engine->isStepDone()) return 0;
    return static_cast<int>(g_session->engine->currentStep().legal.size());
}

RB_EXPORT const char* rb_state_json(int perspective) {
    g_state_buf.clear();
    if (!g_session) {
        g_state_buf = "null";
        fail(kNoGame, "no game");
        return g_state_buf.c_str();
    }
    try {
        g_state_buf = stateJson(*g_session, perspective).dump();
    } catch (const std::exception& e) {
        fail(kEngineError, e.what());
        g_state_buf = json{{"error", e.what()}}.dump();
    }
    return g_state_buf.c_str();
}

RB_EXPORT int rb_apply(int legal_index) {
    g_last_error.clear();
    if (!g_session) return fail(kNoGame, "no game");
    auto& ses = *g_session;
    if (ses.engine->isStepDone()) return fail(kNotADecision, "game is over");
    const auto step = ses.engine->currentStep();
    if (legal_index < 0 || static_cast<size_t>(legal_index) >= step.legal.size())
        return fail(kBadArgument, "legal_index out of range");
    try {
        ses.log.clear();
        ses.last_bot_index = -1;
        ses.history.push_back(legal_index);
        ses.engine->applyChoice(legal_index);
        return kOk;
    } catch (const std::exception& e) {
        return fail(kEngineError, e.what());
    }
}

RB_EXPORT int rb_bot_move() {
    g_last_error.clear();
    if (!g_session) return -fail(kNoGame, "no game");
    auto& ses = *g_session;
    if (ses.engine->isStepDone()) return -fail(kNotADecision, "game is over");
    try {
        const PlayerId who = decisionPlayer(ses.engine->currentStep());
        if (who == PlayerId::None) return -fail(kNotADecision, "no decision pending");
        auto& bot = botFor(ses, who);
        const int idx = bot.choose(*ses.engine, who, ses.history);
        ses.log.clear();
        ses.history.push_back(idx);
        ses.engine->applyChoice(idx);
        ses.last_bot_index = idx;
        ses.last_bot_player = who;
        // Search record for rb_bot_last_search_json.
        json arr = json::array();
        for (const auto& [i, v] : bot.lastSearch()) arr.push_back({{"index", i}, {"score", v}});
        g_search_buf = json{{"player", playerToSeat(who)}, {"chosen", idx},
                            {"ms", bot.lastMs()}, {"worlds", bot.lastWorlds()},
                            {"replayed", bot.lastReplayed()}, {"options", arr}}.dump();
        return idx;
    } catch (const std::exception& e) {
        return -fail(kEngineError, e.what());
    }
}

RB_EXPORT const char* rb_bot_last_search_json() {
    if (g_search_buf.empty()) g_search_buf = "null";
    return g_search_buf.c_str();
}

RB_EXPORT const char* rb_result_json() {
    g_result_buf.clear();
    if (!g_session) {
        g_result_buf = "null";
        return g_result_buf.c_str();
    }
    const auto& ses = *g_session;
    const GameState& s = ses.engine->state();
    json j;
    j["terminal"] = ses.engine->isStepDone();
    j["winner"] = playerToSeat(s.winner);
    j["scores"] = {s.players[0].score, s.players[1].score};
    j["turns"] = s.turn.turn_number;
    j["decisions"] = s.decision_index;
    j["reason"] = s.game_over_reason;
    if (ses.engine->isStepDone()) {
        const auto& r = ses.engine->stepResult();
        j["winner"] = playerToSeat(r.winner);
        j["scores"] = {r.final_scores[0], r.final_scores[1]};
        j["turns"] = r.total_turns;
        j["decisions"] = r.total_decisions;
        j["reason"] = r.termination_reason;
    }
    g_result_buf = j.dump();
    return g_result_buf.c_str();
}

RB_EXPORT int rb_load_value_net(const uint8_t* data, int len) {
    g_last_error.clear();
    if (!data || len <= 0) return fail(kBadArgument, "empty value net buffer");
    try {
        auto net = std::make_shared<const ml::ValueNet>(data, static_cast<size_t>(len), "value net");
        g_net = net;
        if (g_session)
            for (auto& b : g_session->bots) if (b) b->setValueNet(g_net);
        return kOk;
    } catch (const std::exception& e) {
        return fail(kBadNet, e.what());
    }
}

/// Drop the current game (frees the engine and its fiber).
RB_EXPORT void rb_end_game() { g_session.reset(); }

}  // extern "C"
