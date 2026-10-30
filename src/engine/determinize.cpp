#include "engine/determinize.h"

#include <algorithm>
#include <map>
#include <set>

namespace riftbound::search {

IdentityOverrides sampleHiddenIdentities(const GameState& gs, PlayerId observer,
                                         const CardDB& db,
                                         const std::function<double()>& rng) {
    const auto opp = opponent(observer);

    auto pick = [&](size_t n) {
        size_t k = static_cast<size_t>(rng() * static_cast<double>(n));
        return k >= n ? n - 1 : k;
    };
    auto shuffle = [&](std::vector<CardDefId>& v) {
        for (size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[pick(i)]);
    };
    // Objects the chain refers to (an ability whose source went back to hand,
    // a target) are known to the observer: never re-deal them.
    std::set<GameObjectId> pinned;
    auto pinItem = [&](const ChainItem& it) {
        pinned.insert(it.source);
        pinned.insert(it.targets.begin(), it.targets.end());
        pinned.insert(it.chosen_objects.begin(), it.chosen_objects.end());
    };
    for (const auto& it : gs.chain.items) pinItem(it);
    if (gs.chain.resuming.has_value()) pinItem(*gs.chain.resuming);
    auto free = [&](GameObjectId oid) {
        return gs.objectExists(oid) && !pinned.count(oid) &&
               gs.getObject(oid).card_def_id != kInvalidId;
    };
    std::map<GameObjectId, CardDefId> assign;
    auto deal = [&](const std::vector<GameObjectId>& slots) {
        std::vector<GameObjectId> live;
        std::vector<CardDefId> ids;
        for (auto oid : slots) {
            if (!free(oid)) continue;
            live.push_back(oid);
            ids.push_back(gs.getObject(oid).card_def_id);
        }
        shuffle(ids);
        for (size_t i = 0; i < live.size(); ++i) assign[live[i]] = ids[i];
    };

    // The opponent's hidden cards: hand + Main Deck + facedown share a pool.
    {
        const auto& ps = gs.player(opp);
        std::vector<GameObjectId> facedown;
        for (const auto& bf : gs.battlefields)
            for (auto oid : bf.facedown)
                if (free(oid) && gs.getObject(oid).controller == opp)
                    facedown.push_back(oid);
        std::vector<GameObjectId> slots;
        for (auto oid : ps.hand) if (free(oid)) slots.push_back(oid);
        for (auto oid : ps.main_deck) if (free(oid)) slots.push_back(oid);
        std::vector<CardDefId> pool;
        for (auto oid : slots) pool.push_back(gs.getObject(oid).card_def_id);
        for (auto oid : facedown) pool.push_back(gs.getObject(oid).card_def_id);
        shuffle(pool);
        // Facedown slots first, each from a card that could be hidden.
        for (auto oid : facedown) {
            for (size_t i = 0; i < pool.size(); ++i) {
                if (db.get(pool[i]).keywords.has(Keyword::Hidden)) {
                    assign[oid] = pool[i];
                    pool.erase(pool.begin() + static_cast<long>(i));
                    break;
                }
            }
            if (!assign.count(oid)) {   // no hideable card left: keep it
                auto own = gs.getObject(oid).card_def_id;
                auto it = std::find(pool.begin(), pool.end(), own);
                if (it != pool.end()) pool.erase(it);
                assign[oid] = own;
            }
        }
        size_t k = 0;
        for (auto oid : slots)
            if (k < pool.size()) assign[oid] = pool[k++];
    }
    // Deck orders the observer does not know: its own Main Deck, both Rune Decks.
    deal(gs.player(observer).main_deck);
    deal(gs.player(observer).rune_deck);
    deal(gs.player(opp).rune_deck);

    return IdentityOverrides(assign.begin(), assign.end());
}

void applyIdentityOverrides(GameState& gs, const CardDB& db, const IdentityOverrides& overrides) {
    for (const auto& [oid, def_id] : overrides) {
        if (!gs.objectExists(oid) || def_id == kInvalidId) continue;
        auto& obj = gs.getObject(oid);
        const auto& def = db.get(def_id);
        obj.card_def_id   = def_id;
        obj.name          = def.name;
        obj.card_type     = def.card_type;
        obj.super_type    = def.super_type;
        obj.tags          = def.tags;
        obj.domains       = def.domains;
        obj.base_might    = def.might;
        obj.current_might = def.might;
        obj.keywords      = def.keywords;
        obj.assault_value = def.assault_value;
        obj.shield_value  = def.shield_value;
        obj.deflect_value = def.deflect_value;
        obj.might_bonus   = def.might_bonus;
    }
}

namespace {

/// The loop frames recorded for an in-flight decision agree with the state
/// (the phase, the battlefield they name), so the engine can re-enter them.
bool framesConsistent(const GameState& gs) {
    using Kind = DecisionSiteKind;
    using MainStage = DecisionSite::MainStage;
    const auto& site = gs.decision_site;
    const bool chain_kind = site.kind == Kind::ChainPriority || site.kind == Kind::ChainChoice;
    const auto nbf = gs.battlefields.size();
    switch (gs.turn.phase) {
        case TurnPhase::BeginningStep:
        case TurnPhase::EndingStep:
        case TurnPhase::ExpirationStep:
            return chain_kind && site.showdown_bf == kInvalidId && site.combat_bf == kInvalidId;
        case TurnPhase::ScoringStep:
            return chain_kind && site.showdown_bf == kInvalidId && site.combat_bf == kInvalidId &&
                   site.scoring_bf_index >= 0 && static_cast<size_t>(site.scoring_bf_index) < nbf;
        case TurnPhase::MainPhase:
            if (site.main_stage != MainStage::Advance) {
                // A chain run by the chosen action, or after it.
                return chain_kind && site.showdown_bf == kInvalidId && site.combat_bf == kInvalidId;
            }
            {
                // Inside the showdown / combat of battlefield main_bf_index.
                const BattlefieldId frame_bf = site.combat_bf != kInvalidId ? site.combat_bf : site.showdown_bf;
                if (frame_bf == kInvalidId) return false;
                if (site.main_bf_index < 0 || static_cast<size_t>(site.main_bf_index) >= nbf) return false;
                if (gs.battlefields[static_cast<size_t>(site.main_bf_index)].id != frame_bf) return false;
                if (site.combat_bf != kInvalidId && site.showdown_bf != kInvalidId &&
                    site.combat_bf != site.showdown_bf)
                    return false;
                if (site.kind == Kind::ShowdownFocus) return site.showdown_bf != kInvalidId && !site.showdown_in_intent;
                if (site.kind == Kind::CombatDamage)
                    return site.combat_bf != kInvalidId &&
                           (site.combat_stage == DecisionSite::CombatStage::DamageAttacker ||
                            site.combat_stage == DecisionSite::CombatStage::DamageDefender);
                // A chain: at the start of the combat, or inside a showdown play.
                if (site.showdown_bf != kInvalidId) return site.showdown_in_intent;
                return site.combat_stage == DecisionSite::CombatStage::Start;
            }
        default:
            return false;
    }
}

}  // namespace

bool isResumableSnapshot(const GameState& gs) {
    if (gs.game_over) return false;
    const auto& site = gs.decision_site;
    const bool stamped = site.kind != DecisionSiteKind::None && site.decision_index == gs.decision_index;
    if (stamped) {
        switch (site.kind) {
            case DecisionSiteKind::Mulligan:
                return gs.turn.phase == TurnPhase::Mulligan && !gs.cost_cursor.has_value() &&
                       site.mulligan_index >= 0 && site.mulligan_index < 2;
            case DecisionSiteKind::ChainPriority:
                return gs.chain.exists() && gs.turn.priority_holder.has_value() &&
                       !gs.cost_cursor.has_value() && framesConsistent(gs);
            case DecisionSiteKind::ChainChoice:
                return gs.chain.resuming.has_value() && !site.chain_choice_legal.empty() &&
                       !gs.cost_cursor.has_value() && framesConsistent(gs);
            case DecisionSiteKind::ShowdownFocus:
            case DecisionSiteKind::CombatDamage:
                return gs.turn.phase == TurnPhase::MainPhase && !gs.cost_cursor.has_value() &&
                       framesConsistent(gs);
            case DecisionSiteKind::Other:
                return false;
            case DecisionSiteKind::MainPhase: {
                // A main-phase action: the prelude (advanceMainPhase) is re-run,
                // which is exact as long as it has nothing staged to run first.
                // The chain may exist (a trigger left over after a combat is
                // answered in Closed state from this very loop).
                if (gs.turn.phase != TurnPhase::MainPhase || gs.cost_cursor.has_value()) return false;
                if (site.main_stage != DecisionSite::MainStage::Advance) return false;
                for (const auto& bf : gs.battlefields)
                    if (bf.combat_in_progress || bf.showdown_in_progress || bf.combat_staged || bf.showdown_staged)
                        return false;
                return true;
            }
            case DecisionSiteKind::None:
                break;   // the clean-boundary test
        }
    }
    return isCleanMainPhaseSnapshot(gs);
}

bool isCleanMainPhaseSnapshot(const GameState& gs) {
    if (gs.game_over) return false;
    if (gs.turn.phase == TurnPhase::Setup || gs.turn.phase == TurnPhase::Mulligan) return false;
    if (gs.turn.oc_state != OpenClosedState::Open) return false;
    if (gs.turn.ns_state != NeutralShowdownState::Neutral) return false;
    if (gs.chain.exists() || gs.cost_cursor.has_value()) return false;
    for (const auto& bf : gs.battlefields)
        if (bf.combat_in_progress || bf.showdown_in_progress || bf.combat_staged || bf.showdown_staged)
            return false;
    return true;
}

}  // namespace riftbound::search
