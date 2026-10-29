#include "cards/card.h"
#include "cards/card_registry.h"
#include "core/game_state.h"
#include "core/events.h"
#include "engine/effect_executor.h"
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "cards/card_helpers.h"

namespace riftbound {
namespace {

class Encore : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "Choose two — Deal 2 to a unit. / Play two 1 [M] Recruit unit tokens. /
    //  Give up to two units each +1 [M] this turn. / Look at the top 2 cards of
    //  your Main Deck, draw 1 and recycle the other."
    //
    // One pickMode over the six pairs of distinct modes. At finalization the
    // pair, then the unit to deal 2 to (pickTarget) and the units to give +1
    // (pickTargetPair; "up to two": both if two units are on the board, the
    // only one if one is) are chosen. As it resolves the modes happen in
    // printed order; units dealt lethal damage are killed after the other
    // modes (a +1 [M] can save the unit that was dealt 2). The card to draw
    // from the top 2 is picked last (resume_point 20/21, clear of the
    // helpers' ranges); the Recruits go to your base.
    static constexpr int kDealt = 20;     // non-look modes done; look prompt published next
    static constexpr int kLookAwait = 21;
    static constexpr int kAllDone = 22;

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        static const std::array<std::pair<int, int>, 6> kPairs = {{
            {0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}}};
        static const char* kModeNames[4] = {"Deal 2 to a unit", "Two 1 [M] Recruits",
                                            "Up to two units +1 [M]", "Look at 2, draw 1"};
        const bool can_deal = !anyUnit(ctx.state, ctx.controller).empty();
        uint32_t legal = 0;
        std::vector<std::string> labels;
        for (int i = 0; i < 6; ++i) {
            labels.push_back(std::string(kModeNames[kPairs[i].first]) + " + " +
                             kModeNames[kPairs[i].second]);
            if (kPairs[i].first != 0 || can_deal) legal |= (1u << i);
        }
        const int pm = pickMode(ctx, "Encore: choose two", 6, labels, legal);
        if (pm == -1) return;   // waiting for the agent
        if (pm < 0) { if (finalizingChoices(ctx)) return; return; }
        auto has = [&](int m) { return kPairs[pm].first == m || kPairs[pm].second == m; };
        auto& ri_opt = ctx.state.chain.resuming;

        GameObjectId dmg_target = kInvalidId;
        if (has(0)) {
            dmg_target = pickTarget(ctx, "Encore: deal 2 to a unit", anyUnit(ctx.state, ctx.controller));
            if (dmg_target == kInvalidId && ri_opt.has_value() && ri_opt->resume_point == 7)
                return;   // waiting
        }
        GameObjectId buff_a = kInvalidId, buff_b = kInvalidId;
        if (has(2)) {
            const auto units = anyUnit(ctx.state, ctx.controller);
            auto [a, b] = pickTargetPair(ctx, "Encore: up to two units +1 [M]", units,
                                         [&](GameObjectId picked) {
                                             std::vector<GameObjectId> rest;
                                             for (auto u : anyUnit(ctx.state, ctx.controller))
                                                 if (u != picked) rest.push_back(u);
                                             return rest;
                                         });
            if (ri_opt.has_value() && (ri_opt->resume_point == 10 || ri_opt->resume_point == 12))
                return;   // waiting
            buff_a = a;
            buff_b = b;
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)

        if (!ri_opt.has_value() || ri_opt->resume_point < kDealt) {
            std::vector<GameObjectId> damaged;
            if (has(0) && onBoard(ctx, dmg_target)) {
                ctx.executor.dealDamage(dmg_target, 2, ctx.source);
                damaged.push_back(dmg_target);
                ctx.events.logTrace("ENCORE: 2 to " + ctx.state.getObject(dmg_target).name);
            }
            if (has(1)) {
                for (int i = 0; i < 2; ++i)
                    ctx.executor.createToken(ctx.controller, CardType::Unit, "Recruit", 1, {"Recruit"},
                                             KeywordSet{}, LocationId{BaseLocation{ctx.controller}}, false);
                ctx.events.logTrace("ENCORE: two 1 [M] Recruits to base");
            }
            if (has(2)) {
                for (auto u : {buff_a, buff_b})
                    if (onBoard(ctx, u)) ctx.executor.giveTemporaryMight(u, 1);
                ctx.events.logTrace("ENCORE: +1 [M] this turn to up to two units");
            }
            for (auto id : damaged)
                if (onBoard(ctx, id) && ctx.state.getObject(id).hasLethalDamage())
                    ctx.executor.killObject(id);
            if (ri_opt.has_value()) ri_opt->resume_point = kDealt;
        }
        if (has(3)) lookDrawRecycle(ctx);
    }
private:
    static std::vector<GameObjectId> anyUnit(const GameState& state, PlayerId me) {
        auto v = chooseableUnits(state, me, [](const GameObject&) { return true; });
        std::sort(v.begin(), v.end());
        return v;
    }
    static bool onBoard(const CardContext& ctx, GameObjectId id) {
        return id != kInvalidId && ctx.state.objectExists(id) &&
               ctx.state.getObject(id).location.has_value();
    }
    // "Look at the top 2 cards of your Main Deck, draw 1 and recycle the other."
    void lookDrawRecycle(CardContext& ctx) {
        auto& ps = ctx.state.player(ctx.controller);
        auto& ri_opt = ctx.state.chain.resuming;
        auto topTwo = [&]() {
            std::vector<GameObjectId> top;
            for (int i = static_cast<int>(ps.main_deck.size()) - 1; i >= 0 && top.size() < 2; --i)
                top.push_back(ps.main_deck[i]);
            return top;
        };
        if (ri_opt.has_value() && ri_opt->resume_point == kAllDone) return;
        if (!ri_opt.has_value() || ri_opt->resume_point != kLookAwait) {
            const auto top = topTwo();
            if (top.size() < 2 || !ri_opt.has_value()) {
                // Nothing to choose between (or no chain to suspend on): draw the top card.
                if (!top.empty()) {
                    ctx.executor.drawCards(ctx.controller, 1);
                    std::vector<GameObjectId> rest;
                    for (size_t i = 1; i < top.size(); ++i) {
                        auto it = std::find(ps.main_deck.begin(), ps.main_deck.end(), top[i]);
                        if (it != ps.main_deck.end()) { ps.main_deck.erase(it); rest.push_back(top[i]); }
                    }
                    if (!rest.empty()) ctx.executor.recycleCards(ctx.controller, rest);
                }
                if (ri_opt.has_value()) ri_opt->resume_point = kAllDone;
                return;
            }
            std::vector<Intent> opts;
            for (auto cid : top) {
                const auto& o = ctx.state.getObject(cid);
                ctx.events.emit(CardRevealedEvent{cid, o.card_def_id, o.owner, false, ctx.controller,
                                                  ZoneType::MainDeck});
                opts.push_back(objChoice(ctx.controller, cid));
            }
            ctx.executor.requestChoice(ctx.controller, std::move(opts),
                                       "Encore: draw one of the top 2 (recycle the other)");
            ri_opt->resume_point = kLookAwait;
            return;   // waiting for the agent
        }
        const auto top = topTwo();
        GameObjectId drawn = takenObject(ctx);
        if (std::find(top.begin(), top.end(), drawn) == top.end())
            drawn = top.empty() ? kInvalidId : top.front();
        ri_opt->resume_point = kAllDone;
        if (drawn == kInvalidId) return;
        // Put the chosen card on top and draw it (so draw bookkeeping and
        // triggers run), then recycle the other one.
        auto it = std::find(ps.main_deck.begin(), ps.main_deck.end(), drawn);
        if (it != ps.main_deck.end()) { ps.main_deck.erase(it); ps.main_deck.push_back(drawn); }
        ctx.executor.drawCards(ctx.controller, 1);
        std::vector<GameObjectId> rest;
        for (auto cid : top) {
            if (cid == drawn) continue;
            auto jt = std::find(ps.main_deck.begin(), ps.main_deck.end(), cid);
            if (jt != ps.main_deck.end()) { ps.main_deck.erase(jt); rest.push_back(cid); }
        }
        if (!rest.empty()) ctx.executor.recycleCards(ctx.controller, rest);
        ctx.events.logTrace("ENCORE: looked at the top 2, drew 1, recycled the other");
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1094;
        d.def_id = R"RB(rad-152-167)RB";
        d.name = R"RB(Encore)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-152/167)RB";
        d.collector_number = 152;
        d.artist = R"RB(Luscinia Studio)RB";
        d.card_type = CardType::Spell;
        d.super_type = SuperType::Signature;
        d.domains = {Domain::Mind, Domain::Order};
        d.tags = {R"RB(Seraphine)RB"};
        d.energy_cost = 3;
        d.power_cost = 1;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(Choose two —
Deal 2 to a unit.Play two 1 [M] Recruit unit tokens.Give up to two units each +1 [M] this turn.Look at the top 2 cards of your Main Deck, draw 1 and recycle the other.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/0200aeca5d0a1cca4b8ab5b3f3910207118ee4e4-744x1040.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1094(CardRegistry& r) {
    r.registerCard(1094, std::make_unique<Encore>());
}

}  // namespace riftbound
