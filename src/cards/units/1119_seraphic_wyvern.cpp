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
// medium). Notes: text quoted consistently by the sources; the Power cost was
// NOT found in any source (taken as 0 — an Epic 7-cost unit likely has one,
// verify). Artist: Mogi (not recorded here). RiftRef has a ruling on partial
// channels (not modeled beyond "channel what the deck still has").
class SeraphicWyvern : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "If you would channel runes, channel that many runes plus 1 exhausted
    //  rune instead."
    //
    // ENGINE-GAP: channelling (GameEngine::channelRunes for the Channel Phase,
    // EffectExecutor::channelRunes for card effects) has no replacement hook
    // and nothing subscribes to RuneChanneledEvent. Approximation, entirely in
    // this card: each aura pass (recalculateAuras, i.e. every cleanup) every
    // Wyvern compares the runes its controller has in base with the ones it
    // saw on the previous pass (recorded on itself). If new runes arrived — a
    // channel happened — it channels 1 more rune, exhausted, straight from the
    // top of the Rune Deck. That rune is marked so other Wyverns don't count
    // it as a channel of its own (each Wyvern adds exactly one per channel,
    // as stacked replacement effects would). Consequences: the extra rune
    // arrives at the next cleanup rather than as part of the channel; two
    // channel actions with no cleanup between them get one extra rune
    // between them; the extra rune emits no RuneChanneledEvent /
    // EnteredBoardEvent (the aura hook has no event bus); and a rune put
    // into base by something other than channelling also counts.
    static constexpr const char* kInit = "__seraphic_init";
    static constexpr const char* kSeenPrefix = "__seraphic_seen_";
    static constexpr const char* kExtra = "__seraphic_extra_rune";

    // Played: the runes in base as I am played (costs paid) are the ones that
    // were not channeled while I am on the board. A channel resolving before
    // the next aura pass — "when you play a unit, channel 1 rune" (Relentless
    // Storm) resolves with me already on the board (CR 359.2) — then still
    // counts; recording at the first aura pass instead missed it.
    void onPlay(CardContext& ctx) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        record(ctx.state.getObject(ctx.source), baseRunes(ctx.state, ctx.controller));
    }

    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        // Extra-rune markers only matter while the rune stays in base.
        for (auto& [id, obj] : state.objects)
            if (obj.isRune() && !obj.location.has_value()) obj.card_counters.erase(kExtra);

        forEachSelfOnce(*this, state, controller, [&](GameObjectId /*sid*/, GameObject& self) {
            std::vector<GameObjectId> in_base = baseRunes(state, controller);
            auto it = self.card_counters.find(kInit);
            if (it == self.card_counters.end() || it->second == 0) {
                record(self, in_base);   // first look: nothing has been channeled yet
                return;
            }
            bool channeled = false;
            for (auto r : in_base) {
                const auto& rune = state.getObject(r);
                if (rune.card_counters.count(kExtra)) continue;
                if (!self.card_counters.count(kSeenPrefix + std::to_string(r))) { channeled = true; break; }
            }
            if (channeled) {
                auto& ps = state.player(controller);
                if (!ps.rune_deck.empty()) {
                    const GameObjectId extra = ps.rune_deck.back();
                    ps.rune_deck.pop_back();
                    auto& rune = state.getObject(extra);
                    rune.zone = ZoneType::Base;
                    rune.location = BaseLocation{controller};
                    rune.is_exhausted = true;
                    rune.card_counters[kExtra] = 1;
                    in_base.push_back(extra);
                }
            }
            record(self, in_base);
        });
    }
private:
    static std::vector<GameObjectId> baseRunes(const GameState& state, PlayerId controller) {
        std::vector<GameObjectId> out;
        const LocationId base{BaseLocation{controller}};
        for (const auto& [id, obj] : state.objects)
            if (obj.isRune() && obj.controller == controller && obj.location.has_value() &&
                *obj.location == base)
                out.push_back(id);
        return out;
    }
    static void resetRecords(GameObject& self) {
        for (auto it = self.card_counters.begin(); it != self.card_counters.end();) {
            if (it->first.rfind(kSeenPrefix, 0) == 0) it = self.card_counters.erase(it);
            else ++it;
        }
        self.card_counters[kInit] = 0;
    }
    static void record(GameObject& self, const std::vector<GameObjectId>& in_base) {
        resetRecords(self);
        for (auto r : in_base) self.card_counters[kSeenPrefix + std::to_string(r)] = 1;
        self.card_counters[kInit] = 1;
    }

    const CardDef def_ = [] {
        CardDef d;
        d.id = 1119;
        d.def_id = R"RB(rad-092-167)RB";
        d.name = R"RB(Seraphic Wyvern)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-092/167)RB";
        d.collector_number = 92;
        d.card_type = CardType::Unit;
        d.domains = {Domain::Body};
        d.tags = {R"RB(Mount Targon)RB", R"RB(Dragon)RB"};
        d.energy_cost = 7;
        d.power_cost = 0;   // unknown in every source (null)
        d.might = 7;
        d.rarity = Rarity::Epic;
        d.ability_text = R"RB(If you would channel runes, channel that many runes plus 1 exhausted rune instead.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1119(CardRegistry& r) {
    r.registerCard(1119, std::make_unique<SeraphicWyvern>());
}

}  // namespace riftbound
