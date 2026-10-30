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

class EdgeOfNight : public GearCard {
public:
    const CardDef& def() const override { return def_; }

    bool hasEquipAbility() const override { return true; }
    bool onEquip(CardContext& ctx, GameObjectId unit) override {
        // [C] = Chaos-domain power, no energy (mirrors original
        // SimpleEquipGear(460, Domain::Chaos) with energy_cost 0).
        return standardEquip(ctx, ctx.source, unit,
                                         /*energy_cost=*/0, Domain::Chaos);
    }

    TriggerType triggerType() const override {
        return TriggerType::WhenYouPlayThis;
    }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "When you play this from face down, attach it to a unit you control
    // (here)": the player picks which of their units here.
    void onTrigger(CardContext& ctx,
                   const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        const auto& self = ctx.state.getObject(ctx.source);
        // Only the play-from-facedown case: unattached gear sitting at a BF.
        if (self.attached_to.has_value() || !self.isAtBattlefield() || !self.location.has_value()) {
            (void)finalizingChoices(ctx);   // no choice in this branch
            return;
        }
        const auto here = *self.location;
        auto unitsHere = [&]() {
            std::vector<GameObjectId> out;
            for (const auto& [id, obj] : ctx.state.objects)
                if (obj.isUnit() && obj.controller == ctx.controller && obj.location.has_value() &&
                    *obj.location == here)
                    out.push_back(id);
            return out;
        };
        const auto units = unitsHere();
        if (units.empty()) {
            (void)finalizingChoices(ctx);   // no choice in this branch
            return;
        }
        GameObjectId picked = pickTarget(ctx, "Edge of Night: attach to a unit you control here", units);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (picked == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;  // suspended for agent decision
        const auto still = unitsHere();
        if (picked == kInvalidId || std::find(still.begin(), still.end(), picked) == still.end()) return;
        attachFree(ctx, ctx.source, picked);
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 460;
        d.def_id = R"RB(sfd-139-221)RB";
        d.name = R"RB(Edge of Night)RB";
        d.set_code = R"RB(SFD)RB";
        d.set_name = R"RB(Spiritforged)RB";
        d.public_code = R"RB(SFD-139/221)RB";
        d.collector_number = 139;
        d.artist = R"RB(黯荧岛Dark Glow)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Equipment)RB"};
        d.energy_cost = 3;
        d.might_bonus = 2;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Equip);
        d.keywords.set(Keyword::Hidden);
        d.ability_text = R"RB([Hidden] (Hide now for [A] to react with later for [0].)
When you play this from face down, attach it to a unit you control (here).
[Equip] [C] ([C]: Attach this to a unit you control.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b3e385162bf3566618bb58b7a866eef846beefba-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_460(CardRegistry& r) {
    r.registerCard(460, std::make_unique<EdgeOfNight>());
}

} // namespace riftbound
