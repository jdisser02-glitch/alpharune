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

class DocksideLockUp : public GearCard {
public:
    const CardDef& def() const override { return def_; }
    bool deploys() const override { return true; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // [Deploy] via deploys(): played only to a battlefield, not recalled, and
    // killed when an opponent holds here (engine-handled).
    // "When you play this, banish a unit here." — any unit at this
    //  battlefield, chosen as the ability is finalized. The banished card is
    //  remembered on this gear ("__dlu_card"); a token ceases to exist instead.
    // "When you hold here, kill this to play the banished card, ignoring its
    //  cost." — you play it (you control it, whoever owns it) to your base or
    //  a battlefield you control (CR 355.2.a). "Kill this" is the cost, paid
    //  as the ability is finalized; it is optional ([cost] to [effect]).
    //
    // APPROXIMATION (engine gap): TriggerManager fires WhenYouHoldHere only on
    // battlefield cards, not on gear at the battlefield (it does for
    // WhenYouConquerHere). The hold ability is put on the chain at the start
    // of your Main Phase instead (AtStartOfMain) if you scored this
    // battlefield earlier this turn — before the Main Phase that can only be
    // the Scoring Step's hold.
    static constexpr int kAwaitLocation = 20;
    static constexpr int kDone = 21;
    static constexpr const char* kCardKey = "__dlu_card";

    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayThis, TriggerType::AtStartOfMain};
    }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType trigger, GameObjectId /*subject*/) const override {
        if (trigger != TriggerType::AtStartOfMain) return true;
        if (!state.objectExists(source)) return false;
        const auto& self = state.getObject(source);
        const auto here = self.battlefieldId();
        if (!here.has_value()) return false;
        if (!state.player(controller).battlefields_scored_this_turn.count(*here)) return false;
        return banishedCard(state, self) != kInvalidId;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger == TriggerType::AtStartOfMain) { holdHere(ctx); return; }

        // When you play this: banish a unit here.
        std::vector<GameObjectId> here_units;
        if (ctx.state.objectExists(ctx.source)) {
            const auto here = ctx.state.getObject(ctx.source).battlefieldId();
            if (here.has_value())
                here_units = chooseableUnits(ctx.state, ctx.controller,
                    [&](const GameObject& o) { return o.battlefieldId() == here; });
        }
        std::sort(here_units.begin(), here_units.end());
        GameObjectId victim = pickTarget(ctx, "Dockside Lock-Up: banish a unit here", here_units);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (victim == kInvalidId || !ctx.state.objectExists(victim)) return;
        if (!ctx.state.getObject(victim).location.has_value()) return;
        const bool token = ctx.state.getObject(victim).isToken();
        const std::string vname = ctx.state.getObject(victim).name;
        ctx.executor.banishObject(victim);
        if (!token && ctx.state.objectExists(ctx.source))
            ctx.state.getObject(ctx.source).card_counters[kCardKey] = static_cast<int>(victim);
        ctx.events.logTrace("DOCKSIDE LOCK-UP: banished " + vname +
                            (token ? " (a token: it ceases to exist)" : ""));
    }
private:
    static GameObjectId banishedCard(const GameState& state, const GameObject& self) {
        auto it = self.card_counters.find(kCardKey);
        if (it == self.card_counters.end()) return kInvalidId;
        const auto id = static_cast<GameObjectId>(it->second);
        if (!state.objectExists(id)) return kInvalidId;
        const auto& c = state.getObject(id);
        if (c.zone != ZoneType::Banishment || c.isToken()) return kInvalidId;
        const auto& bz = state.player(c.owner).banishment;
        if (std::find(bz.begin(), bz.end(), id) == bz.end()) return kInvalidId;
        return id;
    }

    void holdHere(CardContext& ctx) {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto card = [&]() { return banishedCard(ctx.state, ctx.state.getObject(ctx.source)); };
        auto can_do = [&]() {
            if (card() == kInvalidId) return false;
            return costAlreadyPaid(ctx) || ctx.state.getObject(ctx.source).location.has_value();
        };
        const GameObjectId c0 = card();
        const std::string cname = c0 != kInvalidId ? ctx.state.getObject(c0).name : "?";
        int go = confirmOptional(ctx, "Dockside Lock-Up: kill this to play " + cname + "?", can_do);
        if (go == -1) return;                 // waiting for the agent
        if (go == 1 && costDueNow(ctx)) {
            if (!ctx.state.getObject(ctx.source).location.has_value()) return;  // can't pay
            ctx.executor.killObject(ctx.source);   // the cost: kill this
        }
        if (finalizingChoices(ctx)) return;   // choices and costs end here (CR 383.3)
        if (go != 1) return;
        const GameObjectId c = card();
        if (c == kInvalidId) return;

        // Where it is played: your base or a battlefield you control.
        std::vector<int> dests{0};
        for (const auto& bf : ctx.state.battlefields)
            if (bf.controller == std::optional<PlayerId>{ctx.controller})
                dests.push_back(1 + static_cast<int>(bf.id));
        int dest = 0;
        if (ctx.state.chain.resuming.has_value()) {
            auto& ri = *ctx.state.chain.resuming;
            if (ri.resume_point == kDone) return;
            if (ri.resume_point < kAwaitLocation) {
                std::vector<Intent> options;
                for (int d : dests) options.push_back(valChoice(ctx.controller, d, destLabel(d)));
                ctx.executor.requestChoice(ctx.controller, std::move(options),
                                           "Dockside Lock-Up: where to play " + cname);
                ri.resume_point = kAwaitLocation;
                return;
            }
            dest = takenValue(ctx, 0);
            if (std::find(dests.begin(), dests.end(), dest) == dests.end()) dest = 0;
            ri.resume_point = kDone;
        }
        auto& obj = ctx.state.getObject(c);
        auto& bz = ctx.state.player(obj.owner).banishment;
        bz.erase(std::remove(bz.begin(), bz.end(), c), bz.end());
        const LocationId where = dest <= 0
            ? LocationId{BaseLocation{ctx.controller}}
            : LocationId{BattlefieldLocation{static_cast<BattlefieldId>(dest - 1)}};
        ctx.executor.playIgnoringCost(ctx.controller, c, where);
        if (ctx.state.objectExists(ctx.source))
            ctx.state.getObject(ctx.source).card_counters.erase(kCardKey);
        ctx.events.logTrace("DOCKSIDE LOCK-UP: killed itself -> played " + cname +
                            " to " + destLabel(dest) + ", ignoring its cost");
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1080;
        d.def_id = R"RB(rad-136-167)RB";
        d.name = R"RB(Dockside Lock-Up)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-136/167)RB";
        d.collector_number = 136;
        d.card_type = CardType::Gear;
        d.domains = {Domain::Order};
        d.tags = {R"RB(Device)RB"};
        d.energy_cost = 6;
        d.power_cost = 2;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB([Deploy] (Play this only to a battlefield. When an opponent holds here, kill this.)
When you play this, banish a unit here.
When you hold here, kill this to play the banished card, ignoring its cost. (Tokens aren't cards.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/4f477ccc474cb16e4e9d681b388f0673f77fae5b-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1080(CardRegistry& r) {
    r.registerCard(1080, std::make_unique<DocksideLockUp>());
}

}  // namespace riftbound
