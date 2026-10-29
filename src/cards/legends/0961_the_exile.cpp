#include "cards/card.h"
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

class TheExile : public LegendCard {
public:
    const CardDef& def() const override { return def_; }
    // "Units you play to battlefields you control enter ready." Asked by
    // resolvePermanent as the unit arrives at its destination.
    bool grantsEnterReadyTo(const GameState& state, GameObjectId /*self*/,
                            const GameObject& played) const override {
        if (!played.isUnit()) return false;
        auto bf_id = played.battlefieldId();
        if (!bf_id.has_value()) return false;
        for (const auto& bf : state.battlefields)
            if (bf.id == *bf_id)
                return bf.controller.has_value() && *bf.controller == played.controller;
        return false;
    }
private:
    // RAD-149 is absent from cards/json and cards/card_index.json; the text is
    // the published card text (playriftbound.com gallery, Radiance).
    const CardDef def_ = [] {
        CardDef d;
        d.id = 961;
        d.def_id = R"RB(rad-149-167)RB";
        d.name = R"RB(The Exile)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-149/167)RB";
        d.collector_number = 149;
        d.card_type = CardType::Legend;
        d.domains = {Domain::Calm, Domain::Chaos};
        d.tags = {R"RB(Riven)RB"};
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(Units you play to battlefields you control enter ready.)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_961(CardRegistry& r) {
    r.registerCard(961, std::make_unique<TheExile>());
}

}  // namespace riftbound
