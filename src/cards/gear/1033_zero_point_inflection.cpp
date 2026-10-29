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

class ZeroPointInflection : public GearCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play a card, draw 2.
    //  When you draw 1 or more cards with 9 or more other cards in your hand,
    //  discard your hand, kill this, and choose an opponent. They score 1
    //  point."
    //
    // "Other cards" = cards in hand besides the ones just drawn, checked when
    // the draw happens (an intervening condition: triggerCondition). The
    // draw event (CardsDrawnEvent → WhenYouDrawACard) does not tell the
    // trigger how many cards were drawn, so the card works it out from
    // PlayerState::draws_this_turn against a snapshot this card refreshes in
    // every aura pass and right before its own draw 2. If several separate
    // draws happen with no cleanup in between, the later ones are counted
    // together (fewer "other" cards are assumed). In 1v1 the opponent is
    // chosen automatically.
    static constexpr const char* kSeenTurn  = "__zpi_seen_turn";
    static constexpr const char* kSeenDraws = "__zpi_seen_draws";

    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayACard, TriggerType::WhenYouDrawACard};
    }

    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType trigger, GameObjectId /*subject*/) const override {
        if (trigger != TriggerType::WhenYouDrawACard) return true;
        const auto& ps = state.player(controller);
        int seen = 0;
        if (state.objectExists(source)) {
            const auto& cc = state.getObject(source).card_counters;
            auto t = cc.find(kSeenTurn);
            auto d = cc.find(kSeenDraws);
            if (t != cc.end() && t->second == state.turn.turn_number && d != cc.end())
                seen = d->second;
        }
        const int drawn = std::max(1, ps.draws_this_turn - seen);
        return static_cast<int>(ps.hand.size()) - drawn >= 9;
    }

    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        for (auto& [sid, self] : state.objects) {
            if (self.card_def_id != cardDefId() || self.controller != controller) continue;
            if (!self.location.has_value()) continue;
            snapshot(state, self);
        }
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::WhenYouDrawACard) {
            auto& ps = ctx.state.player(ctx.controller);
            const std::vector<GameObjectId> hand = ps.hand;   // copy: discarding edits it
            for (auto cid : hand) ctx.executor.applyDiscard(ctx.controller, cid);
            if (ctx.state.objectExists(ctx.source) &&
                ctx.state.getObject(ctx.source).location.has_value())
                ctx.executor.killObject(ctx.source);
            const PlayerId opp = opponent(ctx.controller);
            ctx.executor.scorePoints(opp, 1);
            ctx.events.logTrace("ZERO POINT INFLECTION: discarded " + std::to_string(hand.size()) +
                                " cards, killed itself, " + std::string(toString(opp)) +
                                " scores 1");
            return;
        }
        // "When you play a card, draw 2."
        for (auto& [sid, self] : ctx.state.objects)
            if (self.card_def_id == cardDefId() && self.controller == ctx.controller &&
                self.location.has_value())
                snapshot(ctx.state, self);
        ctx.executor.drawCards(ctx.controller, 2);
        ctx.events.logTrace("ZERO POINT INFLECTION: draw 2");
    }
private:
    static void snapshot(const GameState& state, GameObject& self) {
        self.card_counters[kSeenTurn] = state.turn.turn_number;
        self.card_counters[kSeenDraws] = state.player(self.controller).draws_this_turn;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1033;
        d.def_id = R"RB(rad-069-167)RB";
        d.name = R"RB(Zero Point Inflection)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-069/167)RB";
        d.collector_number = 69;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Mind};
        d.energy_cost = 9;
        d.power_cost = 2;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(When you play a card, draw 2.
When you draw 1 or more cards with 9 or more other cards in your hand, discard your hand, kill this, and choose an opponent. They score 1 point.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/5e0646270d27f9fa78f4c3a637695e5cdeb8ee67-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1033(CardRegistry& r) {
    r.registerCard(1033, std::make_unique<ZeroPointInflection>());
}

}  // namespace riftbound
