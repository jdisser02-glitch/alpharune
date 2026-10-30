#pragma once
/// @file determinize.h
/// Determinization for information-set search: re-deal everything one player
/// cannot see in a GameState, so a search started from the result only uses
/// what that player can know. Shared by RiftboundState::ResampleFromInfostate
/// (the OpenSpiel ISMCTS path) and agents/ismcts_search (the engine's own
/// searcher), so both sample hidden information the same way.

#include "core/card_db.h"
#include "core/game_state.h"

#include <functional>
#include <utility>
#include <vector>

namespace riftbound::search {

/// (object, card id) pairs: the identity each re-dealt object takes.
using IdentityOverrides = std::vector<std::pair<GameObjectId, CardDefId>>;

/// Sample identities for everything `observer` cannot see in `gs`: the
/// opponent's hand, Main Deck and facedown cards share one pool (facedown
/// slots only take cards with [Hidden]); the observer's own Main Deck and both
/// Rune Decks are shuffled. Objects the chain refers to are known and never
/// re-dealt. `rng` returns uniform doubles in [0, 1). Public zones and the
/// observer's hand are untouched, so the observer's view of the result is the
/// same as of `gs`.
IdentityOverrides sampleHiddenIdentities(const GameState& gs, PlayerId observer,
                                         const CardDB& db,
                                         const std::function<double()>& rng);

/// Give each object in `overrides` the printed characteristics of its new
/// card (name, type, tags, domains, base Might, keywords, ...). Objects that
/// no longer exist are skipped.
void applyIdentityOverrides(GameState& gs, const CardDB& db, const IdentityOverrides& overrides);

/// True when GameEngine::resumeFromSnapshot can restart a fresh engine from
/// this state at the decision it was taken at: a clean main-phase boundary
/// (isCleanMainPhaseSnapshot), or a decision whose record in
/// `GameState::decision_site` is current and names a site the engine
/// re-enters from the state (mulligan, chain priority, a resolving card's
/// choice, showdown focus, combat damage) with consistent loop frames.
/// Other decision points (inside a card effect, a cost payment, a
/// finalization) are rebuilt by replaying the game.
bool isResumableSnapshot(const GameState& gs);

/// The clean boundary: a decision in the Open / Neutral main phase with no
/// chain, no cost payment in progress and no combat or showdown at any
/// battlefield. Resuming here re-runs the main-phase prelude, which is
/// idempotent; no decision record is needed.
bool isCleanMainPhaseSnapshot(const GameState& gs);

}  // namespace riftbound::search
