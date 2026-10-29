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

class DrakehoundPackmaster : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play me, reveal the top card of each opponent's Main Deck.
    //  Choose one and banish it, then play it, ignoring its cost. Recycle the
    //  rest."
    // In 1v1 there is exactly one opponent, so exactly one card is revealed:
    // it is the one chosen and there is no "rest" to recycle. The revealed card
    // is stashed in the chain item's targets so the location prompt can resume
    // without revealing again (the Dazzling Aurora pattern).
    // Where it is played (CR 355.2.a): a unit to its new controller's base or a
    // battlefield they control; a [Deploy] gear to any battlefield; other gear
    // to base; a spell goes on the chain like any other played spell.
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.chain.resuming.has_value()) return;
        auto& ri = *ctx.state.chain.resuming;
        if (ri.targets.empty()) {
            auto& ops = ctx.state.player(opponent(ctx.controller));
            if (ops.main_deck.empty()) { ri.targets.push_back(kInvalidId); return; }
            const GameObjectId top = ops.main_deck.back();
            auto& card = ctx.state.getObject(top);
            ctx.events.logTrace("DRAKEHOUND PACKMASTER: reveals " + card.name);
            ctx.events.emit(CardRevealedEvent{top, card.card_def_id, card.owner,
                                              /*revealed_to_all=*/true, PlayerId::None,
                                              ZoneType::MainDeck});
            ctx.executor.banishObject(top);
            // playIgnoringCost re-zones it: drop the banishment entry.
            auto& bz = ctx.state.player(ctx.state.getObject(top).owner).banishment;
            bz.erase(std::remove(bz.begin(), bz.end(), top), bz.end());
            ri.targets.push_back(top);
        }
        const GameObjectId cid = ri.targets.front();
        if (cid == kInvalidId || !ctx.state.objectExists(cid)) return;
        auto& card = ctx.state.getObject(cid);
        if (card.zone != ZoneType::Banishment) return;   // already played

        if (card.isSpell()) {
            ctx.executor.playIgnoringCost(ctx.controller, cid);
            ctx.events.emit(ObjectStateChangedEvent{cid, "played_not_from_hand"});
            ctx.events.logTrace("DRAKEHOUND PACKMASTER: plays " + card.name + " (spell)");
            return;
        }

        std::vector<LocationId> locations;
        std::vector<std::string> labels;
        auto bfLabel = [&](const BattlefieldState& bf) {
            if (ctx.state.objectExists(bf.card_object_id))
                return ctx.state.getObject(bf.card_object_id).name;
            return "BF#" + std::to_string(bf.id);
        };
        bool deploy = false;
        if (card.isGear() && ctx.registry && card.card_def_id != kInvalidId)
            if (const Card* c = ctx.registry->get(card.card_def_id)) deploy = c->deploys();
        if (deploy) {
            for (const auto& bf : ctx.state.battlefields) {
                locations.push_back(LocationId{BattlefieldLocation{bf.id}});
                labels.push_back(bfLabel(bf));
            }
        } else {
            locations.push_back(LocationId{BaseLocation{ctx.controller}});
            labels.push_back("base");
            if (card.isUnit())
                for (const auto& bf : ctx.state.battlefields)
                    if (bf.controller.has_value() && *bf.controller == ctx.controller) {
                        locations.push_back(LocationId{BattlefieldLocation{bf.id}});
                        labels.push_back(bfLabel(bf));
                    }
        }
        if (locations.empty()) return;
        const uint32_t mask = locations.size() >= 32 ? 0xFFFFFFFFu
                                                     : ((1u << locations.size()) - 1);
        int chosen = pickMode(ctx, "Drakehound Packmaster: where to play " + card.name,
                              static_cast<int>(locations.size()), labels, mask);
        if (chosen == -1) return;   // waiting for the agent
        if (chosen < 0 || static_cast<size_t>(chosen) >= locations.size()) chosen = 0;
        ctx.executor.playIgnoringCost(ctx.controller, cid, locations[chosen]);
        ctx.events.emit(ObjectStateChangedEvent{cid, "played_not_from_hand"});
        ctx.events.logTrace("DRAKEHOUND PACKMASTER: plays " + card.name + " to " +
                            labels[chosen] + ", ignoring its cost");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 997;
        d.def_id = R"RB(rad-018-167)RB";
        d.name = R"RB(Drakehound Packmaster)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-018/167)RB";
        d.collector_number = 18;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Fury};
        d.energy_cost = 9;
        d.power_cost = 2;
        d.might = 9;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(When you play me, reveal the top card of each opponent's Main Deck. Choose one and banish it, then play it, ignoring its cost. Recycle the rest.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/9ddc2759cf8b039bc1ee77e32326c5475cc001e8-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_997(CardRegistry& r) {
    r.registerCard(997, std::make_unique<DrakehoundPackmaster>());
}

}  // namespace riftbound
