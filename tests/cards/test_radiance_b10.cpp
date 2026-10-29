/// @file test_radiance_b10.cpp
/// Radiance batch 10: Hexplosives Expert, Hexplosive Minefield, Iron Revenant,
/// Realm of Death, Lady of Clockwork, The Ball, Pride of Nazumah, Ntofo
/// Strikes, Starry-Eyed Songstress, Encore.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "cards/radiance_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kHexExpert   = 1085;
constexpr CardDefId kMinefield   = 1086;
constexpr CardDefId kRevenant    = 1087;
constexpr CardDefId kRealm       = 1088;
constexpr CardDefId kClockwork   = 1089;
constexpr CardDefId kTheBall     = 1090;
constexpr CardDefId kPride       = 1091;
constexpr CardDefId kNtofo       = 1092;
constexpr CardDefId kSongstress  = 1093;
constexpr CardDefId kEncore      = 1094;
constexpr CardDefId kBomb        = kBombTokenCardDefId;
constexpr CardDefId kPoster      = 983;  // Concert Poster: 1-energy [Deploy] gear
constexpr CardDefId kOtterpus    = 840;  // 2-energy 2-Might unit, no play or exhaust text

/// Answers int prompts (yes/no, modes) from `answers`; for object prompts picks
/// the next object in `wants` (falling back to `want`). A prompt that mixes
/// objects with an int option (Hexplosive Minefield's "done") takes the next
/// wanted object if it is offered, else the int option.
class B10Agent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    std::vector<GameObjectId> wants;
    size_t wnext = 0;
    GameObjectId want = kInvalidId;
    std::vector<std::vector<int>> int_prompts;
    int object_prompts = 0;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        bool any_obj = false, any_val = false;
        for (const auto& i : legal) {
            if (i.type != IntentType::MakeChoice) continue;
            if (!i.chosen_objects.empty()) any_obj = true;
            else if (i.chosen_value.has_value()) any_val = true;
        }
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice && any_obj) {
            ++object_prompts;
            if (wnext < wants.size()) {
                for (const auto& i : legal)
                    if (!i.chosen_objects.empty() && i.chosen_objects.front() == wants[wnext]) {
                        ++wnext;
                        return i;
                    }
            }
            if (any_val)
                for (const auto& i : legal)
                    if (i.chosen_objects.empty() && i.chosen_value.has_value()) return i;
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            return f;
        }
        if (f.type == IntentType::MakeChoice && any_val) {
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

class RadianceB10Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B10Agent p1_, p2_;
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
    GameObjectId boardObject(PlayerId p, CardDefId def_id, int at_bf = -1) {
        auto id = s().createObject();
        auto& u = s().getObject(id);
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
    GameObjectId unitWithMight(PlayerId p, int might, int at_bf = -1) {
        auto id = boardObject(p, kOtterpus, at_bf);
        auto& u = s().getObject(id);
        u.base_might = might;
        u.recomputeMight();
        return id;
    }
    GameObjectId bombAt(PlayerId p, int bf) {
        auto id = boardObject(p, kBomb, bf);
        s().getObject(id).is_token = true;
        return id;
    }
    GameObjectId cardInHand(PlayerId p, CardDefId def_id) {
        auto id = s().createObject();
        auto& c = s().getObject(id);
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
        c.zone = ZoneType::Hand;
        s().player(p).hand.push_back(id);
        return id;
    }
    GameObjectId cardInTrash(PlayerId p, CardDefId def_id) {
        auto id = cardInHand(p, def_id);
        auto& ps = s().player(p);
        ps.hand.erase(std::find(ps.hand.begin(), ps.hand.end(), id));
        s().getObject(id).zone = ZoneType::Trash;
        ps.trash.push_back(id);
        return id;
    }
    GameObjectId deckCard(PlayerId p) {   // pushed on top
        auto id = s().createObject();
        auto& c = s().getObject(id);
        c.owner = c.controller = p;
        c.card_def_id = kOtterpus;
        c.card_type = CardType::Unit;
        c.name = "Deck card";
        c.zone = ZoneType::MainDeck;
        s().player(p).main_deck.push_back(id);
        return id;
    }
    void play(GameObjectId card, std::optional<LocationId> where) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
    }
    void activate(GameObjectId source, std::vector<GameObjectId> targets = {}) {
        Intent i;
        i.type = IntentType::ActivateAbility;
        i.player = P1;
        i.ability_source = source;
        i.ability_index = 0;
        i.targets = std::move(targets);
        engine_->testHook_executeIntent(i);
    }
    bool offersActivation(GameObjectId source) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::ActivateAbility && a.ability_source == source) return true;
        return false;
    }
    std::vector<GameObjectId> bombs(PlayerId p, int bf = -1) {
        std::vector<GameObjectId> out;
        for (auto& [id, o] : s().objects) {
            if (!o.location.has_value() || o.controller != p || !isBomb(o)) continue;
            if (bf >= 0 && o.battlefieldId() != std::optional<BattlefieldId>{static_cast<BattlefieldId>(bf)})
                continue;
            out.push_back(id);
        }
        return out;
    }
    bool dead(GameObjectId id) { return !s().getObject(id).location.has_value(); }
    void startMainPhase() {
        events.emit(PhaseChangedEvent{TurnPhase::DrawPhase, TurnPhase::MainPhase, P1});
        engine_->testHook_runChain();
    }
};

// ─── Legends' champion tags (for "<tag>, <name>" deck aliases) ──────────────

TEST_F(RadianceB10Test, Legends_CarryTheirChampionTags) {
    EXPECT_TRUE(card_db.get(kHexExpert).hasTag("Ziggs"));
    EXPECT_TRUE(card_db.get(kRevenant).hasTag("Mordekaiser"));
    EXPECT_TRUE(card_db.get(kClockwork).hasTag("Orianna"));
    EXPECT_TRUE(card_db.get(kPride).hasTag("K'Sante"));
    EXPECT_TRUE(card_db.get(kSongstress).hasTag("Seraphine"));
    for (auto id : {kHexExpert, kRevenant, kClockwork, kPride, kSongstress})
        EXPECT_EQ(card_db.get(id).card_type, CardType::Legend);
}

// ─── RAD-141 Hexplosives Expert ─────────────────────────────────────────────

TEST_F(RadianceB10Test, HexplosivesExpert_PlaysABombToTheChosenBattlefield) {
    makeEngine();
    auto legend = setLegend(P1, kHexExpert); setLegend(P2);
    s().player(P1).rune_pool.energy = 1;
    s().player(P1).rune_pool.universal_power = 1;
    EXPECT_TRUE(offersActivation(legend));
    p1_.answers = {1};   // battlefield #1
    activate(legend);
    auto b = bombs(P1, 1);
    ASSERT_EQ(b.size(), 1u);
    EXPECT_EQ(s().getObject(b[0]).card_def_id, kBomb);
    EXPECT_TRUE(s().getObject(b[0]).isToken());
    EXPECT_TRUE(bombs(P1, 0).empty());
    EXPECT_TRUE(s().getObject(legend).is_exhausted);
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
    EXPECT_EQ(s().player(P1).rune_pool.universal_power, 0);
}

TEST_F(RadianceB10Test, HexplosivesExpert_NeedsTheEnergyAndThePower) {
    makeEngine();
    auto legend = setLegend(P1, kHexExpert); setLegend(P2);
    s().player(P1).rune_pool.energy = 1;   // no [A]
    EXPECT_FALSE(offersActivation(legend));
}

// ─── RAD-142 Hexplosive Minefield ───────────────────────────────────────────

TEST_F(RadianceB10Test, Minefield_KillsChosenBombsThenReplaysThatManyAtEndOfTurn) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto a = bombAt(P1, 0);
    auto b = bombAt(P1, 1);
    auto c = bombAt(P1, 0);   // not chosen, but dies to A's explosion ("your other Bombs there")
    s().player(P1).rune_pool.energy = 2;
    s().player(P1).rune_pool.universal_power = 1;
    auto spell = cardInHand(P1, kMinefield);
    p1_.wants = {a, b};   // then "done"
    play(spell, std::nullopt);
    EXPECT_TRUE(dead(a));
    EXPECT_TRUE(dead(b));
    EXPECT_TRUE(dead(c)) << "Bomb A's death killed the other Bomb at its battlefield";
    EXPECT_TRUE(bombs(P1).empty());

    p1_.answers = {0};   // the replacement Bombs go to battlefield #0
    engine_->testHook_endingStep();
    EXPECT_EQ(bombs(P1, 0).size(), 2u) << "that many = the two Minefield killed";
    EXPECT_TRUE(bombs(P1, 1).empty());
}

TEST_F(RadianceB10Test, Minefield_KillingNoBombsSchedulesNothing) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto a = bombAt(P1, 0);
    s().player(P1).rune_pool.energy = 2;
    s().player(P1).rune_pool.universal_power = 1;
    auto spell = cardInHand(P1, kMinefield);
    // No wants: the agent answers "done" straight away.
    play(spell, std::nullopt);
    EXPECT_FALSE(dead(a));
    engine_->testHook_endingStep();
    EXPECT_EQ(bombs(P1).size(), 1u);
}

// ─── RAD-143 Iron Revenant ──────────────────────────────────────────────────

TEST_F(RadianceB10Test, IronRevenant_BanishesAUnitFromTrashToAddPower) {
    makeEngine();
    auto legend = setLegend(P1, kRevenant); setLegend(P2);
    EXPECT_FALSE(offersActivation(legend)) << "empty trash: nothing to banish";
    auto spell_in_trash = cardInTrash(P1, kRealm);
    EXPECT_FALSE(offersActivation(legend)) << "a spell can't pay the cost";
    auto unit_in_trash = cardInTrash(P1, kOtterpus);
    EXPECT_TRUE(offersActivation(legend));
    p1_.want = unit_in_trash;
    activate(legend);
    EXPECT_EQ(s().getObject(unit_in_trash).zone, ZoneType::Banishment);
    EXPECT_EQ(s().getObject(spell_in_trash).zone, ZoneType::Trash);
    EXPECT_EQ(s().player(P1).rune_pool.universal_power, 1);
    EXPECT_TRUE(s().getObject(legend).is_exhausted);
}

// ─── RAD-144 Realm of Death ─────────────────────────────────────────────────

TEST_F(RadianceB10Test, RealmOfDeath_ShiftsMightThenTheyTrade) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto enemy = unitWithMight(P2, 4, 0);
    auto ally = unitWithMight(P1, 3, 0);
    s().player(P1).rune_pool.energy = 2;
    s().player(P1).rune_pool.universal_power = 1;
    auto spell = cardInHand(P1, kRealm);
    p1_.wants = {enemy, ally};
    play(spell, std::nullopt);
    EXPECT_TRUE(dead(enemy)) << "2 [M] after -2, dealt 5";
    ASSERT_FALSE(dead(ally));
    EXPECT_EQ(s().getObject(ally).current_might, 5);
    EXPECT_EQ(s().getObject(ally).damage_marked, 2) << "the enemy dealt its 2 [M]";
}

// ─── RAD-145 Lady of Clockwork ──────────────────────────────────────────────

TEST_F(RadianceB10Test, LadyOfClockwork_GearBecomesAUnitUntilAnotherIsChosen) {
    makeEngine();
    setLegend(P1, kClockwork); setLegend(P2);
    auto ball = boardObject(P1, kTheBall);
    auto poster = boardObject(P1, kPoster);
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {1};
    p1_.want = ball;
    startMainPhase();
    ASSERT_TRUE(s().getObject(ball).isUnit());
    EXPECT_EQ(s().getObject(ball).current_might, 5) << "Might = its Energy cost";
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0) << "paid [1]";
    EXPECT_TRUE(s().getObject(poster).isGear());

    s().turn.turn_number += 1;
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {1};
    p1_.next = 0;
    p1_.want = poster;
    startMainPhase();
    EXPECT_TRUE(s().getObject(poster).isUnit());
    EXPECT_EQ(s().getObject(poster).current_might, 1);
    EXPECT_TRUE(s().getObject(ball).isGear()) << "choosing another gear ends it";
    EXPECT_EQ(s().getObject(ball).base_might, 0);
}

TEST_F(RadianceB10Test, LadyOfClockwork_DecliningPaysNothing) {
    makeEngine();
    setLegend(P1, kClockwork); setLegend(P2);
    auto ball = boardObject(P1, kTheBall);
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {0};
    p1_.want = ball;
    startMainPhase();
    EXPECT_TRUE(s().getObject(ball).isGear());
    EXPECT_EQ(s().player(P1).rune_pool.energy, 1);
}

TEST_F(RadianceB10Test, LadyOfClockwork_UnitGearIsAGearAgainOnceItDies) {
    makeEngine();
    setLegend(P1, kClockwork); setLegend(P2);
    auto ball = boardObject(P1, kTheBall);
    s().player(P1).rune_pool.energy = 1;
    p1_.answers = {1};
    p1_.want = ball;
    startMainPhase();
    ASSERT_TRUE(s().getObject(ball).isUnit());
    engine_->testHook_executor().killObject(ball);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(ball).zone, ZoneType::Trash);
    EXPECT_TRUE(s().getObject(ball).isGear());
}

// ─── RAD-146 The Ball ───────────────────────────────────────────────────────

TEST_F(RadianceB10Test, TheBall_PlayedToABattlefieldDeals2ToEnemyUnitsThere) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto e1 = unitWithMight(P2, 2, 0);
    auto e2 = unitWithMight(P2, 3, 0);
    auto far = unitWithMight(P2, 2, 1);
    s().player(P1).rune_pool.energy = 5;
    s().player(P1).rune_pool.universal_power = 1;
    auto ball = cardInHand(P1, kTheBall);
    p1_.answers = {1};
    play(ball, LocationId{BattlefieldLocation{0}});
    ASSERT_TRUE(s().getObject(ball).isAtBattlefield());
    EXPECT_TRUE(dead(e1));
    ASSERT_FALSE(dead(e2));
    EXPECT_EQ(s().getObject(e2).damage_marked, 2);
    EXPECT_EQ(s().getObject(far).damage_marked, 0) << "only enemy units here";
}

TEST_F(RadianceB10Test, TheBall_DrawMode) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto top = deckCard(P1);
    s().player(P1).rune_pool.energy = 5;
    s().player(P1).rune_pool.universal_power = 1;
    auto ball = cardInHand(P1, kTheBall);
    p1_.answers = {0};
    play(ball, LocationId{BattlefieldLocation{0}});
    EXPECT_EQ(std::count(s().player(P1).hand.begin(), s().player(P1).hand.end(), top), 1);
}

TEST_F(RadianceB10Test, TheBall_MovingItAsAUnitTriggersToo) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto ball = boardObject(P1, kTheBall, 0);
    auto& b = s().getObject(ball);
    b.card_type = CardType::Unit;   // as Lady of Clockwork would make it
    b.base_might = 5;
    b.recomputeMight();
    auto enemy = unitWithMight(P2, 2, 1);
    p1_.answers = {1};
    engine_->testHook_executor().moveToBattlefield(ball, 1);
    engine_->testHook_runChain();
    EXPECT_TRUE(dead(enemy));
}

TEST_F(RadianceB10Test, TheBall_HasDeployAndGanking) {
    EXPECT_TRUE(card_registry.get(kTheBall)->deploys());
    EXPECT_TRUE(card_db.get(kTheBall).keywords.has(Keyword::Ganking));
}

// ─── RAD-147 Pride of Nazumah ───────────────────────────────────────────────

TEST_F(RadianceB10Test, Pride_GivesAssaultEqualToShieldThisTurn) {
    makeEngine();
    auto legend = setLegend(P1, kPride); setLegend(P2);
    auto unit = unitWithMight(P1, 3);
    engine_->testHook_executor().giveTemporaryKeyword(unit, Keyword::Shield, 2);
    s().player(P1).rune_pool.energy = 1;
    p1_.want = unit;
    activate(legend);
    EXPECT_TRUE(s().getObject(legend).is_exhausted);
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
    engine_->testHook_cleanup();
    auto assaultValue = [&]() {
        int v = 0;
        for (const auto& ae : s().getObject(unit).aura_effects)
            if (ae.keyword == Keyword::Assault) v += ae.keyword_value;
        return v;
    };
    EXPECT_EQ(assaultValue(), 2);
    EXPECT_TRUE(s().getObject(unit).hasKeyword(Keyword::Assault));

    // It follows the Shield: more Shield, more Assault.
    engine_->testHook_executor().giveTemporaryKeyword(unit, Keyword::Shield, 1);
    engine_->testHook_cleanup();
    EXPECT_EQ(assaultValue(), 3);

    // "this turn" only.
    s().turn.turn_number += 1;
    engine_->testHook_cleanup();
    EXPECT_EQ(assaultValue(), 0);
}

// ─── RAD-148 Ntofo Strikes ──────────────────────────────────────────────────

TEST_F(RadianceB10Test, NtofoStrikes_ShieldTwoThenDamageEqualToShield) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto ally = unitWithMight(P1, 2);
    engine_->testHook_executor().giveTemporaryKeyword(ally, Keyword::Shield, 1);
    auto enemy = unitWithMight(P2, 4, 0);
    s().player(P1).rune_pool.energy = 2;
    s().player(P1).rune_pool.universal_power = 1;
    auto spell = cardInHand(P1, kNtofo);
    p1_.wants = {ally, enemy};
    play(spell, std::nullopt);
    EXPECT_TRUE(s().getObject(ally).hasKeyword(Keyword::Shield));
    EXPECT_EQ(s().getObject(ally).shield_value, 3);
    ASSERT_FALSE(dead(enemy));
    EXPECT_EQ(s().getObject(enemy).damage_marked, 3) << "Shield 1 + Shield 2";
}

// ─── RAD-151 Starry-Eyed Songstress ─────────────────────────────────────────

TEST_F(RadianceB10Test, Songstress_ExhaustThreeUnitsForATwoEnergyDiscount) {
    makeEngine();
    auto legend = setLegend(P1, kSongstress); setLegend(P2);
    auto u1 = unitWithMight(P1, 1);
    auto u2 = unitWithMight(P1, 1);
    auto u3 = unitWithMight(P1, 1);
    EXPECT_FALSE(offersActivation(legend)) << "no spell in hand";
    auto spell = cardInHand(P1, kRealm);   // [2] + 1 power
    EXPECT_TRUE(offersActivation(legend));
    s().getObject(u3).is_exhausted = true;
    EXPECT_FALSE(offersActivation(legend)) << "only two ready units";
    s().getObject(u3).is_exhausted = false;
    s().player(P1).rune_pool.universal_power = 1;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, spell));
    p1_.wants = {u1, u2, u3};
    activate(legend);
    EXPECT_TRUE(s().getObject(legend).is_exhausted);
    for (auto u : {u1, u2, u3}) EXPECT_TRUE(s().getObject(u).is_exhausted);
    EXPECT_TRUE(engine_->testHook_canAfford(P1, spell)) << "[2] less: only the power is left";
    EXPECT_FALSE(offersActivation(legend));

    // Unused, the discount does not outlive the turn.
    s().turn.turn_number += 1;
    engine_->testHook_cleanup();
    EXPECT_FALSE(engine_->testHook_canAfford(P1, spell));
}

// ─── RAD-152 Encore ─────────────────────────────────────────────────────────

TEST_F(RadianceB10Test, Encore_DealTwoAndTwoRecruits) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto enemy = unitWithMight(P2, 2, 0);
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.universal_power = 1;
    auto spell = cardInHand(P1, kEncore);
    p1_.answers = {0};   // pair 0: deal 2 + Recruits
    p1_.want = enemy;
    play(spell, std::nullopt);
    EXPECT_TRUE(dead(enemy));
    int recruits = 0;
    for (auto& [id, o] : s().objects)
        if (o.isToken() && o.name == "Recruit" && o.controller == P1 && o.isAtBase()) {
            ++recruits;
            EXPECT_EQ(o.current_might, 1);
        }
    EXPECT_EQ(recruits, 2);
}

TEST_F(RadianceB10Test, Encore_BuffTwoAndLookAtTwo) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto u1 = unitWithMight(P1, 2);
    auto u2 = unitWithMight(P1, 3);
    auto bottom = deckCard(P1);
    auto second = deckCard(P1);
    auto top = deckCard(P1);
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.universal_power = 1;
    auto spell = cardInHand(P1, kEncore);
    p1_.answers = {5};   // pair 5: +1 [M] x2 + look at 2
    p1_.wants = {u1, u2, second};
    play(spell, std::nullopt);
    EXPECT_EQ(s().getObject(u1).current_might, 3);
    EXPECT_EQ(s().getObject(u2).current_might, 4);
    auto& ps = s().player(P1);
    EXPECT_EQ(std::count(ps.hand.begin(), ps.hand.end(), second), 1) << "drew the chosen card";
    EXPECT_EQ(std::count(ps.hand.begin(), ps.hand.end(), top), 0);
    ASSERT_EQ(ps.main_deck.size(), 2u);
    EXPECT_EQ(ps.main_deck.front(), top) << "the other one is recycled to the bottom";
    EXPECT_EQ(ps.main_deck.back(), bottom);
}

TEST_F(RadianceB10Test, Encore_DealModesNotOfferedWithoutUnits) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    deckCard(P1);
    deckCard(P1);
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.universal_power = 1;
    auto spell = cardInHand(P1, kEncore);
    p1_.answers = {4};   // Recruits + look
    play(spell, std::nullopt);
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{3, 4, 5}));
}

}  // namespace
}  // namespace riftbound::test
