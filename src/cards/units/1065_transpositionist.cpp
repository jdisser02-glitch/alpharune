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

class Transpositionist : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "[P], [E]: Return a friendly unit to its owner's hand. You may play a
    //  unit with less Might than that unit had, ignoring its cost."
    // The engine pays [P] + exhausts me. The friendly unit is the ability's
    // target, chosen as it is activated (needs_activation_time_target); it may
    // be me. As the ability resolves the unit's Might is read, it is returned,
    // and then — resume points 20.. (past the helpers' ranges) — the player
    // may pick a unit in hand whose printed Might is less than that and play
    // it for free, to their base or a battlefield they control.
    std::vector<ActivatedAbility> activatedAbilities() const override {
        ActivatedAbility ab;
        ab.cost.exhaust = true;
        ab.cost.power = 1;
        ab.cost.power_domain = Domain::Chaos;
        ab.targets = TargetRequirements{.count = 1, .must_be_unit = true, .must_be_friendly = true};
        ab.needs_activation_time_target = true;
        return {ab};
    }
    std::vector<GameObjectId> enumerateLegalTargets(
        const GameState& state, PlayerId controller, int /*ability_index*/) const override {
        return friendlyUnits(state, controller);
    }
    void onActivate(CardContext& ctx, int /*ability_index*/,
                    const std::vector<GameObjectId>& /*targets*/) override {
        auto& ri = ctx.state.chain.resuming;
        if (!ri.has_value()) return;   // only resolves from the chain
        if (ri->resume_point < kAskPlay) {
            GameObjectId unit = pickTarget(ctx, "Transpositionist: return a friendly unit",
                                           friendlyUnits(ctx.state, ctx.controller));
            if (unit == kInvalidId && ri->resume_point == 7) return;   // waiting for the agent
            if (unit == kInvalidId || !ctx.state.objectExists(unit)) return;
            const auto& u = ctx.state.getObject(unit);
            if (!u.location.has_value() || u.controller != ctx.controller) return;
            const int had = u.current_might;
            ctx.executor.bounceToHand(unit);
            ctx.events.logTrace("TRANSPOSITIONIST: returned " + ctx.state.getObject(unit).name +
                                " (" + std::to_string(had) + " [M]) to hand");
            while (ri->resume_data.size() <= kMightSlot) ri->resume_data.push_back(0);
            ri->resume_data[kMightSlot] = had;
            ri->resume_point = kAskPlay;
        }
        const int had = ri->resume_data.size() > kMightSlot ? ri->resume_data[kMightSlot] : 0;
        switch (ri->resume_point) {
        case kAskPlay: {
            if (candidates(ctx, had).empty()) { ri->resume_point = kDone; return; }
            ctx.executor.requestChoice(ctx.controller,
                {valChoice(ctx.controller, 0, "Don't play a unit"),
                 valChoice(ctx.controller, 1, "Play a unit with less than " +
                                              std::to_string(had) + " [M] for free")},
                "may: Transpositionist: play a cheaper-Might unit");
            ri->resume_point = kTakeAnswer;
            return;
        }
        case kTakeAnswer: {
            const int yes = takenValue(ctx, 0);
            const auto cands = candidates(ctx, had);
            if (yes != 1 || cands.empty()) { ri->resume_point = kDone; return; }
            std::vector<Intent> opts;
            for (auto id : cands) opts.push_back(objChoice(ctx.controller, id));
            ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                       "Transpositionist: unit to play");
            ri->resume_point = kTakeUnit;
            return;
        }
        case kTakeUnit: {
            const GameObjectId pick = takenObject(ctx);
            const auto cands = candidates(ctx, had);
            if (std::find(cands.begin(), cands.end(), pick) == cands.end()) {
                ri->resume_point = kDone;
                return;
            }
            while (ri->resume_data.size() <= kUnitSlot) ri->resume_data.push_back(0);
            ri->resume_data[kUnitSlot] = static_cast<int32_t>(pick);
            // CR 355.2.a: the player chooses its base or a battlefield they control.
            std::vector<Intent> where = {valChoice(ctx.controller, 0, "Base")};
            for (const auto& bf : ctx.state.battlefields)
                if (bf.controller.has_value() && *bf.controller == ctx.controller)
                    where.push_back(valChoice(ctx.controller, 1 + static_cast<int>(bf.id),
                                              destLabel(1 + static_cast<int>(bf.id))));
            if (where.size() == 1) { playFree(ctx, pick, 0); ri->resume_point = kDone; return; }
            ctx.executor.requestChoice(ctx.controller, std::move(where),
                                       "Transpositionist: where to play it");
            ri->resume_point = kTakeWhere;
            return;
        }
        case kTakeWhere: {
            const int dest = takenValue(ctx, 0);
            const GameObjectId pick = ri->resume_data.size() > kUnitSlot
                ? static_cast<GameObjectId>(ri->resume_data[kUnitSlot]) : kInvalidId;
            ri->resume_point = kDone;
            const auto cands = candidates(ctx, had);
            if (std::find(cands.begin(), cands.end(), pick) == cands.end()) return;
            playFree(ctx, pick, dest);
            return;
        }
        default:
            return;
        }
    }
private:
    static constexpr int kAskPlay = 20, kTakeAnswer = 21, kTakeUnit = 22, kTakeWhere = 23,
                         kDone = 24;
    static constexpr size_t kMightSlot = 6;   // past the helpers' 0-4 and the engine's 5
    static constexpr size_t kUnitSlot = 7;

    static std::vector<GameObjectId> friendlyUnits(const GameState& state, PlayerId controller) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.controller == controller && obj.location.has_value())
                out.push_back(id);
        return out;
    }
    /// Units in hand whose (printed) Might is less than `had`.
    static std::vector<GameObjectId> candidates(const CardContext& ctx, int had) {
        std::vector<GameObjectId> out;
        for (auto cid : ctx.state.player(ctx.controller).hand) {
            if (!ctx.state.objectExists(cid)) continue;
            const auto& c = ctx.state.getObject(cid);
            if (c.isUnit() && c.base_might < had) out.push_back(cid);
        }
        return out;
    }
    static void playFree(CardContext& ctx, GameObjectId card, int dest) {
        auto& hand = ctx.state.player(ctx.controller).hand;
        auto it = std::find(hand.begin(), hand.end(), card);
        if (it == hand.end()) return;
        hand.erase(it);
        std::optional<LocationId> loc;
        if (dest > 0) loc = LocationId{BattlefieldLocation{static_cast<BattlefieldId>(dest - 1)}};
        ctx.executor.playIgnoringCost(ctx.controller, card, loc);
        ctx.events.logTrace("TRANSPOSITIONIST: played " + ctx.state.getObject(card).name +
                            " ignoring its cost");
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1065;
        d.def_id = R"RB(rad-115-167)RB";
        d.name = R"RB(Transpositionist)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-115/167)RB";
        d.collector_number = 115;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.energy_cost = 6;
        d.might = 4;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB([P], [E]: Return a friendly unit to its owner's hand. You may play a unit with less Might than that unit had, ignoring its cost.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/60cf9dbb9345d44f0a9a78b60d5fb71411b50d69-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1065(CardRegistry& r) {
    r.registerCard(1065, std::make_unique<Transpositionist>());
}

}  // namespace riftbound
