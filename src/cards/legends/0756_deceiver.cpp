#include "cards/card.h"
#include "cards/card_helpers.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace riftbound {
namespace {

class Deceiver : public LegendCard {
public:
    const CardDef& def() const override { return def_; }
    TriggerType triggerType() const override { return TriggerType::WhenIConquerOrHold; }

    // "there" is the battlefield that was conquered or held: the trigger's
    // subject is that battlefield's card object.
    static std::optional<BattlefieldId> scoredBattlefield(GameState& state,
                                                           PlayerId /*controller*/) {
        if (!state.chain.resuming.has_value()) return std::nullopt;
        return battlefieldOfCard(state, state.chain.resuming->triggering_subject);
    }

    // Resume points: 0-2 confirmOptional ("you may"), kAwaitDiscard the card
    // to discard, kCopy.. the token exists and pickTarget (6-8) chooses what
    // it copies. resume_data[3] = the token, [4] = its battlefield + 1 (0 =
    // none); [2] belongs to pickTarget.
    static constexpr int kAwaitDiscard = 3;
    static constexpr int kCopy = 5;
    static constexpr int kFinished = 30;

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>&) override {
        if (!ctx.state.chain.resuming.has_value()) return;
        auto& ri = *ctx.state.chain.resuming;
        if (ri.resume_point == kFinished) return;
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& ps = ctx.state.player(ctx.controller);
        // Both halves of the cost must be payable: discard 1 and exhaust me.
        auto can_pay = [&]() {
            return ctx.state.objectExists(ctx.source) &&
                   !ctx.state.getObject(ctx.source).is_exhausted && !ps.hand.empty();
        };

        if (ri.resume_point <= 2) {
            // "you may discard 1 and exhaust me": optional (CR 383.3.a).
            const int go = confirmOptional(
                ctx, "Deceiver: discard 1 and exhaust me to play a Reflection", can_pay);
            if (go != 1) return;            // waiting, declined, or can't pay
            std::vector<Intent> choices;
            for (auto card_id : ps.hand) {
                Intent c;
                c.type = IntentType::MakeChoice;
                c.player = ctx.controller;
                c.chosen_objects = {card_id};
                choices.push_back(c);
            }
            ctx.executor.requestChoice(ctx.controller, std::move(choices),
                                       "discard 1 (Deceiver)");
            ri.resume_point = kAwaitDiscard;
            return;
        }

        if (ri.resume_point == kAwaitDiscard) {
            auto choice = ctx.executor.takeChoice();
            if (!can_pay()) { ri.resume_point = kFinished; return; }
            GameObjectId discard = ps.hand.front();
            if (choice && !choice->chosen_objects.empty() &&
                std::find(ps.hand.begin(), ps.hand.end(), choice->chosen_objects[0]) !=
                    ps.hand.end())
                discard = choice->chosen_objects[0];
            // Pay the cost: exhaust me, discard the chosen card.
            ctx.executor.exhaustObject(ctx.source);
            ctx.executor.applyDiscard(ctx.controller, discard);

            auto bf = scoredBattlefield(ctx.state, ctx.controller);
            LocationId loc = bf.has_value()
                ? LocationId{BattlefieldLocation{*bf}}
                : LocationId{BaseLocation{ctx.controller}};
            // CR 187.6: a 0 [M] Reflection token; played ready, "there".
            KeywordSet kw;
            kw.set(Keyword::Temporary);
            GameObjectId token = ctx.executor.createToken(
                ctx.controller, CardType::Unit, "Reflection", 0, {}, kw, loc,
                /*enter_ready=*/true);
            ctx.events.logTrace("DECEIVER: created ready Reflection token there");
            while (ri.resume_data.size() < 5) ri.resume_data.push_back(0);
            ri.resume_data[3] = static_cast<int32_t>(token);
            ri.resume_data[4] = bf.has_value() ? static_cast<int32_t>(*bf) + 1 : 0;
            ri.resume_point = kCopy;
        }

        // "Then do this: It becomes a copy of another unit there." Which unit
        // is the controller's choice (any controller's unit; units that can't
        // be chosen by enemy spells and abilities are not offered).
        if (ri.resume_data.size() < 5 || ri.resume_data[4] == 0) {
            ri.resume_point = kFinished;
            return;
        }
        const auto token = static_cast<GameObjectId>(ri.resume_data[3]);
        const auto bf = static_cast<BattlefieldId>(ri.resume_data[4] - 1);
        if (!ctx.state.objectExists(token) ||
            !ctx.state.getObject(token).location.has_value()) {
            ri.resume_point = kFinished;
            return;
        }
        auto models = chooseableUnits(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.id != token && o.battlefieldId() == bf;
        });
        std::sort(models.begin(), models.end());
        const bool first_ask = ri.resume_point < 6;
        GameObjectId model = pickTarget(ctx, "Deceiver: copy which unit there?", models);
        if (model == kInvalidId) {
            // Either suspended for the agent (resume_point 7) or, when asked
            // for the first time, there was nobody to copy.
            if (first_ask && ri.resume_point != 7) ri.resume_point = kFinished;
            return;
        }
        if (ctx.state.objectExists(model)) {
            ctx.executor.copyUnit(token, model);
            // "Give it [Temporary]." copyUnit overwrites keywords with the
            // copied ones, so it is (re)granted afterwards.
            if (ctx.state.objectExists(token))
                ctx.state.getObject(token).keywords.set(Keyword::Temporary);
            ctx.events.logTrace("DECEIVER: token copied a unit there, kept [Temporary]");
        }
        ri.resume_point = kFinished;
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 756;
        d.def_id = R"RB(unl-199-219)RB";
        d.name = R"RB(Deceiver)RB";
        d.set_code = R"RB(UNL)RB";
        d.set_name = R"RB(Unleashed)RB";
        d.public_code = R"RB(UNL-199/219)RB";
        d.collector_number = 199;
        d.artist = R"RB(黯荧岛Dark Glow)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Mind, Domain::Order};
        d.tags = {R"RB(LeBlanc)RB"};
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When you conquer or hold, you may discard 1 and exhaust me to play a ready Reflection unit token there. Then do this: It becomes a copy of another unit there. Give it [Temporary].)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/c17d29602d61d6702372fec4db44c6fb2a29e8a2-744x1039.png?accountingTag=RB)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_756(CardRegistry& r) {
    r.registerCard(756, std::make_unique<Deceiver>());
}

} // namespace riftbound
