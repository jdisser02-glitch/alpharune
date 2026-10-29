/// @file test_radiance_b05.cpp
/// Radiance batch 05: Nazuman Ntofo (Equip [1], Shield 2), Overloaded
/// Traveler (+1 [M] beside a friendly [Deploy] gear), Pillaged Armory
/// ([Deploy], buff a friendly unit on play / conquer here), Raving Witness
/// (on move: draw a Mighty top card, else recycle it), Skystrike Squadron
/// (return a gear for [2] off), Sneaky Zeebles (conquer: [Add] [A]), Wrath of
/// the Freljord (enters ready), Avalanche (3 to every unit in a base),
/// Primordial Roar ([Show Off] a unit: deal its Might) and Stalwart Goatherd
/// (may kill a gear costing [4] or less).

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kNazuman        = 1035;
constexpr CardDefId kTraveler       = 1036;
constexpr CardDefId kArmory         = 1037;
constexpr CardDefId kWitness        = 1038;
constexpr CardDefId kSquadron       = 1039;
constexpr CardDefId kZeebles        = 1040;
constexpr CardDefId kWrath          = 1041;
constexpr CardDefId kAvalanche      = 1042;
constexpr CardDefId kRoar           = 1043;
constexpr CardDefId kGoatherd       = 1044;
constexpr CardDefId kOtterpus       = 840;  // 2-energy 2-Might unit, no play or exhaust text
constexpr CardDefId kScorcher       = 1;    // 5-energy 5-Might unit ([Accelerate])
constexpr CardDefId kBlade          = 962;  // gear, Energy 3 (Equip; no triggers)
constexpr CardDefId kAurora         = 160;  // gear, Energy 9

/// Answers yes/no and mode prompts from a queue of int answers, picks `want`
/// when offered objects, passes priority otherwise. Records every prompt.
class B05Agent : public AgentInterface {
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
        return f;
    }
};

class RadianceB05Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B05Agent p1_, p2_;
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
    GameObjectId rune(PlayerId p, Domain d, bool exhausted = false) {
        auto id = s().createObject();
        auto& r = s().getObject(id);
        r.owner = r.controller = p;
        r.card_type = CardType::Rune;
        r.name = "Rune";
        r.domains = {d};
        r.zone = ZoneType::Base;
        r.location = BaseLocation{p};
        r.is_exhausted = exhausted;
        return id;
    }
    int readyRunes(PlayerId p) {
        int n = 0;
        for (auto& [id, o] : s().objects)
            if (o.isRune() && o.controller == p && o.location.has_value() && !o.is_exhausted) ++n;
        return n;
    }
    void play(GameObjectId card, std::optional<LocationId> where = std::nullopt) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
    }
    void moveToBf1(GameObjectId unit) {
        engine_->testHook_executeIntent(
            Intent::standardMove(P1, {unit}, BattlefieldLocation{1}));
        engine_->testHook_runChain();
    }
    bool offersPlayAt(GameObjectId card, LocationId where) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::PlayCard && a.card == card &&
                a.play_location == std::optional<LocationId>{where})
                return true;
        return false;
    }
    std::vector<std::vector<GameObjectId>> equipTargets(GameObjectId gear) {
        std::vector<std::vector<GameObjectId>> out;
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::ActivateAbility && a.ability_source == gear)
                out.push_back(a.targets);
        return out;
    }
    bool inHand(PlayerId p, GameObjectId id) {
        const auto& h = s().player(p).hand;
        return std::find(h.begin(), h.end(), id) != h.end();
    }
    static bool contains(const std::vector<GameObjectId>& v, GameObjectId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
};

// ─── RAD-071 Nazuman Ntofo ──────────────────────────────────────────────────

TEST_F(RadianceB05Test, Nazuman_EquipForOneGrantsShieldTwo) {
    makeEngine();
    auto gear = boardObject(P1, kNazuman);
    auto unit = boardObject(P1, kOtterpus);
    EXPECT_TRUE(equipTargets(gear).empty()) << "no Energy to pay [1]";

    rune(P1, Domain::Order);
    EXPECT_EQ(equipTargets(gear),
              (std::vector<std::vector<GameObjectId>>{std::vector<GameObjectId>{unit}}));
    Intent equip;
    equip.type = IntentType::ActivateAbility;
    equip.player = P1;
    equip.ability_source = gear;
    equip.targets = {unit};
    engine_->testHook_executeIntent(equip);
    engine_->testHook_cleanup();

    EXPECT_EQ(s().getObject(gear).attached_to, std::optional<GameObjectId>{unit});
    EXPECT_EQ(readyRunes(P1), 0) << "paid [1]";
    EXPECT_TRUE(s().getObject(unit).hasKeyword(Keyword::Shield));
    bool shield2 = false;
    for (const auto& ae : s().getObject(unit).aura_effects)
        if (ae.source == gear && ae.keyword == Keyword::Shield && ae.keyword_value == 2) shield2 = true;
    EXPECT_TRUE(shield2);
    EXPECT_TRUE(card_db.get(kNazuman).hasTag("Equipment"));
}

// ─── RAD-072 Overloaded Traveler ────────────────────────────────────────────

TEST_F(RadianceB05Test, Traveler_PlusOneBesideAFriendlyDeployGear) {
    makeEngine();
    auto traveler = boardObject(P1, kTraveler, 0);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(traveler).current_might, 2);

    auto armory = boardObject(P1, kArmory, 0);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(traveler).current_might, 3) << "a friendly [Deploy] gear here";

    s().getObject(armory).location = BattlefieldLocation{1};
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(traveler).current_might, 2) << "the gear is at another battlefield";
}

TEST_F(RadianceB05Test, Traveler_NoBonusFromEnemyOrNonDeployGear) {
    makeEngine();
    auto traveler = boardObject(P1, kTraveler, 0);
    boardObject(P2, kArmory, 0);                  // enemy [Deploy] gear
    auto blade = boardObject(P1, kBlade, 0);      // not [Deploy]: recalled in cleanup
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(blade).isAtBase());
    EXPECT_EQ(s().getObject(traveler).current_might, 2);
}

// ─── RAD-073 Pillaged Armory ────────────────────────────────────────────────

TEST_F(RadianceB05Test, Armory_DeploysAndBuffsAFriendlyUnitWhenPlayed) {
    makeEngine();
    auto ally = boardObject(P1, kOtterpus);
    auto enemy = boardObject(P2, kOtterpus);
    rune(P1, Domain::Calm);
    auto armory = cardInHand(P1, kArmory);
    EXPECT_FALSE(offersPlayAt(armory, LocationId{BaseLocation{P1}})) << "[Deploy]";
    EXPECT_TRUE(offersPlayAt(armory, LocationId{BattlefieldLocation{1}}));
    p1_.want = ally;
    play(armory, LocationId{BattlefieldLocation{1}});
    EXPECT_TRUE(s().getObject(armory).isAtBattlefield());
    EXPECT_EQ(s().getObject(ally).buff_count, 1);
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), enemy)) << "a friendly unit";
}

TEST_F(RadianceB05Test, Armory_ConquerHereBuffsAFriendlyUnit) {
    makeEngine();
    boardObject(P1, kArmory, 0);
    auto ally = boardObject(P1, kOtterpus, 0);
    p1_.want = ally;
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(ally).buff_count, 1);
}

TEST_F(RadianceB05Test, Armory_NoSecondBuffOnABuffedUnit) {
    makeEngine();
    boardObject(P1, kArmory, 0);
    auto ally = boardObject(P1, kOtterpus, 0);
    s().getObject(ally).buff_count = 1;
    s().getObject(ally).recomputeMight();
    p1_.want = ally;
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    ASSERT_FALSE(p1_.obj_prompts.empty()) << "the conquer ability triggered";
    EXPECT_EQ(s().getObject(ally).buff_count, 1) << "it already has a buff";
}

// ─── RAD-074 Raving Witness ─────────────────────────────────────────────────

TEST_F(RadianceB05Test, Witness_DrawsAMightyTopCard) {
    makeEngine();
    auto witness = boardObject(P1, kWitness);
    auto big = deckCard(P1, kScorcher);
    moveToBf1(witness);
    EXPECT_TRUE(inHand(P1, big));
    EXPECT_EQ(s().getObject(big).zone, ZoneType::Hand);
}

TEST_F(RadianceB05Test, Witness_RecyclesANonMightyTopCard) {
    makeEngine();
    auto witness = boardObject(P1, kWitness);
    auto bottom = deckCard(P1, kOtterpus);
    auto small = deckCard(P1, kOtterpus);
    moveToBf1(witness);
    EXPECT_FALSE(inHand(P1, small));
    const auto& deck = s().player(P1).main_deck;
    ASSERT_EQ(deck.size(), 2u);
    EXPECT_EQ(deck.back(), bottom) << "the other card is on top now";
    EXPECT_EQ(deck.front(), small) << "recycled to the bottom";
}

// ─── RAD-075 Skystrike Squadron ─────────────────────────────────────────────

TEST_F(RadianceB05Test, Squadron_ReturnAGearToPlayForThree) {
    makeEngine();
    auto sq = cardInHand(P1, kSquadron);
    s().player(P1).rune_pool.energy = 3;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, sq)) << "no gear: full [5]";
    auto blade = boardObject(P1, kBlade);
    EXPECT_TRUE(engine_->testHook_canAfford(P1, sq));
    p1_.answers = {0};   // return a gear
    p1_.want = blade;
    play(sq, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(sq).location.has_value());
    EXPECT_TRUE(inHand(P1, blade)) << "the gear went back to its owner's hand";
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0})) << "[2] more can't be paid";
}

TEST_F(RadianceB05Test, Squadron_KeepTheGearAndPayFull) {
    makeEngine();
    auto sq = cardInHand(P1, kSquadron);
    s().player(P1).rune_pool.energy = 5;
    auto blade = boardObject(P1, kBlade);
    p1_.answers = {1};   // pay [2] instead
    play(sq, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(sq).location.has_value());
    EXPECT_TRUE(s().getObject(blade).isAtBase());
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0) << "5 in all";
}

TEST_F(RadianceB05Test, Squadron_NoGearNoPrompt) {
    makeEngine();
    auto sq = cardInHand(P1, kSquadron);
    s().player(P1).rune_pool.energy = 5;
    play(sq, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(sq).location.has_value());
    EXPECT_TRUE(p1_.int_prompts.empty());
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
    EXPECT_TRUE(card_db.get(kSquadron).keywords.has(Keyword::Deflect));
    EXPECT_EQ(card_db.get(kSquadron).deflect_value, 1);
}

// ─── RAD-076 Sneaky Zeebles ─────────────────────────────────────────────────

TEST_F(RadianceB05Test, Zeebles_ConquerAddsUniversalPower) {
    makeEngine();
    boardObject(P1, kZeebles, 0);
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    EXPECT_EQ(s().player(P1).rune_pool.universal_power, 1);
}

// ─── RAD-077 Wrath of the Freljord ──────────────────────────────────────────

TEST_F(RadianceB05Test, Wrath_EntersReadyWithShieldThree) {
    makeEngine();
    s().player(P1).rune_pool.energy = 9;
    auto wrath = cardInHand(P1, kWrath);
    play(wrath, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(wrath).location.has_value());
    EXPECT_FALSE(s().getObject(wrath).is_exhausted);
    EXPECT_TRUE(card_db.get(kWrath).keywords.has(Keyword::Shield));
    EXPECT_EQ(card_db.get(kWrath).shield_value, 3);
}

// ─── RAD-079 Avalanche ──────────────────────────────────────────────────────

TEST_F(RadianceB05Test, Avalanche_DealsThreeToEveryUnitInABase) {
    makeEngine();
    auto mine = boardObject(P1, kOtterpus);
    auto theirs = boardObject(P2, kScorcher);
    auto away = boardObject(P2, kOtterpus, 0);
    s().player(P1).rune_pool.energy = 5;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 2;
    auto spell = cardInHand(P1, kAvalanche);
    play(spell);
    EXPECT_FALSE(s().getObject(mine).location.has_value()) << "friendly units in base too";
    ASSERT_TRUE(s().getObject(theirs).location.has_value());
    EXPECT_EQ(s().getObject(theirs).damage_marked, 3);
    EXPECT_EQ(s().getObject(away).damage_marked, 0) << "not in a base";
}

// ─── RAD-082 Primordial Roar ────────────────────────────────────────────────

TEST_F(RadianceB05Test, Roar_DealsTheShownOffFriendlyUnitsMight) {
    makeEngine();
    auto ally = boardObject(P1, kOtterpus);
    engine_->testHook_executor().giveTemporaryMight(ally, 2);   // 4 [M]
    auto target = boardObject(P2, kScorcher, 0);
    s().player(P1).rune_pool.energy = 4;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 2;
    auto spell = cardInHand(P1, kRoar);
    p1_.want = target;
    play(spell);
    ASSERT_TRUE(s().getObject(target).location.has_value());
    EXPECT_EQ(s().getObject(target).damage_marked, 4);
}

TEST_F(RadianceB05Test, Roar_ShowsOffAUnitFromHand) {
    makeEngine();
    cardInHand(P1, kScorcher);   // 5 [M] in hand
    auto target = boardObject(P2, kScorcher, 0);
    s().player(P1).rune_pool.energy = 4;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 2;
    auto spell = cardInHand(P1, kRoar);
    p1_.want = target;
    play(spell);
    EXPECT_FALSE(s().getObject(target).location.has_value()) << "5 damage kills a 5 [M] unit";
}

TEST_F(RadianceB05Test, Roar_NothingToShowOffDoesNothing) {
    makeEngine();
    auto target = boardObject(P2, kOtterpus, 0);
    s().player(P1).rune_pool.energy = 4;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 2;
    auto spell = cardInHand(P1, kRoar);
    p1_.want = target;
    play(spell);
    EXPECT_EQ(s().getObject(target).damage_marked, 0);
    EXPECT_TRUE(p1_.obj_prompts.empty()) << "no target is chosen";
}

// ─── RAD-083 Stalwart Goatherd ──────────────────────────────────────────────

TEST_F(RadianceB05Test, Goatherd_MayKillAGearCostingFourOrLess) {
    makeEngine();
    auto cheap = boardObject(P2, kBlade);
    auto dear = boardObject(P2, kAurora);
    s().player(P1).rune_pool.energy = 4;
    auto goat = cardInHand(P1, kGoatherd);
    p1_.answers = {1};
    p1_.want = cheap;
    play(goat, LocationId{BaseLocation{P1}});
    EXPECT_FALSE(s().getObject(cheap).location.has_value()) << "killed";
    EXPECT_TRUE(s().getObject(dear).location.has_value());
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), dear)) << "Energy cost 9";
    EXPECT_TRUE(card_db.get(kGoatherd).keywords.has(Keyword::Shield));
    EXPECT_EQ(card_db.get(kGoatherd).shield_value, 2);
}

TEST_F(RadianceB05Test, Goatherd_DeclineKeepsTheGear) {
    makeEngine();
    auto cheap = boardObject(P2, kBlade);
    s().player(P1).rune_pool.energy = 4;
    auto goat = cardInHand(P1, kGoatherd);
    p1_.answers = {0};
    p1_.want = cheap;
    play(goat, LocationId{BaseLocation{P1}});
    EXPECT_TRUE(s().getObject(cheap).location.has_value());
    EXPECT_TRUE(p1_.obj_prompts.empty());
}

}  // namespace
}  // namespace riftbound::test
