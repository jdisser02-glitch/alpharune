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

class GravityField : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }   // [Deploy] (CR 830)

    // "Opponents must pay [1][A] to move one or more units to this
    //  battlefield.
    //  [Deathknell][>] Draw 1."
    //
    // APPROXIMATION (engine gap): moves are uncosted — the move generator has
    // no per-destination surcharge, so the opponent can't be asked to pay
    // before moving. The card charges the toll after the fact instead: when
    // an opponent's unit moves to my battlefield (WhenAnOpponentMovesTo-
    // Battlefield), its controller pays [1][A] (Energy from the pool or by
    // exhausting a rune, then [A] from floating Power or by recycling a
    // rune). If they can't pay, the move could not have been made, so the
    // unit is sent back to its controller's base (where it came from for a
    // standard move from base; a [Ganking] move from another battlefield is
    // also undone to base). Differences from the printed text:
    //   * the toll is charged per moved unit (the engine's standard move moves
    //     one unit at a time, so this matches; a multi-unit move by an effect
    //     pays once per unit);
    //   * it also applies to moves made by effects, including moves of the
    //     opponent's units by my own effects;
    //   * it is a triggered ability on the chain, so responses can happen
    //     between the move and the payment.
    //
    // ENGINE GAP: gear deaths are not dispatched to WhenIDie (only units emit
    // UnitDiedEvent; EffectExecutor::killObject on a gear emits only
    // LeftBoardEvent), so the [Deathknell] below never fires in play today.
    // It is declared so it works once the engine wires gear Deathknell.
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenAnOpponentMovesToBattlefield, TriggerType::WhenIDie};
    }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType trigger, GameObjectId subject) const override {
        if (trigger != TriggerType::WhenAnOpponentMovesToBattlefield) return true;
        if (!state.objectExists(source) || !state.objectExists(subject)) return false;
        const auto& self = state.getObject(source);
        const auto& moved = state.getObject(subject);
        const auto here = self.battlefieldId();
        return here.has_value() && moved.controller != controller &&
               moved.battlefieldId() == here;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::WhenIDie) {
            ctx.executor.drawCards(ctx.controller, 1);
            ctx.events.logTrace("GRAVITY FIELD: [Deathknell] draw 1");
            return;
        }
        if (!ctx.state.chain.resuming.has_value()) return;
        const GameObjectId unit = ctx.state.chain.resuming->triggering_subject;
        if (!ctx.state.objectExists(unit) || !ctx.state.objectExists(ctx.source)) return;
        const auto here = ctx.state.getObject(ctx.source).battlefieldId();
        const auto& moved = ctx.state.getObject(unit);
        if (!here.has_value() || moved.battlefieldId() != here) return;   // already gone
        const PlayerId payer = moved.controller;
        if (payer == ctx.controller) return;
        if (payableEnergy(ctx.state, payer) >= 1 && ctx.executor.payableAnyPower(payer) >= 1) {
            payEnergy(ctx, payer, 1);
            ctx.executor.payAnyPower(payer, 1);
            ctx.events.logTrace("GRAVITY FIELD: " + std::string(toString(payer)) +
                                " pays [1][A] for moving " + moved.name + " here");
        } else {
            ctx.events.logTrace("GRAVITY FIELD: " + std::string(toString(payer)) +
                                " can't pay [1][A]; " + moved.name + " goes back to base");
            ctx.executor.moveToBase(unit);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1028;
        d.def_id = R"RB(rad-062-167)RB";
        d.name = R"RB(Gravity Field)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-062/167)RB";
        d.collector_number = 62;
        d.artist = R"RB(Fish Art Studio)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Calm};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Deathknell);
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
Opponents must pay [1][A] to move one or more units to this battlefield.
[Deathknell][>] Draw 1. (When I die, get the effect.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/51f589e5ed0f7316b6cb64ebc93c4ef3e75b7f46-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1028(CardRegistry& r) {
    r.registerCard(1028, std::make_unique<GravityField>());
}

}  // namespace riftbound
