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

class ScrutinizingScope : public GearCard {
public:
    const CardDef& def() const override { return def_; }

    // "[1], [E]: Choose an opponent. They guess a card type. Reveal the top
    //  card of your Main Deck. If it has that type, recycle it. Otherwise,
    //  draw it. (Unit, gear, and spell are types.)"
    // In 1v1 the opponent is chosen automatically. Their guess is a choice
    // published to THEM (0 = unit, 1 = gear, 2 = spell) as the ability
    // resolves, before the reveal.
    bool hasActivatedAbility() const override { return true; }
    ActivationCost getActivationCost() const override { return {.exhaust = true, .energy = 1}; }

    static constexpr int kGuessPending = 20;   // resume_point while the opponent guesses

    void onActivate(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        const PlayerId opp = opponent(ctx.controller);
        auto& ps = ctx.state.player(ctx.controller);
        int guess = 0;
        if (ctx.state.chain.resuming.has_value()) {
            auto& ri = *ctx.state.chain.resuming;
            if (ri.resume_point != kGuessPending) {
                if (ps.main_deck.empty()) {
                    ctx.events.logTrace("SCRUTINIZING SCOPE: Main Deck empty, nothing to reveal");
                    return;
                }
                ctx.executor.requestChoice(opp,
                    {valChoice(opp, 0, "Guess: unit"), valChoice(opp, 1, "Guess: gear"),
                     valChoice(opp, 2, "Guess: spell")},
                    "Scrutinizing Scope: guess a card type");
                ri.resume_point = kGuessPending;
                return;
            }
            guess = takenValue(ctx, 0);
        }
        if (ps.main_deck.empty()) return;
        const GameObjectId top = ps.main_deck.back();
        if (!ctx.state.objectExists(top)) return;
        const auto& card = ctx.state.getObject(top);
        ctx.events.emit(CardRevealedEvent{top, card.card_def_id, card.owner,
                                          /*revealed_to_all=*/true, ctx.controller,
                                          ZoneType::MainDeck});
        const CardType guessed = guess == 1 ? CardType::Gear
                               : guess == 2 ? CardType::Spell : CardType::Unit;
        const std::string name = card.name;
        if (card.card_type == guessed) {
            ps.main_deck.pop_back();
            ctx.executor.recycleCards(ctx.controller, {top});
            ctx.events.logTrace("SCRUTINIZING SCOPE: opponent guessed right, recycled " + name);
        } else {
            ctx.executor.drawCards(ctx.controller, 1);
            ctx.events.logTrace("SCRUTINIZING SCOPE: opponent guessed wrong, drew " + name);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1030;
        d.def_id = R"RB(rad-064-167)RB";
        d.name = R"RB(Scrutinizing Scope)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-064/167)RB";
        d.collector_number = 64;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Gear;
        d.domains = {Domain::Mind};
        d.energy_cost = 2;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([1], [E]: Choose an opponent. They guess a card type. Reveal the top card of your Main Deck. If it has that type, recycle it. Otherwise, draw it. (Unit, gear, and spell are types.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/6a028fc372c14dda73864aba3d3e84063f6da98d-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1030(CardRegistry& r) {
    r.registerCard(1030, std::make_unique<ScrutinizingScope>());
}

}  // namespace riftbound
