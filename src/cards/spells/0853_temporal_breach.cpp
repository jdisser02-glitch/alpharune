#include "cards/card.h"
#include "cards/card_helpers.h"
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

class TemporalBreach : public SpellCard {
public:
    const CardDef& def() const override { return def_; }
    // "Banish a unit, then its owner plays it to the same location, ignoring
    //  its cost."
    TargetRequirements getTargetRequirements() const override {
        return TargetRequirements{.count = 1, .must_be_unit = true};
    }
    bool needsPlayTimeTarget() const override { return true; }
    void onResolve(CardContext& ctx, const std::vector<GameObjectId>& /*targets*/) override {
        auto legal = enumerateLegalTargets(ctx.state, ctx.controller);
        GameObjectId picked = pickTarget(ctx, "Temporal Breach", legal);
        if (picked == kInvalidId || !ctx.state.objectExists(picked)) return;
        const PlayerId owner = ctx.state.getObject(picked).owner;
        const auto where = ctx.state.getObject(picked).location;
        const bool token = ctx.state.getObject(picked).isToken();
        if (!where.has_value()) return;

        // Attached gear does not follow the unit out of play.
        const std::vector<GameObjectId> gear = ctx.state.getObject(picked).attachments;
        for (auto g : gear) ctx.executor.unattachGear(g);

        ctx.executor.banishObject(picked);
        // CR 183.1: a banished token ceases to exist — nothing to play back.
        if (token) {
            auto& bz = ctx.state.player(owner).banishment;
            bz.erase(std::remove(bz.begin(), bz.end(), picked), bz.end());
            return;
        }
        // The owner cannot play it there ("can't" beats "can", CR 054.2): a
        // battlefield where units can't be played (Rockfall Path), a
        // battlefield while they may only play units to their base (Mageseeker
        // Warden), or another player's base (Vendetta FAQ, Temporal Breach and
        // Hostile Takeover). The play fails its legality check and the unit
        // stays in banishment.
        if (!ownerCanPlayUnitAt(ctx.state, owner, *where)) {
            ctx.events.logTrace("TEMPORAL BREACH: " + ctx.state.getObject(picked).name +
                                " can't be played there; it stays banished");
            return;
        }
        auto& bz = ctx.state.player(owner).banishment;
        bz.erase(std::remove(bz.begin(), bz.end(), picked), bz.end());

        // It comes back as a new object: no damage, buffs, statuses or roles.
        auto& unit = ctx.state.getObject(picked);
        unit.damage_marked = 0;
        unit.buff_count = 0;
        unit.temp_might_bonus = 0;
        unit.is_stunned = false;
        unit.empowered = 0;
        unit.empower_flip_eot = 0;
        unit.combat_designation = CombatDesignation::None;
        ctx.executor.playIgnoringCost(owner, picked, *where);
    }
private:
    static bool ownerCanPlayUnitAt(const GameState& state, PlayerId owner, const LocationId& where) {
        if (const auto* base = std::get_if<BaseLocation>(&where)) return base->player == owner;
        if (const auto* at = std::get_if<BattlefieldLocation>(&where)) {
            if (state.player(owner).units_play_base_only) return false;
            for (const auto& bf : state.battlefields)
                if (bf.id == at->id) return !bf.blocks_unit_play;
        }
        return true;
    }
    const CardDef def_ = [] {
        CardDef d;
        d.id = 853;
        d.def_id = R"RB(ven-066-166)RB";
        d.name = R"RB(Temporal Breach)RB";
        d.set_code = R"RB(VEN)RB";
        d.set_name = R"RB(Vendetta)RB";
        d.public_code = R"RB(VEN-066/166)RB";
        d.collector_number = 66;
        d.artist = R"RB(Kudos Productions)RB";
        d.card_type = CardType::Spell;
        d.domains = {Domain::Mind};
        d.energy_cost = 2;
        d.power_cost = 1;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Hidden);
        d.ability_text = R"RB([Hidden] (Hide now for [A] to react with later for [0].)
Banish a unit, then its owner plays it to the same location, ignoring its cost.)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/7c670dc3d997c52b90d67043989b53c67a72f4c4-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_853(CardRegistry& r) {
    r.registerCard(853, std::make_unique<TemporalBreach>());
}

}  // namespace riftbound
