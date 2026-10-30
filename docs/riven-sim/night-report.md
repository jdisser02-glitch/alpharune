# Night report, 2026-10-29 → 30

What happened while you slept, with the numbers. Everything is committed on
`claude/zen-goodall-hb2ssu`; the dashboard at
https://claude.ai/artifact/8M2z78x8tFJbpjvp6ewJxs is republished with the
latest ladder (Bot tab) and plays with the newest net (Play tab).

## In one paragraph

The bot now learns. After phase 2 landed (01:25 UTC) the self-play loop ran
12 iterations on its own, five of them accepted in a row, taking the current
net from Elo 493 to 759 at its peak (net_014) and from 94 % to 99 % against
random play. One engine bug found by the loop itself (an infinite showdown
with Ekko, Revolutionary) cost 46 minutes and is fixed with a test. Agreement
with human play held at 0.38 to 0.41 top-1 but did not improve with self-play,
which is the main open problem. The deck question (Baron Nashor) is still
"probably +5 points, not proven".

## Engine fixes

Found by replaying the 68 RiftLite human games through the engine and by the
self-play loop. All have tests; 1938 tests pass.

| Fix | Why it mattered |
|---|---|
| CR 144.3 group moves | several units moving together were treated as separate moves |
| CR 323.13 showdown → combat | a showdown at a contested battlefield now turns into combat the way the rules say |
| Hidden [A] cost, play location before triggers, Quick-Draw targets, facedown capacity reset, aura cost modifiers, gear death and "hold here" for gear, Astral Heron counting the played card | each one was a desync between a human game and the engine |
| `recycleCards` leaving the card in its old zone | the source of every "rejected world" in the search; now 0 rejected |
| **Showdown activated-ability costs (tonight, 1e12df7)** | the showdown generator checked only [E]. Ekko, Revolutionary's "Discard 1, [1][R]: banish me, then play me to your base" was offered and paid with an empty hand and no runes, and the bot replayed it forever. One shared gate (`canPayActivationCost`) now serves the main-phase, showdown and closed-state generators; the search bounds single-option steps; `RIFTBOUND_LOG_STREAM=1` streams a game's trace so the next hang can be read; the loop kills a batch after 20 minutes |

## Training, phase 1 and 2 (what was built)

- **Own ISMCTS searcher (`pmcts`)**: PUCT with a prior hook, determinised
  worlds reused at the root, root parallelism. Beats OpenSpiel's ISMCTS 83–17
  at equal simulations.
- **Resume from any decision site**: the engine records where it is paused
  (main phase, mulligan, chain priority, a card's choice, showdown focus,
  combat damage) and a search clone re-enters that loop from a state copy
  instead of replaying the game. ×2.4 search throughput, 0 rejected worlds.
- **Two-head net (RBPN)**: value + policy over the location-aware action
  vocabulary (22,971 slots), sparse first layer. Trained on search visit
  distributions plus the human decisions (weight 0.3), with an entropy bonus
  and a soft pull toward the previous net.
- **Loop with gating**: 200 self-play games per iteration over 24 meta decks,
  SPRT against the current net (stop early when clear, else the rate after
  80–240 games, accept ≥ 50 %), plus 100 games against random and against a
  fixed reference (net_006) so the yardstick does not move. The gate was
  55 %/200 games until tonight; nothing passed and progress could not
  compound, so it was loosened at 01:56 UTC.

## Ladder

Player = the net that generated the self-play games; "vs current" = the gate
match against the net that was current at the time; Elo is relative to our
own nets (random play ≈ 0); human top-1 = share of the 1,702 human decisions
where the net's first choice is the human's move.

| It. | Player | vs random | vs current (games) | vs net_006 | Elo | Human top-1 | Accepted |
|---|---|---|---|---|---|---|---|
| 0 | random | 63 % | 85 % (40) | | | | yes |
| 1–5 | ismcts, value net | 58–75 % | 42–52 % (40–60) | | | | 3, 4 |
| 6 | pmcts, net_004 (value) | 94 % | 53 % (360) | | 465 | 0.38 | no (old gate) |
| 7 | pmcts, net_004 | 94 % | 35 % (80) | 45 % | 436 | 0.39 | no |
| 8 | pmcts, net_004 | 99 % | 55 % (240) | 57 % | 575 | 0.38 | **yes** |
| 9 | pmcts, net_008 (policy) | 98 % | 52 % (240) | 66 % | 616 | 0.37 | **yes** |
| 10 | net_009 | 98 % | 59 % (200) | 57 % | 639 | 0.39 | **yes** |
| 11 | net_010 | 97 % | 58 % (240) | 64 % | 657 | 0.39 | **yes** |
| 12 | net_011 | 99 % | 66 % (80) | 72 % | 729 | 0.38 | **yes** |
| 13 | net_012 | 96 % | 40 % (80) | 68 % | 638 | 0.39 | no |
| 14 | net_012 | 99 % | 63 % (120) | 76 % | 759 | 0.39 | **yes** |
| 15 | net_014 | 99 % | 51 % (200) | 73 % | 733 | 0.38 | no |
| 16 | net_014 | 99 % | 55 % (240) | 66 % | 723 | 0.38 | **yes** |
| 17 | net_016 | 99 % | 51 % (240) | 73 % | 720 | 0.39 | no |
| 18 | net_016 | 99 % | 41 % (80) | 71 % | 677 | 0.39 | no |
| 19 | net_016 | 97 % | 50 % (200) | 69 % | 691 | 0.41 | no |

Current net: **net_016** (value head bundled in the dashboard's Play tab).
One iteration takes about 16 minutes on the 4-core session machine (3.5–4.5
minutes of self-play, 6 of training, the rest gating).

Reading: iterations 8–12 are a clean climb; from 13 on, candidates trained
from the same net alternate between accepted and rejected and the Elo
estimate wobbles between 680 and 760. That is the signature of a loop that
has exhausted what 30 simulations per move can teach it, not of a bug.

## Human agreement, before and after

| Bot | Decisions | Top-1 agreement |
|---|---|---|
| value-only net_004, pmcts sims=300 (full replay of the 68 games) | 1,702 | 20.7 % |
| policy nets, net head alone, all decisions | 1,702 | 38–41 % |
| policy nets, "clean" decisions only (engine state matched the human game exactly) | ~1,000 | 27–28 % |

The jump from 21 % to 38 % comes from imitation (the human set is in the
training mix), not from self-play: it is flat across iterations 6–19 while
Elo rose by 250. The honest number is the "clean" one. The earlier 29 %
figure in the dashboard was inflated by an older searcher evaluating drifted
states and has been replaced.

## Deck lists

No new runs tonight (the CPU went to training). Standing results:

- Baron Nashor swap: 57.2 % vs 52.1 % for the base list over 600 games, the
  intervals overlap. Worth a 1,000-game confirmation with the new bot.
- The one-card list search (riven_flex_1) finalists are unchanged.

## Dashboard

- Published 17 times; the current version carries the ladder through
  iteration 17 and net_016.
- Battlefield panels and a combat mode on the table (lanes with might totals,
  verdict, chain, action rings), card art for every card as sprite sheets,
  the hand made prominent on the Play tab, rules text for all cards.
- The build script now bundles the value head of a two-head net, since the
  browser engine reads only that format.

## What is next, in order

1. **More simulations per move** (100–200) for the self-play games, now that
   the resume-anywhere engine makes them cheap, and gate at the same depth.
   This is the lever for the plateau at iteration 13+.
2. **Human agreement as a gate signal**: weigh the human set higher or add a
   KL penalty toward the human policy on the clean decisions, and report
   "clean" top-1 per iteration in the ladder.
3. **Re-run the human review** with net_016 driving the search (the review
   still uses net_004) so the Replays tab compares against the current bot.
4. **Baron Nashor at 1,000 games** with the new bot, then the next list
   search (riven_flex_2) with the same pilot.
5. Remaining engine gaps from the review desync log: can't-move-this-turn,
   effect moves not counting, Aura/Reaction for spells, first gear per turn,
   mandatory spell costs.

The loop was left running (it stops after iteration 28 or when the machine
goes away); each new row is committed to `lab/learn/ladder.json`.
