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
#include "cards/radiance_helpers.h"

namespace riftbound {
namespace {

class HexplosiveMinefield : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "Kill any number of friendly Bomb tokens. At end of turn, play that many
    //  Bomb tokens to a single battlefield."
    //
    // The Bombs to kill are chosen as the spell is finalized, one at a time
    // ("kill this Bomb" ... "done"), and killed as it resolves, each through
    // killWithDeathTrigger so its own "When this dies" triggers. The number
    // actually killed is kept on the spell (it rests in the trash) and a
    // delayed AtEndOfTurn ability plays that many Bombs to one battlefield,
    // chosen as it resolves.
    //
    // Resume layout (clear of the Card::pick* helpers, which use 0..14 and
    // resume_data[0..5]): resume_point 20 = offer a Bomb, 21 = read the answer,
    // 22 = done; resume_data[6] = how many were picked, [7..] the picks.
    static constexpr int kOffer = 20;
    static constexpr int kRead = 21;
    static constexpr int kDone = 22;
    static constexpr size_t kCountSlot = 6;
    static constexpr const char* kPendingKey = "__minefield_bombs";

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto& ri_opt = ctx.state.chain.resuming;
        if (!ri_opt.has_value()) {
            // Direct invocation (no chain to suspend on): kill every friendly Bomb.
            std::vector<GameObjectId> all = friendlyBombs(ctx, {});
            killAndSchedule(ctx, all);
            return;
        }
        auto& ri = *ri_opt;
        while (ri.resume_data.size() <= kCountSlot) ri.resume_data.push_back(0);
        if (ri.resume_point < kOffer) ri.resume_point = kOffer;
        while (ri.resume_point != kDone) {
            if (ri.resume_point == kRead) {
                const GameObjectId picked = takenObject(ctx);   // kInvalidId = "done"
                if (picked == kInvalidId) { ri.resume_point = kDone; break; }
                ri.resume_data.push_back(static_cast<int32_t>(picked));
                ri.resume_data[kCountSlot] += 1;
                ri.resume_point = kOffer;
            }
            if (ri.resume_point == kOffer) {
                const auto cands = friendlyBombs(ctx, picks(ri));
                if (cands.empty()) { ri.resume_point = kDone; break; }
                std::vector<Intent> opts;
                for (auto id : cands) opts.push_back(objChoice(ctx.controller, id));
                opts.push_back(valChoice(ctx.controller, 0, "Done: kill no more Bombs"));
                ctx.events.logTrace("HEXPLOSIVE MINEFIELD: kill a friendly Bomb? (" +
                                    std::to_string(cands.size()) + " left)");
                ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                           "Hexplosive Minefield: kill a Bomb");
                ri.resume_point = kRead;
                return;   // waiting for the agent
            }
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)

        // Picks that are still friendly Bombs on the board.
        std::vector<GameObjectId> doomed;
        for (auto id : picks(ri)) {
            if (!ctx.state.objectExists(id)) continue;
            const auto& o = ctx.state.getObject(id);
            if (o.location.has_value() && isBomb(o) && o.controller == ctx.controller)
                doomed.push_back(id);
        }
        killAndSchedule(ctx, doomed);
    }

    // The delayed "At end of turn, play that many Bomb tokens to a single
    // battlefield." (fired with ctx.firing_trigger == AtEndOfTurn).
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& cc = ctx.state.getObject(ctx.source).card_counters;
        auto it = cc.find(kPendingKey);
        if (it == cc.end() || it->second <= 0) return;
        const int n = it->second;
        const int bf = pickBombBattlefield(*this, ctx, "Hexplosive Minefield: Bombs to which battlefield");
        if (bf == -1) return;   // waiting for the agent (the count stays stashed)
        ctx.state.getObject(ctx.source).card_counters.erase(kPendingKey);
        if (bf < 0) return;
        for (int i = 0; i < n; ++i)
            playBombToken(ctx, ctx.controller, static_cast<BattlefieldId>(bf));
        ctx.events.logTrace("HEXPLOSIVE MINEFIELD: end of turn -> " + std::to_string(n) +
                            " Bomb(s) to BF#" + std::to_string(bf));
    }
private:
    static std::vector<GameObjectId> picks(const ChainItem& ri) {
        std::vector<GameObjectId> out;
        const int n = ri.resume_data.size() > kCountSlot ? ri.resume_data[kCountSlot] : 0;
        for (int i = 0; i < n && kCountSlot + 1 + i < ri.resume_data.size(); ++i)
            out.push_back(static_cast<GameObjectId>(ri.resume_data[kCountSlot + 1 + i]));
        return out;
    }
    static std::vector<GameObjectId> friendlyBombs(const CardContext& ctx,
                                                   const std::vector<GameObjectId>& exclude) {
        std::vector<GameObjectId> out;
        for (const auto& [id, o] : ctx.state.objects) {
            if (!o.location.has_value() || !isBomb(o) || o.controller != ctx.controller) continue;
            if (std::find(exclude.begin(), exclude.end(), id) != exclude.end()) continue;
            out.push_back(id);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    void killAndSchedule(CardContext& ctx, const std::vector<GameObjectId>& doomed) {
        int killed = 0;
        for (auto id : doomed) {
            if (!ctx.state.objectExists(id) || !ctx.state.getObject(id).location.has_value())
                continue;   // an earlier Bomb's death already took it
            killWithDeathTrigger(ctx, id);
            if (!ctx.state.getObject(id).location.has_value()) ++killed;
        }
        ctx.events.logTrace("HEXPLOSIVE MINEFIELD: killed " + std::to_string(killed) + " Bomb(s)");
        if (killed <= 0 || !ctx.state.objectExists(ctx.source)) return;
        // Accumulates if the same spell object resolves twice in a turn; the
        // first delayed firing then plays them all.
        ctx.state.getObject(ctx.source).card_counters[kPendingKey] += killed;
        DelayedAbility da;
        da.source = ctx.source;
        da.card_def_id = cardDefId();
        da.controller = ctx.controller;
        da.trigger = TriggerType::AtEndOfTurn;
        da.expires_on_turn = ctx.state.turn.turn_number;
        ctx.state.delayed_abilities.push_back(da);
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1086;
        d.def_id = R"RB(rad-142-167)RB";
        d.name = R"RB(Hexplosive Minefield)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-142/167)RB";
        d.collector_number = 142;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Spell;
        d.super_type = SuperType::Signature;
        d.domains = {Domain::Fury, Domain::Chaos};
        d.tags = {R"RB(Ziggs)RB"};
        d.energy_cost = 2;
        d.power_cost = 1;
        d.rarity = Rarity::Epic;
        d.keywords.set(Keyword::Action);
        d.ability_text = R"RB([Action] (Play on your turn or in showdowns.)
Kill any number of friendly Bomb tokens. At end of turn, play that many Bomb tokens to a single battlefield.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/65b599befcc6ed873de0e7965bf497a805505327-744x1040.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1086(CardRegistry& r) {
    r.registerCard(1086, std::make_unique<HexplosiveMinefield>());
}

}  // namespace riftbound
