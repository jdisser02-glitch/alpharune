#pragma once
/// @file guided_replay.h
/// Guided replay of a recorded human game.
///
/// Input: one file of lab/human_games/ (a RiftLite match log normalised by
/// lab/tools/import_riftlite.py). The engine is driven by a scripted policy
/// that, at every decision, takes the legal intent matching the next log line
/// of the deciding player (play X to base, move X to a battlefield, end turn,
/// pass focus, mulligan N, hide a card, equip, ...). Hands are god-moded to
/// follow the log: before a recorded play the card is pulled from that
/// player's deck into their hand (created when the deck has none). At each
/// decision an optional bot is asked what it would have played, so the human
/// move and the bot move can be compared.
///
/// Output: lab/human_replays/<stem>_g<N>/game_1.json in the review-frame
/// format of io/decision_log.h (the dashboard's replay viewer reads it) with
/// per-frame extras, and summary.json with agreement rates, desyncs and the
/// inferred decks.
///
/// This is research tooling: it lives outside the engine and uses only the
/// public step-machine API (beginGame / applyChoice), mutableState() and the
/// legal-action generator.

#include "agents/agent_interface.h"
#include "cards/card_registry.h"
#include "core/card_db.h"
#include "core/game_state.h"
#include "rules/deck_validator.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace riftbound::guided {

/// One line of the match log (the parsed fields the driver uses).
struct LogLine {
    int index = 0;                  // position in the file's `events` array
    std::string actor;              // display name; "both" / "" for actor-less lines
    std::string kind;               // RiftLite kind (played, moved, ended_turn, ...)
    std::string text;
    std::string card;
    std::string from;
    std::string to;
    std::string battlefield;
    int amount = 0;                 // mulligan: cards recycled; scored: points
    int score_to = -1;              // scored: the score after the line
    std::vector<std::string> cards; // revealed_hand / hid_hand
    int seconds = -1;               // the line's timestamp in the capture, when known
    bool token = false;             // played: a token made by an effect, not a card from hand
    bool actor_inferred = false;    // the importer attributed an actor-less line to the capture player
    bool actor_uncertain = false;   // ... and nothing in the log says who really acted (see inferActors)

    /// A recorded play of a real card (not a token).
    bool isCardPlay() const { return kind == "played" && !token && from != "token"; }
};

struct LegendHeader {
    std::string legend_name, legend_code;      // "Riven, The Exile", "RAD-149"
    std::string champion_name, champion_code;  // "Riven, Broken Blade", "RAD-043"
};

/// One game of a human-game file.
struct HumanGame {
    std::string path;
    std::string stem;                        // file name without extension
    std::vector<std::string> players;        // [0] is the capture player
    std::string capture_player;
    std::map<std::string, LegendHeader> legends;  // by player display name
    int game_number = 1;
    std::string first_player;                // who took the first turn
    std::map<std::string, int> score_from_log;
    std::vector<LogLine> events;             // this game's lines only
};

/// Actor-less lines ("Played Defy from hand.") are attributed to the capture
/// player by the importer, whoever played them. Re-attribute each such play
/// to the player whose payment line ("<X> paid 1E / 1P", "<X> exhausted 1
/// Calm rune", "<X> exhausted <unit>") immediately precedes it, else to the
/// player whose payment lines immediately follow it (some captures log the
/// play before its payment) when those lines lead into the spell's effects
/// rather than into another play, or, for a card played from a battlefield,
/// to the player who hid a card there; lines with none of these stay with
/// the capture player and are marked `actor_uncertain`. Returns how many
/// lines changed actor.
int inferActors(HumanGame& game);

/// Parse `games[game_number]` of a human-game file. Throws std::runtime_error
/// when the file or the game is missing.
HumanGame loadHumanGame(const std::string& path, int game_number);
HumanGame humanGameFromJson(const nlohmann::json& file, int game_number,
                            const std::string& path);

/// A deck built from what the log shows for one player.
struct DeckInference {
    DeckSubmission deck;
    std::string deck_text;                      // Piltover-Archive text, loadable by DeckValidator
    std::vector<std::string> battlefield_names; // deck.battlefields, by name, in order
    std::string preferred_battlefield;          // the one the log shows this player bringing
    nlohmann::json notes;                       // seen cards, filler source, drops, validation
};

/// Deck lists usable as filler: lab/decks/meta/**.txt then decks/*.txt under `root`.
std::vector<std::string> findCandidateDeckLists(const std::string& root);

/// Infer `player`'s deck: legend + champion from the header, every card the
/// log attributes to the player, filler from the best-matching candidate list
/// of the same legend (else a list of the same domains, else the card table).
DeckInference inferDeck(const HumanGame& game, const std::string& player,
                        const CardDB& db,
                        const std::vector<std::string>& candidate_lists);

struct GuidedOptions {
    std::string out_dir;          // where game_1.json / summary.json go
    bool compare_both = false;    // ask the bot at both players' decisions
    int max_desyncs = 25;
    int max_decisions = 400;
    uint64_t seed = 1;            // engine seed (< 2^31); the coin toss is matched to the log
    int lookahead = 6;            // log lines inspected for engine-forced choices
    std::string bot_spec;         // recorded in the output header
    std::string deck1_label;      // recorded in the output header
    std::string deck2_label;
    bool verbose = false;
    /// When set, every decision whose human move is known (and that has more
    /// than one option) appends an RBV3 record: the deciding player's masked
    /// features, the legal vocab slots and the human's move as a one-hot
    /// target (ml::kPolicyHuman). The label is the log's final result.
    std::string export_features_path;
};

struct GuidedResult {
    nlohmann::json header;        // the game_1.json header fields
    nlohmann::json summary;       // summary.json
};

/// Replay the game. `bot` may be null (no comparison). Writes the output files
/// when `opts.out_dir` is set and returns the summary either way.
GuidedResult runGuidedReplay(const HumanGame& game, const DeckInference& p1,
                             const DeckInference& p2, const CardDB& db,
                             const CardRegistry& registry, AgentInterface* bot,
                             const GuidedOptions& opts);

// ── Pieces exposed for tests ────────────────────────────────────────────────

/// Lower-case, trimmed, ASCII apostrophes, single spaces, no trailing period.
std::string normalizeName(const std::string& s);

/// `log_name` names `engine_name`: equal after normalisation, or one is the
/// other with a "<Champion>, " prefix (the log says "Riven, The Exile" where
/// the card table says "The Exile").
bool nameMatches(const std::string& log_name, const std::string& engine_name);

/// Card by public code prefix ("RAD-149" matches "RAD-149/167"), else by name.
const CardDef* resolveCard(const CardDB& db, const std::string& name,
                           const std::string& code = "");

/// Make sure `player` holds a card of definition `def` in `zone` (Hand or
/// Trash). Takes one from their main deck when the hand has none — for Hand,
/// swapping a hand card whose name is not in `keep_names` and whose id is not
/// in `protect` back to the bottom of the deck, so the hand size stays what
/// the engine dealt — or creates one when the deck has none either.
/// Returns "already", "pulled" or "created".
std::string ensureCardIn(GameState& state, const CardDB& db, PlayerId player,
                         CardDefId def, ZoneType zone,
                         const std::set<std::string>& keep_names,
                         const std::set<GameObjectId>& protect);

/// A seed in [first, first + 1024) whose coin toss (GameEngine::determineTurnOrder)
/// makes `first_player` the turn player.
uint64_t seedForFirstPlayer(uint64_t first, PlayerId first_player);

}  // namespace riftbound::guided
