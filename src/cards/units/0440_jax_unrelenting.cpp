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
#include "cards/units/weaponmaster_base.h"

namespace riftbound {
namespace {

class JaxUnrelenting : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // Two triggered abilities:
    //  - [Weaponmaster] "When you play me, you may [Equip] one of your
    //    Equipment to me for [A] less, even if it's already attached."
    //  - "When you attach an Equipment to me, you may pay [1] to draw 1." —
    //    its own ability: it triggers on EVERY attach (Weaponmaster, a later
    //    Equip activation, an effect), not only on the play.
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayMe, TriggerType::WhenEquipmentAttachedToMe};
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        if (ctx.firing_trigger == TriggerType::WhenEquipmentAttachedToMe) {
            // "you may pay [1] to draw 1". CR 445.2 / 164.2.a: the [1] may come
            // from floating Energy or from exhausting a ready rune.
            auto still_legal = [&ctx]() {
                return payableEnergy(ctx.state, ctx.controller) >= 1;
            };
            int conf = confirmOptional(ctx, "Jax, Unrelenting: pay [1] to draw 1?",
                                       still_legal);
            if (conf != 1) return;       // waiting, declined, or can't pay
            if (!payEnergy(ctx, ctx.controller, 1)) return;
            ctx.executor.drawCards(ctx.controller, 1);
            ctx.events.logTrace("JAX, UNRELENTING: paid [1] -> draw 1");
            return;
        }

        // ── [Weaponmaster] (when you play me) ──
        weaponmasterTrigger(ctx, "JAX, UNRELENTING");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 440;
        d.def_id = R"RB(sfd-119-221)RB";
        d.name = R"RB(Jax, Unrelenting)RB";
        d.set_code = R"RB(SFD)RB";
        d.set_name = R"RB(Spiritforged)RB";
        d.public_code = R"RB(SFD-119/221)RB";
        d.collector_number = 119;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Jax)RB", R"RB(Icathia)RB"};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.might = 3;
        d.rarity = Rarity::Epic;
        d.keywords.set(Keyword::Equip);
        d.keywords.set(Keyword::Weaponmaster);
        d.ability_text = R"RB([Weaponmaster] (When you play me, you may [Equip] one of your Equipment to me for [A] less, even if it's already attached.)
When you attach an Equipment to me, you may pay [1] to draw 1.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/e63ab3370a6e4ce4baddfba12ab92468f6cc9541-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_440(CardRegistry& r) {
    r.registerCard(440, std::make_unique<JaxUnrelenting>());
}

} // namespace riftbound
