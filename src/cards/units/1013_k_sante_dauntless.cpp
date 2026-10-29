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

class KSanteDauntless : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    /// A unit's total [Shield] value: its own (printed + "this turn") plus
    /// every aura-granted instance (CR 809.2-style summing; an aura instance
    /// without a value is [Shield 1]).
    static int shieldOf(const GameObject& u) {
        int v = std::max(0, u.shield_value);
        for (const auto& ae : u.aura_effects)
            if (ae.keyword == Keyword::Shield) v += std::max(1, ae.keyword_value);
        if (v == 0 && u.hasKeyword(Keyword::Shield)) v = 1;
        return v;
    }

    // "When I attack or defend, double the [Shield] of a unit you control here
    //  this turn." The unit is chosen at finalization among the units you
    // control here that have [Shield] (doubling 0 does nothing; K'Sante
    // himself always qualifies). Doubling = giving it [Shield X] this turn,
    // where X is its Shield as the ability resolves.
    TriggerType triggerType() const override { return TriggerType::WhenIAttackOrDefend; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        const auto here = ctx.state.getObject(ctx.source).battlefieldId();
        std::vector<GameObjectId> mine;
        if (here.has_value()) {
            for (const auto& [id, obj] : ctx.state.objects)
                if (obj.isUnit() && obj.controller == ctx.controller &&
                    obj.battlefieldId() == here && shieldOf(obj) > 0)
                    mine.push_back(id);
            std::sort(mine.begin(), mine.end());
        }
        GameObjectId t = pickTarget(ctx, "K'Sante, Dauntless: double a unit's [Shield]", mine);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (t == kInvalidId || !ctx.state.objectExists(t)) return;
        const auto& u = ctx.state.getObject(t);
        if (!u.location.has_value()) return;
        const int s = shieldOf(u);
        if (s <= 0) return;
        ctx.executor.giveTemporaryKeyword(t, Keyword::Shield, s);
        ctx.events.logTrace("K'SANTE, DAUNTLESS: doubled " + u.name + "'s [Shield] (" +
                            std::to_string(s) + " -> " + std::to_string(2 * s) + ") this turn");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1013;
        d.def_id = R"RB(rad-045-167)RB";
        d.name = R"RB(K'Sante, Dauntless)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-045/167)RB";
        d.collector_number = 45;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Shurima)RB", R"RB(K'Sante)RB"};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Epic;
        d.shield_value = 1;
        d.keywords.set(Keyword::Shield);
        d.ability_text = R"RB([Shield] (+1 [M] while I'm a defender.)
When I attack or defend, double the [Shield] of a unit you control here this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/f56e9f9db199f915e32ea264b7dab70202f97957-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1013(CardRegistry& r) {
    r.registerCard(1013, std::make_unique<KSanteDauntless>());
}

}  // namespace riftbound
