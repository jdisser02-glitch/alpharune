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

class MordekaiserTwiceSlain : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // [Accelerate] and [Assault 3] are keywords (engine-handled).
    //
    // "Once each turn, if I would die, you may kill a friendly unit to heal me,
    //  exhaust me, and recall me instead."
    // A replacement of my own death (Card::replaceOwnDeath), asked as I would
    // die: while it has not been used this turn and another friendly unit is
    // on the board, my controller may take it. Killing the friendly unit is
    // its cost, paid first; then I am healed, exhausted and recalled (moved to
    // my base without it being a Move, CR 457-458; my attachments come along,
    // CR 719.3). Declining leaves it available for a later death this turn.
    bool replaceOwnDeath(CardContext& ctx) override {
        if (!ctx.state.objectExists(ctx.source)) return false;
        auto& self = ctx.state.getObject(ctx.source);
        if (!self.isUnit() || !self.location.has_value()) return false;
        const int turn = ctx.state.turn.turn_number;
        if (counter(self, kUsedTurn) == turn) return false;   // once each turn
        std::vector<GameObjectId> friends;
        for (const auto& [id, o] : ctx.state.objects)
            if (id != ctx.source && o.isUnit() && o.controller == ctx.controller &&
                o.location.has_value())
                friends.push_back(id);
        if (friends.empty()) return false;   // the cost can't be paid
        std::sort(friends.begin(), friends.end());

        const Intent yes_no = ctx.executor.chooseNow(ctx.controller,
            {valChoice(ctx.controller, 0, "Let Mordekaiser die"),
             valChoice(ctx.controller, 1, "Kill a friendly unit to heal, exhaust and recall him")});
        if (yes_no.chosen_value.value_or(0) != 1) {
            ctx.events.logTrace("MORDEKAISER, TWICE SLAIN: declined");
            return false;
        }
        const GameObjectId victim = ctx.executor.chooseObjectNow(ctx.controller, friends);
        if (victim == kInvalidId) return false;
        ctx.state.getObject(ctx.source).card_counters[kUsedTurn] = turn;
        ctx.executor.killObject(victim);   // the cost
        ctx.events.logTrace("MORDEKAISER, TWICE SLAIN: killed " + ctx.state.getObject(victim).name +
                            " to survive (healed, exhausted, recalled)");

        ctx.executor.healObject(ctx.source);
        ctx.executor.exhaustObject(ctx.source);
        auto& me = ctx.state.getObject(ctx.source);
        me.combat_designation = CombatDesignation::None;
        const LocationId base{BaseLocation{me.controller}};
        me.location = base;
        me.zone = ZoneType::Base;
        for (auto gid : me.attachments) {
            if (!ctx.state.objectExists(gid)) continue;
            auto& g = ctx.state.getObject(gid);
            g.location = base;
            g.zone = ZoneType::Base;
        }
        return true;
    }
private:
    static constexpr const char* kUsedTurn = "__mk_used_turn";
    static int counter(const GameObject& o, const char* key) {
        auto it = o.card_counters.find(key);
        return it == o.card_counters.end() ? -1 : it->second;
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 999;
        d.def_id = R"RB(rad-020-167)RB";
        d.name = R"RB(Mordekaiser, Twice Slain)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-020/167)RB";
        d.collector_number = 20;
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Fury};
        d.tags = {R"RB(Noxus)RB", R"RB(Mordekaiser)RB"};
        d.energy_cost = 7;
        d.power_cost = 1;
        d.might = 5;
        d.rarity = Rarity::Rare;
        d.assault_value = 3;
        d.keywords.set(Keyword::Accelerate);
        d.keywords.set(Keyword::Assault);
        d.ability_text = R"RB([Accelerate] (You may pay [1][R] as an additional cost to have me enter ready.)
[Assault 3] (+3 [M] while I'm an attacker.)
Once each turn, if I would die, you may kill a friendly unit to heal me, exhaust me, and recall me instead.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/66db9510de566ac43157ecdcc96f33b5f553a38d-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_999(CardRegistry& r) {
    r.registerCard(999, std::make_unique<MordekaiserTwiceSlain>());
}

}  // namespace riftbound
