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

class ElnukHerd : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // [Hidden] is engine-handled (keyword).
    // "When you play me, give another friendly unit +3 [M] this turn, then
    //  recycle me if I was played from face down."
    // A play from face down is stamped card_counters["__played_from_facedown"]
    // = 1 by the engine. It is latched on the ability's first entry (the
    // finalization pass, or resolution if there was none) and consumed, so a
    // later play of this card from hand does not see a stale marker.
    static constexpr const char* kLatchKey = "__elnuk_from_facedown";

    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        {
            auto& self = ctx.state.getObject(ctx.source);
            const auto& ri = ctx.state.chain.resuming;
            if (!ri.has_value() || (ri->resume_point == 0 && !ri->choices_finalized)) {
                auto& from_facedown = self.card_counters["__played_from_facedown"];
                self.card_counters[kLatchKey] = from_facedown == 1 ? 1 : 0;
                from_facedown = 0;
            }
        }
        const auto others = chooseableUnits(ctx.state, ctx.controller,
            [&](const GameObject& o) {
                return o.controller == ctx.controller && o.id != ctx.source;
            });
        GameObjectId tgt = pickTarget(ctx, "Elnuk Herd: give another friendly unit +3 [M]",
                                      others);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (tgt == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the agent
        if (tgt != kInvalidId && tgt != ctx.source && ctx.state.objectExists(tgt)) {
            ctx.executor.giveTemporaryMight(tgt, 3);
            ctx.events.logTrace("ELNUK HERD: +3 [M] this turn to " +
                                ctx.state.getObject(tgt).name);
        }
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& self = ctx.state.getObject(ctx.source);
        if (self.card_counters[kLatchKey] != 1) return;
        self.card_counters[kLatchKey] = 0;
        recycleSelfFromBoard(ctx);
    }
private:
    /// Recycle this unit from the board to the bottom of its owner's Main Deck.
    /// (EffectExecutor::recycleCards only moves the card; leaving the board —
    /// shedding attachments, clearing damage / combat status, LeftBoardEvent —
    /// is done here, as bounceToHand does it for a return to hand.)
    static void recycleSelfFromBoard(CardContext& ctx) {
        auto& self = ctx.state.getObject(ctx.source);
        if (!self.location.has_value()) return;   // already left the board
        if (self.attached_to.has_value()) ctx.executor.unattachGear(ctx.source);
        const auto attached = self.attachments;   // copy: unattachGear edits it
        for (auto gid : attached) ctx.executor.unattachGear(gid);
        auto& me = ctx.state.getObject(ctx.source);
        const LocationId was_at = *me.location;
        const PlayerId controller = me.controller;
        const CardType type = me.card_type;
        me.damage_marked = 0;
        me.combat_designation = CombatDesignation::None;
        me.is_exhausted = false;
        ctx.executor.recycleCards(ctx.controller, {ctx.source});
        ctx.events.emit(LeftBoardEvent{ctx.source, controller, type, was_at,
                                       ZoneType::MainDeck, false});
        ctx.events.logTrace("ELNUK HERD: played from face down -> recycled");
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 981;
        d.def_id = R"RB(rad-081-167)RB";
        d.name = R"RB(Elnuk Herd)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-081/167)RB";
        d.collector_number = 81;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Fury};
        d.energy_cost = 6;
        d.might = 5;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Hidden);
        d.ability_text = R"RB([Hidden] (Hide now for [A] to react with later for [0].)
When you play me, give another friendly unit +3 [M] this turn, then recycle me if I was played from face down.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/1799cd4c46fcf0243b4022de2d427b15e67a15ad-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_981(CardRegistry& r) {
    r.registerCard(981, std::make_unique<ElnukHerd>());
}

}  // namespace riftbound
