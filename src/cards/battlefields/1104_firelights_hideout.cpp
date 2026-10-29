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

class FirelightsHideout : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you conquer here, the next unit you play this turn has
    //  [Accelerate]."
    // The engine has no "next unit you play" keyword grant, and a battlefield
    // card is not asked Card::grantsAccelerateTo (only on-board cards are). It
    // does honour a per-card "__granted_accelerate" mark when a unit resolves
    // (the usual optional [1] + Power, CR 805), so the conquer arms the grant
    // and this battlefield's passive pass keeps that mark on every unit the
    // player could play next — in hand, in the Champion Zone, face down — until
    // one of them is played (it reaches the board: the grant is spent and the
    // others lose the mark) or the turn ends. Tokens are not counted as "the
    // next unit you play".
    TriggerType triggerType() const override { return TriggerType::WhenYouConquerHere; }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (finalizingChoices(ctx)) return;   // no choices
        if (!ctx.state.objectExists(ctx.source)) return;
        ctx.state.getObject(ctx.source).card_counters[armKey(ctx.controller)] =
            ctx.state.turn.turn_number;
        refresh(ctx.state, ctx.source);
        ctx.events.logTrace("FIRELIGHTS' HIDEOUT: conquered -> the next unit played this turn has [Accelerate]");
    }

    void applyPassiveAura(GameState& state, PlayerId /*controller*/) const override {
        for (const auto& bf : state.battlefields) {
            if (!state.objectExists(bf.card_object_id)) continue;
            if (state.getObject(bf.card_object_id).card_def_id != cardDefId()) continue;
            refresh(state, bf.card_object_id);
        }
    }

private:
    static constexpr const char* kMark = "__firelights_accelerate";
    static constexpr const char* kGrant = "__granted_accelerate";   // read by resolvePermanent

    static std::string armKey(PlayerId p) {
        return p == PlayerId::Player1 ? "__firelights_armed_p1" : "__firelights_armed_p2";
    }

    static void refresh(GameState& st, GameObjectId bf_card) {
        if (!st.objectExists(bf_card)) return;
        const int tag = static_cast<int>(bf_card);
        for (PlayerId p : {PlayerId::Player1, PlayerId::Player2}) {
            auto& counters = st.getObject(bf_card).card_counters;
            auto arm_it = counters.find(armKey(p));
            bool armed = arm_it != counters.end() && arm_it->second == st.turn.turn_number;
            auto& ps = st.player(p);

            // 1. A marked unit that is on the board, or on its way there (its
            //    destination is set as it is played), was the next unit played:
            //    the grant is spent. Its own mark is left for resolvePermanent.
            for (auto& [id, o] : st.objects) {
                if (!o.isUnit() || o.owner != p) continue;
                auto it = o.card_counters.find(kMark);
                if (it == o.card_counters.end() || it->second != tag) continue;
                if (!o.location.has_value() || o.is_hidden) continue;
                o.card_counters.erase(it);
                if (armed) {
                    st.getObject(bf_card).card_counters[armKey(p)] = -1;
                    armed = false;
                }
            }

            // 2. Keep the grant on every unit that could be the next one
            //    played; take it off the rest.
            for (auto& [id, o] : st.objects) {
                if (!o.isUnit() || o.owner != p) continue;
                const bool in_hand = o.zone == ZoneType::Hand &&
                    std::find(ps.hand.begin(), ps.hand.end(), id) != ps.hand.end();
                const bool candidate = !o.isToken() && o.card_def_id != kInvalidId &&
                    (in_hand || ps.champion_zone == id ||
                     (o.is_hidden && o.controller == p));
                auto it = o.card_counters.find(kMark);
                const bool marked = it != o.card_counters.end() && it->second == tag;
                if (armed && candidate) {
                    o.card_counters[kMark] = tag;
                    o.card_counters[kGrant] = 1;
                } else if (marked) {
                    o.card_counters.erase(kMark);
                    o.card_counters.erase(kGrant);
                }
            }
        }
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1104;
        d.def_id = R"RB(rad-162-167)RB";
        d.name = R"RB(Firelights' Hideout)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-162/167)RB";
        d.collector_number = 162;
        d.artist = R"RB(Grafit Studio)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you conquer here, the next unit you play this turn has [Accelerate]. (You may pay [1] and a Power of its Domain as an additional cost to have it enter ready.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/f8e2d555ff0dc250de38a1c8924a199dc121f036-1039x744.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1104(CardRegistry& r) {
    r.registerCard(1104, std::make_unique<FirelightsHideout>());
}

}  // namespace riftbound
