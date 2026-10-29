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

class RallyForWar : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "Choose an occupied enemy battlefield. Play three 1 [M] Recruit unit
    //  tokens there." An enemy battlefield is one an opponent controls; it is
    // occupied while a unit is there (CR 170.11.a). The battlefield is chosen
    // as the spell is played; with none to choose the spell can't be played
    // (CR 355 / the "enemy occupied battlefield" example: it fails to
    // finalize). A battlefield that is no longer an occupied enemy battlefield
    // when the spell resolves is a mistarget (no tokens).
    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        for (const auto& bf : state.battlefields)
            if (occupiedEnemy(state, controller, bf)) return true;
        return false;
    }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto& bfs = ctx.state.battlefields;
        const int n = static_cast<int>(std::min<size_t>(bfs.size(), 32));
        uint32_t legal = 0;
        std::vector<std::string> labels;
        for (int i = 0; i < n; ++i) {
            labels.push_back("Recruits to BF#" + std::to_string(bfs[i].id));
            if (occupiedEnemy(ctx.state, ctx.controller, bfs[i])) legal |= (1u << i);
        }
        const int m = pickMode(ctx, "Rally for War: occupied enemy battlefield", n, labels, legal);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (m < 0 || m >= n) return;          // waiting for the agent / nothing to choose
        if (!occupiedEnemy(ctx.state, ctx.controller, bfs[m])) {
            ctx.events.logTrace("RALLY FOR WAR: BF#" + std::to_string(bfs[m].id) +
                                " is no longer an occupied enemy battlefield");
            return;
        }
        const BattlefieldId bf = bfs[m].id;
        for (int i = 0; i < 3; ++i) {
            auto tok = ctx.executor.createToken(ctx.controller, CardType::Unit, "Recruit", 1,
                                                {"Recruit"}, KeywordSet{},
                                                LocationId{BattlefieldLocation{bf}}, false);
            if (ctx.state.objectExists(tok)) ctx.state.getObject(tok).card_def_id = kRecruitDefId;
        }
        ctx.events.logTrace("RALLY FOR WAR: three 1 [M] Recruits to BF#" + std::to_string(bf));
    }
private:
    static constexpr CardDefId kRecruitDefId = 954;   // the Recruit unit token card
    static bool occupiedEnemy(const GameState& state, PlayerId me, const BattlefieldState& bf) {
        if (!bf.controller.has_value() || *bf.controller == me || *bf.controller == PlayerId::None)
            return false;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.battlefieldId() == std::optional<BattlefieldId>{bf.id}) return true;
        return false;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 990;
        d.def_id = R"RB(rad-007-167)RB";
        d.name = R"RB(Rally for War)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-007/167)RB";
        d.collector_number = 7;
        d.artist = R"RB(Wild Blue Studios)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Fury};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(Choose an occupied enemy battlefield. Play three 1 [M] Recruit unit tokens there.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/09c0fc35d8f4b72f58a95d1745b99c4133d02513-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_990(CardRegistry& r) {
    r.registerCard(990, std::make_unique<RallyForWar>());
}

}  // namespace riftbound
