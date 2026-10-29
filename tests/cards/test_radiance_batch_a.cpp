/// @file test_radiance_batch_a.cpp
/// Radiance batch A: Hush (counter a spell with Power cost [A][A]+), Ki Burst
/// (exhaust a friendly unit as an additional cost, stun a unit), Bilgewater
/// Tracker (deal 1 to each of up to 2 units), Trifarian Captain (look at 4,
/// draw a cheap unit or gear, recycle the rest), Lost to the Sands (deal 4 /
/// kill a gear), Hunker Down (ready a friendly unit, draw 1) and Siren's Song
/// (move an enemy unit that moved this turn).

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kHush           = 971;
constexpr CardDefId kKiBurst        = 972;
constexpr CardDefId kTracker        = 973;
constexpr CardDefId kCaptain        = 974;
constexpr CardDefId kLostToTheSands = 975;
constexpr CardDefId kHunkerDown     = 976;
constexpr CardDefId kSirensSong     = 977;
constexpr CardDefId kRivenDeserter  = 970;  // "When another friendly unit becomes exhausted, +1 [M]"
constexpr CardDefId kBlade          = 962;  // gear, Energy 3 / Power 1
constexpr CardDefId kOnTheHunt      = 518;  // spell, energy 1 / power 2: "Ready your units."
constexpr CardDefId kScorcher       = 1;    // unit, energy 5
constexpr CardDefId kOtterpus       = 840;  // 2-energy 2-Might unit, no play or exhaust text

/// Answers int prompts (yes/no, modes, destinations) from `answers`, object
/// prompts from `picks` (a pick is consumed only when it is offered), plays
/// `react_with` the first time it is offered, passes priority otherwise.
/// Records every prompt it was shown.
class BatchAAgent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    std::vector<GameObjectId> picks;
    size_t pnext = 0;
    GameObjectId react_with = kInvalidId;
    std::vector<std::vector<int>> int_prompts;
    std::vector<std::vector<GameObjectId>> obj_prompts;

    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        if (react_with != kInvalidId) {
            for (const auto& i : legal) {
                const bool is_play = i.type == IntentType::PlayReaction ||
                                     i.type == IntentType::PlayCard ||
                                     i.type == IntentType::PlayActionCard;
                if (is_play && i.card == react_with) {
                    react_with = kInvalidId;
                    return i;
                }
            }
        }
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice) {
            bool has_objects = false;
            for (const auto& i : legal) if (!i.chosen_objects.empty()) has_objects = true;
            if (has_objects) {
                std::vector<GameObjectId> offered;
                for (const auto& i : legal)
                    if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects.front());
                obj_prompts.push_back(offered);
                if (pnext < picks.size()) {
                    for (const auto& i : legal)
                        if (!i.chosen_objects.empty() && i.chosen_objects.front() == picks[pnext]) {
                            ++pnext;
                            return i;
                        }
                }
                return f;
            }
            std::vector<int> offered;
            for (const auto& i : legal) offered.push_back(i.chosen_value.value_or(-1));
            int_prompts.push_back(offered);
            if (next < answers.size()) {
                int a = answers[next++];
                for (const auto& i : legal) if (i.chosen_value == a) return i;
            }
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class RadianceBatchATest : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    BatchAAgent p1_, p2_;
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
    GameObjectId setLegend(PlayerId p) {
        auto id = s().createObject();
        auto& lo = s().getObject(id);
        lo.owner = lo.controller = p;
        lo.card_type = CardType::Legend;
        lo.zone = ZoneType::LegendZone;
        s().player(p).legend_zone = id;
        return id;
    }
    void stamp(GameObject& o, PlayerId p, CardDefId def_id) {
        const auto& def = card_db.get(def_id);
        o.owner = o.controller = p;
        o.card_def_id = def_id;
        o.card_type = def.card_type;
        o.super_type = def.super_type;
        o.name = def.name;
        o.domains = def.domains;
        o.keywords = def.keywords;
        o.tags = def.tags;
        o.base_might = def.might;
        o.might_bonus = def.might_bonus;
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
    void play(GameObjectId card, std::optional<LocationId> where = std::nullopt) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
    }
    bool offersPlay(GameObjectId card) {
        for (const auto& a : engine_->generateLegalActions())
            if ((a.type == IntentType::PlayCard || a.type == IntentType::PlayActionCard ||
                 a.type == IntentType::PlayReaction) && a.card == card)
                return true;
        return false;
    }
    bool inZone(GameObjectId id, ZoneType z) { return s().getObject(id).zone == z; }
    static bool contains(const std::vector<GameObjectId>& v, GameObjectId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
    /// The first object prompt the agent was shown that offered `id`.
    static const std::vector<GameObjectId>* promptWith(const BatchAAgent& a, GameObjectId id) {
        for (const auto& p : a.obj_prompts) if (contains(p, id)) return &p;
        return nullptr;
    }
};

// ─── RAD-041 Hush ───────────────────────────────────────────────────────────

TEST_F(RadianceBatchATest, Hush_CountersASpellWithPowerCostTwo) {
    makeEngine();
    auto tired = boardObject(P1, kOtterpus);
    s().getObject(tired).is_exhausted = true;
    auto hunt = cardInHand(P1, kOnTheHunt);
    s().player(P1).rune_pool.energy = 1;
    s().player(P1).rune_pool.universal_power = 2;
    auto hush = cardInHand(P2, kHush);
    s().player(P2).rune_pool.energy = 1;
    s().player(P2).rune_pool.universal_power = 2;   // Hush has no domain: [A][A]
    p2_.react_with = hush;

    play(hunt);

    EXPECT_EQ(p2_.react_with, kInvalidId) << "Hush was offered and played";
    EXPECT_TRUE(s().chain.items.empty());
    EXPECT_TRUE(s().getObject(tired).is_exhausted) << "On the Hunt was countered";
    EXPECT_TRUE(inZone(hunt, ZoneType::Trash));
    EXPECT_TRUE(inZone(hush, ZoneType::Trash));
}

TEST_F(RadianceBatchATest, Hush_DoesNotCounterACheaperSpell) {
    makeEngine();
    auto tired = boardObject(P1, kOtterpus);
    s().getObject(tired).is_exhausted = true;
    deckCard(P1, kOtterpus);                   // something for Hunker Down to draw
    auto hunker = cardInHand(P1, kHunkerDown);  // no Power cost
    s().player(P1).rune_pool.energy = 1;
    p1_.picks = {tired};
    auto hush = cardInHand(P2, kHush);
    s().player(P2).rune_pool.energy = 1;
    s().player(P2).rune_pool.universal_power = 2;
    p2_.react_with = hush;

    play(hunker);

    EXPECT_EQ(p2_.react_with, kInvalidId) << "Hush was played in response";
    EXPECT_FALSE(s().getObject(tired).is_exhausted) << "Hunker Down still resolved";
    EXPECT_TRUE(inZone(hush, ZoneType::Trash));
}

TEST_F(RadianceBatchATest, Hush_NotPlayableWithNothingOnTheChain) {
    makeEngine();
    auto hush = cardInHand(P1, kHush);
    s().player(P1).rune_pool.energy = 1;
    s().player(P1).rune_pool.universal_power = 2;
    EXPECT_FALSE(offersPlay(hush));
}

// ─── RAD-028 Ki Burst ───────────────────────────────────────────────────────

TEST_F(RadianceBatchATest, KiBurst_NeedsAReadyFriendlyUnitToPlay) {
    makeEngine();
    auto mine = boardObject(P1, kOtterpus);
    s().getObject(mine).is_exhausted = true;
    boardObject(P2, kOtterpus, 0);
    auto ki = cardInHand(P1, kKiBurst);
    s().player(P1).rune_pool.energy = 1;
    EXPECT_FALSE(offersPlay(ki)) << "no ready friendly unit to exhaust";
    s().getObject(mine).is_exhausted = false;
    EXPECT_TRUE(offersPlay(ki));
}

TEST_F(RadianceBatchATest, KiBurst_ExhaustsTheChosenUnitAndStuns) {
    makeEngine();
    auto deserter = boardObject(P1, kRivenDeserter);
    auto payer = boardObject(P1, kOtterpus);
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto ki = cardInHand(P1, kKiBurst);
    s().player(P1).rune_pool.energy = 1;
    p1_.picks = {payer, enemy};

    play(ki);

    EXPECT_TRUE(s().getObject(payer).is_exhausted) << "the additional cost";
    EXPECT_FALSE(s().getObject(deserter).is_exhausted) << "only the chosen unit";
    EXPECT_TRUE(s().getObject(enemy).is_stunned);
    EXPECT_EQ(s().getObject(deserter).current_might, 6)
        << "exhausting through the executor fires 'becomes exhausted' triggers";
    ASSERT_GE(p1_.obj_prompts.size(), 2u);
    const auto* payers = promptWith(p1_, payer);
    ASSERT_NE(payers, nullptr);
    EXPECT_FALSE(contains(*payers, enemy)) << "only friendly units can pay";
    EXPECT_TRUE(inZone(ki, ZoneType::Trash));
}

// ─── RAD-003 Bilgewater Tracker ─────────────────────────────────────────────

TEST_F(RadianceBatchATest, Tracker_DealsOneToTwoUnitsAndKillsTheLethallyDamaged) {
    makeEngine();
    auto hurt = boardObject(P2, kOtterpus, 0);
    s().getObject(hurt).damage_marked = 1;     // 1 more is lethal
    auto healthy = boardObject(P2, kOtterpus, 1);
    auto tracker = cardInHand(P1, kTracker);
    s().player(P1).rune_pool.energy = 3;
    p1_.answers = {2};                         // two units
    p1_.picks = {hurt, healthy};

    play(tracker, LocationId{BaseLocation{P1}});

    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0, 1, 2}));
    EXPECT_FALSE(s().getObject(hurt).location.has_value()) << "killed";
    EXPECT_TRUE(s().getObject(healthy).location.has_value());
    EXPECT_EQ(s().getObject(healthy).damage_marked, 1);
    EXPECT_EQ(s().getObject(tracker).damage_marked, 0);
}

TEST_F(RadianceBatchATest, Tracker_MayChooseNoUnits) {
    makeEngine();
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto tracker = cardInHand(P1, kTracker);
    s().player(P1).rune_pool.energy = 3;
    p1_.answers = {0};                         // none

    play(tracker, LocationId{BaseLocation{P1}});

    EXPECT_EQ(s().getObject(enemy).damage_marked, 0);
    EXPECT_EQ(s().getObject(tracker).damage_marked, 0);
    EXPECT_TRUE(p1_.obj_prompts.empty()) << "no unit is asked for";
}

// ─── RAD-129 Trifarian Captain ──────────────────────────────────────────────

TEST_F(RadianceBatchATest, Captain_DrawsAnEligibleCardAndRecyclesTheRest) {
    makeEngine();
    auto filler   = deckCard(P1, kScorcher);   // fifth from the top: not looked at
    auto scorcher = deckCard(P1, kScorcher);   // energy 5: not eligible
    auto hunt     = deckCard(P1, kOnTheHunt);  // a spell: not eligible
    auto blade    = deckCard(P1, kBlade);      // gear [3] / [A]: eligible
    auto otter    = deckCard(P1, kOtterpus);   // unit [2]: eligible (top card)
    auto captain = cardInHand(P1, kCaptain);
    s().player(P1).rune_pool.energy = 2;
    p1_.answers = {1};                         // yes, reveal one
    p1_.picks = {blade};

    play(captain, LocationId{BaseLocation{P1}});

    const auto& ps = s().player(P1);
    EXPECT_TRUE(contains(ps.hand, blade));
    EXPECT_TRUE(inZone(blade, ZoneType::Hand));
    const auto* offered = promptWith(p1_, blade);
    ASSERT_NE(offered, nullptr);
    EXPECT_TRUE(contains(*offered, otter));
    EXPECT_FALSE(contains(*offered, scorcher));
    EXPECT_FALSE(contains(*offered, hunt));
    ASSERT_EQ(ps.main_deck.size(), 4u);
    EXPECT_EQ(ps.main_deck.back(), filler) << "the other three went to the bottom";
    EXPECT_TRUE(contains(ps.main_deck, otter));
    EXPECT_TRUE(contains(ps.main_deck, scorcher));
    EXPECT_TRUE(contains(ps.main_deck, hunt));
}

TEST_F(RadianceBatchATest, Captain_NothingEligibleRecyclesAllFour) {
    makeEngine();
    auto filler = deckCard(P1, kOtterpus);
    deckCard(P1, kScorcher);
    deckCard(P1, kOnTheHunt);
    deckCard(P1, kScorcher);
    deckCard(P1, kLostToTheSands);
    auto captain = cardInHand(P1, kCaptain);
    s().player(P1).rune_pool.energy = 2;
    const size_t hand_before = s().player(P1).hand.size() - 1;   // minus the Captain

    play(captain, LocationId{BaseLocation{P1}});

    EXPECT_TRUE(p1_.int_prompts.empty()) << "the reveal is not offered";
    EXPECT_EQ(s().player(P1).hand.size(), hand_before);
    ASSERT_EQ(s().player(P1).main_deck.size(), 5u);
    EXPECT_EQ(s().player(P1).main_deck.back(), filler);
}

// ─── RAD-013 Lost to the Sands ──────────────────────────────────────────────

TEST_F(RadianceBatchATest, LostToTheSands_DealsFourToAUnitAtABattlefield) {
    makeEngine();
    auto at_bf = boardObject(P2, kOtterpus, 0);
    auto at_base = boardObject(P2, kOtterpus);
    auto spell = cardInHand(P1, kLostToTheSands);
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Fury)] = 1;
    p1_.answers = {0};
    p1_.picks = {at_bf};

    play(spell);

    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0})) << "no gear: only mode 0";
    EXPECT_FALSE(s().getObject(at_bf).location.has_value()) << "4 damage killed it";
    const auto* offered = promptWith(p1_, at_bf);
    ASSERT_NE(offered, nullptr);
    EXPECT_FALSE(contains(*offered, at_base)) << "only units at a battlefield";
    EXPECT_TRUE(s().getObject(at_base).location.has_value());
}

TEST_F(RadianceBatchATest, LostToTheSands_KillsAGear) {
    makeEngine();
    auto unit = boardObject(P2, kOtterpus, 0);
    auto gear = boardObject(P2, kBlade);
    auto spell = cardInHand(P1, kLostToTheSands);
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Fury)] = 1;
    p1_.answers = {1};
    p1_.picks = {gear};

    play(spell);

    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0, 1}));
    EXPECT_FALSE(s().getObject(gear).location.has_value());
    EXPECT_EQ(s().getObject(unit).damage_marked, 0);
}

// ─── RAD-034 Hunker Down ────────────────────────────────────────────────────

TEST_F(RadianceBatchATest, HunkerDown_ReadiesAFriendlyUnitAndDraws) {
    makeEngine();
    auto unit = boardObject(P1, kOtterpus, 0);
    s().getObject(unit).is_exhausted = true;
    auto enemy = boardObject(P2, kOtterpus, 0);
    s().getObject(enemy).is_exhausted = true;
    auto top = deckCard(P1, kScorcher);
    auto spell = cardInHand(P1, kHunkerDown);
    s().player(P1).rune_pool.energy = 1;
    p1_.picks = {unit};

    play(spell);

    EXPECT_FALSE(s().getObject(unit).is_exhausted);
    EXPECT_TRUE(s().getObject(enemy).is_exhausted);
    EXPECT_TRUE(contains(s().player(P1).hand, top)) << "draw 1";
    const auto* offered = promptWith(p1_, unit);
    ASSERT_NE(offered, nullptr);
    EXPECT_FALSE(contains(*offered, enemy)) << "a friendly unit";
    EXPECT_EQ(s().getObject(unit).card_counters["__cant_move_turn"], s().turn.turn_number + 1)
        << "the can't-move-this-turn marker";
}

// ─── RAD-098 Siren's Song ───────────────────────────────────────────────────

TEST_F(RadianceBatchATest, SirensSong_MovesAnEnemyUnitThatMoved) {
    makeEngine();
    auto moved = boardObject(P2, kOtterpus, 0);
    s().getObject(moved).moves_this_turn = 1;
    auto stayed = boardObject(P2, kOtterpus, 1);
    auto spell = cardInHand(P1, kSirensSong);
    s().player(P1).rune_pool.energy = 2;
    p1_.picks = {moved};
    p1_.answers = {0};                         // to its controller's base

    play(spell);

    EXPECT_EQ(s().getObject(moved).location, std::optional<LocationId>{LocationId{BaseLocation{P2}}});
    EXPECT_TRUE(s().getObject(stayed).isAtBattlefield());
    const auto* offered = promptWith(p1_, moved);
    ASSERT_NE(offered, nullptr);
    EXPECT_FALSE(contains(*offered, stayed)) << "it hasn't moved this turn";
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.back(), (std::vector<int>{0, 2})) << "base or the other battlefield";
}

TEST_F(RadianceBatchATest, SirensSong_NotPlayableWithoutAnEnemyThatMoved) {
    makeEngine();
    boardObject(P2, kOtterpus, 0);
    auto mine = boardObject(P1, kOtterpus, 1);
    s().getObject(mine).moves_this_turn = 1;   // friendly: doesn't count
    auto spell = cardInHand(P1, kSirensSong);
    s().player(P1).rune_pool.energy = 2;
    EXPECT_FALSE(offersPlay(spell));
}

}  // namespace
}  // namespace riftbound::test
