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

// RECONSTRUCTED TEXT: this card is not in cards/card_index.json. Its data and
// ability text were reconstructed from web-search summaries (confidence:
// high). Notes: gathered from web-search summaries of wiki / retailer pages;
// no timing keyword found (none set).
class RaucousAnthem : public SpellCard {
public:
    const CardDef& def() const override { return def_; }

    // "Ready your units and give them +3 [M] this turn." Every unit you
    // control on the board, collected first.
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        std::vector<GameObjectId> mine;
        for (const auto& [id, obj] : ctx.state.objects)
            if (obj.isUnit() && obj.controller == ctx.controller && obj.location.has_value())
                mine.push_back(id);
        std::sort(mine.begin(), mine.end());
        for (auto id : mine) {
            if (!ctx.state.objectExists(id)) continue;
            ctx.executor.readyObject(id);
            ctx.executor.giveTemporaryMight(id, 3);
        }
        ctx.events.logTrace("RAUCOUS ANTHEM: readied " + std::to_string(mine.size()) +
                            " unit(s), +3 [M] this turn");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1126;
        d.def_id = R"RB(rad-134-167)RB";
        d.name = R"RB(Raucous Anthem)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-134/167)RB";
        d.collector_number = 134;
        d.card_type = CardType::Spell;
        d.domains = {Domain::Order};
        d.energy_cost = 7;
        d.power_cost = 1;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(Ready your units and give them +3 [M] this turn.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1126(CardRegistry& r) {
    r.registerCard(1126, std::make_unique<RaucousAnthem>());
}

}  // namespace riftbound
