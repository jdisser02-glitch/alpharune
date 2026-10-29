#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace riftbound {
namespace {

class CommandAttack : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    // "Choose one — Move a friendly gear with [Deploy] to a battlefield. /
    //  Look at the top 5 cards of your Main Deck. You may reveal a gear from
    //  among them and draw it. Recycle the rest."
    // One pickMode covers the mode and the destination: option 0 is the look,
    // option 1 + i moves a gear to battlefield i; the gear is then picked.
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto& bfs = ctx.state.battlefields;
        const int n = 1 + static_cast<int>(std::min<size_t>(bfs.size(), 30));
        uint32_t legal = 1u;
        std::vector<std::string> labels = {"Look at the top 5 for a gear"};
        for (int i = 1; i < n; ++i) {
            const auto bf = bfs[i - 1].id;
            labels.push_back("Move a [Deploy] gear to BF#" + std::to_string(static_cast<int>(bf)));
            if (!movableGear(ctx, bf).empty()) legal |= (1u << i);
        }
        int mode = pickMode(ctx, "Command: Attack", n, labels, legal);
        if (mode < 0) return;
        if (mode == 0) { lookForGear(ctx); return; }
        const BattlefieldId dest = bfs[mode - 1].id;
        GameObjectId gear = pickTarget(ctx, "Command: Attack: gear to move", movableGear(ctx, dest));
        if (gear == kInvalidId && ctx.state.chain.resuming.has_value() &&
            ctx.state.chain.resuming->resume_point == 7)
            return;   // waiting for the agent
        if (gear == kInvalidId || !ctx.state.objectExists(gear)) return;
        auto& g = ctx.state.getObject(gear);
        g.location = BattlefieldLocation{dest};
        g.zone = ZoneType::BattlefieldZone;
        ctx.events.emit(ObjectStateChangedEvent{gear, "moved"});
        ctx.events.logTrace("COMMAND: ATTACK: moved " + g.name + " to BF#" +
                            std::to_string(static_cast<int>(dest)));
    }
private:
    std::vector<GameObjectId> movableGear(const CardContext& ctx, BattlefieldId dest) const {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : ctx.state.objects) {
            if (!obj.isGear() || obj.controller != ctx.controller || !obj.location.has_value()) continue;
            if (obj.attached_to.has_value() || obj.battlefieldId() == std::optional<BattlefieldId>{dest})
                continue;
            const Card* c = (ctx.registry && obj.card_def_id != kInvalidId)
                ? ctx.registry->get(obj.card_def_id) : nullptr;
            if (c && c->deploys()) out.push_back(id);
        }
        return out;
    }
    void lookForGear(CardContext& ctx) {
        auto& ps = ctx.state.player(ctx.controller);
        std::vector<GameObjectId> top;
        for (int i = static_cast<int>(ps.main_deck.size()) - 1; i >= 0 && top.size() < 5; --i)
            top.push_back(ps.main_deck[i]);
        GameObjectId drawn = kInvalidId;
        for (auto cid : top)
            if (ctx.state.objectExists(cid) && ctx.state.getObject(cid).isGear()) { drawn = cid; break; }
        if (drawn != kInvalidId) {
            ps.main_deck.erase(std::remove(ps.main_deck.begin(), ps.main_deck.end(), drawn),
                               ps.main_deck.end());
            auto& obj = ctx.state.getObject(drawn);
            obj.zone = ZoneType::Hand;
            obj.location = std::nullopt;
            ps.hand.push_back(drawn);
            ctx.events.logTrace("COMMAND: ATTACK: revealed and drew " + obj.name);
        }
        std::vector<GameObjectId> rest;
        for (auto cid : top) {
            if (cid == drawn) continue;
            auto it = std::find(ps.main_deck.begin(), ps.main_deck.end(), cid);
            if (it != ps.main_deck.end()) { ps.main_deck.erase(it); rest.push_back(cid); }
        }
        if (!rest.empty()) ctx.executor.recycleCards(ctx.controller, rest);
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 984;
        d.def_id = R"RB(rad-033-167)RB";
        d.name = R"RB(Command: Attack)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-033/167)RB";
        d.collector_number = 33;
        d.card_type = CardType::Spell;
        d.domains = {Domain::Calm};
        d.energy_cost = 1;
        d.power_cost = 1;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(Choose one —
Move a friendly gear with [Deploy] to a battlefield.Look at the top 5 cards of your Main Deck. You may reveal a gear from among them and draw it. Recycle the rest.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b025756992e22af480902e13b01e25acbc747221-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_984(CardRegistry& r) {
    r.registerCard(984, std::make_unique<CommandAttack>());
}

}  // namespace riftbound
