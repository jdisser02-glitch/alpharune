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

class KaiSaRebel : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }

    // "[Disarm] (When I attack, give an enemy unit here -1 [M] this turn.)"
    // [Disarm] (CR 831) is not an engine keyword: a WhenIAttack trigger whose
    // enemy unit is chosen at finalization.
    TriggerType triggerType() const override { return TriggerType::WhenIAttack; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        GameObjectId victim = pickTarget(ctx, "Kai'Sa, Rebel: [Disarm]", disarmTargets(ctx, ctx.source));
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (victim == kInvalidId) return;     // no enemy unit here / waiting for the agent
        applyDisarm(ctx, victim, 1);
    }

    // "While I'm in a showdown, your spells have [Reaction]."
    //
    // DEVIATION: the engine has no "your spells have [Reaction]" grant — the
    // spell timing check reads the card's own `keywords`, not aura keywords.
    // While an instance of me is at a battlefield where a showdown (or a
    // combat, whose showdown it is) is in progress, this aura sets [Reaction]
    // directly on each spell in my controller's hand (marked kGrantKey and
    // tracked in temp_keywords so the Expiration Step clears it at the latest
    // at end of turn). Once I'm on the board but no longer in a showdown, it
    // takes the grant back. If I leave the board during the showdown, the
    // grant lingers until end of turn. Spells drawn during the showdown get it
    // at the next cleanup.
    static constexpr const char* kGrantKey = "__kaisa_rebel_reaction";

    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        bool in_showdown = false;
        bool on_board = false;
        for (const auto& [id, obj] : state.objects) {
            if (obj.card_def_id != cardDefId() || obj.controller != controller) continue;
            if (!obj.location.has_value()) continue;
            on_board = true;
            const auto bf = obj.battlefieldId();
            if (!bf.has_value()) continue;
            for (const auto& b : state.battlefields)
                if (b.id == *bf && (b.showdown_in_progress || b.combat_in_progress))
                    in_showdown = true;
        }
        if (!on_board) return;
        auto& ps = state.player(controller);
        if (in_showdown) {
            for (auto cid : ps.hand) {
                if (!state.objectExists(cid)) continue;
                auto& card = state.getObject(cid);
                if (!card.isSpell() || card.keywords.has(Keyword::Reaction)) continue;
                card.keywords.set(Keyword::Reaction);
                card.temp_keywords.set(Keyword::Reaction);
                card.card_counters[kGrantKey] = 1;
            }
            return;
        }
        for (auto cid : ps.hand) {
            if (!state.objectExists(cid)) continue;
            auto& card = state.getObject(cid);
            auto it = card.card_counters.find(kGrantKey);
            if (it == card.card_counters.end()) continue;
            card.card_counters.erase(it);
            card.keywords.clear(Keyword::Reaction);
            card.temp_keywords.clear(Keyword::Reaction);
        }
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1029;
        d.def_id = R"RB(rad-063-167)RB";
        d.name = R"RB(Kai'Sa, Rebel)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-063/167)RB";
        d.collector_number = 63;
        d.artist = R"RB(Calf Art Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Mind};
        d.tags = {R"RB(Kai'Sa)RB", R"RB(The Void)RB"};
        d.energy_cost = 5;
        d.power_cost = 1;
        d.might = 5;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB([Disarm] (When I attack, give an enemy unit here -1 [M] this turn).
While I'm in a showdown, your spells have [Reaction]. (Play them any time, even before spells and abilities resolve.))RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/53a7fdbb66f0d05111f4a7f54853db30f62fa5c4-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1029(CardRegistry& r) {
    r.registerCard(1029, std::make_unique<KaiSaRebel>());
}

}  // namespace riftbound
