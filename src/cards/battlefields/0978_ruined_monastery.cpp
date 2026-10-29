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

class RuinedMonastery : public BattlefieldCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    TriggerType triggerType() const override { return TriggerType::WhenYouConquerHere; }

    // "When you conquer here, you may discard 1 and channel 1 rune exhausted."
    // The "you may" is decided at finalization (CR 383.3.a); the discard is
    // part of the effect, not a cost, so the card to discard is chosen as the
    // ability resolves. Each half is done as far as it can be (CR 055): with
    // an empty hand the rune is still channeled, with an empty Rune Deck the
    // card is still discarded.
    // Resume points: 0-2 confirmOptional, kAwaitDiscard the discard choice,
    // kDone finished.
    static constexpr int kAwaitDiscard = 3;
    static constexpr int kDone = 4;

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto& ps = ctx.state.player(ctx.controller);
        auto can_do = [&ctx]() {
            const auto& p = ctx.state.player(ctx.controller);
            return !p.hand.empty() || !p.rune_deck.empty();
        };
        const int go = confirmOptional(
            ctx, "Ruined Monastery: discard 1 and channel 1 rune exhausted?", can_do);
        if (go == -1) return;                 // waiting for the agent
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (go != 1) return;

        if (!ctx.state.chain.resuming.has_value()) {
            // Direct invocation (no chain): discard the first card.
            if (!ps.hand.empty()) ctx.executor.applyDiscard(ctx.controller, ps.hand.front());
            ctx.executor.channelRunes(ctx.controller, 1, /*enter_exhausted=*/true);
            return;
        }
        auto& ri = *ctx.state.chain.resuming;
        if (ri.resume_point == kDone) return;
        if (ri.resume_point < kAwaitDiscard) {
            if (!ps.hand.empty()) {
                std::vector<Intent> options;
                for (auto cid : ps.hand) options.push_back(objChoice(ctx.controller, cid));
                ctx.executor.requestChoice(ctx.controller, std::move(options),
                                           "Ruined Monastery: discard 1");
                ri.resume_point = kAwaitDiscard;
                return;
            }
        } else if (ri.resume_point == kAwaitDiscard) {
            GameObjectId picked = takenObject(ctx);
            if (std::find(ps.hand.begin(), ps.hand.end(), picked) == ps.hand.end())
                picked = ps.hand.empty() ? kInvalidId : ps.hand.front();
            if (picked != kInvalidId) ctx.executor.applyDiscard(ctx.controller, picked);
        }
        ri.resume_point = kDone;
        ctx.executor.channelRunes(ctx.controller, 1, /*enter_exhausted=*/true);
        ctx.events.logTrace("RUINED MONASTERY: discarded 1, channeled 1 rune exhausted");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 978;
        d.def_id = R"RB(rad-166-167)RB";
        d.name = R"RB(Ruined Monastery)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-166/167)RB";
        d.collector_number = 166;
        d.artist = R"RB(Grafit Studio)RB";
        d.card_type = CardType::Battlefield;
        d.rarity = Rarity::Uncommon;
        d.ability_text = R"RB(When you conquer here, you may discard 1 and channel 1 rune exhausted.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/2d756c5198dafb151f51cceca4f11824575960cd-1039x744.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_978(CardRegistry& r) {
    r.registerCard(978, std::make_unique<RuinedMonastery>());
}

}  // namespace riftbound
