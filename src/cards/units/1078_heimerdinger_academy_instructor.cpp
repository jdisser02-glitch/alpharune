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

class HeimerdingerAcademyInstructor : public UnitCard {
public:
    const CardDef& def() const override { return def_; }

    // "If you would play a unit token to a location while I'm at a
    //  battlefield, you may play a 3 [M] Mech unit token to that same location
    //  instead."
    //
    // APPROXIMATION (engine gap): EffectExecutor::createToken offers no
    // replacement hook to cards. The replacement is applied at the next aura
    // pass (each Cleanup): a friendly unit token created since the previous
    // pass, while I'm at a battlefield, becomes a 3 [M] Mech unit token in
    // place (same object, same location and exhausted state). "You may" is
    // taken automatically, and only when it is an upgrade: tokens with 3 or
    // more base Might (and Mechs) are kept. New tokens are told apart by
    // object id: the card remembers state.next_object_id after each pass
    // ("__heim_next_id"), reset when I'm played.
    static constexpr int kMechMight = 3;

    void onPlay(CardContext& ctx) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        ctx.state.getObject(ctx.source).card_counters["__heim_next_id"] =
            static_cast<int>(ctx.state.next_object_id);
    }

    void applyPassiveAura(GameState& state, PlayerId controller) const override {
        forEachSelfOnce(*this, state, controller, [&](GameObjectId /*sid*/, GameObject& self) {
            auto it = self.card_counters.find("__heim_next_id");
            const bool watching = it != self.card_counters.end();
            const GameObjectId first_new = watching ? static_cast<GameObjectId>(it->second)
                                                    : state.next_object_id;
            if (watching && self.isAtBattlefield()) {
                for (auto& [id, obj] : state.objects) {
                    if (id < first_new) continue;
                    if (!obj.isUnit() || !obj.isToken() || obj.controller != controller) continue;
                    if (!obj.location.has_value()) continue;
                    if (obj.base_might >= kMechMight || hasTag(obj, "Mech")) continue;
                    obj.name = "Mech";
                    obj.card_def_id = kInvalidId;
                    obj.base_might = kMechMight;
                    obj.tags = {"Mech"};
                    obj.keywords = KeywordSet{};
                    obj.recomputeMight();
                }
            }
            self.card_counters["__heim_next_id"] = static_cast<int>(state.next_object_id);
        });
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 1078;
        d.def_id = R"RB(rad-132-167)RB";
        d.name = R"RB(Heimerdinger, Academy Instructor)RB";
        d.set_code = R"RB(RAD)RB";
        d.set_name = R"RB(Radiance)RB";
        d.public_code = R"RB(RAD-132/167)RB";
        d.collector_number = 132;
        d.artist = R"RB(Polar Engine Studio)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Order};
        d.tags = {R"RB(Yordle)RB", R"RB(Heimerdinger)RB", R"RB(Piltover)RB"};
        d.energy_cost = 6;
        d.power_cost = 1;
        d.might = 5;
        d.rarity = Rarity::Rare;
        d.ability_text = R"RB(If you would play a unit token to a location while I'm at a battlefield, you may play a 3 [M] Mech unit token to that same location instead.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/1795e905171c74b84b4b7f84a57fecbb4bbd6f75-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_1078(CardRegistry& r) {
    r.registerCard(1078, std::make_unique<HeimerdingerAcademyInstructor>());
}

}  // namespace riftbound
