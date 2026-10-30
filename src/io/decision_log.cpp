#include "io/decision_log.h"

#include <fstream>
#include <sstream>

namespace riftbound {

namespace {

std::string objName(const GameState& s, GameObjectId id) {
    if (id == kInvalidId || !s.objectExists(id)) return "?";
    return s.getObject(id).name;
}

std::string locName(const GameState& s, const LocationId& loc) {
    if (std::holds_alternative<BaseLocation>(loc)) return "base";
    const auto bf = std::get<BattlefieldLocation>(loc).id;
    for (const auto& b : s.battlefields)
        if (b.id == bf && s.objectExists(b.card_object_id))
            return s.getObject(b.card_object_id).name;
    return "BF#" + std::to_string(static_cast<int>(bf));
}

std::string names(const GameState& s, const std::vector<GameObjectId>& ids) {
    std::string out;
    for (auto id : ids) {
        if (!out.empty()) out += ", ";
        out += objName(s, id);
    }
    return out;
}

nlohmann::json objJson(const GameState& s, const GameObject& o) {
    nlohmann::json j = {{"id", o.id}, {"name", o.name}, {"card", o.card_def_id}};
    if (o.isUnit()) {
        j["might"] = o.current_might;
        if (o.damage_marked) j["damage"] = o.damage_marked;
        if (o.buff_count) j["buffs"] = o.buff_count;
        if (o.is_stunned) j["stunned"] = true;
        if (!o.attachments.empty()) {
            nlohmann::json g = nlohmann::json::array();
            for (auto a : o.attachments) g.push_back(objName(s, a));
            j["gear"] = g;
        }
    }
    if (o.is_exhausted) j["exhausted"] = true;
    if (o.is_hidden) j["facedown"] = true;
    return j;
}

}  // namespace

std::string describeIntent(const Intent& i, const GameState& s, const CardDB* db) {
    std::ostringstream o;
    switch (i.type) {
    case IntentType::PlayCard:
    case IntentType::PlayReaction:
    case IntentType::PlayActionCard:
        o << "Play " << objName(s, i.card);
        if (i.play_location) o << " to " << locName(s, *i.play_location);
        if (!i.targets.empty()) o << " targeting " << names(s, i.targets);
        if (i.use_alt_play_cost) o << " (alternative cost)";
        break;
    case IntentType::StandardMove:
        o << "Move " << names(s, i.units_to_move);
        if (i.move_destination) o << " to " << locName(s, *i.move_destination);
        break;
    case IntentType::ActivateAbility:
    case IntentType::ActivateReactionAbility:
    case IntentType::ActivateActionAbility: {
        const auto& src = objName(s, i.ability_source);
        if (s.objectExists(i.ability_source) && s.getObject(i.ability_source).isGear() &&
            !s.getObject(i.ability_source).attached_to.has_value() && !i.targets.empty()) {
            o << "Equip " << src << " to " << objName(s, i.targets[0]);
            if (i.targets.size() > 1) o << " (exhaust " << names(s, {i.targets.begin() + 1, i.targets.end()}) << ")";
        } else {
            o << "Use " << src;
            if (i.ability_index) o << " ability " << i.ability_index + 1;
            if (!i.targets.empty()) o << " on " << names(s, i.targets);
        }
        break;
    }
    case IntentType::HideCard:
        o << "Hide " << objName(s, i.card);
        if (i.play_location) o << " at " << locName(s, *i.play_location);
        break;
    case IntentType::MakeChoice:
        if (!i.choice_label.empty()) o << i.choice_label;
        else if (!i.chosen_objects.empty()) o << "Choose " << names(s, i.chosen_objects);
        else if (i.chosen_value) o << "Choose " << *i.chosen_value;
        else o << "Choose";
        break;
    case IntentType::AssignCombatDamage: {
        o << "Assign damage:";
        for (const auto& d : i.damage_assignments) o << " " << d.damage << " to " << objName(s, d.target_unit) << ";";
        break;
    }
    case IntentType::MulliganDecision:
        o << (i.cards_to_mulligan.empty() ? std::string("Keep hand")
                                          : "Mulligan " + names(s, i.cards_to_mulligan));
        break;
    case IntentType::ChooseBattlefield: {
        // At setup the choice is an index into the player's battlefield pool
        // (no battlefield object exists yet); card effects reuse the intent
        // for an existing battlefield object.
        const auto& pool = s.player(i.player).battlefield_pool;
        const auto idx = static_cast<size_t>(i.chosen_battlefield);
        if (idx < pool.size() && db)
            o << "Bring battlefield " << db->get(pool[idx]).name;
        else if (i.chosen_battlefield != kInvalidId && s.objectExists(i.chosen_battlefield))
            o << "Bring battlefield " << s.getObject(i.chosen_battlefield).name;
        else
            o << "Bring battlefield #" << (i.chosen_battlefield == kInvalidId ? 0 : i.chosen_battlefield + 1);
        break;
    }
    case IntentType::PlayFirstDecision:
        o << (i.choose_first ? "Go first" : "Go second");
        break;
    case IntentType::EndTurn: o << "End turn"; break;
    case IntentType::PassPriority: o << "Pass"; break;
    case IntentType::PassFocus: o << "Pass focus"; break;
    case IntentType::Concede: o << "Concede"; break;
    default: o << toString(i.type); break;
    }
    return o.str();
}

nlohmann::json boardJson(const GameState& s) {
    nlohmann::json b;
    b["turn"] = s.turn.turn_number;
    b["phase"] = toString(s.turn.phase);
    b["turn_player"] = s.turn.turn_player == PlayerId::Player1 ? 1 : 2;
    nlohmann::json players = nlohmann::json::array();
    for (auto p : {PlayerId::Player1, PlayerId::Player2}) {
        const auto& ps = s.player(p);
        nlohmann::json pj;
        pj["score"] = ps.score;
        pj["deck"] = ps.main_deck.size();
        pj["trash"] = ps.trash.size();
        nlohmann::json hand = nlohmann::json::array();
        for (auto h : ps.hand) hand.push_back(objName(s, h));
        pj["hand"] = hand;
        int ready = 0, exhausted = 0;
        for (auto r : s.runesInBase(p)) (s.getObject(r).is_exhausted ? exhausted : ready)++;
        pj["runes_ready"] = ready;
        pj["runes_exhausted"] = exhausted;
        pj["energy_pool"] = ps.rune_pool.energy;
        if (ps.legend_zone != kInvalidId && s.objectExists(ps.legend_zone))
            pj["legend"] = objJson(s, s.getObject(ps.legend_zone));
        nlohmann::json base_units = nlohmann::json::array(), base_gear = nlohmann::json::array();
        for (const auto& [id, o] : s.objects) {
            if (o.controller != p || !o.isAtBase() || o.attached_to.has_value()) continue;
            if (o.isUnit()) base_units.push_back(objJson(s, o));
            else if (o.isGear()) base_gear.push_back(objJson(s, o));
        }
        pj["base_units"] = base_units;
        pj["base_gear"] = base_gear;
        players.push_back(pj);
    }
    b["players"] = players;
    nlohmann::json bfs = nlohmann::json::array();
    for (const auto& bf : s.battlefields) {
        nlohmann::json bj;
        bj["name"] = s.objectExists(bf.card_object_id) ? s.getObject(bf.card_object_id).name
                                                       : "BF#" + std::to_string(static_cast<int>(bf.id));
        bj["controller"] = bf.controller ? (*bf.controller == PlayerId::Player1 ? 1 : 2) : 0;
        nlohmann::json side[2] = {nlohmann::json::array(), nlohmann::json::array()};
        for (const auto& [id, o] : s.objects) {
            if (o.attached_to.has_value() || o.battlefieldId() != std::optional<BattlefieldId>{bf.id}) continue;
            if (!o.isUnit() && !o.isGear() && !o.is_hidden) continue;
            side[o.controller == PlayerId::Player1 ? 0 : 1].push_back(objJson(s, o));
        }
        bj["p1"] = side[0];
        bj["p2"] = side[1];
        if (bf.combat_in_progress) bj["combat"] = true;
        bfs.push_back(bj);
    }
    b["battlefields"] = bfs;
    nlohmann::json chain = nlohmann::json::array();
    for (const auto& it : s.chain.items)
        chain.push_back({{"source", objName(s, it.source)},
                         {"player", it.controller == PlayerId::Player1 ? 1 : 2}});
    b["chain"] = chain;
    return b;
}

void DecisionLog::record(const GameState& state, const std::vector<Intent>& options,
                         const Intent& chosen, const std::vector<std::pair<int, double>>& search,
                         std::vector<std::string> log_lines) {
    nlohmann::json f;
    f["n"] = state.decision_index;
    f["player"] = chosen.player == PlayerId::Player1 ? 1 : 2;
    f["board"] = boardJson(state);
    nlohmann::json opts = nlohmann::json::array();
    int chosen_idx = -1;
    for (size_t k = 0; k < options.size(); ++k) {
        opts.push_back(describeIntent(options[k], state));
        if (chosen_idx < 0 && options[k] == chosen) chosen_idx = static_cast<int>(k);
    }
    f["options"] = opts;
    f["chosen"] = chosen_idx;
    if (chosen_idx < 0) f["chosen_text"] = describeIntent(chosen, state);
    if (!search.empty()) {
        nlohmann::json sj = nlohmann::json::array();
        for (const auto& [idx, share] : search) sj.push_back({idx, share});
        f["search"] = sj;
    }
    if (!log_lines.empty()) f["log"] = std::move(log_lines);
    frames_.push_back(std::move(f));
}

void DecisionLog::annotateLast(const nlohmann::json& extra) {
    if (frames_.empty() || !extra.is_object()) return;
    for (auto it = extra.begin(); it != extra.end(); ++it) frames_.back()[it.key()] = it.value();
}

void DecisionLog::finish(const nlohmann::json& header, const std::string& path) const {
    nlohmann::json out = header;
    out["frames"] = frames_;
    std::ofstream f(path);
    f << out.dump() << "\n";
}

}  // namespace riftbound
