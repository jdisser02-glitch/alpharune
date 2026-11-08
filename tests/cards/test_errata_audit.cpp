/// @file test_errata_audit.cpp
/// Card errata (errata/vendetta-errata.txt, errata/radiance-errata.txt,
/// errata/unleased.txt): every errata'd card carries its NEW TEXT, and the
/// behaviour changes the errata make are implemented.
///
///  * Relentless Pursuit (Spiritforged erratum): "You may attach UP TO ONE
///    Equipment" -- optional, and the player picks which; it used to attach
///    the first Equipment found without asking.
///  * Elnuk Herd (Radiance erratum, "a friendly unit"): tested in
///    test_radiance_batch_b.cpp (ElnukHerd_CanBuffItself).

#include "tests/cards/card_test_fixture.h"
#include "engine/game_engine.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace riftbound::test {
namespace {

constexpr CardDefId kRelentlessPursuit = 503;   // 2E 1P [Action] spell
constexpr CardDefId kDoransBlade       = 417;   // Equipment, +2 [M]
constexpr CardDefId kOtterpus          = 840;   // 2E 2M vanilla unit

// ─── Every errata'd card's ability text carries the new wording ─────────────

struct ErrataText {
    CardDefId id;
    const char* name;
    std::vector<const char*> must_contain;
    std::vector<const char*> must_not_contain;
};

TEST_F(CardTestFixture, Errata_AbilityTextMatchesNewText) {
    const std::vector<ErrataText> errata = {
        // errata/vendetta-errata.txt
        {343, "Draven, Vanquisher", {"When I attack or defend, you may pay [R] to give me +2 [M] this turn."},
         {"If you do"}},
        {521, "Emperor's Dais", {"return a unit you control here to its owner's hand to play a 2 [M] Sand Soldier unit token here."},
         {"If you do"}},
        {461, "Fizz, Trickster", {"ignoring its Energy cost. Then recycle it. (You must still pay its Power cost.)"},
         {"after you play it"}},
        {641, "Diana, Lunari", {"you may pay [1] to [Predict], then reveal the top card of your Main Deck."},
         {"If you do"}},
        {728, "Stalking Wolf", {"You may [Ambush] me to its battlefield, even if you don't have other units there."},
         {"You may play me to its battlefield"}},
        {831, "Astral Heron", {"the next card you play this turn costs [2][A][A] less."}, {"your next card"}},
        {873, "Gangplank, Naval", {"or return me to hand, give me +3 [M] this turn instead."}, {}},
        {821, "Resonating Strike", {"[Reaction] (Play any time, even before spells and abilities resolve.)"},
         {"[Reaction] (Play on your turn or in showdowns.)"}},
        // errata/radiance-errata.txt
        {981, "Elnuk Herd", {"When you play me, give a friendly unit +3 [M] this turn,"}, {"another friendly unit"}},
        {1096, "Last Caress", {"The next unit you play this turn, even from face down, can be played to any battlefield."},
         {"(even from face down)"}},
        {1107, "Forbidden Technique", {"you may play this from your trash for [1][R][R]. Then banish this."}, {}},
        {1106, "Neeko, Blending In", {"choose up to 3 different legends in addition to your starting legend.",
                                      "choose one of those Legends and add it to your Legend Zone"},
         {"choose 3 different"}},
        // errata/unleased.txt
        {475, "Guards!", {"Play a 2 [M] Sand Soldier unit token. Then do this: You may pay [C] to ready it."}, {}},
        {503, "Relentless Pursuit", {"You may attach up to one Equipment with the same controller to it."},
         {"attach an Equipment"}},
        {747, "Death from Below", {"Then, if it had 3 [M] or less, do this: You may play this from your trash for [A]."}, {}},
        {701, "Bone Skewer", {"If they do, then do this: [Stun] it."}, {"When they do"}},
        {756, "Deceiver", {"to play a ready Reflection unit token there. Then do this: It becomes a copy of another unit there."}, {}},
        {757, "Mirror Image", {"to your base. Then do this: It becomes a copy of that unit. Give it [Temporary]."}, {}},
        {643, "Keeper of Masks", {"play two Reflection unit tokens here. Then do this: They become copies of me."}, {}},
        {682, "Rengar, Trophy Hunter",
         {"I can [Ambush] to a battlefield where there are enemy units, even if you don't have units there."},
         {"I can be played to"}},
    };
    for (const auto& e : errata) {
        const auto& def = card_db.get(e.id);
        EXPECT_EQ(def.name, e.name) << "card " << e.id;
        for (const char* want : e.must_contain)
            EXPECT_NE(def.ability_text.find(want), std::string::npos)
                << e.name << ": missing erratum text \"" << want << "\"\n  text: " << def.ability_text;
        for (const char* old : e.must_not_contain)
            EXPECT_EQ(def.ability_text.find(old), std::string::npos)
                << e.name << ": still has pre-erratum text \"" << old << "\"";
    }
}

// ─── Relentless Pursuit: "You may attach up to one Equipment" ──────────────

/// Answers yes/no prompts with `yes`, picks `want` when offered objects (else
/// the first), passes priority otherwise. Counts the yes/no prompts.
class PursuitAgent : public AgentInterface {
public:
    bool yes = true;
    GameObjectId want = kInvalidId;
    int may_prompts = 0;
    std::vector<GameObjectId> offered;
    Intent selectAction(const GameState&, const std::vector<Intent>& legal) override {
        if (legal.empty()) return Intent{};
        const auto& f = legal.front();
        if (f.type == IntentType::MakeChoice && f.chosen_value.has_value()) {
            ++may_prompts;
            for (const auto& i : legal) if (i.chosen_value == (yes ? 1 : 0)) return i;
            return f;
        }
        if (f.type == IntentType::MakeChoice && !f.chosen_objects.empty()) {
            offered.clear();
            for (const auto& i : legal) if (!i.chosen_objects.empty()) offered.push_back(i.chosen_objects.front());
            for (const auto& i : legal)
                if (!i.chosen_objects.empty() && i.chosen_objects.front() == want) return i;
            return f;
        }
        for (const auto& i : legal) if (i.type == IntentType::PassPriority) return i;
        return f;
    }
};

class ErrataAuditTest : public CardTestFixture {
protected:
    std::unique_ptr<GameEngine> engine_;
    PursuitAgent p1_, p2_;
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
    void setLegend(PlayerId p) {
        auto id = s().createObject();
        auto& lo = s().getObject(id);
        lo.owner = lo.controller = p;
        lo.card_type = CardType::Legend;
        lo.zone = ZoneType::LegendZone;
        s().player(p).legend_zone = id;
    }
    GameObjectId boardObject(PlayerId p, CardDefId def_id) {
        auto id = s().createObject();
        auto& u = s().getObject(id);
        const auto& def = card_db.get(def_id);
        u.owner = u.controller = p;
        u.card_def_id = def_id;
        u.card_type = def.card_type;
        u.name = def.name;
        u.domains = def.domains;
        u.keywords = def.keywords;
        u.tags = def.tags;
        u.base_might = def.might;
        u.might_bonus = def.might_bonus;
        u.zone = ZoneType::Base;
        u.location = BaseLocation{p};
        u.recomputeMight();
        return id;
    }
    GameObjectId cardInHand(PlayerId p, CardDefId def_id) {
        auto id = s().createObject();
        auto& c = s().getObject(id);
        const auto& def = card_db.get(def_id);
        c.owner = c.controller = p;
        c.card_def_id = def_id;
        c.card_type = def.card_type;
        c.name = def.name;
        c.domains = def.domains;
        c.keywords = def.keywords;
        c.tags = def.tags;
        c.zone = ZoneType::Hand;
        s().player(p).hand.push_back(id);
        return id;
    }
    void castPursuit(GameObjectId unit) {
        s().player(P1).rune_pool.energy = 2;
        s().player(P1).rune_pool.universal_power = 1;
        auto spell = cardInHand(P1, kRelentlessPursuit);
        Intent i;
        i.type = IntentType::PlayCard;
        i.player = P1;
        i.card = spell;
        i.targets = {unit};
        engine_->testHook_executeIntent(i);
        engine_->testHook_runChain();
    }
};

TEST_F(ErrataAuditTest, RelentlessPursuit_AttachingAnEquipmentIsOptional) {
    makeEngine();
    auto unit = boardObject(P1, kOtterpus);
    auto blade = boardObject(P1, kDoransBlade);
    p1_.yes = false;
    castPursuit(unit);
    EXPECT_EQ(p1_.may_prompts, 1) << "\"You may attach\" is asked";
    EXPECT_FALSE(s().getObject(blade).attached_to.has_value()) << "declined: nothing attached";
    EXPECT_EQ(s().getObject(unit).current_might, 2);
}

TEST_F(ErrataAuditTest, RelentlessPursuit_AttachesTheOneEquipmentChosen) {
    makeEngine();
    auto unit = boardObject(P1, kOtterpus);
    auto first = boardObject(P1, kDoransBlade);
    auto second = boardObject(P1, kDoransBlade);
    boardObject(P2, kDoransBlade);                 // not "with the same controller"
    p1_.yes = true;
    p1_.want = second;
    castPursuit(unit);
    EXPECT_EQ(p1_.offered, (std::vector<GameObjectId>{first, second})) << "only my Equipment is offered";
    EXPECT_FALSE(s().getObject(first).attached_to.has_value());
    ASSERT_TRUE(s().getObject(second).attached_to.has_value());
    EXPECT_EQ(*s().getObject(second).attached_to, unit);
    EXPECT_EQ(s().getObject(unit).attachments.size(), 1u) << "up to ONE Equipment";
    EXPECT_EQ(s().getObject(unit).current_might, 4) << "2 + Doran's Blade's +2";
}

TEST_F(ErrataAuditTest, RelentlessPursuit_NoEquipmentNoPrompt) {
    makeEngine();
    auto unit = boardObject(P1, kOtterpus);
    castPursuit(unit);
    EXPECT_EQ(p1_.may_prompts, 0);
    EXPECT_TRUE(s().getObject(unit).attachments.empty());
}

}  // namespace
}  // namespace riftbound::test
