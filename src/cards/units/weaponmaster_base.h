#pragma once
/// @file weaponmaster_base.h
/// [Weaponmaster] (CR 818): "When you play me, you may [Equip] one of your
/// Equipment to me for [A] less, even if it's already attached."
/// The shared choice + payment (weaponmasterTrigger) and the WeaponmasterUnit
/// base class for units whose only extra text is the keyword. Lives in the
/// units/ type dir; derived per-card files include this header.

#include "cards/card.h"
#include "cards/card_helpers.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/intent.h"
#include "engine/effect_executor.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace riftbound {

/// What "[Equip] ... for [A] less" costs for `gear`.
/// ENGINE-GAP: an Equip cost lives inside each gear's onEquip — no Card hook
/// exposes it or accepts a discount. The printed "[Equip] [n][X]" cost is read
/// instead: its Energy is charged and one Power is taken off. Equipment whose
/// Equip cost has non-rune parts ("[Equip] — Spend 1 XP", "..., Recycle 2
/// cards") or would still have Power to pay after the discount is not offered.
struct ReducedEquipCost {
    bool payable_here = false;
    int energy = 0;
};
inline ReducedEquipCost weaponmasterEquipCost(CardContext& ctx, const GameObject& gear) {
    ReducedEquipCost out;
    if (gear.card_def_id == kInvalidId) {   // no printed cost at all
        out.payable_here = true;
        return out;
    }
    const std::string& text = ctx.executor.cardDB().get(gear.card_def_id).ability_text;
    static const std::string kKey = "[Equip]";
    size_t p = text.find(kKey);
    if (p == std::string::npos) return out;
    p += kKey.size();
    while (p < text.size() && text[p] == ' ') ++p;
    int energy = 0, power = 0;
    bool any = false;
    while (p < text.size() && text[p] == '[') {
        const size_t q = text.find(']', p);
        if (q == std::string::npos) return out;
        const std::string tok = text.substr(p + 1, q - p - 1);
        if (tok.empty()) return out;
        if (std::all_of(tok.begin(), tok.end(),
                        [](unsigned char c) { return c >= '0' && c <= '9'; })) {
            energy += std::stoi(tok);
        } else if (tok.size() == 1 && std::string("RGBOPYCA").find(tok[0]) != std::string::npos) {
            ++power;
        } else {
            return out;
        }
        any = true;
        p = q + 1;
    }
    if (!any) return out;                                   // "[Equip] — ..." form
    while (p < text.size() && text[p] == ' ') ++p;
    if (p < text.size() && text[p] == ',') return out;     // further costs follow
    if (power > 1) return out;                              // Power left after "[A] less"
    out.payable_here = true;
    out.energy = energy;
    return out;
}

/// Equipment `ctx.source` may Weaponmaster-equip right now (cost payable,
/// CR 445.2), in id order.
inline std::vector<GameObjectId> weaponmasterEquippable(CardContext& ctx) {
    std::vector<GameObjectId> out;
    const int energy = payableEnergy(ctx.state, ctx.controller);
    for (const auto& [id, obj] : ctx.state.objects) {
        if (obj.controller != ctx.controller || !obj.location.has_value()) continue;
        if (!isEquipment(obj)) continue;
        // "even if it's already attached" — to another unit; one already on
        // me has nothing to gain.
        if (obj.attached_to.has_value() && *obj.attached_to == ctx.source) continue;
        const auto cost = weaponmasterEquipCost(ctx, obj);
        if (!cost.payable_here || cost.energy > energy) continue;
        out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

inline void weaponmasterEquip(CardContext& ctx, GameObjectId gear_id, const std::string& who) {
    const auto cost = weaponmasterEquipCost(ctx, ctx.state.getObject(gear_id));
    if (!payEnergy(ctx, ctx.controller, cost.energy)) return;
    // Through the executor so the gear's onUnattached hook runs.
    if (ctx.state.getObject(gear_id).attached_to.has_value())
        ctx.executor.unattachGear(gear_id);
    attachFree(ctx, gear_id, ctx.source);   // emits "equipped"
    ctx.events.logTrace(who + ": [Weaponmaster] equipped " + ctx.state.getObject(gear_id).name);
}

/// The [Weaponmaster] trigger: which Equipment — or none ("you may") — is the
/// controller's choice, asked as the ability resolves (one MakeChoice per
/// Equipment plus a "No"). Uses resume points kWeaponmasterAwait / Done, clear
/// of the choice-helper ranges.
constexpr int kWeaponmasterAwait = 20;
constexpr int kWeaponmasterDone = 21;
inline void weaponmasterTrigger(CardContext& ctx, const std::string& who) {
    if (!ctx.state.objectExists(ctx.source) || !ctx.state.getObject(ctx.source).location.has_value())
        return;
    auto& ri_opt = ctx.state.chain.resuming;
    if (!ri_opt.has_value()) {
        // Direct invocation (no chain item to resume): no agent can be asked,
        // so take the first Equipment, as the choice helpers do.
        auto cands = weaponmasterEquippable(ctx);
        if (!cands.empty()) weaponmasterEquip(ctx, cands.front(), who);
        return;
    }
    auto& ri = *ri_opt;
    if (ri.resume_point == kWeaponmasterDone) return;
    if (ri.resume_point == kWeaponmasterAwait) {
        ri.resume_point = kWeaponmasterDone;
        auto choice = ctx.executor.takeChoice();
        auto cands = weaponmasterEquippable(ctx);
        GameObjectId picked = kInvalidId;
        if (choice && !choice->chosen_objects.empty()) picked = choice->chosen_objects[0];
        if (std::find(cands.begin(), cands.end(), picked) == cands.end()) {
            ctx.events.logTrace(who + ": [Weaponmaster] declined");
            return;
        }
        weaponmasterEquip(ctx, picked, who);
        return;
    }
    auto cands = weaponmasterEquippable(ctx);
    if (cands.empty()) { ri.resume_point = kWeaponmasterDone; return; }
    std::vector<Intent> opts;
    for (auto id : cands) {
        Intent c;
        c.type = IntentType::MakeChoice;
        c.player = ctx.controller;
        c.chosen_objects = {id};
        opts.push_back(std::move(c));
    }
    Intent none;
    none.type = IntentType::MakeChoice;
    none.player = ctx.controller;
    none.chosen_value = 0;
    none.choice_label = "No — " + who + ": [Weaponmaster]";
    opts.push_back(std::move(none));
    ctx.executor.requestChoice(ctx.controller, std::move(opts),
                               who + ": [Weaponmaster] equip which Equipment?");
    ri.resume_point = kWeaponmasterAwait;
}

/// Base class for units whose only triggered text is [Weaponmaster].
class WeaponmasterUnit : public UnitCard {
public:
    // Abstract: concrete weaponmaster cards supply their data via def().
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const std::string who = ctx.state.objectExists(ctx.source)
            ? ctx.state.getObject(ctx.source).name : std::string("Weaponmaster");
        weaponmasterTrigger(ctx, who);
    }
};

} // namespace riftbound
