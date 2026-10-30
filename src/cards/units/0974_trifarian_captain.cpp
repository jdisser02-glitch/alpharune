#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "cards/card_helpers.h"

namespace riftbound {
namespace {

class TrifarianCaptain : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play me, look at the top 4 cards of your Main Deck. You may
    //  reveal from among them a unit or gear with Energy cost no more than [3]
    //  and with Power cost no more than [A] and draw it. Recycle the rest."
    // Same shape as Fate Weaver: the "you may" is decided after looking, so it
    // is a resolution-time choice (confirmOptional 0/1/2, then pickTarget
    // 6/7/8 among the eligible cards).
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }

    static std::vector<GameObjectId> topFour(const CardContext& ctx) {
        const auto& ps = ctx.state.player(ctx.controller);
        std::vector<GameObjectId> out;
        for (auto it = ps.main_deck.rbegin(); it != ps.main_deck.rend() && out.size() < 4; ++it)
            if (ctx.state.objectExists(*it)) out.push_back(*it);
        return out;
    }
    static std::vector<GameObjectId> eligible(const CardContext& ctx) {
        std::vector<GameObjectId> out;
        for (auto id : topFour(ctx)) {
            const auto& obj = ctx.state.getObject(id);
            if (!obj.isUnit() && !obj.isGear()) continue;
            if (obj.card_def_id == kInvalidId) continue;
            const auto& d = ctx.executor.cardDB().get(obj.card_def_id);
            if (d.energy_cost <= 3 && d.power_cost <= 1) out.push_back(id);
        }
        return out;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        // First entry: the look itself. It is private, but it is a look from
        // the top of the deck, so "As you look at or reveal me from the top
        // of your deck" (Nocturne, Horrifying) applies to each card.
        const auto& ri = ctx.state.chain.resuming;
        if (!ri.has_value() || ri->resume_point == 0) {
            for (auto id : topFour(ctx)) {
                const auto& obj = ctx.state.getObject(id);
                ctx.events.emit(CardRevealedEvent{
                    id, obj.card_def_id, obj.owner,
                    /*revealed_to_all=*/false, /*revealed_to=*/ctx.controller,
                    ZoneType::MainDeck});
            }
        }
        int conf = confirmOptional(ctx,
            "Trifarian Captain: reveal a unit or gear ([3] or less, [A] or less) from the top 4 and draw it?",
            [&]() { return !eligible(ctx).empty(); });
        if (conf == -1) return;   // waiting for the agent

        // The top 4 as looked at, before anything is drawn.
        const std::vector<GameObjectId> looked_at = topFour(ctx);

        GameObjectId drawn = kInvalidId;
        if (conf == 1) {
            drawn = pickTarget(ctx, "Trifarian Captain: card to reveal and draw", eligible(ctx));
            if (drawn == kInvalidId && ctx.state.chain.resuming.has_value() &&
                ctx.state.chain.resuming->resume_point == 7)
                return;   // waiting for the agent
            if (drawn != kInvalidId && ctx.state.objectExists(drawn)) {
                auto& ps = ctx.state.player(ctx.controller);
                auto& obj = ctx.state.getObject(drawn);
                ctx.events.emit(CardRevealedEvent{
                    drawn, obj.card_def_id, obj.owner,
                    /*revealed_to_all=*/true, /*revealed_to=*/ctx.controller,
                    ZoneType::MainDeck});
                auto it = std::find(ps.main_deck.begin(), ps.main_deck.end(), drawn);
                if (it != ps.main_deck.end()) ps.main_deck.erase(it);
                obj.zone = ZoneType::Hand;
                ps.hand.push_back(drawn);
                ps.draws_this_turn += 1;
                ctx.events.emit(CardsDrawnEvent{ctx.controller, 1});
                ctx.events.logTrace("TRIFARIAN CAPTAIN: revealed " + obj.name + " -> drew it");
            }
        }

        // "Recycle the rest." recycleCards only inserts at the bottom, so take
        // each card out of the deck first.
        std::vector<GameObjectId> rest;
        auto& ps = ctx.state.player(ctx.controller);
        for (auto id : looked_at) {
            if (id == drawn) continue;
            auto it = std::find(ps.main_deck.begin(), ps.main_deck.end(), id);
            if (it != ps.main_deck.end()) ps.main_deck.erase(it);
            rest.push_back(id);
        }
        if (!rest.empty()) {
            ctx.executor.recycleCards(ctx.controller, rest);
            ctx.events.logTrace("TRIFARIAN CAPTAIN: recycled the rest of the top 4");
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 974;
        d.def_id = R"RB(rad-129-167)RB";
        d.name = R"RB(Trifarian Captain)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-129/167)RB";
        d.collector_number = 129;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Order};
        d.energy_cost = 2;
        d.might = 1;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you play me, look at the top 4 cards of your Main Deck. You may reveal from among them a unit or gear with Energy cost no more than [3] and with Power cost no more than [A] and draw it. Recycle the rest.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/914ced10400826a878ed5cf7294207a4e7b542b1-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_974(CardRegistry& r) {
    r.registerCard(974, std::make_unique<TrifarianCaptain>());
}

}  // namespace riftbound
