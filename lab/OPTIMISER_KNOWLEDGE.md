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

**Done**
- 09:05 UTC, step 1. Twelve shards in (720 lists, 17,280 games, 45 minutes a shard: about 1,900 games per
  machine-hour). The model was first fitted with every opponent counting the same; that is wrong (Riven is half
  the field and a third of those games), so `fit` now re-weights to the field. Field-weighted table: overall 70.6%;
  **a copy of a card the pilot never trained on costs 1.4 points (± 0.4)**: the pilot bias is real and large next
  to the card effects. Clear cards, points per copy: Elder Dragon +3.2 (± 0.6), Seraphic Wyvern +2.4, Kadregrin
  +1.4, Deadbloom Predator +1.2, Rengar +1.2, Mobilize +1.1; Challenge −1.9, Thermo Beam −1.9, Trinity Force −1.8
  (−8.3 ± 1.2 when it is in the dealt hand), Avalanche −1.6, Platewyrm Egg −1.0. Starting lists as they are (288
  games each, about ± 2.7): f3 81.0%, Ritualo 79.8%, soldog-runner 78.8%, f6 78.4%, soldog-topend 77.3%, the other
  soldog replacements 68-75%, Laggy 71.8%, oryzias 37%. **Without Dazzling Aurora, soldog's shell no longer
  stands above Ritualo's list** (with it, it led by about 7 points on 12 Nov).
- Cheap pilots (`lab/tools/pilot_agreement.py`, the same 720 lists, halves of the shards against each other, 18
  cards in at least 200 lists): "dealt against not dealt" agrees across pilots as well as the full pilot agrees
  with itself (full with itself 0.73; full with sims=4 on different games 0.78; with sims=1 0.77); the regression
  effects are noisier (0.52 with itself; 0.59 and 0.39 across). So the cheap pilots rate the common cards like
  the full one, as far as 17,000 games can tell; nothing is known yet for the rarely played cards. sims=1 rates
  Ritualo's list 5 points higher than the other two do (88.9% against 84%, unweighted, 288 games): sims=4 is the
  cheap pilot to use.
- 09:05 UTC: 36 more shards with sims=4 on the session's machine (spec `…-fast4w`: new lists, opponents drawn by
  their weight, five times as many lists around Ritualo's; about 52,000 games in 110 minutes; written outside the
  repository and moved into `lab/deckopt/volibear-noaurora-cards-fast4w/` when done). S1 now lists both cheap
  sets under `explore_from`: its model uses them with their own intercepts.

- 10:45 UTC, step 2. All 20 runner shards in (2,160 lists) and 32 of the 36 local cheap-pilot shards: model on
  4,800 lists and 115,200 games (full and cheap pilots together, each with its own intercepts). A copy of a card
  the pilot never trained on: −2.0 points (± 0.2). Points per copy (± 0.2 to 0.5): Elder Dragon +5.1, Seraphic
  Wyvern +2.3, Gentle Gemdragon +2.3, Blazing Scorcher +1.4, Rengar +1.3, Mobilize +1.3, Dune Drake +1.1, Deadbloom
  Predator +1.0, Jayce +1.0; Thermo Beam −3.5, Unyielding Spirit −3.1, Trinity Force −2.7, Challenge −2.4,
  Avalanche −2.2, Sabotage −1.9, Ravenborn Tome −1.7, Falling Star −1.5, Show of Strength −1.4, Portable Portal
  −1.1. Starting lists as they are, full pilot, 480 games each, field-weighted: f6 79.2%, Ritualo 78.3%, f3 77.9%,
  soldog-runner 77.6%, soldog-drake 76.8%, the rest 66-75%, oryzias 36%.
  `propose` now charges a card the pilot does not know its measured cost and adds improvements of the reference.
  Fourteen lists race (`lab/deckopt/volibear-noaurora-cards/candidates.json`): the reference; the starting lists
  f6, soldog-gemdragon, soldog-topend, f3; the reference with the model's 2, 4, 6 best swaps (Show of Strength out
  for Rengar first); f6 with 2, 4, 6, 9; soldog-gemdragon with 3, 6. The model predicts +5 to +24 points for the
  swapped lists: additive extrapolation, not to be believed before the race.
  Race 1 dispatched 10:47 UTC: six requests `lab/specs/deckopt/volibear-noaurora-cards.race1.*.json`, 432 games
  per list shared out by the opponents' weights (110, 110, 90, 76, 28, 18), seed 5101.

- 11:45 UTC, step 3. Race 1 (432 games per list, full pilot, seed 5101): the reference 72.5% (it had 78.3% on its
  480 exploration games: the two together put it near 75.5%), the candidates between 73.6% and 82.6%; spread minus
  noise: the candidates really differ by about 1.2 points only, so the order among them is mostly luck. Top five
  to race 2: ref+2 82.6% (Ritualo, two Show of Strength out for two Rengar), c2+3 82.4% (soldog-gemdragon with
  Trinity Force and Avalanche out), ref+4 81.5%, c1+4 81.0%, centre2 80.3% (soldog-gemdragon as it is). The lists
  with the most swaps did worst (ref+6 73.6%, c1+9 75.4%): the model's additive prediction over-reaches beyond
  about four swaps, as expected. Cheap pilot on the same fourteen lists (408 games each so far): reference 77.2%,
  the candidates 77.7-84.3%; it too puts the candidates a few points above the reference.
  Race 2 dispatched 11:47 UTC: six requests `…race2.*.json`, 1,728 more games per list by weight (440, 440, 356,
  308, 112, 70), seed 5102; `rank --stage race2` adds race 1's games.

- 13:25 UTC, step 4. Race 2 with race 1's games (2,158 games per list, full pilot): reference 75.3%; ref+2 82.9%,
  +7.6 [+5.3, +10.0]; c2+3 82.7%, +7.4 [+5.0, +9.7]; ref+4 82.4%, +7.1 [+4.8, +9.5]; c1+4 80.8%, +5.5; centre2
  78.6%, +3.3. The gain is against Riven (65% for the reference, 71-78% for the others) and Kai'Sa (79% → 85-89%).
  These five were picked on race 1's games, which are in those numbers: the confirmation's fresh seeds decide.
  Cheap pilot against full pilot on the fourteen raced lists (864 against 432 games each): correlation of the
  rates 0.64, about what two noisy measures of the same thing give.
  Confirmation dispatched 13:27 UTC: six requests `…confirm.*.json`, the reference, ref+2 (Ritualo with two Show of
  Strength out for two Rengar), c2+3 (soldog's deck with Aurora replaced by Gentle Gemdragon, then Trinity Force,
  Avalanche and a Portable Portal out for Seraphic Wyvern and two Blazing Scorcher) and soldog WITH Aurora as the
  yardstick; 3,600 games per list by weight (918, 918, 744, 642, 234, 146), seed 5103, about two hours.
  On the session's machine: a cheap-pilot race of race 2's six lists, 9,600 games per list
  (`lab/results/*-volibear-noaurora-cards-cheaprace-*.json`, merged by `merge_results.py`).

- 16:10 UTC, step 5. **Round 1's confirmation** (fresh seeds, 3,602 games per list, full pilot spec_volibear_108_v0,
  `lab/results/20261113-155901-volibear-noaurora-cards-confirm.json`): Ritualo 76.9%; ref+2 81.2%, +4.3 [+2.5,
  +6.1]; c2+3 82.5%, +5.6 [+3.8, +7.4]; soldog WITH Aurora 82.2%, +5.4 [+3.6, +7.2]. c2+3 against soldog with
  Aurora +0.2 [-1.5, +1.9]: **the assumed ban costs nothing that 3,600 games can see**. c2+3 against ref+2 +1.3
  [-0.5, +3.0] (cheap pilot, 9,588 games each: +1.6 [+0.5, +2.6]). Both finalists pass (interval above 0): the first
  time a search of this lab finds lists that hold on fresh games. `lab/variants/volibear-noaurora-best.txt` = c2+3:
  soldog's main deck with −3 Dazzling Aurora, −1 Trinity Force, −1 Avalanche, −1 Portable Portal, +3 Gentle
  Gemdragon, +1 Seraphic Wyvern, +2 Blazing Scorcher. Runner-up ref+2: Ritualo's list with 2 Show of Strength out
  for 2 Rengar. Provisional: the pilot had not trained on variations.
  Cheap pilot (sims=4) against full pilot at list level: cheap race (9,588 games per list) reference 77.4%, ref+2
  +4.0, c2+3 +5.6, ref+4 +4.3, c1+4 +4.8, centre2 +3.1; full confirmation ref+2 +4.3, c2+3 +5.6. **The cheap pilot
  gave the confirmation's answer** on these lists, at a fourteenth of the cost. Races can run on it; the last
  confirmation stays with the full pilot.
  What the search cost: 115,200 exploration games (63,360 of them cheap, on the session's machine), 6,048 + 10,368
  in the races, 14,408 in the confirmation, 57,528 cheap in the cheap race: about 10 hours of wall time.

- 16:25 UTC: **hold-out check** dispatched (five requests `lab/specs/deckopt/volibear-noaurora-cards.holdout.*.json`,
  results `lab/results/*-volibear-noaurora-cards-holdout-*.json`, then `python3 lab/tools/merge_results.py
  volibear-noaurora-cards-holdout --write`): the reference, ref+2, c2+3 and soldog with Aurora against five lists
  that were NOT in the search's field and that their pilots know (DiamondHat's and Caravaggio's Riven, IcrazyFool's
  Kai'Sa, Yizhenglong's Kennen, Alex Gorica's Akali), 1,800 games per list by weight, seed 5104. If the gain over
  the reference holds there, it is not an over-fit to the six lists of the search; if it does not, say so plainly.
  On the session's machine: cheap-pilot exploration around the four leading lists (`…-fast4d`, 36 shards).

- 16:35 UTC: **round 1b on the session's machine, cheap pilot only** (`lab/specs/deckopt/volibear-noaurora-cards-b.json`,
  `explore_from` = the full-pilot exploration and the four cheap sets): card model on 9,360 lists and 224,640
  games (± 0.15 to 0.4 a copy): Elder Dragon +4.6, Seraphic Wyvern +1.9, Gentle Gemdragon +1.8, Rengar +1.2, Dune
  Drake +1.1; Thermo Beam −3.3, Unyielding Spirit −2.6, Trinity Force −2.2, Sabotage −1.6, Avalanche −1.5,
  Challenge −1.5, Morgana −1.4; a card the pilot never trained on −1.9 (± 0.16) a copy. Thirteen candidates built
  from c2+3, c1+4, ref+4, ref+2 (first swaps: Punch First out for Dune Drake) race locally, 2,400 games each
  (`lab/results/*-volibear-noaurora-cards-b-race1-*.json`); then `wave … --stage race2`, run the six specs with
  `riftlab.py run`, `rank`. Its leaders become extra starting lists of round 2; they are not ratings.

**Next, in order (each as soon as its inputs exist)**
0. Five hold-out results in: merge, add the table to section 4 and tell Jeremy in three lines.
1. (done, above) First twelve shards in: `python3 lab/deckopt.py model S1`, tell Jeremy in three lines what the card table shows
   (preliminary) and what the cheap pilots give (`model` on the two fast specs; compare the card effects: rank
   correlation over the cards with at least 100 lists; write the answer here).
2. (done, above) All twenty shards in: `model S1`, `propose S1`, check no candidate plays Dazzling Aurora, `wave S1 --stage
   race1`, commit, push, dispatch the six `lab/specs/deckopt/volibear-noaurora-cards.race1.*.json`.
3. (done, above) Six race1 results (`lab/results/*-volibear-noaurora-cards-race1-*.json`): `rank S1 --stage race1`, `wave S1
   --stage race2`, commit, push, dispatch the six race2 requests.
4. (done, above) Six race2 results: `rank --stage race2`, `wave --stage confirm`, commit, push, dispatch the six confirm requests.
5. (done, above) Six confirm results: `rank --stage confirm`, `python3 lab/tools/merge_results.py
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
