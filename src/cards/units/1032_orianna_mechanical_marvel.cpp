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

class OriannaMechanicalMarvel : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "Once each turn, while I'm at a battlefield, you may pay [1][A] less to
    //  play a gear."
    //
    // Published as a PlayerState::CostModifier (gear_only, [1] Energy and one
    // Power of any domain off) while I'm at a battlefield and the discount is
    // unused this turn. APPROXIMATIONS:
    //   * the "you may" is always taken: the discount applies to the next gear
    //     my controller plays while it is up (never worse, except that it
    //     can't be saved for a later gear this turn);
    //   * the modifier is next_card_only, so the engine drops it after any
    //     card's cost payment and this aura re-publishes it at the next
    //     cleanup unless a gear was played while it was up (counted through
    //     PlayerState::gears_played_this_turn, which only plays from hand /
    //     champion zone bump — a gear played by an effect with the discount
    //     doesn't use it up);
    //   * cost modifiers are not cleared by the aura pass, so each instance
    //     removes the one it published before re-publishing; if I leave the
    //     board the last one stays until the next card is paid for or the
    //     turn ends.
    static constexpr const char* kTurnKey  = "__orianna_mm_turn";
    static constexpr const char* kUsedKey  = "__orianna_mm_used";
    static constexpr const char* kGearsKey = "__orianna_mm_gears";
    static constexpr const char* kUpKey    = "__orianna_mm_up";

    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        auto& ps = state.player(controller);
        const int turn = state.turn.turn_number;
        for (auto& [sid, self] : state.objects) {
            if (self.card_def_id != cardDefId() || self.controller != controller) continue;
            if (!self.location.has_value()) continue;
            auto& cc = self.card_counters;
            if (cc[kTurnKey] != turn) {
                cc[kTurnKey] = turn;
                cc[kUsedKey] = 0;
                cc[kUpKey] = 0;
                cc[kGearsKey] = ps.gears_played_this_turn;
            }
            // Was it up while a gear was played since the last pass?
            if (cc[kUpKey] && ps.gears_played_this_turn > cc[kGearsKey]) cc[kUsedKey] = 1;
            cc[kGearsKey] = ps.gears_played_this_turn;
            const GameObjectId me = sid;
            ps.cost_modifiers.erase(
                std::remove_if(ps.cost_modifiers.begin(), ps.cost_modifiers.end(),
                               [&](const PlayerState::CostModifier& m) { return m.source == me; }),
                ps.cost_modifiers.end());
            cc[kUpKey] = 0;
            if (!self.isAtBattlefield() || cc[kUsedKey]) continue;
            PlayerState::CostModifier m;
            m.source = me;
            m.energy_reduction = 1;
            m.power_reduction = 1;
            m.gear_only = true;
            m.affects_non_token_only = true;
            m.next_card_only = true;
            m.this_turn_only = true;
            ps.cost_modifiers.push_back(m);
            cc[kUpKey] = 1;
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1032;
        d.def_id = R"RB(rad-068-167)RB";
        d.name = R"RB(Orianna, Mechanical Marvel)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-068/167)RB";
        d.collector_number = 68;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Piltover)RB", R"RB(Orianna)RB"};
        d.energy_cost = 5;
        d.might = 5;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(Once each turn, while I'm at a battlefield, you may pay [1][A] less to play a gear.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/00f5a081a085e9c3851daf79d9e39bd378f71fe3-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1032(CardRegistry& r) {
    r.registerCard(1032, std::make_unique<OriannaMechanicalMarvel>());
}

}  // namespace riftbound
