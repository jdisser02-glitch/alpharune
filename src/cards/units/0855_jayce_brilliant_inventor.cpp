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

class JayceBrilliantInventor : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play me or the first time you play a non-token gear each turn,
    // you may ready something besides me that's exhausted."
    // The gear half rides WhenYouPlayACard (its chain item carries the played
    // card as triggering_subject, which WhenYouPlayAGear does not).
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayMe, TriggerType::WhenYouPlayACard};
    }
    // The gear half triggers only on the turn's first non-token gear: any
    // other card played is not a trigger at all (so nothing goes on the chain).
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId /*controller*/,
                          TriggerType trigger, GameObjectId subject) const override {
        if (trigger != TriggerType::WhenYouPlayACard) return true;
        if (subject == kInvalidId || !state.objectExists(subject)) return false;
        const auto& p = state.getObject(subject);
        if (!p.isGear() || p.isToken()) return false;
        if (!state.objectExists(source)) return false;
        const auto& cc = state.getObject(source).card_counters;
        auto it = cc.find("__jayce_first_gear_turn");
        return it == cc.end() || it->second != state.turn.turn_number + 1;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& self = ctx.state.getObject(ctx.source);
        const bool first_entry = !ctx.state.chain.resuming.has_value() ||
                                 ctx.state.chain.resuming->resume_point == 0;
        const int turn_id = ctx.state.turn.turn_number + 1;   // +1 so 0 = never
        if (first_entry) {
            int& gear_turn = self.card_counters["__jayce_first_gear_turn"];
            if (ctx.firing_trigger == TriggerType::WhenYouPlayACard) {
                GameObjectId played = ctx.state.chain.resuming.has_value()
                    ? ctx.state.chain.resuming->triggering_subject : kInvalidId;
                if (played == kInvalidId || !ctx.state.objectExists(played)) return;
                const auto& p = ctx.state.getObject(played);
                if (!p.isGear() || p.isToken()) return;       // non-token gear only
                if (gear_turn == turn_id) return;             // not the first this turn
                gear_turn = turn_id;
            } else {
                // Played after a non-token gear this turn: a later gear this
                // turn is not "the first".
                if (ctx.state.player(ctx.controller).played_nontoken_gear_this_turn)
                    gear_turn = turn_id;
            }
        }
        auto find_targets = [&]() {
            std::vector<GameObjectId> out;
            for (auto& [id, obj] : ctx.state.objects) {
                if (id == ctx.source || obj.controller != ctx.controller) continue;
                if (!obj.is_exhausted) continue;
                if (!obj.location.has_value() && obj.zone != ZoneType::LegendZone) continue;
                out.push_back(id);
            }
            std::sort(out.begin(), out.end());
            return out;
        };
        int answer = confirmOptional(ctx, "Jayce, Brilliant Inventor: ready something else?",
                                     [&]() { return !find_targets().empty(); });
        if (answer != 1) return;                      // waiting, declined or nothing exhausted
        auto target = pickTarget(ctx, "Jayce, Brilliant Inventor: ready", find_targets());
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (target == kInvalidId || !ctx.state.objectExists(target)) return;
        ctx.executor.readyObject(target);
        ctx.events.logTrace("JAYCE, BRILLIANT INVENTOR: readied " +
                            ctx.state.getObject(target).name);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 855;
        d.def_id = R"RB(ven-068-166)RB";
        d.name = R"RB(Jayce, Brilliant Inventor)RB";
        d.set_code = R"RB(VEN)RB";
        d.set_name = R"RB(Vendetta)RB";
        d.public_code = R"RB(VEN-068/166)RB";
        d.collector_number = 68;
        d.artist = R"RB(League of Legends)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Jayce)RB", R"RB(Piltover)RB"};
        d.energy_cost = 6;
        d.power_cost = 1;
        d.might = 6;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(When you play me or the first time you play a non-token gear each turn, you may ready something besides me that's exhausted.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/123e949085a63fdb6e1590ff0e519cab5758688b-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_855(CardRegistry& r) {
    r.registerCard(855, std::make_unique<JayceBrilliantInventor>());
}

}  // namespace riftbound
