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

class JarvanIVMartialParagon : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "Your gear with [Deploy] have [Action]. (They can be played on your turn
    //  or in showdowns.)"
    // DEVIATION (engine gap): the engine has no way to play a gear from hand
    // in a showdown (generateShowdownActions offers only spells, [Ambush]
    // units and [Action] abilities) and no "your gear have [Action]" grant.
    // Approximation: while Jarvan is on the board, his controller gets a
    // cost-free [Action] ability, offered only in a showdown, once per
    // gear card in hand (the ability's target). On resolution the chosen gear
    // — if it is still in hand and has [Deploy] — is played to a battlefield
    // the controller picks, paying its costs (EffectExecutor::playPayingCost);
    // if they aren't paid it stays in hand. Differences from the printed
    // permission: the play goes on the chain as an ability first (so it can
    // be responded to twice), battlefield cost discounts don't apply, and —
    // because enumerateLegalTargets has no CardRegistry to ask
    // Card::deploys() — gear without [Deploy] are offered too and do nothing
    // when chosen. On the controller's own turn the gear is already playable
    // normally (Neutral Open), so the ability is not offered then.
    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility ab;
        ab.targets = TargetRequirements{.count = 1, .must_be_gear = true};
        ab.is_action = true;
        return {ab};
    }
    using Card::enumerateLegalTargets;
    std::vector<GameObjectId> enumerateLegalTargets(const GameState& state,
                                                    PlayerId controller) const override {
        std::vector<GameObjectId> out;
        // In a showdown (Open to offer it; Closed while it resolves — the
        // engine re-checks an ability's targets against this list then).
        if (state.turn.ns_state != NeutralShowdownState::Showdown) return out;
        const auto& ps = state.player(controller);
        if (ps.cant_play_cards_this_turn) return out;
        for (auto cid : ps.hand)
            if (state.objectExists(cid) && state.getObject(cid).isGear()) out.push_back(cid);
        return out;
    }
    void onActivate(CardContext& ctx, int /*ability_index*/,
                    const std::vector<GameObjectId>& targets) override {
        if (targets.empty()) return;
        const GameObjectId gear = targets[0];
        const PlayerId me = ctx.controller;
        auto& ps = ctx.state.player(me);
        auto in_hand = [&]() {
            return ctx.state.objectExists(gear) &&
                   std::find(ps.hand.begin(), ps.hand.end(), gear) != ps.hand.end();
        };
        if (!in_hand()) return;
        const auto& g = ctx.state.getObject(gear);
        const Card* c = (ctx.registry && g.card_def_id != kInvalidId)
            ? ctx.registry->get(g.card_def_id) : nullptr;
        if (!g.isGear() || !c || !c->deploys()) {
            ctx.events.logTrace("JARVAN IV: " + g.name + " has no [Deploy]; nothing to play");
            return;
        }
        if (!ctx.executor.canPlayPayingCost(me, gear)) {
            ctx.events.logTrace("JARVAN IV: can't pay for " + g.name);
            return;
        }
        const auto& bfs = ctx.state.battlefields;
        const int n = static_cast<int>(std::min<size_t>(bfs.size(), 32));
        if (n == 0) return;
        std::vector<std::string> labels;
        for (int i = 0; i < n; ++i)
            labels.push_back("Play " + g.name + " to BF#" + std::to_string(static_cast<int>(bfs[i].id)));
        const uint32_t legal = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
        const int m = pickMode(ctx, "Jarvan IV: [Deploy] gear battlefield", n, labels, legal);
        if (m < 0 || m >= n) return;          // waiting for the agent
        if (!in_hand()) return;
        const LocationId where = BattlefieldLocation{bfs[m].id};
        ps.hand.erase(std::find(ps.hand.begin(), ps.hand.end(), gear));
        const std::string name = ctx.state.getObject(gear).name;
        if (!ctx.executor.playPayingCost(me, gear, where)) {
            if (ctx.state.objectExists(gear)) {
                auto& back = ctx.state.getObject(gear);
                back.zone = ZoneType::Hand;
                back.location = std::nullopt;
                ps.hand.push_back(gear);
            }
            ctx.events.logTrace("JARVAN IV: costs of " + name + " not paid; it stays in hand");
            return;
        }
        ctx.events.logTrace("JARVAN IV: played " + name + " with [Action] timing");
    }

    // "When I conquer, if there is a friendly gear with [Deploy] here, channel
    //  1 rune exhausted."
    // triggerCondition has no CardRegistry, so it recognises the gear the way
    // Overloaded Traveler does (an unattached friendly gear at my battlefield
    // — after cleanup only [Deploy] gear stay at battlefields); the effect
    // re-checks with Card::deploys() as it resolves.
    TriggerType triggerType() const override { return TriggerType::WhenIConquer; }
    bool triggerCondition(const GameState& state, GameObjectId source, PlayerId controller,
                          TriggerType /*trigger*/, GameObjectId /*subject*/) const override {
        if (!state.objectExists(source)) return false;
        const auto here = state.getObject(source).battlefieldId();
        if (!here.has_value()) return false;
        for (const auto& [id, obj] : state.objects)
            if (obj.isGear() && obj.controller == controller && !obj.attached_to.has_value() &&
                obj.battlefieldId() == here)
                return true;
        return false;
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        const auto here = ctx.state.getObject(ctx.source).battlefieldId();
        if (!here.has_value()) return;
        bool deploy_here = false;
        for (const auto& [id, obj] : ctx.state.objects) {
            if (!obj.isGear() || obj.controller != ctx.controller || obj.battlefieldId() != here)
                continue;
            const Card* c = (ctx.registry && obj.card_def_id != kInvalidId)
                ? ctx.registry->get(obj.card_def_id) : nullptr;
            if (c ? c->deploys() : !obj.attached_to.has_value()) { deploy_here = true; break; }
        }
        if (!deploy_here) return;
        ctx.executor.channelRunes(ctx.controller, 1, /*enter_exhausted=*/true);
        ctx.events.logTrace("JARVAN IV: conquered beside a [Deploy] gear -> channel 1 rune exhausted");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1051;
        d.def_id = R"RB(rad-091-167)RB";
        d.name = R"RB(Jarvan IV, Martial Paragon)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-091/167)RB";
        d.collector_number = 91;
        d.artist = R"RB(JunHuan)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Demacia)RB", R"RB(Jarvan IV)RB"};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(Your gear with [Deploy] have [Action]. (They can be played on your turn or in showdowns.)
When I conquer, if there is a friendly gear with [Deploy] here, channel 1 rune exhausted.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/8edacba51db193506e13e1afb6a8509256cdf55c-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1051(CardRegistry& r) {
    r.registerCard(1051, std::make_unique<JarvanIVMartialParagon>());
}

}  // namespace riftbound
