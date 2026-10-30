/// @file test_human_replay_fidelity.cpp
/// Engine and card gaps found by replaying real RiftLite games through the
/// engine (lab/tools/human_review.py, the first desync of a game):
///   - [Temporary] was set on cards that only NAME the keyword (Lillia, Fae
///     Fawn; LeBlanc, Everywhere at Once; Petal Pixie; Sprite Mother; ...):
///     they died at their controller's Beginning Phase like a Sprite token;
///   - a showdown offered [Action] activated abilities but not [Reaction]
///     ones, though Reaction grants every permission of Action (CR 813.1.b):
///     Kai'Sa's legend and Gold tokens could not be used while holding focus;
///   - recycling a READY rune for power threw its Energy away: a player
///     exhausts it for [1] first and then recycles it (CR 164.2.a/b);
///   - abilities that [Add] resources went on the chain and passed priority,
///     so a spell played "in response" was paid before the [A] arrived;
///     they finalize and resolve at once and focus does not pass (CR 430.2).

#include "tests/cards/card_test_fixture.h"

#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace riftbound::test {
namespace {

constexpr CardDefId kLilliaFaeFawn     = 644;
constexpr CardDefId kLeBlancEverywhere = 652;
constexpr CardDefId kPetalPixie        = 638;
constexpr CardDefId kSpriteMother      = 106;
constexpr CardDefId kSpriteQueen       = 646;
constexpr CardDefId kTrevor            = 610;
constexpr CardDefId kSpriteToken       = 569;
constexpr CardDefId kKeeperOfMasks     = 643;
constexpr CardDefId kSpriteFountain    = 640;
constexpr CardDefId kDaughterOfTheVoid = 294;   // [E]: [Reaction] — [Add] [A]
constexpr CardDefId kGold              = 326;   // Kill this, [E]: [Reaction] — [Add] [A]
constexpr CardDefId kSealOfDiscord     = 204;   // gear, [P] (0 Energy + 1 Chaos power)
constexpr CardDefId kOtterpus          = 840;   // [2] unit, no text
constexpr CardDefId kSmokeAndMirrors   = 645;

/// Passes priority when it can, otherwise takes the first option (or, with
/// `last_mode`, the last of a yes/no or modal prompt).
class PassingAgent : public AgentInterface {
public:
    bool last_mode = false;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        if (last_mode && !legal.empty() && legal.front().type == IntentType::MakeChoice &&
            legal.front().chosen_objects.empty())
            return legal.back();
        return legal.empty() ? Intent{} : legal.front();
    }
};

class ReplayFidelityTest : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    PassingAgent p1_, p2_;
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
    int runesOnBoard(PlayerId p) {
        int n = 0;
        for (const auto& [id, o] : s().objects)
            if (o.isRune() && o.controller == p && o.location.has_value()) ++n;
        return n;
    }
    Intent activate(PlayerId p, GameObjectId source, IntentType type = IntentType::ActivateAbility) {
        Intent i;
        i.type = type;
        i.player = p;
        i.ability_source = source;
        i.ability_index = 0;
        return i;
    }
};

// ── [Temporary] belongs to the cards that have it, not the ones naming it ──

TEST_F(ReplayFidelityTest, Temporary_OnlyOnCardsThatHaveTheKeyword) {
    for (CardDefId id : {kLilliaFaeFawn, kLeBlancEverywhere, kPetalPixie, kSpriteMother,
                         kSpriteQueen, kTrevor})
        EXPECT_FALSE(card_db.get(id).keywords.has(Keyword::Temporary)) << card_db.get(id).name;
    for (CardDefId id : {kSpriteToken, kKeeperOfMasks, kSpriteFountain})
        EXPECT_TRUE(card_db.get(id).keywords.has(Keyword::Temporary)) << card_db.get(id).name;
}

TEST_F(ReplayFidelityTest, Temporary_LilliaAndLeBlancSurviveTheirBeginningPhase) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto lillia = boardObject(P1, kLilliaFaeFawn);
    auto leblanc = boardObject(P1, kLeBlancEverywhere, 0);
    auto pixie = boardObject(P1, kPetalPixie, 0);
    auto sprite = boardObject(P1, kSpriteToken, 0);

    engine_->testHook_beginningStep();

    EXPECT_TRUE(s().objectExists(lillia) && s().getObject(lillia).location.has_value());
    EXPECT_TRUE(s().objectExists(leblanc) && s().getObject(leblanc).location.has_value());
    EXPECT_TRUE(s().objectExists(pixie) && s().getObject(pixie).location.has_value());
    EXPECT_FALSE(s().objectExists(sprite) && s().getObject(sprite).location.has_value())
        << "the Sprite is [Temporary] and dies";
}

// ── Keywords a card gives conditionally or to others are not its own ──

TEST_F(ReplayFidelityTest, Keywords_ConditionalOrGivenAwayAreNotStatic) {
    constexpr CardDefId kFioraVictorious = 232, kCarrionDredger = 715, kYuumi = 618,
                        kChakramDancer = 633, kMasterYiTempered = 675;
    const auto& fiora = card_db.get(kFioraVictorious);
    EXPECT_FALSE(fiora.keywords.has(Keyword::Deflect)) << "only while Mighty";
    EXPECT_FALSE(fiora.keywords.has(Keyword::Ganking)) << "only while Mighty";
    EXPECT_FALSE(fiora.keywords.has(Keyword::Shield)) << "only while Mighty";
    EXPECT_EQ(fiora.shield_value, 0);
    EXPECT_EQ(fiora.deflect_value, 0);
    EXPECT_FALSE(card_db.get(kCarrionDredger).keywords.has(Keyword::Deflect)) << "its Bird token has it";
    EXPECT_EQ(card_db.get(kCarrionDredger).deflect_value, 0);
    EXPECT_FALSE(card_db.get(kYuumi).keywords.has(Keyword::Tank)) << "she gives it to another unit";
    EXPECT_FALSE(card_db.get(kChakramDancer).keywords.has(Keyword::Shield)) << "it gives it to other units";
    EXPECT_FALSE(card_db.get(kMasterYiTempered).keywords.has(Keyword::Deflect)) << "only at Level 6";
}

TEST_F(ReplayFidelityTest, FioraVictorious_HasDeflectOnlyWhileMighty) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto fiora = boardObject(P1, 232);
    engine_->testHook_cleanup();
    EXPECT_EQ(s().getObject(fiora).current_might, 4);
    EXPECT_FALSE(s().getObject(fiora).hasKeyword(Keyword::Deflect))
        << "a 4-Might Fiora can be chosen by Falling Star without paying [A]";
    EXPECT_FALSE(s().getObject(fiora).hasKeyword(Keyword::Ganking));
    EXPECT_EQ(deflectCostFor(s(), P2, fiora), 0);

    s().getObject(fiora).buff_count = 1;    // +1 [M] buff: Mighty
    engine_->testHook_cleanup();
    ASSERT_GE(s().getObject(fiora).current_might, 5);
    EXPECT_TRUE(s().getObject(fiora).hasKeyword(Keyword::Deflect));
    EXPECT_TRUE(s().getObject(fiora).hasKeyword(Keyword::Ganking));
    EXPECT_EQ(deflectCostFor(s(), P2, fiora), 1);
}

// ── A showdown offers [Reaction] activated abilities (CR 813.1.b) ──

TEST_F(ReplayFidelityTest, Showdown_OffersReactionAbilities) {
    makeEngine();
    auto legend = setLegend(P1, kDaughterOfTheVoid); setLegend(P2);
    auto gold = boardObject(P1, kGold);

    const auto legal = engine_->testHook_generateShowdownActions(P1);
    auto offered = [&](GameObjectId src) {
        return std::any_of(legal.begin(), legal.end(), [&](const Intent& i) {
            return i.ability_source == src &&
                   (i.type == IntentType::ActivateActionAbility ||
                    i.type == IntentType::ActivateReactionAbility ||
                    i.type == IntentType::ActivateAbility);
        });
    };
    EXPECT_TRUE(offered(legend)) << "Kai'Sa's [E]: [Reaction] — [Add] [A]";
    EXPECT_TRUE(offered(gold)) << "Gold's [Reaction] ability";
}

// ── A ready rune recycled for power is exhausted for its [1] first ──

TEST_F(ReplayFidelityTest, RecyclingAReadyRuneFloatsItsEnergy) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    rune(P1, Domain::Chaos);
    rune(P1, Domain::Chaos);
    auto seal = cardInHand(P1, kSealOfDiscord);
    auto otter = cardInHand(P1, kOtterpus);
    ASSERT_TRUE(engine_->testHook_canAfford(P1, seal));

    ASSERT_TRUE(engine_->testHook_payCardCost(P1, seal));   // [P]: recycle a ready Chaos rune

    EXPECT_EQ(runesOnBoard(P1), 1);
    EXPECT_EQ(s().player(P1).rune_pool.energy, 1) << "the recycled rune's [1] floats";
    EXPECT_TRUE(engine_->testHook_canAfford(P1, otter))
        << "1 floating Energy + 1 ready rune pay a [2] unit (the human's next play)";
}

TEST_F(ReplayFidelityTest, RecyclingAnExhaustedRuneAddsNoEnergy) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto spent = rune(P1, Domain::Chaos, /*exhausted=*/true);
    auto ready = rune(P1, Domain::Chaos);

    ASSERT_TRUE(engine_->testHook_payAdditionalCost(P1, 0, 1, Domain::Chaos));

    EXPECT_FALSE(s().getObject(spent).location.has_value()) << "the exhausted rune is recycled first";
    EXPECT_TRUE(s().getObject(ready).location.has_value());
    EXPECT_EQ(s().player(P1).rune_pool.energy, 0);
}

TEST_F(ReplayFidelityTest, AdditionalPowerFromAReadyRuneFloatsItsEnergy) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    rune(P1, Domain::Body);

    ASSERT_TRUE(engine_->testHook_payAdditionalCost(P1, 0, 1, Domain::Body));

    EXPECT_EQ(runesOnBoard(P1), 0);
    EXPECT_EQ(s().player(P1).rune_pool.energy, 1);
}

// ── [Add] abilities resolve at once: no chain item, focus stays (CR 430.2) ──

TEST_F(ReplayFidelityTest, AddAbility_ResolvesAtOnceInTheMainPhase) {
    makeEngine();
    auto legend = setLegend(P1, kDaughterOfTheVoid); setLegend(P2);

    engine_->testHook_executeIntent(activate(P1, legend));

    EXPECT_TRUE(s().chain.items.empty()) << "no chain item, nothing to respond to";
    EXPECT_TRUE(s().getObject(legend).is_exhausted);
    EXPECT_EQ(s().player(P1).rune_pool.universal_power, 1)
        << "the [A] is in the pool before the next spell is paid";
}

TEST_F(ReplayFidelityTest, AddAbility_KeepsFocusAndPassesInAShowdown) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    s().turn.ns_state = NeutralShowdownState::Showdown;
    auto gold = boardObject(P1, kGold, -1);
    s().turn.players_passed_focus = {P2};
    int actions = 0;

    auto next = engine_->testHook_resolveShowdownDecision(
        0, activate(P1, gold, IntentType::ActivateActionAbility), P1, actions);

    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(*next, P1) << "focus does not pass (CR 430.2.a)";
    EXPECT_EQ(s().turn.players_passed_focus.count(P2), 1u);
    EXPECT_EQ(s().player(P1).rune_pool.universal_power, 1);
    EXPECT_FALSE(s().objectExists(gold) && s().getObject(gold).location.has_value())
        << "Gold is killed as its cost";
}

// ── Smoke and Mirrors chooses two FRIENDLY units (it was never offered) ──

TEST_F(ReplayFidelityTest, SmokeAndMirrors_IsOfferedAndSwapsTheTwoUnits) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto lillia = boardObject(P1, kLilliaFaeFawn, 0);
    auto sprite = boardObject(P1, kSpriteToken);        // [Temporary], in base
    auto sm = cardInHand(P1, kSmokeAndMirrors);
    auto top = s().createObject();
    s().getObject(top).owner = s().getObject(top).controller = P1;
    s().getObject(top).zone = ZoneType::MainDeck;
    s().player(P1).main_deck.push_back(top);
    s().player(P1).rune_pool.energy = 2;

    const auto legal = engine_->testHook_generateShowdownActions(P1);
    ASSERT_TRUE(std::any_of(legal.begin(), legal.end(), [&](const Intent& i) { return i.card == sm; }))
        << "[Action] spell with two friendly units at different locations";

    Intent play;
    play.type = IntentType::PlayCard;
    play.player = P1;
    play.card = sm;
    engine_->testHook_executeIntent(play);
    engine_->testHook_runChain();

    EXPECT_TRUE(s().getObject(lillia).isAtBase()) << "Lillia escapes to the Sprite's place";
    EXPECT_EQ(s().getObject(sprite).battlefieldId(), std::optional<BattlefieldId>(0));
    EXPECT_EQ(std::count(s().player(P1).hand.begin(), s().player(P1).hand.end(), top), 1) << "Draw 1";
}

TEST_F(ReplayFidelityTest, SmokeAndMirrors_NotOfferedWithoutASecondLocation) {
    makeEngine();
    setLegend(P1); setLegend(P2);
    boardObject(P1, kLilliaFaeFawn, 0);
    boardObject(P1, kSpriteToken, 0);                   // same battlefield
    auto sm = cardInHand(P1, kSmokeAndMirrors);
    s().player(P1).rune_pool.energy = 2;

    const auto legal = engine_->testHook_generateShowdownActions(P1);
    EXPECT_FALSE(std::any_of(legal.begin(), legal.end(), [&](const Intent& i) { return i.card == sm; }));
}

// ── Singularity: "Deal 6 to each of up to two units" — any two units ──

TEST_F(ReplayFidelityTest, Singularity_HitsTwoEnemyUnitsWithNoUnitOfItsOwn) {
    constexpr CardDefId kSingularity = 105;
    makeEngine();
    setLegend(P1); setLegend(P2);
    auto e1 = boardObject(P2, kOtterpus, 0);
    auto e2 = boardObject(P2, kOtterpus, 1);
    auto sing = cardInHand(P1, kSingularity);
    s().player(P1).rune_pool.energy = 6;
    s().player(P1).rune_pool.universal_power = 2;

    const auto legal = engine_->testHook_generateShowdownActions(P1);
    ASSERT_TRUE(std::any_of(legal.begin(), legal.end(), [&](const Intent& i) { return i.card == sing; }))
        << "two enemy units are legal targets; no friendly unit is needed";

    p1_.last_mode = true;                         // "Two units"
    Intent play;
    play.type = IntentType::PlayCard;
    play.player = P1;
    play.card = sing;
    engine_->testHook_executeIntent(play);
    engine_->testHook_runChain();

    for (auto e : {e1, e2})
        EXPECT_FALSE(s().objectExists(e) && s().getObject(e).location.has_value())
            << "both 2-Might enemy units take 6 and die";
}

}  // namespace
}  // namespace riftbound::test
