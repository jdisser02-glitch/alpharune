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

class ForDemacia : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "Choose a battlefield. Give friendly units there [Shield 2] this turn."
    // The battlefield is chosen as the spell is played (pickMode, one mode per
    // battlefield); the friendly units there as it resolves get the keyword.
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto& bfs = ctx.state.battlefields;
        const int n = static_cast<int>(std::min<size_t>(bfs.size(), 32));
        if (n == 0) return;
        std::vector<std::string> labels;
        for (int i = 0; i < n; ++i)
            labels.push_back(ctx.state.objectExists(bfs[i].card_object_id)
                ? ctx.state.getObject(bfs[i].card_object_id).name
                : "BF#" + std::to_string(static_cast<int>(bfs[i].id)));
        const uint32_t all = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
        int mode = pickMode(ctx, "For Demacia!: battlefield", n, labels, all);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (mode < 0 || mode >= n) return;
        const BattlefieldId bf = ctx.state.battlefields[mode].id;
        std::vector<GameObjectId> units;
        for (const auto& [id, obj] : ctx.state.objects)
            if (obj.isUnit() && obj.controller == ctx.controller && obj.battlefieldId() == bf)
                units.push_back(id);
        for (auto id : units) ctx.executor.giveTemporaryKeyword(id, Keyword::Shield, 2);
        ctx.events.logTrace("FOR DEMACIA!: [Shield 2] to " + std::to_string(units.size()) +
                            " friendly unit(s) at BF#" + std::to_string(static_cast<int>(bf)));
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1066;
        d.def_id = R"RB(rad-117-167)RB";
        d.name = R"RB(For Demacia!)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-117/167)RB";
        d.collector_number = 117;
        d.artist = R"RB(Original Force)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Order};
        d.energy_cost = 3;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Reaction);
        d.ability_text = R"RB([Reaction] (Play any time, even before spells and abilities resolve.)
Choose a battlefield. Give friendly units there [Shield 2] this turn. (+2 [M] while they're defenders.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/5d119653fcba423c150815de3a412922c4e481e8-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1066(CardRegistry& r) {
    r.registerCard(1066, std::make_unique<ForDemacia>());
}

}  // namespace riftbound
