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

class YuumiMagicalCat : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "When I attack or defend, give one of your other units here +3 [M] and [Tank] this turn."
    // The unit is chosen as the trigger is finalized (CR 383.3). A triggered
    // ability's chain item carries no chosen targets, so the previous
    // version, which read the unit from `targets`, never did anything.
    static std::vector<GameObjectId> candidates(const CardContext& ctx) {
        const auto here = hereOf(ctx.state, ctx.source);
        if (!here.has_value()) return {};
        return chooseableUnitsSorted(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller == ctx.controller && o.id != ctx.source && o.battlefieldId() == here;
        });
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId t = pickTarget(ctx, "Yuumi, Magical Cat: give another of your units here +3 [M] and [Tank]", candidates(ctx));
        if (waitingForTarget(ctx, t)) return;
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;
        ctx.executor.giveTemporaryMight(t, 3);
        ctx.executor.giveTemporaryKeyword(t, Keyword::Tank, 0);   // [Tank] this turn
    }
    TriggerType triggerType() const override { return TriggerType::WhenIAttackOrDefend; }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 618;
        d.def_id = R"RB(unl-056-219)RB";
        d.name = R"RB(Yuumi, Magical Cat)RB";
        d.set_code = R"RB(UNL)RB";
        d.set_name = R"RB(Unleashed)RB";
        d.public_code = R"RB(UNL-056/219)RB";
        d.collector_number = 56;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Fae)RB", R"RB(Cat)RB", R"RB(Bandle City)RB", R"RB(Yuumi)RB"};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.might = 1;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When I attack or defend, give one of your other units here +3 [M] and [Tank] this turn. (It must be assigned combat damage first.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/588ddfeb01ab7b37110d8a7e656cf9f35530ce4e-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_618(CardRegistry& r) {
    r.registerCard(618, std::make_unique<YuumiMagicalCat>());
}

} // namespace riftbound
