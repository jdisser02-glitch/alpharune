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

class MechanicalBandit : public GearCard {
public:
    const CardDef& def() const override { return def_; }

    // "At the start of your Beginning Phase, each player reveals the top card
    //  of their Main Deck. If you revealed a card with the lowest Energy cost,
    //  draw it. Recycle the rest."
    // A tie for lowest counts as revealing a card with the lowest cost. A
    // player with an empty Main Deck reveals nothing (no burn out). Revealed
    // cards are shown to both players (CardRevealedEvent).
    TriggerType triggerType() const override { return TriggerType::AtStartOfBeginning; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        struct Revealed { PlayerId player; GameObjectId card; int cost; };
        std::vector<Revealed> revealed;
        for (PlayerId p : {ctx.controller, opponent(ctx.controller)}) {
            auto& deck = ctx.state.player(p).main_deck;
            if (deck.empty() || !ctx.state.objectExists(deck.back())) continue;
            const GameObjectId top = deck.back();
            const auto& card = ctx.state.getObject(top);
            ctx.events.emit(CardRevealedEvent{top, card.card_def_id, card.owner,
                                              /*revealed_to_all=*/true, PlayerId::None,
                                              ZoneType::MainDeck});
            revealed.push_back({p, top, energyCost(ctx, card)});
            ctx.events.logTrace("MECHANICAL BANDIT: " + std::string(toString(p)) + " reveals " +
                                card.name + " (" + std::to_string(revealed.back().cost) + " Energy)");
        }
        if (revealed.empty()) return;
        int lowest = revealed.front().cost;
        for (const auto& r : revealed) lowest = std::min(lowest, r.cost);
        GameObjectId drawn = kInvalidId;
        for (const auto& r : revealed)
            if (r.player == ctx.controller && r.cost == lowest) drawn = r.card;
        if (drawn != kInvalidId) {
            auto& deck = ctx.state.player(ctx.controller).main_deck;
            if (!deck.empty() && deck.back() == drawn) {
                ctx.executor.drawCards(ctx.controller, 1);
                ctx.events.logTrace("MECHANICAL BANDIT: you revealed the lowest Energy cost -> draw it");
            }
        }
        for (const auto& r : revealed) {
            if (r.card == drawn) continue;
            auto& deck = ctx.state.player(r.player).main_deck;
            auto it = std::find(deck.begin(), deck.end(), r.card);
            if (it == deck.end()) continue;
            deck.erase(it);
            ctx.executor.recycleCards(ctx.controller, {r.card});
        }
    }
private:
    static int energyCost(const CardContext& ctx, const GameObject& card) {
        if (card.card_def_id == kInvalidId) return 0;
        const auto& all = ctx.executor.cardDB().all();
        auto it = all.find(card.card_def_id);
        return it != all.end() ? it->second.energy_cost : 0;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1063;
        d.def_id = R"RB(rad-111-167)RB";
        d.name = R"RB(Mechanical Bandit)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-111/167)RB";
        d.collector_number = 111;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Fury};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(At the start of your Beginning Phase, each player reveals the top card of their Main Deck. If you revealed a card with the lowest Energy cost, draw it. Recycle the rest.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/cb2a419b61b6f1e6d138c3c3b07b393a4037237b-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1063(CardRegistry& r) {
    r.registerCard(1063, std::make_unique<MechanicalBandit>());
}

}  // namespace riftbound
