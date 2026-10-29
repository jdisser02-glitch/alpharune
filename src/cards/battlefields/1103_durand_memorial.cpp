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

class DurandMemorial : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you defend here, choose a unit you control here. Give it [Tank]
    //  this combat."
    // The unit is chosen as the ability is finalized. APPROXIMATION: the
    // engine has no "this combat" duration, so [Tank] lasts this turn (it only
    // matters in combat; a second combat here this turn would still see it).
    TriggerType triggerType() const override { return TriggerType::WhenYouDefendHere; }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto here = battlefieldOfCard(ctx.state, ctx.source);
        std::vector<GameObjectId> mine;
        if (here.has_value())
            for (const auto& [id, o] : ctx.state.objects)
                if (o.isUnit() && o.controller == ctx.controller && o.battlefieldId() == here)
                    mine.push_back(id);
        GameObjectId unit = pickTarget(ctx, "Durand Memorial: give a unit here [Tank]", mine);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;
        const auto& u = ctx.state.getObject(unit);
        if (!u.isUnit() || u.controller != ctx.controller || u.battlefieldId() != here) return;
        ctx.executor.giveTemporaryKeyword(unit, Keyword::Tank, 0);
        ctx.events.logTrace("DURAND MEMORIAL: " + u.name + " has [Tank] this combat");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1103;
        d.def_id = R"RB(rad-161-167)RB";
        d.name = R"RB(Durand Memorial)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-161/167)RB";
        d.collector_number = 161;
        d.artist = R"RB(Fish Art Studio)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you defend here, choose a unit you control here. Give it [Tank] this combat. (It must be assigned combat damage first.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b565fec4e11308f5f01416a1c914d50a1d17ca19-1039x744.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1103(CardRegistry& r) {
    r.registerCard(1103, std::make_unique<DurandMemorial>());
}

}  // namespace riftbound
