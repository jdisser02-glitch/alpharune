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

class RousingDisplay : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeResolve; }

    // "[Show Off] a card with Energy cost [7] or more. Play two 1 [M] Recruit
    //  unit tokens to your base. If you showed off, play four to your base
    //  instead."
    // Showing off costs only information, which the engine does not model, so
    // the player shows off whenever a card with printed Energy cost [7]+ is in
    // their hand or among their game objects on the board (canShowOff). That
    // is decided as the spell is played (CR 832.1) and kept in
    // resume_data[kShowOffSlot] (1 = showed off, 0 = didn't, unset = not yet).
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto& ri = ctx.state.chain.resuming;
        bool showed_off = false;
        if (!ri.has_value() || ri->finalizing || !ri->choices_finalized ||
            ri->resume_data.size() <= kShowOffSlot) {
            showed_off = canShowOff(ctx, ctx.source, [&](const GameObject& o) {
                return o.card_def_id != kInvalidId &&
                       ctx.executor.cardDB().get(o.card_def_id).energy_cost >= kMinEnergy;
            });
            if (ri.has_value()) {
                while (ri->resume_data.size() <= kShowOffSlot) ri->resume_data.push_back(0);
                ri->resume_data[kShowOffSlot] = showed_off ? 1 : 0;
            }
        } else {
            showed_off = ri->resume_data[kShowOffSlot] == 1;
        }
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        const int count = showed_off ? 4 : 2;
        LocationId base{BaseLocation{ctx.controller}};
        for (int i = 0; i < count; ++i)
            ctx.executor.createToken(ctx.controller, CardType::Unit, "Recruit",
                                     1, {"Recruit"}, KeywordSet{}, base,
                                     /*enter_ready=*/false);
        ctx.events.logTrace("ROUSING DISPLAY: " + std::string(showed_off ? "showed off, " : "") +
                            std::to_string(count) + " Recruit tokens");
    }
private:
    static constexpr int kMinEnergy = 7;
    static constexpr size_t kShowOffSlot = 6;   // past the helpers' slots 0-4 and the engine's 5
    // The published text in cards/card_index.json ends "...instead.disa"; the
    // trailing "disa" is a data-entry artifact and is dropped here.
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1074;
        d.def_id = R"RB(rad-127-167)RB";
        d.name = R"RB(Rousing Display)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-127/167)RB";
        d.collector_number = 127;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Order};
        d.energy_cost = 5;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB([Show Off] a card with Energy cost [7] or more. (As you play this, you may reveal such a card from hand or pick a friendly one.)
Play two 1 [M] Recruit unit tokens to your base. If you showed off, play four to your base instead.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/18aa7cab082fd0e293260bcf480a1a76d12b64d9-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1074(CardRegistry& r) {
    r.registerCard(1074, std::make_unique<RousingDisplay>());
}

}  // namespace riftbound
