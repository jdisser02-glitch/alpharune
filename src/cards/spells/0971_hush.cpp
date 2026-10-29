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

class Hush : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "Counter a spell with Power cost [A][A] or more." ([A][A] = 2 Power.)
    //
    // Peek-and-pop: Hush has been popped by FEPR when it resolves, so the spell
    // it answers is the top of the chain. Playable only while the top of the
    // chain is a spell. The Power-cost condition needs the CardDB, which
    // hasLegalTargets cannot reach (it only sees GameState), so it is checked
    // at resolution — the same split Defy uses for its cost gate.
    bool hasLegalTargets(const GameState& state, PlayerId /*controller*/) const override {
        if (state.chain.items.empty()) return false;
        const auto& top = state.chain.items.back();
        return top.is_spell && top.card_def_id != kInvalidId;
    }

    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.state.chain.items.empty()) return;
        const auto& top = ctx.state.chain.items.back();
        if (!top.is_spell || top.card_def_id == kInvalidId) return;
        const int power = ctx.executor.cardDB().get(top.card_def_id).power_cost;
        if (power < 2) {
            ctx.events.logTrace("HUSH: cannot counter - Power cost " +
                                std::to_string(power) + " is less than [A][A]");
            return;   // illegal target; that spell resolves normally
        }
        counterChainTop(ctx);   // asks isUncounterable itself
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 971;
        d.def_id = R"RB(rad-041-167)RB";
        d.name = R"RB(Hush)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-041/167)RB";
        d.collector_number = 41;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {};   // printed without a domain: its Power cost is [A][A]
        d.energy_cost = 1;
        d.power_cost = 2;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Reaction);
        d.ability_text = R"RB([Reaction] (Play any time, even before spells and abilities resolve.)
Counter a spell with Power cost [A][A] or more.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b9c1b9441e42639805b715466938ccd26b89892d-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_971(CardRegistry& r) {
    r.registerCard(971, std::make_unique<Hush>());
}

}  // namespace riftbound
