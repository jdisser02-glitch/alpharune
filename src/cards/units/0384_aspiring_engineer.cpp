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

class AspiringEngineer : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    // "When you play me, return a gear from your trash to your hand." The gear
    // comes from the trash; a gear on the board is not a candidate.
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto& ps = ctx.state.player(ctx.controller);
        std::vector<GameObjectId> trash_gear;
        for (auto cid : ps.trash) {
            if (!ctx.state.objectExists(cid)) continue;
            if (ctx.state.getObject(cid).isGear()) trash_gear.push_back(cid);
        }
        if (trash_gear.empty()) return;
        GameObjectId picked = pickTarget(ctx, "Aspiring Engineer (gear from trash)", trash_gear);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (picked == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7) {
            return;  // suspended for agent decision
        }
        if (picked == kInvalidId || !ctx.state.objectExists(picked)) return;
        auto it = std::find(ps.trash.begin(), ps.trash.end(), picked);
        if (it == ps.trash.end()) return;
        ps.trash.erase(it);
        auto& obj = ctx.state.getObject(picked);
        obj.zone = ZoneType::Hand;
        obj.location = std::nullopt;
        ps.hand.push_back(picked);
        ctx.events.logTrace("ASPIRING ENGINEER: returned " + obj.name + " from trash to hand");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 384;
        d.def_id = R"RB(sfd-061-221)RB";
        d.name = R"RB(Aspiring Engineer)RB";
        d.set_code = R"RB(SFD)RB";
        d.set_name = R"RB(Spiritforged)RB";
        d.public_code = R"RB(SFD-061/221)RB";
        d.collector_number = 61;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Piltover)RB"};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.might = 3;
        d.ability_text = R"RB(When you play me, return a gear from your trash to your hand.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/5d61995ae384bcee56838beb0eb6d83bf5faefad-744x1039.png?accountingTag=RB)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_384(CardRegistry& r) {
    r.registerCard(384, std::make_unique<AspiringEngineer>());
}

} // namespace riftbound
