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

class ClocktowerGuardian : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "When you play me or when I hold, [Predict 3].
    //  [Deathknell][>] Banish the top card of your Main Deck. You may play it,
    //  ignoring its Energy cost."
    // The Deathknell's "you may" depends on the banished card, so it is asked
    // as the ability resolves. Playing it pays the rest of its cost (Power)
    // through the normal cost path (EffectExecutor::playPayingCost with
    // ignore_energy); it can be played only if that is affordable. A unit
    // goes to its controller's base or a battlefield they control, a [Deploy]
    // gear to any battlefield, another gear to the base (agent-chosen
    // destination where there is a choice); a spell goes on the chain.
    std::vector<TriggerType> triggerTypes() const override {
        return {TriggerType::WhenYouPlayMe, TriggerType::WhenIHold, TriggerType::WhenIDie};
    }

    static constexpr int kBanishedSlot = 6;   // resume_data index of the banished card
    static constexpr int kBanishedFlag = 7;   // resume_data index: 1 once the banish happened

    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        if (ctx.firing_trigger != TriggerType::WhenIDie) {
            ctx.executor.predict(ctx.controller, 3);
            ctx.events.logTrace("CLOCKTOWER GUARDIAN: Predict 3");
            return;
        }
        deathknell(ctx);
    }
private:
    void deathknell(CardContext& ctx) {
        auto& ps = ctx.state.player(ctx.controller);
        GameObjectId banished = kInvalidId;
        if (ctx.state.chain.resuming.has_value()) {
            auto& ri = *ctx.state.chain.resuming;
            while (static_cast<int>(ri.resume_data.size()) <= kBanishedFlag) ri.resume_data.push_back(0);
            if (ri.resume_data[kBanishedFlag] == 0) {
                ri.resume_data[kBanishedFlag] = 1;
                ri.resume_data[kBanishedSlot] = static_cast<int32_t>(banishTop(ctx));
            }
            banished = static_cast<GameObjectId>(ri.resume_data[kBanishedSlot]);
        } else {
            banished = banishTop(ctx);
        }
        if (banished == kInvalidId || !ctx.state.objectExists(banished)) return;

        const EffectPlayCost free_energy{.ignore_energy = true};
        auto can_play = [&]() {
            if (!ctx.state.objectExists(banished)) return false;
            if (ctx.state.getObject(banished).zone != ZoneType::Banishment) return false;
            return ctx.executor.canPlayPayingCost(ctx.controller, banished, free_energy);
        };
        int go = confirmOptional(ctx, "Clocktower Guardian: play " +
                                 ctx.state.getObject(banished).name + " ignoring its Energy cost?",
                                 can_play);
        if (go != 1) return;

        // Where it goes.
        const auto& card = ctx.state.getObject(banished);
        std::vector<LocationId> dests;
        if (card.isUnit()) {
            dests.push_back(BaseLocation{ctx.controller});
            for (const auto& bf : ctx.state.battlefields)
                if (bf.controller.has_value() && *bf.controller == ctx.controller)
                    dests.push_back(BattlefieldLocation{bf.id});
        } else if (card.isGear()) {
            const Card* gc = ctx.registry ? ctx.registry->get(card.card_def_id) : nullptr;
            if (gc && gc->deploys()) {
                for (const auto& bf : ctx.state.battlefields)
                    dests.push_back(BattlefieldLocation{bf.id});
            } else {
                dests.push_back(BaseLocation{ctx.controller});
            }
        }
        std::optional<LocationId> where;
        if (dests.size() > 1) {
            std::vector<std::string> labels;
            for (const auto& d : dests)
                labels.push_back(std::holds_alternative<BattlefieldLocation>(d)
                    ? "Battlefield #" + std::to_string(std::get<BattlefieldLocation>(d).id)
                    : std::string("Base"));
            int m = pickMode(ctx, "Clocktower Guardian: where to play it",
                             static_cast<int>(dests.size()), labels);
            if (m == -1) return;   // waiting for the agent
            if (m >= 0 && m < static_cast<int>(dests.size())) where = dests[m];
        } else if (!dests.empty()) {
            where = dests.front();
        }

        if (!can_play()) return;
        auto it = std::find(ps.banishment.begin(), ps.banishment.end(), banished);
        if (it != ps.banishment.end()) ps.banishment.erase(it);
        if (!ctx.executor.playPayingCost(ctx.controller, banished, where, free_energy)) {
            // Not paid: it stays banished (CR 445.2).
            auto& b = ctx.state.getObject(banished);
            b.zone = ZoneType::Banishment;
            b.location = std::nullopt;
            ps.banishment.push_back(banished);
            return;
        }
        ctx.events.logTrace("CLOCKTOWER GUARDIAN: played the banished " +
                            ctx.state.getObject(banished).name + " ignoring its Energy cost");
    }

    static GameObjectId banishTop(CardContext& ctx) {
        auto& ps = ctx.state.player(ctx.controller);
        if (ps.main_deck.empty()) return kInvalidId;
        const GameObjectId top = ps.main_deck.back();
        if (!ctx.state.objectExists(top)) return kInvalidId;
        const auto& obj = ctx.state.getObject(top);
        ctx.events.emit(CardRevealedEvent{top, obj.card_def_id, obj.owner,
                                          /*revealed_to_all=*/true, ctx.controller,
                                          ZoneType::MainDeck});
        ctx.events.logTrace("CLOCKTOWER GUARDIAN: banishing " + obj.name +
                            " from the top of the Main Deck");
        ctx.executor.banishObject(top);
        return top;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1031;
        d.def_id = R"RB(rad-067-167)RB";
        d.name = R"RB(Clocktower Guardian)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-067/167)RB";
        d.collector_number = 67;
        d.artist = R"RB(Polar Engine Studio)RB";
        d.card_type = CardType::Unit;
        d.domains = {Domain::Fury};
        d.tags = {R"RB(Mech)RB"};
        d.energy_cost = 8;
        d.power_cost = 2;
        d.might = 8;
        d.rarity = Rarity::Epic;
        d.keywords.set(Keyword::Deathknell);
        d.ability_text = R"RB(When you play me or when I hold, [Predict 3].
[Deathknell][>] Banish the top card of your Main Deck. You may play it, ignoring its Energy cost.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/b846d604eee450a4ab8f457f2738b0596e7b361e-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1031(CardRegistry& r) {
    r.registerCard(1031, std::make_unique<ClocktowerGuardian>());
}

}  // namespace riftbound
