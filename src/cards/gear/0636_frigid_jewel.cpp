#include "cards/card.h"
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

class FrigidJewel : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you draw your second card each turn, give a friendly unit +2 [M]
    //  this turn."
    // WhenYouDrawACard fires per draw event. It triggers only for the draw
    // that takes the controller's draws_this_turn from below 2 to 2 or more:
    // the count before the draw is the snapshot this card refreshes every aura
    // pass (two draws with no cleanup in between both see the older count, so
    // the "once each turn" guard at resolution still applies). The friendly
    // unit is the agent's choice, made as the trigger is finalized
    // (CR 383.3). (It used to be the first friendly unit found.)
    static constexpr const char* kSeenTurn  = "__frigid_seen_turn";
    static constexpr const char* kSeenDraws = "__frigid_seen_draws";
    static constexpr const char* kFiredKey  = "__frigid_fired_turn";
    TriggerType triggerType() const override { return TriggerType::WhenYouDrawACard; }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType /*trigger*/, GameObjectId /*subject*/) const override {
        const int draws = state.player(controller).draws_this_turn;
        if (draws < 2 || !state.objectExists(source)) return false;
        const auto& cc = state.getObject(source).card_counters;
        const int turn = state.turn.turn_number;
        auto f = cc.find(kFiredKey);
        if (f != cc.end() && f->second == turn + 1) return false;   // already this turn
        auto t = cc.find(kSeenTurn);
        auto d = cc.find(kSeenDraws);
        const int before = (t != cc.end() && t->second == turn + 1 && d != cc.end()) ? d->second : 0;
        return before < 2;
    }
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        forEachSelfOnce(*this, state, controller, [&](GameObjectId, GameObject& self) {
            self.card_counters[kSeenTurn] = state.turn.turn_number + 1;
            self.card_counters[kSeenDraws] = state.player(controller).draws_this_turn;
        });
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto friendly = chooseableUnits(ctx.state, ctx.controller,
            [&](const GameObject& o) { return o.controller == ctx.controller; });
        const GameObjectId tgt = pickTarget(ctx, "Frigid Jewel: give a friendly unit +2 [M]",
                                            friendly);
        if (tgt == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the agent
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (!ctx.state.objectExists(ctx.source)) return;
        if (ctx.state.player(ctx.controller).draws_this_turn < 2) return;   // not yet the 2nd
        auto& self = ctx.state.getObject(ctx.source);
        const int sentinel = ctx.state.turn.turn_number + 1;   // avoid 0-default clash
        if (self.card_counters[kFiredKey] == sentinel) return;  // once each turn
        self.card_counters[kFiredKey] = sentinel;
        if (tgt == kInvalidId || !ctx.state.objectExists(tgt)) return;
        ctx.executor.giveTemporaryMight(tgt, 2);
        ctx.events.logTrace("FRIGID JEWEL: 2nd draw -> " + ctx.state.getObject(tgt).name +
                            " +2 [M] this turn");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 636;
        d.def_id = R"RB(unl-074-219)RB";
        d.name = R"RB(Frigid Jewel)RB";
        d.set_code = R"RB(UNL)RB";
        d.set_name = R"RB(Unleashed)RB";
        d.public_code = R"RB(UNL-074/219)RB";
        d.collector_number = 74;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Mind};
        d.energy_cost = 2;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you draw your second card each turn, give a friendly unit +2 [M] this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/f1a6c28649bf4478b4728379b6d548058739f0e9-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_636(CardRegistry& r) {
    r.registerCard(636, std::make_unique<FrigidJewel>());
}

} // namespace riftbound
