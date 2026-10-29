#include "cards/card.h"
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

class BattlePreparations : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true};
    }
    bool needsPlayTimeTarget() const override { return true; }
    // "[Show Off] a gear. Give a unit +2 [M] this turn. If you showed off,
    //  ready up to 2 runes." Showing off costs nothing but information, which
    // the engine does not model, so the player shows off whenever they can:
    // a gear in hand or a friendly gear on the board.
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId picked = pickTarget(ctx, "Battle Preparations",
                                         enumerateLegalTargets(ctx.state, ctx.controller));
        if (picked == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (picked != kInvalidId && ctx.state.objectExists(picked))
            ctx.executor.giveTemporaryMight(picked, 2);
        if (!canShowOffAGear(ctx)) return;
        int readied = 0;
        for (auto& [id, obj] : ctx.state.objects) {
            if (readied == 2) break;
            if (!obj.isRune() || obj.controller != ctx.controller || !obj.is_exhausted) continue;
            if (!obj.location.has_value()) continue;
            ctx.executor.readyObject(id);
            ++readied;
        }
        ctx.events.logTrace("BATTLE PREPARATIONS: showed off a gear, readied " +
                            std::to_string(readied) + " rune(s)");
    }
private:
    static bool canShowOffAGear(const CardContext& ctx) {
        for (auto cid : ctx.state.player(ctx.controller).hand)
            if (ctx.state.objectExists(cid) && ctx.state.getObject(cid).isGear()) return true;
        for (const auto& [id, obj] : ctx.state.objects)
            if (obj.isGear() && obj.controller == ctx.controller && obj.location.has_value())
                return true;
        return false;
    }
    // RAD-032 is absent from cards/json and cards/card_index.json; the text is
    // the published card text. The Power cost is assumed to be 0.
    const CardDef def_ = [] {
        CardDef d;
        d.id = 968;
        d.def_id = R"RB(rad-032-167)RB";
        d.name = R"RB(Battle Preparations)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-032/167)RB";
        d.collector_number = 32;
        d.card_type = CardType::Spell;
        d.domains = {Domain::Calm};
        d.energy_cost = 2;
        d.rarity = Rarity::Uncommon;
        d.keywords.set(Keyword::Reaction);
        d.ability_text = R"RB([Reaction] (Play any time, even before spells and abilities resolve.)
[Show Off] a gear. (As you play this, you may reveal a gear from hand or pick a friendly one.)
Give a unit +2 [M] this turn. If you showed off, ready up to 2 runes.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_968(CardRegistry& r) {
    r.registerCard(968, std::make_unique<BattlePreparations>());
}

}  // namespace riftbound
