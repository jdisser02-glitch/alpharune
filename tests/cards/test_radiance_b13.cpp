/// @file test_radiance_b13.cpp
/// Radiance batch 13 (card text reconstructed from web-search summaries):
/// Bloodscent Swarm ([2] less per battlefield conquered this turn), Seraphic
/// Wyvern (channel 1 extra exhausted rune), Explosimian (hold: three Bombs to
/// one battlefield), Gutter Guardian ([Show Off] a [Hidden] card: [Add] [A]),
/// Pitiless Stalker ([E]: stun an enemy unit), Angler Wyrm ([E]: the opponent
/// moves a unit to my battlefield), Protector's Shield (Quick-Draw / Equip
/// [1], +1 [M] and [Tank]), Burn Away (banish a small unit or a gear, Burn 2),
/// Raucous Anthem (ready your units, +3 [M]) and Remember the Fallen (play up
/// to 3 cheap units / gear from your trash).

#include "tests/cards/card_test_fixture.h"
#include "cards/card_helpers.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

namespace riftbound::test {
namespace {

constexpr CardDefId kSwarm       = 1118;
constexpr CardDefId kWyvern      = 1119;
constexpr CardDefId kExplosimian = 1120;
constexpr CardDefId kGuardian    = 1121;
constexpr CardDefId kStalker     = 1122;
constexpr CardDefId kAngler      = 1123;
constexpr CardDefId kShield      = 1124;
constexpr CardDefId kBurnAway    = 1125;
constexpr CardDefId kAnthem      = 1126;
constexpr CardDefId kRemember    = 1127;
constexpr CardDefId kOtterpus    = 840;  // 2-energy 2-Might unit, no play or exhaust text
constexpr CardDefId kScorcher    = 1;    // 5-energy 5-Might unit ([Accelerate])
constexpr CardDefId kBlade       = 962;  // gear, Energy 3 / Power 1 (Equip; no triggers)
constexpr CardDefId kSwitcheroo  = 466;  // spell with [Hidden]

/// Answers int prompts (yes/no, modes, destinations) from `answers`; object
/// prompts from `picks` (a pick is consumed only when it is offered), else
/// `want`, else a "Done" option (value -1) when there is one, else the first
/// option. Passes priority otherwise. Records every prompt it was shown.
class B13Agent : public AgentInterface {
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
            bool has_obj = false;
            for (const auto& i : legal) if (!i.chosen_objects.empty()) has_obj = true;
            if (has_obj) {
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
                for (const auto& i : legal)
                    if (i.chosen_objects.empty() && i.chosen_value == std::optional<int>{-1}) return i;
                return f;
            }
            if (f.chosen_value.has_value()) {
                std::vector<int> offered;
                for (const auto& i : legal) offered.push_back(i.chosen_value.value_or(-1));
                int_prompts.push_back(offered);
                if (next < answers.size()) {
                    int a = answers[next++];
                    for (const auto& i : legal) if (i.chosen_value == a) return i;
                }
                return f;
            }
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class RadianceB13Test : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    B13Agent p1_, p2_;
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
        c.deflect_value = def.deflect_value;
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
    GameObjectId trashCard(PlayerId p, CardDefId def_id) {
        auto id = s().createObject();
        auto& c = s().getObject(id);
        stamp(c, p, def_id);
        c.zone = ZoneType::Trash;
        s().player(p).trash.push_back(id);
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
    /// Puts a rune on top of `p`'s Rune Deck.
    GameObjectId deckRune(PlayerId p, Domain d = Domain::Body) {
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
    int baseRunes(PlayerId p, bool exhausted_only = false) {
        int n = 0;
        for (auto& [id, o] : s().objects)
            if (o.isRune() && o.controller == p && o.location.has_value() &&
                (!exhausted_only || o.is_exhausted))
                ++n;
        return n;
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
        engine_->testHook_runChain();
    }
    void activate(GameObjectId source, int index = 0) {
        Intent i;
        i.type = IntentType::ActivateAbility;
        i.player = P1;
        i.ability_source = source;
        i.ability_index = index;
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
    bool offersActivation(GameObjectId source) {
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::ActivateAbility && a.ability_source == source) return true;
        return false;
    }
    std::vector<std::vector<GameObjectId>> equipTargets(GameObjectId gear) {
        std::vector<std::vector<GameObjectId>> out;
        for (const auto& a : engine_->generateLegalActions())
            if (a.type == IntentType::ActivateAbility && a.ability_source == gear)
                out.push_back(a.targets);
        return out;
    }
    std::vector<GameObjectId> bombsOf(PlayerId p) {
        std::vector<GameObjectId> out;
        for (auto& [id, o] : s().objects)
            if (o.controller == p && o.isGear() && o.isToken() && o.name == "Bomb" &&
                o.location.has_value())
                out.push_back(id);
        return out;
    }
    bool inTrash(PlayerId p, GameObjectId id) {
        const auto& t = s().player(p).trash;
        return std::find(t.begin(), t.end(), id) != t.end();
    }
    void controlBf(BattlefieldId bf, PlayerId p) { s().battlefields[bf].controller = p; }
    static bool onBf(const GameObject& o, BattlefieldId bf) {
        return o.battlefieldId() == std::optional<BattlefieldId>{bf};
    }
    static bool contains(const std::vector<GameObjectId>& v, GameObjectId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
};

// ─── Card data ──────────────────────────────────────────────────────────────

TEST_F(RadianceB13Test, Data_CostsKeywordsAndTypes) {
    EXPECT_EQ(card_db.get(kSwarm).energy_cost, 8);
    EXPECT_EQ(card_db.get(kSwarm).power_cost, 2);
    EXPECT_EQ(card_db.get(kWyvern).might, 7);
    EXPECT_EQ(card_db.get(kWyvern).power_cost, 0) << "unknown in the sources: 0";
    EXPECT_TRUE(card_db.get(kGuardian).keywords.has(Keyword::Hidden));
    EXPECT_TRUE(card_db.get(kAngler).keywords.has(Keyword::Deflect));
    EXPECT_EQ(card_db.get(kAngler).deflect_value, 1);
    EXPECT_EQ(card_db.get(kShield).card_type, CardType::Gear);
    EXPECT_TRUE(card_db.get(kShield).keywords.has(Keyword::QuickDraw));
    EXPECT_TRUE(card_db.get(kShield).keywords.has(Keyword::Reaction));
    EXPECT_TRUE(card_db.get(kShield).keywords.has(Keyword::Equip));
    EXPECT_TRUE(card_db.get(kShield).hasTag("Equipment"));
    EXPECT_EQ(card_db.get(kShield).might_bonus, 1);
    EXPECT_EQ(card_db.get(kBurnAway).card_type, CardType::Spell);
    EXPECT_EQ(card_db.get(kAnthem).energy_cost, 7);
    EXPECT_EQ(card_db.get(kRemember).power_cost, 2);
}

// ─── RAD-080 Bloodscent Swarm ───────────────────────────────────────────────

TEST_F(RadianceB13Test, Swarm_CostsTwoLessPerBattlefieldConqueredThisTurn) {
    makeEngine();
    auto swarm = cardInHand(P1, kSwarm);
    s().player(P1).rune_pool.energy = 4;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 2;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, swarm)) << "8 Energy with no conquest";

    s().battlefields[0].conquered_on_turn = 5;
    s().battlefields[0].conquered_by_player = P1;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, swarm)) << "one conquest: 6 Energy";

    s().battlefields[1].conquered_on_turn = 5;
    s().battlefields[1].conquered_by_player = P1;
    EXPECT_TRUE(engine_->testHook_canAfford(P1, swarm)) << "two conquests: 4 Energy";
}

TEST_F(RadianceB13Test, Swarm_IgnoresOldAndOpponentConquests) {
    makeEngine();
    auto swarm = cardInHand(P1, kSwarm);
    s().player(P1).rune_pool.energy = 6;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Body)] = 2;
    s().battlefields[0].conquered_on_turn = 4;   // last turn
    s().battlefields[0].conquered_by_player = P1;
    s().battlefields[1].conquered_on_turn = 5;
    s().battlefields[1].conquered_by_player = P2;
    EXPECT_FALSE(engine_->testHook_canAfford(P1, swarm));
}

// ─── RAD-092 Seraphic Wyvern ────────────────────────────────────────────────

TEST_F(RadianceB13Test, Wyvern_ChannelGetsOneExtraExhaustedRune) {
    makeEngine();
    boardObject(P1, kWyvern);
    rune(P1, Domain::Body);
    rune(P1, Domain::Body);
    for (int i = 0; i < 4; ++i) deckRune(P1);
    engine_->testHook_cleanup();
    EXPECT_EQ(baseRunes(P1), 2) << "nothing channeled yet";

    engine_->testHook_executor().channelRunes(P1, 2);
    engine_->testHook_cleanup();
    EXPECT_EQ(baseRunes(P1), 5) << "2 channeled + 1 extra";
    EXPECT_EQ(baseRunes(P1, /*exhausted_only=*/true), 1) << "the extra rune is exhausted";
    EXPECT_EQ(s().player(P1).rune_deck.size(), 1u);

    engine_->testHook_cleanup();
    EXPECT_EQ(baseRunes(P1), 5) << "the extra rune is not itself a channel";
}

TEST_F(RadianceB13Test, Wyvern_NoExtraWithoutAChannelOrForTheOpponent) {
    makeEngine();
    boardObject(P1, kWyvern);
    auto r = rune(P1, Domain::Body);
    for (int i = 0; i < 3; ++i) deckRune(P1);
    for (int i = 0; i < 3; ++i) deckRune(P2);
    engine_->testHook_cleanup();

    // A rune leaving the base (recycled) is not a channel.
    auto& ro = s().getObject(r);
    ro.location = std::nullopt;
    ro.zone = ZoneType::RuneDeck;
    s().player(P1).rune_deck.insert(s().player(P1).rune_deck.begin(), r);
    engine_->testHook_cleanup();
    EXPECT_EQ(baseRunes(P1), 0);

    engine_->testHook_executor().channelRunes(P2, 2);
    engine_->testHook_cleanup();
    EXPECT_EQ(baseRunes(P2), 2) << "only its controller's channels are replaced";
    EXPECT_EQ(baseRunes(P1), 0);
}

TEST_F(RadianceB13Test, Wyvern_TwoWyvernsAddOneEach) {
    makeEngine();
    boardObject(P1, kWyvern);
    boardObject(P1, kWyvern);
    for (int i = 0; i < 5; ++i) deckRune(P1);
    engine_->testHook_cleanup();
    engine_->testHook_executor().channelRunes(P1, 1);
    engine_->testHook_cleanup();
    engine_->testHook_cleanup();
    EXPECT_EQ(baseRunes(P1), 3) << "1 channeled + 1 per Wyvern";
    EXPECT_EQ(baseRunes(P1, true), 2);
}

// ─── RAD-094 Explosimian ────────────────────────────────────────────────────

TEST_F(RadianceB13Test, Explosimian_HoldPlaysThreeBombsToOneBattlefield) {
    makeEngine();
    boardObject(P1, kExplosimian, 0);
    p1_.answers = {1};   // battlefield 1
    engine_->testHook_scoreHold(P1, 0);
    engine_->testHook_runChain();

    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0, 1})) << "any battlefield";
    auto bombs = bombsOf(P1);
    ASSERT_EQ(bombs.size(), 3u);
    for (auto b : bombs) {
        const auto& bomb = s().getObject(b);
        EXPECT_FALSE(bomb.is_exhausted);
        // A Bomb bound to a registered Bomb class has [Deploy] and stays at
        // the chosen battlefield; an unbound one may have been recalled.
        const Card* c = bomb.card_def_id != kInvalidId ? card_registry.get(bomb.card_def_id) : nullptr;
        if (c && c->deploys()) EXPECT_TRUE(onBf(bomb, 1));
    }
}

TEST_F(RadianceB13Test, Explosimian_ConquerDoesNotTrigger) {
    makeEngine();
    boardObject(P1, kExplosimian, 0);
    engine_->testHook_scoreConquer(P1, 0);
    engine_->testHook_runChain();
    EXPECT_TRUE(bombsOf(P1).empty());
}

// ─── RAD-103 Gutter Guardian ────────────────────────────────────────────────

TEST_F(RadianceB13Test, Guardian_ShowingOffAHiddenCardAddsAnyPower) {
    makeEngine();
    cardInHand(P1, kSwitcheroo);   // a card with [Hidden] to show off
    auto guardian = cardInHand(P1, kGuardian);
    s().player(P1).rune_pool.energy = 3;
    play(guardian, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(guardian).isAtBase());
    EXPECT_EQ(s().player(P1).rune_pool.universal_power, 1);
}

TEST_F(RadianceB13Test, Guardian_NothingToShowOffAddsNothing) {
    makeEngine();
    cardInHand(P1, kOtterpus);     // no [Hidden]
    auto guardian = cardInHand(P1, kGuardian);
    s().player(P1).rune_pool.energy = 3;
    play(guardian, LocationId{BaseLocation{P1}});
    ASSERT_TRUE(s().getObject(guardian).isAtBase());
    EXPECT_EQ(s().player(P1).rune_pool.universal_power, 0) << "it can't show off itself";
}

// ─── RAD-104 Pitiless Stalker ───────────────────────────────────────────────

TEST_F(RadianceB13Test, Stalker_ExhaustsToStunAnEnemyUnit) {
    makeEngine();
    auto stalker = boardObject(P1, kStalker);
    auto friendly = boardObject(P1, kOtterpus);
    auto enemy = boardObject(P2, kOtterpus, 0);
    auto enemy2 = boardObject(P2, kOtterpus);
    ASSERT_TRUE(offersActivation(stalker));
    p1_.want = enemy;
    activate(stalker);
    EXPECT_TRUE(s().getObject(stalker).is_exhausted);
    EXPECT_TRUE(s().getObject(enemy).is_stunned);
    EXPECT_FALSE(s().getObject(enemy2).is_stunned);
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), friendly)) << "enemy units only";
    EXPECT_FALSE(offersActivation(stalker)) << "exhausted";
}

TEST_F(RadianceB13Test, Stalker_NotOfferedWithoutAnEnemyUnit) {
    makeEngine();
    auto stalker = boardObject(P1, kStalker);
    boardObject(P1, kOtterpus);
    EXPECT_FALSE(offersActivation(stalker));
}

// ─── RAD-113 Angler Wyrm ────────────────────────────────────────────────────

TEST_F(RadianceB13Test, Angler_OpponentMovesAUnitOfTheirChoiceToMyBattlefield) {
    makeEngine();
    auto wyrm = boardObject(P1, kAngler, 0);
    auto at_base = boardObject(P2, kOtterpus);
    auto at_other = boardObject(P2, kOtterpus, 1);
    auto already_here = boardObject(P2, kOtterpus, 0);
    ASSERT_TRUE(offersActivation(wyrm));
    p2_.want = at_other;
    activate(wyrm);
    EXPECT_TRUE(s().getObject(wyrm).is_exhausted);
    EXPECT_TRUE(onBf(s().getObject(at_other), 0)) << "the opponent's pick moved here";
    EXPECT_TRUE(s().getObject(at_base).isAtBase());
    ASSERT_FALSE(p2_.obj_prompts.empty()) << "the opponent chooses";
    EXPECT_FALSE(contains(p2_.obj_prompts.front(), already_here));
    EXPECT_TRUE(contains(p2_.obj_prompts.front(), at_base));
    EXPECT_TRUE(p1_.obj_prompts.empty());
}

TEST_F(RadianceB13Test, Angler_NotOfferedAtBase) {
    makeEngine();
    auto wyrm = boardObject(P1, kAngler);
    boardObject(P2, kOtterpus, 0);
    EXPECT_FALSE(offersActivation(wyrm));
}

// ─── RAD-123 Protector's Shield ─────────────────────────────────────────────

TEST_F(RadianceB13Test, Shield_QuickDrawAttachesAndGrantsTank) {
    makeEngine();
    auto unit = boardObject(P1, kOtterpus);
    auto shield = cardInHand(P1, kShield);
    s().player(P1).rune_pool.energy = 2;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 1;
    Intent qd;
    qd.type = IntentType::PlayReaction;
    qd.player = P1;
    qd.card = shield;
    qd.targets = {unit};
    engine_->testHook_executeIntent(qd);
    engine_->testHook_runChain();
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(shield).attached_to, std::optional<GameObjectId>{unit});
    EXPECT_TRUE(s().getObject(unit).hasKeyword(Keyword::Tank));
    EXPECT_EQ(s().getObject(unit).current_might, 3) << "+1 [M]";
}

TEST_F(RadianceB13Test, Shield_EquipForOne) {
    makeEngine();
    auto shield = boardObject(P1, kShield);
    auto unit = boardObject(P1, kOtterpus);
    EXPECT_TRUE(equipTargets(shield).empty()) << "no Energy to pay [1]";

    rune(P1, Domain::Order);
    EXPECT_EQ(equipTargets(shield),
              (std::vector<std::vector<GameObjectId>>{std::vector<GameObjectId>{unit}}));
    Intent equip;
    equip.type = IntentType::ActivateAbility;
    equip.player = P1;
    equip.ability_source = shield;
    equip.targets = {unit};
    engine_->testHook_executeIntent(equip);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(shield).attached_to, std::optional<GameObjectId>{unit});
    EXPECT_EQ(readyRunes(P1), 0) << "paid [1]";
    EXPECT_TRUE(s().getObject(unit).hasKeyword(Keyword::Tank));
}

// ─── RAD-124 Burn Away ──────────────────────────────────────────────────────

TEST_F(RadianceB13Test, BurnAway_BanishesASmallUnitAndBurnsTwo) {
    makeEngine();
    auto small = boardObject(P2, kOtterpus, 0);
    auto big = boardObject(P2, kScorcher, 0);
    auto gear = boardObject(P2, kBlade);
    auto bottom = deckCard(P1, kOtterpus);
    auto d2 = deckCard(P1, kOtterpus);
    auto d1 = deckCard(P1, kOtterpus);   // top
    auto spell = cardInHand(P1, kBurnAway);
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 1;
    p1_.want = small;

    play(spell);

    EXPECT_EQ(s().getObject(small).zone, ZoneType::Banishment);
    EXPECT_FALSE(s().getObject(small).location.has_value());
    EXPECT_TRUE(s().getObject(big).location.has_value());
    EXPECT_TRUE(s().getObject(gear).location.has_value());
    EXPECT_TRUE(inTrash(P1, d1));
    EXPECT_TRUE(inTrash(P1, d2));
    EXPECT_FALSE(inTrash(P1, bottom));
    ASSERT_EQ(s().player(P1).main_deck.size(), 1u);
    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), big)) << "5 [M] is too much";
    EXPECT_TRUE(contains(p1_.obj_prompts.front(), gear)) << "any gear";
}

TEST_F(RadianceB13Test, BurnAway_BanishesAGear) {
    makeEngine();
    auto unit = boardObject(P2, kOtterpus, 0);
    auto gear = boardObject(P2, kBlade);
    deckCard(P1, kOtterpus);
    deckCard(P1, kOtterpus);
    auto spell = cardInHand(P1, kBurnAway);
    s().player(P1).rune_pool.energy = 3;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 1;
    p1_.want = gear;

    play(spell);

    EXPECT_EQ(s().getObject(gear).zone, ZoneType::Banishment);
    EXPECT_TRUE(s().getObject(unit).location.has_value());
    EXPECT_TRUE(s().player(P1).main_deck.empty());
}

// ─── RAD-134 Raucous Anthem ─────────────────────────────────────────────────

TEST_F(RadianceB13Test, Anthem_ReadiesYourUnitsAndGivesThemThree) {
    makeEngine();
    auto a = boardObject(P1, kOtterpus);
    auto b = boardObject(P1, kOtterpus, 0);
    auto enemy = boardObject(P2, kOtterpus, 1);
    s().getObject(a).is_exhausted = true;
    s().getObject(b).is_exhausted = true;
    s().getObject(enemy).is_exhausted = true;
    auto spell = cardInHand(P1, kAnthem);
    s().player(P1).rune_pool.energy = 7;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 1;

    play(spell);
    engine_->testHook_cleanup();

    EXPECT_FALSE(s().getObject(a).is_exhausted);
    EXPECT_FALSE(s().getObject(b).is_exhausted);
    EXPECT_EQ(s().getObject(a).current_might, 5);
    EXPECT_EQ(s().getObject(b).current_might, 5);
    EXPECT_TRUE(s().getObject(enemy).is_exhausted);
    EXPECT_EQ(s().getObject(enemy).current_might, 2);
}

// ─── RAD-135 Remember the Fallen ────────────────────────────────────────────

TEST_F(RadianceB13Test, Remember_PlaysChosenCheapCardsFromTrashThenStops) {
    makeEngine();
    controlBf(0, P1);
    boardObject(P1, kOtterpus, 0);   // so P1 really controls battlefield 0
    auto otter = trashCard(P1, kOtterpus);
    auto blade = trashCard(P1, kBlade);
    auto scorcher = trashCard(P1, kScorcher);   // 5 Energy: not allowed
    auto otter2 = trashCard(P1, kOtterpus);
    auto spell = cardInHand(P1, kRemember);
    s().player(P1).rune_pool.energy = 8;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 2;
    p1_.picks = {otter, blade};
    p1_.answers = {1};   // the Otterpus to battlefield 0

    play(spell);

    ASSERT_FALSE(p1_.obj_prompts.empty());
    EXPECT_FALSE(contains(p1_.obj_prompts.front(), scorcher));
    EXPECT_TRUE(contains(p1_.obj_prompts.front(), otter2));
    EXPECT_EQ(p1_.obj_prompts.size(), 3u) << "third prompt answered Done";
    ASSERT_FALSE(p1_.int_prompts.empty());
    EXPECT_EQ(p1_.int_prompts.front(), (std::vector<int>{0, 1})) << "base or the controlled battlefield";
    EXPECT_TRUE(onBf(s().getObject(otter), 0));
    EXPECT_TRUE(s().getObject(blade).isAtBase());
    EXPECT_TRUE(inTrash(P1, scorcher));
    EXPECT_TRUE(inTrash(P1, otter2));
    EXPECT_FALSE(inTrash(P1, otter));
    EXPECT_FALSE(inTrash(P1, blade));
}

TEST_F(RadianceB13Test, Remember_AtMostThree) {
    makeEngine();
    std::vector<GameObjectId> otters;
    for (int i = 0; i < 4; ++i) otters.push_back(trashCard(P1, kOtterpus));
    auto spell = cardInHand(P1, kRemember);
    s().player(P1).rune_pool.energy = 8;
    s().player(P1).rune_pool.power[static_cast<int>(Domain::Order)] = 2;
    p1_.picks = otters;

    play(spell);

    int played = 0;
    for (auto id : otters) if (s().getObject(id).isAtBase()) ++played;
    EXPECT_EQ(played, 3);
    EXPECT_TRUE(inTrash(P1, otters[3]));
    EXPECT_TRUE(p1_.int_prompts.empty()) << "no battlefield controlled: base without asking";
}

}  // namespace
}  // namespace riftbound::test
