/// @file test_human_replay_fidelity_2.cpp
/// Engine and card gaps behind the first desync of real RiftLite games
/// (lab/tools/human_review.py), second pass:
///   - a battlefield's card never heard of a unit dying there or of a card
///     hidden there: Black Rose Sanctum ("the first time each turn a non-token
///     unit they control dies here, they play a Recruit") and Bandle Scouts'
///     Academy ("when you hide a card here, play a Gold token exhausted") did
///     nothing. Real games: Truce baldazzi's Recruit after Punching Poro died
///     at Black Rose Sanctum; SergeantTV's Gold after hiding at the Academy,
///     spent two turns later on Graves.
///   - Seraphic Wyvern ("if you would channel runes, channel that many plus 1
///     exhausted") missed the channel of "when you play a Mighty unit, you may
///     exhaust me to channel 1 rune exhausted" (Relentless Storm) played with
///     the Wyvern itself: the Wyvern is on the board as that ability resolves
///     (CR 359.2), but it only started counting runes at its first aura pass.
///     Real game: ponti channeled two runes, then cast Elder Dragon for 12.
///   - Last Caress ("the next unit you play this turn can be played to any
///     battlefield") offered units from hand and face down, never the chosen
///     champion in the Champion Zone, and priced them without the discount of
///     the battlefield they go to (Hunters' Circle). Real game: SergeantTV
///     played Evelynn from the Champion Zone to Hunters' Circle for one rune.
///   - [Ambush] ("I may be played to a battlefield where you control units",
///     CR 822.1.b) was only offered as a [Reaction] in showdowns and closed
///     states, never in the open main phase; Rengar, Trophy Hunter's "to a
///     battlefield where there are enemy units" likewise. Real game: Karni
///     played Rengar on her turn to Rakelstake, held by Damus's units.
///   - Treasure Trove's "When this leaves the board, draw 1 and channel 1 rune
///     exhausted" only fired when it died; returned to hand by Pack of Wonders
///     it did nothing. Real game: Damus bounced it twice, each time drawing
///     and channeling, and paid a second Trove with the extra rune.

#include "tests/cards/card_test_fixture.h"

#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace riftbound::test {
namespace {

constexpr CardDefId kBlackRoseSanctum   = 1101;
constexpr CardDefId kBandleScouts       = 1099;
constexpr CardDefId kHuntersCircle      = 966;
constexpr CardDefId kOtterpus           = 840;    // [2] 2-Might unit, no text
constexpr CardDefId kTideturner         = 199;    // [Hidden] unit
constexpr CardDefId kSeraphicWyvern     = 1119;   // [7] 7-Might unit
constexpr CardDefId kRelentlessStorm    = 295;    // Volibear legend
constexpr CardDefId kLastCaress         = 1096;   // [2] [Reaction] spell
constexpr CardDefId kEvelynnEntrancing  = 703;    // [2] champion unit
constexpr CardDefId kRengarTrophyHunter = 682;    // [5][A] champion, [Ambush], enemy battlefields too
constexpr CardDefId kInferna            = 560;    // [2] unit with [Ambush]
constexpr CardDefId kTreasureTrove      = 186;    // gear: "When this leaves the board, draw 1 and channel 1 exhausted"

/// Passes priority; answers "yes"; picks the mode whose label names
/// `mode_label` (else the last mode); picks `want` among offered objects.
class ScriptedAgent : public AgentInterface {
public:
    std::string mode_label;
    GameObjectId want = kInvalidId;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice && f.chosen_value.has_value()) {
            if (!mode_label.empty())
                for (const auto& i : legal)
                    if (i.choice_label.find(mode_label) != std::string::npos) return i;
            for (const auto& i : legal) if (i.chosen_value == 1) return i;
            return legal.back();
        }
        if (f.type == IntentType::MakeChoice) {
            for (const auto& i : legal) {
                for (auto t : i.chosen_objects) if (t == want) return i;
                for (auto t : i.targets) if (t == want) return i;
            }
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class ReplayFidelity2 : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    ScriptedAgent p1_, p2_;
    GameState& s() { return engine_->mutableState(); }

    void makeEngine() {
        engine_ = std::make_unique<GameEngine>(card_db, events, card_registry);
        auto& st = s();
        st.mode = ModeOfPlay{};
        st.players[0].id = P1;
        st.players[1].id = P2;
        for (int i = 0; i < 2; ++i) {
            BattlefieldState bf;
            bf.id = i;
            st.battlefields.push_back(bf);
        }
        engine_->testHook_setAgents(&p1_, &p2_);
        st.turn.turn_player = P1;
        st.turn.turn_number = 5;
        st.turn.phase = TurnPhase::MainPhase;
        st.turn.ns_state = NeutralShowdownState::Neutral;
        st.turn.oc_state = OpenClosedState::Open;
    }
    GameObjectId setLegend(PlayerId p, CardDefId def_id = kInvalidId) {
        auto id = s().createObject();
        auto& lo = s().getObject(id);
        lo.owner = lo.controller = p;
        lo.card_type = CardType::Legend;
        lo.card_def_id = def_id;
        if (def_id != kInvalidId) lo.name = card_db.get(def_id).name;
        lo.zone = ZoneType::LegendZone;
        s().player(p).legend_zone = id;
        return id;
    }
    void setBattlefieldCard(BattlefieldId bf, CardDefId def_id) {
        auto id = s().createObject();
        auto& o = s().getObject(id);
        o.card_type = CardType::Battlefield;
        o.card_def_id = def_id;
        o.name = card_db.get(def_id).name;
        o.zone = ZoneType::BattlefieldZone;
        s().battlefields[bf].card_object_id = id;
    }
    void stamp(GameObject& u, PlayerId p, CardDefId def_id) {
        const auto& def = card_db.get(def_id);
        u.owner = u.controller = p;
        u.card_def_id = def_id;
        u.card_type = def.card_type;
        u.super_type = def.super_type;
        u.name = def.name;
        u.domains = def.domains;
        u.keywords = def.keywords;
        u.tags = def.tags;
        u.base_might = def.might;
        u.might_bonus = def.might_bonus;
    }
    GameObjectId boardUnit(PlayerId p, CardDefId def_id, int at_bf = -1) {
        auto id = s().createObject();
        auto& u = s().getObject(id);
        stamp(u, p, def_id);
        if (at_bf >= 0) {
            u.zone = ZoneType::BattlefieldZone;
            u.location = BattlefieldLocation{static_cast<BattlefieldId>(at_bf)};
        } else {
            u.zone = ZoneType::Base;
            u.location = BaseLocation{p};
        }
        u.recomputeMight();
        return id;
    }
    GameObjectId cardInHand(PlayerId p, CardDefId def_id) {
        auto id = s().createObject();
        stamp(s().getObject(id), p, def_id);
        s().getObject(id).zone = ZoneType::Hand;
        s().player(p).hand.push_back(id);
        return id;
    }
    GameObjectId rune(PlayerId p, Domain d, bool exhausted = false) {
        auto id = s().createObject();
        auto& r = s().getObject(id);
        r.owner = r.controller = p;
        r.card_type = CardType::Rune;
        r.name = std::string(toString(d)) + " Rune";
        r.domains = {d};
        r.zone = ZoneType::Base;
        r.location = BaseLocation{p};
        r.is_exhausted = exhausted;
        return id;
    }
    GameObjectId deckRune(PlayerId p, Domain d) {
        auto id = s().createObject();
        auto& r = s().getObject(id);
        r.owner = r.controller = p;
        r.card_type = CardType::Rune;
        r.name = std::string(toString(d)) + " Rune";
        r.domains = {d};
        r.zone = ZoneType::RuneDeck;
        s().player(p).rune_deck.push_back(id);
        return id;
    }
    int baseRunes(PlayerId p) {
        int n = 0;
        for (const auto& [id, o] : s().objects)
            if (o.isRune() && o.controller == p && o.location.has_value()) ++n;
        return n;
    }
    int named(PlayerId p, const std::string& name, bool at_base = true) {
        int n = 0;
        for (const auto& [id, o] : s().objects)
            if (o.controller == p && o.name == name && o.location.has_value() &&
                (!at_base || std::holds_alternative<BaseLocation>(*o.location)))
                ++n;
        return n;
    }
};

// ── Black Rose Sanctum: a death at the battlefield reaches its card ──

TEST_F(ReplayFidelity2, BlackRoseSanctum_FirstNonTokenDeathHerePlaysARecruitOncePerTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kBlackRoseSanctum);
    auto first = boardUnit(P2, kOtterpus, 0);
    auto second = boardUnit(P2, kOtterpus, 0);

    engine_->testHook_executor().killObject(first);
    engine_->testHook_runChain();
    EXPECT_EQ(named(P2, "Recruit"), 1) << "the dying unit's controller plays a Recruit to their base";
    EXPECT_EQ(named(P1, "Recruit"), 0);

    engine_->testHook_executor().killObject(second);
    engine_->testHook_runChain();
    EXPECT_EQ(named(P2, "Recruit"), 1) << "only the first time each turn";
}

TEST_F(ReplayFidelity2, BlackRoseSanctum_DeathAtTheOtherBattlefieldDoesNotCount) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kBlackRoseSanctum);
    auto elsewhere = boardUnit(P2, kOtterpus, 1);
    engine_->testHook_executor().killObject(elsewhere);
    engine_->testHook_runChain();
    EXPECT_EQ(named(P2, "Recruit"), 0);
}

// ── Bandle Scouts' Academy: a hide at the battlefield reaches its card ──

TEST_F(ReplayFidelity2, BandleScoutsAcademy_HidingHerePlaysAnExhaustedGold) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kBandleScouts);
    s().battlefields[0].controller = P1;
    boardUnit(P1, kOtterpus, 0);
    rune(P1, Domain::Chaos);
    auto card = cardInHand(P1, kTideturner);

    Intent hide;
    for (const auto& a : engine_->generateLegalActions())
        if (a.type == IntentType::HideCard && a.card == card && a.chosen_battlefield == 0) hide = a;
    ASSERT_EQ(hide.type, IntentType::HideCard) << "Tideturner can be hidden at the Academy";
    engine_->testHook_executeIntent(hide);
    engine_->testHook_runChain();

    ASSERT_TRUE(s().getObject(card).is_hidden);
    ASSERT_EQ(named(P1, "Gold"), 1) << "hiding here plays a Gold token";
    for (const auto& [id, o] : s().objects)
        if (o.name == "Gold" && o.controller == P1) EXPECT_TRUE(o.is_exhausted) << "exhausted";
    EXPECT_EQ(named(P2, "Gold"), 0);
}

// ── Seraphic Wyvern: a channel resolving after its play is replaced ──

TEST_F(ReplayFidelity2, SeraphicWyvern_ChannelFromItsOwnPlayGetsTheExtraRune) {
    makeEngine();
    setLegend(P1, kRelentlessStorm);
    setLegend(P2);
    for (int i = 0; i < 7; ++i) rune(P1, Domain::Body);
    for (int i = 0; i < 4; ++i) deckRune(P1, Domain::Body);
    auto wyvern = cardInHand(P1, kSeraphicWyvern);

    Intent play;
    play.type = IntentType::PlayCard;
    play.player = P1;
    play.card = wyvern;
    play.play_location = LocationId{BaseLocation{P1}};
    engine_->testHook_executeIntent(play);   // Relentless Storm: "yes"
    engine_->testHook_runChain();
    engine_->testHook_cleanup();

    ASSERT_TRUE(s().getObject(wyvern).location.has_value());
    EXPECT_TRUE(s().getObject(s().player(P1).legend_zone).is_exhausted) << "Relentless Storm was used";
    EXPECT_EQ(baseRunes(P1), 9) << "7 + 1 channeled by Relentless Storm + 1 more for the Wyvern";
    EXPECT_EQ(s().player(P1).rune_deck.size(), 2u);
}

// ── Last Caress: the champion in the Champion Zone, at the discounted cost ──

TEST_F(ReplayFidelity2, LastCaress_PlaysTheChampionToABattlefieldWithItsPlayDiscount) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    setBattlefieldCard(0, kHuntersCircle);
    auto& ps = s().player(P1);
    auto champ = s().createObject();
    stamp(s().getObject(champ), P1, kEvelynnEntrancing);
    s().getObject(champ).zone = ZoneType::ChampionZone;
    ps.champion_zone = champ;
    auto only_rune = rune(P1, Domain::Chaos);
    auto caress = cardInHand(P1, kLastCaress);
    ps.rune_pool.energy = 2;   // Last Caress's own cost

    p1_.mode_label = "Hunters' Circle";
    p1_.want = champ;
    Intent play;
    play.type = IntentType::PlayCard;
    play.player = P1;
    play.card = caress;
    engine_->testHook_executeIntent(play);
    engine_->testHook_runChain();

    const auto& ev = s().getObject(champ);
    ASSERT_TRUE(ev.location.has_value()) << "Evelynn left the Champion Zone";
    EXPECT_EQ(*ev.location, LocationId{BattlefieldLocation{0}}) << "at Hunters' Circle";
    EXPECT_GT(ev.buff_count, 0) << "buffed";
    EXPECT_TRUE(s().getObject(only_rune).is_exhausted) << "[2] less Hunters' Circle's [1]: one rune";
    EXPECT_EQ(s().player(P1).champion_zone, kInvalidId);
}

// ── Treasure Trove: leaving the board is not only dying ──

TEST_F(ReplayFidelity2, TreasureTrove_ReturnedToHandDrawsAndChannelsExhausted) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto trove = boardUnit(P1, kTreasureTrove);
    deckRune(P1, Domain::Chaos);
    auto drawn = s().createObject();
    stamp(s().getObject(drawn), P1, kOtterpus);
    s().getObject(drawn).zone = ZoneType::MainDeck;
    s().player(P1).main_deck.push_back(drawn);

    engine_->testHook_executor().bounceToHand(trove);
    engine_->testHook_runChain();

    const auto& hand = s().player(P1).hand;
    EXPECT_NE(std::find(hand.begin(), hand.end(), trove), hand.end()) << "back in hand";
    EXPECT_NE(std::find(hand.begin(), hand.end(), drawn), hand.end()) << "drew 1";
    EXPECT_EQ(baseRunes(P1), 1) << "channeled 1 rune";
    for (const auto& [id, o] : s().objects)
        if (o.isRune() && o.location.has_value()) EXPECT_TRUE(o.is_exhausted) << "exhausted";
}

// ── [Ambush] is a play location in the open main phase too ──

TEST_F(ReplayFidelity2, Ambush_PlaysToABattlefieldWithFriendlyUnitsOnYourTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    s().battlefields[0].controller = P2;      // contested, held by P2
    boardUnit(P1, kOtterpus, 0);
    boardUnit(P2, kOtterpus, 0);
    for (int i = 0; i < 2; ++i) rune(P1, Domain::Fury);
    auto inferna = cardInHand(P1, kInferna);
    auto plain = cardInHand(P1, kOtterpus);

    bool ambush_here = false, plain_here = false;
    for (const auto& a : engine_->generateLegalActions()) {
        if (a.type != IntentType::PlayCard || !a.play_location) continue;
        if (*a.play_location != LocationId{BattlefieldLocation{0}}) continue;
        if (a.card == inferna) ambush_here = true;
        if (a.card == plain) plain_here = true;
    }
    EXPECT_TRUE(ambush_here) << "[Ambush] to a battlefield where P1 has units";
    EXPECT_FALSE(plain_here) << "a unit without it: only to battlefields P1 controls";
}

TEST_F(ReplayFidelity2, Ambush_RengarTrophyHunterGoesWhereEnemyUnitsAre) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    s().battlefields[0].controller = P2;
    boardUnit(P2, kOtterpus, 0);
    for (int i = 0; i < 6; ++i) rune(P1, Domain::Body);
    auto rengar = cardInHand(P1, kRengarTrophyHunter);

    bool there = false, empty_bf = false;
    for (const auto& a : engine_->generateLegalActions()) {
        if (a.type != IntentType::PlayCard || a.card != rengar || !a.play_location) continue;
        if (*a.play_location == LocationId{BattlefieldLocation{0}}) there = true;
        if (*a.play_location == LocationId{BattlefieldLocation{1}}) empty_bf = true;
    }
    EXPECT_TRUE(there) << "enemy units there, none of P1's";
    EXPECT_FALSE(empty_bf) << "no units at all there";
}

}  // namespace
}  // namespace riftbound::test
