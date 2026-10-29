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

// RECONSTRUCTED TEXT: this card is not in cards/card_index.json. Its data and
// ability text were reconstructed from web-search summaries (confidence:
// medium). Notes: Power cost — one (Chinese) wiki says 0, the others list
// none (null, taken as 0). RiftRef ruling: the opponent must pay the move
// cost; if they can't or won't, the unit doesn't move.
class AnglerWyrm : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "[E]: Choose an opponent. They must move a unit they control to my
    //  battlefield."
    // 1v1: the opponent is chosen implicitly. As the ability resolves the
    // OPPONENT picks one of their units that is not already at my battlefield
    // (and that enemy effects may move) and it moves here. Offered only while
    // I'm at a battlefield (at base there is no "my battlefield" and the
    // ability would do nothing).
    // APPROXIMATION: the ruling's "the opponent must pay the move cost" is
    // not modeled — the engine has no cost for an effect-driven move, so the
    // move always happens when they have a unit to move.
    static constexpr int kAwaitUnit = 300;   // clear of the Card::pick* resume points
    static constexpr int kDone = 301;

    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility ab;
        ab.cost.exhaust = true;
        return {ab};
    }
    bool canActivateAbilityAt(const GameState& state, PlayerId controller,
                              GameObjectId source, int ability_index) const override {
        if (!Card::canActivateAbilityAt(state, controller, source, ability_index)) return false;
        return state.objectExists(source) && state.getObject(source).isAtBattlefield();
    }
    void onActivate(CardContext& ctx, int /*ability_index*/,
                    const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        const auto here = ctx.state.getObject(ctx.source).battlefieldId();
        if (!here.has_value()) return;
        const PlayerId opp = opponent(ctx.controller);
        const auto movable = movableUnits(ctx.state, opp, *here);

        GameObjectId picked = kInvalidId;
        auto& ri_opt = ctx.state.chain.resuming;
        if (!ri_opt.has_value()) {
            if (movable.empty()) return;
            picked = ctx.executor.chooseObjectNow(opp, movable);
        } else {
            auto& ri = *ri_opt;
            if (ri.resume_point == kDone) return;
            if (ri.resume_point != kAwaitUnit) {
                if (movable.empty()) { ri.resume_point = kDone; return; }
                std::vector<Intent> opts;
                for (auto id : movable) opts.push_back(objChoice(opp, id));
                ctx.executor.requestChoice(opp, std::move(opts),
                                           "Angler Wyrm: move a unit to its battlefield");
                ri.resume_point = kAwaitUnit;
                return;
            }
            ri.resume_point = kDone;
            picked = takenObject(ctx);
            if (std::find(movable.begin(), movable.end(), picked) == movable.end())
                picked = movable.empty() ? kInvalidId : movable.front();   // they must move one
        }
        if (picked == kInvalidId || !ctx.state.objectExists(picked)) return;
        ctx.executor.moveToBattlefield(picked, *here);
        ctx.events.logTrace("ANGLER WYRM: " + ctx.state.getObject(picked).name +
                            " moved to BF#" + std::to_string(static_cast<int>(*here)));
    }
private:
    static std::vector<GameObjectId> movableUnits(const GameState& state, PlayerId opp,
                                                  BattlefieldId here) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects) {
            if (!obj.isUnit() || obj.controller != opp || !obj.location.has_value()) continue;
            if (obj.battlefieldId() == std::optional<BattlefieldId>{here}) continue;
            if (obj.cant_be_moved_by_enemy) continue;
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1123;
        d.def_id = R"RB(rad-113-167)RB";
        d.name = R"RB(Angler Wyrm)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-113/167)RB";
        d.collector_number = 113;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Bilgewater)RB", R"RB(Dragon)RB"};
        d.energy_cost = 8;
        d.power_cost = 0;   // unknown (null)
        d.might = 8;
        d.rarity = Rarity::Epic;
        d.deflect_value = 1;
        d.keywords.set(Keyword::Deflect);
        d.ability_text = R"RB([Deflect] (Opponents must pay [A] to choose me with a spell or ability.)
[E]: Choose an opponent. They must move a unit they control to my battlefield.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1123(CardRegistry& r) {
    r.registerCard(1123, std::make_unique<AnglerWyrm>());
}

}  // namespace riftbound
