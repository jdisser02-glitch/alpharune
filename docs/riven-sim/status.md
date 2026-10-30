# Riven lab — where things stand (2026-10-30, evening)

The goal: a test environment that can tell which Riven list is better against
the current metagame. Everything below serves that goal. Deck results are only
trusted once two gates pass, both measured on 68 real RiftLite games replayed
through the engine (`lab/tools/human_review.py`), and shown at the top of the
dashboard's Tests tab.

## The two gates

| Gate | Target | Morning | Now |
|---|---|---|---|
| Engine follows real games: desyncs per game (median) | ≤ 3 | 17 | 9 |
| … games followed to ≥ 90% of their log | ≥ 90% | 48 of 68 | 59 of 68 |
| … games stopped at the desync cap | — | 19 | 6 |
| Bot agrees with human players (clean decisions) | ≥ 40% | 41% | 34% |

The engine gate is closed; the bot gate dropped because the bot (net_027) was
trained on an engine where about twenty cards were wrong. A training batch on
the corrected engine is running (`lab/learn`, iteration 31 onward, 50
simulations per move, human imitation set rebuilt on the corrected engine:
4,491 decisions).

## What was wrong and is fixed (engine and cards)

- **Riven's own list** (card-by-card audit): The Exile did not ready units an
  effect plays; Blitzcrank, Meditation and Charm did not let their controller
  choose; Defy countered 2-power spells; Valley of Idols buffed the wrong
  unit; a battlefield stayed "contested" after its only contester was
  bounced in a showdown.
- **The Kennen jury decks**: Heart of the Tempest never empowered from effect
  plays; Nocturne never triggered off Lightning Rush or Trifarian Captain;
  Tideturner never swapped; Ride the Wind always moved to base; Last Rites
  played units for free; five more "when you play me" units did nothing.
- **Data bugs across the card pool**: [Temporary] on 17 cards that only
  mention it (Lillia, LeBlanc died at their owner's turn start); static
  Deflect / Shield / Ganking / Tank / Assault on cards that only have them
  conditionally (Fiora paid Deflect twice).
- **Rules**: [Reaction] abilities in showdowns; [Add] abilities resolve at
  once; a ready rune recycled for power keeps its energy; Ambush in the open
  main phase; battlefields hear deaths and hides on them; leaves-the-board
  triggers on a bounce; hidden cards are part of the board snapshot.
- **The replay tool**: many readings of the RiftLite logs (actors, payments,
  undos, hidden rune and deck order, effect moves).

All with regression tests (2004 pass).

## Deck results so far — not validated

Two genetic searches (`lab/ga.py`) and a confirmation test ran on engines
from before most of these fixes. They agree on a direction — fewer Battle
Preparations, Scuttle Crab, Tideturner and Zhonya's Hourglass, more bodies
(Vex, Reckless Innovator, Ki Burst) — but none of the variants separated
from the base list with confidence, and the engine has changed since. They
are a test of the machinery, not advice.

## Tools ready for when the gates pass

- `lab/ga.py` — genetic list search, multi-start (seeded from the 7 meta
  Riven lists), base list kept as a permanent control, common random numbers.
- `lab/card_impact.py` — improvement-when-drawn of every card of a list,
  to choose which cards to question and which to try.
- `lab/tools/review_games.py` — replays a game saved by the dashboard's Play
  tab with the strong bot and annotates every decision.
- GitHub Actions (`.github/workflows/riftlab.yml`) runs any of them from a
  request file pushed to `lab/requests/`.

## Next, in order

1. Fix each game's first divergence until the engine gate passes; separate
   desyncs that are log artefacts (the log shows something the rules forbid)
   from engine errors.
2. Train the bot on the corrected engine until the agreement gate passes
   again.
3. Check the jury: every deck should hold a plausible win rate.
4. Card impact for the Riven list, then the multi-start genetic search with
   300+ games per finalist; a best list is declared only when its interval
   clears the base list's.
