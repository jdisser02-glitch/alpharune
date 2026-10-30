/// @file test_human_replay_fidelity_3.cpp
/// Engine and card gaps behind the first desync of real RiftLite games
/// (lab/tools/human_review.py), third pass:
///   - a Chosen Champion played from the Champion Zone was played "from
///     hand": Kennen, Heart of the Tempest ("when you play a card from
///     anywhere other than your hand, empower me") never empowered. Real
///     game: bertilQ played Kennen, Storm of Shuriken from the Champion Zone
///     and used the legend at once to give Trifarian Captain [Assault 2].
///   - a second buff stacked (CR 427.1.b, 702.3: one buff per unit, unless the
///     unit "can have any number of buffs", Lee Sin, Ascetic). Real game:
///     Riven, Broken Blade's "[Buff] me" while buffed made her 6 [M]; Sziszu's
///     4 [M] Riven died to a 5 [M] Sand Soldier.
///   - Equipment's [Assault N] / [Shield N] never added Might in combat, and
///     its plain keyword was a second instance; an [Assault]/[Shield] granted
///     by another card (Pride of Nazumah) carried its Might only from the next
///     cleanup after the combat designations, so not in the damage step; and
///     Pride of Nazumah did not count an Equipment's [Shield]. Real game:
///     Damus's Armored Tusker (Shield 3 + Nazuman Ntofo's Shield 2, Pride of
///     Nazumah) attacked for 9 and traded with a 9 [M] Astral Heron.
///   - a card hidden on an earlier turn could be played from face down only
///     in closed states, not in the open main phase or a showdown (CR 811.1.b,
///     811.6: it has [Reaction]). Real game: SergeantTV played Gutter Guardian
///     from face down at Hunters' Circle on his turn.
///   - group moves (CR 144.3) from different origins were never offered
///     (144.3.b: origins need not be the same), and of more than four units
///     only the whole group (the guided replay raises the limit).
///   - Repeat's yes/no carries the additional-cost wording a replay reads.

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

constexpr CardDefId kOtterpus         = 840;    // [2] 2-Might unit
constexpr CardDefId kNazumanNtofo     = 1035;   // Equipment: [Shield 2]
constexpr CardDefId kSerratedDirk     = 332;    // Equipment: [Assault 2]
constexpr CardDefId kStaunchSurvivor  = 1048;   // 2 Might, [Assault 2], [Shield 2]
constexpr CardDefId kPrideOfNazumah   = 1091;   // K'Sante legend
constexpr CardDefId kHeartOfTempest   = 942;    // Kennen legend
constexpr CardDefId kKennenStorm      = 900;    // Kennen champion unit, [3][A]
constexpr CardDefId kRivenBrokenBlade = 963;
constexpr CardDefId kLeeSinAscetic    = 78;     // "I can have any number of buffs."
constexpr CardDefId kTideturner       = 199;    // [Hidden] unit
constexpr CardDefId kBellowsBreath    = 402;    // [Repeat] [1][B] spell

/// Passes priority and focus; picks `want` among offered objects; records
/// every yes/no label it is shown and answers "no".
class Fidelity3Agent : public AgentInterface {
public:
    GameObjectId want = kInvalidId;
    std::vector<std::string> labels;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        for (const auto& i : legal) if (!i.choice_label.empty()) labels.push_back(i.choice_label);
        for (const auto& i : legal) {
            for (auto t : i.chosen_objects) if (t == want) return i;
            for (auto t : i.targets) if (t == want) return i;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassFocus) return i;
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        for (const auto& i : legal) if (i.type == IntentType::MakeChoice && i.chosen_value == 0) return i;
        return legal.front();
    }
};

class ReplayFidelity3 : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    Fidelity3Agent p1_, p2_;
    GameState& s() { return engine_->mutableState(); }

    void makeEngine() {
        engine_ = std::make_unique<GameEngine>(card_db, events, card_registry);
        auto& st = s();
        st.mode = ModeOfPlay{};
        st.players[0].id = P1;
        st.players[1].id = P2;
        st.players[0].turns_taken = st.players[1].turns_taken = 5;
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
        u.assault_value = def.assault_value;
        u.shield_value = def.shield_value;
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
        stamp(s().getObject(id), p, def_id);
        s().getObject(id).zone = ZoneType::Hand;
        s().player(p).hand.push_back(id);
        return id;
    }
    GameObjectId rune(PlayerId p, Domain d) {
        auto id = s().createObject();
        auto& r = s().getObject(id);
        r.owner = r.controller = p;
        r.card_type = CardType::Rune;
        r.name = std::string(toString(d)) + " Rune";
        r.domains = {d};
        r.zone = ZoneType::Base;
        r.location = BaseLocation{p};
        return id;
    }
    void attach(GameObjectId gear, GameObjectId unit) {
        auto& g = s().getObject(gear);
        auto& u = s().getObject(unit);
        g.attached_to = unit;
        g.location = u.location;
        u.attachments.push_back(gear);
        u.attachment_might_bonus += g.might_bonus;
    }
    void activate(GameObjectId source) {
        Intent i;
        i.type = IntentType::ActivateAbility;
        i.player = P1;
        i.ability_source = source;
        i.ability_index = 0;
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
    /// A combat at battlefield 0 with `attacker` attacking (designations are
    /// given by the next cleanup, CR 323.2).
    void combatAt0(PlayerId attacker) {
        auto& bf = s().battlefields[0];
        bf.combat_in_progress = true;
        bf.contested_by = attacker;
        bf.attacker = attacker;
        bf.defender = attacker == P1 ? P2 : P1;
    }
    int might(GameObjectId id) { return s().getObject(id).current_might; }
    bool onBoard(GameObjectId id) { return s().objectExists(id) && s().getObject(id).location.has_value(); }
    int groupMovesTo(BattlefieldId bf, size_t size) {
        int n = 0;
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::StandardMove && a.units_to_move.size() == size &&
                a.move_destination == std::optional<LocationId>{BattlefieldLocation{bf}})
                ++n;
        return n;
    }
};

// ── The Champion Zone is not the hand ──

TEST_F(ReplayFidelity3, ChampionPlayedFromTheChampionZoneEmpowersHeartOfTheTempest) {
    makeEngine();
    auto legend = setLegend(P1, kHeartOfTempest); setLegend(P2);
    auto& ps = s().player(P1);
    auto champ = s().createObject();
    stamp(s().getObject(champ), P1, kKennenStorm);
    s().getObject(champ).zone = ZoneType::ChampionZone;
    ps.champion_zone = champ;
    ps.rune_pool.energy = 3;
    ps.rune_pool.universal_power = 1;

    Intent play;
    for (const auto& a : engine_->generateLegalActions())
        if (a.type == IntentType::PlayCard && a.card == champ &&
            a.play_location == std::optional<LocationId>{BaseLocation{P1}})
            play = a;
    ASSERT_EQ(play.card, champ) << "the champion can be played from the Champion Zone";
    engine_->testHook_executeIntent(play);
    engine_->testHook_runChain();

    ASSERT_TRUE(onBoard(champ));
    EXPECT_EQ(s().getObject(legend).empowered, 1) << "played from somewhere other than the hand";
}

// ── One buff per unit (CR 702.3) ──

TEST_F(ReplayFidelity3, ABuffedUnitGetsNoSecondBuff) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto riven = boardObject(P1, kRivenBrokenBlade);
    auto& exec = engine_->testHook_executor();
    exec.buffUnit(riven);
    EXPECT_EQ(s().getObject(riven).buff_count, 1);
    exec.buffUnit(riven);
    EXPECT_EQ(s().getObject(riven).buff_count, 1) << "a unit has one buff at most";
    EXPECT_EQ(might(riven), card_db.get(kRivenBrokenBlade).might + 1);

    auto lee = boardObject(P1, kLeeSinAscetic);
    exec.buffUnit(lee);
    exec.buffUnit(lee);
    EXPECT_EQ(s().getObject(lee).buff_count, 2) << "Lee Sin, Ascetic can have any number of buffs";
}

// ── Equipment [Shield N] / [Assault N] in combat ──

TEST_F(ReplayFidelity3, EquipmentShieldAddsMightWhileDefendingAsOneInstance) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto unit = boardObject(P1, kOtterpus, 0);
    auto gear = boardObject(P1, kNazumanNtofo);
    attach(gear, unit);
    engine_->testHook_cleanup();
    EXPECT_EQ(might(unit), 2) << "not a defender";
    int shields = 0;
    for (const auto& ae : s().getObject(unit).aura_effects)
        if (ae.source == gear && ae.keyword == Keyword::Shield) {
            ++shields;
            EXPECT_EQ(ae.keyword_value, 2);
        }
    EXPECT_EQ(shields, 1) << "[Shield 2] is one instance, not [Shield 2] and [Shield]";

    combatAt0(P2);
    engine_->testHook_cleanup();
    EXPECT_EQ(might(unit), 4) << "+2 [M] while a defender";
}

TEST_F(ReplayFidelity3, EquipmentAssaultAddsMightWhileAttacking) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto unit = boardObject(P1, kOtterpus, 0);
    attach(boardObject(P1, kSerratedDirk), unit);
    combatAt0(P1);
    engine_->testHook_cleanup();
    EXPECT_EQ(might(unit), 4) << "+2 [M] while an attacker";
}

TEST_F(ReplayFidelity3, TheDamageStepSeesMightThatTheDesignationsGrant) {
    // A 2 [M] unit with a Serrated Dirk ([Assault 2]) attacks a 3 [M] unit:
    // 4 damage kills it and the attacker survives the 3 it takes back.
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto attacker = boardObject(P1, kOtterpus, 0);
    attach(boardObject(P1, kSerratedDirk), attacker);
    auto defender = boardObject(P2, kOtterpus, 0);
    s().getObject(defender).base_might = 3;
    s().getObject(defender).recomputeMight();
    engine_->testHook_cleanup();
    auto& bf = s().battlefields[0];
    bf.is_contested = true;
    bf.contested_by = P1;
    bf.combat_staged = true;
    engine_->testHook_advanceMainPhase();
    EXPECT_FALSE(onBoard(defender)) << "4 damage from the attacker with [Assault 2]";
    EXPECT_TRUE(onBoard(attacker)) << "4 [M] while attacking takes 3";
}

TEST_F(ReplayFidelity3, PrideOfNazumahCountsTheEquipmentsShield) {
    // Staunch Survivor ([Assault 2], [Shield 2]) with a Nazuman Ntofo ([Shield
    // 2]): Pride of Nazumah gives it [Assault] equal to its [Shield], 4.
    makeEngine();
    auto legend = setLegend(P1, kPrideOfNazumah); setLegend(P2);
    auto unit = boardObject(P1, kStaunchSurvivor, 0);
    attach(boardObject(P1, kNazumanNtofo), unit);
    s().player(P1).rune_pool.energy = 1;
    p1_.want = unit;
    activate(legend);
    combatAt0(P1);
    engine_->testHook_cleanup();
    EXPECT_EQ(might(unit), 2 + 2 + 4) << "2 [M] + its [Assault 2] + Pride's [Assault 4]";
}

// ── Plays from face down: [Reaction] timing, the open main phase included ──

TEST_F(ReplayFidelity3, AHiddenCardCanBePlayedInTheOpenMainPhaseOfALaterTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    s().battlefields[0].controller = P1;
    boardObject(P1, kOtterpus, 0);
    rune(P1, Domain::Chaos);
    auto card = cardInHand(P1, kTideturner);
    s().turn.turn_number = 4;
    Intent hide;
    for (const auto& a : engine_->generateLegalActions())
        if (a.type == IntentType::HideCard && a.card == card && a.chosen_battlefield == 0) hide = a;
    ASSERT_EQ(hide.type, IntentType::HideCard);
    engine_->testHook_executeIntent(hide);
    ASSERT_TRUE(s().getObject(card).is_hidden);

    auto facedownPlays = [&]() {
        int n = 0;
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::PlayReaction && a.card == card) ++n;
        return n;
    };
    EXPECT_EQ(facedownPlays(), 0) << "not on the turn it was hidden";
    s().turn.turn_number = 6;   // its controller's next turn
    EXPECT_EQ(facedownPlays(), 1) << "[Reaction]: playable in the open main phase (CR 811.6)";
}

// ── Group moves (CR 144.3) ──

TEST_F(ReplayFidelity3, UnitsFromDifferentOriginsMoveTogether) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto at_base = boardObject(P1, kOtterpus);
    auto ganker = boardObject(P1, kOtterpus, 1);
    s().getObject(ganker).keywords.set(Keyword::Ganking);
    bool both = false;
    for (const auto& a : engine_->generateLegalActions())
        if (a.type == IntentType::StandardMove && a.units_to_move.size() == 2 &&
            a.move_destination == std::optional<LocationId>{BattlefieldLocation{0}})
            both = std::count(a.units_to_move.begin(), a.units_to_move.end(), at_base) &&
                   std::count(a.units_to_move.begin(), a.units_to_move.end(), ganker);
    EXPECT_TRUE(both) << "a unit from the base and one from another battlefield (CR 144.3.b)";
}

TEST_F(ReplayFidelity3, AnySubsetOfALargeGroupWhenTheLimitIsRaised) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    for (int i = 0; i < 5; ++i) boardObject(P1, kOtterpus);
    EXPECT_EQ(groupMovesTo(0, 2), 0) << "the bots' default: five units move whole";
    EXPECT_EQ(groupMovesTo(0, 5), 1);
    s().mode.move_group_subset_limit = 8;
    EXPECT_EQ(groupMovesTo(0, 2), 10) << "every pair of the five";
    EXPECT_EQ(groupMovesTo(0, 5), 1);
}

// ── Repeat's yes/no names the card and the cost ──

TEST_F(ReplayFidelity3, RepeatChoiceIsLabelledAsAnAdditionalCost) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto target = boardObject(P2, kOtterpus, 0);
    auto spell = cardInHand(P1, kBellowsBreath);
    auto& pool = s().player(P1).rune_pool;
    pool.energy = 2;
    pool.power[static_cast<int>(Domain::Mind)] = 2;
    Intent play;
    play.type = IntentType::PlayCard;
    play.player = P1;
    play.card = spell;
    play.targets = {target};
    engine_->testHook_executeIntent(play);
    bool yes = false, no = false;
    for (const auto& l : p1_.labels) {
        if (l == "Yes \xE2\x80\x94 additional cost for Bellows Breath: pay 1 energy + 1 power? (Repeat 1)") yes = true;
        if (l == "No \xE2\x80\x94 additional cost for Bellows Breath: pay 1 energy + 1 power? (Repeat 1)") no = true;
    }
    EXPECT_TRUE(yes && no) << "labels seen: " << p1_.labels.size();
}

}  // namespace
}  // namespace riftbound::test
