/// @file test_radiance_b02.cpp
/// Radiance batch 02 (Calm): Iridescent Avian's buff-or-+2, Life from Stone's
/// exhaust-a-unit additional cost, Sanctum Conservator's ready on a [7]+ card,
/// Storm Raptor's draw on a stunned enemy's death, Watchful Curator's gear
/// [Deflect], Ahri, Confident's [Disarm] and self-pump, Ephemeral Ward's +2
/// aura and self-kill, Heartening Soul's legend ready and granted ability,
/// K'Sante, Dauntless's Shield doubling and Resplendent Rostrum's draw/channel
/// and exhaust pump.

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kAvian       = 1005;
constexpr CardDefId kLifeStone   = 1006;
constexpr CardDefId kConservator = 1007;
constexpr CardDefId kRaptor      = 1008;
constexpr CardDefId kCurator     = 1009;
constexpr CardDefId kAhri        = 1010;
constexpr CardDefId kWard        = 1011;
constexpr CardDefId kSoul        = 1012;
constexpr CardDefId kKSante      = 1013;
constexpr CardDefId kRostrum     = 1014;
constexpr CardDefId kTheExile    = 961;  // a legend with no activated ability
constexpr CardDefId kLeona       = 238;  // Leona, Determined: [Shield] unit, 4 [M]
constexpr CardDefId kOtterpus    = 840;  // 2-energy 2-Might unit, no play or exhaust text

/// Answers int prompts from `answers`, object prompts from `picks` (a pick is
/// consumed only when offered; `want` is the fallback), passes priority
/// otherwise. Records every prompt it was shown.
class B02Agent : public AgentInterface {
public:
    std::vector<int> answers;
    size_t next = 0;
    std::vector<GameObjectId> picks;
    size_t pnext = 0;
    GameObjectId want = kInvalidId;
    std::vector<std::vector<int>> int_prompts;
    std::vector<std::vector<GameObjectId>> obj_prompts;

    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
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
                for (const auto& i : legal)
                    if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
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

class RadianceB02Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B02Agent p1_, p2_;
    GameState& s() { return engine_->mutableState(); }

    void makeEngine(CardDefId p1_legend = kInvalidId) {
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
        setLegend(P1, p1_legend);
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
        o.assault_value = def.assault_value;
        o.shield_value = def.shield_value;
        o.deflect_value = def.deflect_value;
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
    /// A blank card on TOP of `p`'s Main Deck (back = top).
    GameObjectId deckCard(PlayerId p) {
        auto top = s().createObject();
        s().getObject(top).owner = s().getObject(top).controller = p;
        s().getObject(top).zone = ZoneType::MainDeck;
        s().player(p).main_deck.push_back(top);
        return top;
    }
    GameObjectId runeInRuneDeck(PlayerId p, Domain d) {
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
    bool inHand(PlayerId p, GameObjectId id) {
        const auto& h = s().player(p).hand;
        return std::find(h.begin(), h.end(), id) != h.end();
    }
    void play(GameObjectId card, std::optional<LocationId> where = std::nullopt) {
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = card;
        i.play_location = where;
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
    bool offersPlay(GameObjectId card) {
        for (const auto& a : engine_->generateLegalActions())
            if ((a.type == IntentType::PlayCard || a.type == IntentType::PlayActionCard ||
                 a.type == IntentType::PlayReaction) && a.card == card)
                return true;
        return false;
    }
    void activate(GameObjectId source, CardDefId granted = 0) {
        Intent act;
        act.type = IntentType::ActivateAbility;
        act.player = P1;
        act.ability_source = source;
        act.ability_index = 0;
        act.granted_ability_def = granted;
        engine_->testHook_executeIntent(act);
        engine_->testHook_runChain();
    }
    std::vector<Intent> activationsOf(GameObjectId source) {
        std::vector<Intent> out;
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::ActivateAbility && a.ability_source == source)
                out.push_back(a);
        return out;
    }
    static bool contains(const std::vector<GameObjectId>& v, GameObjectId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
};

// ─── RAD-027 Iridescent Avian ───────────────────────────────────────────────

TEST_F(RadianceB02Test, Avian_BuffsAnUnbuffedUnit) {
    makeEngine();
    auto avian = boardObject(P1, kAvian);
    auto otter = boardObject(P1, kOtterpus);
    ASSERT_EQ(activationsOf(avian).size(), 1u) << "one intent; the unit is chosen on the chain";
    p1_.picks = {otter};
    activate(avian);
    EXPECT_TRUE(s().getObject(avian).is_exhausted) << "the cost: [E]";
    EXPECT_EQ(s().getObject(otter).buff_count, 1);
    EXPECT_EQ(s().getObject(otter).current_might, 3);
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_TRUE(contains(p1_.obj_prompts.front(), avian)) << "any unit, including itself";
}

TEST_F(RadianceB02Test, Avian_AlreadyBuffedUnitGetsPlusTwoThisTurnInstead) {
    makeEngine();
    auto avian = boardObject(P1, kAvian);
    auto enemy = boardObject(P2, kOtterpus, 0);
    s().getObject(enemy).buff_count = 1;
    s().getObject(enemy).recomputeMight();
    p1_.picks = {enemy};
    activate(avian);
    EXPECT_EQ(s().getObject(enemy).buff_count, 1) << "no second buff";
    EXPECT_EQ(s().getObject(enemy).temp_might_bonus, 2);
    EXPECT_EQ(s().getObject(enemy).current_might, 5) << "2 + 1 buff + 2 this turn";
}

// ─── RAD-029 Life from Stone ────────────────────────────────────────────────

TEST_F(RadianceB02Test, LifeFromStone_NeedsAReadyFriendlyUnitToPlay) {
    makeEngine();
    auto mine = boardObject(P1, kOtterpus);
    s().getObject(mine).is_exhausted = true;
    boardObject(P2, kOtterpus, 0);
    auto spell = cardInHand(P1, kLifeStone);
    s().player(P1).rune_pool.energy = 1;
    EXPECT_FALSE(offersPlay(spell)) << "no ready friendly unit to exhaust";
    s().getObject(mine).is_exhausted = false;
    EXPECT_TRUE(offersPlay(spell));
}

TEST_F(RadianceB02Test, LifeFromStone_ExhaustsTheChosenUnitDrawsAndChannelsExhausted) {
    makeEngine();
    auto payer = boardObject(P1, kOtterpus);
    auto other = boardObject(P1, kOtterpus);
    auto top = deckCard(P1);
    auto rune = runeInRuneDeck(P1, Domain::Calm);
    auto spell = cardInHand(P1, kLifeStone);
    s().player(P1).rune_pool.energy = 1;
    p1_.picks = {payer};
    play(spell);
    EXPECT_TRUE(s().getObject(payer).is_exhausted) << "the additional cost";
    EXPECT_FALSE(s().getObject(other).is_exhausted) << "only the chosen unit";
    EXPECT_TRUE(inHand(P1, top)) << "draw 1";
    ASSERT_TRUE(s().getObject(rune).location.has_value()) << "channel 1 rune";
    EXPECT_TRUE(s().getObject(rune).is_exhausted) << "channeled exhausted";
    EXPECT_EQ(s().getObject(spell).zone, ZoneType::Trash);
}

TEST_F(RadianceB02Test, LifeFromStone_HasReaction) {
    makeEngine();
    EXPECT_TRUE(card_db.get(kLifeStone).keywords.has(Keyword::Reaction));
}

// ─── RAD-030 Sanctum Conservator ────────────────────────────────────────────

TEST_F(RadianceB02Test, Conservator_ReadiesWhenYouPlayASevenCostCard) {
    makeEngine(kTheExile);
    auto cons = boardObject(P1, kConservator);
    s().getObject(cons).is_exhausted = true;
    auto soul = cardInHand(P1, kSoul);   // Energy cost 7
    s().player(P1).rune_pool.energy = 7;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Calm)] = 1;
    play(soul, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(soul).location.has_value());
    EXPECT_FALSE(s().getObject(cons).is_exhausted);
}

TEST_F(RadianceB02Test, Conservator_StaysExhaustedForACheaperCard) {
    makeEngine();
    auto cons = boardObject(P1, kConservator);
    s().getObject(cons).is_exhausted = true;
    auto otter = cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 2;
    play(otter, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(otter).location.has_value());
    EXPECT_TRUE(s().getObject(cons).is_exhausted);
}

// ─── RAD-036 Storm Raptor ───────────────────────────────────────────────────

TEST_F(RadianceB02Test, Raptor_DrawsWhenAStunnedEnemyDiesHere) {
    makeEngine();
    boardObject(P1, kRaptor, 0);
    auto enemy = boardObject(P2, kOtterpus, 0);
    s().getObject(enemy).is_stunned = true;
    auto top = deckCard(P1);
    engine_->testHook_executor().killObject(enemy);
    engine_->testHook_runChain();
    EXPECT_TRUE(inHand(P1, top));
}

TEST_F(RadianceB02Test, Raptor_NoDrawForAnUnstunnedEnemyOrOneElsewhere) {
    makeEngine();
    boardObject(P1, kRaptor, 0);
    auto plain = boardObject(P2, kOtterpus, 0);
    auto far = boardObject(P2, kOtterpus, 1);
    s().getObject(far).is_stunned = true;
    auto top = deckCard(P1);
    engine_->testHook_executor().killObject(plain);
    engine_->testHook_executor().killObject(far);
    engine_->testHook_runChain();
    EXPECT_FALSE(inHand(P1, top));
}

TEST_F(RadianceB02Test, Raptor_HasDeflect) {
    makeEngine();
    auto raptor = boardObject(P1, kRaptor, 0);
    EXPECT_EQ(deflectCostFor(s(), P2, raptor), 1);
}

// ─── RAD-037 Watchful Curator ───────────────────────────────────────────────

TEST_F(RadianceB02Test, Curator_GivesYourGearDeflect) {
    makeEngine();
    auto curator = boardObject(P1, kCurator);
    auto my_gear = boardObject(P1, kRostrum);
    auto their_gear = boardObject(P2, kRostrum);
    engine_->testHook_cleanup();
    EXPECT_TRUE(s().getObject(my_gear).hasKeyword(Keyword::Deflect));
    EXPECT_EQ(deflectCostFor(s(), P2, my_gear), 1);
    EXPECT_EQ(deflectCostFor(s(), P1, their_gear), 0) << "only your gear";
    EXPECT_EQ(deflectCostFor(s(), P2, curator), 0) << "gear only, not units";

    // Two Curators: granted Deflect values are summed (CR 809.2).
    boardObject(P1, kCurator);
    engine_->testHook_cleanup();
    EXPECT_EQ(deflectCostFor(s(), P2, my_gear), 2);
}

// ─── RAD-038 Ahri, Confident ────────────────────────────────────────────────

TEST_F(RadianceB02Test, Ahri_DisarmGivesAnEnemyMinusOneAndHerPlusOne) {
    makeEngine();
    auto ahri = boardObject(P1, kAhri, 0);
    auto e1 = boardObject(P2, kOtterpus, 0);
    auto e2 = boardObject(P2, kOtterpus, 0);
    boardObject(P2, kOtterpus, 1);
    p1_.picks = {e2};
    events.emit(CombatStartedEvent{0, /*attacker=*/P1, /*defender=*/P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(e2).current_might, 1) << "[Disarm]: -1 this turn";
    EXPECT_EQ(s().getObject(e1).current_might, 2);
    EXPECT_EQ(s().getObject(ahri).current_might, 5) << "reduced an enemy's Might here: +1";
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_EQ(p1_.obj_prompts.front().size(), 2u) << "only enemy units here";
}

TEST_F(RadianceB02Test, Ahri_NoBonusWhenTheDisarmReducesNothing) {
    makeEngine();
    auto ahri = boardObject(P1, kAhri, 0);
    auto e = boardObject(P2, kOtterpus, 0);
    s().getObject(e).temp_might_bonus = -2;   // already at 0 Might
    s().getObject(e).recomputeMight();
    p1_.picks = {e};
    events.emit(CombatStartedEvent{0, /*attacker=*/P1, /*defender=*/P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(ahri).current_might, 4);
}

TEST_F(RadianceB02Test, Ahri_DefendingDoesNotDisarm) {
    makeEngine();
    auto ahri = boardObject(P1, kAhri, 0);
    auto e = boardObject(P2, kOtterpus, 0);
    events.emit(CombatStartedEvent{0, /*attacker=*/P2, /*defender=*/P1});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(e).current_might, 2);
    EXPECT_EQ(s().getObject(ahri).current_might, 4);
}

// ─── RAD-039 Ephemeral Ward ─────────────────────────────────────────────────

TEST_F(RadianceB02Test, Ward_YourUnitsHavePlusTwo) {
    makeEngine();
    boardObject(P1, kWard);
    auto mine = boardObject(P1, kOtterpus, 0);
    auto theirs = boardObject(P2, kOtterpus, 0);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(mine).current_might, 4);
    EXPECT_EQ(s().getObject(theirs).current_might, 2);
}

TEST_F(RadianceB02Test, Ward_DiesWhenYouPlayAUnit) {
    makeEngine();
    auto ward = boardObject(P1, kWard);
    auto mine = boardObject(P1, kOtterpus, 0);
    engine_->testHook_cleanup();
    ASSERT_EQ(s().getObject(mine).current_might, 4);
    auto otter = cardInHand(P1, kOtterpus);
    s().player(P1).rune_pool.energy = 2;
    play(otter, LocationId{BaseLocation{P1}});
    engine_->testHook_cleanup();
    EXPECT_FALSE(s().getObject(ward).location.has_value()) << "killed";
    EXPECT_EQ(s().getObject(mine).current_might, 2) << "the aura is gone";
}

// ─── RAD-040 Heartening Soul ────────────────────────────────────────────────

TEST_F(RadianceB02Test, Soul_PlayReadiesYourLegend) {
    makeEngine(kTheExile);
    const auto legend = s().player(P1).legend_zone;
    s().getObject(legend).is_exhausted = true;
    auto soul = cardInHand(P1, kSoul);
    s().player(P1).rune_pool.energy = 7;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Calm)] = 1;
    play(soul, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(soul).location.has_value());
    EXPECT_FALSE(s().getObject(legend).is_exhausted);
}

TEST_F(RadianceB02Test, Soul_FriendlyLegendHasTheGrantedAbility) {
    makeEngine(kTheExile);
    const auto legend = s().player(P1).legend_zone;
    auto soul = boardObject(P1, kSoul);
    auto otter = boardObject(P2, kOtterpus, 0);
    engine_->testHook_cleanup();
    EXPECT_TRUE(activationsOf(soul).empty()) << "Heartening Soul itself has no such ability";
    const auto acts = activationsOf(legend);
    ASSERT_EQ(acts.size(), 1u);
    EXPECT_EQ(acts.front().granted_ability_def, kSoul);

    p1_.picks = {otter};
    activate(legend, kSoul);
    EXPECT_TRUE(s().getObject(legend).is_exhausted) << "the legend pays [E]";
    EXPECT_EQ(s().getObject(otter).current_might, 5) << "2 + 3 this turn";
    EXPECT_TRUE(activationsOf(legend).empty()) << "exhausted";
}

TEST_F(RadianceB02Test, Soul_NoGrantWhileNotOnTheBoard) {
    makeEngine(kTheExile);
    const auto legend = s().player(P1).legend_zone;
    cardInHand(P1, kSoul);
    boardObject(P1, kOtterpus);
    engine_->testHook_cleanup();
    EXPECT_TRUE(activationsOf(legend).empty());
}

// ─── RAD-045 K'Sante, Dauntless ─────────────────────────────────────────────

TEST_F(RadianceB02Test, KSante_DefendingDoublesHisShield) {
    makeEngine();
    auto ks = boardObject(P1, kKSante, 0);
    boardObject(P2, kOtterpus, 0);
    s().getObject(ks).combat_designation = CombatDesignation::Defender;
    s().getObject(ks).recomputeMight();
    ASSERT_EQ(s().getObject(ks).current_might, 5) << "4 + [Shield]";
    p1_.picks = {ks};
    events.emit(CombatStartedEvent{0, /*attacker=*/P2, /*defender=*/P1});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(ks).shield_value, 2);
    EXPECT_EQ(s().getObject(ks).temp_shield_value, 1) << "this turn";
    EXPECT_EQ(s().getObject(ks).current_might, 6) << "4 + [Shield 2]";
}

TEST_F(RadianceB02Test, KSante_AttackingDoublesAnotherShieldedUnitHere) {
    makeEngine();
    auto ks = boardObject(P1, kKSante, 0);
    auto leona = boardObject(P1, kLeona, 0);
    auto otter = boardObject(P1, kOtterpus, 0);
    auto far = boardObject(P1, kLeona, 1);
    boardObject(P2, kOtterpus, 0);
    p1_.picks = {leona};
    events.emit(CombatStartedEvent{0, /*attacker=*/P1, /*defender=*/P2});
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(leona).shield_value, 2);
    EXPECT_EQ(s().getObject(ks).shield_value, 1);
    // The prompt: units you control here that have [Shield].
    bool saw = false;
    for (const auto& prompt : p1_.obj_prompts) {
        if (!contains(prompt, ks)) continue;
        saw = true;
        EXPECT_TRUE(contains(prompt, leona));
        EXPECT_FALSE(contains(prompt, otter)) << "no [Shield] to double";
        EXPECT_FALSE(contains(prompt, far)) << "not here";
    }
    EXPECT_TRUE(saw);
}

// ─── RAD-046 Resplendent Rostrum ────────────────────────────────────────────

TEST_F(RadianceB02Test, Rostrum_ActivateDrawsChannelsAndPumpsAFriendlyUnit) {
    makeEngine();
    auto rostrum = boardObject(P1, kRostrum);
    auto otter = boardObject(P1, kOtterpus);
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto top = deckCard(P1);
    auto rune = runeInRuneDeck(P1, Domain::Calm);
    p1_.picks = {otter};
    activate(rostrum);
    EXPECT_TRUE(s().getObject(rostrum).is_exhausted);
    EXPECT_TRUE(inHand(P1, top)) << "draw 1";
    ASSERT_TRUE(s().getObject(rune).location.has_value());
    EXPECT_TRUE(s().getObject(rune).is_exhausted) << "channeled exhausted";
    EXPECT_EQ(s().getObject(otter).current_might, 5) << "became exhausted: +3 this turn";
    ASSERT_FALSE(p1_.obj_prompts.empty());
    for (const auto& prompt : p1_.obj_prompts)
        EXPECT_FALSE(contains(prompt, enemy)) << "friendly units only";
}

TEST_F(RadianceB02Test, Rostrum_ExhaustedByAnEffectAlsoPumps) {
    makeEngine();
    auto rostrum = boardObject(P1, kRostrum);
    auto otter = boardObject(P1, kOtterpus);
    p1_.picks = {otter};
    engine_->testHook_executor().exhaustObject(rostrum);
    engine_->testHook_runChain();
    EXPECT_EQ(s().getObject(otter).current_might, 5);
}

}  // namespace
}  // namespace riftbound::test
