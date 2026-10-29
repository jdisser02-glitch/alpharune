/// @file test_radiance_b06.cpp
/// Radiance batch 06: Cataclysm (spell lockout + move to an occupied enemy
/// battlefield), K'Sante, Courageous (hold: channel 1), Portable Portal
/// ([Deploy], pay [O] to ready a unit here, Ganking here), Staunch Survivor
/// (Assault 2 / Shield 2), Titanic Shrine (exhaust to draw on playing the
/// mightiest unit), Evelynn, Consuming (face-down play: ready, +1), Jarvan IV,
/// Martial Paragon ([Deploy] gear with [Action]; conquer: channel exhausted),
/// Disposal Expert (enters ready alone, Disarm 2), Momentary Companion
/// ([Temporary] off-turn) and Satchel Charge (move, then a Bomb).

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kCataclysm     = 1045;
constexpr CardDefId kKSante        = 1046;
constexpr CardDefId kPortal        = 1047;
constexpr CardDefId kSurvivor      = 1048;
constexpr CardDefId kShrine        = 1049;
constexpr CardDefId kEvelynn       = 1050;
constexpr CardDefId kJarvan        = 1051;
constexpr CardDefId kExpert        = 1052;
constexpr CardDefId kCompanion     = 1053;
constexpr CardDefId kSatchel       = 1054;
constexpr CardDefId kOtterpus      = 840;  // 2-energy 2-Might unit, no play text
constexpr CardDefId kScorcher      = 1;    // 5-energy 5-Might unit
constexpr CardDefId kConcertPoster = 983;  // [Deploy] gear, Energy 1

/// Answers int prompts (yes/no, modes, battlefields) from `answers`, picks
/// `want` when offered objects, passes priority otherwise. Records every
/// prompt it was shown.
class B06Agent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    GameObjectId want = kInvalidId;
    std::vector<std::vector<int>> int_prompts;
    std::vector<std::vector<GameObjectId>> obj_prompts;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice && f.chosen_value.has_value()) {
            std::vector<int> offered;
            for (const auto& i : legal) offered.push_back(i.chosen_value.value_or(-1));
            int_prompts.push_back(offered);
            if (next < answers.size()) {
                int a = answers[next++];
                for (const auto& i : legal) if (i.chosen_value == a) return i;
            }
            return f;
        }
        if (f.type == IntentType::MakeChoice && !f.chosen_objects.empty()) {
            std::vector<GameObjectId> offered;
            for (const auto& i : legal)
                if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects.front());
            obj_prompts.push_back(offered);
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        for (const auto& i : legal) if (i.type == IntentType::PassFocus) return i;
        return f;
    }
};

class RadianceB06Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B06Agent p1_, p2_;
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
        setLegend(P1);
        setLegend(P2);
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
    void stamp(GameObject& c, PlayerId p, CardDefId def_id) {
        const auto& def = card_db.get(def_id);
        c.owner = c.controller = p;
        c.card_def_id = def_id;
        c.card_type = def.card_type;
        c.super_type = def.super_type;
        c.name = def.name;
        c.domains = def.domains;
        c.keywords = def.keywords;
        c.tags = def.tags;
        c.base_might = def.might;
        c.might_bonus = def.might_bonus;
    }
    GameObjectId boardObject(PlayerId p, CardDefId def_id, int at_bf = -1) {
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
        auto& c = s().getObject(id);
        stamp(c, p, def_id);
        c.recomputeMight();
        c.zone = ZoneType::Hand;
        s().player(p).hand.push_back(id);
        return id;
    }
    /// Puts a card on TOP of `p`'s Main Deck (back = top).
    GameObjectId deckCard(PlayerId p, CardDefId def_id) {
        auto id = s().createObject();
        auto& c = s().getObject(id);
        stamp(c, p, def_id);
        c.zone = ZoneType::MainDeck;
        s().player(p).main_deck.push_back(id);
        return id;
    }
    /// Puts a rune on top of `p`'s rune deck.
    GameObjectId runeInDeck(PlayerId p, Domain d) {
        auto id = s().createObject();
        auto& r = s().getObject(id);
        r.owner = r.controller = p;
        r.card_type = CardType::Rune;
        r.name = "Rune";
        r.domains = {d};
        r.zone = ZoneType::RuneDeck;
        s().player(p).rune_deck.push_back(id);
        return id;
    }
    void play(GameObjectId card, std::optional<LocationId> where = std::nullopt) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
    }
    bool inHand(PlayerId p, GameObjectId id) {
        const auto& h = s().player(p).hand;
        return std::find(h.begin(), h.end(), id) != h.end();
    }
    static bool contains(const std::vector<GameObjectId>& v, GameObjectId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
    int bodyPower(PlayerId p) { return s().player(p).rune_pool.power[static_cast<int>(Domain::Body)]; }
    std::vector<GameObjectId> bombsAt(PlayerId p, BattlefieldId bf) {
        std::vector<GameObjectId> out;
        for (auto& [id, o] : s().objects)
            if (o.isGear() && o.controller == p && o.name == "Bomb" && o.battlefieldId() == bf)
                out.push_back(id);
        return out;
    }
};

// ─── RAD-084 Cataclysm ──────────────────────────────────────────────────────

TEST_F(RadianceB06Test, Cataclysm_MovesAFriendlyUnitToAnOccupiedEnemyBattlefield) {
    makeEngine();
    auto mine = boardObject(P1, kScorcher);
    boardObject(P2, kOtterpus, 0);
    s().player(P1).rune_pool.energy = 4;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 1;
    auto spell = cardInHand(P1, kCataclysm);
    p1_.answers = {0};   // battlefield 0
    p1_.want = mine;
    play(spell);
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0})) << "BF1 has no enemy units";
    EXPECT_EQ(s().getObject(mine).battlefieldId(), std::optional<BattlefieldId>{0});
    EXPECT_TRUE(s().player(P2).cant_play_spells_this_turn);
    EXPECT_FALSE(s().player(P1).cant_play_spells_this_turn);
}

TEST_F(RadianceB06Test, Cataclysm_LocksSpellsEvenWithNowhereToMove) {
    makeEngine();
    auto mine = boardObject(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 4;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 1;
    auto spell = cardInHand(P1, kCataclysm);
    play(spell);
    EXPECT_TRUE(s().player(P2).cant_play_spells_this_turn);
    EXPECT_TRUE(s().getObject(mine).isAtBase());
    EXPECT_TRUE(p1_.int_prompts.empty()) << "no occupied enemy battlefield";
}

// ─── RAD-086 K'Sante, Courageous ────────────────────────────────────────────

TEST_F(RadianceB06Test, KSante_HoldChannelsOneReadyRune) {
    makeEngine();
    boardObject(P1, kKSante, 0);
    auto r = runeInDeck(P1, Domain::Body);
    engine_->testHook_scoreHold(P1, 0);
    engine_->testHook_runChain();
    ASSERT_TRUE(s().getObject(r).location.has_value());
    EXPECT_TRUE(s().getObject(r).isAtBase());
    EXPECT_FALSE(s().getObject(r).is_exhausted);
}

TEST_F(RadianceB06Test, KSante_HasAccelerateAndShieldTwo) {
    makeEngine();
    const auto& d = card_db.get(kKSante);
    EXPECT_TRUE(d.keywords.has(Keyword::Accelerate));
    EXPECT_TRUE(d.keywords.has(Keyword::Shield));
    EXPECT_EQ(d.shield_value, 2);
}

// ─── RAD-087 Portable Portal ────────────────────────────────────────────────

TEST_F(RadianceB06Test, Portal_PayBodyToReadyAUnitHere) {
    makeEngine();
    auto ally = boardObject(P1, kOtterpus, 1);
    s().getObject(ally).is_exhausted = true;
    auto elsewhere = boardObject(P1, kOtterpus, 0);
    s().getObject(elsewhere).is_exhausted = true;
    s().player(P1).rune_pool.energy = 2;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 1;
    auto portal = cardInHand(P1, kPortal);
    p1_.answers = {1};
    p1_.want = ally;
    play(portal, LocationId{BattlefieldLocation{1}});
    EXPECT_TRUE(s().getObject(portal).isAtBattlefield());
    EXPECT_FALSE(s().getObject(ally).is_exhausted);
    EXPECT_TRUE(s().getObject(elsewhere).is_exhausted);
    EXPECT_EQ(bodyPower(P1), 0) << "paid [O]";
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), elsewhere)) << "only units here";
}

TEST_F(RadianceB06Test, Portal_DeclineKeepsThePower) {
    makeEngine();
    auto ally = boardObject(P1, kOtterpus, 1);
    s().getObject(ally).is_exhausted = true;
    s().player(P1).rune_pool.energy = 2;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 1;
    auto portal = cardInHand(P1, kPortal);
    p1_.answers = {0};
    p1_.want = ally;
    play(portal, LocationId{BattlefieldLocation{1}});
    EXPECT_TRUE(s().getObject(ally).is_exhausted);
    EXPECT_EQ(bodyPower(P1), 1);
}

TEST_F(RadianceB06Test, Portal_FriendlyUnitsHereHaveGanking) {
    makeEngine();
    boardObject(P1, kPortal, 0);
    auto here = boardObject(P1, kOtterpus, 0);
    auto there = boardObject(P1, kOtterpus, 1);
    auto base = boardObject(P1, kOtterpus);
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(here).hasKeyword(Keyword::Ganking));
    EXPECT_FALSE(s().getObject(there).hasKeyword(Keyword::Ganking));
    EXPECT_FALSE(s().getObject(base).hasKeyword(Keyword::Ganking));
}

TEST_F(RadianceB06Test, Portal_DeploysOnlyToBattlefields) {
    makeEngine();
    s().player(P1).rune_pool.energy = 2;
    auto portal = cardInHand(P1, kPortal);
    bool base = false, bf = false;
    for (const auto& a : engine_->generateLegalActions()) {
        if (a.type != IntentType::PlayCard || a.card != portal) continue;
        if (a.play_location == std::optional<LocationId>{LocationId{BaseLocation{P1}}}) base = true;
        if (a.play_location == std::optional<LocationId>{LocationId{BattlefieldLocation{0}}}) bf = true;
    }
    EXPECT_FALSE(base);
    EXPECT_TRUE(bf);
}

// ─── RAD-088 Staunch Survivor ───────────────────────────────────────────────

TEST_F(RadianceB06Test, Survivor_HasAssaultTwoAndShieldTwo) {
    makeEngine();
    const auto& d = card_db.get(kSurvivor);
    EXPECT_TRUE(d.keywords.has(Keyword::Assault));
    EXPECT_TRUE(d.keywords.has(Keyword::Shield));
    EXPECT_EQ(d.assault_value, 2);
    EXPECT_EQ(d.shield_value, 2);
    EXPECT_EQ(d.might, 2);
}

// ─── RAD-089 Titanic Shrine ─────────────────────────────────────────────────

TEST_F(RadianceB06Test, Shrine_ExhaustsToDrawWhenTheMightiestUnitIsPlayed) {
    makeEngine();
    auto shrine = boardObject(P1, kShrine);
    auto small = boardObject(P2, kOtterpus);
    s().getObject(small).base_might = 1;
    s().getObject(small).recomputeMight();
    auto top = deckCard(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 2;
    auto unit = cardInHand(P1, kOtterpus);
    p1_.answers = {1};
    play(unit, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().getObject(shrine).is_exhausted);
    EXPECT_TRUE(inHand(P1, top));
}

TEST_F(RadianceB06Test, Shrine_NoTriggerOnATie) {
    makeEngine();
    auto shrine = boardObject(P1, kShrine);
    boardObject(P2, kOtterpus);
    auto top = deckCard(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 2;
    auto unit = cardInHand(P1, kOtterpus);
    p1_.answers = {1};
    play(unit, LocationId{BaseLocation{P1}});
    EXPECT_FALSE(s().getObject(shrine).is_exhausted);
    EXPECT_FALSE(inHand(P1, top));
    EXPECT_TRUE(p1_.int_prompts.empty());
}

TEST_F(RadianceB06Test, Shrine_DeclineKeepsItReady) {
    makeEngine();
    auto shrine = boardObject(P1, kShrine);
    auto top = deckCard(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 2;
    auto unit = cardInHand(P1, kOtterpus);
    p1_.answers = {0};
    play(unit, LocationId{BaseLocation{P1}});
    ASSERT_EQ(p1_.int_prompts.size(), 1u) << "the only unit on the board is the mightiest";
    EXPECT_FALSE(s().getObject(shrine).is_exhausted);
    EXPECT_FALSE(inHand(P1, top));
}

// ─── RAD-090 Evelynn, Consuming ─────────────────────────────────────────────

TEST_F(RadianceB06Test, Evelynn_FaceDownPlayReadiesAndGrowsHer) {
    makeEngine();
    auto eve = boardObject(P1, kEvelynn, 0);
    s().getObject(eve).is_exhausted = true;
    auto card = cardInHand(P1, kOtterpus);
    events.emit(PlayedFromFacedownEvent{card, P1});
    engine_->testHook_runChain();
    EXPECT_FALSE(s().getObject(eve).is_exhausted);
    EXPECT_EQ(s().getObject(eve).current_might, 6) << "5 + 1 this turn";
}

TEST_F(RadianceB06Test, Evelynn_OpponentsFaceDownPlayDoesNothing) {
    makeEngine();
    auto eve = boardObject(P1, kEvelynn, 0);
    s().getObject(eve).is_exhausted = true;
    auto card = cardInHand(P2, kOtterpus);
    events.emit(PlayedFromFacedownEvent{card, P2});
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(eve).is_exhausted);
    EXPECT_EQ(s().getObject(eve).current_might, 5);
    EXPECT_TRUE(card_db.get(kEvelynn).keywords.has(Keyword::Ambush));
}

// ─── RAD-091 Jarvan IV, Martial Paragon ─────────────────────────────────────

TEST_F(RadianceB06Test, Jarvan_ConquerBesideADeployGearChannelsAnExhaustedRune) {
    makeEngine();
    boardObject(P1, kJarvan, 0);
    boardObject(P1, kConcertPoster, 0);
    auto r = runeInDeck(P1, Domain::Body);
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    ASSERT_TRUE(s().getObject(r).location.has_value());
    EXPECT_TRUE(s().getObject(r).is_exhausted);
}

TEST_F(RadianceB06Test, Jarvan_ConquerWithoutADeployGearChannelsNothing) {
    makeEngine();
    boardObject(P1, kJarvan, 0);
    boardObject(P1, kConcertPoster, 1);   // not here
    auto r = runeInDeck(P1, Domain::Body);
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    EXPECT_FALSE(s().getObject(r).location.has_value());
}

TEST_F(RadianceB06Test, Jarvan_DeployGearCanBePlayedInAShowdown) {
    makeEngine();
    auto jarvan = boardObject(P1, kJarvan, 0);
    auto poster = cardInHand(P1, kConcertPoster);
    s().player(P1).rune_pool.energy = 1;
    auto jarvanAbility = [&]() -> std::optional<Intent> {
        for (const auto& a : engine_->generateLegalActions())
            if ((a.type == IntentType::ActivateAbility ||
                 a.type == IntentType::ActivateActionAbility) &&
                a.ability_source == jarvan && a.targets == std::vector<GameObjectId>{poster})
                return a;
        return std::nullopt;
    };
    EXPECT_FALSE(jarvanAbility().has_value()) << "not offered outside a showdown";

    s().turn.ns_state = NeutralShowdownState::Showdown;
    s().turn.focus_holder = P1;
    auto act = jarvanAbility();
    ASSERT_TRUE(act.has_value());
    p1_.answers = {1};   // battlefield 1
    engine_->testHook_executeIntent(*act);
    engine_->testHook_runChain();
    EXPECT_FALSE(inHand(P1, poster));
    EXPECT_EQ(s().getObject(poster).battlefieldId(), std::optional<BattlefieldId>{1});
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0) << "the gear's cost was paid";
}

// ─── RAD-093 Disposal Expert ────────────────────────────────────────────────

TEST_F(RadianceB06Test, Expert_EntersReadyWithNoOtherUnits) {
    makeEngine();
    s().player(P1).rune_pool.energy = 4;
    auto expert = cardInHand(P1, kExpert);
    play(expert, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(expert).isAtBase());
    EXPECT_FALSE(s().getObject(expert).is_exhausted);
}

TEST_F(RadianceB06Test, Expert_EntersExhaustedBesideAnotherUnit) {
    makeEngine();
    boardObject(P1, kOtterpus, 1);
    boardObject(P2, kOtterpus);   // enemy units don't matter
    s().player(P1).rune_pool.energy = 4;
    auto expert = cardInHand(P1, kExpert);
    play(expert, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(expert).isAtBase());
    EXPECT_TRUE(s().getObject(expert).is_exhausted);
}

TEST_F(RadianceB06Test, Expert_DisarmTwo) {
    makeEngine();
    auto expert = boardObject(P1, kExpert, 0);
    auto here = boardObject(P2, kScorcher, 0);
    auto there = boardObject(P2, kScorcher, 1);
    p1_.want = here;
    engine_->testHook_executor().queueTriggeredAbility(expert, P1, TriggerType::WhenIAttack);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(here).current_might, 3) << "5 - 2 this turn";
    EXPECT_EQ(s().getObject(there).current_might, 5);
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), there)) << "only enemy units here";
}

// ─── RAD-095 Momentary Companion ────────────────────────────────────────────

TEST_F(RadianceB06Test, Companion_TemporaryWhenPlayedOnAnOpponentsTurn) {
    makeEngine();
    s().turn.turn_player = P2;
    auto comp = boardObject(P1, kCompanion, 0);
    engine_->testHook_executor().queueTriggeredAbility(comp, P1, TriggerType::WhenYouPlayMe);
    engine_->testHook_runChain();
    EXPECT_TRUE(s().getObject(comp).hasKeyword(Keyword::Temporary));
}

TEST_F(RadianceB06Test, Companion_NoTemporaryOnYourOwnTurn) {
    makeEngine();
    s().player(P1).rune_pool.energy = 4;
    auto comp = cardInHand(P1, kCompanion);
    play(comp, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(comp).isAtBase());
    EXPECT_FALSE(s().getObject(comp).hasKeyword(Keyword::Temporary));
    EXPECT_TRUE(card_db.get(kCompanion).keywords.has(Keyword::Hidden));
}

// ─── RAD-096 Satchel Charge ─────────────────────────────────────────────────

TEST_F(RadianceB06Test, Satchel_MovesAFriendlyUnitThenPlaysABomb) {
    makeEngine();
    auto mine = boardObject(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Chaos)] = 1;
    auto spell = cardInHand(P1, kSatchel);
    p1_.answers = {2, 0};   // move to BF1 (moveDests code 2); Bomb to BF0
    p1_.want = mine;
    play(spell);
    ASSERT_GE(p1_.int_prompts.size(), 2u);
    EXPECT_EQ(p1_.int_prompts[0], (std::vector<int>{1, 2})) << "the unit is already in base";
    EXPECT_EQ(p1_.int_prompts[1], (std::vector<int>{0, 1}));
    EXPECT_EQ(s().getObject(mine).battlefieldId(), std::optional<BattlefieldId>{1});
    EXPECT_EQ(bombsAt(P1, 0).size(), 1u);
    EXPECT_TRUE(bombsAt(P1, 1).empty());
}

TEST_F(RadianceB06Test, Satchel_WithNoFriendlyUnitStillPlaysTheBomb) {
    makeEngine();
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Chaos)] = 1;
    auto spell = cardInHand(P1, kSatchel);
    p1_.answers = {1};   // Bomb to BF1
    play(spell);
    ASSERT_EQ(p1_.int_prompts.size(), 1u) << "only the Bomb's battlefield is asked";
    EXPECT_EQ(bombsAt(P1, 1).size(), 1u);
    EXPECT_TRUE(card_db.get(kSatchel).keywords.has(Keyword::Action));
}

}  // namespace
}  // namespace riftbound::test
