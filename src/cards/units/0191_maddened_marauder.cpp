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

class MaddenedMarauder : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "When you play me, move a unit from a battlefield to its base."
    // The unit is chosen as the trigger is finalized (CR 383.3). A triggered
    // ability's chain item carries no chosen targets, so the previous
    // version, which read the unit from `targets`, never did anything.
    static std::vector<GameObjectId> candidates(const CardContext& ctx) {
        return chooseableUnitsSorted(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.isAtBattlefield() &&
                   !(o.controller != ctx.controller && o.cant_be_moved_by_enemy);
        });
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId t = pickTarget(ctx, "Maddened Marauder: move a unit from a battlefield to its base", candidates(ctx));
        if (waitingForTarget(ctx, t)) return;
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;
        if (ctx.state.getObject(t).isAtBattlefield()) ctx.executor.moveToBase(t);
    }
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 191;
        d.def_id = R"RB(ogn-191-298)RB";
        d.name = R"RB(Maddened Marauder)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-191/298)RB";
        d.collector_number = 191;
        d.artist = R"RB(Alex Heath)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Pirate)RB", R"RB(Bilgewater)RB"};
        d.energy_cost = 5;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Tank);
        d.ability_text = R"RB([Tank] (I must be assigned combat damage first.)
When you play me, move a unit from a battlefield to its base.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/0f3a2ab8e5894e0dd1b625ee29f3304e7832ef9a-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_191(CardRegistry& r) {
    r.registerCard(191, std::make_unique<MaddenedMarauder>());
}

} // namespace riftbound
