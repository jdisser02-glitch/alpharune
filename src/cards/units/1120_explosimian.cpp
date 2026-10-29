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

// The Bomb gear token's class is staged twice (gear/1199_bomb.cpp with
// radiance_helpers.h in one batch, gear/0956_bomb.cpp in another), and which
// id survives the merge is not known here. A Bomb this card plays is bound to
// whichever of these ids is registered as a card named "Bomb" (first match);
// with neither registered it stays an unbound token named "Bomb" (which the
// Bomb classes also recognise by name, but it then has no [Deploy] / death
// ability). Collapse this to the surviving constant once the merge is done.
constexpr CardDefId kBombCandidates_1120[] = {1199, 956};

GameObjectId playBombToken_1120(CardContext& ctx, BattlefieldId bf) {
    auto id = ctx.executor.createToken(ctx.controller, CardType::Gear, "Bomb", 0, {}, KeywordSet{},
                                       LocationId{BattlefieldLocation{bf}}, /*enter_ready=*/true);
    if (!ctx.state.objectExists(id)) return kInvalidId;
    if (ctx.registry) {
        for (auto cand : kBombCandidates_1120) {
            const Card* c = ctx.registry->get(cand);
            if (c && c->def().name == "Bomb") {
                ctx.state.getObject(id).card_def_id = cand;
                break;
            }
        }
    }
    return id;
}

// RECONSTRUCTED TEXT: this card is not in cards/card_index.json. Its data and
// ability text were reconstructed from web-search summaries (confidence:
// medium). Notes: the main sentence is quoted consistently; the parenthetical
// Bomb reminder is modeled on Amateur Demolitionist (RAD-001) and its exact
// wording is unverified. Power cost not shown in any source (null, taken as
// 0). Also an Organized Play promo.
class Explosimian : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When I hold, play three Bomb gear tokens to a single battlefield."
    // Bombs have [Deploy], so any battlefield will do; the controller picks
    // it as the ability resolves (a destination, not a target) through
    // pickMode (resume points 3/4/5).
    TriggerType triggerType() const override { return TriggerType::WhenIHold; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const auto& bfs = ctx.state.battlefields;
        const int n = static_cast<int>(std::min<size_t>(bfs.size(), 32));
        if (n == 0) return;
        std::vector<std::string> labels;
        for (int i = 0; i < n; ++i)
            labels.push_back("Three Bombs to BF#" + std::to_string(static_cast<int>(bfs[i].id)));
        const uint32_t legal = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
        const int m = pickMode(ctx, "Explosimian: Bombs to", n, labels, legal);
        if (m < 0) return;   // waiting for the agent
        const BattlefieldId bf = bfs[m].id;
        for (int i = 0; i < 3; ++i) playBombToken_1120(ctx, bf);
        ctx.events.logTrace("EXPLOSIMIAN: held -> three Bomb gear tokens to BF#" +
                            std::to_string(static_cast<int>(bf)));
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1120;
        d.def_id = R"RB(rad-094-167)RB";
        d.name = R"RB(Explosimian)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-094/167)RB";
        d.collector_number = 94;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Zaun)RB"};
        d.energy_cost = 6;
        d.power_cost = 0;   // unknown in every source (null)
        d.might = 6;
        d.rarity = Rarity::Common;
        d.ability_text = R"RB(When I hold, play three Bomb gear tokens to a single battlefield. (They have [Deploy]. When one dies, deal 2 to an enemy there and kill your other Bombs there.))RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1120(CardRegistry& r) {
    r.registerCard(1120, std::make_unique<Explosimian>());
}

}  // namespace riftbound
