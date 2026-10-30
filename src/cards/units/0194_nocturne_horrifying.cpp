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

class NocturneHorrifying : public UnitCard {
public:
    const CardDef& def() const override { return def_; }
    unsigned choicesAtFinalization() const override { return kFinalizeTrigger; }
    // "As you look at or reveal me from the top of your deck, you may banish
    // me. If you do, you may play me for [A]." [Ganking] engine-handled.
    // The two nested "may"s are collapsed into one decision (the resume-slot
    // helpers run once per resolution). The [A] cost is approximated as
    // ignoring-cost — there's no clean cost-payment cursor in a reveal-trigger
    // context (TODO).
    // CR 359 example (Nocturne): the replacement applies as the look/reveal
    // happens. The engine puts this ability on the chain instead, so it is
    // decided and resolved right after the looking effect, which may already
    // have put me where it puts "the rest" — the trash (Lightning Rush) or
    // the deck bottom (Trifarian Captain). I am therefore banished from the
    // deck or from the trash; drawn to hand, I am no longer reachable.
    TriggerType triggerType() const override { return TriggerType::WhenIRevealedFromTop; }
    void onTrigger(CardContext& ctx, const std::vector<GameObjectId>&) override {
        if (!ctx.state.objectExists(ctx.source)) return;
        PlayerId owner = ctx.state.getObject(ctx.source).owner;
        auto reachable = [&] {
            if (!ctx.state.objectExists(ctx.source)) return false;
            const auto zone = ctx.state.getObject(ctx.source).zone;
            return zone == ZoneType::MainDeck || zone == ZoneType::Trash;
        };
        if (!reachable()) return;  // already left the deck top for good
        int conf = confirmOptional(ctx, "Nocturne: banish from deck and play for [A]?",
                                    reachable);
        if (finalizingChoices(ctx)) return;   // choices end here (CR 383.3)
        if (conf != 1) return;
        // banishObject drops me from the deck / trash list I am in.
        ctx.executor.banishObject(ctx.source);
        auto& bz = ctx.state.player(owner).banishment;
        bz.erase(std::remove(bz.begin(), bz.end(), ctx.source), bz.end());
        ctx.executor.playIgnoringCost(owner, ctx.source,
                                      LocationId{BaseLocation{owner}});
        ctx.events.logTrace("NOCTURNE: banished from deck top and played for [A]");
    }
private:
    const CardDef def_ = [] {
        CardDef d;
        d.id = 194;
        d.def_id = R"RB(ogn-194-298)RB";
        d.name = R"RB(Nocturne, Horrifying)RB";
        d.set_code = R"RB(OGN)RB";
        d.set_name = R"RB(Origins)RB";
        d.public_code = R"RB(OGN-194/298)RB";
        d.collector_number = 194;
        d.artist = R"RB(Six More Vodka)RB";
        d.card_type = CardType::Unit;
        d.super_type = SuperType::Champion;
        d.domains = {Domain::Chaos};
        d.tags = {R"RB(Nocturne)RB"};
        d.energy_cost = 4;
        d.power_cost = 1;
        d.might = 4;
        d.rarity = Rarity::Rare;
        d.keywords.set(Keyword::Ganking);
        d.ability_text = R"RB([Ganking] (I can move from battlefield to battlefield.)
As you look at or reveal me from the top of your deck, you may banish me. If you do, you may play me for [A].)RB";
        d.image_url = R"RB(https://cmsassets.rgpub.io/sanity/images/dsfx7636/game_data_live/19a751364bc6eb5297596e8733d0d30a1111ac78-744x1039.png)RB";
        return d;
    }();
};

}  // anonymous namespace

void register_card_194(CardRegistry& r) {
    r.registerCard(194, std::make_unique<NocturneHorrifying>());
}

} // namespace riftbound
