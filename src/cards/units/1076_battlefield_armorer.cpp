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

class BattlefieldArmorer : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "When a friendly gear at a battlefield dies, you may pay [1] to return it
    //  to its owner's hand."
    //
    // APPROXIMATION (engine gap): a gear dying emits only LeftBoardEvent, which
    // TriggerManager does not dispatch, and there is no "a friendly gear dies"
    // TriggerType. The card detects the death itself: every aura pass (each
    // Cleanup) it remembers the friendly non-token gear at battlefields
    // (GameObject::tracked_objects); a remembered gear that is now in a trash
    // died, and the card puts its triggered ability on the chain directly
    // (the same chain item ChainManager::addAbility builds, with the gear as
    // triggering_subject). So the ability goes on the chain at the Cleanup
    // after the death rather than at the death itself; a gear that dies while
    // the Armorer leaves the board at the same time does not trigger it. Gear
    // tokens are ignored: they cease to exist and can't return to hand.
    // The [1] is paid as the ability is finalized (CR 383.3).
    void onPlay(CardContext& ctx) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& self = ctx.state.getObject(ctx.source);
        self.tracked_objects.clear();          // a fresh presence: forget old gear
        self.card_counters["__bfa_watching"] = 0;
    }

    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        forEachSelfOnce(*this, state, controller, [&](GameObjectId sid, GameObject& self) {
            std::vector<GameObjectId> now;
            for (const auto& [id, obj] : state.objects)
                if (obj.isGear() && !obj.isToken() && obj.controller == controller &&
                    obj.isAtBattlefield())
                    now.push_back(id);
            std::sort(now.begin(), now.end());
            if (self.card_counters["__bfa_watching"] == 1) {
                for (auto g : self.tracked_objects) {
                    if (std::binary_search(now.begin(), now.end(), g)) continue;
                    if (!state.objectExists(g)) continue;
                    const auto& gear = state.getObject(g);
                    if (gear.location.has_value() || gear.zone != ZoneType::Trash) continue;
                    if (gear.controller != controller) continue;
                    queueReturnTrigger(state, sid, controller, g);
                }
            }
            self.tracked_objects = std::move(now);
            self.card_counters["__bfa_watching"] = 1;
        });
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const GameObjectId gear = ctx.state.chain.resuming.has_value()
            ? ctx.state.chain.resuming->triggering_subject : kInvalidId;
        auto in_trash = [&]() {
            if (gear == kInvalidId || !ctx.state.objectExists(gear)) return false;
            const auto& g = ctx.state.getObject(gear);
            if (g.zone != ZoneType::Trash) return false;
            const auto& trash = ctx.state.player(g.owner).trash;
            return std::find(trash.begin(), trash.end(), gear) != trash.end();
        };
        auto can_do = [&]() {
            return in_trash() &&
                   (costAlreadyPaid(ctx) || payableEnergy(ctx.state, ctx.controller) >= 1);
        };
        if (!can_do()) { if (finalizingChoices(ctx)) return; return; }
        const std::string gname = ctx.state.getObject(gear).name;
        int go = confirmOptional(ctx, "Battlefield Armorer: pay [1] to return " + gname +
                                      " to its owner's hand?", can_do);
        if (go == -1) return;                 // waiting for the agent
        if (go == 1 && costDueNow(ctx) && !payEnergy(ctx, ctx.controller, 1)) return;
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (go != 1 || !in_trash()) return;

        auto& g = ctx.state.getObject(gear);
        auto& ops = ctx.state.player(g.owner);
        ops.trash.erase(std::remove(ops.trash.begin(), ops.trash.end(), gear), ops.trash.end());
        g.zone = ZoneType::Hand;
        g.location = std::nullopt;
        g.controller = g.owner;
        ops.hand.push_back(gear);
        ctx.events.logTrace("BATTLEFIELD ARMORER: paid [1] -> " + g.name + " returned to hand");
    }
private:
    static void queueReturnTrigger(GameState& state, GameObjectId self, PlayerId controller,
                                   GameObjectId gear) {
        ChainItem item;
        item.id = state.chain.allocateId();
        item.status = ChainItemStatus::Pending;
        item.source = self;
        item.card_def_id = 1076;
        item.controller = controller;
        item.is_ability = true;
        item.fired_trigger = TriggerType::Passive;
        item.triggering_subject = gear;
        state.chain.items.push_back(std::move(item));
        state.turn.oc_state = OpenClosedState::Closed;   // as ChainManager::addAbility
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1076;
        d.def_id = R"RB(rad-130-167)RB";
        d.name = R"RB(Battlefield Armorer)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-130/167)RB";
        d.collector_number = 130;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Order};
        d.energy_cost = 3;
        d.might = 3;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When a friendly gear at a battlefield dies, you may pay [1] to return it to its owner's hand.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/eb04c39763de8fb8e56153d1f0e4f799e15dced9-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1076(CardRegistry& r) {
    r.registerCard(1076, std::make_unique<BattlefieldArmorer>());
}

}  // namespace riftbound
