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
#include "core/intent.h"
#include <utility>

namespace riftbound {
namespace {

class ScorchedEarth : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }
    // [Action] is the keyword; the domainless [A] power cost is engine-paid.

    // "Deal 5 damage split among units at a single battlefield."
    // The battlefield is chosen as the spell is played (finalize pass,
    // pickMode over battlefields with units). The division is decided as it
    // resolves (CR 355.14.e), one damage per prompt, among the units there
    // (either player's).
    // ENGINE-GAP (as Alpha Strike / Volibear, Furious): CR 355.14.a-b make the
    // split units targets chosen at finalization; the engine cannot declare a
    // variable number of targets, so they are picked at resolution.
    bool hasLegalTargets(const GameState& state, PlayerId /*controller*/) const override {
        for (const auto& bf : state.battlefields)
            if (!unitsAt(state, bf.id).empty()) return true;
        return false;
    }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto& ri_opt = ctx.state.chain.resuming;
        const auto& bfs = ctx.state.battlefields;
        const int n = static_cast<int>(std::min<size_t>(bfs.size(), 32));
        // The split below rewrites resume_data, so the battlefield is read from
        // pickMode only before the split starts, then kept on the spell object.
        if (!ri_opt.has_value() || ri_opt->resume_point < kSplitAssign) {
            uint32_t legal = 0;
            std::vector<std::string> labels;
            for (int i = 0; i < n; ++i) {
                labels.push_back("Units at BF#" + std::to_string(bfs[i].id));
                if (!unitsAt(ctx.state, bfs[i].id).empty()) legal |= (1u << i);
            }
            const int m = pickMode(ctx, "Scorched Earth: battlefield", n, labels, legal);
            if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
            if (m < 0) return;                    // waiting for the agent / no battlefield
            if (!ctx.state.objectExists(ctx.source)) return;
            ctx.state.getObject(ctx.source).card_counters["__scorched_bf"] =
                static_cast<int>(bfs[m].id) + 1;
        }
        if (!ctx.state.objectExists(ctx.source)) return;
        const int stored = ctx.state.getObject(ctx.source).card_counters["__scorched_bf"];
        if (stored <= 0) return;
        const BattlefieldId bf = static_cast<BattlefieldId>(stored - 1);
        std::vector<GameObjectId> damaged;
        if (!splitDamage(ctx, "Scorched Earth", 5, ctx.source,
                         chooseable(ctx, unitsAt(ctx.state, bf)), damaged))
            return;   // waiting for the next pick
        ctx.state.getObject(ctx.source).card_counters["__scorched_bf"] = 0;
        ctx.events.logTrace("SCORCHED EARTH: 5 damage split among " +
                            std::to_string(damaged.size()) + " unit(s) at BF#" + std::to_string(bf));
    }
private:
    static std::vector<GameObjectId> unitsAt(const GameState& state, BattlefieldId bf) {
        std::vector<GameObjectId> out;
        for (const auto& [id, obj] : state.objects)
            if (obj.isUnit() && obj.battlefieldId() == std::optional<BattlefieldId>{bf}) out.push_back(id);
        return out;
    }
    static std::vector<GameObjectId> chooseable(const CardContext& ctx, std::vector<GameObjectId> in) {
        in.erase(std::remove_if(in.begin(), in.end(), [&](GameObjectId id) {
                     const auto& o = ctx.state.getObject(id);
                     return o.controller != ctx.controller && o.untargetable_by_enemy;
                 }),
                 in.end());
        return in;
    }

    // ── Split damage (CR 355.14) ── same algorithm as 0751_alpha_strike.cpp /
    // 0041_volibear_furious.cpp (kept as class members: card files share a
    // unity TU). Each prompt assigns the next point to a unit the controller
    // picks; damage is marked once the whole division is known. Progress lives
    // on the resolving chain item: resume_point kSplitAssign/Await/Done and
    // resume_data = [total, damage per pick, picked unit, ...].
    static constexpr int kSplitAssign = 100;
    static constexpr int kSplitAwait = 101;
    static constexpr int kSplitDone = 102;
    static constexpr int kSplitMaxPicks = 12;
    static bool splitDamage(CardContext& ctx, const std::string& label, int total,
                            GameObjectId damage_source, std::vector<GameObjectId> legal,
                            std::vector<GameObjectId>& damaged) {
        std::sort(legal.begin(), legal.end());
        std::vector<std::pair<GameObjectId, int>> alloc;
        auto add = [&alloc](GameObjectId id, int k) {
            for (auto& a : alloc) if (a.first == id) { a.second += k; return; }
            alloc.emplace_back(id, k);
        };
        auto deal = [&]() {
            for (const auto& [id, k] : alloc) {
                if (k <= 0 || !ctx.state.objectExists(id)) continue;
                if (ctx.state.getObject(id).controller != ctx.controller)
                    ctx.state.player(ctx.controller).chose_enemy_unit_this_turn = true;
                ctx.events.logTrace("SPLIT: " + label + " -> " + std::to_string(k) +
                                    " to " + ctx.state.getObject(id).name);
                ctx.executor.dealDamage(id, k, damage_source);
                damaged.push_back(id);
            }
        };
        auto& ri_opt = ctx.state.chain.resuming;
        if (!ri_opt.has_value()) {
            if (total <= 0 || legal.empty()) return true;
            for (int i = 0; i < total; ++i) add(legal[i % legal.size()], 1);
            deal();
            return true;
        }
        auto& ri = *ri_opt;
        if (ri.resume_point == kSplitDone) return true;
        auto alreadyPicked = [&ri](GameObjectId id) {
            for (size_t i = 2; i < ri.resume_data.size(); ++i)
                if (static_cast<GameObjectId>(ri.resume_data[i]) == id) return true;
            return false;
        };
        auto offered = [&]() {
            const int payable = ctx.executor.payableAnyPower(ctx.controller);
            std::vector<GameObjectId> out;
            for (auto t : legal)
                if (alreadyPicked(t) || ctx.executor.deflectCostFor(ctx.controller, t) <= payable)
                    out.push_back(t);
            return out;
        };
        if (ri.resume_point == kSplitAwait) {
            auto picked = ctx.executor.takeChoice();
            auto can_pick = offered();
            GameObjectId t = can_pick.empty() ? kInvalidId : can_pick.front();
            if (picked.has_value() && !picked->chosen_objects.empty() &&
                std::find(can_pick.begin(), can_pick.end(), picked->chosen_objects.front()) != can_pick.end())
                t = picked->chosen_objects.front();
            if (t != kInvalidId && !alreadyPicked(t)) {
                const int cost = ctx.executor.deflectCostFor(ctx.controller, t);
                if (cost > 0) {
                    ctx.executor.payAnyPower(ctx.controller, cost);
                    ctx.events.logTrace("DEFLECT: pays " + std::to_string(cost) +
                                        " [A] to choose " + ctx.state.getObject(t).name);
                }
                ri.chosen_objects.push_back(t);
            }
            ri.resume_data.push_back(static_cast<int32_t>(t));
            ri.resume_point = kSplitAssign;
        } else if (ri.resume_point != kSplitAssign) {
            if (total <= 0 || legal.empty()) { ri.resume_point = kSplitDone; return true; }
            const int per_pick = legal.size() == 1 ? total : (total + kSplitMaxPicks - 1) / kSplitMaxPicks;
            ri.resume_data = {total, per_pick};
            ri.resume_point = kSplitAssign;
            if (offered().empty()) { ri.resume_point = kSplitDone; return true; }
        }
        total = ri.resume_data[0];
        const int per_pick = std::max(1, ri.resume_data[1]);
        int remaining = total;
        for (size_t i = 2; i < ri.resume_data.size() && remaining > 0; ++i) {
            const int k = std::min(per_pick, remaining);
            add(static_cast<GameObjectId>(ri.resume_data[i]), k);
            remaining -= k;
        }
        const auto can_pick = remaining > 0 ? offered() : std::vector<GameObjectId>{};
        if (remaining > 0 && !can_pick.empty()) {
            std::vector<Intent> options;
            options.reserve(can_pick.size());
            for (auto t : can_pick) options.push_back(objChoice(ctx.controller, t));
            ctx.events.logTrace("SPLIT_PROMPT: " + label + " (" + std::to_string(remaining) +
                                " of " + std::to_string(total) + " damage left to assign)");
            ctx.executor.requestChoice(ctx.controller, std::move(options),
                                       "split damage: " + label + " (" +
                                       std::to_string(remaining) + " left)");
            ri.resume_point = kSplitAwait;
            return false;
        }
        ri.resume_point = kSplitDone;
        deal();
        return true;
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 989;
        d.def_id = R"RB(rad-006-167)RB";
        d.name = R"RB(Scorched Earth)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-006/167)RB";
        d.collector_number = 6;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.energy_cost = 4;
        d.power_cost = 1;
        d.rarity = Rarity::Common;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
Deal 5 damage split among units at a single battlefield.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/e646f9850571e20eca22e07af5d4dd68813fdd19-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_989(CardRegistry& r) {
    r.registerCard(989, std::make_unique<ScorchedEarth>());
}

}  // namespace riftbound
