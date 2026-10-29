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

class BedevilingStranger : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play me, choose a battlefield. If there are 4 or more units
    //  there, you may return an enemy unit there to its owner's hand."
    // The choice of battlefield only matters when the bounce is taken, so the
    // "you may" is asked first (the helpers' resume slots compose in that
    // order) and the battlefield is then picked among those where the bounce
    // can happen. Declining is the same as choosing a battlefield with fewer
    // than 4 units.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto& bfs = ctx.state.battlefields;
        const int n = static_cast<int>(std::min<size_t>(bfs.size(), 32));
        auto mask = [&]() {
            uint32_t m = 0;
            for (int i = 0; i < n; ++i)
                if (unitsAt(ctx.state, bfs[i].id) >= 4 && !enemiesAt(ctx, bfs[i].id).empty())
                    m |= (1u << i);
            return m;
        };
        int go = confirmOptional(ctx, "Bedeviling Stranger: return an enemy unit to hand?",
                                 [&]() { return mask() != 0; });
        int mode = -2;
        GameObjectId victim = kInvalidId;
        if (go == 1) {
            std::vector<std::string> labels;
            for (int i = 0; i < n; ++i)
                labels.push_back(ctx.state.objectExists(bfs[i].card_object_id)
                    ? ctx.state.getObject(bfs[i].card_object_id).name
                    : "BF#" + std::to_string(static_cast<int>(bfs[i].id)));
            mode = pickMode(ctx, "Bedeviling Stranger: battlefield", n, labels, mask());
            if (mode >= 0 && mode < n)
                victim = pickTarget(ctx, "Bedeviling Stranger: enemy unit",
                                    enemiesAt(ctx, bfs[mode].id));
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (go != 1 || mode < 0 || mode >= n) return;
        if (victim == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the target
        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        // The count is checked as the ability resolves.
        if (unitsAt(ctx.state, bfs[mode].id) < 4) return;
        auto at = ctx.state.getObject(victim).battlefieldId();
        if (!at.has_value() || *at != bfs[mode].id) return;
        ctx.executor.bounceToHand(victim);
        ctx.events.logTrace("BEDEVILING STRANGER: returned " +
                            ctx.state.getObject(victim).name + " to hand");
    }
private:
    static int unitsAt(const GameState& state, BattlefieldId bf) {
        int k = 0;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.battlefieldId() == bf) ++k;
        return k;
    }
    static std::vector<GameObjectId> enemiesAt(const CardContext& ctx, BattlefieldId bf) {
        return chooseableUnits(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller != ctx.controller && o.battlefieldId() == bf;
        });
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 964;
        d.def_id = R"RB(rad-107-167)RB";
        d.name = R"RB(Bedeviling Stranger)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-107/167)RB";
        d.collector_number = 107;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.energy_cost = 2;
        d.might = 2;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Hidden);
        d.ability_text = R"RB([Hidden] (Hide now for [A] to react with later for [0].)
When you play me, choose a battlefield. If there are 4 or more units there, you may return an enemy unit there to its owner's hand.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/d112a706fe957e5cea58ea1e4366ac02e3ba292b-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_964(CardRegistry& r) {
    r.registerCard(964, std::make_unique<BedevilingStranger>());
}

}  // namespace riftbound
