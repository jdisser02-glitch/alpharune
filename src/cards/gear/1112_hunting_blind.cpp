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

// RECONSTRUCTED TEXT (not in cards/card_index.json; confidence: medium).
// Gathered from web-search summaries (WebFetch to card sites was blocked);
// ability_text written in the repo's symbol convention with standard reminder
// text. Card type conflict: most sources say Gear (Device subtype, cost
// '4CC'); one summary of riftbound.org called it a Unit — Gear accepted.
// Whether the enemy move is optional ('you may') is from paraphrase — verify
// exact wording. No image_url / artist.
class HuntingBlind : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When you play this, you may move an enemy unit to here."
    // The "you may" and the enemy unit (from anywhere on the board but here)
    // are chosen as the ability is finalized.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayThis; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        int go = confirmOptional(ctx, "Hunting Blind: move an enemy unit to here?",
                                 [&]() { return !enemiesElsewhere(ctx).empty(); });
        GameObjectId victim = kInvalidId;
        if (go == 1) victim = pickTarget(ctx, "Hunting Blind: enemy unit to move here",
                                         enemiesElsewhere(ctx));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (go != 1) return;
        if (victim == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;                           // waiting for the target
        const auto here = hereOf(ctx);
        if (victim == kInvalidId || !ctx.state.objectExists(victim) || !here.has_value()) return;
        ctx.executor.moveToBattlefield(victim, *here);
        ctx.events.logTrace("HUNTING BLIND: moved " + ctx.state.getObject(victim).name + " here");
    }

    // "Friendly units here have [Shield 2]."
    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        forEachSelfOnce(*this, state, controller, [&](GameObjectId sid, GameObject& self) {
            const auto here = self.battlefieldId();
            if (!here.has_value()) return;
            for (auto& [uid, u] : state.objects)
                if (u.isUnit() && u.controller == controller && u.battlefieldId() == here)
                    auraKeyword(u, sid, Keyword::Shield, 2);
        });
    }
private:
    static std::optional<BattlefieldId> hereOf(const CardContext& ctx) {
        if (!ctx.state.objectExists(ctx.source)) return std::nullopt;
        return ctx.state.getObject(ctx.source).battlefieldId();
    }
    static std::vector<GameObjectId> enemiesElsewhere(const CardContext& ctx) {
        const auto here = hereOf(ctx);
        if (!here.has_value()) return {};
        auto out = chooseableUnits(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller != ctx.controller && o.battlefieldId() != here;
        });
        std::sort(out.begin(), out.end());
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1112;
        d.def_id = R"RB(rad-044-167)RB";
        d.name = R"RB(Hunting Blind)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-044/167)RB";
        d.collector_number = 44;
        d.card_type = CardType::Gear;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 4;
        d.power_cost = 2;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
When you play this, you may move an enemy unit to here.
Friendly units here have [Shield 2]. (+2 [M] while they're defenders.))RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1112(CardRegistry& r) {
    r.registerCard(1112, std::make_unique<HuntingBlind>());
}

}  // namespace riftbound
