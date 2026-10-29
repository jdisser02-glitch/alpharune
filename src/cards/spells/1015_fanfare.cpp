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

class Fanfare : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "The next three times you play a unit this turn, draw 1."
    //
    // DEVIATION (engine gap): the engine has no "this turn, when you play a
    // unit" effect that a resolved spell can leave behind — DelayedAbility is
    // only consulted for a few trigger types (not WhenYouPlayAUnit /
    // WhenYouPlayACard), and a spell in the trash receives no triggers. The
    // card models the effect itself with an EFFECT ANCHOR: as it resolves it
    // creates a stand-in object (card_def_id = Fanfare, zone = LegendZone, no
    // location, a token) that carries the remaining uses and the turn. The
    // trigger sweep for "When you play a card" visits Legend Zone objects, so
    // the anchor's WhenYouPlayACard ability triggers for each unit its
    // controller plays; triggerCondition limits it to units, this turn, while
    // uses remain. Each resolution draws 1 and spends a use; the anchor is
    // retired (moved to Banishment, as a token that ceases to exist) after the
    // third use, and stale anchors from earlier turns are retired by the next
    // Fanfare (they can no longer trigger in any case). A use is spent as the
    // ability RESOLVES, so a fourth unit played before the third draw resolves
    // still triggers, but its ability finds no use left and draws nothing.
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayACard};
    }

    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId /*controller*/,
                          TriggerType trigger, GameObjectId subject) const override {
        if (trigger != TriggerType::WhenYouPlayACard) return false;
        if (!state.objectExists(source)) return false;
        const auto& anchor = state.getObject(source);
        if (!isAnchor(anchor) || anchor.zone != ZoneType::LegendZone) return false;
        if (counter(anchor, kTurnKey) != state.turn.turn_number) return false;
        if (counter(anchor, kLeftKey) <= 0) return false;
        return subject != kInvalidId && state.objectExists(subject) &&
               state.getObject(subject).isUnit();
    }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const int turn = ctx.state.turn.turn_number;
        for (auto& [id, obj] : ctx.state.objects)
            if (isAnchor(obj) && obj.zone == ZoneType::LegendZone && counter(obj, kTurnKey) != turn)
                retire(obj);
        const GameObjectId id = ctx.state.createObject();
        auto& anchor = ctx.state.getObject(id);
        anchor.owner = anchor.controller = ctx.controller;
        anchor.card_def_id = cardDefId();
        anchor.card_type = CardType::Spell;
        anchor.name = R"RB(Fanfare)RB";
        anchor.is_token = true;
        anchor.zone = ZoneType::LegendZone;
        anchor.location = std::nullopt;
        anchor.card_counters[kAnchorKey] = 1;
        anchor.card_counters[kTurnKey] = turn;
        anchor.card_counters[kLeftKey] = 3;
        ctx.events.logTrace("FANFARE: the next three units played this turn draw 1");
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& anchor = ctx.state.getObject(ctx.source);
        if (!isAnchor(anchor)) return;
        if (counter(anchor, kTurnKey) != ctx.state.turn.turn_number) return;
        int& left = anchor.card_counters[kLeftKey];
        if (left <= 0) return;
        --left;
        const int remaining = left;
        if (remaining <= 0) retire(anchor);
        ctx.executor.drawCards(ctx.controller, 1);
        ctx.events.logTrace("FANFARE: played a unit -> draw 1 (" + std::to_string(remaining) +
                            " left)");
    }

private:
    static constexpr const char* kAnchorKey = "__fanfare_anchor";
    static constexpr const char* kTurnKey = "__fanfare_turn";
    static constexpr const char* kLeftKey = "__fanfare_left";

    static int counter(const GameObject& o, const char* key) {
        auto it = o.card_counters.find(key);
        return it == o.card_counters.end() ? 0 : it->second;
    }
    static bool isAnchor(const GameObject& o) { return counter(o, kAnchorKey) == 1; }
    static void retire(GameObject& o) {
        o.card_counters[kLeftKey] = 0;
        o.zone = ZoneType::Banishment;   // a token: it ceases to exist (CR 183.1)
        o.location = std::nullopt;
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1015;
        d.def_id = R"RB(rad-047-167)RB";
        d.name = R"RB(Fanfare)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-047/167)RB";
        d.collector_number = 47;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {};
        d.energy_cost = 3;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(The next three times you play a unit this turn, draw 1.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/75a3d8fbfd8caeabfd5be38513a6624c61f5230f-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1015(CardRegistry& r) {
    r.registerCard(1015, std::make_unique<Fanfare>());
}

}  // namespace riftbound
