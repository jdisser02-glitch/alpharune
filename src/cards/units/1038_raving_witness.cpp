#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>
#include "cards/card_helpers.h"

namespace riftbound {
namespace {

class RavingWitness : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    TriggerType triggerType() const override { return TriggerType::WhenIMove; }

    // "When I move, reveal the top card of your Main Deck. If it's [Mighty],
    //  draw it. Otherwise, recycle it." A card in the deck is Mighty when it
    // is a unit with 5+ [M] (CR 708) — its printed Might.
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto& ps = ctx.state.player(ctx.controller);
        if (ps.main_deck.empty()) return;
        GameObjectId top = ps.main_deck.back();
        if (!ctx.state.objectExists(top)) return;
        ps.main_deck.pop_back();
        auto& obj = ctx.state.getObject(top);
        ctx.events.emit(CardRevealedEvent{
            top, obj.card_def_id, obj.owner,
            /*revealed_to_all=*/true, /*revealed_to=*/ctx.controller,
            ZoneType::MainDeck});
        if (mightyCard(ctx, obj)) {
            obj.zone = ZoneType::Hand;
            obj.location = std::nullopt;
            ps.hand.push_back(top);
            ctx.events.logTrace("RAVING WITNESS: revealed Mighty " + obj.name + " -> drew it");
        } else {
            ctx.executor.recycleCards(ctx.controller, {top});
            ctx.events.logTrace("RAVING WITNESS: revealed " + obj.name + " -> recycled");
        }
    }
private:
    static bool mightyCard(const CardContext& ctx, const GameObject& obj) {
        if (!obj.isUnit()) return false;
        int might = obj.base_might;
        if (obj.card_def_id != kInvalidId) {
            try { might = std::max(might, ctx.executor.cardDB().get(obj.card_def_id).might); }
            catch (const std::out_of_range&) {}
        }
        return might >= 5;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1038;
        d.def_id = R"RB(rad-074-167)RB";
        d.name = R"RB(Raving Witness)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-074/167)RB";
        d.collector_number = 74;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.energy_cost = 3;
        d.might = 3;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When I move, reveal the top card of your Main Deck. If it's [Mighty], draw it. Otherwise, recycle it. (A unit is Mighty while it has 5+ [M].))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/778e70b353d7f28eae9b54b1a37af3853238ba04-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1038(CardRegistry& r) {
    r.registerCard(1038, std::make_unique<RavingWitness>());
}

}  // namespace riftbound
