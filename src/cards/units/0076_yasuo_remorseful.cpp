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

class YasuoRemorseful : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "When I attack, deal damage equal to my Might to an enemy unit here."
    // The enemy unit (at my battlefield) is chosen as the trigger is finalized
    // (CR 383.3); the damage is my Might as the ability resolves. A triggered
    // ability's chain item carries no chosen targets, so the previous version,
    // which read the unit from `targets`, never dealt damage.
    static std::vector<GameObjectId> candidates(const CardContext& ctx) {
        const auto here = hereOf(ctx.state, ctx.source);
        if (!here.has_value()) return {};
        return chooseableUnitsSorted(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller != ctx.controller && o.battlefieldId() == here;
        });
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId t = pickTarget(ctx, "Yasuo, Remorseful: deal my Might to an enemy unit here",
                                          candidates(ctx));
        if (waitingForTarget(ctx, t)) return;
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (t == kInvalidId || !ctx.state.objectExists(t) || !ctx.state.objectExists(ctx.source)) return;
        const int dmg = ctx.state.getObject(ctx.source).current_might;
        if (dmg <= 0) return;
        ctx.executor.dealDamage(t, dmg, ctx.source);
        if (ctx.state.objectExists(t) && ctx.state.getObject(t).hasLethalDamage())
            ctx.executor.killObject(t);
    }
    TriggerType triggerType() const override { return TriggerType::WhenIAttack; }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 76;
        d.def_id = R"RB(ogn-076-298)RB";
        d.name = R"RB(Yasuo, Remorseful)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-076/298)RB";
        d.collector_number = 76;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Yasuo)RB", R"RB(Ionia)RB"};
        d.energy_cost = 6;
        d.power_cost = 2;
        d.might = 6;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When I attack, deal damage equal to my Might to an enemy unit here.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/1643a6c93626884c93363557e1a483642bda6c45-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_76(CardRegistry& r) {
    r.registerCard(76, std::make_unique<YasuoRemorseful>());
}

} // namespace riftbound
