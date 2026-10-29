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

class DuneDevourer : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "You may kill a friendly unit as an additional cost to play me. If you
    //  do, I cost [2] less." [Deflect] is data + the engine keyword.
    //
    // APPROXIMATION (the engine has no "kill a unit" additional cost that
    // feeds a discount — see Atakhan / Commander Ledros): whenever a friendly
    // unit could be killed, the [2] discount is taken through
    // selfCostReduction, and the choice is made as I'm played (onPlay, CR
    // 355.1 — before I reach the board): the player picks the friendly unit
    // to kill, or picks me to pay the [2] instead (offered only if [2] can
    // still be paid). The net result is the printed choice: kill a unit and
    // pay [2] less, or pay the full cost. onPlay acts only on a play from
    // hand (the engine's play path leaves my zone at Hand until I resolve);
    // a play by another effect sets my zone first and has no additional
    // cost. Gap: if other discounts already brought my cost to 0, the [2]
    // saved nothing but the kill-or-pay is still asked.
    int selfCostReduction(const GameState& state, PlayerId player) const override {
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == player && obj.location.has_value())
                return kDiscount;
        return 0;
    }
    void onPlay(CardContext& ctx) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        if (ctx.state.getObject(ctx.source).zone != ZoneType::Hand) return;
        std::vector<GameObjectId> options;
        for (const auto& [id, obj] : ctx.state.objects)
            if (id != ctx.source && obj.isUnit() && obj.controller == ctx.controller &&
                obj.location.has_value())
                options.push_back(id);
        if (options.empty()) return;   // no friendly unit: no discount was taken
        const bool can_pay = payableEnergy(ctx.state, ctx.controller) >= kDiscount;
        if (can_pay) options.push_back(ctx.source);   // "me" = pay [2] instead of killing
        const GameObjectId pick = ctx.executor.chooseObjectNow(ctx.controller, options);
        if (pick == ctx.source) {
            payEnergy(ctx, ctx.controller, kDiscount);
            ctx.events.logTrace("DUNE DEVOURER: paid [2] instead of killing a unit");
            return;
        }
        if (pick == kInvalidId || !ctx.state.objectExists(pick)) return;
        ctx.events.logTrace("DUNE DEVOURER: killed " + ctx.state.getObject(pick).name +
                            " as an additional cost ([2] less)");
        ctx.executor.killObject(pick);
    }
private:
    static constexpr int kDiscount = 2;
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1072;
        d.def_id = R"RB(rad-125-167)RB";
        d.name = R"RB(Dune Devourer)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-125/167)RB";
        d.collector_number = 125;
        d.artist = R"RB(Envar Studio)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Order};
        d.energy_cost = 7;
        d.power_cost = 1;
        d.might = 7;
        d.rarity = Rarity::Uncommon;
        d.deflect_value = 1;
        d.keywords.set(Keyword::Deflect);
        d.ability_text = R"RB(You may kill a friendly unit as an additional cost to play me. If you do, I cost [2] less.
[Deflect] (Opponents must pay [A] to choose me with a spell or ability.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/4f49bf38f0d3abb71e148783ebec97f446db8166-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1072(CardRegistry& r) {
    r.registerCard(1072, std::make_unique<DuneDevourer>());
}

}  // namespace riftbound
