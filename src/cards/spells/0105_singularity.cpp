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

class Singularity : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }
    // "Deal 6 to each of up to two units." Any units: two enemy units as well
    // as one of each. The engine's generic two-target play pairs one friendly
    // with one enemy unit, so it could not hit two enemies and was never
    // offered while its player had no unit; the card chooses its own targets
    // (how many, then which), like Bilgewater Tracker.
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 2, .must_be_unit = true, .optional = true};
    }
    bool needsPlayTimeTargetPair() const override { return true; }
    static std::vector<GameObjectId> boardUnits(const GameState& state, PlayerId chooser) {
        auto out = chooseableUnits(state, chooser, [](const GameObject&) { return true; });
        std::sort(out.begin(), out.end());
        return out;
    }
    bool hasLegalTargets(const GameState& state, PlayerId controller) const override {
        return !boardUnits(state, controller).empty();
    }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& targets) override {
        std::vector<GameObjectId> chosen = targets;   // a direct play names them
        if (chosen.empty()) {
            const auto units = boardUnits(ctx.state, ctx.controller);
            uint32_t legal = 0;
            if (units.size() >= 1) legal |= 1u;
            if (units.size() >= 2) legal |= 2u;
            const int count = pickMode(ctx, "Singularity: deal 6 to how many units", 2,
                                       {"One unit", "Two units"}, legal);
            if (count == -1) return;   // waiting for the agent
            if (count == 0) {
                GameObjectId a = pickTarget(ctx, "Singularity: deal 6", units);
                if (a == kInvalidId && ctx.state.chain.resuming.has_value() &&
                    ctx.state.chain.resuming->resume_point == 7)
                    return;   // waiting for the agent
                chosen.push_back(a);
            } else if (count == 1) {
                auto [a, b] = pickTargetPair(
                    ctx, "Singularity: deal 6 to each of two units", units,
                    [&](GameObjectId first) {
                        std::vector<GameObjectId> out;
                        for (auto u : units) if (u != first) out.push_back(u);
                        return out;
                    });
                const bool suspending = (a == kInvalidId || b == kInvalidId) &&
                                        ctx.state.chain.resuming.has_value() &&
                                        (ctx.state.chain.resuming->resume_point == 10 ||
                                         ctx.state.chain.resuming->resume_point == 12);
                if (suspending) return;   // waiting for the agent
                chosen.push_back(a);
                chosen.push_back(b);
            }
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)

        // Deal all the damage first, then kill what is lethally damaged.
        std::vector<GameObjectId> hit;
        for (auto t : chosen) {
            if (t == kInvalidId || !ctx.state.objectExists(t)) continue;
            if (!ctx.state.getObject(t).location.has_value()) continue;
            ctx.executor.dealDamage(t, 6, ctx.source);
            hit.push_back(t);
        }
        for (auto t : hit) {
            if (!ctx.state.objectExists(t)) continue;
            const auto& o = ctx.state.getObject(t);
            if (o.location.has_value() && o.hasLethalDamage()) ctx.executor.killObject(t);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 105;
        d.def_id = R"RB(ogn-105-298)RB";
        d.name = R"RB(Singularity)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-105/298)RB";
        d.collector_number = 105;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Mind};
        d.energy_cost = 6;
        d.power_cost = 2;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(Deal 6 to each of up to two units.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/792edce1a64d4a92740aa3fa240e21e161513944-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_105(CardRegistry& r) {
    r.registerCard(105, std::make_unique<Singularity>());
}

} // namespace riftbound
