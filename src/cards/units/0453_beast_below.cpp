#include "cards/card.h"
#include "cards/card_helpers.h"
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

class BeastBelow : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "When you play me, return another friendly unit and an enemy unit to
    //  their owners' hands." Both are chosen here (at finalization, CR 383.3)
    // through pickTargetPair: a unit's play carries no targets into its
    // trigger. With no other friendly unit the enemy is still returned
    // (CR 055: do as much as possible) through a single pickTarget; the
    // branch taken is kept in resume_data[6] so re-entries follow it.
    std::vector<GameObjectId> otherFriendly(const CardContext& ctx) const {
        auto out = chooseableUnits(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller == ctx.controller;
        });
        out.erase(std::remove(out.begin(), out.end(), ctx.source), out.end());
        return out;
    }
    std::vector<GameObjectId> enemies(const CardContext& ctx) const {
        return chooseableUnits(ctx.state, ctx.controller, [&](const GameObject& o) {
            return o.controller != ctx.controller;
        });
    }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        auto& ri_opt = ctx.state.chain.resuming;
        int branch = 0;   // 1 = friendly + enemy pair, 2 = enemy only
        if (ri_opt.has_value()) {
            while (ri_opt->resume_data.size() < 7) ri_opt->resume_data.push_back(0);
            branch = ri_opt->resume_data[6];
        }
        if (branch == 0) {
            branch = otherFriendly(ctx).empty() ? 2 : 1;
            if (ri_opt.has_value()) ri_opt->resume_data[6] = branch;
        }
        GameObjectId friendly = kInvalidId, enemy = kInvalidId;
        if (branch == 1) {
            auto pair = pickTargetPair(ctx, "Beast Below", otherFriendly(ctx),
                                       [&](GameObjectId) { return enemies(ctx); });
            friendly = pair.first;
            enemy = pair.second;
        } else {
            enemy = pickTarget(ctx, "Beast Below: return an enemy unit", enemies(ctx));
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (ri_opt.has_value()) {
            const int rp = ri_opt->resume_point;
            if ((branch == 1 && (friendly == kInvalidId || enemy == kInvalidId) &&
                 (rp == 10 || rp == 12)) ||
                (branch == 2 && enemy == kInvalidId && rp == 7))
                return;                       // waiting for the agent
        }
        if (friendly != kInvalidId && friendly != ctx.source && ctx.state.objectExists(friendly))
            ctx.executor.bounceToHand(friendly);
        if (enemy != kInvalidId && ctx.state.objectExists(enemy))
            ctx.executor.bounceToHand(enemy);
    }
    TriggerType triggerType() const override { return TriggerType::WhenYouPlayMe; }
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 2, .must_be_unit = true};
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 453;
        d.def_id = R"RB(sfd-132-221)RB";
        d.name = R"RB(Beast Below)RB";
        d.set_code = R"RB(SFD)RB";
        d.set_name = R"RB(Spiritforged)RB";
        d.public_code = R"RB(SFD-132/221)RB";
        d.collector_number = 132;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Bilgewater)RB"};
        d.energy_cost = 7;
        d.power_cost = 2;
        d.might = 8;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you play me, return another friendly unit and an enemy unit to their owners' hands.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/d27a56111c68c651099d7b73b22a54a825458857-744x1039.png?accountingTag=RB)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_453(CardRegistry& r) {
    r.registerCard(453, std::make_unique<BeastBelow>());
}

} // namespace riftbound
