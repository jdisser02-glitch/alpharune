#include "tools/guided_replay.h"

#include "core/events.h"
#include "core/game_object.h"
#include "engine/game_engine.h"
#include "io/decision_log.h"
#include "ml/feature_extractor.h"
#include "ml/value_net.h"
#include "openspiel/action_vocab.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

namespace riftbound::guided {

// ═══════════════════════════════════════════════════════════════════════════
// Names
// ═══════════════════════════════════════════════════════════════════════════

std::string normalizeName(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool space = false;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        // U+2019 right single quotation mark (E2 80 99) -> '
        if (c == 0xE2 && i + 2 < s.size() &&
            static_cast<unsigned char>(s[i + 1]) == 0x80 &&
            static_cast<unsigned char>(s[i + 2]) == 0x99) {
            out += '\'';
            i += 2;
            space = false;
            continue;
        }
        if (std::isspace(c)) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += static_cast<char>(std::tolower(c));
    }
    while (!out.empty() && out.back() == '.') out.pop_back();
    return out;
}

namespace {

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

bool nameMatches(const std::string& log_name, const std::string& engine_name) {
    const std::string a = normalizeName(log_name), b = normalizeName(engine_name);
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;
    return endsWith(a, ", " + b) || endsWith(b, ", " + a);
}

const CardDef* resolveCard(const CardDB& db, const std::string& name, const std::string& code) {
    if (!code.empty()) {
        for (const auto& [id, c] : db.all()) {
            const auto slash = c.public_code.find('/');
            const std::string prefix =
                slash == std::string::npos ? c.public_code : c.public_code.substr(0, slash);
            if (!prefix.empty() && prefix == code) return &c;
        }
    }
    if (name.empty()) return nullptr;
    if (const auto* d = db.findByName(name)) return d;
    const std::string n = normalizeName(name);
    const CardDef* suffix_hit = nullptr;
    for (const auto& [id, c] : db.all()) {
        const std::string cn = normalizeName(c.name);
        if (cn == n) return &c;
        if (!suffix_hit && (endsWith(n, ", " + cn) || endsWith(cn, ", " + n))) suffix_hit = &c;
    }
    return suffix_hit;
}

// ═══════════════════════════════════════════════════════════════════════════
// Human-game file
// ═══════════════════════════════════════════════════════════════════════════

namespace {

std::string jstr(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return "";
    return it->get<std::string>();
}

int jint(const nlohmann::json& j, const char* key, int dflt = 0) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return dflt;
    return it->get<int>();
}

}  // namespace

HumanGame humanGameFromJson(const nlohmann::json& file, int game_number, const std::string& path) {
    HumanGame g;
    g.path = path;
    g.stem = std::filesystem::path(path).stem().string();
    g.game_number = game_number;
    if (file.contains("players"))
        for (const auto& p : file["players"]) g.players.push_back(p.get<std::string>());
    g.capture_player = jstr(file, "capture_player");
    if (g.capture_player.empty() && !g.players.empty()) g.capture_player = g.players[0];
    if (file.contains("legends")) {
        for (auto it = file["legends"].begin(); it != file["legends"].end(); ++it) {
            LegendHeader h;
            h.legend_name = jstr(*it, "legend");
            if (it->contains("cards")) {
                for (const auto& c : (*it)["cards"]) {
                    const std::string nm = jstr(c, "name"), code = jstr(c, "code");
                    if (normalizeName(nm) == normalizeName(h.legend_name) && h.legend_code.empty()) {
                        h.legend_code = code;
                    } else if (h.champion_name.empty()) {
                        h.champion_name = nm;
                        h.champion_code = code;
                    }
                }
            }
            g.legends[it.key()] = h;
        }
    }
    int begin = 0, end = -1;
    bool found = false;
    if (file.contains("games")) {
        for (const auto& ge : file["games"]) {
            if (jint(ge, "game", 1) != game_number) continue;
            found = true;
            if (ge.contains("events") && ge["events"].is_array() && ge["events"].size() == 2) {
                begin = ge["events"][0].get<int>();
                end = ge["events"][1].get<int>();
            }
            g.first_player = jstr(ge, "first_player");
            if (ge.contains("score_from_log") && ge["score_from_log"].is_object())
                for (auto it = ge["score_from_log"].begin(); it != ge["score_from_log"].end(); ++it)
                    g.score_from_log[it.key()] = it->is_number() ? it->get<int>() : 0;
        }
    }
    if (!found) throw std::runtime_error(path + ": no game " + std::to_string(game_number));
    const auto& events = file.at("events");
    if (end < 0) end = static_cast<int>(events.size());
    for (int i = begin; i < end && i < static_cast<int>(events.size()); ++i) {
        const auto& e = events[i];
        if (jint(e, "game", game_number) != game_number) continue;
        LogLine l;
        l.index = i;
        l.actor = jstr(e, "actor");
        l.kind = jstr(e, "kind");
        l.text = jstr(e, "text");
        l.card = jstr(e, "card");
        l.from = jstr(e, "from");
        l.to = jstr(e, "to");
        l.battlefield = jstr(e, "battlefield");
        if (l.kind == "mulligan") l.amount = jint(e, "recycled", 0);
        else l.amount = jint(e, "amount", 0);
        l.score_to = jint(e, "score_to", -1);
        l.seconds = jint(e, "seconds", -1);
        bool repeat = false;
        {
            auto t = e.find("token");
            l.token = t != e.end() && t->is_boolean() && t->get<bool>();
            auto a = e.find("actor_inferred");
            l.actor_inferred = a != e.end() && a->is_boolean() && a->get<bool>();
            auto r = e.find("repeat_of_previous");
            repeat = r != e.end() && r->is_boolean() && r->get<bool>();
        }
        if (e.contains("cards") && e["cards"].is_array())
            for (const auto& c : e["cards"]) if (c.is_string()) l.cards.push_back(c.get<std::string>());
        // "<X> played Seal of Rage from banished to base.": the importer left
        // the destination inside `from`.
        if (l.kind == "played" && l.to.empty()) {
            const auto at = l.from.rfind(" to ");
            if (at != std::string::npos && at > 0) {
                l.to = l.from.substr(at + 4);
                l.from = l.from.substr(0, at);
            }
        }
        // "<X> moved a hidden card to <bf>." is how some captures log a hide
        // (the [A] is the "recycled 1 <D> rune" line before it), the way
        // others log "<X> put a hidden card to <bf>."
        if (l.kind == "moved" && normalizeName(l.card) == "a hidden card") {
            l.kind = "hidden_card_placed";
            l.card.clear();
        }
        // "<X> played 3 Bomb tokens to <bf>": an effect's tokens, like the
        // singular "played Bomb token" the importer flags.
        if (l.kind == "played" && !l.token && l.amount > 1 &&
            l.text.find(" tokens") != std::string::npos)
            l.token = true;
        // Capture artefacts: a line repeated verbatim at the same second is
        // genuine for runes, draws and tokens (two Mech tokens, two runes
        // exhausted), never for a decision a player can only take once (the
        // same unit moved to the same battlefield twice, the turn ended
        // twice). A repeated "ended their turn" can also show up a few lines
        // later, after the other player's scoring lines, still at the same
        // second as the first.
        if (repeat && (l.kind == "moved" || l.kind == "ended_turn" || l.kind == "passed_focus" ||
                       l.kind == "showdown" || l.kind == "equipped" || l.kind == "hidden_card_placed" ||
                       l.kind == "mulligan" || l.kind == "battlefield_locked"))
            continue;
        if (l.kind == "ended_turn") {
            const LogLine* prev_end = nullptr;
            for (auto it = g.events.rbegin(); it != g.events.rend(); ++it)
                if (it->kind == "ended_turn") { prev_end = &*it; break; }
            if (prev_end && prev_end->actor == l.actor && l.seconds >= 0 && prev_end->seconds == l.seconds)
                continue;
        }
        g.events.push_back(std::move(l));
    }
    // "<X> paid 2E / 1P — Exhaust 2 · Kai'Sa, Daughter of the Void · Recycle 1
    // Fury rune": besides runes, the payment names the sources whose [Add]
    // abilities paid part of it (a legend, Gold tokens, Seals; CR 430.3).
    // Each is an activation: give it its own "<X> exhausted <source>" line
    // right before the payment, the way other captures log it, so the
    // ability is used before the card is paid.
    {
        const std::string dash = "\xE2\x80\x94 ", dot = " \xC2\xB7 ", times = "\xC3\x97";
        std::vector<LogLine> out;
        out.reserve(g.events.size());
        std::string prev_text;
        for (auto& l : g.events) {
            const auto d = l.text.find(dash);
            // A payment line repeated verbatim right after itself is one
            // payment (the second copy is a capture artefact: one play follows).
            const bool repeated = l.text == prev_text;
            prev_text = l.text;
            if (l.kind == "paid" && !l.actor.empty() && d != std::string::npos && !repeated) {
                std::string rest = l.text.substr(d + dash.size());
                while (!rest.empty() && rest.back() == '.') rest.pop_back();
                std::vector<std::string> parts;
                for (size_t a = 0;;) {
                    const auto b = rest.find(dot, a);
                    parts.push_back(rest.substr(a, b == std::string::npos ? std::string::npos : b - a));
                    if (b == std::string::npos) break;
                    a = b + dot.size();
                }
                for (std::string part : parts) {
                    auto starts = [&](const char* w) { return part.rfind(w, 0) == 0; };
                    if (part.empty() || starts("Exhaust ") || starts("Recycle ") || starts("Float ")) continue;
                    int n = 1;
                    size_t k = 0;
                    while (k < part.size() && std::isdigit(static_cast<unsigned char>(part[k]))) ++k;
                    if (k > 0 && k < part.size()) {
                        size_t after = k;
                        if (part.compare(after, times.size(), times) == 0) after += times.size();
                        if (after < part.size() && part[after] == ' ') {
                            n = std::max(1, std::stoi(part.substr(0, k)));
                            part = part.substr(after + 1);
                        }
                    }
                    for (int c = 0; c < n && c < 6; ++c) {
                        LogLine x;
                        x.index = l.index;
                        x.actor = l.actor;
                        x.kind = "exhausted";
                        x.card = part;
                        x.text = l.actor + " exhausted " + part + ". (named by the payment)";
                        x.seconds = l.seconds;
                        out.push_back(std::move(x));
                    }
                }
            }
            out.push_back(std::move(l));
        }
        g.events = std::move(out);
    }
    // "<X> revealed Edge of Night on the board.": the capture's line for a
    // card played from face down (CR 811) — the play's own line, unless an
    // explicit "Played <C> from <battlefield>." follows.
    for (size_t i = 0; i < g.events.size(); ++i) {
        LogLine& l = g.events[i];
        if (l.kind != "revealed" || l.text.find(" on the board") == std::string::npos || l.card.empty()) continue;
        bool explicit_play = false;
        for (size_t j = i + 1; j < g.events.size() && j <= i + 3; ++j)
            if (g.events[j].kind == "played" && normalizeName(g.events[j].card) == normalizeName(l.card))
                explicit_play = true;
        if (explicit_play) continue;
        l.kind = "played";
        l.from = "face down";
        l.to.clear();
        // The capture logs the reveal where the card shows, not always where
        // it was played (a reaction to a spell or a replacement such as
        // Zhonya's Hourglass): when the engine cannot take the play there,
        // the line is dropped the way it was before plays from face down
        // were read.
        l.soft = true;
    }
    // A play the client took back: "<X> played C from hand to <loc>", at once
    // "<X> moved C to hand", then "<X> readied 1 <D> rune" for each rune its
    // payment used. Nothing returns a card to its owner's hand as it is
    // played; neither the play nor its payment happened.
    for (size_t i = 0; i + 1 < g.events.size();) {
        const LogLine& a = g.events[i];
        const LogLine& b = g.events[i + 1];
        const bool undo = a.isCardPlay() && (a.from.empty() || normalizeName(a.from) == "hand") &&
                          b.kind == "moved" && b.actor == a.actor && !a.actor.empty() &&
                          normalizeName(b.to) == "hand" && !a.card.empty() &&
                          normalizeName(a.card) == normalizeName(b.card);
        if (!undo) { ++i; continue; }
        size_t to_end = i + 2;
        int refunded = 0;
        while (to_end < g.events.size() && g.events[to_end].kind == "readied" &&
               g.events[to_end].actor == a.actor && g.events[to_end].text.find(" rune") != std::string::npos) {
            ++refunded;
            ++to_end;
        }
        size_t from = i;
        // The payment the refund gives back: "<X> paid 2E — Exhaust 2" or one
        // "<X> exhausted 1 <D> rune" line per rune, right before the play.
        while (refunded > 0 && from > 0) {
            const LogLine& p = g.events[from - 1];
            if (p.actor != a.actor) break;
            if (p.kind == "exhausted" && p.text.find(" rune") != std::string::npos) { --refunded; --from; continue; }
            if (p.kind == "paid") {
                const auto at = p.text.find(" paid ");
                int x = 0;
                if (at != std::string::npos) { std::istringstream is(p.text.substr(at + 6)); is >> x; }
                if (x > 0 && x <= refunded) { refunded -= x; --from; continue; }
            }
            break;
        }
        g.events.erase(g.events.begin() + static_cast<long>(from), g.events.begin() + static_cast<long>(to_end));
        i = from;
    }
    // A move the client took back: "<X> moved U to <battlefield>", then at
    // once "<X> moved U to base" and "<X> readied U". No rule moves a unit
    // from a battlefield to base and readies it in the same breath (a move
    // exhausts, a recall keeps the unit's state, CR 460); it is the client
    // undoing the first move, so neither move happened.
    for (size_t i = 0; i + 2 < g.events.size();) {
        const LogLine& a = g.events[i];
        const LogLine& b = g.events[i + 1];
        const LogLine& c = g.events[i + 2];
        const std::string to = normalizeName(a.to);
        const bool to_battlefield = !to.empty() && to != "base" && to != "hand" && to != "trash" &&
                                    to != "deck" && to.rfind("deck", 0) != 0 && to != "banished" &&
                                    to != "banishment" && to != "champion area";
        const bool undo = a.kind == "moved" && b.kind == "moved" && c.kind == "readied" &&
                          !a.actor.empty() && a.actor == b.actor && b.actor == c.actor &&
                          to_battlefield && normalizeName(b.to) == "base" &&
                          !a.card.empty() && normalizeName(a.card) == normalizeName(b.card) &&
                          normalizeName(b.card) == normalizeName(c.card) &&
                          (a.seconds < 0 || c.seconds < 0 || c.seconds - a.seconds <= 5);
        if (undo) {
            g.events.erase(g.events.begin() + static_cast<long>(i), g.events.begin() + static_cast<long>(i) + 3);
            continue;
        }
        // A champion played and put back: "<X> paid 2E — Exhaust 2", "<X>
        // moved Evelynn, Entrancing to base", "<X> moved Evelynn, Entrancing
        // to champion area", "<X> readied 1 Chaos rune" ×2. Nothing returns a
        // unit to the Champion Zone: the client undid the play and its payment.
        if (a.kind == "moved" && b.kind == "moved" && !a.actor.empty() && a.actor == b.actor &&
            !a.card.empty() && normalizeName(a.card) == normalizeName(b.card) &&
            normalizeName(b.to) == "champion area") {
            size_t from = i, to_end = i + 2;
            if (from > 0 && g.events[from - 1].kind == "paid" && g.events[from - 1].actor == a.actor) --from;
            while (to_end < g.events.size() && g.events[to_end].kind == "readied" &&
                   g.events[to_end].actor == a.actor && g.events[to_end].text.find(" rune") != std::string::npos)
                ++to_end;
            g.events.erase(g.events.begin() + static_cast<long>(from), g.events.begin() + static_cast<long>(to_end));
            i = from;
            continue;
        }
        ++i;
    }
    if (g.first_player.empty()) {
        for (const auto& e : g.events)
            if (e.kind == "first_turn" || e.kind == "ended_turn") { g.first_player = e.actor; break; }
    }
    inferActors(g);
    return g;
}

namespace {

bool isRuneLine(const LogLine& e) { return e.text.find(" rune") != std::string::npos; }

/// "<X> paid ...", "<X> exhausted 1 Calm rune", "<X> recycled 1 Chaos rune",
/// "<X> exhausted <unit>" (Kai'Sa's free spells, additional costs): the
/// payment that precedes a spell.
bool isPaymentLine(const LogLine& e) {
    if (e.actor.empty() || e.actor == "both") return false;
    if (e.kind == "paid") return true;
    if (e.kind == "exhausted") return true;
    if (e.kind == "recycled" && isRuneLine(e)) return true;
    return false;
}

/// A payment line, or "<X> used a Gold token": what a capture that logs the
/// play first writes right after it.
bool isPaymentOrTokenLine(const LogLine& e) {
    return isPaymentLine(e) || (e.kind == "token_used" && !e.actor.empty() && e.actor != "both");
}

bool isZoneWord(const std::string& s) {
    const std::string n = normalizeName(s);
    return n.empty() || n == "hand" || n == "trash" || n == "deck" || n == "banished" ||
           n == "banishment" || n == "token" || n == "base" || n == "chain";
}

}  // namespace

int inferActors(HumanGame& game) {
    int changed = 0;
    auto& ev = game.events;
    for (size_t i = 0; i < ev.size(); ++i) {
        LogLine& e = ev[i];
        if (e.kind != "played" || !e.actor_inferred || e.token) continue;
        std::string who;
        // The payment just before the play (one intermediate payment line of
        // the same player is allowed: "paid 1E", "exhausted Pyke", "Played X").
        // A unit dying between them ("<Y> moved Bomb from Startipped Peak to
        // trash", the result of an earlier chain item) is skipped.
        size_t skipped = 0;
        for (size_t back = 1; back <= 2 + skipped && back <= i; ++back) {
            const LogLine& prev = ev[i - back];
            if (back == 1 && skipped == 0 && prev.kind == "killed") { ++skipped; continue; }
            if (!isPaymentLine(prev)) break;
            if (!who.empty() && prev.actor != who) break;
            who = prev.actor;
            if (prev.kind == "paid" || isRuneLine(prev)) break;
        }
        // Some captures log the play first and its payment right after
        // ("Played Cull the Weak from hand." / "W exhausted 1 Mind rune" / "W
        // exhausted 1 Order rune" / "W used a Gold token" / the kills). The
        // payer is the spell's player when the run of payment lines leads into
        // the spell's effects or its resolution, not into another play (whose
        // payment it would then be).
        if (who.empty()) {
            size_t j = i + 1;
            std::string payer;
            for (; j < ev.size() && j <= i + 4 && isPaymentOrTokenLine(ev[j]); ++j) {
                if (!payer.empty() && ev[j].actor != payer) { payer.clear(); break; }
                payer = ev[j].actor;
            }
            if (!payer.empty() && j < ev.size()) {
                const LogLine& after = ev[j];
                const bool another_play = after.kind == "played" && !after.token;
                if (!another_play && !isPaymentOrTokenLine(after)) who = payer;
            }
        }
        // A card played from a battlefield was hidden there by its player.
        if (who.empty() && !e.from.empty() && !isZoneWord(e.from)) {
            for (size_t j = i; j-- > 0;) {
                const LogLine& h = ev[j];
                if (h.kind == "hidden_card_placed" && nameMatches(h.to, e.from) && !h.actor.empty()) {
                    who = h.actor;
                    break;
                }
            }
        }
        if (who.empty()) {
            e.actor_uncertain = true;
            continue;
        }
        if (who != e.actor) {
            e.actor = who;
            ++changed;
        }
    }
    return changed;
}

HumanGame loadHumanGame(const std::string& path, int game_number) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    nlohmann::json j;
    f >> j;
    return humanGameFromJson(j, game_number, path);
}

// ═══════════════════════════════════════════════════════════════════════════
// Deck inference
// ═══════════════════════════════════════════════════════════════════════════

std::vector<std::string> findCandidateDeckLists(const std::string& root) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    const fs::path meta = fs::path(root) / "lab" / "decks" / "meta";
    if (fs::exists(meta)) {
        std::vector<std::string> found;
        for (const auto& e : fs::recursive_directory_iterator(meta))
            if (e.is_regular_file() && e.path().extension() == ".txt") found.push_back(e.path().string());
        std::sort(found.begin(), found.end());
        out.insert(out.end(), found.begin(), found.end());
    }
    const fs::path decks = fs::path(root) / "decks";
    if (fs::exists(decks)) {
        std::vector<std::string> found;
        for (const auto& e : fs::directory_iterator(decks))
            if (e.is_regular_file() && e.path().extension() == ".txt") found.push_back(e.path().string());
        std::sort(found.begin(), found.end());
        out.insert(out.end(), found.begin(), found.end());
    }
    return out;
}

namespace {

bool isLocationWord(const std::string& s) {
    const std::string n = normalizeName(s);
    return n.empty() || n == "base" || n == "hand" || n == "trash" || n == "deck" ||
           n == "chain" || n == "token" || n == "banishment";
}

bool fitsDomains(const CardDef& c, const CardDef& legend) {
    for (auto d : c.domains)
        if (!legend.hasDomain(d)) return false;
    return true;
}

bool isDeckCardType(const CardDef& c) {
    if (c.super_type == SuperType::Token) return false;   // Bomb, Recruit, Sprite, ...
    return c.card_type == CardType::Unit || c.card_type == CardType::Spell ||
           c.card_type == CardType::Gear;
}

std::string sectionLines(const CardDB& db, const std::vector<CardDefId>& ids) {
    std::vector<std::pair<std::string, int>> counts;  // keep first-seen order
    for (auto id : ids) {
        const auto& name = db.get(id).name;
        auto it = std::find_if(counts.begin(), counts.end(),
                               [&](const auto& p) { return p.first == name; });
        if (it == counts.end()) counts.emplace_back(name, 1);
        else it->second++;
    }
    std::string out;
    for (const auto& [name, n] : counts) out += std::to_string(n) + " " + name + "\n";
    return out;
}

}  // namespace

int settleActorsByDomain(HumanGame& game, const CardDB& db) {
    if (game.players.size() != 2) return 0;
    const CardDef* legends[2] = {nullptr, nullptr};
    for (int i = 0; i < 2; ++i) {
        auto it = game.legends.find(game.players[static_cast<size_t>(i)]);
        if (it == game.legends.end()) return 0;
        const CardDef* l = resolveCard(db, it->second.legend_name, it->second.legend_code);
        if (!l || l->card_type != CardType::Legend) {
            l = nullptr;
            for (const auto& [id, c] : db.all())
                if (c.card_type == CardType::Legend && nameMatches(it->second.legend_name, c.name)) { l = &c; break; }
        }
        if (!l) return 0;
        legends[i] = l;
    }
    int changed = 0;
    for (auto& e : game.events) {
        if (e.kind != "played" || !e.actor_inferred || !e.isCardPlay()) continue;
        const CardDef* d = resolveCard(db, e.card);
        if (!d || d->domains.empty()) continue;
        const int seat = e.actor == game.players[0] ? 0 : e.actor == game.players[1] ? 1 : -1;
        if (seat < 0) continue;
        if (fitsDomains(*d, *legends[seat]) || !fitsDomains(*d, *legends[1 - seat])) continue;
        e.actor = game.players[static_cast<size_t>(1 - seat)];
        e.actor_uncertain = false;
        ++changed;
    }
    return changed;
}

DeckInference inferDeck(const HumanGame& game, const std::string& player, const CardDB& db,
                        const std::vector<std::string>& candidate_lists) {
    DeckInference out;
    auto& notes = out.notes;
    notes["player"] = player;

    auto hit = game.legends.find(player);
    if (hit == game.legends.end())
        throw std::runtime_error("no legend header for player '" + player + "' in " + game.path);
    const LegendHeader& hdr = hit->second;

    const CardDef* legend = resolveCard(db, hdr.legend_name, hdr.legend_code);
    if (!legend || legend->card_type != CardType::Legend) {
        legend = nullptr;
        for (const auto& [id, c] : db.all())
            if (c.card_type == CardType::Legend && nameMatches(hdr.legend_name, c.name)) { legend = &c; break; }
    }
    if (!legend) throw std::runtime_error("legend not in card table: " + hdr.legend_name + " (" + hdr.legend_code + ")");
    const CardDef* champion = resolveCard(db, hdr.champion_name, hdr.champion_code);
    if (!champion || champion->card_type != CardType::Unit || champion->super_type != SuperType::Champion) {
        champion = nullptr;
        for (const auto& [id, c] : db.all())
            if (c.card_type == CardType::Unit && c.super_type == SuperType::Champion &&
                nameMatches(hdr.champion_name, c.name)) { champion = &c; break; }
    }
    if (!champion) throw std::runtime_error("champion not in card table: " + hdr.champion_name + " (" + hdr.champion_code + ")");
    notes["legend"] = legend->name;
    notes["champion"] = champion->name;

    // ── Cards the log attributes to this player ──
    struct Seen {
        const CardDef* def = nullptr;
        int plays_from_hand = 0, bounces = 0, hand_mult = 0, mentions = 0;
    };
    std::map<std::string, Seen> seen;  // by normalised name
    nlohmann::json unknown = nlohmann::json::array();
    auto collect = [&](const std::string& nm) -> Seen* {
        if (nm.empty()) return nullptr;
        const CardDef* def = resolveCard(db, nm);
        if (!def) {
            unknown.push_back(nm);
            return nullptr;
        }
        if (!isDeckCardType(*def)) return nullptr;
        auto& s = seen[normalizeName(def->name)];
        s.def = def;
        s.mentions++;
        return &s;
    };
    for (const auto& e : game.events) {
        if (e.actor != player) continue;
        if (e.kind == "played") {
            if (!e.isCardPlay()) continue;
            if (auto* s = collect(e.card))
                if (e.from.empty() || normalizeName(e.from) == "hand") s->plays_from_hand++;
        } else if (e.kind == "moved") {
            if (auto* s = collect(e.card))
                if (normalizeName(e.to) == "hand" && e.from.empty()) s->bounces++;
        } else if (e.kind == "hid_hand" || e.kind == "revealed_hand") {
            std::map<std::string, int> mult;
            for (const auto& c : e.cards) mult[normalizeName(c)]++;
            for (const auto& [n, k] : mult) {
                auto* s = collect(n);
                if (s) s->hand_mult = std::max(s->hand_mult, k);
            }
        } else if (e.kind == "revealed") {
            if (e.text.find("in their hand") != std::string::npos) collect(e.card);
        } else if (e.kind == "discarded" || e.kind == "killed" || e.kind == "equipped" ||
                   e.kind == "buff_applied" || e.kind == "exhausted" || e.kind == "readied") {
            collect(e.card);
        }
    }
    if (!unknown.empty()) notes["unknown_cards"] = unknown;

    std::map<std::string, int> counts;                 // normalised name -> copies
    std::map<std::string, const CardDef*> defs;        // normalised name -> def
    const std::string champ_key = normalizeName(champion->name);
    auto limitFor = [&](const std::string& key, const CardDef& d) {
        if (d.unlimited_copies) return 39;
        return key == champ_key ? 2 : 3;  // the chosen champion is the third copy
    };
    nlohmann::json seen_json = nlohmann::json::array(), dropped = nlohmann::json::array();
    int total = 0;
    std::vector<std::pair<std::string, int>> order;    // insertion order with evidence
    for (const auto& [key, s] : seen) {
        if (normalizeName(s.def->name) == normalizeName(legend->name)) continue;
        int copies;
        if (key == champ_key) copies = std::clamp(s.plays_from_hand - s.bounces, 0, 2);
        else copies = std::clamp(std::max({1, s.plays_from_hand - s.bounces, s.hand_mult}), 1, 3);
        if (copies == 0) continue;
        if (!fitsDomains(*s.def, *legend)) {
            dropped.push_back({{"card", s.def->name}, {"reason", "outside the legend's domains"}});
            continue;
        }
        copies = std::min(copies, limitFor(key, *s.def));
        counts[key] = copies;
        defs[key] = s.def;
        total += copies;
        order.emplace_back(key, s.mentions);
        seen_json.push_back({{"card", s.def->name}, {"copies", copies},
                             {"plays_from_hand", s.plays_from_hand}, {"hand_multiplicity", s.hand_mult}});
    }
    notes["seen"] = seen_json;
    if (!dropped.empty()) notes["dropped"] = dropped;
    // Too much evidence for 39 slots: trim the least-mentioned names.
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    for (const auto& [key, m] : order) {
        if (total <= 39) break;
        while (counts[key] > 0 && total > 39) { counts[key]--; total--; }
    }

    // ── Candidate lists ──
    struct Cand {
        std::string path;
        DeckSubmission deck;
        int score = 0;
        bool same_legend = false, same_domains = false;
    };
    std::vector<Cand> cands;
    for (const auto& path : candidate_lists) {
        Cand c;
        c.path = path;
        try {
            c.deck = DeckValidator::loadFromDeckList(path, db);
        } catch (const std::exception&) {
            continue;
        }
        const auto& l = db.get(c.deck.legend);
        c.same_legend = l.id == legend->id || nameMatches(l.name, legend->name);
        std::set<Domain> a(l.domains.begin(), l.domains.end()), b(legend->domains.begin(), legend->domains.end());
        c.same_domains = a == b;
        std::map<std::string, int> in_deck;
        for (auto id : c.deck.main_deck) in_deck[normalizeName(db.get(id).name)]++;
        in_deck[normalizeName(db.get(c.deck.chosen_champion).name)]++;
        for (const auto& [key, n] : counts) {
            auto it = in_deck.find(key);
            if (it != in_deck.end()) c.score += std::min(it->second, n);
        }
        cands.push_back(std::move(c));
    }
    const Cand* best = nullptr;
    for (int pass = 0; pass < 2 && !best; ++pass)
        for (const auto& c : cands) {
            if (pass == 0 ? !c.same_legend : !c.same_domains) continue;
            if (!best || c.score > best->score) best = &c;
        }
    std::vector<const CardDef*> filler;
    if (best) {
        notes["filler"] = {{"source", best->path}, {"overlap", best->score}, {"same_legend", best->same_legend}};
        for (auto id : best->deck.main_deck) filler.push_back(&db.get(id));
        for (auto id : best->deck.sideboard) filler.push_back(&db.get(id));
    } else {
        notes["filler"] = {{"source", "card table"}, {"overlap", 0}, {"same_legend", false}};
    }
    {
        std::vector<const CardDef*> generic;
        for (const auto& [id, c] : db.all())
            if (isDeckCardType(c) && !c.banned && c.super_type == SuperType::None && fitsDomains(c, *legend))
                generic.push_back(&c);
        std::sort(generic.begin(), generic.end(), [](const CardDef* a, const CardDef* b) {
            if (a->energy_cost != b->energy_cost) return a->energy_cost < b->energy_cost;
            if (a->power_cost != b->power_cost) return a->power_cost < b->power_cost;
            return a->name < b->name;
        });
        for (const auto* c : generic)
            for (int k = 0; k < 3; ++k) filler.push_back(c);
    }
    int filler_added = 0;
    for (const auto* c : filler) {
        if (total >= 39) break;
        if (!fitsDomains(*c, *legend) || c->banned) continue;
        const std::string key = normalizeName(c->name);
        if (counts[key] >= limitFor(key, *c)) continue;
        counts[key]++;
        defs[key] = c;
        total++;
        filler_added++;
    }
    notes["filler_cards"] = filler_added;
    if (total != 39) throw std::runtime_error("could not fill a 39-card deck for " + player);

    // ── Battlefields ──
    std::vector<const CardDef*> seen_bfs;
    auto noteBf = [&](const std::string& nm) {
        if (isLocationWord(nm)) return;
        const CardDef* d = resolveCard(db, nm);
        if (!d || d->card_type != CardType::Battlefield) return;
        if (std::find(seen_bfs.begin(), seen_bfs.end(), d) == seen_bfs.end()) seen_bfs.push_back(d);
    };
    for (const auto& e : game.events) {
        noteBf(e.battlefield);
        noteBf(e.to);
        noteBf(e.from);
    }
    const bool is_capture = !game.players.empty() && player == game.players[0];
    const size_t mine = is_capture ? 0 : 1, theirs = is_capture ? 1 : 0;
    std::vector<const CardDef*> bfs;
    auto addBf = [&](const CardDef* d) {
        if (!d || bfs.size() >= 3) return;
        for (const auto* b : bfs) if (b->name == d->name) return;
        bfs.push_back(d);
    };
    // Both players brought the same battlefield (a mirror match): one name
    // on the log stands for two battlefields. A player scores a battlefield
    // once per turn (CR 472), so the same player conquering that name twice
    // in one turn shows there are two of them.
    if (seen_bfs.size() == 1) {
        std::map<std::pair<std::string, std::string>, int> conquests;
        for (const auto& e : game.events) {
            if (e.kind == "ended_turn") conquests.clear();
            if (e.kind != "conquered" || e.battlefield.empty()) continue;
            if (++conquests[{e.actor, normalizeName(e.battlefield)}] >= 2 &&
                nameMatches(e.battlefield, seen_bfs[0]->name)) {
                seen_bfs.push_back(seen_bfs[0]);
                notes["battlefield_mirrored"] = seen_bfs[0]->name;
                break;
            }
        }
    }
    if (seen_bfs.size() > mine) {
        addBf(seen_bfs[mine]);
        out.preferred_battlefield = seen_bfs[mine]->name;
    }
    for (size_t i = 0; i < seen_bfs.size(); ++i)
        if (i != theirs) addBf(seen_bfs[i]);
    if (best) for (auto id : best->deck.battlefields) addBf(&db.get(id));
    {
        std::vector<const CardDef*> all_bfs;
        for (const auto& [id, c] : db.all()) if (c.card_type == CardType::Battlefield && !c.banned) all_bfs.push_back(&c);
        std::sort(all_bfs.begin(), all_bfs.end(), [](const CardDef* a, const CardDef* b) { return a->name < b->name; });
        for (const auto* b : all_bfs) addBf(b);
    }
    nlohmann::json bf_json = nlohmann::json::array();
    for (const auto* b : bfs) bf_json.push_back(b->name);
    notes["battlefields"] = bf_json;
    notes["battlefield_brought"] = out.preferred_battlefield;

    // ── Runes ──
    std::vector<const CardDef*> runes;
    auto runeFor = [&](Domain d) -> const CardDef* {
        for (const auto& [id, c] : db.all())
            if (c.card_type == CardType::Rune && c.hasDomain(d)) return &c;
        return nullptr;
    };
    bool runes_from_list = false;
    if (best && best->deck.rune_deck.size() == 12) {
        runes_from_list = true;
        for (auto id : best->deck.rune_deck) {
            const auto& r = db.get(id);
            if (!fitsDomains(r, *legend)) { runes_from_list = false; break; }
            runes.push_back(&r);
        }
        if (!runes_from_list) runes.clear();
    }
    if (!runes_from_list) {
        const int n = std::max<int>(1, static_cast<int>(legend->domains.size()));
        for (int i = 0; i < n; ++i) {
            const int share = 12 / n + (i < 12 % n ? 1 : 0);
            const CardDef* r = runeFor(legend->domains[i]);
            if (!r) throw std::runtime_error("no rune card for domain " + std::string(toString(legend->domains[i])));
            for (int k = 0; k < share; ++k) runes.push_back(r);
        }
    }
    notes["runes_from_list"] = runes_from_list;

    // ── Deck text, then parse it so the ids are exactly what the bot loads ──
    std::vector<CardDefId> main_ids, bf_ids, rune_ids;
    for (const auto& [key, n] : counts)
        for (int k = 0; k < n; ++k) main_ids.push_back(defs.at(key)->id);
    for (const auto* b : bfs) bf_ids.push_back(b->id);
    for (const auto* r : runes) rune_ids.push_back(r->id);
    std::ostringstream text;
    text << "# Inferred from " << game.stem << " game " << game.game_number << " for " << player << "\n"
         << "# filler: " << notes["filler"]["source"].get<std::string>() << "\n"
         << "Legend:\n1 " << legend->name << "\n\n"
         << "Champion:\n1 " << champion->name << "\n\n"
         << "MainDeck:\n" << sectionLines(db, main_ids) << "\n"
         << "Battlefields:\n" << sectionLines(db, bf_ids) << "\n"
         << "Runes:\n" << sectionLines(db, rune_ids);
    out.deck_text = text.str();
    std::istringstream in(out.deck_text);
    out.deck = DeckValidator::parseDeckList(in, "inferred deck for " + player, db);
    for (auto id : out.deck.battlefields) out.battlefield_names.push_back(db.get(id).name);

    DeckValidator validator(db);
    auto v = validator.validate(out.deck);
    notes["legal"] = v.is_legal;
    if (!v.is_legal) notes["validation_errors"] = v.errors;
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// God-mode helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

void fillFromDef(GameObject& obj, const CardDef& def, PlayerId owner) {
    obj.card_def_id = def.id;
    obj.owner = owner;
    obj.controller = owner;
    obj.name = def.name;
    obj.card_type = def.card_type;
    obj.super_type = def.super_type;
    obj.tags = def.tags;
    obj.domains = def.domains;
    obj.base_might = def.might;
    obj.current_might = def.might;
    obj.keywords = def.keywords;
    obj.assault_value = def.assault_value;
    obj.shield_value = def.shield_value;
    obj.deflect_value = def.deflect_value;
    obj.might_bonus = def.might_bonus;
}

}  // namespace

std::string ensureCardIn(GameState& state, const CardDB& db, PlayerId player, CardDefId def_id,
                         ZoneType zone, const std::set<std::string>& keep_names,
                         const std::set<GameObjectId>& protect) {
    auto& ps = state.player(player);
    std::vector<GameObjectId>& target = zone == ZoneType::Trash ? ps.trash : ps.hand;
    const CardDef& def = db.get(def_id);
    const std::string key = normalizeName(def.name);
    for (auto id : target)
        if (state.objectExists(id) && normalizeName(state.getObject(id).name) == key) return "already";

    auto place = [&](GameObjectId id) {
        auto& o = state.getObject(id);
        o.zone = zone;
        o.location.reset();
        o.is_exhausted = false;
        o.combat_designation = CombatDesignation::None;
        target.push_back(id);
    };

    // From the main deck (top first — the card the human drew most recently).
    for (auto it = ps.main_deck.rbegin(); it != ps.main_deck.rend(); ++it) {
        const auto id = *it;
        if (!state.objectExists(id) || normalizeName(state.getObject(id).name) != key) continue;
        ps.main_deck.erase(std::next(it).base());
        place(id);
        if (zone == ZoneType::Hand) {
            // Keep the hand size: a card the log never shows goes back under the deck.
            for (size_t k = 0; k < ps.hand.size(); ++k) {
                const auto hid = ps.hand[k];
                if (hid == id || protect.count(hid) || !state.objectExists(hid)) continue;
                auto& ho = state.getObject(hid);
                if (keep_names.count(normalizeName(ho.name))) continue;
                ps.hand.erase(ps.hand.begin() + static_cast<long>(k));
                ho.zone = ZoneType::MainDeck;
                ho.location.reset();
                ps.main_deck.insert(ps.main_deck.begin(), hid);
                break;
            }
        }
        return "pulled";
    }
    // Not in the deck at all: conjure one.
    const auto id = state.createObject();
    fillFromDef(state.getObject(id), def, player);
    place(id);
    return "created";
}

uint64_t seedForFirstPlayer(uint64_t first, PlayerId first_player) {
    if (first == 0) first = 1;
    for (uint64_t s = first; s < first + 1024; ++s) {
        std::mt19937_64 rng;
        rng.seed(s);
        std::uniform_int_distribution<int> coin(0, 1);  // mirrors GameEngine::determineTurnOrder
        const PlayerId p = coin(rng) == 0 ? PlayerId::Player1 : PlayerId::Player2;
        if (p == first_player) return s;
    }
    return first;
}

// ═══════════════════════════════════════════════════════════════════════════
// Driver
// ═══════════════════════════════════════════════════════════════════════════

namespace {

bool isPlayType(IntentType t) {
    return t == IntentType::PlayCard || t == IntentType::PlayReaction || t == IntentType::PlayActionCard;
}
bool isAbilityType(IntentType t) {
    return t == IntentType::ActivateAbility || t == IntentType::ActivateReactionAbility ||
           t == IntentType::ActivateActionAbility;
}
bool isForcedType(IntentType t) {
    return t == IntentType::MakeChoice || t == IntentType::AssignCombatDamage ||
           t == IntentType::PlaceOptionalTrigger || t == IntentType::DeclineOptionalTrigger ||
           t == IntentType::PayTriggeredCost || t == IntentType::DeclineTriggeredCost ||
           t == IntentType::SideboardSwap;
}

std::string kindLabel(const Intent& i) {
    switch (i.type) {
    case IntentType::PlayCard: case IntentType::PlayReaction: case IntentType::PlayActionCard: return "play";
    case IntentType::StandardMove: return "move";
    case IntentType::EndTurn: return "end_turn";
    case IntentType::PassFocus: return "pass_focus";
    case IntentType::PassPriority: return "pass_priority";
    case IntentType::ActivateAbility: case IntentType::ActivateReactionAbility:
    case IntentType::ActivateActionAbility: return "ability";
    case IntentType::MulliganDecision: return "mulligan";
    case IntentType::ChooseBattlefield: return "battlefield";
    case IntentType::HideCard: return "hide";
    case IntentType::MakeChoice: return "choice";
    case IntentType::AssignCombatDamage: return "combat_damage";
    case IntentType::PlaceOptionalTrigger: case IntentType::DeclineOptionalTrigger:
    case IntentType::PayTriggeredCost: case IntentType::DeclineTriggeredCost: return "trigger";
    default: return "other";
    }
}

std::string objName(const GameState& s, GameObjectId id) {
    if (id == kInvalidId || !s.objectExists(id)) return "";
    return s.getObject(id).name;
}

std::string bfName(const GameState& s, BattlefieldId bf) {
    for (const auto& b : s.battlefields)
        if (b.id == bf) return objName(s, b.card_object_id);
    return "";
}

/// `to` of a log line ("base" or a battlefield name) names `loc`.
bool locMatches(const GameState& s, const std::optional<LocationId>& loc, const std::string& to) {
    if (!loc) return false;
    if (normalizeName(to) == "base") return std::holds_alternative<BaseLocation>(*loc);
    const auto* bl = std::get_if<BattlefieldLocation>(&*loc);
    return bl && nameMatches(to, bfName(s, bl->id));
}

/// The snapshot the bot searches from is exact only at a clean boundary; in
/// flight (chain, showdown, combat, cost payment, mulligan) the OpenSpiel
/// wrapper replays action_history instead, which does not see god-mode edits.
bool cleanBoundary(const GameState& s) {
    if (s.game_over) return false;
    if (s.turn.phase == TurnPhase::Setup || s.turn.phase == TurnPhase::Mulligan) return false;
    if (s.turn.oc_state != OpenClosedState::Open) return false;
    if (s.turn.ns_state != NeutralShowdownState::Neutral) return false;
    if (s.chain.exists() || s.cost_cursor.has_value()) return false;
    for (const auto& bf : s.battlefields)
        if (bf.combat_in_progress || bf.showdown_in_progress || bf.combat_staged || bf.showdown_staged)
            return false;
    return true;
}

struct Match {
    static constexpr int kAlreadyDone = -2;  // the line describes what an engine effect already did
    int index = -1;
    std::string why;               // when index < 0
    std::vector<int> also;         // further log lines this one intent covers (a group move)
    std::string effect_name;       // a card the chosen intent's effect then picks (lookahead evidence)
};

class Driver {
public:
    Driver(const HumanGame& game, const DeckInference& p1, const DeckInference& p2, const CardDB& db,
           const CardRegistry& registry, AgentInterface* bot, const GuidedOptions& opts)
        : game_(game), decks_{&p1, &p2}, db_(db), registry_(registry), bot_(bot), opts_(opts),
          consumed_(game.events.size(), 0), matched_(game.events.size(), 0) {
        addSourcesBeforePlays();
        // Whose turn each log line falls in, from the log's own turn ends
        // ("<X> queued an extra turn" makes X's next turn follow X's).
        std::string current = game_.first_player;
        std::set<std::string> extra;
        turn_of_.reserve(game_.events.size());
        for (const LogLine& e : game_.events) {
            turn_of_.push_back(current);
            if (e.kind == "extra_turn" && !e.actor.empty()) extra.insert(e.actor);
            if (e.kind == "ended_turn" && game_.players.size() == 2) {
                if (extra.erase(e.actor)) current = e.actor;
                else current = e.actor == game_.players[0] ? game_.players[1] : game_.players[0];
            }
        }
    }

    GuidedResult run();

private:
    /// "<X> played Nasus from hand", "<X> exhausted 1 Calm rune" ×5, "<X>
    /// moved Gold from base to trash": a capture that logs the play first
    /// lists the [Add] sources used to pay it among the payment lines after
    /// it (CR 430.3: an Add Reaction can be used while paying). The engine
    /// pays a card's cost from the Rune Pool as it is played, so each such
    /// source is activated right before the play instead: its line moves
    /// there, a Gold token's sacrifice as "<X> exhausted Gold". Returns how
    /// many lines moved.
    int addSourcesBeforePlays() {
        int moved = 0;
        auto addSource = [&](const LogLine& f) -> bool {
            std::string name = f.card;
            if (f.kind == "killed") {
                if (normalizeName(f.from) != "base" && f.text.find(" from base to trash") == std::string::npos) return false;
            } else if (f.kind != "exhausted" || name.empty() || isRuneLine(f)) {
                return false;
            }
            const CardDef* d = resolveCard(db_, name);
            const Card* c = d ? registry_.get(d->id) : nullptr;
            if (!c) return false;
            for (const auto& ab : c->activatedAbilities()) if (ab.adds_resources) return true;
            return false;
        };
        for (size_t i = 0; i < game_.events.size(); ++i) {
            const LogLine& play = game_.events[i];
            bool is_play = play.isCardPlay();
            if (!is_play && play.kind == "moved" && !play.actor.empty()) {
                auto it = game_.legends.find(play.actor);
                is_play = it != game_.legends.end() && !it->second.champion_name.empty() &&
                          nameMatches(play.card, it->second.champion_name);
            }
            if (!is_play) continue;
            // The payment run after the play: rune lines and add sources of the
            // same player, nothing else.
            std::vector<size_t> sources;
            size_t j = i + 1;
            bool runes = false;
            for (; j < game_.events.size(); ++j) {
                const LogLine& f = game_.events[j];
                if (f.actor != play.actor) break;
                if (isPaymentLine(f) && isRuneLine(f)) { runes = true; continue; }
                if (addSource(f)) { sources.push_back(j); continue; }
                break;
            }
            if (!runes || sources.empty()) continue;
            size_t at = i;
            for (size_t src : sources) {
                LogLine line = game_.events[src];
                if (line.kind == "killed") {
                    line.kind = "exhausted";
                    line.from.clear();
                    line.to.clear();
                    line.text = line.actor + " exhausted " + line.card + ". (used for the payment)";
                }
                game_.events.erase(game_.events.begin() + static_cast<long>(src));
                game_.events.insert(game_.events.begin() + static_cast<long>(at), std::move(line));
                ++at;
                ++moved;
            }
            i = j;
        }
        return moved;
    }

    // ── log cursor ──
    PlayerId seatOf(const std::string& actor) const {
        if (game_.players.size() > 0 && actor == game_.players[0]) return PlayerId::Player1;
        if (game_.players.size() > 1 && actor == game_.players[1]) return PlayerId::Player2;
        return PlayerId::None;
    }
    static int seatIndex(PlayerId p) { return p == PlayerId::Player1 ? 1 : p == PlayerId::Player2 ? 2 : 0; }
    bool isDecisionEvent(const LogLine& e, const std::vector<Intent>& legal, const GameState& s) const;
    void consumeNonDecision(const LogLine& e, const GameState& s, const std::vector<Intent>& legal);
    int nextPendingDecision(const std::vector<Intent>& legal, const GameState& s);
    int findInWindow(int after, PlayerId player, const std::vector<Intent>& legal, const GameState& s, Match& m);
    int focusPassInWindow(int from, PlayerId player, const std::vector<Intent>& legal, const GameState& s) const;
    int focusPassAtBattlefield(int from, PlayerId player, const std::vector<Intent>& legal, const GameState& s) const;
    static std::string focusBattlefield(const GameState& s);
    std::vector<std::pair<std::string, int>> lookaheadNames() const;
    std::set<std::string> upcomingPlays(PlayerId player, int max_plays) const;
    std::string hiddenCardName(const LogLine& e) const;
    Match matchEvent(const LogLine& e, const std::vector<Intent>& legal, const GameState& s, PlayerId p);
    int chooseForced(const std::vector<Intent>& legal, const GameState& s, bool& known, std::string& note);
    int paymentChoice(const std::vector<Intent>& legal, const GameState& s, bool& known) const;
    struct LoggedPayment {
        int energy = 0, power = 0;           // what the payment lines add up to
        std::vector<std::string> recycled;   // normalised domain of each recycled rune
        std::vector<std::string> exhausted;  // normalised domain of each rune exhausted for Energy
        bool ready_recycled = false;         // "(+1E)": a ready rune was recycled
        bool any = false;
    };
    LoggedPayment paymentAround(PlayerId payer) const;
    int choiceEvidence(const Intent& in, const GameState& s) const;
    int effectPlayEvidence(const Intent& in, PlayerId who) const;
    int relocationAfterPlay(const LogLine& e, bool logged_location_illegal = false) const;
    int sameNameDestinationScore(const LogLine& e, const GameState& s, PlayerId p,
                                 const std::optional<LocationId>& dest) const;
    std::vector<std::string> effect_names_;   // cards an effect just played (lookahead evidence)
    int combatEvidence(const Intent& in, const GameState& s) const;
    std::vector<const LogLine*> upcomingLinesOf(PlayerId player, int max_lines, bool stop_at_decision) const;
    int passLike(const std::vector<Intent>& legal) const;
    int scoreIntent(const Intent& in, const GameState& s, const std::vector<std::pair<std::string, int>>& preferred) const;
    void prefetch(GameEngine& engine, const std::vector<Intent>& legal);
    void fixRuneDomains(GameState& ms, PlayerId p, const std::set<GameObjectId>& protect);
    void desync(const LogLine& e, const std::vector<Intent>& legal, const GameState& s, const std::string& reason);

    HumanGame game_;                 // a copy: uncertain actors are settled as the replay runs
    const DeckInference* decks_[2];
    const CardDB& db_;
    const CardRegistry& registry_;
    AgentInterface* bot_;
    GuidedOptions opts_;

    std::vector<char> consumed_;
    std::vector<char> matched_;      // consumed as a decision line (or undone / desynced)
    int last_matched_ = -1;          // evidence for engine-side choices starts after this line
    int prev_matched_ = -1;          // the decision line matched before it
    std::vector<std::string> turn_of_;   // the log's turn player at each line
    PlayerId current_player_ = PlayerId::None;  // whose decision the engine is asking for
    size_t cursor_ = 0;
    void markDecision(int i, bool snapshot = true) {
        consumed_[i] = 1;
        matched_[i] = 1;
        if (i > last_matched_) {
            prev_matched_ = last_matched_;
            last_matched_ = i;
        }
        // A line read as what an effect already did keeps the snapshot of the
        // decision before it: the effect's other moves (Emperor's Divide
        // sending three units home) are measured against it too.
        if (snapshot) snapshotLocations();
        effect_names_.clear();
        effect_location_.clear();
    }
    std::string effect_location_;   // where an effect just played a card (a "where to" choice)
    // Where each player's units and gear were (by name) when the last log line
    // was matched: a later "<X> moved U to D" whose U the engine has since put
    // at D by an effect (Star Spring, a Zhonya's recall, a [Temporary]
    // gear's repeated play effect) describes that effect, not a move.
    std::map<std::tuple<int, std::string, std::string>, int> locations_at_match_;
    static std::string locKey(const GameState& s, const GameObject& o) {
        if (const auto bf = o.battlefieldId()) return "bf:" + normalizeName(bfName(s, *bf));
        return "base";
    }
    void snapshotLocations() {
        if (!mutable_state_) return;
        locations_at_match_.clear();
        for (const auto& [id, o] : mutable_state_->objects) {
            if (!o.location.has_value() || o.isRune() || !(o.isUnit() || o.isGear())) continue;
            ++locations_at_match_[{seatIndex(o.controller), normalizeName(o.name), locKey(*mutable_state_, o)}];
        }
    }
    bool alreadyMovedByEffect(const LogLine& e, const GameState& s, PlayerId p) const {
        const std::string to = normalizeName(e.to);
        if (to.empty() || e.card.empty()) return false;
        const std::string key = to == "base" ? std::string("base") : "bf:" + to;
        int now = 0;
        for (const auto& [id, o] : s.objects) {
            if (o.controller != p || !o.location.has_value() || o.isRune()) continue;
            if (!nameMatches(e.card, o.name) || !locMatches(s, o.location, e.to)) continue;
            ++now;
        }
        if (now == 0) return false;
        int then = 0;
        for (const auto& [k, n] : locations_at_match_)
            if (std::get<0>(k) == seatIndex(p) && std::get<2>(k) == key && nameMatches(e.card, std::get<1>(k)))
                then += n;
        return now > then;
    }
    int effect_lines_ = 0;   // log lines read as what an engine effect already did
    int deck_tops_ = 0;      // cards put on top of a Main Deck for an effect the log shows taking them
    int soft_dropped_ = 0;   // soft lines (plays from face down) the engine could not take where logged
    std::set<std::string> deck_top_names_;   // ... their names: a "draw one of them" choice leaves them
    size_t evidenceStart() const { return static_cast<size_t>(last_matched_ + 1); }
    std::vector<std::pair<PlayerId, CardDefId>> pending_hand_;

    GameState* mutable_state_ = nullptr;  // the engine's state, for forced readiness
    const GameEngine* engine_ = nullptr;  // for the engine's play costs (artefact classifier)

    // ── Log-artefact classifier ──
    // Units the log itself shows becoming exhausted: the line (position in
    // game_.events) of the standard move or play that exhausted each object
    // (CR 144.2: a standard move exhausts the unit; CR 143.4: a unit enters
    // the board exhausted), or of an "<X> exhausted <unit>" line.
    std::map<GameObjectId, size_t> log_exhausted_at_;
    int log_artefacts_ = 0;
    void noteLogExhausted(GameObjectId id, size_t line) {
        if (mutable_state_ && mutable_state_->objectExists(id) && mutable_state_->getObject(id).is_exhausted)
            log_exhausted_at_[id] = line;
    }
    bool classifyArtefact(const LogLine& e, const GameState& s, const std::string& reason,
                          std::string& rule) const;
    int loggedPaymentOf(size_t pos, int& energy, int& power, bool& clean) const;
    // A play this turn whose logged payment was below the engine's cost for
    // it, per seat (1/2): the turn, its line and a note.
    struct Underpaid { int turn = -1; size_t line = 0; std::string text; };
    std::map<int, Underpaid> underpaid_;

    // stats
    int decisions_ = 0, desyncs_ = 0, silent_passes_ = 0, out_of_order_ = 0, forced_ = 0;
    int rewinds_ = 0, rewinds_ignored_ = 0, readiness_fixes_ = 0, actors_settled_ = 0;
    int score_mismatches_ = 0, pulled_ = 0, created_ = 0, bot_calls_ = 0, bot_fallbacks_ = 0, bot_errors_ = 0;
    int rune_swaps_ = 0;
    double bot_seconds_ = 0;
    nlohmann::json desync_list_ = nlohmann::json::array();
    nlohmann::json score_mismatch_list_ = nlohmann::json::array();
    nlohmann::json hand_fixes_ = nlohmann::json::array();
    std::map<std::string, int> desync_causes_;
    std::string stop_reason_;
    const GameState* state_for_scores_ = nullptr;
    std::vector<ml::ValueSample> export_samples_;   // GuidedOptions::export_features_path
};

bool Driver::isDecisionEvent(const LogLine& e, const std::vector<Intent>& legal, const GameState& s) const {
    const std::string& k = e.kind;
    // "played <X> token to <bf>" is an effect creating a token, not a play.
    if (k == "played") return e.isCardPlay();
    if (k == "moved") {
        // "moved X to hand / trash / banished / deck" are effects (a bounce, a
        // sacrifice, a legend banishing a card from the trash), never a move.
        {
            const std::string to = normalizeName(e.to);
            if (to == "hand" || to == "trash" || to.rfind("deck", 0) == 0 || to.rfind("bottom", 0) == 0 ||
                to.rfind("top of", 0) == 0 || to == "banished" || to == "banishment")
                return false;
        }
        // A Standard Move is the turn player's, in their Main Phase (CR
        // 144.1.a). The other player's unit "moving" is an effect: Charm or
        // Blitzcrank pulling it, Tideturner's swap, Zhonya's recall. Only a
        // play logged as a move (a champion from the Champion Zone) can be
        // that player's own action, and only when the engine offers it.
        {
            const size_t pos = static_cast<size_t>(&e - game_.events.data());
            if (pos < turn_of_.size() && !turn_of_[pos].empty() && !e.actor.empty() &&
                e.actor != turn_of_[pos]) {
                for (const auto& in : legal)
                    if (isPlayType(in.type) && nameMatches(e.card, objName(s, in.card))) return true;
                return false;
            }
        }
        // Gear "moves to base" by itself when the unit it was attached to
        // dies; a gear move is a decision only when the engine offers one
        // (gear turned into a unit). A unit's move (or a champion / Ambush
        // play logged as a move) always is.
        const CardDef* d = resolveCard(db_, e.card);
        if (!d || d->card_type == CardType::Unit) return true;
        for (const auto& in : legal)
            if (in.type == IntentType::StandardMove && in.units_to_move.size() == 1 &&
                nameMatches(e.card, objName(s, in.units_to_move[0])))
                return true;
        // A gear a legend "made a unit" (Orianna) moves like a unit from
        // then on, even when the engine has not caught up yet at scan time.
        for (const LogLine& f : game_.events) {
            if (f.index >= e.index) break;
            if (f.kind == "made_unit" && f.actor == e.actor && nameMatches(f.card, e.card)) return true;
        }
        for (const auto& [id, o] : s.objects)
            if (o.isGear() && o.isUnit() && o.controller == seatOf(e.actor) && nameMatches(e.card, o.name)) return true;
        return false;
    }
    if (k == "ended_turn" || k == "passed_focus" || k == "showdown" ||
        k == "hidden_card_placed" || k == "equipped" || k == "battlefield_locked")
        return true;
    if (k == "made_unit") {
        for (const auto& in : legal)
            if (isAbilityType(in.type) && !in.targets.empty() && nameMatches(e.card, objName(s, in.targets[0])))
                return true;
        return false;
    }
    if (k == "mulligan") return e.text.find("finalized") != std::string::npos;
    if (k == "exhausted" && !e.card.empty() && e.text.find(" rune") == std::string::npos) {
        for (const auto& in : legal)
            if (isAbilityType(in.type) && nameMatches(e.card, objName(s, in.ability_source))) return true;
        // "<X> exhausted <legend>" is an activation. While the engine is in
        // flight (a chain to resolve, the beginning phase) the ability is not
        // offered yet: keep the line pending rather than consuming it as a
        // state change, which would force the legend exhausted before its
        // ability could be used.
        if (const CardDef* d = resolveCard(db_, e.card); d && d->card_type == CardType::Legend) {
            bool neutral = s.turn.phase == TurnPhase::Setup || s.turn.phase == TurnPhase::Mulligan;
            for (const auto& in : legal)
                if (in.type == IntentType::EndTurn || in.type == IntentType::PassFocus) { neutral = true; break; }
            if (!neutral) return true;
        }
    }
    return false;
}

void Driver::consumeNonDecision(const LogLine& e, const GameState& s, const std::vector<Intent>& legal) {
    const PlayerId actor = seatOf(e.actor);
    const bool unit_line = !e.card.empty() && e.text.find(" rune") == std::string::npos;
    if ((e.kind == "readied" || e.kind == "exhausted") && unit_line && actor != PlayerId::None) {
        // Card effects the engine may not reproduce (Riven readying herself,
        // a unit exhausted to pay a cost): force the recorded readiness so
        // the next recorded move is legal. An object the current options
        // refer to is left alone — the option list was built from its state.
        std::set<GameObjectId> referenced;
        for (const auto& in : legal) {
            referenced.insert(in.card);
            referenced.insert(in.ability_source);
            for (auto id : in.units_to_move) referenced.insert(id);
            for (auto id : in.targets) referenced.insert(id);
        }
        const bool exhausted = e.kind == "exhausted";
        // "<X> exhausted Scuttle Crab", "<X> equipped Blade of the Exile to
        // Astral Heron": the unit is exhausted to pay the equip ("[Equip] —
        // [A] or exhaust a friendly unit"); the equip's own match reads that
        // line as its cost, which needs the unit still ready.
        if (exhausted) {
            const size_t pos = static_cast<size_t>(&e - game_.events.data());
            for (size_t j = pos + 1; j < game_.events.size() && j <= pos + 3; ++j) {
                const LogLine& f = game_.events[j];
                if (f.actor != e.actor) continue;
                if (f.kind == "equipped") return;
                if (!isPaymentLine(f)) break;
            }
        }
        for (auto& [id, o] : mutable_state_->objects) {
            if (o.controller != actor || o.is_exhausted == exhausted) continue;
            if (!o.location.has_value() && o.zone != ZoneType::LegendZone) continue;
            if (!(o.isUnit() || o.card_type == CardType::Legend) || !nameMatches(e.card, o.name)) continue;
            if (exhausted && referenced.count(id)) continue;
            // A legend with no activated ability is exhausted only as the cost
            // of its own triggered ability ("you may exhaust me to channel 1
            // rune", Grand Duelist), which the engine pays when the trigger
            // is finalized; the capture can log that before the event that
            // triggers it. Exhausting it here would deny the trigger its cost.
            if (exhausted && o.card_type == CardType::Legend && o.card_def_id != kInvalidId) {
                const Card* lc = registry_.get(o.card_def_id);
                if (lc && lc->activatedAbilities().empty()) continue;
            }
            o.is_exhausted = exhausted;
            ++readiness_fixes_;
            if (exhausted) noteLogExhausted(id, static_cast<size_t>(&e - game_.events.data()));
            break;
        }
    }
    // "<X> played Recruit token to base": a unit token enters exhausted
    // (CR 143.4) unless its effect says ready; the newest such token.
    if (e.kind == "played" && e.token && actor != PlayerId::None && mutable_state_) {
        GameObjectId newest = kInvalidId;
        for (const auto& [id, o] : mutable_state_->objects)
            if (o.controller == actor && o.location.has_value() && o.isUnit() && o.is_token &&
                nameMatches(e.card, o.name) && (newest == kInvalidId || id > newest))
                newest = id;
        if (newest != kInvalidId && !log_exhausted_at_.count(newest))
            noteLogExhausted(newest, static_cast<size_t>(&e - game_.events.data()));
    }
    if (e.kind == "scored" && e.score_to >= 0 && actor != PlayerId::None) {
        const int engine_score = s.player(actor).score;
        if (engine_score != e.score_to) {
            ++score_mismatches_;
            if (score_mismatch_list_.size() < 10)
                score_mismatch_list_.push_back({{"event_index", e.index}, {"text", e.text},
                                                {"engine_score", engine_score}, {"log_score", e.score_to},
                                                {"turn", s.turn.turn_number}});
        }
    } else if (actor != PlayerId::None) {
        auto queue = [&](const std::string& nm) {
            const CardDef* d = resolveCard(db_, nm);
            if (d && isDeckCardType(*d)) pending_hand_.emplace_back(actor, d->id);
        };
        if (e.kind == "revealed" && e.text.find("in their hand") != std::string::npos) queue(e.card);
        else if (e.kind == "hid_hand") for (const auto& c : e.cards) queue(c);
        else if (e.kind == "moved" && normalizeName(e.to) == "hand" && normalizeName(e.from) == "deck") queue(e.card);
    }
}

int Driver::nextPendingDecision(const std::vector<Intent>& legal, const GameState& s) {
    while (true) {
        while (cursor_ < consumed_.size() && consumed_[cursor_]) ++cursor_;
        int found = -1;
        for (size_t i = cursor_; i < game_.events.size(); ++i) {
            if (consumed_[i]) continue;
            const LogLine& e = game_.events[i];
            if (isDecisionEvent(e, legal, s)) { found = static_cast<int>(i); break; }
            consumed_[i] = 1;
            consumeNonDecision(e, s, legal);
        }
        if (found < 0) return -1;
        // "<actor> rewound to before their last action": the decision line
        // right before it never happened. Only a line the rewind directly
        // follows is undone: when draws, chain resolutions or readies of the
        // same player sit in between, the action's effects were already
        // public and the client undid something smaller (the Scuttle Crab a
        // player "rewound" after its draw was still exhausted for Life from
        // Stone two turns later; an Astral Heron "rewound" after it readied
        // was equipped the next turn).
        // The other player's focus pass in answer to that last action (the
        // showdown it closed) is undone with it: "<X> passed focus at P",
        // "<Y> passed focus at P", "<X> rewound to before their last action",
        // then X plays a spell in that showdown.
        const std::string& actor = game_.events[found].actor;
        int last = found, prev_any = found, rewind = -1;
        bool adjacent = false;
        std::vector<int> responses;
        for (size_t j = static_cast<size_t>(found) + 1; j < game_.events.size() && j < static_cast<size_t>(found) + 12; ++j) {
            if (consumed_[j]) continue;
            const LogLine& e2 = game_.events[j];
            if (e2.kind == "rewound") {
                if (e2.actor == actor) {
                    rewind = static_cast<int>(j);
                    adjacent = prev_any == last ||
                               (!responses.empty() && prev_any == responses.back() &&
                                game_.events[static_cast<size_t>(last)].kind == "passed_focus");
                }
                break;
            }
            if (isDecisionEvent(e2, legal, s)) {
                if (e2.actor != actor) {
                    const bool answer = (e2.kind == "passed_focus" || e2.kind == "showdown") &&
                                        game_.events[static_cast<size_t>(last)].kind == "passed_focus" &&
                                        nameMatches(e2.battlefield, game_.events[static_cast<size_t>(last)].battlefield);
                    if (!answer) break;
                    responses.push_back(static_cast<int>(j));
                } else {
                    if (!responses.empty()) break;
                    last = static_cast<int>(j);
                }
            } else if (!responses.empty()) {
                break;
            }
            prev_any = static_cast<int>(j);
        }
        if (rewind < 0) return found;
        consumed_[rewind] = 1;
        if (!adjacent) { ++rewinds_ignored_; return found; }
        markDecision(last);
        if (prev_any != last) for (int r : responses) markDecision(r);
        ++rewinds_;
        if (last == found) continue;  // the found line was the one undone: look again
        return found;
    }
}

int Driver::findInWindow(int after, PlayerId player, const std::vector<Intent>& legal, const GameState& s, Match& m) {
    for (size_t j = static_cast<size_t>(after) + 1; j < game_.events.size(); ++j) {
        if (consumed_[j]) continue;
        const LogLine& e = game_.events[j];
        if (e.kind == "ended_turn") break;  // the window is the rest of this turn
        if (seatOf(e.actor) != player || !isDecisionEvent(e, legal, s)) continue;
        m = matchEvent(e, legal, s, player);
        if (m.index >= 0) return static_cast<int>(j);
    }
    return -1;
}

/// The engine asks `player` for focus at a showdown; the player's next
/// unconsumed decision line, skipping the other player's lines and anything
/// that is not a decision, is a focus pass at that same battlefield: its
/// index, else -1. Scanning starts at `from` (a pending line of the other
/// player) and ends at the turn's end.
std::string Driver::focusBattlefield(const GameState& s) {
    for (const auto& b : s.battlefields)
        if (b.showdown_in_progress || b.combat_in_progress) return bfName(s, b.id);
    return "";
}

/// The engine asks `player` for focus at a showdown and the player's pending
/// line is something a showdown does not allow (a move): the player's focus
/// pass at this showdown's battlefield later in the turn, before the
/// battlefield is conquered, is this decision (RiftLite logs moves to two
/// battlefields before the first showdown's passes). Its index, else -1.
int Driver::focusPassAtBattlefield(int from, PlayerId player, const std::vector<Intent>& legal,
                                   const GameState& s) const {
    bool focus = false;
    for (const auto& in : legal) focus |= in.type == IntentType::PassFocus;
    if (!focus) return -1;
    const std::string bf = focusBattlefield(s);
    if (bf.empty()) return -1;
    for (size_t j = static_cast<size_t>(from); j < game_.events.size(); ++j) {
        if (consumed_[j]) continue;
        const LogLine& e = game_.events[j];
        if (e.kind == "ended_turn") break;
        if (e.kind == "conquered" && nameMatches(e.battlefield, bf)) break;
        if (seatOf(e.actor) != player) continue;
        if ((e.kind == "passed_focus" || e.kind == "showdown") && nameMatches(e.battlefield, bf))
            return static_cast<int>(j);
    }
    return -1;
}

int Driver::focusPassInWindow(int from, PlayerId player, const std::vector<Intent>& legal, const GameState& s) const {
    bool focus = false;
    for (const auto& in : legal) focus |= in.type == IntentType::PassFocus;
    if (!focus) return -1;
    const std::string bf = focusBattlefield(s);
    if (bf.empty()) return -1;
    for (size_t j = static_cast<size_t>(from); j < game_.events.size(); ++j) {
        if (consumed_[j]) continue;
        const LogLine& e = game_.events[j];
        if (e.kind == "ended_turn") break;
        if (seatOf(e.actor) != player || !isDecisionEvent(e, legal, s)) continue;
        if ((e.kind == "passed_focus" || e.kind == "showdown") && nameMatches(e.battlefield, bf))
            return static_cast<int>(j);
        return -1;   // the player's next decision is something else
    }
    return -1;
}

/// Card names the next log lines touch, with a weight: units that die,
/// are discarded or banished (3) say most about what a spell targeted; units
/// exhausted, readied, revealed, buffed, bounced or equipped (2) next; a unit
/// the deciding player moves or a token they play (1) hints at buffs and
/// effect moves. Lines already consumed since the last decision line still
/// count: they are the effects of what is being decided now.
std::vector<std::pair<std::string, int>> Driver::lookaheadNames() const {
    std::vector<std::pair<std::string, int>> names;
    for (const auto& n : effect_names_) names.emplace_back(n, 3);
    // Discards logged just before the last matched line (see choiceEvidence).
    for (int i = last_matched_ - 1; i > prev_matched_ && i >= 0 && i >= last_matched_ - 6; --i) {
        const LogLine& e = game_.events[static_cast<size_t>(i)];
        if (!matched_[static_cast<size_t>(i)] && e.kind == "discarded" && !e.card.empty())
            names.emplace_back(e.card, 3);
    }
    int seen = 0;
    bool past_focus = false;   // kills after a focus pass are a showdown's or combat's
    for (size_t i = evidenceStart(); i < game_.events.size() && seen < opts_.lookahead; ++i) {
        if (matched_[i]) continue;
        ++seen;
        const LogLine& e = game_.events[i];
        if (e.kind == "passed_focus" || e.kind == "showdown") past_focus = true;
        if (e.card.empty() && e.kind != "equipped") continue;
        if (e.kind == "killed" || e.kind == "discarded" || e.kind == "banished")
            names.emplace_back(e.card, past_focus && e.kind == "killed" ? 1 : 3);
        else if (e.kind == "exhausted" || e.kind == "readied" || e.kind == "revealed" ||
                 e.kind == "buff_applied" || (e.kind == "moved" && normalizeName(e.to) == "hand"))
            names.emplace_back(e.card, 2);
        else if ((e.kind == "moved" || (e.kind == "played" && e.token)) && seatOf(e.actor) == current_player_)
            names.emplace_back(e.card, 1);
        // A unit of a player who is not the turn player "moving" was moved by
        // an effect (Charm, Blitzcrank): what the decision now chose.
        else if (e.kind == "moved" && seatOf(e.actor) != PlayerId::None && seatOf(e.actor) != current_player_) {
            const size_t pos = i;
            if (pos < turn_of_.size() && !turn_of_[pos].empty() && e.actor != turn_of_[pos])
                names.emplace_back(e.card, 2);
        }
        if (e.kind == "equipped") {
            if (!e.card.empty()) names.emplace_back(e.card, 2);
            if (!e.to.empty()) names.emplace_back(e.to, 2);
        }
    }
    return names;
}

std::set<std::string> Driver::upcomingPlays(PlayerId player, int max_plays) const {
    std::set<std::string> out;
    int n = 0;
    for (size_t i = cursor_; i < game_.events.size() && n < max_plays; ++i) {
        if (consumed_[i]) continue;
        const LogLine& e = game_.events[i];
        if (seatOf(e.actor) != player) continue;
        if (e.isCardPlay()) {
            out.insert(normalizeName(e.card));
            ++n;
        } else if (e.kind == "hidden_card_placed") {
            const std::string nm = hiddenCardName(e);
            if (!nm.empty()) out.insert(normalizeName(nm));
            ++n;
        }
    }
    return out;
}

std::string Driver::hiddenCardName(const LogLine& e) const {
    for (const LogLine& f : game_.events) {
        if (f.index <= e.index || f.actor != e.actor) continue;
        if (f.kind == "played" && !f.from.empty() && nameMatches(f.from, e.to)) return f.card;
        if (f.kind == "revealed" && f.text.find("on the board") != std::string::npos) return f.card;
        if (f.kind == "played" && normalizeName(f.from) == "face down") return f.card;
    }
    return "";
}

int Driver::scoreIntent(const Intent& in, const GameState& s, const std::vector<std::pair<std::string, int>>& preferred) const {
    int score = choiceEvidence(in, s) + combatEvidence(in, s);
    // A card the log has an effect take from the top of the deck later this
    // turn was not drawn or picked now ("look at the top 3, draw one").
    if (in.type == IntentType::MakeChoice && !deck_top_names_.empty()) {
        auto top = [&](GameObjectId id) {
            return s.objectExists(id) && s.getObject(id).zone == ZoneType::MainDeck &&
                   deck_top_names_.count(normalizeName(s.getObject(id).name));
        };
        for (auto t : in.targets) if (top(t)) score -= 1;
        for (auto t : in.chosen_objects) if (top(t)) score -= 1;
    }
    if (preferred.empty()) return score;
    auto hit = [&](const std::string& nm) {
        int w = 0;
        for (const auto& [p, pw] : preferred) if (nameMatches(p, nm)) w = std::max(w, pw);
        return w;
    };
    auto hitObj = [&](GameObjectId id) { return hit(objName(s, id)); };
    for (auto t : in.targets) score += 2 * hitObj(t);
    for (auto t : in.chosen_objects) score += 2 * hitObj(t);
    for (const auto& d : in.damage_assignments) score += hitObj(d.target_unit) > 0 ? 1 : 0;
    if (in.chosen_card_def != kInvalidId) score += 2 * (hit(db_.get(in.chosen_card_def).name) > 0 ? 1 : 0);
    return score;
}

/// The next unconsumed lines of `player`, in order: up to `max_lines` lines
/// of the log are inspected (other players' lines are skipped); with
/// `stop_at_decision` the scan ends before the player's next play / end of
/// turn / focus pass / hide / equip (a move may be an effect, so it does not
/// end the scan).
std::vector<const LogLine*> Driver::upcomingLinesOf(PlayerId player, int max_lines, bool stop_at_decision) const {
    std::vector<const LogLine*> out;
    int seen = 0;
    for (size_t i = evidenceStart(); i < game_.events.size() && seen < max_lines; ++i) {
        if (matched_[i]) continue;
        ++seen;
        const LogLine& e = game_.events[i];
        if (seatOf(e.actor) != player) continue;
        if (stop_at_decision &&
            (e.isCardPlay() || e.kind == "ended_turn" || e.kind == "passed_focus" ||
             e.kind == "showdown" || e.kind == "hidden_card_placed" || e.kind == "equipped"))
            break;
        out.push_back(&e);
    }
    return out;
}

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

/// A "Yes" whose effect plays a card: "Fizz: play a spell (cost <= 3) from
/// trash for free?" (the next play is from the trash), "Flame Chompers: pay
/// [R] to play me?" / "Nocturne: banish from deck and play for [A]?" (the next
/// play is the prompt's own card), "Last Caress: play a unit to any
/// battlefield" (the next unit play). The index of `who`'s card-play line
/// that effect logged, else -1.
int Driver::effectPlayEvidence(const Intent& in, PlayerId who) const {
    const std::string label = lower(in.choice_label);
    std::string self;
    const bool from_trash = label.find("from trash") != std::string::npos;
    if (!from_trash) {
        const std::string dash = "\xE2\x80\x94 ";
        const auto d = label.find(dash);
        const auto colon = label.find(':', d == std::string::npos ? 0 : d);
        if (d != std::string::npos && colon != std::string::npos && colon > d + dash.size())
            self = label.substr(d + dash.size(), colon - d - dash.size());
    }
    // "Last Caress: play a unit to any battlefield": the player's next unit
    // play, a champion's from the Champion Zone logged as a move included.
    const bool any_unit = label.find("play a unit") != std::string::npos;
    int seen = 0;
    for (size_t i = evidenceStart(); i < game_.events.size() && seen < 8; ++i) {
        if (matched_[i]) continue;
        ++seen;
        const LogLine& e = game_.events[i];
        if (seatOf(e.actor) != who) continue;
        if (e.kind == "ended_turn" || e.kind == "passed_focus" || e.kind == "showdown") break;
        if (any_unit) {
            const CardDef* d = resolveCard(db_, e.card);
            const bool unit = d && d->card_type == CardType::Unit;
            bool champion_move = false;
            if (e.kind == "moved" && unit) {
                auto it = game_.legends.find(e.actor);
                champion_move = it != game_.legends.end() && nameMatches(e.card, it->second.champion_name);
            }
            if ((e.isCardPlay() && unit) || champion_move) return static_cast<int>(i);
            if (e.isCardPlay()) return -1;
            continue;
        }
        if (!e.isCardPlay()) continue;
        if (from_trash && normalizeName(e.from) != "trash") return -1;
        if (!from_trash && (self.empty() || !(nameMatches(e.card, self) ||
                                              normalizeName(e.card).rfind(normalizeName(self) + ",", 0) == 0)))
            return -1;
        return static_cast<int>(i);
    }
    return -1;
}

/// Evidence in the log for a yes/no or modal answer the engine asks for.
/// "Yes — <prompt>" scores when the lines that follow show the prompt's
/// effect (a payment for "pay [1]", a unit moved to base for "to base", a
/// token play for "token", a bounce for "return ... hand", ...); "No — …"
/// scores a little when nothing does. Mode labels ("[Buff] me", "Spend my
/// buff to ready me") are matched against buff_applied / buff_consumed /
/// readied lines.
int Driver::choiceEvidence(const Intent& in, const GameState& s) const {
    if (in.type != IntentType::MakeChoice || in.choice_label.empty()) return 0;
    const PlayerId who = in.player != PlayerId::None ? in.player : current_player_;
    const std::string label = lower(in.choice_label);
    const bool yes = label.rfind("yes", 0) == 0;
    const bool no = label.rfind("no", 0) == 0 && (label.size() == 2 || !std::isalpha(static_cast<unsigned char>(label[2])));
    auto lines = upcomingLinesOf(who, 8, /*stop_at_decision=*/true);
    // The rune lines right before the player's next card play pay for that
    // play ("<X> exhausted 1 Calm rune" ×7, "<X> played Astral Heron"), not for
    // an ability's "[1]" asked about now.
    std::set<const LogLine*> next_play_payment;
    {
        std::vector<const LogLine*> run;
        int seen = 0;
        for (size_t i = evidenceStart(); i < game_.events.size() && seen < 16; ++i) {
            if (matched_[i]) continue;
            ++seen;
            const LogLine& e = game_.events[i];
            if (seatOf(e.actor) != who) continue;
            if (e.isCardPlay()) { next_play_payment.insert(run.begin(), run.end()); break; }
            if (e.kind == "ended_turn" || e.kind == "passed_focus" || e.kind == "showdown" ||
                e.kind == "hidden_card_placed" || e.kind == "equipped")
                break;
            if (isPaymentLine(e) && isRuneLine(e)) run.push_back(&e);
            else run.clear();
        }
    }
    // "move an enemy unit", "an opponent discards": the effect shows up as
    // the other player's line.
    const PlayerId other = who == PlayerId::Player1 ? PlayerId::Player2 : PlayerId::Player1;
    bool enemy_moved = false;
    if (has(label, "enemy") || has(label, "opponent")) {
        for (const auto* e : upcomingLinesOf(other, 8, /*stop_at_decision=*/true)) {
            if (e->kind == "moved" || e->kind == "killed" || e->kind == "discarded" || e->kind == "exhausted" ||
                e->kind == "readied" || e->kind == "revealed" || e->kind == "revealed_hand")
                lines.push_back(e);
            if (e->kind == "moved") {
                const std::string to = normalizeName(e->to);
                if (!to.empty() && to != "hand" && to != "trash" && to != "deck" && to != "banished" &&
                    to != "banishment")
                    enemy_moved = true;
            }
        }
    }
    // The effects of what the engine asks about now can sit just BEFORE the
    // last matched line: "<X> duplicated Honest Broker", "<X> moved Dredge Up
    // from hand to trash" (LeBlanc, Deceiver's conquest trigger) logged ahead
    // of the other player's "ended showdown responses" that closed the
    // showdown. Such lines of the player, consumed as state changes since the
    // decision line before, count too: copies and discards only (an exhausted
    // unit or rune there belongs to what came before).
    for (int i = last_matched_ - 1; i > prev_matched_ && i >= 0 && i >= last_matched_ - 6; --i) {
        const LogLine& e = game_.events[static_cast<size_t>(i)];
        if (matched_[static_cast<size_t>(i)] || seatOf(e.actor) != who) continue;
        if (e.kind == "duplicated" || e.kind == "discarded") lines.push_back(&e);
    }
    auto any = [&](auto pred) {
        for (const auto* e : lines) if (pred(*e)) return true;
        return false;
    };
    // An ability's "[1]" / "[A]" shows as "<X> exhausted 1 Calm rune" /
    // "<X> recycled 1 Calm rune"; "<X> paid 2E" lines belong to card plays.
    const bool paid = any([&](const LogLine& e) {
        return (e.kind == "exhausted" || e.kind == "recycled") && e.text.find(" rune") != std::string::npos &&
               !next_play_payment.count(&e);
    });
    const bool made_unit = any([](const LogLine& e) { return e.kind == "made_unit"; });
    const bool to_base = any([](const LogLine& e) { return e.kind == "moved" && normalizeName(e.to) == "base"; });
    const bool to_hand = any([](const LogLine& e) { return e.kind == "moved" && normalizeName(e.to) == "hand"; });
    const bool moved_any = any([](const LogLine& e) { return e.kind == "moved"; });
    const bool token = any([](const LogLine& e) { return (e.kind == "played" && e.token) || e.kind == "duplicated"; });
    const bool drew = any([](const LogLine& e) { return e.kind == "drew"; });
    const bool channeled = any([](const LogLine& e) { return e.kind == "channeled"; });
    const bool discarded = any([](const LogLine& e) { return e.kind == "discarded"; });
    const bool looked = any([](const LogLine& e) { return e.kind == "looked" || e.kind == "revealed"; });
    const bool killed = any([](const LogLine& e) { return e.kind == "killed" || e.kind == "banished"; });
    const bool readied = any([](const LogLine& e) { return e.kind == "readied" && e.text.find(" rune") == std::string::npos; });
    const bool readied_rune = any([](const LogLine& e) { return e.kind == "readied" && e.text.find(" rune") != std::string::npos; });
    const bool exhausted_unit = any([](const LogLine& e) { return e.kind == "exhausted" && e.text.find(" rune") == std::string::npos; });
    const bool buff_applied = any([](const LogLine& e) { return e.kind == "buff_applied"; });
    const bool buff_consumed = any([](const LogLine& e) { return e.kind == "buff_consumed"; });
    const bool equipped = any([](const LogLine& e) { return e.kind == "equipped"; });

    if (yes || no) {
        const std::string prompt = label.substr(label.find(' ') == std::string::npos ? label.size() : label.find(' '));
        // "additional cost for Rampage: pay 0 energy + 1 power?": the play's
        // payment lines show it when they add up to more than the card's own
        // cost ("Billie paid 3E — Exhaust 3." + "Billie recycled 1 Body rune."
        // for a 3-Energy Rampage).
        if (const auto f = prompt.find("additional cost for "); f != std::string::npos) {
            const auto colon = prompt.find(": pay ", f);
            if (colon != std::string::npos) {
                const std::string name = prompt.substr(f + 20, colon - f - 20);
                std::istringstream is(prompt.substr(colon + 6));
                int add_e = 0, add_p = 0;
                std::string w1, plus;
                is >> add_e >> w1 >> plus >> add_p;
                const CardDef* d = resolveCard(db_, name);
                const LoggedPayment paid = paymentAround(who);
                if (d && paid.any) {
                    const bool did = add_p > 0 ? paid.power >= d->power_cost + add_p
                                               : paid.energy >= d->energy_cost + add_e;
                    return yes ? (did ? 3 : 0) : (did ? 0 : 2);
                }
            }
            return 0;
        }
        // Accelerate: the capture logs the [1][D] as rune lines after the
        // play and then "<X> readied <unit>"; without that line the unit
        // entered exhausted.
        if (has(prompt, "accelerate")) {
            const auto a = prompt.find("to have "), b = prompt.find(" enter ready");
            std::string unit = a != std::string::npos && b != std::string::npos && b > a + 8
                ? prompt.substr(a + 8, b - a - 8) : std::string();
            bool readied_unit = false;
            for (const auto* e : upcomingLinesOf(who, 10, /*stop_at_decision=*/true))
                if (e->kind == "readied" && !e->card.empty() && e->text.find(" rune") == std::string::npos &&
                    (unit.empty() || nameMatches(e->card, unit)))
                    readied_unit = true;
            return yes ? (readied_unit ? 3 : 0) : 0;
        }
        // "play a spell from your trash" (Fizz), "pay [R] to play me" (Flame
        // Chompers, discarded): the player's next card play is that card,
        // from the trash.
        if (has(prompt, "from trash") || has(prompt, "play me") || has(prompt, "play for") ||
            has(prompt, "play a unit")) {
            const int line = effectPlayEvidence(in, who);
            return yes ? (line >= 0 ? 3 : 0) : (line >= 0 ? 0 : 1);
        }
        int hits = 0;
        // "Armored Tusker: deal damage equal to my [Shield]?": a unit of
        // either player dying right after, before any focus pass, play or
        // end of turn, is that damage (the victim's line is its owner's).
        if (has(prompt, "deal ")) {
            int seen = 0;
            for (size_t i = evidenceStart(); i < game_.events.size() && seen < 6; ++i) {
                if (matched_[i]) continue;
                ++seen;
                const LogLine& f = game_.events[i];
                if (f.kind == "passed_focus" || f.kind == "showdown" || f.kind == "ended_turn" ||
                    f.kind == "conquered" || f.isCardPlay())
                    break;
                if (f.kind == "killed") { ++hits; break; }
            }
        }
        // "Concert Poster: pay [1] for a Recruit?": the token the prompt names
        // played next ("<X> played Recruit token to base").
        for (const auto* f : lines)
            if (f->kind == "played" && f->token && !f->card.empty() &&
                has(prompt, lower(normalizeName(f->card)).c_str())) { ++hits; break; }
        if (has(prompt, "pay ")) hits += paid;
        if (has(prompt, "to base") || has(prompt, "recall")) hits += to_base;
        else if (has(prompt, "move")) hits += has(prompt, "enemy") ? enemy_moved : moved_any;
        // Lady of Clockwork: "<X> made The Ball a unit with Orianna".
        if (has(prompt, "choose a friendly gear")) hits += made_unit;
        if (has(prompt, "hand") || has(prompt, "return")) hits += to_hand;
        if (has(prompt, "token")) hits += token;
        if (has(prompt, "draw")) hits += drew;
        if (has(prompt, "channel")) hits += channeled;
        if (has(prompt, "discard")) hits += discarded;
        if (has(prompt, "predict") || has(prompt, "look") || has(prompt, "reveal")) hits += looked;
        if (has(prompt, "kill") || has(prompt, "banish")) hits += killed;
        if (has(prompt, "ready")) hits += has(prompt, "rune") ? readied_rune : readied;
        if (has(prompt, "exhaust")) hits += exhausted_unit;
        if (has(prompt, "equip") || has(prompt, "attach") || has(prompt, "gear")) hits += equipped;
        // Lookahead names (targets the effect touches) weigh on the Yes side
        // through scoreIntent's target hits; here only the prompt's own effect.
        if (yes) return hits > 0 ? 3 : 0;
        return 0;
    }
    // "Deal 1 to each of up to 2 units" (Bilgewater Tracker): how many units
    // the log kills before the player's next decision.
    if (label == "no units" || label == "one unit" || label == "two units") {
        int kills = 0, seen = 0;
        for (size_t i = evidenceStart(); i < game_.events.size() && seen < 8; ++i) {
            if (matched_[i]) continue;
            ++seen;
            const LogLine& e = game_.events[i];
            if (seatOf(e.actor) == who && (e.isCardPlay() || e.kind == "ended_turn" || e.kind == "passed_focus"))
                break;
            if (e.kind == "killed") ++kills;
        }
        if (label == "no units") return kills == 0 ? 1 : 0;
        if (label == "one unit") return kills >= 1 ? 2 : 0;
        return kills >= 2 ? 3 : 0;
    }
    // Modal labels.
    int score = 0;
    // "Base" / a battlefield's name: where the card an effect plays went.
    if (!effect_location_.empty() && nameMatches(effect_location_, in.choice_label)) score += 3;
    // "Bomb to BF#1" (a token or a move to a battlefield the card names by
    // its id): the battlefield the player's next token play or move goes to
    // ("<X> played Bomb token to Sigil of the Storm").
    if (const auto at = in.choice_label.find("BF#"); at != std::string::npos) {
        int id = -1;
        try { id = std::stoi(in.choice_label.substr(at + 3)); } catch (...) { id = -1; }
        std::string where;
        for (const auto* f : lines) {
            if (f->to.empty()) continue;
            if ((f->kind == "played" && f->token) || f->kind == "moved") { where = f->to; break; }
        }
        // ... or a token line logged just before the last matched line (an
        // effect's move matched as a decision after it: Star Spring sending
        // the unit home after its Bomb).
        for (int i = last_matched_ - 1; where.empty() && i > prev_matched_ && i >= 0 && i >= last_matched_ - 4; --i) {
            const LogLine& f = game_.events[static_cast<size_t>(i)];
            if (matched_[static_cast<size_t>(i)] || seatOf(f.actor) != who) continue;
            if (f.kind == "played" && f.token && !f.to.empty()) where = f.to;
        }
        if (id >= 0 && !where.empty()) {
            for (const auto& b : s.battlefields)
                if (static_cast<int>(b.id) == id && nameMatches(where, bfName(s, b.id))) score += 3;
        }
    }
    // "You discard" / "Opponent discards", "You burn 1" / "Opponent burns 1":
    // whose discard / burn / draw line comes next.
    for (const auto& [verb, kind] : {std::pair<const char*, const char*>{"discard", "discarded"},
                                     {"burn", "burned"}, {"draw", "drew"}}) {
        const bool mine = label.rfind("you ", 0) == 0, theirs = label.rfind("opponent ", 0) == 0;
        if (!has(label, verb) || !(mine || theirs)) continue;
        int seen = 0;
        for (size_t i = evidenceStart(); i < game_.events.size() && seen < 8; ++i) {
            if (matched_[i]) continue;
            ++seen;
            const LogLine& f = game_.events[i];
            if (f.kind != kind) continue;
            const PlayerId by = seatOf(f.actor);
            if (by == PlayerId::None) break;
            if ((by == who) == mine) score += 3;
            break;
        }
    }
    if (has(label, "buff me") || has(label, "[buff]")) score += buff_applied ? 3 : 0;
    if (has(label, "spend")) score += buff_consumed ? 2 : -1;
    if (has(label, "ready")) score += readied ? 2 : -1;
    if (has(label, "stun") && buff_consumed && !readied) score += 1;
    return score;
}

/// Combat damage: the log shows which units died ("<X> moved <unit> from
/// <bf> to trash"). Prefer the assignment whose lethal set is exactly the
/// recorded kills: +3 per lethal assignment on a unit the log kills, -3 per
/// lethal assignment on a unit that survives in the log.
int Driver::combatEvidence(const Intent& in, const GameState& s) const {
    if (in.type != IntentType::AssignCombatDamage || in.damage_assignments.empty()) return 0;
    std::vector<std::string> killed;
    int seen = 0;
    for (size_t i = evidenceStart(); i < game_.events.size() && seen < 12; ++i) {
        if (matched_[i]) continue;
        ++seen;
        const LogLine& e = game_.events[i];
        if (e.kind == "ended_turn") break;
        if (e.kind == "killed" && !e.card.empty()) killed.push_back(e.card);
    }
    int score = 0;
    for (const auto& d : in.damage_assignments) {
        if (d.damage <= 0 || !s.objectExists(d.target_unit)) continue;
        const auto& u = s.getObject(d.target_unit);
        const bool lethal = u.damage_marked + d.damage >= u.current_might;
        bool dies_in_log = false;
        for (const auto& k : killed) if (nameMatches(k, u.name)) { dies_in_log = true; break; }
        if (lethal) score += dies_in_log ? 3 : -3;
        else if (dies_in_log) score -= 1;
    }
    return score;
}

int Driver::passLike(const std::vector<Intent>& legal) const {
    for (size_t i = 0; i < legal.size(); ++i) if (legal[i].type == IntentType::PassPriority) return static_cast<int>(i);
    for (size_t i = 0; i < legal.size(); ++i) if (legal[i].type == IntentType::PassFocus) return static_cast<int>(i);
    return -1;
}

/// "<X> played Ember Monk from hand to base", "<X> moved Ember Monk to
/// Grove of the God-Willow" with nothing readying the unit in between: a
/// unit enters exhausted (CR 143.4, 359.2.c) and cannot make that move, so
/// the client logged the play at its default location before the player put
/// the card where it was played. The index of that move line, else -1. A
/// unit with [Accelerate] may have entered ready and moved: left alone.
int Driver::relocationAfterPlay(const LogLine& e, bool logged_location_illegal) const {
    if (e.card.empty()) return -1;
    if (!logged_location_illegal) {
        if (normalizeName(e.to) != "base") return -1;
        if (const CardDef* d = resolveCard(db_, e.card); !d || d->card_type != CardType::Unit ||
            d->keywords.has(Keyword::Accelerate))
            return -1;
    }
    const size_t pos = static_cast<size_t>(&e - game_.events.data());
    // The play's own payment lines after it (a capture that logs the play
    // first: "<X> exhausted 1 Calm rune" x8, "<X> recycled 1 Calm rune" x2)
    // do not count toward the window.
    int seen = 0;
    for (size_t j = pos + 1; j < game_.events.size() && seen < 8; ++j) {
        if (consumed_[j]) continue;
        const LogLine& f = game_.events[j];
        if (f.actor == e.actor && isPaymentLine(f) && isRuneLine(f)) continue;
        ++seen;
        if (f.kind == "ended_turn" || f.kind == "passed_focus" || f.kind == "showdown" ||
            f.kind == "conquered" || f.kind == "rewound")
            return -1;
        if (f.card.empty() || !nameMatches(e.card, f.card)) continue;
        if (f.kind != "moved") return -1;   // readied, exhausted, killed, ...: not a relocation
        if (f.actor != e.actor) return -1;
        const std::string to = normalizeName(f.to);
        if (to == "base" && logged_location_illegal) return static_cast<int>(j);
        if (isZoneWord(to) || to == "champion area") return -1;
        return static_cast<int>(j);
    }
    return -1;
}

/// Two battlefields share a name when both players brought the same one (a
/// mirror match): "<X> moved U to Void Gate" names either. A move to a
/// battlefield the mover does not control contests it and opens a showdown
/// there (CR 190.3.a.1, 344); one to a battlefield they control does not. So
/// the battlefield whose control matches what the log shows next (a focus
/// pass or conquest at that name, or none) scores 2. 0 when the name is
/// unique or `dest` is not a battlefield.
int Driver::sameNameDestinationScore(const LogLine& e, const GameState& s, PlayerId p,
                                     const std::optional<LocationId>& dest) const {
    if (!dest.has_value() || !std::holds_alternative<BattlefieldLocation>(*dest)) return 0;
    const BattlefieldId id = std::get<BattlefieldLocation>(*dest).id;
    const std::string name = bfName(s, id);
    int same = 0;
    for (const auto& b : s.battlefields) if (nameMatches(name, bfName(s, b.id))) ++same;
    if (same < 2) return 0;
    bool showdown = false;
    const size_t pos = static_cast<size_t>(&e - game_.events.data());
    int seen = 0;
    for (size_t j = pos + 1; j < game_.events.size() && seen < 8; ++j) {
        if (consumed_[j]) continue;
        ++seen;
        const LogLine& f = game_.events[j];
        if (f.kind == "passed_focus" || f.kind == "showdown" || f.kind == "conquered" ||
            f.kind == "showdown_dismissed") {
            showdown = nameMatches(f.battlefield.empty() ? f.to : f.battlefield, name);
            break;
        }
        if (f.kind == "ended_turn" || f.isCardPlay() || (f.kind == "moved" && f.actor == e.actor &&
                                                         !nameMatches(f.card, e.card)))
            break;
    }
    bool controlled = false;
    for (const auto& b : s.battlefields)
        if (b.id == id) controlled = b.controller.has_value() && *b.controller == p;
    int score = showdown != controlled ? 2 : 0;
    // A showdown the mover wins: the battlefield had enemy units (a combat)
    // when the log kills one there before the conquest, else it was empty.
    if (showdown) {
        bool conquered = false, enemy_killed = false;
        for (size_t j = pos + 1; j < game_.events.size(); ++j) {
            const LogLine& f = game_.events[j];
            if (f.kind == "ended_turn") break;
            if (f.kind == "killed" && f.actor != e.actor && nameMatches(f.from, name)) enemy_killed = true;
            if (f.kind == "conquered" && nameMatches(f.battlefield, name)) {
                conquered = f.actor == e.actor;
                break;
            }
        }
        if (conquered) {
            bool enemies_there = false;
            for (const auto& [oid, o] : s.objects)
                if (o.isUnit() && o.controller != p && o.location.has_value() &&
                    o.battlefieldId() == std::optional<BattlefieldId>{id})
                    enemies_there = true;
            if (enemies_there == enemy_killed) score += 1;
        }
    }
    return score;
}

Match Driver::matchEvent(const LogLine& e, const std::vector<Intent>& legal, const GameState& s, PlayerId p) {
    Match m;
    const auto preferred = lookaheadNames();
    auto pickBest = [&](const std::vector<int>& cands, auto extra) -> int {
        int best = -1, best_score = -1;
        for (int k : cands) {
            const int sc = scoreIntent(legal[k], s, preferred) + extra(legal[k]);
            if (sc > best_score) { best_score = sc; best = k; }
        }
        return best;
    };
    const std::string& k = e.kind;

    if (k == "played") {
        std::vector<int> cands;
        bool name_hit = false, loc_miss = false;
        for (size_t i = 0; i < legal.size(); ++i) {
            const Intent& in = legal[i];
            if (!isPlayType(in.type) || !nameMatches(e.card, objName(s, in.card))) continue;
            name_hit = true;
            if (!e.to.empty() && !locMatches(s, in.play_location, e.to)) { loc_miss = true; continue; }
            cands.push_back(static_cast<int>(i));
        }
        // "<X> played Plundering Poro from hand to Bandle Scouts' Academy", at
        // once "<X> moved Plundering Poro to base": the logged location is not
        // one the card can be played to (a unit goes to its controller's base
        // or a battlefield they control, CR 813.3.a); the client let the card
        // be dropped there and the player put it where it was played.
        if (cands.empty() && name_hit && loc_miss) {
            if (const int r = relocationAfterPlay(e, /*logged_location_illegal=*/true); r >= 0) {
                std::vector<int> moved_to;
                for (size_t i = 0; i < legal.size(); ++i) {
                    const Intent& in = legal[i];
                    if (isPlayType(in.type) && nameMatches(e.card, objName(s, in.card)) &&
                        locMatches(s, in.play_location, game_.events[static_cast<size_t>(r)].to))
                        moved_to.push_back(static_cast<int>(i));
                }
                if (!moved_to.empty()) {
                    m.index = pickBest(moved_to, [](const Intent&) { return 0; });
                    m.also.push_back(r);
                    return m;
                }
            }
        }
        // "<X> played Seal of Rage from banished": a play permission granted
        // to the legend (Runeblade Anvil: "Until your next Ending Phase, you
        // may play that card") is the legend's granted ability, whose
        // resolution picks the banished card.
        if (cands.empty() && normalizeName(e.from) == "banished") {
            bool banished = false;
            for (const auto& [id, o] : s.objects)
                if (o.owner == p && o.zone == ZoneType::Banishment && nameMatches(e.card, o.name)) banished = true;
            if (banished) {
                for (size_t i = 0; i < legal.size(); ++i) {
                    const Intent& in = legal[i];
                    if (in.type == IntentType::ActivateAbility && in.granted_ability_def != 0 &&
                        in.ability_source == s.player(p).legend_zone) {
                        m.index = static_cast<int>(i);
                        m.effect_name = e.card;
                        return m;
                    }
                }
            }
        }
        if (cands.empty()) {
            if (name_hit && loc_miss) m.why = "card playable but not to '" + e.to + "'";
            else {
                bool in_hand = false, in_trash = false;
                for (auto id : s.player(p).hand) if (nameMatches(e.card, objName(s, id))) in_hand = true;
                for (auto id : s.player(p).trash) if (nameMatches(e.card, objName(s, id))) in_trash = true;
                if (normalizeName(e.from) == "trash") m.why = in_trash ? "card in trash but not replayable now" : "card not in trash";
                else if (!e.from.empty() && normalizeName(e.from) != "hand") m.why = "play from '" + e.from + "' not offered";
                else m.why = in_hand ? "card in hand but not playable now (cost/timing)" : "card not in hand";
            }
            return m;
        }
        const bool from_trash = normalizeName(e.from) == "trash";
        // "Played Emperor's Divide from Minefield": from face down at that
        // battlefield.
        bool from_bf = false;
        if (!isZoneWord(e.from) && !from_trash)
            for (const auto& b : s.battlefields) if (nameMatches(e.from, bfName(s, b.id))) from_bf = true;
        const bool from_facedown = normalizeName(e.from) == "face down" || from_bf;
        if (from_facedown) {
            m.index = pickBest(cands, [&](const Intent& in) {
                return s.objectExists(in.card) && s.getObject(in.card).is_hidden ? 2 : 0;
            });
            return m;
        }
        if (const int r = relocationAfterPlay(e); r >= 0) {
            std::vector<int> moved_to;
            for (size_t i = 0; i < legal.size(); ++i) {
                const Intent& in = legal[i];
                if (isPlayType(in.type) && nameMatches(e.card, objName(s, in.card)) &&
                    locMatches(s, in.play_location, game_.events[static_cast<size_t>(r)].to))
                    moved_to.push_back(static_cast<int>(i));
            }
            if (!moved_to.empty()) {
                m.index = pickBest(moved_to, [](const Intent&) { return 0; });
                m.also.push_back(r);
                return m;
            }
        }
        m.index = pickBest(cands, [&](const Intent& in) {
            return (from_trash && in.play_source == Intent::PlaySource::Trash) ? 1 : 0;
        });
        return m;
    }
    if (k == "moved") {
        // CR 144.3: "<X> moved A to BF" / "<X> moved B to BF" right after each
        // other are one simultaneous move (the showdown opens after both
        // arrive). Collect the run and look for the group intent first.
        {
            std::vector<int> run;
            std::vector<std::string> names{e.card};
            // `e` is one of game_.events; LogLine::index is its position in
            // the FILE (games 2-3 of a BO3 start further on, dropped capture
            // artefacts shift it), not in game_.events.
            const size_t pos = static_cast<size_t>(&e - game_.events.data());
            for (size_t j = pos + 1; j < game_.events.size() && run.size() < 4; ++j) {
                if (consumed_[j]) continue;
                const LogLine& f = game_.events[j];
                if (f.kind == "moved" && f.actor == e.actor && normalizeName(f.to) == normalizeName(e.to) &&
                    normalizeName(f.to) != "hand") {
                    run.push_back(static_cast<int>(j));
                    names.push_back(f.card);
                    continue;
                }
                // Lines a move's own triggers leave between the units.
                if ((f.actor == e.actor || f.actor.empty()) &&
                    (f.kind == "buff_applied" || f.kind == "buff_consumed" || f.kind == "readied" ||
                     f.kind == "exhausted" || f.kind == "drew" || f.kind == "chain_resolved" ||
                     f.kind == "token_used" || f.kind == "looked" || f.kind == "recycled" ||
                     (f.kind == "played" && f.token)))
                    continue;
                break;
            }
            int group_best = 0;
            for (size_t n = names.size(); n >= 2 && !run.empty(); --n) {
                for (size_t i = 0; i < legal.size(); ++i) {
                    const Intent& in = legal[i];
                    if (in.type != IntentType::StandardMove || in.units_to_move.size() != n) continue;
                    if (!locMatches(s, in.move_destination, e.to)) continue;
                    std::vector<bool> used(n, false);
                    bool all = true;
                    for (size_t a = 0; a < n && all; ++a) {
                        bool found = false;
                        for (size_t b = 0; b < n; ++b) {
                            if (used[b] || !nameMatches(names[a], objName(s, in.units_to_move[b]))) continue;
                            used[b] = true; found = true; break;
                        }
                        all = found;
                    }
                    if (!all) continue;
                    const int sc = sameNameDestinationScore(e, s, p, in.move_destination);
                    if (m.index < 0 || sc > group_best) {
                        m.index = static_cast<int>(i);
                        group_best = sc;
                    }
                }
                if (m.index >= 0) {
                    m.also.assign(run.begin(), run.begin() + static_cast<long>(n - 1));
                    return m;
                }
            }
        }
        std::vector<int> cands;
        bool unit_hit = false;
        for (size_t i = 0; i < legal.size(); ++i) {
            const Intent& in = legal[i];
            if (in.type != IntentType::StandardMove || in.units_to_move.size() != 1) continue;
            if (!nameMatches(e.card, objName(s, in.units_to_move[0]))) continue;
            unit_hit = true;
            if (locMatches(s, in.move_destination, e.to)) cands.push_back(static_cast<int>(i));
        }
        if (cands.empty() && alreadyMovedByEffect(e, s, p)) {
            m.index = Match::kAlreadyDone;
            return m;
        }
        if (cands.empty()) {
            // A champion played from the champion zone, or an Ambush unit from hand,
            // is logged as "moved <card> to <location>".
            for (size_t i = 0; i < legal.size(); ++i) {
                const Intent& in = legal[i];
                if (isPlayType(in.type) && nameMatches(e.card, objName(s, in.card)) &&
                    locMatches(s, in.play_location, e.to))
                    cands.push_back(static_cast<int>(i));
            }
            if (!cands.empty()) {
                if (const int r = relocationAfterPlay(e); r >= 0) {
                    std::vector<int> moved_to;
                    for (size_t i = 0; i < legal.size(); ++i) {
                        const Intent& in = legal[i];
                        if (isPlayType(in.type) && nameMatches(e.card, objName(s, in.card)) &&
                            locMatches(s, in.play_location, game_.events[static_cast<size_t>(r)].to))
                            moved_to.push_back(static_cast<int>(i));
                    }
                    if (!moved_to.empty()) {
                        m.index = pickBest(moved_to, [](const Intent&) { return 0; });
                        m.also.push_back(r);
                        return m;
                    }
                }
            }
        }
        if (cands.empty()) {
            if (unit_hit) m.why = "unit can move but not to '" + e.to + "'";
            else {
                bool on_board = false, exhausted = false;
                for (const auto& [id, o] : s.objects)
                    if (o.controller == p && o.location && nameMatches(e.card, o.name)) { on_board = true; exhausted = o.is_exhausted; }
                m.why = on_board ? (exhausted ? "unit is exhausted (no move option)" : "unit on board but no move option")
                                 : "no such unit on board (not played / already gone)";
            }
            return m;
        }
        m.index = pickBest(cands, [&](const Intent& in) {
            return sameNameDestinationScore(e, s, p, in.move_destination);
        });
        return m;
    }
    if (k == "ended_turn") {
        for (size_t i = 0; i < legal.size(); ++i) if (legal[i].type == IntentType::EndTurn) { m.index = static_cast<int>(i); return m; }
        m.why = "no End turn option (" + std::string(toString(s.turn.phase)) + ")";
        return m;
    }
    if (k == "passed_focus" || k == "showdown") {
        // The pass is at the battlefield the log names: a focus pass at
        // another battlefield is not this showdown's (RiftLite lets a player
        // make moves to two battlefields before either showdown is logged).
        const std::string here = focusBattlefield(s);
        if (!e.battlefield.empty() && !here.empty() && !nameMatches(e.battlefield, here)) {
            m.why = "focus pass at '" + e.battlefield + "' while the engine's showdown is at '" + here + "'";
            return m;
        }
        for (size_t i = 0; i < legal.size(); ++i) if (legal[i].type == IntentType::PassFocus) { m.index = static_cast<int>(i); return m; }
        m.why = "no Pass focus option (no showdown in the engine)";
        return m;
    }
    if (k == "mulligan") {
        // Recycle the cards the log needs latest (or never): the first
        // recorded play weighs most.
        std::map<std::string, int> rank;
        {
            int n = 0;
            for (size_t i = cursor_; i < game_.events.size() && n < 8; ++i) {
                if (consumed_[i]) continue;
                const LogLine& f = game_.events[i];
                if (seatOf(f.actor) != p || !isDecisionEvent(f, legal, s)) continue;
                if (f.kind != "played" && f.kind != "hidden_card_placed") continue;
                const std::string nm = f.kind == "played" ? f.card : hiddenCardName(f);
                if (!nm.empty()) rank.emplace(normalizeName(nm), 8 - n);
                ++n;
            }
        }
        int best = -1, best_kept = 1 << 20;
        for (size_t i = 0; i < legal.size(); ++i) {
            const Intent& in = legal[i];
            if (in.type != IntentType::MulliganDecision || static_cast<int>(in.cards_to_mulligan.size()) != e.amount) continue;
            int kept = 0;
            for (auto c : in.cards_to_mulligan) {
                auto it = rank.find(normalizeName(objName(s, c)));
                if (it != rank.end()) kept += it->second;
            }
            if (kept < best_kept) { best_kept = kept; best = static_cast<int>(i); }
        }
        if (best < 0) m.why = "no mulligan option with " + std::to_string(e.amount) + " cards";
        m.index = best;
        return m;
    }
    if (k == "hidden_card_placed") {
        const std::string hidden = hiddenCardName(e);
        std::vector<int> cands;
        for (size_t i = 0; i < legal.size(); ++i) {
            const Intent& in = legal[i];
            if (in.type != IntentType::HideCard) continue;
            if (!e.to.empty() && !nameMatches(e.to, bfName(s, in.chosen_battlefield))) continue;
            cands.push_back(static_cast<int>(i));
        }
        if (cands.empty()) { m.why = "no hide option at '" + e.to + "'"; return m; }
        m.index = pickBest(cands, [&](const Intent& in) {
            return (!hidden.empty() && nameMatches(hidden, objName(s, in.card))) ? 3 : 0;
        });
        return m;
    }
    if (k == "equipped") {
        std::vector<int> cands;
        for (size_t i = 0; i < legal.size(); ++i) {
            const Intent& in = legal[i];
            if (!isAbilityType(in.type) || in.targets.empty()) continue;
            if (!nameMatches(e.card, objName(s, in.ability_source))) continue;
            if (!nameMatches(e.to, objName(s, in.targets[0]))) continue;
            cands.push_back(static_cast<int>(i));
        }
        if (cands.empty()) {
            // [Quick-Draw] ("When you play it, attach it to a unit you
            // control"): a play of the gear from hand carrying the unit.
            for (size_t i = 0; i < legal.size(); ++i) {
                const Intent& in = legal[i];
                if (!isPlayType(in.type) || in.targets.empty() || !nameMatches(e.card, objName(s, in.card))) continue;
                if (!nameMatches(e.to, objName(s, in.targets[0]))) continue;
                cands.push_back(static_cast<int>(i));
            }
            if (!cands.empty()) {
                m.index = cands.front();
                return m;
            }
        }
        if (cands.empty()) { m.why = "no equip option for '" + e.card + "' onto '" + e.to + "'"; return m; }
        // The exhaust cost: the log's "<actor> exhausted <unit>" lines just
        // before the equip name the unit; without them, not the equip target.
        std::vector<std::string> exhausted;
        bool paid_power = false;   // "<actor> recycled 1 Calm rune" just before: the [A] was paid
        for (auto it = game_.events.rbegin(); it != game_.events.rend(); ++it) {
            if (it->index >= e.index) continue;
            if (e.index - it->index > 6) break;
            if (it->actor != e.actor) continue;
            if (it->kind == "exhausted" && !it->card.empty() && it->text.find(" rune") == std::string::npos)
                exhausted.push_back(it->card);
            if (it->kind == "recycled" && it->text.find(" rune") != std::string::npos && e.index - it->index <= 3)
                paid_power = true;
        }
        m.index = pickBest(cands, [&](const Intent& in) {
            int sc = 0;
            if (in.targets.size() == 1 && paid_power && exhausted.empty()) sc += 3;
            for (size_t t = 1; t < in.targets.size(); ++t) {
                for (const auto& ex : exhausted) if (nameMatches(ex, objName(s, in.targets[t]))) sc += 3;
                if (in.targets[t] == in.targets[0]) sc -= 1;
                if (paid_power && exhausted.empty()) sc -= 1;
            }
            return sc;
        });
        return m;
    }
    if (k == "exhausted") {
        // An [Add] ability used before the turn's Main Phase would lose its
        // resources when the Rune Pool empties there (CR 167): the capture's
        // "<X> exhausted Kai'Sa" sits among the Main Phase's payment lines.
        const bool before_main = s.turn.phase == TurnPhase::AwakenPhase ||
                                 s.turn.phase == TurnPhase::BeginningStep ||
                                 s.turn.phase == TurnPhase::ScoringStep ||
                                 s.turn.phase == TurnPhase::ChannelPhase ||
                                 s.turn.phase == TurnPhase::DrawPhase;
        auto addsResources = [&](const Intent& in) {
            if (!s.objectExists(in.ability_source)) return false;
            const Card* c = registry_.get(s.getObject(in.ability_source).card_def_id);
            if (!c) return false;
            const auto abs = c->activatedAbilities();
            return in.ability_index >= 0 && in.ability_index < static_cast<int>(abs.size()) &&
                   abs[in.ability_index].adds_resources;
        };
        std::vector<int> cands;
        for (size_t i = 0; i < legal.size(); ++i)
            if (isAbilityType(legal[i].type) && nameMatches(e.card, objName(s, legal[i].ability_source)) &&
                !(before_main && addsResources(legal[i])))
                cands.push_back(static_cast<int>(i));
        if (cands.empty()) { m.why = "no ability option for '" + e.card + "'"; return m; }
        m.index = pickBest(cands, [](const Intent& in) { return in.targets.empty() ? 1 : 0; });
        return m;
    }
    if (k == "made_unit") {
        // "<actor> made <gear> a unit with <legend>": the legend's ability on that gear.
        std::vector<int> cands;
        for (size_t i = 0; i < legal.size(); ++i)
            if (isAbilityType(legal[i].type) && !legal[i].targets.empty() &&
                nameMatches(e.card, objName(s, legal[i].targets[0])))
                cands.push_back(static_cast<int>(i));
        if (cands.empty()) { m.why = "no ability option targeting '" + e.card + "'"; return m; }
        m.index = cands.front();
        return m;
    }
    m.why = "unhandled log kind '" + k + "'";
    return m;
}

/// The payment lines of the play just matched (game_.events[last_matched_]):
/// up to four lines before it (the usual order) and the ones right after it
/// (captures that log the play first), never another play's. "<X> paid 2E /
/// 1P — Exhaust 2 · Recycle 1 Mind rune", "<X> exhausted 1 Fury rune",
/// "<X> recycled 1 Mind rune".
Driver::LoggedPayment Driver::paymentAround(PlayerId payer) const {
    LoggedPayment out;
    auto parse = [&](const LogLine& e) {
        const std::string& text = e.text;
        if (e.kind == "paid") {
            const auto at = text.find(" paid ");
            if (at != std::string::npos) {
                std::istringstream is(text.substr(at + 6));
                int x = 0;
                char unit = 0;
                if (is >> x >> unit && unit == 'E') {
                    out.energy += x;
                    std::string slash;
                    int y = 0;
                    if (is >> slash && slash == "/" && is >> y >> unit && unit == 'P') out.power += y;
                    out.any = true;
                }
            }
        } else if (e.kind == "exhausted" && text.find(" rune") != std::string::npos) {
            out.energy += 1;
            out.any = true;
            // "<X> exhausted 1 Calm rune"
            const auto at = text.find("exhausted ");
            if (at != std::string::npos) {
                std::istringstream is(text.substr(at + 10));
                int n = 0;
                std::string dom, word;
                if (is >> n >> dom >> word && word.rfind("rune", 0) == 0)
                    for (int c = 0; c < n && c < 8; ++c) out.exhausted.push_back(normalizeName(dom));
            }
        }
        for (const std::string verb : {"Recycle ", "recycled "}) {
            for (size_t at = text.find(verb); at != std::string::npos; at = text.find(verb, at + 1)) {
                std::istringstream is(text.substr(at + verb.size()));
                int n = 0;
                std::string dom, word;
                if (!(is >> n >> dom >> word) || word.rfind("rune", 0) != 0) continue;
                for (int c = 0; c < n && c < 8; ++c) out.recycled.push_back(normalizeName(dom));
                if (e.kind == "recycled") { out.power += n; out.any = true; }
                if (text.find("(+", at) != std::string::npos) out.ready_recycled = true;
            }
        }
    };
    if (last_matched_ < 0) return out;
    const int lm = last_matched_;
    for (int j = lm - 1; j >= 0 && j >= lm - 4; --j) {
        const LogLine& e = game_.events[static_cast<size_t>(j)];
        if (e.isCardPlay() || e.kind == "ended_turn" || e.kind == "moved") break;
        // The run right before the play only: "<X> played Bomb token" ends
        // it (the lines before pay for the ability that made the token).
        if (e.kind == "played") break;
        if (seatOf(e.actor) == payer && (e.kind == "paid" || e.kind == "recycled" || e.kind == "exhausted")) parse(e);
    }
    for (size_t j = static_cast<size_t>(lm) + 1; j < game_.events.size() && j <= static_cast<size_t>(lm) + 5; ++j) {
        const LogLine& e = game_.events[j];
        if (e.isCardPlay() || e.kind == "ended_turn" || e.kind == "moved" || e.kind == "passed_focus") break;
        if (seatOf(e.actor) == payer && (e.kind == "paid" || e.kind == "recycled" || e.kind == "exhausted")) parse(e);
    }
    return out;
}

/// A rune to recycle while a cost is paid. The payment lines around the play
/// name the recycled runes' domains ("<X> recycled 1 Mind rune", "<X> paid
/// 2E / 1P — Exhaust 2 · Recycle 1 Mind rune"), and a READY rune recycled is
/// marked "(+1E)" (its Energy floats). Recycling the domain the log names
/// keeps the right runes on the board for the next plays; without a name, an
/// exhausted rune goes first (the client spends exhausted runes for power).
int Driver::paymentChoice(const std::vector<Intent>& legal, const GameState& s, bool& known) const {
    known = false;
    const auto& cur = *s.cost_cursor;
    if (cur.phase == GameState::CostPaymentCursor::Phase::PayingEnergy) {
        // "<X> exhausted 1 Chaos rune": the rune exhausted for Energy is of
        // the domain the log names (the one recycled after it for Power can
        // be that same rune, CR 164.2).
        const LoggedPayment paid = paymentAround(cur.player);
        if (paid.exhausted.empty()) {
            // "Exhaust 1 · Recycle 1 Fury rune" (no "(+1E)": the recycled rune
            // was not ready): the rune exhausted for Energy is the Fury rune
            // recycled after it, when the cost still owes Power of its domain.
            if (cur.power_remaining <= 0 || paid.recycled.empty() || paid.ready_recycled) return 0;
            for (size_t i = 0; i < legal.size(); ++i) {
                if (legal[i].chosen_objects.empty() || !s.objectExists(legal[i].chosen_objects[0])) continue;
                for (auto d : s.getObject(legal[i].chosen_objects[0]).domains) {
                    const bool named = std::find(paid.recycled.begin(), paid.recycled.end(),
                                                 normalizeName(toString(d))) != paid.recycled.end();
                    const bool pays = std::find(cur.power_domains.begin(), cur.power_domains.end(), d) !=
                                      cur.power_domains.end();
                    if (named && pays) return static_cast<int>(i);
                }
            }
            return 0;
        }
        for (size_t i = 0; i < legal.size(); ++i) {
            if (legal[i].chosen_objects.empty() || !s.objectExists(legal[i].chosen_objects[0])) continue;
            for (auto d : s.getObject(legal[i].chosen_objects[0]).domains)
                if (std::find(paid.exhausted.begin(), paid.exhausted.end(), normalizeName(toString(d))) !=
                    paid.exhausted.end()) {
                    known = true;
                    return static_cast<int>(i);
                }
        }
        return 0;
    }
    if (cur.phase != GameState::CostPaymentCursor::Phase::PayingPower) return 0;
    const LoggedPayment paid = paymentAround(cur.player);
    const std::vector<std::string>& named = paid.recycled;
    const bool ready_recycled = paid.ready_recycled;
    int best = 0, best_score = -1;
    for (size_t i = 0; i < legal.size(); ++i) {
        if (legal[i].chosen_objects.empty() || !s.objectExists(legal[i].chosen_objects[0])) continue;
        const auto& rune = s.getObject(legal[i].chosen_objects[0]);
        int sc = 0;
        for (auto d : rune.domains)
            if (std::find(named.begin(), named.end(), normalizeName(toString(d))) != named.end()) { sc += 4; break; }
        if (rune.is_exhausted != ready_recycled) sc += 1;
        if (sc > best_score) { best_score = sc; best = static_cast<int>(i); }
    }
    known = !named.empty();
    return best;
}

int Driver::chooseForced(const std::vector<Intent>& legal, const GameState& s, bool& known, std::string& note) {
    known = false;
    if (s.cost_cursor.has_value()) {
        note = "cost payment";
        const int pick = paymentChoice(legal, s, known);
        if (known) note = "cost payment: rune the log names";
        known = false;   // a rune pick is never a compared decision
        return pick;
    }
    const auto preferred = lookaheadNames();
    // The second target of a pair ("Deal 3 to a unit. Deal 3 to a unit.",
    // Falling Star): when the log kills a unit other than the first target,
    // that unit was the second target, not the first one again.
    std::string first_target;
    if (s.chain.resuming.has_value() && s.chain.resuming->resume_point == 12 &&
        s.chain.resuming->resume_data.size() > 3) {
        const auto a = static_cast<GameObjectId>(s.chain.resuming->resume_data[3]);
        if (s.objectExists(a)) first_target = s.getObject(a).name;
    }
    bool other_kill = false;
    if (!first_target.empty()) {
        for (const auto& in : legal)
            for (auto t : in.chosen_objects) {
                const std::string nm = objName(s, t);
                if (nameMatches(first_target, nm)) continue;
                for (const auto& [n, w] : preferred) if (w >= 3 && nameMatches(n, nm)) other_kill = true;
            }
    }
    int best = 0, best_score = std::numeric_limits<int>::min();
    for (size_t i = 0; i < legal.size(); ++i) {
        int sc = scoreIntent(legal[i], s, preferred);
        if (other_kill)
            for (auto t : legal[i].chosen_objects) if (nameMatches(first_target, objName(s, t))) sc -= 4;
        if (sc > best_score) { best_score = sc; best = static_cast<int>(i); }
    }
    if (best_score > 0) {
        known = true;
        note = "matched upcoming log lines";
        // The card a "Yes" plays (from the trash, the deck top, the Champion
        // Zone) is played by the effect: its "played <X>" line is that
        // effect, not a later decision.
        const Intent& chosen = legal[static_cast<size_t>(best)];
        const std::string label = lower(chosen.choice_label);
        if (chosen.type == IntentType::MakeChoice && label.rfind("yes", 0) == 0 &&
            (has(label, "from trash") || has(label, "play me") || has(label, "play for") ||
             has(label, "play a unit"))) {
            const PlayerId who = chosen.player != PlayerId::None ? chosen.player : current_player_;
            const int line = effectPlayEvidence(chosen, who);
            if (line >= 0) {
                const std::string card = game_.events[static_cast<size_t>(line)].card;
                const std::string where = game_.events[static_cast<size_t>(line)].to;
                markDecision(line);
                effect_names_.push_back(card);
                effect_location_ = where;
                note = "matched upcoming log lines; the effect plays " + card;
            }
        }
    } else {
        note = "no log evidence: first option";
    }
    return best;
}

void Driver::prefetch(GameEngine& engine, const std::vector<Intent>& legal) {
    const GameState& s = engine.state();
    // Hands exist from the mulligan on; fixing them there already lets the
    // first main-phase decision offer the first recorded play.
    if (s.turn.phase == TurnPhase::Setup) return;
    std::set<GameObjectId> protect;
    for (const auto& in : legal) {
        protect.insert(in.card);
        for (auto id : in.units_to_move) protect.insert(id);
        for (auto id : in.targets) protect.insert(id);
        for (auto id : in.cards_to_mulligan) protect.insert(id);
        for (auto id : in.chosen_objects) protect.insert(id);
        protect.insert(in.ability_source);
    }
    GameState& ms = engine.mutableState();
    auto fix = [&](PlayerId p, const CardDef& d, ZoneType zone, const std::set<std::string>& keep, const LogLine* why) {
        const std::string r = ensureCardIn(ms, db_, p, d.id, zone, keep, protect);
        if (r == "already") return;
        if (r == "pulled") ++pulled_; else ++created_;
        if (hand_fixes_.size() < 400)
            hand_fixes_.push_back({{"player", seatIndex(p)}, {"card", d.name}, {"how", r},
                                   {"zone", zone == ZoneType::Trash ? "trash" : "hand"},
                                   {"turn", s.turn.turn_number}, {"for", why ? why->text : ""}});
    };
    for (PlayerId p : {PlayerId::Player1, PlayerId::Player2}) {
        const std::set<std::string> keep = upcomingPlays(p, 6);
        int n = 0;
        for (size_t i = cursor_; i < game_.events.size() && n < 2; ++i) {
            if (consumed_[i]) continue;
            const LogLine& e = game_.events[i];
            if (seatOf(e.actor) != p) continue;
            if (e.kind == "played") {
                if (!e.isCardPlay()) continue;
                const std::string from = normalizeName(e.from);
                ++n;
                const CardDef* d = resolveCard(db_, e.card);
                if (!d || !isDeckCardType(*d)) continue;
                if (from == "trash") fix(p, *d, ZoneType::Trash, keep, &e);
                else if (from.empty() || from == "hand") fix(p, *d, ZoneType::Hand, keep, &e);
            } else if (e.kind == "hidden_card_placed") {
                ++n;
                const CardDef* d = resolveCard(db_, hiddenCardName(e));
                if (d && isDeckCardType(*d)) fix(p, *d, ZoneType::Hand, keep, &e);
            } else if (e.kind == "equipped" && !e.card.empty()) {
                // A gear equipped that is not on the board is played from hand
                // and attached ([Quick-Draw]).
                bool on_board = false;
                for (const auto& [id, o] : ms.objects)
                    if (o.controller == p && o.location.has_value() && nameMatches(e.card, o.name)) on_board = true;
                const CardDef* d = resolveCard(db_, e.card);
                if (!on_board && d && d->card_type == CardType::Gear && d->keywords.has(Keyword::QuickDraw))
                    fix(p, *d, ZoneType::Hand, keep, &e);
            } else if (e.kind == "discarded" && normalizeName(e.from) == "hand") {
                // "<X> moved Flame Chompers from hand to trash": the card is
                // in the hand, so the engine's discard choice can pick it.
                const CardDef* d = resolveCard(db_, e.card);
                if (d && isDeckCardType(*d)) fix(p, *d, ZoneType::Hand, keep, &e);
            }
        }
    }
    for (const auto& [p, def] : pending_hand_) fix(p, db_.get(def), ZoneType::Hand, upcomingPlays(p, 6), nullptr);
    pending_hand_.clear();
    deck_top_names_.clear();
    // "<X> banished Seal of Rage from the top of their deck with Runeblade
    // Anvil", "<X> played Nocturne, Horrifying from top of deck to base": the
    // log names a card an effect takes from the top of the Main Deck, whose
    // order is hidden. Put it there (the first one named on top).
    for (PlayerId p : {PlayerId::Player1, PlayerId::Player2}) {
        // This turn only: a card put there before the Draw Phase is drawn.
        std::vector<CardDefId> tops;
        int seen = 0;
        for (size_t i = cursor_; i < game_.events.size() && seen < 12; ++i) {
            if (consumed_[i]) continue;
            const LogLine& e = game_.events[i];
            if (e.kind == "ended_turn") break;
            if (seatOf(e.actor) != p) continue;
            ++seen;
            const std::string from = normalizeName(e.from);
            if ((e.kind == "played" || e.kind == "banished") && !e.token &&
                (from.rfind("top of", 0) == 0 || from.rfind("the top of", 0) == 0)) {
                const CardDef* d = resolveCard(db_, e.card);
                if (d && isDeckCardType(*d)) {
                    tops.push_back(d->id);
                    deck_top_names_.insert(normalizeName(d->name));
                }
            }
        }
        auto& deck = ms.player(p).main_deck;
        for (auto it = tops.rbegin(); it != tops.rend(); ++it) {
            // Already among the top cards this many lines name: leave it.
            const size_t window = tops.size();
            bool near_top = false;
            for (size_t k = 0; k < window && k < deck.size(); ++k)
                if (ms.objectExists(deck[deck.size() - 1 - k]) &&
                    ms.getObject(deck[deck.size() - 1 - k]).card_def_id == *it)
                    near_top = true;
            if (near_top) continue;
            bool placed = false;
            for (size_t k = deck.size(); k-- > 0;) {
                if (!ms.objectExists(deck[k]) || ms.getObject(deck[k]).card_def_id != *it) continue;
                const GameObjectId id = deck[k];
                deck.erase(deck.begin() + static_cast<long>(k));
                deck.push_back(id);
                ++deck_tops_;
                placed = true;
                break;
            }
            if (!placed) {
                // Not in the deck (the inferred list holds fewer copies than the
                // human's): conjure one, as the hand fixes do.
                const auto id = ms.createObject();
                fillFromDef(ms.getObject(id), db_.get(*it), p);
                ms.getObject(id).zone = ZoneType::MainDeck;
                deck.push_back(id);
                ++deck_tops_;
                ++created_;
            }
        }
    }
    for (PlayerId p : {PlayerId::Player1, PlayerId::Player2}) fixRuneDomains(ms, p, protect);
}

/// The rune deck's order is hidden: the engine's shuffle channels other runes
/// than the human's. The log names the domain of each rune a payment uses
/// ("<X> exhausted 1 Fury rune", "<X> recycled 1 Mind rune", "<X> paid 2E /
/// 1P — Exhaust 2 · Recycle 1 Body rune"). When `p`'s next payment names more
/// runes of a domain than `p` has on the board, a board rune of a domain the
/// payment does not use is swapped with one of the needed domain from the rune
/// deck (same place on the board, same readiness).
void Driver::fixRuneDomains(GameState& ms, PlayerId p, const std::set<GameObjectId>& protect) {
    // Runes exhausted for Energy and runes recycled for Power, per domain. A
    // rune is often exhausted and then recycled, so a domain needs as many
    // runes as the larger of the two counts.
    std::map<std::string, int> exhausted, recycled;
    bool started = false;
    int seen = 0;
    auto add = [&](std::map<std::string, int>& into, const std::string& text, const char* verb) {
        for (size_t at = text.find(verb); at != std::string::npos; at = text.find(verb, at + 1)) {
            std::istringstream is(text.substr(at + std::strlen(verb)));
            int n = 0;
            std::string dom, word;
            if (!(is >> n >> dom >> word) || word.rfind("rune", 0) != 0) continue;
            into[normalizeName(dom)] += std::min(n, 12);
            started = true;
        }
    };
    for (size_t i = cursor_; i < game_.events.size() && seen < 10; ++i) {
        if (consumed_[i]) continue;
        const LogLine& e = game_.events[i];
        if (seatOf(e.actor) != p) continue;
        ++seen;
        const bool rune_line = e.text.find(" rune") != std::string::npos;
        const bool payment = e.kind == "paid" || isPaymentOrTokenLine(e);
        if (e.kind == "paid") add(recycled, e.text, "Recycle ");
        else if (e.kind == "exhausted" && rune_line) add(exhausted, e.text, " exhausted ");
        else if (e.kind == "recycled" && rune_line) add(recycled, e.text, " recycled ");
        if (payment) continue;
        // The payment's own play may be logged before it; anything else of
        // the player's ends the payment (or, before it, the search).
        if (started) break;
        if (e.kind == "ended_turn" || e.kind == "passed_focus" || e.kind == "showdown" ||
            e.kind == "hidden_card_placed" || e.kind == "equipped")
            break;
    }
    std::map<std::string, int> need;
    for (const auto& [d, n] : exhausted) need[d] = std::max(need[d], n);
    for (const auto& [d, n] : recycled) need[d] = std::max(need[d], n);
    if (need.empty()) return;

    auto& ps = ms.player(p);
    std::map<std::string, std::vector<GameObjectId>> board;
    for (auto& [id, o] : ms.objects)
        if (o.isRune() && o.controller == p && o.location.has_value() && !o.domains.empty())
            board[normalizeName(toString(o.domains.front()))].push_back(id);
    // Runes of a needed domain the board cannot supply by a swap (no rune of
    // a spare domain there yet, as before a player's first Channel Phase) go
    // to the top of the rune deck, so the next channel brings them.
    std::vector<std::pair<std::string, int>> to_top;
    for (const auto& [dom, n] : need) {
        int have = static_cast<int>(board[dom].size());
        while (have < n) {
            // A board rune of a domain this payment needs less of than there is.
            GameObjectId out = kInvalidId;
            for (auto& [od, ids] : board) {
                if (od == dom) continue;
                const int spare = static_cast<int>(ids.size()) - (need.count(od) ? need.at(od) : 0);
                if (spare <= 0) continue;
                for (auto it = ids.rbegin(); it != ids.rend(); ++it)
                    if (!protect.count(*it)) { out = *it; break; }
                if (out != kInvalidId) { ids.erase(std::find(ids.begin(), ids.end(), out)); break; }
            }
            if (out == kInvalidId) { to_top.emplace_back(dom, n - have); break; }
            // The topmost rune of the needed domain in the rune deck.
            auto in_it = ps.rune_deck.end();
            for (auto it = ps.rune_deck.rbegin(); it != ps.rune_deck.rend(); ++it) {
                if (!ms.objectExists(*it)) continue;
                const auto& r = ms.getObject(*it);
                if (!r.domains.empty() && normalizeName(toString(r.domains.front())) == dom) { in_it = std::next(it).base(); break; }
            }
            if (in_it == ps.rune_deck.end()) return;
            auto& leaving = ms.getObject(out);
            auto& coming = ms.getObject(*in_it);
            coming.zone = leaving.zone;
            coming.location = leaving.location;
            coming.is_exhausted = leaving.is_exhausted;
            leaving.zone = ZoneType::RuneDeck;
            leaving.location.reset();
            leaving.is_exhausted = false;
            const GameObjectId in_id = *in_it;
            *in_it = out;
            board[dom].push_back(in_id);
            ++have;
            ++rune_swaps_;
        }
    }
    for (const auto& [dom, count] : to_top) {
        // The top `count` runes of the deck (its back) should hold `count`
        // runes of `dom`: move the nearest ones there, keeping the rest in order.
        auto& deck = ps.rune_deck;
        auto isDom = [&](GameObjectId id) {
            if (!ms.objectExists(id)) return false;
            const auto& r = ms.getObject(id);
            return !r.domains.empty() && normalizeName(toString(r.domains.front())) == dom;
        };
        const int top = std::min<int>(count, static_cast<int>(deck.size()));
        int already = 0;
        for (int k = 0; k < top; ++k) already += isDom(deck[deck.size() - 1 - static_cast<size_t>(k)]);
        const size_t block = deck.size() - static_cast<size_t>(top);   // first index of the top block
        for (int have_top = already; have_top < top; ++have_top) {
            // Swap a rune of another domain in the top block with the nearest
            // rune of `dom` below it.
            size_t out = deck.size(), in = deck.size();
            for (size_t k = block; k < deck.size(); ++k) if (!isDom(deck[k])) { out = k; break; }
            for (size_t k = block; k-- > 0;) if (isDom(deck[k])) { in = k; break; }
            if (out == deck.size() || in == deck.size()) break;
            std::swap(deck[out], deck[in]);
            ++rune_swaps_;
        }
    }
}

/// A desync the log itself proves impossible, whatever the engine does
/// (conservative: anything the log does not show is assumed possible):
///
///  - "exhausted unit moved": every unit of that name the mover controls is
///    exhausted, and for each the log shows what exhausted it this turn - its
///    standard move (CR 144.2), its play or a token's (CR 143.4: units enter
///    exhausted), or an "<X> exhausted <unit>" line - with no "readied <unit>"
///    line and no end of turn between that line and the move.
///  - "paid below cost": a card played from hand whose payment lines, the
///    contiguous run right before the play (or right after it, for captures
///    that log the play first), add up to less Energy, or less Energy plus
///    Power, than the engine's cost for it there after every reduction it
///    knows; only when the run is runes alone (no [Add] source, no ready rune
///    recycled for its Energy) and the player has nothing floating.
/// The payment lines of the play at `pos`: the contiguous run of the
/// player's rune lines and "paid" lines right before it, plus the run right
/// after it (captures that log the play first, or an additional cost logged
/// after it) unless that run pays for the player's next play. Adds up the
/// Energy and Power they show; `clean` turns false when the run holds an
/// [Add] source or a ready rune recycled for its Energy ("(+1E)"), whose
/// resources the lines do not show. Returns the number of lines.
int Driver::loggedPaymentOf(size_t pos, int& energy, int& power, bool& clean) const {
    if (pos >= game_.events.size()) return 0;
    const std::string& actor = game_.events[pos].actor;
    auto runAt = [&](long from, long step, int& en, int& pw, bool& cl) {
        int n = 0;
        for (long j = from; j >= 0 && j < static_cast<long>(game_.events.size()); j += step) {
            const LogLine& f = game_.events[static_cast<size_t>(j)];
            if (f.actor != actor) break;
            if (f.kind == "paid") {
                const auto at = f.text.find(" paid ");
                std::istringstream is(at == std::string::npos ? std::string() : f.text.substr(at + 6));
                int x = 0, y = 0;
                char unit = 0;
                std::string slash;
                if (!(is >> x >> unit) || unit != 'E') { cl = false; break; }
                en += x;
                if (is >> slash && slash == "/" && is >> y >> unit && unit == 'P') pw += y;
                if (f.text.find("(+") != std::string::npos) cl = false;
                ++n;
                continue;
            }
            if ((f.kind == "exhausted" || f.kind == "recycled") && isRuneLine(f)) {
                if (f.kind == "exhausted") en += 1;
                else {
                    std::istringstream is(f.text.substr(f.text.find("recycled ") + 9));
                    int k = 0;
                    is >> k;
                    pw += k;
                    if (f.text.find("(+") != std::string::npos) cl = false;
                }
                ++n;
                continue;
            }
            if (isPaymentOrTokenLine(f) || f.kind == "killed") cl = false;   // an [Add] source
            break;
        }
        return n;
    };
    int n = runAt(static_cast<long>(pos) - 1, -1, energy, power, clean);
    int e2 = 0, p2 = 0;
    bool c2 = true;
    const int n2 = runAt(static_cast<long>(pos) + 1, +1, e2, p2, c2);
    const size_t next = pos + 1 + static_cast<size_t>(n2);
    const bool next_play = next < game_.events.size() && game_.events[next].actor == actor &&
                           (game_.events[next].isCardPlay() || game_.events[next].kind == "exhausted");
    if (n2 > 0 && !next_play) { energy += e2; power += p2; n += n2; clean = clean && c2; }
    return n;
}

bool Driver::classifyArtefact(const LogLine& e, const GameState& s, const std::string& reason,
                              std::string& rule) const {
    const PlayerId p = seatOf(e.actor);
    if (p == PlayerId::None) return false;
    const size_t pos = static_cast<size_t>(&e - game_.events.data());
    if (pos >= game_.events.size()) return false;

    if (e.kind == "moved" && reason.rfind("unit is exhausted", 0) == 0) {
        // This turn's token plays of that name with no ready after them: a
        // token the engine made for an effect enters exhausted (CR 143.4).
        size_t turn_start = 0;
        for (size_t j = pos; j-- > 0;)
            if (game_.events[j].kind == "ended_turn") { turn_start = j + 1; break; }
        int token_lines = 0;
        size_t token_line = pos;
        for (size_t j = turn_start; j < pos; ++j) {
            const LogLine& f = game_.events[j];
            if (f.kind == "played" && f.token && seatOf(f.actor) == p && nameMatches(e.card, f.card)) {
                ++token_lines;
                token_line = std::min(token_line, j);
            }
            if (f.kind == "readied" && !isRuneLine(f) && (f.card.empty() || nameMatches(e.card, f.card)))
                token_lines = 0;
        }
        int candidates = 0;
        size_t since = pos;
        for (const auto& [id, o] : s.objects) {
            if (o.controller != p || !o.location.has_value() || !o.isUnit() || !nameMatches(e.card, o.name)) continue;
            ++candidates;
            if (!o.is_exhausted) return false;
            auto it = log_exhausted_at_.find(id);
            if (it == log_exhausted_at_.end() && o.isToken() && token_lines > 0) {
                --token_lines;
                since = std::min(since, token_line);
                continue;
            }
            if (it == log_exhausted_at_.end() || it->second >= pos) return false;
            for (size_t j = it->second + 1; j < pos; ++j) {
                const LogLine& f = game_.events[j];
                if (f.kind == "ended_turn") return false;
                if (f.kind == "readied" && !isRuneLine(f) && (f.card.empty() || nameMatches(e.card, f.card)))
                    return false;   // a ready the log shows (or one it cannot place)
            }
            since = std::min(since, it->second);
        }
        if (candidates == 0) return false;
        rule = "exhausted unit moved: the log exhausts " + e.card + " at line " +
               std::to_string(game_.events[since].index) + " (\"" + game_.events[since].text +
               "\") and readies it nowhere before this move";
        return true;
    }

    if (e.kind == "played" && reason.rfind("card in hand but not playable now", 0) == 0 && engine_) {
        if (!e.from.empty() && normalizeName(e.from) != "hand") return false;
        const auto& ps = s.player(p);
        if (ps.rune_pool.energy != 0 || ps.rune_pool.totalPower() != 0) return false;
        GameObjectId card = kInvalidId;
        for (auto id : ps.hand) if (nameMatches(e.card, objName(s, id))) { card = id; break; }
        if (card == kInvalidId) return false;
        std::optional<LocationId> where;
        if (!e.to.empty() && normalizeName(e.to) != "base") {
            for (const auto& b : s.battlefields) if (nameMatches(e.to, bfName(s, b.id))) where = BattlefieldLocation{b.id};
            if (!where) return false;
        }
        const auto cost = engine_->currentPlayCost(p, card, where);
        if (!cost.valid) return false;
        int energy = 0, power = 0;
        bool clean = true;
        const int n = loggedPaymentOf(pos, energy, power, clean);
        if (n == 0 || !clean) {
            // An earlier play this turn the log paid below its cost: the
            // runes this one lacks are the ones that play never used.
            const auto it = underpaid_.find(seatIndex(p));
            if (it != underpaid_.end() && it->second.turn == s.turn.turn_number && it->second.line < pos) {
                rule = "paid below cost earlier this turn: " + it->second.text;
                return true;
            }
            return false;
        }
        const int need_e = cost.energy, need_total = cost.energy + cost.power + cost.extra_any_power;
        if (energy >= need_e && energy + power >= need_total) {
            const auto it = underpaid_.find(seatIndex(p));
            if (it != underpaid_.end() && it->second.turn == s.turn.turn_number && it->second.line < pos) {
                rule = "paid below cost earlier this turn: " + it->second.text;
                return true;
            }
            return false;
        }
        rule = "paid below cost: the log pays " + std::to_string(energy) + "E/" + std::to_string(power) +
               "P for " + e.card + ", which costs " + std::to_string(cost.energy) + "E/" +
               std::to_string(cost.power + cost.extra_any_power) + "P here";
        return true;
    }
    return false;
}

void Driver::desync(const LogLine& e, const std::vector<Intent>& legal, const GameState& s, const std::string& reason) {
    ++desyncs_;
    const std::string cause = e.kind + ": " + reason;
    desync_causes_[cause]++;
    nlohmann::json opts = nlohmann::json::array();
    for (size_t i = 0; i < legal.size() && i < 40; ++i) opts.push_back(describeIntent(legal[i], s, &db_));
    nlohmann::json entry = {{"decision", decisions_}, {"turn", s.turn.turn_number},
                            {"phase", toString(s.turn.phase)}, {"player", seatIndex(legal.empty() ? PlayerId::None : legal[0].player)},
                            {"event_index", e.index}, {"event_text", e.text}, {"event_kind", e.kind},
                            {"reason", reason}, {"options", opts}};
    std::string rule;
    const bool artefact = classifyArtefact(e, s, reason, rule);
    if (artefact) {
        ++log_artefacts_;
        entry["log_artefact"] = true;
        entry["log_artefact_rule"] = rule;
    }
    desync_list_.push_back(std::move(entry));
    if (opts_.verbose) {
        std::cerr << "  [desync " << desyncs_ << "] turn " << s.turn.turn_number << " ev#" << e.index
                  << " \"" << e.text << "\": " << reason << (artefact ? " [log artefact: " + rule + "]" : std::string()) << "\n";
        // The board as the engine has it, to tell an engine gap from a log artefact.
        for (PlayerId p : {PlayerId::Player1, PlayerId::Player2}) {
            const auto& ps = s.player(p);
            int ready = 0, exhausted = 0;
            std::string units, hand;
            for (const auto& [id, o] : s.objects) {
                if (o.controller != p || !o.location.has_value()) continue;
                if (o.isRune()) {
                    (o.is_exhausted ? exhausted : ready)++;
                    units += std::string(" rune:") + (o.domains.empty() ? "?" : toString(o.domains.front())) +
                             (o.is_exhausted ? "(x)" : "");
                    continue;
                }
                units += " " + o.name + (o.is_exhausted ? "(x)" : "") + "@" +
                         (o.battlefieldId() ? bfName(s, *o.battlefieldId()) : std::string("base"));
            }
            for (auto id : ps.hand) hand += " " + objName(s, id) + ";";
            std::cerr << "    P" << seatIndex(p) << " score " << ps.score << " runes " << ready << " ready/"
                      << exhausted << " exhausted, pool " << ps.rune_pool.energy << "E/"
                      << ps.rune_pool.totalPower() << "P, deck " << ps.main_deck.size() << "\n      board:" << units
                      << "\n      hand:" << hand << "\n";
        }
    }
}

GuidedResult Driver::run() {
    const auto t_start = std::chrono::steady_clock::now();
    const PlayerId first = seatOf(game_.first_player) == PlayerId::None ? PlayerId::Player1 : seatOf(game_.first_player);
    const uint64_t seed = seedForFirstPlayer(opts_.seed & 0x7FFFFFFFu, first) & 0x7FFFFFFFu;

    std::vector<std::string> review_lines;
    DecisionLog review;
    EventBus events;
    GameEngine engine(db_, events, registry_);
    mutable_state_ = &engine.mutableState();
    engine_ = &engine;
    // RIFTBOUND_LOG_STREAM=1 (with --verbose) streams the engine trace to
    // stderr between the decision lines, as the riftbound CLI does.
    static const bool stream = std::getenv("RIFTBOUND_LOG_STREAM") != nullptr;
    events.on_log.connect([&review_lines, this](const LogEvent& e) {
        if (stream && opts_.verbose) std::cerr << "    | " << e.message << "\n";
        if (e.level == LogLevel::Debug || e.level == LogLevel::Info ||
            (e.level == LogLevel::Trace && review_lines.size() < 200))
            review_lines.push_back(e.message);
    });

    // Per-frame bookkeeping for the summary.
    struct Compared { int n, turn, player, human, bot; std::string kind, event, human_text, bot_text; bool agree, clean; std::vector<std::pair<int, double>> search; std::vector<std::string> options; };
    std::vector<Compared> compared;
    std::map<std::string, std::pair<int, int>> by_kind;   // kind -> (n, agree)
    int compared_clean = 0, agree_clean = 0, known_total = 0, trivial = 0;

    // Chosen legal indices so far, for a bot that rebuilds in-flight decisions
    // by replaying the game (AgentInterface::setDecisionHistory). God-mode
    // edits are not replayable; the bot verifies the rebuilt state itself.
    std::vector<int32_t> choice_history;
    if (bot_) bot_->setDecisionHistory(&choice_history);

    StepResult sr = engine.beginGame(decks_[0]->deck, decks_[1]->deck, seed);
    // Any group of units a player moved together is an option (CR 144.3),
    // not only the whole group once more than four could go.
    engine.mutableState().mode.move_group_subset_limit = 8;
    while (sr.kind == StepKind::NeedDecision && !sr.legal.empty()) {
        if (decisions_ >= opts_.max_decisions) { stop_reason_ = "decision cap"; break; }
        prefetch(engine, sr.legal);
        const GameState& st = engine.state();
        const std::vector<Intent>& legal = sr.legal;
        const PlayerId P = legal[0].player != PlayerId::None ? legal[0].player : sr.perspective;
        current_player_ = P;

        // Mulligans ran in the engine's coin-toss order; make sure it is the log's.
        if (st.turn.phase == TurnPhase::Mulligan && st.turn.turn_player != first) {
            engine.mutableState().turn.turn_player = first;
            engine.mutableState().turn.starting_player = first;
        }

        int choice = -1;
        bool known = false, forced = false;
        const LogLine* ev = nullptr;
        std::string note;
        bool stop = false;

        if (st.cost_cursor.has_value() ||
            std::all_of(legal.begin(), legal.end(), [](const Intent& in) { return isForcedType(in.type); })) {
            forced = true;
            ++forced_;
            choice = chooseForced(legal, st, known, note);
        } else if (legal[0].type == IntentType::ChooseBattlefield) {
            const DeckInference& d = *decks_[P == PlayerId::Player2 ? 1 : 0];
            choice = 0;
            for (size_t i = 0; i < d.battlefield_names.size(); ++i)
                if (d.battlefield_names[i] == d.preferred_battlefield) choice = static_cast<int>(i);
            if (choice >= static_cast<int>(legal.size())) choice = 0;
            known = !d.preferred_battlefield.empty();
            for (size_t i = cursor_; i < game_.events.size(); ++i)
                if (!consumed_[i] && game_.events[i].kind == "battlefield_locked") { markDecision(static_cast<int>(i)); ev = &game_.events[i]; break; }
        } else {
            while (true) {
                const int i = nextPendingDecision(legal, st);
                if (i < 0) { stop = true; stop_reason_ = "log ended"; break; }
                const LogLine& e = game_.events[i];
                const PlayerId actor = seatOf(e.actor);
                if (e.soft) {
                    Match m;
                    if (actor == P) m = matchEvent(e, legal, st, P);
                    if (m.index >= 0) {
                        choice = m.index; markDecision(i); ev = &e; known = true;
                        break;
                    }
                    consumed_[i] = 1;
                    matched_[i] = 1;
                    ++soft_dropped_;
                    continue;
                }
                if (actor == P) {
                    Match m = matchEvent(e, legal, st, P);
                    if (m.index == Match::kAlreadyDone) {
                        markDecision(i, /*snapshot=*/false);
                        ++effect_lines_;
                        continue;
                    }
                    if (m.index >= 0) { choice = m.index; markDecision(i); for (int a : m.also) markDecision(a); if (!m.effect_name.empty()) effect_names_.push_back(m.effect_name); ev = &e; known = true; break; }
                    if (const int fp = focusPassAtBattlefield(i, P, legal, st); fp >= 0) {
                        for (size_t q = 0; q < legal.size(); ++q)
                            if (legal[q].type == IntentType::PassFocus) { choice = static_cast<int>(q); break; }
                        markDecision(fp); ev = &game_.events[fp]; known = true; ++out_of_order_;
                        note = "focus pass at this showdown's battlefield matched out of order";
                        break;
                    }
                    const int pl = passLike(legal);
                    if (pl >= 0) { choice = pl; ++silent_passes_; note = "pass (engine still in flight before: " + e.text + ")"; break; }
                    desync(e, legal, st, m.why);
                    markDecision(i);
                } else if (e.actor_uncertain && e.kind == "played" && matchEvent(e, legal, st, P).index >= 0) {
                    // An actor-less spell line nobody's payment precedes: the
                    // player the engine asks can play it, so it is theirs.
                    Match m = matchEvent(e, legal, st, P);
                    game_.events[i].actor = P == PlayerId::Player1 ? game_.players[0] : game_.players[1];
                    game_.events[i].actor_uncertain = false;
                    ++actors_settled_;
                    choice = m.index; markDecision(i); for (int a : m.also) markDecision(a); if (!m.effect_name.empty()) effect_names_.push_back(m.effect_name); ev = &e; known = true; note = "actor settled by the engine's turn";
                    break;
                } else {
                    // The engine is at a focus decision and P's own next
                    // decision line is the focus pass at this showdown's
                    // battlefield, behind lines of the other player (a move
                    // matched out of order opened the showdown early, or
                    // the capture ordered the two players' lines differently):
                    // that pass is this decision.
                    if (const int fp = focusPassInWindow(i, P, legal, st); fp >= 0) {
                        for (size_t k = 0; k < legal.size(); ++k)
                            if (legal[k].type == IntentType::PassFocus) { choice = static_cast<int>(k); break; }
                        markDecision(fp); ev = &game_.events[fp]; known = true; ++out_of_order_;
                        note = "focus pass matched out of order";
                        break;
                    }
                    // RiftLite keeps the focus with a player whose spell or
                    // ability resolved in a showdown, and logs that player's
                    // pass before the other player acts; by the rules the
                    // focus passes when the chain closes (CR 346, 347.1.b).
                    // The engine asks P now: P's next action, after the other
                    // player's pass at this battlefield, is this decision.
                    if (e.kind == "passed_focus" &&
                        std::any_of(legal.begin(), legal.end(), [](const Intent& in) { return in.type == IntentType::PassFocus; }) &&
                        nameMatches(e.battlefield, focusBattlefield(st))) {
                        int hit = -1;
                        Match mm;
                        for (size_t j = static_cast<size_t>(i) + 1; j < game_.events.size(); ++j) {
                            if (consumed_[j]) continue;
                            const LogLine& f = game_.events[j];
                            if (f.kind == "ended_turn" || (f.kind == "conquered" && nameMatches(f.battlefield, e.battlefield))) break;
                            if (seatOf(f.actor) != P || !isDecisionEvent(f, legal, st)) continue;
                            if (f.kind == "passed_focus" || f.kind == "showdown") break;
                            mm = matchEvent(f, legal, st, P);
                            if (mm.index >= 0) hit = static_cast<int>(j);
                            break;
                        }
                        if (hit >= 0) {
                            choice = mm.index; markDecision(hit); for (int a : mm.also) markDecision(a);
                            if (!mm.effect_name.empty()) effect_names_.push_back(mm.effect_name);
                            ev = &game_.events[static_cast<size_t>(hit)]; known = true; ++out_of_order_;
                            note = "showdown action read before the other player's logged focus pass (CR 346)";
                            break;
                        }
                    }
                    const int pl = passLike(legal);
                    if (pl >= 0) { choice = pl; ++silent_passes_; note = "pass (log continues with the other player)"; break; }
                    Match m;
                    const int j = findInWindow(i, P, legal, st, m);
                    if (j >= 0) { choice = m.index; markDecision(j); for (int a : m.also) markDecision(a); if (!m.effect_name.empty()) effect_names_.push_back(m.effect_name); ev = &game_.events[j]; known = true; ++out_of_order_; note = "matched out of order"; break; }
                    std::string who = actor == PlayerId::None ? std::string("nobody") : "P" + std::to_string(seatIndex(actor));
                    desync(e, legal, st, "engine asks P" + std::to_string(seatIndex(P)) + " but the log's next action is by " + who + " (" + e.kind + ")");
                    markDecision(i);
                }
                if (desyncs_ >= opts_.max_desyncs) { stop = true; stop_reason_ = "desync cap"; break; }
            }
        }
        if (stop) break;

        // ── Bot ──
        int bot_idx = -1;
        std::vector<std::pair<int, double>> shares;
        bool asked = false, fallback = false;
        const bool clean = cleanBoundary(st);
        if (bot_ && known && legal.size() > 1 && !st.cost_cursor.has_value() &&
            (P == PlayerId::Player1 || opts_.compare_both)) {
            asked = true;
            ++bot_calls_;
            const auto t0 = std::chrono::steady_clock::now();
            try {
                const Intent b = bot_->selectAction(st, legal);
                for (size_t i = 0; i < legal.size(); ++i) if (legal[i] == b) { bot_idx = static_cast<int>(i); break; }
                shares = bot_->lastSearch();
                if (shares.empty()) { fallback = true; ++bot_fallbacks_; }
            } catch (const std::exception& e) {
                ++bot_errors_;
                note += (note.empty() ? "" : "; ") + std::string("bot error: ") + e.what();
                bot_idx = -1;
            }
            bot_seconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
        const bool is_compared = asked && !fallback && bot_idx >= 0;
        const std::string kind = kindLabel(legal[choice]);
        // ── Imitation record (RBV3, source = human) ──
        if (!opts_.export_features_path.empty() && known && legal.size() > 1 && !st.cost_cursor.has_value() &&
            (P == PlayerId::Player1 || P == PlayerId::Player2)) {
            ml::ValueSample sample;
            sample.who = P;
            sample.turn = st.turn.turn_number;
            sample.decision_index = st.decision_index;
            sample.features = ml::sparsify(ml::extractStateFeatures(st, P, db_));
            ml::setHumanTarget(sample, ::riftbound::openspiel::encodeActions(legal, st), choice);
            if (clean) sample.flags |= ml::kPolicyFlagCleanBoundary;
            if (sample.source == ml::kPolicyHuman) export_samples_.push_back(std::move(sample));
        }
        if (known) ++known_total;
        if (known && legal.size() <= 1) ++trivial;
        if (is_compared) {
            const bool agree = bot_idx == choice;
            auto& bk = by_kind[kind];
            bk.first++;
            if (agree) bk.second++;
            if (clean) { ++compared_clean; if (agree) ++agree_clean; }
            Compared c{st.decision_index, st.turn.turn_number, seatIndex(P), choice, bot_idx, kind,
                       ev ? ev->text : "", describeIntent(legal[choice], st, &db_), describeIntent(legal[bot_idx], st, &db_),
                       agree, clean, shares, {}};
            if (!agree) for (const auto& in : legal) c.options.push_back(describeIntent(in, st, &db_));
            compared.push_back(std::move(c));
        }

        // ── Frame ──
        review.record(st, legal, legal[choice], shares, std::move(review_lines));
        review_lines.clear();
        nlohmann::json extra = {{"human_choice", choice}, {"human_known", known}, {"forced", forced},
                                {"bot_choice", bot_idx}, {"kind", kind}, {"clean_boundary", clean},
                                {"event_text", ev ? ev->text : ""}, {"event_index", ev ? ev->index : -1},
                                {"event_kind", ev ? ev->kind : ""}};
        if (is_compared) extra["agree"] = bot_idx == choice;
        if (fallback) extra["bot_fallback"] = true;
        if (!note.empty()) extra["note"] = note;
        review.annotateLast(extra);
        if (opts_.verbose)
            std::cerr << "#" << st.decision_index << " T" << st.turn.turn_number << " P" << seatIndex(P) << " "
                      << toString(st.turn.phase) << " | " << describeIntent(legal[choice], st, &db_)
                      << (ev ? " <= \"" + ev->text + "\"" : "")
                      << (is_compared ? (bot_idx == choice ? " | bot agrees" : " | bot: " + describeIntent(legal[bot_idx], st, &db_)) : "")
                      << "\n";

        ++decisions_;
        choice_history.push_back(choice);
        const Intent applied = legal[static_cast<size_t>(choice)];
        const int applied_line = ev ? static_cast<int>(ev - game_.events.data()) : -1;
        // A play from hand the log pays below the engine's cost for it.
        if (applied_line >= 0 && ev->isCardPlay() && (ev->from.empty() || normalizeName(ev->from) == "hand") &&
            isPlayType(applied.type) && st.objectExists(applied.card) &&
            st.getObject(applied.card).zone == ZoneType::Hand) {
            int pe = 0, pp = 0;
            bool clean = true;
            const int n = loggedPaymentOf(static_cast<size_t>(applied_line), pe, pp, clean);
            const auto cost = engine.currentPlayCost(P, applied.card, applied.play_location);
            if (n > 0 && clean && cost.valid && st.player(P).rune_pool.energy == 0 &&
                st.player(P).rune_pool.totalPower() == 0 &&
                (pe < cost.energy || pe + pp < cost.energy + cost.power + cost.extra_any_power))
                underpaid_[seatIndex(P)] = {st.turn.turn_number, static_cast<size_t>(applied_line),
                                            "\"" + ev->text + "\" shows " + std::to_string(pe) + "E/" +
                                                std::to_string(pp) + "P for a cost of " + std::to_string(cost.energy) +
                                                "E/" + std::to_string(cost.power + cost.extra_any_power) + "P"};
        }
        sr = engine.applyChoice(choice);
        // A unit the engine has readied since is no longer the log's to judge.
        for (auto it = log_exhausted_at_.begin(); it != log_exhausted_at_.end();) {
            const GameState& now = engine.state();
            if (!now.objectExists(it->first) || !now.getObject(it->first).is_exhausted ||
                !now.getObject(it->first).location.has_value())
                it = log_exhausted_at_.erase(it);
            else
                ++it;
        }
        // What the matched line exhausted, for the artefact classifier.
        if (applied_line >= 0 && ev && (ev->kind == "moved" || ev->isCardPlay())) {
            if (applied.type == IntentType::StandardMove)
                for (auto id : applied.units_to_move) noteLogExhausted(id, static_cast<size_t>(applied_line));
            else if (isPlayType(applied.type) && engine.state().objectExists(applied.card) &&
                     engine.state().getObject(applied.card).isUnit())
                noteLogExhausted(applied.card, static_cast<size_t>(applied_line));
        }
    }

    // ── Game end ──
    GameState& ms = engine.mutableState();
    const GameState& fs = engine.state();
    std::string reason;
    if (sr.kind == StepKind::Done || fs.game_over) {
        reason = fs.game_over_reason.empty() ? "game over" : fs.game_over_reason;
        if (stop_reason_.empty()) stop_reason_ = "engine game over";
    } else {
        if (stop_reason_.empty()) stop_reason_ = "log ended";
        int log1 = 0, log2 = 0;
        if (game_.players.size() > 0) { auto it = game_.score_from_log.find(game_.players[0]); if (it != game_.score_from_log.end()) log1 = it->second; }
        if (game_.players.size() > 1) { auto it = game_.score_from_log.find(game_.players[1]); if (it != game_.score_from_log.end()) log2 = it->second; }
        ms.game_over = true;
        if (log1 != log2) ms.winner = log1 > log2 ? PlayerId::Player1 : PlayerId::Player2;
        else if (fs.players[0].score != fs.players[1].score)
            ms.winner = fs.players[0].score > fs.players[1].score ? PlayerId::Player1 : PlayerId::Player2;
        reason = stop_reason_ + " (log score " + std::to_string(log1) + "-" + std::to_string(log2) + ")";
        ms.game_over_reason = reason;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();

    if (!opts_.export_features_path.empty() && !export_samples_.empty()) {
        // The label is the log's result (the engine's when the log has no
        // scores); the game id hashes the source file and game number.
        int l1 = 0, l2 = 0;
        if (game_.players.size() > 0 && game_.score_from_log.count(game_.players[0])) l1 = game_.score_from_log.at(game_.players[0]);
        if (game_.players.size() > 1 && game_.score_from_log.count(game_.players[1])) l2 = game_.score_from_log.at(game_.players[1]);
        if (l1 == 0 && l2 == 0) { l1 = fs.players[0].score; l2 = fs.players[1].score; }
        uint32_t gid = 2166136261u;
        for (unsigned char c : game_.stem) { gid ^= c; gid *= 16777619u; }
        gid ^= static_cast<uint32_t>(game_.game_number) * 0x9E3779B1u;
        ml::appendValueRecords(opts_.export_features_path, ml::kStateFeatureDim, export_samples_,
                               fs.winner, l1, l2, fs.mode.victory_score, gid);
    }

    GuidedResult out;
    out.header = {
        {"game", 1}, {"seed", seed}, {"uuid", ""},
        {"deck1", opts_.deck1_label}, {"deck2", opts_.deck2_label},
        {"agent1", "human:" + (game_.players.size() > 0 ? game_.players[0] : std::string("P1"))},
        {"agent2", "human:" + (game_.players.size() > 1 ? game_.players[1] : std::string("P2"))},
        {"winner", fs.winner == PlayerId::Player1 ? 1 : fs.winner == PlayerId::Player2 ? 2 : 0},
        {"scores", {fs.players[0].score, fs.players[1].score}},
        {"turns", fs.turn.turn_number}, {"decisions", decisions_}, {"reason", reason},
        {"guided", true}, {"source", game_.path}, {"source_game", game_.game_number},
        {"players", game_.players}, {"capture_player", game_.capture_player}, {"capture_seat", 1},
        {"bot", opts_.bot_spec}, {"compare_both", opts_.compare_both}};

    nlohmann::json by_kind_json = nlohmann::json::object();
    for (const auto& [k, v] : by_kind)
        by_kind_json[k] = {{"n", v.first}, {"agree", v.second}, {"rate", v.first ? double(v.second) / v.first : 0.0}};
    int agree_total = 0;
    for (const auto& c : compared) agree_total += c.agree ? 1 : 0;
    nlohmann::json disagreements = nlohmann::json::array();
    for (const auto& c : compared) {
        if (c.agree) continue;
        std::vector<std::pair<int, double>> top = c.search;
        std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        nlohmann::json top3 = nlohmann::json::array();
        for (size_t i = 0; i < top.size() && i < 3; ++i)
            top3.push_back({{"index", top[i].first},
                            {"text", top[i].first >= 0 && top[i].first < static_cast<int>(c.options.size()) ? c.options[top[i].first] : ""},
                            {"share", top[i].second}});
        double human_share = 0;
        for (const auto& [i, sh] : c.search) if (i == c.human) human_share = sh;
        disagreements.push_back({{"decision", c.n}, {"turn", c.turn}, {"player", c.player}, {"kind", c.kind},
                                 {"clean_boundary", c.clean}, {"event_text", c.event},
                                 {"human", {{"index", c.human}, {"text", c.human_text}, {"share", human_share}}},
                                 {"bot", {{"index", c.bot}, {"text", c.bot_text}}}, {"top3", top3}});
    }
    nlohmann::json causes = nlohmann::json::object();
    for (const auto& [k, v] : desync_causes_) causes[k] = v;
    int actors_reattributed = 0, actors_uncertain = 0;
    for (const auto& e : game_.events) {
        if (!e.actor_inferred) continue;
        if (e.actor_uncertain) ++actors_uncertain;
        else if (e.actor != game_.capture_player) ++actors_reattributed;
    }
    const int n_log1 = game_.players.size() > 0 && game_.score_from_log.count(game_.players[0]) ? game_.score_from_log.at(game_.players[0]) : 0;
    const int n_log2 = game_.players.size() > 1 && game_.score_from_log.count(game_.players[1]) ? game_.score_from_log.at(game_.players[1]) : 0;
    out.summary = {
        {"source", game_.path}, {"game", game_.game_number}, {"players", game_.players},
        {"capture_player", game_.capture_player}, {"first_player", game_.first_player},
        {"legends", {{"P1", decks_[0]->notes.value("legend", "")}, {"P2", decks_[1]->notes.value("legend", "")}}},
        {"bot", opts_.bot_spec}, {"compare_both", opts_.compare_both}, {"seed", seed},
        {"decisions", decisions_}, {"decisions_human_known", known_total}, {"decisions_trivial", trivial},
        {"decisions_compared", static_cast<int>(compared.size())},
        {"agreement", {{"n", static_cast<int>(compared.size())}, {"agree", agree_total},
                       {"rate", compared.empty() ? 0.0 : double(agree_total) / compared.size()},
                       {"by_kind", by_kind_json},
                       {"clean_boundary", {{"n", compared_clean}, {"agree", agree_clean},
                                           {"rate", compared_clean ? double(agree_clean) / compared_clean : 0.0}}},
                       {"in_flight", {{"n", static_cast<int>(compared.size()) - compared_clean},
                                      {"agree", agree_total - agree_clean}}}}},
        {"desyncs", desyncs_}, {"log_artefacts", log_artefacts_}, {"desyncs_engine", desyncs_ - log_artefacts_},
        {"desync_causes", causes}, {"desync_list", desync_list_},
        {"silent_passes", silent_passes_}, {"out_of_order_matches", out_of_order_},
        {"rewinds_applied", rewinds_}, {"rewinds_ignored", rewinds_ignored_},
        {"readiness_fixes", readiness_fixes_}, {"effect_lines", effect_lines_}, {"deck_tops", deck_tops_}, {"soft_dropped", soft_dropped_},
        {"actors_reattributed", actors_reattributed}, {"actors_settled", actors_settled_},
        {"actors_uncertain", actors_uncertain},
        {"forced_decisions", forced_}, {"score_mismatches", score_mismatches_},
        {"exported_records", static_cast<int>(export_samples_.size())},
        {"score_mismatch_examples", score_mismatch_list_},
        {"hand_fixes", {{"pulled_from_deck", pulled_}, {"created", created_}, {"list", hand_fixes_}}},
        {"rune_swaps", rune_swaps_},
        {"log_events", static_cast<int>(game_.events.size())},
        {"log_events_consumed", static_cast<int>(std::count(consumed_.begin(), consumed_.end(), 1))},
        {"stop_reason", stop_reason_},
        {"result", {{"engine_winner", out.header["winner"]}, {"engine_scores", out.header["scores"]},
                    {"log_scores", {n_log1, n_log2}}, {"reason", reason}, {"turns", fs.turn.turn_number}}},
        {"bot_calls", bot_calls_}, {"bot_fallbacks", bot_fallbacks_}, {"bot_errors", bot_errors_},
        {"timing", {{"seconds", seconds}, {"bot_seconds", bot_seconds_}}},
        {"inferred_decks", {{"P1", decks_[0]->notes}, {"P2", decks_[1]->notes}}},
        {"disagreements", disagreements}};

    if (!opts_.out_dir.empty()) {
        std::filesystem::create_directories(opts_.out_dir);
        if (fs.game_over) review.finishGame(fs, std::move(review_lines));
        review.finish(out.header, opts_.out_dir + "/game_1.json");
        std::ofstream(opts_.out_dir + "/summary.json") << out.summary.dump(1) << "\n";
    }
    return out;
}

}  // namespace

GuidedResult runGuidedReplay(const HumanGame& game, const DeckInference& p1, const DeckInference& p2,
                             const CardDB& db, const CardRegistry& registry, AgentInterface* bot,
                             const GuidedOptions& opts) {
    Driver d(game, p1, p2, db, registry, bot, opts);
    return d.run();
}

}  // namespace riftbound::guided
