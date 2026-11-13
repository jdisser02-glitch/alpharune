# List search: what the audit found, what the literature says, what the lab does now (13 Nov 2026)

Read this before touching `lab/ga.py`, `lab/optimize.py`, `lab/card_impact.py` or `lab/deckopt.py`, and before
telling anyone which list is "best". The training side has its own notebook, `lab/learn/TRAINING_KNOWLEDGE.md`.
Section 4 is the log: append to it after every search.

Jeremy, 13 Nov: "j'ai l'impression que tu fais des demi-tâches […] j'aimerais que tu fasses des recherches et que
tu t'instruises. Tu perds du contexte à chaque fois." This file is the answer to the last sentence.

## 0. The tools that exist (inventory, so none is rebuilt by mistake)

| tool | what it does |
|---|---|
| `lab/riftlab.py run SPEC` | plays lists against opponents (`decks` × `opponents`), result in `lab/results/` |
| `lab/tools/merge_results.py NAME` | puts one-opponent results back together as one test (a field test is dispatched one opponent per machine) |
| `lab/optimize.py` | successive halving over explicit candidates or one-for-one swaps |
| `lab/ga.py` | genetic search (population, crossover, 1-3 swaps); see section 1 for why it is not trusted alone |
| `lab/card_impact.py` | improvement-when-drawn of each card of ONE list, from game logs (1 Nov); writes A/B specs |
| `lab/deckopt.py` | the card-level search (13 Nov): explore → model → propose → race → confirm |
| engine `--review-dir D --review-games 0` | `D/games.ndjson`, one line per game: seed, winner, and per player the hand as dealt, the opening hand, cards held (with the turn) and cards played |
| `riftlab.play_batch(..., cards_seat=1|2)` | the same counted per card (`card_exposure`) |

Runners: a plain request is one 4-core machine, 350 minutes at most, about 1,400 games an hour with the trained
pilots (`pmcts:sims=50,…,worlds=8`). Several requests run side by side (each its own machine). Twelve were run at
once on 13 Nov.

## 1. Audit of the genetic search (12 Nov, Volibear, six starts, 25,920 games)

**It ranks luck.** Each list played 72 to 360 games during the search; the three finalists of each start then
played 40 fresh games per opponent (240), and eight lists played 200 per opponent (1,200).

| | lead over the reference |
|---|---|
| the 18 finalists, during the search | +7.8 points on average (from +3.3 to +15.0) |
| the same 18, on the search's own fresh games | −2.3 points on average |

A list's rate on 72 games is known to ±6 points (one standard error), on 360 games to ±2.6; the real differences
between good lists are 2 to 8. Picking the best of 12-16 such numbers picks the luckiest (the best of 16 equal
lists looks about 11 points better than it is on 72 games). The games that made a list survive stay in its
average, so the bias never washes out.

**It returns its starting lists.** The lists that held up on 1,200 games were soldog's tournament list and that
list with 1 to 4 cards changed; none of the changed ones beat it. The search told us which starting list is
best, which a plain test of the starting lists would have told for a fraction of the games.

**The last test was under-powered too.** At 1,200 games per list the standard error of a difference is 2 points:
a real +3 passes "interval above 0" less than half the time.

**What the same games say card by card.** A regression of the win on the card counts over those 25,920 games
gives each card's effect to about ±2 points per copy: only Elder Dragon (+6 per copy) and Thermo Beam (−6) stand
out; Dazzling Aurora is +1.1 ± 2.0 per copy, i.e. not shown to matter. Card effects are small (most within ±2
points a copy), so finding them needs either ten times the games or statistics from inside the games.

**Same seed, near lists.** The engine is deterministic for a seed, and the opponent's deal is the same for two
lists on the same seed: the outcomes of two lists three cards apart correlate at 0.29 (random pilots, 3,000
games), 0.11 for lists 25 cards apart. Our own shuffle is NOT coupled (same opening hand in 25% of games only):
shuffling by a per-card key would raise the correlation and shrink every paired comparison's noise. Not done yet.

## 2. What the literature says (review of 13 Nov; the full report with URLs and what was read in full is in the
session transcript, the PDFs were not kept)

**Budgets.** Published deck searches used 0.25 to 3 million games: García-Sánchez et al. 2018 (Hearthstone,
population 10+10, 200 generations, 128 games per evaluation, about 257k games per hero); Fontaine et al. 2019
(MAP-Elites, 10,000 evaluations × 200 games = 2M per run); Zhang et al. 2021 (same budget plus a surrogate model);
Chen et al. 2018, Q-DeckRec (300 games per evaluation, GA plateaued after about 270 evaluations); Kowalski &
Miernik 2020 (1M games). Zhang: "to evaluate a candidate deck requires at least 200 games even against restricted
opponents". Nobody reports success at about 60 games per evaluation. A broad search at 30-60k games has no
support.

**What worked there.** Small swap mutations with a geometric number of swaps (Fontaine, Zhang). A model trained
online on the decks the search actually proposes, on the bag-of-cards vector (Zhang: every surrogate variant beat
plain MAP-Elites; a linear one beat an MLP trained offline on random decks). Card-level credit (Kowalski &
Miernik's "active genes": only the genes of the cards that were actually drafted are updated; the baseline that
re-evaluated everything "generates average solutions during the first generation that it cannot further
improve").

**What failed there.** Maximising a learned win-rate predictor (Q-DeckRec): it got worse with more samples because
"outlier decks with spuriously high predicted win rates trick" the search, the winner's curse on a model. Only
44% of Zhang's surrogate elites stayed elites when really played. So a model proposes, play decides.

**Noise.** Racing (irace, Hoeffding/Bernstein races): candidates play the same seeds in blocks (one block = one
game per opponent and seat), nobody is dropped before 5 blocks, elites are dropped only after as many blocks as
their challengers. Successive halving (Karnin et al. 2013): halve the field each round with the budget split
evenly over the rounds. Two lists differing by Δ need about 2.9/Δ² games in total to tell apart at 90% (Garivier
& Kaufmann 2016): 3,200 games for 3 points, 1,150 for 5. Chess engines (Fishtest) test every change with a
sequential test on paired games and allow "four or five tries" at most, because each extra try is another chance
for luck. Selection bias: the best of many noisy estimates overstates itself; shrink it,
`m + A/(A+σ²)·(x−m)` with A the spread of the estimates minus their sampling variance (Efron 2011), and never
reuse the games that selected a list to rate it.

**Card statistics from play (17Lands, Limited Magic).** GIH (in hand at some point), OH (opening hand), GNS
(never seen), IWD = GIH − GNS. Their own caveats: GIH flatters late-game cards (long games show more cards and
are won more by the deck that got there), OH is biased by mulligans, and all of them rate a card against the rest
of its deck, not against its replacement (practitioners who cut "underperformers" saw former stars turn
mediocre). The hand as dealt, before the mulligan, depends on the shuffle alone: that comparison is clean. No
published work closes the loop from drawn-vs-not statistics in simulation to constructed-deck search (de
Mesentier Silva et al. 2019 computed them and found only a small correlation with what a nerf does).

**The pilot.** Every deck paper flags it, none solves it: the decks found are the decks the agent plays well
(García-Sánchez: curves shifted low, the AI misplaying specific cards; Fontaine: "dependent on a particular
game-playing agent"). On training breadth, VGC-Bench (Pokémon, 2025): an agent trained on 64 teams beats the
1-team agent 67% on unseen teams, but on its own team the 1-team agent beats the broad ones 70-74%. Breadth buys
generalisation and costs peak skill. ByteDance's Hearthstone agent randomises the first deck-building picks so
the battle policy sees diverse decks. Recipe that follows: before a search, fine-tune the pilot by self-play on
randomised lists (the incumbent plus 0, 1, 2 or 4 random swaps) so every candidate card has been seen; for the
final test give incumbent and finalist the same list-specific fine-tuning; put "cards the pilot never trained on"
in the regression (a clearly negative coefficient means the search measures the pilot, not the deck).

**The field.** Papers that validated only against the opponents and pilot used for fitness could not tell
over-fitting from strength. Keep two or three opposing lists (or ±3-card variants of the six) out of the search
and report the gain on them separately. Co-evolving the field costs millions of games: not affordable.

## 3. The method now (`lab/deckopt.py`)

1. **Explore** (runners, 12 shards): about 720 lists, each a starting list plus a geometric number of random legal
   swaps (mean 3, at most 8), 24 games each (2 per opponent and seat), independent seeds, card exposure recorded.
   Every starting list is played once per shard as it is (the yardsticks). About 17,000 games.
2. **Model** (session): ridge logistic regression of the win on card counts + opponent × seat, the penalty set by
   how much the cards really differ (spread of the effects minus their noise); a covariate for copies of cards
   the pilot never trained on; and per card "dealt vs not dealt" inside the lists that play it.
3. **Propose** (session): the reference, the best starting lists by the model, and 2-9 of the model's best swaps
   from the two best. The model's favourite only earns a place in the race.
4. **Race** (runners, one request per opponent): race1 every candidate, 72 games per opponent on shared seeds;
   race2 the best five + the reference, 288 more. Ranking by shrunk difference.
5. **Confirm** (runners): the best two + the reference (+ a yardstick), 600 games per opponent on new seeds.
   A list is "better" only if its interval against the reference is above 0 there. At most two finalists.

Not done yet, in order of worth: (a) the pilot fine-tuned on randomised lists before a search, and the same
fine-tuning for incumbent and finalist before the confirmation; (b) paired analysis by seed and a coupled shuffle;
(c) opposing lists held out of the search; (d) a sequential stopping rule for the confirmation.

## 4. Log

**13 Nov, Volibear without Dazzling Aurora** (Jeremy: "continue en assumant que Dazzling Aurora est ban"; an
assumption, nothing announced). Six starts of the genetic search were dispatched at 07:05 UTC and cancelled at
07:32 once the audit above was done; the card-level search replaced them (`lab/specs/deckopt/volibear-noaurora-cards.json`,
twelve exploration shards dispatched 07:33 UTC, results in `lab/deckopt/volibear-noaurora-cards/`).
Scaled up at 07:50 UTC after Jeremy's remark that 20 machines make about 670,000 games a day and the searches used
a few tens of thousands: 32 exploration shards instead of 12 (shards 12-19 with 180 lists, 20-31 with 130 once the
first twelve end), about 89,000 games and 3,700 lists. The repository is public, so the standard runners are free;
the limit is 20 jobs at once and 350 minutes a job.

**Why so few games until now.** Not a limit, a choice of mine: each search was sized for an answer in 3-4 hours on
6 machines (about 26,000 games). One day of all 20 machines is about 670,000 games with the full pilot.

**Cheaper pilots (measured 13 Nov, this session's machine, core-seconds per game, startup included).**
sims=50/worlds=8 (the pilot used everywhere): 6.6; sims=16/worlds=4: 2.3; sims=4/worlds=2: 0.47; sims=1/worlds=1
(the policy net alone): 0.14, i.e. 47 times faster. Whether a cheap pilot rates the cards like the full one is
being measured: the same exploration lists (same seed) played with sims=1 and sims=4
(`lab/specs/deckopt/volibear-noaurora-cards-fast1.json`, `-fast4.json`, outputs under `lab/deckopt/`). If the card
effects agree, exploration moves to the cheap pilot at 15-50 times the volume and the full pilot keeps the race
and the confirmation.

## 5. Plan agreed with Jeremy (13 Nov): alternate search and training

Jeremy: "on peut alterner non ? chercher des listes, puis l'entraîner avec ces nouvelles listes/cartes, puis
rechercher des listes, etc." Yes. One round:

1. Search (section 3) with the current pilot. Outcome: the best list, the runners-up, the card table.
2. Train the pilot (continuous training, `lab/learn/TRAINING_KNOWLEDGE.md`) on: the reference, the best lists of
   the search, and random variations of them (each a list plus 0, 1, 2 or 4 random swaps from the search's pool),
   so that every card the next search may try has been played in training. The reference stays in with the same
   weight as the newcomers: a pilot trained mostly on the search's winners would make them win again for that
   reason alone (the loop must not reward itself).
3. Confirm again with the new pilot: the best lists of step 1 and the reference, fresh seeds. A list that was
   ahead only because the old pilot knew it better falls back here.
4. Search again from the lists that held. Stop when two rounds in a row keep the same best list.

To build before round 2: random-variation lists in the self-play deck pool of `lab/learn/loop.py` (today a
specialist trains on a fixed list of files). Step 2 needs Jeremy's go-ahead each time it uses the runners for
training (he paused both tracks on purpose).

## 6. State and next actions (the watchdog reads this; update it every time something is dispatched or finishes)

Jeremy, 13 Nov 07:50 UTC: "ok go, feu vert pour l'entraînement Volibear. Je veux que les machines tournent en
permanence de la façon la plus efficace possible." So: never an idle machine (20 jobs at once at most), training
and search share them, and each finished step starts the next one without asking.

S1 = `lab/specs/deckopt/volibear-noaurora-cards.json` (round 1, pilot spec_volibear_108_v0).

**Running now**
- Round 1 exploration: shards 00-11 (60 lists, dispatched 07:33 UTC), shards 12-19 (180 lists, dispatched 07:50,
  about 3 hours). Shards 20-31 are NOT dispatched: the training took those machines. Exploration is done when the
  20 files `lab/deckopt/volibear-noaurora-cards/explore-00..19.json` exist.
- Volibear training, continuous, with list variations (`"vary"` in `lab/learn/loop.py`: 60% of the self-play pairs
  play a starting list of S1 plus 0, 1, 2 or 4 random swaps): dispatched 07:55 UTC,
  `request=lab/specs/train_spec_volibear.json`, `chain=16` (iterations 119 to about 135, 35-50 minutes each while
  it shares the machines). If no run is in progress or queued, there is no PAUSE file and the ladder
  (`lab/learn/specialists/volibear/ladder.json`) is below iteration 135: dispatch it again with the chain that is
  left. A net is committed every 12 iterations (`current_net`); the head stays in the Actions cache.
- Cheap-pilot calibration, on the session's machine (not the runners): `…-fast1` and `…-fast4`.

**Next, in order (each as soon as its inputs exist)**
1. First twelve shards in: `python3 lab/deckopt.py model S1`, tell Jeremy in three lines what the card table shows
   (preliminary) and what the cheap pilots give (`model` on the two fast specs; compare the card effects: rank
   correlation over the cards with at least 100 lists; write the answer here).
2. All twenty shards in: `model S1`, `propose S1`, check no candidate plays Dazzling Aurora, `wave S1 --stage
   race1`, commit, push, dispatch the six `lab/specs/deckopt/volibear-noaurora-cards.race1.*.json`.
3. Six race1 results (`lab/results/*-volibear-noaurora-cards-race1-*.json`): `rank S1 --stage race1`, `wave S1
   --stage race2`, commit, push, dispatch the six race2 requests.
4. Six race2 results: `rank --stage race2`, `wave --stage confirm`, commit, push, dispatch the six confirm requests.
5. Six confirm results: `rank --stage confirm`, `python3 lab/tools/merge_results.py
   volibear-noaurora-cards-confirm --write`, write `lab/variants/volibear-noaurora-best.txt` if a list's interval
   against the reference is above 0, log it in section 4, dashboard, report to Jeremy (French, plain): this is the
   answer with the OLD pilot, provisional.
6. Training chain ended (ladder at about 135, a `current_net` committed at 132 or later) and step 5 done: round 2
   with the new pilot. Copy S1 to `lab/specs/deckopt/volibear-noaurora-cards-r2.json` with `name` …-r2, `agent` =
   the committed current net, `centres` = round 1's starting lists + its race2 survivors, seeds changed (seed
   1501, race seeds 5201-5203); `requests`, dispatch 20 shards (or the cheap pilot at ten times the lists if step 1
   showed it rates the cards like the full one); then steps 2-5 again. Round 1's best lists are confirmed again
   inside round 2's confirmation: a list that was ahead only because the old pilot knew it better falls back there.
7. While round 2's search runs on 6-20 machines, the training goes on (chain again) with `vary` pointing at the
   round-2 spec, unless round 2 and round 1 agree on the best list (then the alternation has converged: say so to
   Jeremy and ask whether Riven is next).
