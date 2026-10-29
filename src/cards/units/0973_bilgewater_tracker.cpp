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

class BilgewaterTracker : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play me, deal 1 to each of up to 2 units."
    // Choices at finalization (CR 383.3): how many units (pickMode: 0, 1 or 2),
    // then that many distinct units (pickTarget for one, pickTargetPair for
    // two). Damage is dealt to every chosen unit first, then lethally damaged
    // units are killed (AoE rule: never kill inside the damage loop).
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }

    static std::vector<GameObjectId> boardUnits(const GameState& state, PlayerId chooser) {
        auto out = chooseableUnits(state, chooser, [](const GameObject&) { return true; });
        std::sort(out.begin(), out.end());
        return out;
    }

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto units = boardUnits(ctx.state, ctx.controller);
        uint32_t legal = 1u;                       // "up to": choosing none is allowed
        if (units.size() >= 1) legal |= 2u;
        if (units.size() >= 2) legal |= 4u;
        const int count = pickMode(ctx, "Bilgewater Tracker: deal 1 to how many units", 3,
                                   {"No units", "One unit", "Two units"}, legal);
        if (count == -1) return;   // waiting for the agent

        std::vector<GameObjectId> chosen;
        if (count == 1) {
            GameObjectId a = pickTarget(ctx, "Bilgewater Tracker: deal 1", units);
            if (a == kInvalidId && ctx.state.chain.resuming.has_value() &&
                ctx.state.chain.resuming->resume_point == 7)
                return;   // waiting for the agent
            chosen.push_back(a);
        } else if (count == 2) {
            auto [a, b] = pickTargetPair(
                ctx, "Bilgewater Tracker: deal 1 to each of two units", units,
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
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)

        // Deal all the damage first ...
        std::vector<GameObjectId> hit;
        for (auto t : chosen) {
            if (t == kInvalidId || !ctx.state.objectExists(t)) continue;
            if (!ctx.state.getObject(t).location.has_value()) continue;
            ctx.executor.dealDamage(t, 1, ctx.source);
            hit.push_back(t);
        }
        // ... then kill what is lethally damaged.
        for (auto t : hit) {
            if (!ctx.state.objectExists(t)) continue;
            const auto& o = ctx.state.getObject(t);
            if (o.location.has_value() && o.hasLethalDamage()) ctx.executor.killObject(t);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 973;
        d.def_id = R"RB(rad-003-167)RB";
        d.name = R"RB(Bilgewater Tracker)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-003/167)RB";
        d.collector_number = 3;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.energy_cost = 3;
        d.might = 3;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When you play me, deal 1 to each of up to 2 units.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b660759a6bcb9c7ca41ea969cf11463259e284c9-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_973(CardRegistry& r) {
    r.registerCard(973, std::make_unique<BilgewaterTracker>());
}

}  // namespace riftbound
