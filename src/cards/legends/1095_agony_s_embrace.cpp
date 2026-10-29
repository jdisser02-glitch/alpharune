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

class AgonysEmbrace : public LegendCard {
public:
    const CardDef& def() const override { return def_; }

    // "Once each turn, when you play a card from face down, draw 1. Then, if
    //  it's not your turn, discard 1."
    // The engine's WhenYouPlayFromFacedown sweep reaches on-board cards only,
    // not the Legend Zone, so the legend listens to "played from anywhere other
    // than your hand" (which does reach it, with the played card as subject)
    // and keeps only plays from face down: both face-down play paths stamp
    // __played_from_facedown on the card before announcing it.
    static constexpr const char* kUsedKey = "__agony_used_turn";

    TriggerType triggerType() const override { return TriggerType::WhenYouPlayACardNotFromHand; }

    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId /*controller*/,
                          TriggerType /*trigger*/, GameObjectId subject) const override {
        if (!state.objectExists(subject) || !state.objectExists(source)) return false;
        const auto& played = state.getObject(subject);
        auto it = played.card_counters.find("__played_from_facedown");
        if (it == played.card_counters.end() || it->second != 1) return false;
        // Once each turn: not if it already resolved this turn, nor while an
        // earlier trigger of it is still waiting on the chain.
        const auto& self = state.getObject(source);
        auto used = self.card_counters.find(kUsedKey);
        if (used != self.card_counters.end() && used->second == state.turn.turn_number) return false;
        for (const auto& ci : state.chain.items)
            if (ci.source == source && ci.is_ability && !ci.is_activated_ability) return false;
        return true;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.chain.resuming.has_value()) {
            // Driven outside the chain: draw, and discard the first card.
            mark(ctx);
            ctx.executor.drawCards(ctx.controller, 1);
            if (ctx.state.turn.turn_player != ctx.controller) {
                auto& hand = ctx.state.player(ctx.controller).hand;
                if (!hand.empty()) ctx.executor.applyDiscard(ctx.controller, hand.front());
            }
            return;
        }
        auto& ri = *ctx.state.chain.resuming;
        if (ri.resume_point == 0) {
            mark(ctx);
            ctx.executor.drawCards(ctx.controller, 1);
            ctx.events.logTrace("AGONY'S EMBRACE: played from face down -> draw 1");
            if (ctx.state.turn.turn_player == ctx.controller) return;
        }
        // Not my turn: discard 1 (the controller picks the card).
        GameObjectId pick = pickTarget(ctx, "Agony's Embrace: discard 1",
                                       ctx.state.player(ctx.controller).hand);
        if (pick == kInvalidId) return;   // waiting for the pick, or an empty hand
        ctx.executor.applyDiscard(ctx.controller, pick);
        ctx.events.logTrace("AGONY'S EMBRACE: not my turn -> discard 1");
    }

private:
    static void mark(CardContext& ctx) {
        if (ctx.state.objectExists(ctx.source))
            ctx.state.getObject(ctx.source).card_counters[kUsedKey] = ctx.state.turn.turn_number;
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1095;
        d.def_id = R"RB(rad-153-167)RB";
        d.name = R"RB(Agony's Embrace)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-153/167)RB";
        d.collector_number = 153;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Legend;
        d.domains = {Domain::Body, Domain::Chaos};
        d.tags = {R"RB(Evelynn)RB"};
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(Once each turn, when you play a card from face down, draw 1. Then, if it's not your turn, discard 1.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/ca217b57d0e8d2e04e7a35105deaa6be17e854f0-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1095(CardRegistry& r) {
    r.registerCard(1095, std::make_unique<AgonysEmbrace>());
}

}  // namespace riftbound
