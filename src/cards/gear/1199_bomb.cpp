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
#include "cards/radiance_helpers.h"

namespace riftbound {
namespace {

// The Bomb gear token (Radiance). Every card that "plays a Bomb gear token"
// creates it through playBombToken (radiance_helpers.h), which binds the token
// to this class: "(It has [Deploy]. When it dies, deal 2 to an enemy there and
// kill your other Bombs there.)"
// PROPOSED id kBombTokenCardDefId (no official token number is known).
class RadianceBombToken : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When it dies, deal 2 to an enemy there and kill your other Bombs
    //  there." "There" is the battlefield it died at (last_location). The enemy
    // unit is chosen as the ability is finalized. The other Bombs are collected
    // first and then killed, each through killWithDeathTrigger so their own
    // death abilities trigger too.
    // ENGINE-GAP (see radiance_helpers.h): a gear's death does not trigger by
    // itself; only kills made through killWithDeathTrigger fire this.
    TriggerType triggerType() const override { return TriggerType::WhenIDie; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) { if (finalizingChoices(ctx)) return; return; }
        const auto& self = ctx.state.getObject(ctx.source);
        std::optional<BattlefieldId> there = self.battlefieldId();
        if (!there && self.last_location.has_value() &&
            std::holds_alternative<BattlefieldLocation>(*self.last_location))
            there = std::get<BattlefieldLocation>(*self.last_location).id;
        std::vector<GameObjectId> enemies;
        if (there) {
            enemies = chooseableUnits(ctx.state, ctx.controller, [&](const GameObject& o) {
                return o.controller != ctx.controller && o.battlefieldId() == there;
            });
        }
        const GameObjectId victim = pickTarget(ctx, "Bomb: deal 2 to an enemy there", enemies);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (victim == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the agent
        if (!there) return;
        if (victim != kInvalidId && ctx.state.objectExists(victim)) {
            ctx.executor.dealDamage(victim, 2, ctx.source);
            ctx.events.logTrace("BOMB: 2 damage to " + ctx.state.getObject(victim).name);
        }
        std::vector<GameObjectId> others;
        for (const auto& [id, obj] : ctx.state.objects)
            if (id != ctx.source && isBomb(obj) && obj.controller == ctx.controller &&
                obj.battlefieldId() == there)
                others.push_back(id);
        for (auto id : others) killWithDeathTrigger(ctx, id);
        if (!others.empty())
            ctx.events.logTrace("BOMB: killed " + std::to_string(others.size()) + " other Bomb(s) there");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = kBombTokenCardDefId;
        d.def_id = R"RB(rad-token-bomb)RB";
        d.name = R"RB(Bomb)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.card_type = CardType::Gear;
        d.super_type = SuperType::Token;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
When this dies, deal 2 to an enemy there and kill your other Bombs there.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1199(CardRegistry& r) {
    r.registerCard(kBombTokenCardDefId, std::make_unique<RadianceBombToken>());
}

}  // namespace riftbound
