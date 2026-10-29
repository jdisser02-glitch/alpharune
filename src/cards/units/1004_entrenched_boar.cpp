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

class EntrenchedBoar : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "You may have me enter ready. If you do, I can't move this turn."
    // The choice is made as I'm played, through the engine's optional
    // additional-cost prompt with a cost of nothing (yes/no, after my cost is
    // paid); onPlay turns the answer into "enters ready this turn", which
    // entersReadyOnPlay reads when I land.
    //
    // PARTIAL: "I can't move this turn" — the engine has no per-unit "can't
    // move this turn" restriction. The restriction is recorded as
    // card_counters["__cant_move_turn"] = turn + 1 (the Hunker Down
    // convention, for an engine check to honour), and while I'm at a
    // battlefield that turn the move to base is blocked through
    // GameObject::cant_move_to_base (applyPassiveAura sets it and clears it
    // afterwards). A ready Boar at base can still move to a battlefield.
    OptionalAdditionalCost optionalAdditionalCost() const override {
        OptionalAdditionalCost c;
        c.valid = true;
        c.paid_flag = kChoseReady;
        return c;
    }
    void onPlay(CardContext& ctx) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& self = ctx.state.getObject(ctx.source);
        const int turn = ctx.state.turn.turn_number;
        const bool ready = self.card_counters[kChoseReady] == 1;
        self.card_counters[kChoseReady] = 0;   // the flag is only ever set, never cleared, by the engine
        self.card_counters[kReadyTurn] = ready ? turn : -1;
        if (ready) {
            self.card_counters[kCantMoveKey] = turn + 1;
            ctx.events.logTrace("ENTRENCHED BOAR: enters ready, can't move this turn");
        } else {
            self.card_counters.erase(kCantMoveKey);
        }
    }
    using Card::entersReadyOnPlay;
    bool entersReadyOnPlay(const GameState& state, PlayerId /*controller*/,
                           const GameObject& self) const override {
        auto it = self.card_counters.find(kReadyTurn);
        return it != self.card_counters.end() && it->second == state.turn.turn_number;
    }

    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        const int turn = state.turn.turn_number;
        for (auto& [id, self] : state.objects) {
            if (self.card_def_id != cardDefId() || self.controller != controller) continue;
            if (!self.location.has_value()) continue;
            auto rt = self.card_counters.find(kReadyTurn);
            const bool locked = rt != self.card_counters.end() && rt->second == turn &&
                                self.isAtBattlefield();
            auto mark = self.card_counters.find(kSetCantMoveToBase);
            const bool marked = mark != self.card_counters.end() && mark->second == 1;
            if (locked && !self.cant_move_to_base) {
                self.cant_move_to_base = true;
                self.card_counters[kSetCantMoveToBase] = 1;
            } else if (!locked && marked) {
                self.cant_move_to_base = false;   // only undo what this card set
                self.card_counters[kSetCantMoveToBase] = 0;
            }
        }
    }
private:
    static constexpr const char* kChoseReady = "__boar_chose_ready";
    static constexpr const char* kReadyTurn = "__boar_ready_turn";
    static constexpr const char* kCantMoveKey = "__cant_move_turn";
    static constexpr const char* kSetCantMoveToBase = "__boar_set_cant_move_to_base";

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1004;
        d.def_id = R"RB(rad-026-167)RB";
        d.name = R"RB(Entrenched Boar)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-026/167)RB";
        d.collector_number = 26;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.energy_cost = 2;
        d.might = 2;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(You may have me enter ready. If you do, I can't move this turn.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/8d8688f43e219f7f4b5f5fd17cff204dcebc3bdc-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1004(CardRegistry& r) {
    r.registerCard(1004, std::make_unique<EntrenchedBoar>());
}

}  // namespace riftbound
