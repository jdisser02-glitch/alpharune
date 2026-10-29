# Training plan — making the Riftbound bot strong on a CPU budget (2026-10-29)

Scope: what the literature says works for hidden-information card games, evaluated
against this repo's constraints (4-core machine, GitHub Actions runners with 4 cores
and 6 h per run, no GPU, numpy-only trainer, 73 human games), and a three-phase
roadmap with the concrete files to touch. No code changes come with this document.

## 1. Where we stand

- **Search.** `IsMctsAgent` (`src/agents/mcts_agent.cpp`) wraps OpenSpiel's
  `ISMCTSBot`: one determinization per simulation via
  `RiftboundState::ResampleFromInfostate`, a leaf evaluated by `RiftboundHeuristicEvaluator`
  (score, `board`, or `net:<file>`), UCT selection with `c = 2.0`. OpenSpiel's
  `ISMCTSBot` selects children with plain UCT and never calls `Evaluator::Prior`,
  so the hand-written prior in the evaluator only affects the perfect-information
  `MctsAgent`. The honest bot has **no policy prior at all**.
- **Value net.** One ReLU layer (64 hidden) over the 6,688-dim state vector, 428k
  parameters, trained in numpy on 12 reservoir-sampled decisions per game with the
  final result as label. `lab/learn/ladder.json`: 5 iterations at `sims=30`,
  200 self-play games each, a 3-iteration window (≈7k records), gating by 60 games
  against the previous net (95% CI ≈ ±12 points). Validation loss drifts *above*
  the constant baseline by iteration 5 (0.714 vs 0.693): the net is data-starved
  and overfits, and the gate cannot tell 50% from 60%. Win rate vs random play
  is 62–75% at 30 simulations.
- **Throughput.** Random play runs ≈240 games/s per core (`stage1-baseline.md`),
  i.e. ≈13 µs per engine step. ISMCTS at 30 sims runs ≈0.15 games/s on 4 cores
  (ladder: 240 games ≈ 28 min), i.e. ≈3 ms per simulation — about 200× an engine
  step. The cost is in cloning `GameState` (an `unordered_map` of objects), one
  resample per simulation, a dense 6,688-wide feature extraction plus a dense
  430k-MAC net evaluation, and linear scans to encode/decode intents. One 6-hour
  runner therefore yields ≈3,000 games at 30 sims or ≈300 at 300 sims.
- **Humans.** 73 RiftLite games, 14,645 log lines (≈1,860 plays, 1,290 moves,
  755 focus passes, 835 end-turns, 201 mulligans). The 8 Riven games replayed
  through `riftbound_guided` give 1,446 bot decisions, 419 with a known human
  move, 203 compared: **58 agree (28.6%)**, 30/132 (22.7%) at clean decision
  boundaries. The bot's visit shares at 300 sims are nearly flat (top-3 at
  0.35/0.33/0.32 is typical), which says the value signal barely separates
  siblings.
- **Action space.** `action_vocab.h` gives 14,571 dense slots (Play/Hide/Choice
  ×1,200 card ids, ActivateAbility ×4,800, 128 combat buckets, …) — a workable
  policy-head size.

## 2. State of the art, briefly

**Determinized search for card games.** PIMC/ISMCTS is what shipped card-game bots
actually use. Cowling, Powley & Whitehouse's
[ISMCTS](https://eprints.whiterose.ac.uk/75048/1/CowlingPowleyWhitehouse2012.pdf)
(2012) and the same group's
[ensemble determinization for Magic: The Gathering](https://eprints.whiterose.ac.uk/75050/1/EnsDetMagic.pdf)
(2012) found that move pruning and a binary decomposition of complex moves mattered
more than raw simulation count. Long et al. explain
[when PIMC works](https://ojs.aaai.org/index.php/AAAI/article/view/7562) (2010):
well when hidden information is revealed quickly ("disambiguation") and sibling
outcomes are correlated — both true of a TCG where hands get played out. The
Hearthstone AI Competition (2018–2020) was won by search agents (ISMCTS with sparse
sampling, dynamic lookahead, rolling-horizon evolution) paired with a learned or
hand-made state evaluation
([Dockhorn & Mostaghim](https://arxiv.org/pdf/1906.04238),
[2019 results](https://hearthstoneai.github.io/files/slides/2019-Results-Hearthstone-AI-Competition.pdf)).
Świechowski et al. showed that a **small network scoring states** raised an MCTS
Hearthstone agent's win rate while cutting compute
([arXiv 1808.04794](https://arxiv.org/pdf/1808.04794)); Zhang & Buro got gains from
**learned high-level rollout policies and chance-node bucketing**
([CIG 2017](https://skatgame.net/mburo/ps/cig17-hsai.pdf)). In Skat, Solinas, Rebstock
& Buro improved PIMC substantially by **inferring where hidden cards are** with a
supervised net ([AAAI 2019](https://arxiv.org/pdf/1903.09604)) — but from 20 M human
games. No public Legends of Runeterra bot or paper exists beyond the
[utilForever/Runeterra](https://github.com/utilForever/Runeterra) simulator.

**AlphaZero-style self-play, adapted.** Expert Iteration
([Anthony et al. 2017](https://arxiv.org/abs/1705.08439)) and AlphaZero train a
policy on search visit counts and a value on outcomes. Two results make this
affordable on CPU: KataGo's accounting of what actually buys efficiency —
auxiliary targets (score, ownership), playout-cap randomisation, policy-target
pruning — claimed a 50× compute reduction
([Wu 2019](https://arxiv.org/abs/1902.10565v5)); and Gumbel AlphaZero, which
yields a sound policy improvement with **2–16 simulations** per move
([Danihelka et al. 2022](https://iclr.cc/virtual/2022/spotlight/6419),
[MiniZero](https://arxiv.org/pdf/2310.11305)). For card games specifically, DouZero
(DouDizhu) reached top-of-leaderboard strength with plain Monte-Carlo returns, no
tree search, 48 cores + 4 GPUs for days ([Zha et al. 2021](https://arxiv.org/abs/2106.06135v1));
the open-source Yu-Gi-Oh agent trains PPO with an LSTM in self-play on one GPU
([ygo-agent](https://github.com/sbl1996/ygo-agent)). Both are 1–2 orders of
magnitude above our budget but show that search-free self-play works for TCGs when
the simulator is fast.

**Game-theoretic methods.** ReBeL ([Brown et al. 2020](https://arxiv.org/abs/2007.13544))
searches over public belief states — it needs a compact public state and a
belief-conditioned value net, and was demonstrated on poker and Liar's Dice;
Riftbound's public state is huge and beliefs over 40-card decks are not
enumerable. DeepNash's R-NaD ([Perolat et al. 2022](https://arxiv.org/pdf/2206.15378v1.pdf))
is search-free regularised self-play that converged in Stratego but at DeepMind
scale. Student of Games ([Schmid et al. 2023](https://arxiv.org/abs/2112.03178))
unifies both but its authors say it would struggle with far more hidden information
than poker ([Live Science](https://www.livescience.com/technology/artificial-intelligence/student-of-games-is-the-1st-ai-master-different-games-like-chess-and-poker)).
Deep CFR ([Brown et al. 2019](https://arxiv.org/abs/1811.00164v2)) needs external
sampling traversals over the full game tree — out of reach here. The one cheap,
transferable idea is **magnetic mirror descent / entropy-regularised policy targets**
([Sokota et al. 2023](https://arxiv.org/abs/2206.05825)), which reduces cycling in
self-play and is a few lines in a numpy trainer.

**Evolutionary methods.** OpenAI-ES matched A3C on Atari with 3–10× the data
([Salimans et al. 2017](https://arxiv.org/abs/1703.03864)); its advantage is
parallelism across thousands of workers, which we do not have. Evolving network
weights (NEAT, CMA-ES on 400k parameters) with a fitness that costs tens of noisy
games per evaluation is the worst possible fit for 4 cores. Where GAs shine is
**deck building** — García-Sánchez et al. in Hearthstone
([CIG 2016](https://iris.polito.it/handle/11583/2666239), KBS 2018) — and tuning a
dozen heuristic weights; `lab/optimize.py` already does a successive-halving race
over swaps, which is the right shape.

**Imitation.** AlphaGo's SL policy came from 30 M positions (57% top-1); DAgger
([Ross et al. 2011](https://proceedings.mlr.press/v15/ross11a)) fixes the
distribution shift of behaviour cloning by querying the expert on the learner's own
states — we cannot query humans, but we can query a *stronger search* (ExIt is
DAgger with search as the expert). ≈2,000 human decisions with hidden hands is
enough to shape a prior and to serve as a **metric**, not enough to train a policy.

## 3. Comparison against our constraints

| Method | Data it needs | Compute on our budget | Expected strength gain | Effort in this repo | Main risks |
|---|---|---|---|---|---|
| Fix the value loop (all decisions as samples, replay buffer, smaller input, sparse inference, honest gate) | Self-play only | Low: fits in one runner per iteration | Medium; prerequisite for everything else | Low (`train_value.py`, `loop.py`, `game_runner.cpp`, `value_net.cpp`) | None beyond engineering |
| Faster search (own PUCT-ISMCTS, root parallelism, cheaper clones, shared determinizations) | None | Pays for itself: 3–10× more sims per decision | Medium–high (ISMCTS strength scales with sims until the evaluator caps it) | Medium (new `src/agents/ismcts_search.{h,cpp}`) | Reimplementing OpenSpiel's searcher; keep its tests as a reference |
| AlphaZero/ExIt policy + value, visit-count targets, Gumbel at low sims, ISMCTS determinization | Self-play; 10⁴–10⁵ games over weeks | Medium: ≈3k games/runner-day at 30 sims; Gumbel at 8–16 sims gives 2–4× more games | High — this is the known recipe; the policy prior is what makes few sims useful | Medium–high (policy head in numpy, visit recording, C++ `PolicyNet`, PUCT) | Policy over 14.5k slots with aliased targets; cycling without an opponent pool |
| Imitation from 73 human games (BC prior, mixed loss) | The 73 games (≈2k decisions) | Negligible | Low–medium as a prior; valuable as a metric | Low (dataset builder from `lab/human_replays`) | Hidden hands → noisy labels; desyncs; overfitting a 14.5k-way softmax to 2k examples |
| Opponent pool / league gating | Self-play | Low (eval games only) | Medium: prevents regressions and rock-paper-scissors cycling | Low (`loop.py`, `ladder.json` schema) | More eval games needed per iteration |
| Hidden-card inference (Skat-style) to weight determinizations | Self-play positions (opponent hand is known in self-play) | Low | Medium late-game (counters, held reactions) | Medium (`ResampleFromInfostate` + small net) | Needs trusted engine first |
| ReBeL / Student of Games (public-belief search) | Self-play, belief value net | Infeasible: belief space over 40-card hidden decks | High in theory | Very high | Not realistic here |
| R-NaD / Deep CFR as the main algorithm | Self-play, large | Infeasible at convergence scale; MMD-style regulariser is cheap | Unknown at our scale | High (new learner) | Compute |
| ES / NEAT on network weights | Self-play | Infeasible: each fitness eval = tens of games ≈ minutes; 3–10× less sample-efficient than gradients | Low | Low | Wasted runner time |
| GA / CMA-ES on ≤20 heuristic weights (board eval, `uct_c`, prior weights) | Self-play | Low (a runner-day) | Small but cheap | Low (`optimize.py` spec type) | Overfits the opponent pool |
| GA for deck building | Self-play | Already affordable | Deck strength, not bot strength | Exists (`optimize.py`) | Bot weakness biases deck fitness |

## 4. Roadmap

### Phase 1 (≈2–3 weeks): make the loop honest and fast

1. **Record every decision, not 12 per game.** `src/engine/game_runner.cpp`: drop the
   reservoir; write all non-trivial decisions with `turn`, `perspective`, final
   result **and** final score margin `(own − opp)/8` as a second target. Keep the
   sparse `RBVD` format; bump its version. 300 decisions × 3k games ≈ 10⁶ rows per
   runner-day instead of 2.4k.
2. **Shrink the input, keep the data.** In `lab/learn/train_value.py`: factorise the
   five 1,200-wide multi-hot zones through a shared 16–32-dim card embedding
   (`E[1200×d]`, zone counts × E), so the first layer sees ≈470 + 5·d inputs
   instead of 6,688. Train from a rolling replay buffer of all data (weighted
   toward recent iterations) rather than a 3-iteration window, with a held-out set
   of whole games. Add the score-margin head as an auxiliary loss (KataGo's
   cheapest trick).
3. **Sparse inference in C++.** `src/ml/value_net.cpp`: fold `mean·inv_std` into
   the first-layer bias and accumulate only non-zero features; with the embedding
   the dense pass becomes a few thousand MACs. Bump the `RBVN` format version.
4. **Our own searcher.** Add `src/agents/ismcts_search.{h,cpp}`: PUCT selection
   with a prior hook, single-observer ISMCTS (one determinization per simulation,
   as now), root parallelism across the 4 cores (independent trees, summed visits
   — [Chaslot et al. 2008](https://cris.maastrichtuniversity.nl/en/publications/parallel-monte-carlo-tree-search/)),
   a cheaper per-simulation path (reuse one resampled world for *k* simulations;
   profile `GameState` copy and `encodeAction` scans), and visit-count export.
   Keep OpenSpiel's bot behind `search=openspiel` for A/B tests. Expose
   `sims`, `c_puct`, `det_reuse`, `threads` through `SearchOptions`.
5. **Honest evaluation.** `lab/learn/loop.py`: gate with ≥200 games or an SPRT,
   seats and decks swapped, against the *current best* and against two fixed
   yardsticks (`ismcts:sims=100,eval=board` and random); maintain Elo in
   `ladder.json` with [BayesElo/Ordo-style](https://www.remi-coulom.fr/Bayesian-Elo/)
   fitting over all pairings. Add `lab/tools/human_review.py` agreement as a
   secondary column (top-1 and top-3 on clean boundaries).

*Measure:* simulations/s per core, games per runner-hour, held-out value loss vs
baseline (must stay below 0.69 and improve), Elo vs the fixed yardsticks, win rate
vs `net_004` at equal time.

### Phase 2 (≈4–6 weeks): policy head, visit targets, human prior

1. **Visit-distribution targets.** `game_runner.cpp` already gets
   `AgentInterface::lastSearch()` for the review log; write it (legal-slot ids
   from `encodeAction` + normalised visits, after Gumbel/temperature) into a new
   `RBPD` policy-data file alongside the value rows. Randomise the playout cap per
   move (e.g. 25% of moves at full sims, the rest at ¼) and only write policy rows
   for full-sim moves (KataGo's playout-cap randomisation).
2. **Two-head network in numpy.** Extend `train_value.py` into `train_net.py`: a
   shared trunk, a value head, a score-margin head, and a policy head over the
   14,571 slots implemented as a legal-masked softmax (store per-row legal slot
   lists; compute logits only on legal slots). Cross-entropy to the visit
   distribution, with a small entropy/KL-to-previous-policy regulariser
   (the MMD idea) to damp cycling.
3. **C++ `PolicyNet` prior.** `src/ml/policy_net.{h,cpp}` loading the same file;
   `ismcts_search` uses it as the PUCT prior and Gumbel-style sequential halving
   at the root so 8–16 simulations already improve on the raw policy. Add a
   `policy-only` agent (`agent=net:FILE`) for fast self-play data generation and
   as a cheap opponent.
4. **Imitation dataset and prior.** New `lab/tools/build_human_dataset.py`: walk
   `lab/human_replays/*/game_1.json` (every frame already carries the option list,
   the human's option index and the state), extract `(masked features, legal slots,
   human slot)` for clean-boundary decisions, mark in-flight ones. Use it (a) to
   pre-train the policy head with a mixed loss (human CE at weight 0.1–0.3 on
   top of self-play CE) and (b) as the human-agreement validation set. Do not
   expect more than a prior from ≈2k rows; DAgger here means ExIt — the stronger
   search relabels the learner's own states.
5. **Trainer in CI.** A `lab/learn/loop.py --ci` mode and a workflow
   (`.github/workflows/learn.yml`) that runs one iteration per dispatch
   (self-play → train → gate → commit `ladder.json` + net) so the 6-hour runners
   do the self-play while the local machine evaluates.

*Measure:* policy top-1 accuracy against its own search targets (held-out), Elo
gain per 10k self-play games, human agreement (top-1/top-3 on clean boundaries —
target >40% top-1 from 23%), win rate at a fixed 1 s/move against the Phase 1
best, and the entropy of root visit shares (they should stop being flat).

### Phase 3 (ongoing): league, inference, robustness

1. **Opponent pool.** Keep the last *k* accepted nets plus the board heuristic
   and, when available, a "main exploiter" trained only against the current best
   (AlphaStar's league in miniature). Self-play opponents are sampled from the
   pool; acceptance requires non-regression against the whole pool. Rotate deck
   pairs over the full `lab/decks/meta` pool with per-deck Elo so one archetype
   cannot dominate the data.
2. **Hidden-card inference.** In self-play the opponent's hand is known: train a
   small head that predicts the opponent's hand composition from the public
   features and use it to weight `ResampleFromInfostate` (Skat-style), first for
   cards already revealed by play patterns (held reactions, hidden cards at
   battlefields).
3. **Engine trust.** Re-run `human_review.py --all` after each engine fix (§6);
   the fraction of human games replayable to the end without desync is the
   leading indicator that self-play is learning the real game.
4. **GA where it belongs.** Keep `optimize.py` for lists; add a CMA-ES spec type
   for ≤20 search/heuristic hyper-parameters (`c_puct`, prior temperature, board
   weights) raced with shared seeds. Do not evolve network weights.

## 5. What genetic algorithms are and are not good for here

Good: deck construction and sideboard plans (discrete, combinatorial, no gradient,
fitness = win rate from a fixed bot — already `optimize.py`), tuning a handful of
scalar knobs (CMA-ES or successive halving), and generating diverse opponents for a
league. Not good: learning the policy or value function. A fitness evaluation costs
tens of noisy games (minutes on 4 cores), ES needs 3–10× the samples of gradient
methods even on a cluster of thousands of workers
([Salimans et al.](https://arxiv.org/abs/1703.03864)), and our gradient trainer
gets ≈10⁶ labelled decisions per runner-day for free from the same games. Note
also that deck-building fitness inherits the bot's blind spots: a weak bot
undervalues reactive cards, so deck GAs should re-run after each bot upgrade.

## 6. Engine issues that limit training (from `lab/human_replays/*/summary.json`)

174 desyncs over the 8 replayed Riven games, plus 515 "silent passes" (the replayer
had to pass priority for the engine where the log shows no decision). Grouped
causes, most frequent first:

1. **Showdown / focus flow (≈64):** "no Pass focus option (no showdown in the
   engine)" ×34, and "engine asks P1 but the log's next action is by P2" for
   `passed_focus`/`showdown` ×30. The engine opens showdowns and hands focus in a
   different order or at different moments than the client; this is the single
   biggest source and it also hides every combat decision from comparison.
2. **Priority / turn-order mismatches on plays and moves (≈21):** the engine asks
   the wrong player before a `moved`/`played` line — reaction windows and
   priority passing differ from the client.
3. **Legal play set too narrow (≈30):** "card in hand but not playable now
   (cost/timing)" ×24 (cost reductions, Accelerate/Ambush timing, rune
   accounting), "card in trash but not replayable" ×2, play location refused ×2,
   play from a battlefield not offered ×1.
4. **Movement and board state (≈42):** "no such unit on board" ×21 (a unit the
   client has that the engine killed or never resolved), "unit is exhausted" ×8,
   "unit on board but no move option" ×4, "can move but not to <battlefield>" ×9
   (destination rules, token battlefields).
5. **Hidden cards (≈8):** no hide option at Emperor's Dais, Star Spring, Ornn's
   Forge, Targon's Peak, Veiled Temple.
6. **Equip options (≈8):** Blade of the Exile onto Riven/Blitzcrank/Scuttle
   Crab/Stellacorn Herder/Vex, Cloth Armor onto Frigid Jewel/Orianna — equip cost
   alternatives and targets missing.

Two data-side limits also cap what imitation can give: RiftLite logs contain no
damage-assignment lines (combat decisions: 1 compared), and the human's hand is
reconstructed only when a card is played ("hand fixes"), so a behaviour-cloning
label is conditioned on a partially fabricated hand.
