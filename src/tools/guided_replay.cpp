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
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
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
        {
            auto t = e.find("token");
            l.token = t != e.end() && t->is_boolean() && t->get<bool>();
            auto a = e.find("actor_inferred");
            l.actor_inferred = a != e.end() && a->is_boolean() && a->get<bool>();
        }
        if (e.contains("cards") && e["cards"].is_array())
            for (const auto& c : e["cards"]) if (c.is_string()) l.cards.push_back(c.get<std::string>());
        g.events.push_back(std::move(l));
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
        for (size_t back = 1; back <= 2 && back <= i; ++back) {
            const LogLine& prev = ev[i - back];
            if (!isPaymentLine(prev)) break;
            if (!who.empty() && prev.actor != who) break;
            who = prev.actor;
            if (prev.kind == "paid" || isRuneLine(prev)) break;
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
    int index = -1;
    std::string why;               // when index < 0
    std::vector<int> also;         // further log lines this one intent covers (a group move)
};

class Driver {
public:
    Driver(const HumanGame& game, const DeckInference& p1, const DeckInference& p2, const CardDB& db,
           const CardRegistry& registry, AgentInterface* bot, const GuidedOptions& opts)
        : game_(game), decks_{&p1, &p2}, db_(db), registry_(registry), bot_(bot), opts_(opts),
          consumed_(game.events.size(), 0), matched_(game.events.size(), 0) {}

    GuidedResult run();

private:
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
    std::vector<std::pair<std::string, int>> lookaheadNames() const;
    std::set<std::string> upcomingPlays(PlayerId player, int max_plays) const;
    std::string hiddenCardName(const LogLine& e) const;
    Match matchEvent(const LogLine& e, const std::vector<Intent>& legal, const GameState& s, PlayerId p);
    int chooseForced(const std::vector<Intent>& legal, const GameState& s, bool& known, std::string& note);
    int choiceEvidence(const Intent& in, const GameState& s) const;
    int combatEvidence(const Intent& in, const GameState& s) const;
    std::vector<const LogLine*> upcomingLinesOf(PlayerId player, int max_lines, bool stop_at_decision) const;
    int passLike(const std::vector<Intent>& legal) const;
    int scoreIntent(const Intent& in, const GameState& s, const std::vector<std::pair<std::string, int>>& preferred) const;
    void prefetch(GameEngine& engine, const std::vector<Intent>& legal);
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
    PlayerId current_player_ = PlayerId::None;  // whose decision the engine is asking for
    size_t cursor_ = 0;
    void markDecision(int i) {
        consumed_[i] = 1;
        matched_[i] = 1;
        if (i > last_matched_) last_matched_ = i;
    }
    size_t evidenceStart() const { return static_cast<size_t>(last_matched_ + 1); }
    std::vector<std::pair<PlayerId, CardDefId>> pending_hand_;

    GameState* mutable_state_ = nullptr;  // the engine's state, for forced readiness

    // stats
    int decisions_ = 0, desyncs_ = 0, silent_passes_ = 0, out_of_order_ = 0, forced_ = 0;
    int rewinds_ = 0, rewinds_ignored_ = 0, readiness_fixes_ = 0, actors_settled_ = 0;
    int score_mismatches_ = 0, pulled_ = 0, created_ = 0, bot_calls_ = 0, bot_fallbacks_ = 0, bot_errors_ = 0;
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
        if (normalizeName(e.to) == "hand") return false;
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
        for (auto& [id, o] : mutable_state_->objects) {
            if (o.controller != actor || o.is_exhausted == exhausted) continue;
            if (!o.location.has_value() && o.zone != ZoneType::LegendZone) continue;
            if (!(o.isUnit() || o.card_type == CardType::Legend) || !nameMatches(e.card, o.name)) continue;
            if (exhausted && referenced.count(id)) continue;
            o.is_exhausted = exhausted;
            ++readiness_fixes_;
            break;
        }
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
        const std::string& actor = game_.events[found].actor;
        int last = found, prev_any = found, rewind = -1;
        bool adjacent = false;
        for (size_t j = static_cast<size_t>(found) + 1; j < game_.events.size() && j < static_cast<size_t>(found) + 12; ++j) {
            if (consumed_[j]) continue;
            const LogLine& e2 = game_.events[j];
            if (e2.kind == "rewound") {
                if (e2.actor == actor) {
                    rewind = static_cast<int>(j);
                    adjacent = prev_any == last;
                }
                break;
            }
            if (isDecisionEvent(e2, legal, s)) {
                if (e2.actor != actor) break;
                last = static_cast<int>(j);
            }
            prev_any = static_cast<int>(j);
        }
        if (rewind < 0) return found;
        consumed_[rewind] = 1;
        if (!adjacent) { ++rewinds_ignored_; return found; }
        markDecision(last);
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

/// Card names the next log lines touch, with a weight: units that die,
/// are discarded or banished (3) say most about what a spell targeted; units
/// exhausted, readied, revealed, buffed, bounced or equipped (2) next; a unit
/// the deciding player moves or a token they play (1) hints at buffs and
/// effect moves. Lines already consumed since the last decision line still
/// count: they are the effects of what is being decided now.
std::vector<std::pair<std::string, int>> Driver::lookaheadNames() const {
    std::vector<std::pair<std::string, int>> names;
    int seen = 0;
    for (size_t i = evidenceStart(); i < game_.events.size() && seen < opts_.lookahead; ++i) {
        if (matched_[i]) continue;
        ++seen;
        const LogLine& e = game_.events[i];
        if (e.card.empty() && e.kind != "equipped") continue;
        if (e.kind == "killed" || e.kind == "discarded" || e.kind == "banished")
            names.emplace_back(e.card, 3);
        else if (e.kind == "exhausted" || e.kind == "readied" || e.kind == "revealed" ||
                 e.kind == "buff_applied" || (e.kind == "moved" && normalizeName(e.to) == "hand"))
            names.emplace_back(e.card, 2);
        else if ((e.kind == "moved" || (e.kind == "played" && e.token)) && seatOf(e.actor) == current_player_)
            names.emplace_back(e.card, 1);
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
    }
    return "";
}

int Driver::scoreIntent(const Intent& in, const GameState& s, const std::vector<std::pair<std::string, int>>& preferred) const {
    int score = choiceEvidence(in, s) + combatEvidence(in, s);
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
    // "move an enemy unit", "an opponent discards": the effect shows up as
    // the other player's line.
    if (has(label, "enemy") || has(label, "opponent")) {
        const PlayerId other = who == PlayerId::Player1 ? PlayerId::Player2 : PlayerId::Player1;
        for (const auto* e : upcomingLinesOf(other, 8, /*stop_at_decision=*/true))
            if (e->kind == "moved" || e->kind == "killed" || e->kind == "discarded" || e->kind == "exhausted" ||
                e->kind == "readied" || e->kind == "revealed" || e->kind == "revealed_hand")
                lines.push_back(e);
    }
    auto any = [&](auto pred) {
        for (const auto* e : lines) if (pred(*e)) return true;
        return false;
    };
    // An ability's "[1]" / "[A]" shows as "<X> exhausted 1 Calm rune" /
    // "<X> recycled 1 Calm rune"; "<X> paid 2E" lines belong to card plays.
    const bool paid = any([](const LogLine& e) { return (e.kind == "exhausted" || e.kind == "recycled") && e.text.find(" rune") != std::string::npos; });
    const bool to_base = any([](const LogLine& e) { return e.kind == "moved" && normalizeName(e.to) == "base"; });
    const bool to_hand = any([](const LogLine& e) { return e.kind == "moved" && normalizeName(e.to) == "hand"; });
    const bool moved_any = any([](const LogLine& e) { return e.kind == "moved"; });
    const bool token = any([](const LogLine& e) { return e.kind == "played" && e.token; });
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
        int hits = 0;
        if (has(prompt, "pay ")) hits += paid;
        if (has(prompt, "to base") || has(prompt, "recall")) hits += to_base;
        else if (has(prompt, "move")) hits += moved_any;
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
    // Modal labels.
    int score = 0;
    if (has(label, "buff me") || has(label, "[buff]")) score += buff_applied ? 3 : 0;
    if (has(label, "spend")) score += buff_consumed ? 2 : -1;
    if (has(label, "ready")) score += readied ? 2 : -1;
    if (has(label, "stun") && buff_consumed && !readied) score += 1;
    (void)s;
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
            for (size_t j = static_cast<size_t>(e.index) + 1; j < game_.events.size() && run.size() < 4; ++j) {
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
                    m.index = static_cast<int>(i);
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
        if (cands.empty()) {
            // A champion played from the champion zone, or an Ambush unit from hand,
            // is logged as "moved <card> to <location>".
            for (size_t i = 0; i < legal.size(); ++i) {
                const Intent& in = legal[i];
                if (isPlayType(in.type) && nameMatches(e.card, objName(s, in.card)) &&
                    locMatches(s, in.play_location, e.to))
                    cands.push_back(static_cast<int>(i));
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
        m.index = pickBest(cands, [](const Intent&) { return 0; });
        return m;
    }
    if (k == "ended_turn") {
        for (size_t i = 0; i < legal.size(); ++i) if (legal[i].type == IntentType::EndTurn) { m.index = static_cast<int>(i); return m; }
        m.why = "no End turn option (" + std::string(toString(s.turn.phase)) + ")";
        return m;
    }
    if (k == "passed_focus" || k == "showdown") {
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
        std::vector<int> cands;
        for (size_t i = 0; i < legal.size(); ++i)
            if (isAbilityType(legal[i].type) && nameMatches(e.card, objName(s, legal[i].ability_source)))
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

int Driver::chooseForced(const std::vector<Intent>& legal, const GameState& s, bool& known, std::string& note) {
    known = false;
    if (s.cost_cursor.has_value()) { note = "cost payment"; return 0; }
    const auto preferred = lookaheadNames();
    int best = 0, best_score = 0;
    for (size_t i = 0; i < legal.size(); ++i) {
        const int sc = scoreIntent(legal[i], s, preferred);
        if (sc > best_score) { best_score = sc; best = static_cast<int>(i); }
    }
    if (best_score > 0) {
        known = true;
        note = "matched upcoming log lines";
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
            }
        }
    }
    for (const auto& [p, def] : pending_hand_) fix(p, db_.get(def), ZoneType::Hand, upcomingPlays(p, 6), nullptr);
    pending_hand_.clear();
}

void Driver::desync(const LogLine& e, const std::vector<Intent>& legal, const GameState& s, const std::string& reason) {
    ++desyncs_;
    const std::string cause = e.kind + ": " + reason;
    desync_causes_[cause]++;
    nlohmann::json opts = nlohmann::json::array();
    for (size_t i = 0; i < legal.size() && i < 40; ++i) opts.push_back(describeIntent(legal[i], s));
    desync_list_.push_back({{"decision", decisions_}, {"turn", s.turn.turn_number},
                            {"phase", toString(s.turn.phase)}, {"player", seatIndex(legal.empty() ? PlayerId::None : legal[0].player)},
                            {"event_index", e.index}, {"event_text", e.text}, {"event_kind", e.kind},
                            {"reason", reason}, {"options", opts}});
    if (opts_.verbose)
        std::cerr << "  [desync " << desyncs_ << "] turn " << s.turn.turn_number << " ev#" << e.index
                  << " \"" << e.text << "\": " << reason << "\n";
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
    events.on_log.connect([&review_lines](const LogEvent& e) {
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
                if (actor == P) {
                    Match m = matchEvent(e, legal, st, P);
                    if (m.index >= 0) { choice = m.index; markDecision(i); for (int a : m.also) markDecision(a); ev = &e; known = true; break; }
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
                    choice = m.index; markDecision(i); for (int a : m.also) markDecision(a); ev = &e; known = true; note = "actor settled by the engine's turn";
                    break;
                } else {
                    const int pl = passLike(legal);
                    if (pl >= 0) { choice = pl; ++silent_passes_; note = "pass (log continues with the other player)"; break; }
                    Match m;
                    const int j = findInWindow(i, P, legal, st, m);
                    if (j >= 0) { choice = m.index; markDecision(j); for (int a : m.also) markDecision(a); ev = &game_.events[j]; known = true; ++out_of_order_; note = "matched out of order"; break; }
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
                       ev ? ev->text : "", describeIntent(legal[choice], st), describeIntent(legal[bot_idx], st),
                       agree, clean, shares, {}};
            if (!agree) for (const auto& in : legal) c.options.push_back(describeIntent(in, st));
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
                      << toString(st.turn.phase) << " | " << describeIntent(legal[choice], st)
                      << (ev ? " <= \"" + ev->text + "\"" : "")
                      << (is_compared ? (bot_idx == choice ? " | bot agrees" : " | bot: " + describeIntent(legal[bot_idx], st)) : "")
                      << "\n";

        ++decisions_;
        choice_history.push_back(choice);
        sr = engine.applyChoice(choice);
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
        {"desyncs", desyncs_}, {"desync_causes", causes}, {"desync_list", desync_list_},
        {"silent_passes", silent_passes_}, {"out_of_order_matches", out_of_order_},
        {"rewinds_applied", rewinds_}, {"rewinds_ignored", rewinds_ignored_},
        {"readiness_fixes", readiness_fixes_},
        {"actors_reattributed", actors_reattributed}, {"actors_settled", actors_settled_},
        {"actors_uncertain", actors_uncertain},
        {"forced_decisions", forced_}, {"score_mismatches", score_mismatches_},
        {"exported_records", static_cast<int>(export_samples_.size())},
        {"score_mismatch_examples", score_mismatch_list_},
        {"hand_fixes", {{"pulled_from_deck", pulled_}, {"created", created_}, {"list", hand_fixes_}}},
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
