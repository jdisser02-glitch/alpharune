/// @file test_kennen_audit.cpp
/// Fidelity audit of the Kennen, Heart of the Tempest decks: the synergies the
/// bot could only exploit with competent play.
///   - a card an effect plays from the trash / deck / banishment is "played
///     from anywhere other than your hand" (Heart of the Tempest empowers);
///   - "look at the top N" (Lightning Rush, Trifarian Captain) lets Nocturne,
///     Horrifying banish himself and be played (CR 359 example on Nocturne);
///   - Tideturner chooses the unit it swaps with;
///   - Ride the Wind's controller chooses the Move's destination (CR 355.4);
///   - Last Rites pays the unit's cost; Fizz picks which spell to replay.

#include "tests/cards/card_test_fixture.h"

#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/events.h"
#include "core/game_state.h"
#include "engine/chain_manager.h"
#include "engine/effect_executor.h"
#include "engine/trigger_manager.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace riftbound::test {
namespace {

constexpr CardDefId kHeartOfTheTempest = 942;
constexpr CardDefId kNocturne          = 194;
constexpr CardDefId kLightningRush     = 943;
constexpr CardDefId kTrifarianCaptain  = 974;
constexpr CardDefId kTideturner        = 199;
constexpr CardDefId kRideTheWind       = 173;
constexpr CardDefId kLastRites         = 471;
constexpr CardDefId kFizz              = 461;
constexpr CardDefId kPunchingPoro      = 794;   // [2] unit, no Power cost
constexpr CardDefId kOnslaught         = 868;   // [4] spell (not Captain-eligible)
constexpr CardDefId kGust              = 169;   // [1] spell
constexpr CardDefId kFlash             = 309;   // [2] spell

class KennenAuditTest : public CardTestFixture {
protected:
    GameObjectId addToTrash(PlayerId owner, CardDefId def_id) {
        auto id = state.createObject();
        auto& obj = state.getObject(id);
        obj.owner = owner;
        obj.controller = owner;
        obj.card_def_id = def_id;
        const auto& def = card_db.get(def_id);
        obj.name = def.name;
        obj.card_type = def.card_type;
        obj.super_type = def.super_type;
        obj.keywords = def.keywords;
        obj.domains = def.domains;
        obj.base_might = def.might;
        obj.current_might = def.might;
        obj.zone = ZoneType::Trash;
        state.player(owner).trash.push_back(id);
        return id;
    }
    GameObjectId addLegend(PlayerId owner, CardDefId def_id) {
        auto id = state.createObject();
        auto& obj = state.getObject(id);
        obj.owner = owner;
        obj.controller = owner;
        obj.card_def_id = def_id;
        const auto& def = card_db.get(def_id);
        obj.name = def.name;
        obj.card_type = def.card_type;
        obj.zone = ZoneType::LegendZone;
        obj.is_exhausted = false;
        state.player(owner).legend_zone = id;
        return id;
    }
    GameObjectId addGear(PlayerId owner, CardDefId def_id) {
        auto id = state.createObject();
        auto& obj = state.getObject(id);
        obj.owner = owner;
        obj.controller = owner;
        obj.card_def_id = def_id;
        obj.card_type = CardType::Gear;
        obj.name = card_db.get(def_id).name;
        obj.zone = ZoneType::Base;
        obj.location = BaseLocation{owner};
        return id;
    }
    bool chainHasAbilityFrom(GameObjectId source) const {
        for (const auto& it : state.chain.items)
            if (it.is_ability && it.source == source) return true;
        return false;
    }
    static Intent pickYes(const std::vector<Intent>& legal) {
        for (const auto& i : legal)
            if (i.chosen_value.has_value() && *i.chosen_value == 1) return i;
        return legal.front();
    }
    static Intent pickObject(const std::vector<Intent>& legal, GameObjectId id) {
        for (const auto& i : legal)
            if (!i.chosen_objects.empty() && i.chosen_objects[0] == id) return i;
        return legal.front();
    }
    static bool isMayPrompt(const std::vector<Intent>& legal) {
        return legal.size() == 2 && legal[0].chosen_objects.empty() &&
               legal[0].chosen_value.has_value() && legal[1].chosen_value.has_value() &&
               *legal[1].chosen_value == 1;
    }
};

// ── "When you play a card from anywhere other than your hand" ─────────────

TEST_F(KennenAuditTest, PlayFromTrashByEffect_EmpowersHeartOfTheTempest) {
    auto legend = addLegend(P1, kHeartOfTheTempest);
    ChainManager cm(state, events, card_db);
    TriggerManager tm(state, events, card_db, cm, card_registry);
    tm.subscribe();
    EffectExecutor exec(state, events, card_db, &card_registry);

    auto poro = addToTrash(P1, kPunchingPoro);
    auto& trash = state.player(P1).trash;
    trash.erase(std::remove(trash.begin(), trash.end(), poro), trash.end());
    exec.playIgnoringCost(P1, poro, std::nullopt);   // Tail-Cloaked Matriarch, Last Rites, …

    EXPECT_TRUE(state.getObject(poro).isAtBase());
    EXPECT_EQ(state.player(P1).cards_played_not_from_hand_this_turn, 1);
    ASSERT_TRUE(chainHasAbilityFrom(legend)) << "the legend's trigger goes on the chain";

    // Resolving it empowers the legend.
    Card* heart = card_registry.get(kHeartOfTheTempest);
    ASSERT_NE(heart, nullptr);
    CardContext ctx{state, events, exec, P1, legend};
    heart->onTrigger(ctx, {});
    EXPECT_EQ(state.getObject(legend).empowered, 1);
}

TEST_F(KennenAuditTest, PlayFromHandByEffect_DoesNotEmpowerHeartOfTheTempest) {
    auto legend = addLegend(P1, kHeartOfTheTempest);
    ChainManager cm(state, events, card_db);
    TriggerManager tm(state, events, card_db, cm, card_registry);
    tm.subscribe();
    EffectExecutor exec(state, events, card_db, &card_registry);

    auto poro = addToHand(P1, kPunchingPoro);
    auto& hand = state.player(P1).hand;
    hand.erase(std::remove(hand.begin(), hand.end(), poro), hand.end());
    exec.playIgnoringCost(P1, poro, std::nullopt);

    EXPECT_EQ(state.player(P1).cards_played_not_from_hand_this_turn, 0);
    EXPECT_FALSE(chainHasAbilityFrom(legend));
}

// ── Nocturne, Horrifying off "look at the top N" ──────────────────────────

TEST_F(KennenAuditTest, LightningRush_LookLetsNocturneBePlayedFromTheTrash) {
    auto legend = addLegend(P1, kHeartOfTheTempest);
    ChainManager cm(state, events, card_db);
    TriggerManager tm(state, events, card_db, cm, card_registry);
    tm.subscribe();
    EffectExecutor exec(state, events, card_db, &card_registry);

    auto bottom = addToDeck(P1, kOnslaught);
    auto c1 = addToDeck(P1, kOnslaught);
    auto noct = addToDeck(P1, kNocturne);
    auto c3 = addToDeck(P1, kOnslaught);   // top
    auto rush = addToHand(P1, kLightningRush);

    // "Draw none": the three looked-at cards go to the trash.
    driveResumable(kLightningRush, P1, rush,
        [](const std::vector<Intent>& legal) {
            for (const auto& i : legal) if (i.chosen_objects.empty()) return i;
            return legal.front();
        }, exec);
    EXPECT_TRUE(inTrash(P1, noct));
    EXPECT_TRUE(inTrash(P1, c1));
    EXPECT_TRUE(inTrash(P1, c3));
    EXPECT_TRUE(inDeck(P1, bottom));
    ASSERT_TRUE(chainHasAbilityFrom(noct)) << "the look wakes Nocturne";

    // Nocturne's ability: banish him (from where the look left him) and play him.
    driveResumableTrigger(kNocturne, P1, noct, pickYes, exec);
    EXPECT_FALSE(inTrash(P1, noct));
    EXPECT_TRUE(state.getObject(noct).isAtBase());
    EXPECT_EQ(state.getObject(noct).zone, ZoneType::Base);
    const auto& bz = state.player(P1).banishment;
    EXPECT_EQ(std::find(bz.begin(), bz.end(), noct), bz.end());
    EXPECT_TRUE(chainHasAbilityFrom(legend)) << "played from the deck: Heart of the Tempest fires";
}

TEST_F(KennenAuditTest, LightningRush_NocturneDrawnToHandIsNotPlayed) {
    ChainManager cm(state, events, card_db);
    TriggerManager tm(state, events, card_db, cm, card_registry);
    tm.subscribe();
    EffectExecutor exec(state, events, card_db, &card_registry);

    addToDeck(P1, kOnslaught);
    auto noct = addToDeck(P1, kNocturne);
    addToDeck(P1, kOnslaught);
    auto rush = addToHand(P1, kLightningRush);
    driveResumable(kLightningRush, P1, rush,
        [&](const std::vector<Intent>& legal) { return pickObject(legal, noct); }, exec);
    ASSERT_TRUE(inHand(P1, noct));
    ASSERT_TRUE(chainHasAbilityFrom(noct));

    driveResumableTrigger(kNocturne, P1, noct, pickYes, exec);
    EXPECT_TRUE(inHand(P1, noct)) << "no longer on top of the deck nor among the rest";
}

TEST_F(KennenAuditTest, TrifarianCaptain_LookLetsNocturneBePlayedFromTheDeckBottom) {
    ChainManager cm(state, events, card_db);
    TriggerManager tm(state, events, card_db, cm, card_registry);
    tm.subscribe();
    EffectExecutor exec(state, events, card_db, &card_registry);

    auto filler = addToDeck(P1, kOnslaught);
    addToDeck(P1, kOnslaught);
    auto noct = addToDeck(P1, kNocturne);
    addToDeck(P1, kOnslaught);
    addToDeck(P1, kOnslaught);   // top; nothing in the top 4 is Captain-eligible
    auto captain = addUnit(P1, kTrifarianCaptain, 0, 0);

    fireTriggerAs(kTrifarianCaptain, P1, captain, TriggerType::WhenYouPlayMe, exec);
    ASSERT_EQ(deckSize(P1), 5);
    EXPECT_EQ(state.player(P1).main_deck.back(), filler) << "the top 4 were recycled";
    EXPECT_TRUE(inDeck(P1, noct));
    ASSERT_TRUE(chainHasAbilityFrom(noct)) << "the look wakes Nocturne";

    driveResumableTrigger(kNocturne, P1, noct, pickYes, exec);
    EXPECT_FALSE(inDeck(P1, noct));
    EXPECT_EQ(deckSize(P1), 4);
    EXPECT_TRUE(state.getObject(noct).isAtBase());
}

TEST_F(KennenAuditTest, Nocturne_DecliningLeavesHimWhereHeIs) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto noct = addToDeck(P1, kNocturne);
    driveResumableTrigger(kNocturne, P1, noct,
        [](const std::vector<Intent>& legal) { return legal.front(); }, exec);   // "No"
    EXPECT_TRUE(inDeck(P1, noct));
    EXPECT_EQ(state.getObject(noct).zone, ZoneType::MainDeck);
}

// ── Tideturner ────────────────────────────────────────────────────────────

TEST_F(KennenAuditTest, Tideturner_SwapsLocationsWithTheChosenUnit) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto tide = addUnit(P1, kTideturner, 0, 0);
    auto at_base = addUnit(P1, kPunchingPoro, 0, -1);
    auto at_bf1 = addUnit(P1, kPunchingPoro, 0, 1);
    auto same_bf = addUnit(P1, kPunchingPoro, 0, 0);
    (void)same_bf;

    bool offered_same_bf = false;
    driveResumableTrigger(kTideturner, P1, tide,
        [&](const std::vector<Intent>& legal) {
            if (isMayPrompt(legal)) return pickYes(legal);
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects[0] == same_bf)
                    offered_same_bf = true;
            return pickObject(legal, at_bf1);
        }, exec);

    EXPECT_FALSE(offered_same_bf) << "only units at ANOTHER location";
    EXPECT_EQ(state.getObject(tide).battlefieldId(), std::optional<BattlefieldId>(1));
    EXPECT_EQ(state.getObject(at_bf1).battlefieldId(), std::optional<BattlefieldId>(0));
    EXPECT_TRUE(state.getObject(at_base).isAtBase());
}

TEST_F(KennenAuditTest, Tideturner_DeclinedSwapMovesNothing) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto tide = addUnit(P1, kTideturner, 0, 0);
    auto at_base = addUnit(P1, kPunchingPoro, 0, -1);
    driveResumableTrigger(kTideturner, P1, tide,
        [](const std::vector<Intent>& legal) { return legal.front(); }, exec);
    EXPECT_EQ(state.getObject(tide).battlefieldId(), std::optional<BattlefieldId>(0));
    EXPECT_TRUE(state.getObject(at_base).isAtBase());
}

// ── Ride the Wind ─────────────────────────────────────────────────────────

TEST_F(KennenAuditTest, RideTheWind_MovesToTheChosenBattlefieldAndReadies) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto unit = addUnit(P1, kPunchingPoro, 0, -1);
    state.getObject(unit).is_exhausted = true;
    auto spell = addToHand(P1, kRideTheWind);

    driveResumable(kRideTheWind, P1, spell,
        [&](const std::vector<Intent>& legal) {
            // Destination prompt: int-coded (0 = base, 1 + i = battlefield i).
            if (!legal.front().chosen_objects.empty()) return pickObject(legal, unit);
            for (const auto& i : legal)
                if (i.chosen_value.has_value() && *i.chosen_value == 2) return i;
            return legal.front();
        }, exec);

    EXPECT_EQ(state.getObject(unit).battlefieldId(), std::optional<BattlefieldId>(1));
    EXPECT_FALSE(state.getObject(unit).is_exhausted);
}

TEST_F(KennenAuditTest, RideTheWind_CanMoveBackToBase) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto unit = addUnit(P1, kPunchingPoro, 0, 0);
    state.getObject(unit).is_exhausted = true;
    auto spell = addToHand(P1, kRideTheWind);

    bool offered_current = false;
    driveResumable(kRideTheWind, P1, spell,
        [&](const std::vector<Intent>& legal) {
            if (!legal.front().chosen_objects.empty()) return pickObject(legal, unit);
            for (const auto& i : legal)
                if (i.chosen_value.has_value() && *i.chosen_value == 1) offered_current = true;
            for (const auto& i : legal)
                if (i.chosen_value.has_value() && *i.chosen_value == 0) return i;
            return legal.front();
        }, exec);

    EXPECT_FALSE(offered_current) << "CR 355.4.a: not its current location";
    EXPECT_TRUE(state.getObject(unit).isAtBase());
    EXPECT_FALSE(state.getObject(unit).is_exhausted);
}

// ── Last Rites pays the unit's cost ───────────────────────────────────────

TEST_F(KennenAuditTest, LastRites_PaysTheUnitsCostWhenPlayingItFromTrash) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto bearer = addUnit(P1, kPunchingPoro, 0, 0);
    auto gear = addGear(P1, kLastRites);
    auto poro = addToTrash(P1, kPunchingPoro);   // [2]
    addRune(P1, Domain::Chaos);
    addRune(P1, Domain::Chaos);

    Card* c = card_registry.get(kLastRites);
    ASSERT_NE(c, nullptr);
    CardContext ctx{state, events, exec, P1, gear};
    c->onEquippedTrigger(ctx, bearer, {});

    EXPECT_FALSE(inTrash(P1, poro));
    EXPECT_TRUE(state.getObject(poro).isAtBase());
    EXPECT_EQ(readyRuneCount(P1), 0) << "[2] was paid";
}

TEST_F(KennenAuditTest, LastRites_UnaffordableUnitIsNotOffered) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto bearer = addUnit(P1, kPunchingPoro, 0, 0);
    auto gear = addGear(P1, kLastRites);
    auto poro = addToTrash(P1, kPunchingPoro);   // [2], but only one rune
    addRune(P1, Domain::Chaos);

    Card* c = card_registry.get(kLastRites);
    ASSERT_NE(c, nullptr);
    CardContext ctx{state, events, exec, P1, gear};
    c->onEquippedTrigger(ctx, bearer, {});

    EXPECT_TRUE(inTrash(P1, poro));
    EXPECT_EQ(state.getObject(poro).zone, ZoneType::Trash);
    EXPECT_EQ(readyRuneCount(P1), 1);
}

// ── Fizz chooses the spell ────────────────────────────────────────────────

TEST_F(KennenAuditTest, Fizz_PlayerChoosesWhichSpellToReplay) {
    EffectExecutor exec(state, events, card_db, &card_registry);
    auto fizz = addUnit(P1, kFizz, 0, 0);
    auto gust = addToTrash(P1, kGust);
    auto flash = addToTrash(P1, kFlash);   // most recent

    driveResumableTrigger(kFizz, P1, fizz,
        [&](const std::vector<Intent>& legal) {
            if (isMayPrompt(legal)) return pickYes(legal);
            return pickObject(legal, gust);
        }, exec, TriggerType::WhenYouPlayMe);

    EXPECT_FALSE(inTrash(P1, gust)) << "the chosen spell was played (then recycled)";
    EXPECT_TRUE(inDeck(P1, gust));
    EXPECT_TRUE(inTrash(P1, flash));
}

}  // namespace
}  // namespace riftbound::test
