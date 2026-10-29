# Stage 2 — Riven playable (2026-10-29)

## Cards added (C++ classes, ids from the reserved Radiance range 961..1100)

| Id | Code | Card | File |
| ---: | --- | --- | --- |
| 961 | RAD-149 | Riven, The Exile (legend; printed name "The Exile") | `src/cards/legends/0961_the_exile.cpp` |
| 962 | RAD-150 | Blade of the Exile (signature gear) | `src/cards/gear/0962_blade_of_the_exile.cpp` |
| 963 | RAD-043 | Riven, Broken Blade (champion unit) | `src/cards/units/0963_riven_broken_blade.cpp` |
| 964 | RAD-107 | Bedeviling Stranger | `src/cards/units/0964_bedeviling_stranger.cpp` |
| 965 | RAD-105 | Reckless Innovator | `src/cards/units/0965_reckless_innovator.cpp` |
| 966 | RAD-163 | Hunters' Circle (battlefield) | `src/cards/battlefields/0966_hunters_circle.cpp` |
| 967 | RAD-165 | Rakelstake (battlefield) | `src/cards/battlefields/0967_rakelstake.cpp` |
| 968 | RAD-032 | Battle Preparations | `src/cards/spells/0968_battle_preparations.cpp` |
| 969 | RAD-101 | An Offer You Can't Refuse | `src/cards/spells/0969_an_offer_you_can_t_refuse.cpp` |

967–969 go beyond the six requested cards: the Sacramento list plays
Rakelstake and Battle Preparations, and Build B plays An Offer You Can't Refuse;
without them the deck files do not load.

## Engine hooks (generic, no card-specific logic in the engine)

- `TriggerType::WhenIBecomeExhausted`: fired from `ObjectStateChangedEvent "exhausted"`
  (standard move, `[E]` costs, `EffectExecutor::exhaustObject`). `exhaustObject`
  no longer announces an object that was already exhausted.
- `Card::grantsEnterReadyTo`: asked of the controller's board and legend in
  `resolvePermanent` (The Exile).
- `Card::equipOptions` / `Card::onEquipWith`: equip costs with alternatives; one
  equip intent per (unit, way to pay), extra targets carry the unit to exhaust.
- `BattlefieldCard::playCostReductionHere` + `GameEngine::playLocationDiscount`:
  location-dependent discount, applied in the unit play generator (per
  destination) and staged into `transient_play_discount` when paying.
- `BattlefieldState::last_play_here_turn` + `ObjectStateChangedEvent "first_play_here"`
  → `TriggerType::WhenFirstPlayHereEachTurn` on the battlefield card (Rakelstake).
- Chosen Champion from the Champion Zone can now be played to a battlefield its
  controller controls (CR 420.1.a; previously base only). Needed for The Exile
  to matter for Riven, Broken Blade; applies to every deck.
- `GameEngine::testHook_runChain` (test seam).

## Deviations from the official text

- **RAD-149 The Exile, RAD-032 Battle Preparations**: absent from `cards/json`
  and `cards/card_index.json`; text taken from the published card text (web).
  Battle Preparations' Power cost is assumed 0 (not confirmed).
- **Battle Preparations**: [Show Off] is not a choice; the player shows off
  whenever they have a gear in hand or a friendly gear on board (revealing is
  free and hidden information is not modelled). Checked as the spell resolves,
  not when it is played. "Ready up to 2 runes" always readies the first two
  exhausted runes found.
- **Riven, Broken Blade**: the two "spend my buff" modes are offered only when
  she has a buff (and, for the stun, an enemy unit is choosable). The chosen
  mode is recorded when the ability resolves, so two of her triggers finalized
  before either resolves could pick the same mode.
- **Bedeviling Stranger**: the "you may" is asked before the battlefield, and
  only battlefields where the bounce is possible (4+ units and a choosable
  enemy) are offered. Declining is equivalent to choosing a battlefield with
  fewer than 4 units, so the outcome set is the same.
- **Blade of the Exile**: under OpenSpiel/MCTS the equip intents share the
  existing equip-target hash slots (4 per gear), so the [A] and exhaust options
  can collide in the action vocabulary. Random-vs-random (BatchRunner) sees all
  options.
- **Hunters' Circle**: the engine plays gear to base only (CR 149.2), so the
  gear half of the discount never applies; units only.
- **Riven, Deserter (RAD-114)**: not implemented (optional).

## Tests

`tests/cards/test_riven_radiance.cpp`, 16 tests: enter-ready (BF / base / no
legend / champion-zone play), Blade equip by exhausting a unit and for [A],
Innovator Gold (exhausted, draw not offered without Chaos power) and pay-[P]-draw,
Accelerate keyword, Stranger bounce with 4 units and no offer with 3, Hidden
keyword, Broken Blade buff on move, the three modes with the once-per-turn
restriction and reset next turn, Hunters' Circle discount (first play only,
only that battlefield, not on the opponent's turn).

Full suite: 1511 tests, 1511 passed, 0 failed, 1 disabled (upstream).

## Games

Random vs random, seed base 42, 100 games:

| P1 | P2 | Threads | Reported rate | P1–P2 | Avg turns |
| --- | --- | ---: | ---: | --- | ---: |
| riven_sacramento | kaisa_test (Viktor) | 1 | 192.2 games/s | 56–44 | 17 |
| riven_sacramento | kaisa_test (Viktor) | 4 | 807.2 games/s | 56–44 | 17 |
| riven_sacramento | miss_fortune_test | 1 | 253.0 games/s | 40–60 | 17 |
| riven_sacramento | miss_fortune_test | 4 | 882.1 games/s | 40–60 | 17 |
| riven_build_b | kaisa_test (Viktor) | 4 | 806.3 games/s | 73–27 | 16 |

Random play says nothing about deck strength; these runs show the decks load
and every game ends. A 40-game traced sample (Sacramento vs Build B) logged
186 enter-ready grants, 138 Broken Blade modes (99 buff / 18 stun / 21 ready),
109 Innovator triggers, 55 Blade equips by exhaustion, 39 Rakelstake predicts,
26 Battle Preparations and 1 Stranger bounce.
