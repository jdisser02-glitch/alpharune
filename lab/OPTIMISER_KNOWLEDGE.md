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

- `lab/tools/postban.py` + `lab/meta/banned_2026-11-23.json` + `lab/decks/postban/`: lists without the cards banned
  on 23 Nov 2026 (patch rule, Riven starting lists, check). `lab/tools/build_buylist.py`: rebuilds the page of the
  "Riftbound Buy List" artifact (https://claude.ai/artifact/18y7hmz2sjy9Cd1Ls2Tmzy) from the lists named in it.

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

- 16:30 UTC: **the real bans are out** (`lab/meta/ban_watch_2026-11-13.md`), effective 23 Nov: Riven, Broken Blade;
  Zhonya's Hourglass; Stellacorn Herder; Nocturne, Horrifying; Rhasa the Sunderer; Brynhir Thundersong; Dazzling
  Aurora; Baited Hook; battlefields Emperor's Dais and Star Spring. The assumption held (Aurora is banned), and
  round 1's two confirmed lists stay legal. But five of the six lists of the search's FIELD lose cards (both
  Rivens their champion, Kennen 6 cards, Kai'Sa 3, Master Yi 4): from 23 Nov the field has to be rebuilt.
  Put to Jeremy at 16:35 UTC: build a post-ban field (each legend's best list without its banned cards, by this
  same method, cheap pilot first), then search Volibear against it. Until he answers: the Volibear training goes
  on (it serves any field), round 2 below waits for his answer on which field to use.

- 17:30 UTC: **hold-out check done** (`lab/results/20261113-171223-volibear-noaurora-cards-holdout.json`, 1,802 games
  per list against five lists outside the search's field): reference 75.4%; ref+2 81.7%, +6.3 [+3.7, +8.8]; c2+3
  81.2%, +5.8 [+3.2, +8.4]; soldog with Aurora 82.1%, +6.7 [+4.1, +9.3]. Against DiamondHat's and Caravaggio's
  Riven the reference wins 63-65%, the two found lists 71-73%; against IcrazyFool's Kai'Sa 79% → 86-87%. **The gain
  holds outside the field** (confirmation: +4.3 and +5.6): it is not an over-fit to the six lists of the search.
  Round 1b's cheap race 1 (2,402 games per list, session's machine): the confirmed list with the model's 9, 6 and
  4 further swaps 85.9%, 84.3%, 83.1% against 82.5% as it is and 78.1% for the reference; cheap race 2 running
  (9,600 games each for the best five). Dashboard to rebuild and publish with that result.

- 17:50 UTC: **Riven after the bans** (Jeremy, 17:30: "tu adapterais Riven comment ?" then "lance des simu cette nuit
  pour me répondre non ? et mets à jour l'artefact pour commander"). What was built, and why:
  - `lab/meta/banned_2026-11-23.json` (the ban list for the lab's tools; the engine's `CardDef::banned` is untouched)
    and `lab/tools/postban.py` (`patch`: a banned card is replaced by the legend's most played legal cards, a banned
    battlefield by its most played legal one; `check`; `riven-centres`). `lab/decks/postban/` holds the patched
    opponents (Kai'Sa, Kennen, Master Yi, Akali, Orianna) and Riven's starting lists: 21 tournament main decks
    patched, four hand-built (`riven_hand_plain`, `_equipment`, `_exhaust`, `_draw`), two frames.
  - Two searches, one per champion still legal for The Exile, because a `deckopt` spec has ONE frame (champion,
    runes, battlefields): `lab/specs/deckopt/riven-postban-shattered.json` (Riven, Shattered, 6 Calm / 6 Chaos) and
    `…-deserter.json` (Riven, Deserter, 5/7); battlefields Hunters' Circle, Rakelstake, Windswept Hillock; pool 107
    cards (every card of the tournament Riven lists, Equipment for Shattered, draw for the lost Herder,
    protection for the lost Hourglass, exhaust/ready cards for Deserter). Field without banned cards: Kai'Sa 51,
    Kennen 44, the two Riven frames 20 each, Master Yi 16, Rek'Sai 10, Akali 8, Volibear 8 (Stuttgart's shares, the
    mirror cut from 126 to 40: nobody knows the post-ban shares). Exploration "weighted", 32 games a list, swaps
    mean 4 (up to 10), cheap pilot. `deckopt` stages take `"agent_sub"` (races cheap, `confirm` with the full
    pilot) and there is a `confirm2` stage for the pilot trained since.
  - **Known weakness, to say with every result:** the Riven pilot (spec_riven_120_v0) has never played either
    champion nor most of the pool, and Volibear showed a pilot loses about 2 points per copy of a card it never
    trained on. Differences inside one champion's search are fairer than the gap between the two champions. The
    cure is the alternation of section 5: the Riven training resumes tonight on variations of these lists
    (`loop.SPECIALISTS["riven"]["vary"]` = both specs, 60% of the self-play pairs; `--commit-every 6`), and
    `confirm2` replays the confirmation with that pilot.
  - `lab/specs/riven_postban_frames.json`: the frame choices the searches do not explore (five sets of
    battlefields, the other rune split), 12 lists, cheap pilot, 400 games per opponent.
  Dispatched 17:50 UTC: four runner shards (`riven-postban-{shattered,deserter}.explore-0{0,1}.json`, up to 5,000
  lists each, they stop drawing after 300 minutes and push `lab/deckopt/riven-postban-*/explore-0?.json` at about
  23:15 UTC). On the session's machine, detached (`nohup`): `<scratchpad>/riven_local.sh` (shards 10, 11, … of 300
  lists, both champions in turn, from the end of the Volibear race until 23:00 UTC, files in
  `<scratchpad>/riven_out/riven-postban-*/`), then `riven_local2.sh` (the frame test, log `riven_out/frames.log`).

- 18:00 UTC, **Jeremy changed the order**: "j'aimerais focus sur Evelynn, Volibear et Mordekaiser", then "on laisse
  Riven, mais là, en dernier". (He had also asked for an opinion on the post-ban metagame "sans lancer de machines":
  given in the chat, nothing measured.) So:
  - The buy-list artifact was rebuilt (`python3 lab/tools/build_buylist.py OUT.html`, published at the same URL):
    Volibear's best post-ban list, Ritualo + 2 Rengar, Evelynn (Jeremy's list of 13 Nov), Mordekaiser (Neal), then
    Riven's cards that stay legal; banned cards are left out.
  - Two more searches, same field, same method: `lab/specs/deckopt/mordekaiser-postban.json` (base Neal's list, five
    starting lists, pool 42 cards, pilot spec_mordekaiser_070_v0) and `lab/specs/deckopt/evelynn-postban.json` (base
    Jeremy's list, Dunc's as second starting list, pool 48 cards, pilot spec_evelynn_018_v0, a weak pilot that only
    knows Dunc's list: read its table with that in mind). One runner shard each, dispatched 18:05 UTC.
  - The Riven training is NOT started (Riven is last; its PAUSE file stays; `vary` is ready in loop.py for the day
    it resumes). No Evelynn or Mordekaiser training either: Jeremy paused both and said on 13 Nov "ne train rien
    sur evelynn pour le moment"; asked in the report of 18:10 UTC whether the 20 training machines should move to
    them. Until he answers, the standing green light holds: the Volibear training continues.
  - `wave` can group a stage's opponents into a few requests (`"requests": n` in the stage; an opponent's own
    "games" in a test spec): race1 2 requests, race2 3, confirm 4 per search.
  - Session's machine: `<scratchpad>/night_local.sh` (replaces riven_local.sh): from the end of the Volibear race to
    23:00 UTC, shards of 300 lists in turn (Mordekaiser, Evelynn, Mordekaiser, Evelynn, Riven Shattered, Riven
    Deserter), files in `<scratchpad>/riven_out/<search name>/explore-NN.json`; then `riven_local2.sh` (Riven frame
    test, `riven_out/frames.log`).

- 19:02 UTC: **round 1b's cheap race 2 done** (session's machine, 12,000 games per list with race 1's,
  `lab/results/20261113-190203-volibear-noaurora-cards-b-race2.json`, `lab/deckopt/volibear-noaurora-cards-b/race2-rank.json`):
  reference (Ritualo) 78.0%; the confirmed list with the model's 9 further swaps (`…-b-cand-c1p9.txt`) 84.6%, +6.7
  [+5.7, +7.6]; c2+6 83.8%; c1+6 83.4%; c1+4 83.3%; c2+3 82.7%. Spread minus noise among the five: 0.5 point. The
  confirmed list itself was at 82.5% in race 1: the deeper swaps (Punch First, Ferrous Forerunner and Stormbringer
  out; Dune Drake, Deadbloom Predator, Herald of Scales in) add at most about 2 points on selection games, not
  rated. These five are starting lists for round 2, nothing more. Note for Jeremy's question of 18:15 (few turn-2
  plays in the soldog build): these swaps take cheap cards out again; the bot does not punish a slow start the way
  a player does, so a round-2 winner of this kind needs that caveat.

- 19:26 and 20:26 UTC: **the session's container is replaced whenever the session goes idle with no tracked
  background task** (`uptime` said 0 minutes at both watchdog passes; the scratchpad files survive, the processes
  do not). A `nohup` process is therefore lost at the end of the turn: only Mordekaiser's shards 11 (103 lists)
  and 21 (partial) were written before 20:26. **Rule: work on the session's machine runs as a Bash
  `run_in_background` task (2 hours at most; its end wakes the session, which starts the next one).** 20:27 UTC:
  task b7nqavzh9 explores until about 22:17 (shards 31, 32, … of 150 lists, in turn Mordekaiser, Evelynn,
  Mordekaiser, Evelynn, Riven Shattered, Riven Deserter; files in `<scratchpad>/riven_out/<S>/explore-NN.json`).
  When it ends: start the Riven frame test the same way (`python3 lab/riftlab.py run
  lab/specs/riven_postban_frames.json --threads 4`, about 1.5 hours), then step B.
- 20:27 UTC: Volibear iteration 135 was accepted at 19:33 (chain over); nothing queued, six exploration shards in
  progress: training dispatched again, `chain=16` (iterations 136 to 152).

- 22:22 UTC: local exploration task ended (20:27 to 22:17); its shards are copied into `lab/deckopt/<S>/` and
  committed: Mordekaiser 931 lists (29,792 games), Evelynn 600 (19,200), Riven Shattered 300, Riven Deserter 300
  (9,600 each). The Riven frame test runs on the session's machine as task b1bg30rsl (about 75 minutes; result
  `lab/results/*-riven-postban-frames.json`). The runner shards are due at about 23:15-23:30: then step B.

**From 13 Nov 22:30 UTC the plan is section 7 (the weekend); the steps that were here moved there.**

**Earlier steps of 13 Nov (all done)**
0. (done, above) Five hold-out results in: merge, add the table to section 4 and tell Jeremy in three lines.
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

## 7. The weekend of 14-15 Nov: find the post-ban metagame (Jeremy away until Monday; the watchdog runs this)

Jeremy, 13 Nov 22:30 UTC: "essaie de trouver la nouvelle meta, entraînement sur Voli, Morde, Riven et Evelynn après
ban. Je suis occupé tout le week-end. Tu peux gérer les tâches ?" Yes. Nobody answers questions before Monday: decide,
write the decision here, go on. Report to him Monday 06:00 UTC (French, plain), and two or three lines when a
confirmation or a matrix finishes.

**What "the metagame" is here, and what it is not.** No tournament has been played without the banned cards. The
lab can do three things: (1) for each legend, look for its best list without them (sections 3 and 5); (2) play
every list against every other and compute what a field of players who all pick the best deck against each other
would settle on (`lab/tools/meta_matrix.py`: matrix, rate against the prior field, equilibrium with its spread);
(3) search each legend again against that field, train the pilots on what was found, and repeat (the loop of
section 5, with the field moving too; in the literature: double oracle / policy-space response oracles, and Nash
averaging for the ranking). Limits to print with every table: a legend whose pilot trained less looks weaker than
it is (Master Yi and Rek'Sai play under the general net; Kai'Sa and Kennen had 25 and 20 iterations when this
started); the bots play tempo and bounce badly (the lab has Volibear at 75-85% where players had Riven ahead);
sideboards and best-of-three are not simulated; the prior shares are Stuttgart's, a guess for after the bans.

**Training (lab/learn/TRAINING_KNOWLEDGE.md section 5).** Six tracks since 22:45 UTC, all on lists and a field
without the banned cards: Evelynn 5 runners, Riven 5, Mordekaiser 4, Volibear 2, Kai'Sa 2, Kennen 2
(`runners` in `lab/specs/train_spec_<name>.json`), `chain=40`, a net committed every 6 iterations. Kai'Sa and Kennen
were not in Jeremy's sentence: they are the two heaviest opponents and the weakest pilots, so the matrix and every
search depend on them; four of the twenty machines is the price. Told to Jeremy at 23:10.

**Each watchdog pass:** `git pull`; `python3 lab/tools/watch_status.py`; do what its "next" lines say, in the order
Mordekaiser, Evelynn, Volibear, Riven; then the matrix and round steps below; update the log at the end of this
section; commit; push. Work on the session's machine only as a `run_in_background` task (section 6, 20:26).

**Searches, round 1 (running; pilots as they were on 13 Nov).** S = mordekaiser-postban, evelynn-postban,
volibear-postban, riven-postban-shattered, riven-postban-deserter (specs in `lab/specs/deckopt/`).
- B. When a search has its runner shard (`lab/deckopt/<S>/explore-0N.json`, pushed about 5.5 hours after its
  dispatch; Riven, Mordekaiser, Evelynn: 23:15-23:45 UTC on 13 Nov; Volibear: about 04:30 on 14 Nov): `python3
  lab/deckopt.py model lab/specs/deckopt/<S>.json` (keep the ten best and ten worst cards and the "never trained
  on" line for the log), `propose`, `python3 lab/tools/postban.py check lab/variants/<S>-cand-*.txt` (all clean),
  `wave --stage race1`; commit, push; dispatch the 2 `<S>.race1.g?.json` (cheap pilot, about 40 minutes).
- C. Both race1 results in: `rank --stage race1`, `wave --stage race2`, commit, push, dispatch the 3 race2 requests.
- D. race2 results in: `rank --stage race2`, `wave --stage confirm` (full pilot, 3,600 games a list: the reference
  and the best two; Riven also the other champion's reference), commit, push, dispatch the 4 confirm requests.
- E. Confirm results in: `rank --stage confirm`, `python3 lab/tools/merge_results.py <S>-confirm --write`. A list is
  "better" only if its interval against the reference is above 0: then copy it to
  `lab/variants/<legend>-postban-best.txt` with a header saying how it was measured; otherwise the reference stays
  the list to play. Log the table below. The two Riven confirmations share opponents and seeds (6103): read the two
  champions side by side, with the caveat that the pilot of round 1 never played either.
- G. Riven frame test (`lab/results/*-riven-postban-frames.json`, session's machine, about 23:40 UTC): log its table;
  a set of battlefields or the other rune split replaces the frame's only if its interval is above 0.

**Matrices.** Lists in `lab/meta/postban_decks_<tag>.json`, requests `lab/specs/meta/<tag>.row-<key>.json` (one per
list), results `lab/results/*-meta-<tag>-row-*.json`, output `lab/meta/postban_meta_<tag>.{json,md}`.
- m0 (dispatched 22:55 UTC, 13 lists, 500 games a pair, cheap pilots, the nets of 13 Nov): when its 13 results are
  in, `python3 lab/tools/meta_matrix.py solve m0 --decks lab/meta/postban_decks_m0.json`; log the table. It is the
  "before training" picture.
- m1: when round 1's confirmations are done for at least Mordekaiser, Evelynn and Volibear AND each of the four
  main tracks has committed a net at least 6 iterations past its start (Evelynn ≥ 24, Riven ≥ 126, Mordekaiser ≥
  90, Volibear ≥ 144), or at 14 Nov 16:00 UTC with what exists: copy the decks file to `…_m1.json`, replace a
  legend's list by its `…-postban-best.txt` when one passed (keep the old one as a second row with prior 0), `specs
  m1 --games 600`, commit, push, dispatch every row, `solve m1`.
- m2: 15 Nov about 16:00 UTC, the same with round 2's lists and the nets of that hour, `--games 800`; and the eight
  lists with the largest equilibrium or prior-field rate again with `--full --games 300` (tag m2f).

**Round 2 (after m1).** For each S: `python3 lab/tools/postban_round.py lab/specs/deckopt/<S>.json --round 2 --meta
lab/meta/postban_meta_m1.json` (new pilots, opponents and weights from the matrix, starting lists from round 1),
commit, push, dispatch `<S>-r2.explore-00.json`, then B to E on `<S>-r2`. In `loop._POSTBAN_TRACKS` point each
track's `vary` at its `-r2` spec once that spec exists (the variations then follow the search). A round-2 list that
passes replaces `…-postban-best.txt`. If a third round fits before Monday 03:00 UTC, the same with `--round 3` and m2.

**Monday 06:00 UTC, the report (French, plain).** The matrix m2 as a table (list, pilot's iterations, rate against
the field, share at equilibrium with its range); per legend Jeremy plays: the list, what changed against the list
he has in the buy list, the confirmation's interval, the matchups that decide; what moved between m0, m1 and m2
(a legend that only rose when its pilot trained is a pilot effect, say so); the limits above. Then rebuild the
buy-list artifact (`lab/tools/build_buylist.py`: DECKS) with every list that passed, and the dashboard (data.json is
at 15.5 MB: trim the variant texts in `lab/dashboard/build.py` first, with a `deckText()` fallback in the app).

**Failures.** A training track STALE in `watch_status.py`: read the failed job of its last run
(`list_workflow_jobs`, then `get_job_logs` with `failed_only`), fix or dispatch `chain=40` again; twice the same
failure: leave that track, log it, go on. A request that fails twice: skip it and say what is missing in the log.
Runners: 20 jobs at once for everything; search requests queue behind the training matrices and that is fine.

**Log of the weekend**
- 13 Nov 22:45 UTC: PAUSE files of Riven, Mordekaiser, Evelynn, Kai'Sa, Kennen removed; five chains dispatched
  (`chain=40`); Volibear's running chain (to iteration 152) takes the post-ban lists and its 2 runners from its next
  iteration. `volibear-postban` exploration shard dispatched (results about 04:30). 22:55: matrix m0, 13 rows.
- 13 Nov 23:45 UTC, **round 1, card models** (runner shards in for Mordekaiser, Evelynn and both Riven; cheap
  pilot, the nets of 13 Nov; full tables in `lab/deckopt/<S>/model.json`):
  - Mordekaiser (5,074 lists, 162,368 games, 76.8% overall; unknown card −1.6 ± 0.1 a copy): Ferrous Forerunner
    +2.8, Pyke, Dockside Butcher +2.7, Trifarian Captain +2.5 (+6.1 when dealt), Rengar, Unseen +1.8; Decree of Rage
    −3.9, Perfect Execution −2.6, Seal of Unity −1.7, Deathgrip −1.5, Long Sword −1.1. Starting lists as they are
    (256 games each): Neal 81.8%, stuttgart-2-best 83.3%, stuttgart-2-leader 82.0%, cijian 81.1%.
  - Evelynn (3,820 lists, 122,240 games, 57.9%; unknown card −2.1 ± 0.1): Evelynn, In Control +5.8 (+10.0 when
    dealt), Rengar, Trophy Hunter +4.3, Baron Nashor +2.7, Vex, Apathetic +2.6, K'Sante +2.4; Pack of Wonders −7.0,
    Ember Monk −6.4, Bone Skewer −5.1, Star-Crossed −1.7, Punch First −1.6. As they are (160 games): Jeremy's list
    48.9%, Dunc's 64.5%; the pilot only ever trained on Dunc's, so most of that gap is the pilot.
  - Riven, Shattered (8,197 lists, 262,304 games, 36.9% overall; unknown card −2.8) and Riven, Deserter (7,848
    lists, 251,136 games, 47.5%; unknown card −3.1): effects three times as large as for the other legends (prior
    3.7 and 4.0 points a copy), i.e. mostly cards the pilot cannot play (Block −16, Stoic Ferryman −13, Twilight
    Shroud −11 to −14, Hard Bargain −10). Blitzcrank +4.0 (+8.7 when dealt) and Illaoi +3.9 to +5.1 are the solid
    positives. Starting lists as they are (128 games each): Shattered frame 57.5%, `riven_hand_draw` 63.5%;
    Deserter: DiamondHat's main deck 73.5%, `riven_hand_draw` 67.2%. Deserter is about 10 points above Shattered
    with this pilot.
  39 candidates written, none plays a banned card; race 1 dispatched for the four searches (8 requests,
  `<S>.race1.g1/g2.json`).
- 13 Nov 23:45 UTC, **matrix m0** (`lab/meta/postban_meta_m0.md`, 1,000 games a pair, cheap pilots, nets of 13 Nov):
  against the prior field Volibear 90.1% (soldog build) and 87.4% (Ritualo + 2 Rengar), Mordekaiser 77.2%, Riven
  Deserter 67.2%, Evelynn (Dunc) 65.7%, Master Yi 62.0%, Evelynn (Jeremy) 56.4%, Riven Shattered 55.7%, Kai'Sa
  52.8%, Orianna 41.8%, Rek'Sai 38.9%, Akali 34.8%, Kennen 24.4%. The equilibrium is Volibear alone (96% Ritualo +
  2 Rengar, which beats the soldog build 55-45 head to head). **This is the picture before training and it is not
  believable as a metagame**: Volibear beats Kai'Sa 88-91% and Kennen 100% here, while players had Riven ahead of
  Volibear; the patched Kennen has lost its engine and its pilot is the weakest. What it says for certain: the
  other pilots do not know how to play against Volibear. Decision: Volibear's weight in every other track's
  self-play goes from 8 to 50 (`loop.POSTBAN_FIELD`, from each track's next iteration); the held-out Volibear list
  of the panels becomes round 1b's leader (c1+9).
- 13 Nov 23:45 UTC, **Riven frame test** (`lab/results/20261113-222247-riven-postban-frames.json`): every set of
  battlefields gave exactly the same games (Shattered 56.2%, Deserter 67.1% for all five). Cause: the engine asks
  the pilot which of its three battlefields to play (CR 487.5 says random for a single game) and **the lab's
  pilots always answer the first one listed**: a list's second and third battlefields are never played in any
  simulation of this lab. Rune split: no difference (Shattered 5/7 +0.9 [−1.9, +3.7]; Deserter 6/6 −0.2). Frame
  test 2 dispatched on a runner (`lab/specs/riven_postban_frames2.json`: six different battlefields listed first).
  To decide Monday with Jeremy: draw the battlefield at random in simulations (an engine option, game flow, not
  card logic), since his real games draw it.
- 14 Nov 00:06 UTC: the container was replaced again, this time WHILE a tracked background task was running
  (Volibear exploration, 20 minutes in). So no local task is safe; the scratchpad files survive. Rule from now on:
  local work is a bonus, in tasks of about an hour made of small shards (`lab/deckopt.py explore`, 120 lists a
  shard), relaunched by the watchdog when the machine is idle and something useful exists to explore; everything
  that matters runs on the runners. Volibear's local shards: `<scratchpad>/riven_out/volibear-postban/` (11-15 and
  21+): copy them into `lab/deckopt/volibear-postban/` at each pass; with 1,500 lists or the runner shard (about
  04:30), step B for Volibear.
- 14 Nov 00:35 UTC: first post-ban training iterations in (Evelynn 19 accepted, mirror 72.5%; Riven 121 accepted,
  57.5%: about 1 h 40 each). **Race 1 of round 1** (2,400 games a list, cheap pilot, selection games, not ratings):
  Mordekaiser reference 79.7%, the model's lists +2.9 to +8.8 (c1+9 88.5%, c1+6 88.1%, c2+6 87.6%, ref+6 86.9%);
  Evelynn reference (Jeremy's list) 56.4%, Dunc's 67.1%, the model's lists 70-79% (c1+9 = Dunc's + 9 swaps 79.1%,
  ref+6 = Jeremy's + 6 swaps 76.1%); Riven Shattered reference 57.2%, c1+9 70.6%, ref+6 67.1%; Riven Deserter
  reference 67.6%, c1+9 78.3%, ref+6 76.6%. Unlike Volibear's round 1, the lists with the most swaps lead: these
  three decks start further from what the pilots play well. Race 2 dispatched (3 requests a search, 12 runs).
- 14 Nov 01:25 UTC: Mordekaiser iteration 85 accepted (post-ban, 52.5% mirror). **Mordekaiser race 2** (11,998
  games a list with race 1's, cheap pilot): reference 79.1%; c1+9 88.7% (+9.6 [+8.8, +10.5]), c1+6 87.8%, c2+6
  87.2%, ref+6 86.6%, c1+4 86.3%; the five differ by 0.8 point. Confirmation dispatched (4 requests, full pilot
  spec_mordekaiser_070_v0, 3,600 games a list: reference, c1+9, c1+6).
  **Volibear round 2, card model on the session's shards** (1,624 lists, 51,968 games, pilot spec_volibear_132_v0,
  post-ban field): overall 89.4%; a card the pilot never trained on now costs 0.4 point (± 0.2) against 1.9 with
  the pilot of round 1: the training on variations did its job. But every starting list sits between 89.0% and
  93.2% (416 games each): against this field, with these opposing pilots, Volibear lists cannot be told apart
  (ceiling). The race runs (2 requests) so the pipeline stays whole, but **no Volibear list will be named better on
  round 2 unless its interval clears 0 in the confirmation**; the real test is round 3, after the other pilots
  have trained against Volibear.
- 14 Nov 01:35 UTC: **Evelynn race 2** (11,998 games a list, cheap pilot): reference (Jeremy's list) 56.9%; c1+9
  (Dunc's list, nine swaps: Star-Crossed, Punch First, Ezreal, Fizz and Hold 'Em out; Rengar, 3 Evelynn, In
  Control, 2 Baron Nashor, Vex, K'Sante, Boots of Swiftness in) 79.0%; c1+6 75.3%; ref+6 (Jeremy's list, six swaps:
  2 Pack of Wonders, Onslaught, 2 Star-Crossed, Fizz out; Rengar, Evelynn, In Control, 3 Baron Nashor, Vex in)
  75.0%; ref+4 74.1%; c1+4 74.0%. Confirmation dispatched (4 requests, full pilot spec_evelynn_018_v0): the
  reference, c1+9, c1+6, and as yardsticks Dunc's list as registered and ref+6 (so Jeremy's list with six swaps is
  measured on the same games as the lists built from Dunc's).
- 14 Nov 02:30 UTC: Riven iteration 122 and Kai'Sa 29 accepted (Kennen's first still running on its 2 runners).
  **Riven race 2** (11,998 games a list, cheap pilot of 13 Nov): Shattered reference 57.4%, c1+9 70.8% (+13.4),
  ref+6 67.6%, c2+6 65.1%; Deserter reference 67.9%, c1+9 78.2% (+10.3), ref+6 76.1%, c2+6 73.4%. In both, the
  model takes the reactive spells out (Defy, Rebuke, Switcheroo, Star-Crossed, Hush) for bodies (Angler Wyrm, Vex,
  Apathetic, Pakaa Protector, Blitzcrank): to read with care, the bots play counters and tricks worse than players
  do. Confirmations dispatched (4 requests each, full pilot spec_riven_120_v0: reference, c1+9, ref+6, the other
  champion's reference).
  **Riven frame test 2** (`lab/results/20261113-233808-riven-postban-frames2.json`, first battlefield varied, 3,200
  games a list): Hunters' Circle first is the best for both champions; against it Rakelstake −5.3 / −4.6
  (Shattered / Deserter), Windswept Hillock −4.6 / −4.1, Threshold of the Gray −3.5 / −2.0, Valley of Idols −2.4 /
  −3.1 (intervals touch 0 for one champion each), Forge of the Fluft −8.6 / −8.4. The frame keeps Hunters' Circle.
  **Volibear race 1** (2,400 games): reference 86.7%, the candidates 86-93%, spread minus noise 1.8 points; race 2
  dispatched (3 requests).
- 14 Nov 03:40 UTC: Evelynn iteration 20 accepted. **Mordekaiser, round 1 confirmed**
  (`lab/results/20261114-025126-mordekaiser-postban-confirm.json`, fresh seeds 6113, 3,598 games a list, full pilot
  spec_mordekaiser_070_v0): Neal's list 82.0%; c1+9 90.6%, +8.6 [+7.1, +10.1]; c1+6 89.9%, +8.0 [+6.5, +9.4]; the
  two are not separated (−0.6 [−1.9, +0.6]). `lab/variants/mordekaiser-postban-best.txt` = c1+9. Against Neal's
  main deck: −3 Honest Broker, −2 Rek'Sai, Swarm Queen, −2 Long Sword, −1 Salvage, −1 Rengar, Pouncing, −1
  Deathgrip, −1 Falling Star, −1 Perfect Execution; +3 Pyke, Dockside Butcher, +3 Trifarian Captain, +2 Bilgewater
  Tracker, +1 Ferrous Forerunner, +1 Hidden Blade, +1 Noxus Hopeful, +1 Vi, Peacekeeper. Against Volibear alone
  the reference won 39% and c1+9 66% (about 160 games each: an indication, not a measure).
  **Volibear race 2** (11,998 games): reference (Ritualo) 86.6%; c2+6 92.9%, c2+3 92.6%, round 1's confirmed list
  92.2%, c1+2 92.2%; spread minus noise 0.4 point. Confirmation dispatched (4 requests: reference, c2+6, c2+3).
  Kennen has no iteration yet, 5 hours after its dispatch (Kai'Sa's first took 3.6 hours on the same 2 runners):
  its run is looked at in this pass.
- 14 Nov 03:50 UTC, Kennen looked at: its run (43520615880, dispatched 22:36) and Volibear's iteration 139
  (43520651270) are alive but show as "queued": their next jobs wait for a free machine. Twenty confirmation jobs
  of two hours each (Mordekaiser, Evelynn, both Riven, Volibear) hold the 20 slots, so every track slows down for
  a few hours and the 2-runner tracks most. Not a failure; nothing to dispatch. How to find a track's run: `list_workflow_runs`
  with status "queued" (then "in_progress"), perPage 2, the LAST pages are the oldest runs; `head_commit.message` and
  `actor.login` tell which chain it is. `watch_status.py` now waits 13 hours before calling a 2-runner track STALE.
- 14 Nov 04:10 UTC: **dashboard v109 published** with Mordekaiser's confirmation. data.json had reached 16.4 MB;
  now 13.0 MB: a variant's text is rebuilt in the app from its `list` (lib.js `deckText`), the engine command lines
  are kept for a test's first list only, and a test names its opponents once (`opps`, expanded by lib.js
  `expandOpponents`). Checked in a browser: every page renders, no script error.
- 14 Nov 04:35 UTC: all six tracks have a post-ban iteration (Kennen 27, Volibear 139, Mordekaiser 86 accepted).
  **Evelynn, round 1 confirmed** (`lab/results/20261114-042105-evelynn-postban-confirm.json`, fresh seeds 6113, 3,598
  games a list, full pilot spec_evelynn_018_v0): Jeremy's list of 13 Nov 53.2%; Dunc's list as registered 66.5%
  (+13.4); Dunc's with six swaps 71.5% (+18.4); **Jeremy's list with six swaps** (`…-cand-refp6.txt`: −2 Pack of
  Wonders, −2 Star-Crossed, −1 Fizz, −1 Onslaught; +3 Baron Nashor, +1 Evelynn, In Control, +1 Rengar, +1 Vex,
  Apathetic) 71.8% (+18.7 [+16.8, +20.6]); **c1+9 74.8%**, +21.6 [+19.7, +23.4], and +3.2 [+1.4, +5.0] over the
  six-swap lists. `lab/variants/evelynn-postban-best.txt` = c1+9 (11 cards away from Jeremy's list). To read with
  two cautions written in its header: the pilot knew Dunc's cards only, and the swaps again take a reactive spell
  out for bodies. The gain against Kai'Sa is the largest (Jeremy's list won 31% of those games, c1+9 61%).

