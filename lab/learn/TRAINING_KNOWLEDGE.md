# Training: what the audit found and what the literature says (10 Nov 2026)

Two inputs, both of 10 Nov: a read-only audit of the specialist ladders (volibear, mordekaiser, riven, through
04:43 UTC) and a literature brief on self-play training at small compute for an imperfect-information card game.
Statements marked [I] are inferences or our own calculations, not something a source states.

## 1. Audit of the current loop

**The adoption rule mostly passes noise.** A panel is 480 games (Volibear, Mordekaiser) or 768 (Riven) per side;
`diff_ci95` is ±4.4–5.0 points, so the +3 threshold sits at 1.2–1.35 standard errors. A candidate equal to the
current net is adopted 3–7% of the time; a true +3 is detected half the time. 23 of 36 adoptions had a lower bound
≤ 0.

**Claimed gains did not hold when re-measured** (the adopted net's later `base_rate` minus its predecessor's, same
jury):

| Track | Adoptions | Claimed | Realised |
|---|---|---|---|
| Volibear | 35, 37, 48 | +14.6 | +1.1 |
| Mordekaiser | 37 to 70 | +30.6 | +6.9 |
| Riven | all 4 | +22.7 | 0.0 |

Mirror Elo is flat since the early iterations (Volibear 226→240 since iteration 16, Mordekaiser 816→838, Riven
802→825). Over the current rejection streaks the mean panel diff is negative (−0.9, −0.7, −1.0 points): candidates
are slightly worse than the net they start from.

**The jury is not a credible yardstick.**
- Jury decks are piloted by frozen nets on lists they never trained on. Since the 9 Nov jury change, DiamondHat's
  Riven and DZiden's Kai'Sa lose 96–98% (reproduced locally: Volibear 12/12 against DiamondHat, 7/12 against the
  near-identical Sonecon list). They are 40% of the panel and carry no signal. Cause not established: most likely
  pilots that do not know the lists (The Good Stuff, the Mind/Fury Kai'Sa build); an engine bug is not excluded.
- Self-play opponents are the jury itself, so a specialist learns to exploit those pilots. Mordekaiser scores
  98/79/91/85% in simulation where real players have 28/41/35/29% (`lab/tools/favorites.py`: 0 of 4 right
  favourites).

**The improvement step is weak.** Self-play search is 100 simulations over 8 worlds (Riven 200), the move with the
most visits is played, with no root noise and no temperature. The net already predicts the search's move 82–91% of
the time (KL about 0.05 nats), so the policy target barely differs from the net. Each candidate restarts from the
current net (fresh Adam, 1–2 epochs, 10% pull toward it); the buffer is 3 iterations (80–150k rows against up to
7.9M parameters); the value head overfits (Mordekaiser train/validation loss 0.32/0.43).

**The population search is a random walk.** The screen is 120 games per variant; correlation between screen lead and
panel diff is −0.10 / 0.06 / 0.02; genes changed in 64–77% of steps; the parent is the last screen winner even when
it was rejected.

**Compute goes to evaluation.** Per iteration: self-play 32–39% of the payload, screen 15–18%, gate and panel
39–48%, checkout and apt 19–24%. Riven since its last adoption: 14,880 self-play games against 68,076 evaluation
games.

**Structural.** The warm start zeroes the weights of features absent from the buffer (`train_policy.py:227,439`); a
timed-out batch counts as all losses (`loop.py:305-307`). No leak between training and evaluation seeds was found.

Not verified: the pilots actually written in `plan.json` (read from code), why exactly the two jury decks collapse,
Volibear's realism (no real-data row).

## 2. What the literature says

**Gating.** AlphaGo Zero gated at 55% over 400 games; AlphaZero removed the gate and trains one net continually
(arXiv 1712.01815). ELF OpenGo went ungated and gained throughput (1902.04522). KataGo keeps only a regression guard
(win at least 100 of 200) and says the loop works without it (1902.10565). Minigo used no evaluation; SAI
auto-promoted and damped oscillation by promoting the best of several snapshots (1905.10863). [I] Detecting +3
points at 80% power needs about 1,700 games, +2 about 3,900; a sequential test (SPRT, 50% vs 53%) averages about
1,470. Fishtest (Stockfish) runs SPRT on paired games.

**Data efficiency (KataGo ablations, in Go).** Playout-cap randomisation (full search on about 25% of moves, cheap
search elsewhere, only full searches become policy targets): 1.37x. Forced playouts with policy-target pruning:
1.25x. Auxiliary targets (score, ownership): 1.65x. Value target: mixing the game result with search values lowers
variance. Window: grow it slowly, about 4 training passes per new sample. Averaging weights (EMA of snapshots, SWA)
improves generalisation at almost no cost. Minigo's stall was value overfitting, found with held-out positions.

**Few simulations.** With few simulations AlphaZero can fail to improve its policy; Gumbel AlphaZero is built for
that regime (ICLR 2022; reference code: deepmind/mctx). [I] Its guarantee is for perfect-information search: apply
per world and A/B it.

**Imperfect information.** Determinised search (PIMC) has two errors that more worlds do not fix: strategy fusion
and non-locality (Long et al., AAAI 2010); it does well when hidden information is revealed during play. Practical
fixes at our budget: average policies rather than values across worlds (AlphaZe**), weight sampled worlds by how
likely the opponent's observed plays are under a policy model (Rebstock et al., 1905.10911, Skat), train with
hidden features then drop them out (Suphx, 2003.13590). ReBeL, Student of Games and DeepNash are sound but [I] far
beyond this budget.

**Overfitting to opponents.** Pure self-play cycles and forgets (AlphaStar); OpenAI Five plays 80% against the
latest policy and 20% against older ones; independent RL overfits its training opponents (PSRO, 1711.00832).
Reusing one holdout adaptively overfits it (Dwork et al., 1506.02629): [I] a fixed jury used for every adoption is
such a holdout. Population-based training selects with a significance test; [I] on 120 noisy games it copies luck.

**Human data.** Useful as an imitation start and a regulariser at scale (AlphaStar, piKL with 73k–10M games).
[I] About 70 games cannot train a prior: keep them as a light auxiliary loss and a held-out agreement check.

**Measuring progress.** Elo from successive gated matches is inflated (SAI); rate every net jointly against several
frozen checkpoints (BayesElo, Ordo); OpenSpiel's AlphaZero tracks strength against a fixed plain MCTS.

## 3. Changes these point to, in order

1. **Fix the yardstick first.** Remove jury decks whose pilot cannot play them; split the opponents into those used
   in self-play and held-out ones used only to measure; keep 6–10 frozen checkpoints and the real-favourite score
   as the progress measure.
2. **Train continuously; adopt unless shown worse.** Replace "+3 on the panel" by a regression guard. Rate a
   separate "release" net (the one Play and the list searches use) with a sequential paired test.
3. **Drop the 120-game screen and the gene mutations.** Fixed settings, variants chosen by held-out loss; the
   games go to self-play.
4. **Accumulate.** A window over several iterations, no restart from the current net on a rejection, weight
   averaging, held-out positions with early stopping.
5. **Strengthen the improvement step.** Exploration in self-play (sampled moves or root noise), playout-cap
   randomisation with 400–800 simulations on the full-search moves, then Gumbel root search (A/B).
6. **Auxiliary value targets** (final score margin, game length) and the search-value mix already in place.
7. **Pilots that know their lists.** One deck-conditioned net, or pilots refreshed on the jury lists, so that a
   list search optimises against opponents that play properly.
8. **Budget-sized hidden-information fixes** (policy averaging across worlds, belief-weighted worlds), after the
   above.

[I] The Go multipliers will not transfer as such to a card-game MLP; each change needs its own A/B on the
yardstick of step 1.

## 4. What was changed, and what it gave

**10 Nov 07:54 UTC, from iteration 91 (Riven alone on the 20 runners): steps 1 to 4 above.** Self-play opponents
separated from the held-out panel; four frozen anchors met every iteration; one candidate per iteration, adopted
unless clearly worse; 1,600 self-play games (was 480) and a 6-iteration window; no screen, no mutations.

Baseline, iterations 91 to 102 (about 35 minutes each; 10 adopted, 2 refused):

| Measure | Start | End |
|---|---|---|
| Mirror against the four frozen anchors | 48.8% [43.3, 54.2] (iterations 91-92, 320 games) | 62.7% [58.9, 66.4] (99-102, 640 games) |
| Same, by halves | 56.8% [53.6, 59.9] (91-96) | 62.9% [59.9, 66.0] (97-102) |
| Held-out panel, net in place | 50.3% [47.5, 53.2] (91-96) | 57.0% [54.2, 59.9] (97-102) |
| Value loss on held-out games | 0.613 | 0.458 |
| Same first move as the search | 82.7% | 84.7% |
| Same first move as humans (Riven) | 46.2% | 45.5% |

The gain came in the first five iterations; from 96 to 102 the anchor rate sits around 63%. The rate against each
anchor rose (general net 55 → 67%, spec_riven_059 56 → 63%). Before the change, spec_riven_059 scored exactly 100
of 200 against spec_riven_028 (three adoptions earlier). Held-out panel by opponent (iterations 97-102): Kennen
70%, Kai'Sa 68%, Master Yi 68%, Akali 66%, Orianna 43%, Mordekaiser 30% (the Mordekaiser pilot still beats Riven
far more than real players do).

The two refusals were noise: at 192 games a side the panel difference has a standard error of about 5 points, so a
-3 guard refuses roughly a quarter of the candidates that are as good as the net in place (iteration 100 was
refused with the best anchor rate of the series, 70.6%). The guard is -8 points from iteration 104; the mirror
floor (45% over 200 games) stays.

**10 Nov 15:15 UTC, from iteration 104: step 5.** `--selfplay-sims 500 --selfplay-extra cap=0.25,fast=80,explore=4`:
a full 500-simulation search on a quarter of the decisions (the only ones recorded as policy targets), 80
simulations on the rest, and the move drawn from the root visits during the first 4 turns. The opponents' pilots
search at 200. Average cost per decision about the same as before. [I] One track, so the comparison is before and
after on the anchor curve, not a controlled A/B.

**10 Nov 23:15 UTC, review after iterations 104 to 115** (12 iterations with step 5, 11 adopted, 1 refused on the
mirror floor; about 35 minutes each):

| Measure | Before (97-102) | Since step 5 (104-115) |
|---|---|---|
| Mirror against the four frozen anchors | 62.9% [59.9, 66.0] (960 games) | 72.2% [70.2, 74.2] (1,920 games) |
| Same, by halves | | 71.7% (104-109), 72.7% (110-115) |
| Held-out panel, net in place | 57.0% [54.2, 59.9] | 61.8% [59.8, 63.8] |
| Value loss on held-out games | 0.458 | 0.435 |
| Same first move as the search | 84.7% | 81.9% |
| Same first move as humans (Riven) | 45.5% | 45.5% |

Step 5 does not hurt, and its own share of the gain cannot be told. The level changed at iteration 103 (75.6%
against the anchors, 66.7% on the panel), and that net was trained and measured entirely with the earlier settings
(its run was dispatched 12 minutes before the change, on the earlier commit). Since then the curve is flat at about
72%: the two halves of 104-115 differ by 1.0 point (standard error 2.0). The fall of "same first move as the
search" is expected: only the full 500-simulation searches are recorded now (about 160k rows in the window, was
610k), and they are harder to predict than 200-simulation ones.

By anchor (104-115): general net 75%, spec_riven_020 71%, spec_riven_028 74%, spec_riven_059 69%. Held-out panel
by opponent: Master Yi 73%, Akali 70%, Kai'Sa 69%, Kennen 67%, Orianna 51% (was 38%), Mordekaiser 40% (was 29%):
the gain on the panel comes from the two bad matchups.

[I] Over the whole change (iterations 91 to 115) the net went from 46-52% to about 72% against the frozen nets,
most of it in two steps (91-96 and 103), with two plateaus. Human agreement did not move at all: the net gets
stronger at beating earlier nets without playing more like the Riven players in the replays.

**11 Nov 02:10 UTC: Riven paused at iteration 120, Volibear takes the 20 runners.** Stopping rule set the evening
before: pooled anchor rate of the last 8 iterations less than 2 points above the 8 before. It read 74.1% (113-120)
against 72.2% (105-112): +1.9, standard error 1.8. The net in place is spec_riven_120_v0 (committed, in Play).

The rule passed by a hair and the held-out panel says the net was still improving: the net in place scored 64.7%
over 113-120 against 59.6% over 105-112 (+5.1 points, standard error 1.7). [I] Riven is not exhausted; the pause is
a choice of where the runners are worth most before the ban announcement of 13 Nov, and the track can resume from
net 120.

Whole run, iterations 91 to 120 (30 iterations, 26 adopted, about 17.5 hours): anchors 48.8% → 74.1%; held-out
panel of the net in place 50.3% → 64.7%; value loss 0.613 → 0.437; same first move as humans 46.2% → about 45.5%
(unchanged).

**Volibear, same method: 11 Nov 02:07 UTC, from iteration 65** (the Riven settings without the human weight, 15%
mirror; start net spec_volibear_048_v0). First review at 11:00 UTC, iterations 65 to 79: 15 iterations, all 15
kept, about 30 minutes each.

| Measure | 65-71 | 72-79 |
|---|---|---|
| Mirror against its three frozen earlier nets (022, 037, 048) | 53.5% [50.1, 56.8] (840 games) | 59.0% [55.8, 62.1] (960 games) |
| Held-out panel, net in place | 69.9% [67.5, 72.4] | 75.3% [73.2, 77.5] |
| Value loss on held-out games | 0.486 (iteration 65) | 0.364 (iteration 79) |
| Same first move as the search | 88.4% (65) | 83.6% (79) |
| Same first move as humans (Volibear) | 44.3% (65) | 44.5% (79) |

The curve rises: +5.5 points against the frozen nets (standard error 2.4) and +5.4 on the held-out panel
(standard error 1.7). The first iteration alone read 49.2% against the three nets. The fourth anchor, the general
net, loses 96-98% of its games and carries no signal: the four-anchor figure in the ladder (64.0% → 68.6%) is
inflated by it and is not the one to read.

Held-out panel by opponent, 65-71 → 72-79: DiamondHat Riven 53 → 63%, Caravaggio Riven 51 → 60%, Kai'Sa 67 → 73%,
Kennen 80 → 82%, Master Yi 76 → 83%, Akali 93 → 91%. In self-play the three Riven lists went from 46-50% to
54-56%. [I] All the Riven lists, held-out ones included, are piloted by the same frozen net (spec_riven_120_v0), so
part of the gain against Riven can be this pilot's habits being learned rather than the matchup; real players have
Riven ahead of Volibear, and the lab cannot tell the two apart without a Riven pilot that keeps training.

One incident: the run for iteration 72 was dispatched 4 seconds after iteration 71's push, resolved the branch to
the commit before it, played iteration 71 a second time and died at the decide step (06:05 UTC, about 50 minutes
lost until the restart). The workflow now restarts such a run from its first job (commit a244cea).

**Volibear, second review, 11 Nov 19:05 UTC: iterations 80 to 88** (24 continuous iterations in all, all 24 kept).

| Measure | 65-71 | 72-79 | 80-88 |
|---|---|---|---|
| Mirror against its three frozen earlier nets | 53.5% [50.1, 56.8] | 59.0% [55.8, 62.1] | 58.0% [55.0, 60.9] (1,080 games) |
| Held-out panel, net in place | 69.9% [67.5, 72.4] | 75.3% [73.2, 77.5] | 77.7% [75.7, 79.6] (1,728 games) |
| Value loss on held-out games | 0.486 → 0.394 | 0.402 → 0.364 | 0.369 → 0.372 (lowest 0.343) |
| Same first move as humans | 44.3% | 44.5% | 45.4% (88) |

Against its own frozen nets the curve has been flat since iteration 72: last 8 iterations minus the 8 before,
-1.4 points (standard error 2.2). The held-out panel still edges up, +2.0 points (standard error 1.5), which is
not distinguishable from flat either. Panel by opponent, 72-79 → 80-88: DiamondHat Riven 63 → 63%, Caravaggio
Riven 60 → 63%, Kai'Sa 73 → 79%, Kennen 82 → 83%, Master Yi 83 → 88%, Akali 91 → 91%; in self-play the three Riven
lists went from 54-56% to 58-60%. [I] Same shape as Riven's run: a quick first step, then a plateau; Riven's
second step came after seven flat iterations, so a plateau of this length does not yet say the net is exhausted.

No iteration between 13:50 and 16:50 UTC: GitHub's apt mirror (azure.archive.ubuntu.com) stopped resolving on part
of the runners; three iterations failed on one machine each (a gate job, then self-play jobs, the last one despite
four retries), and a fourth run sat 40 minutes in p_plan's install. The engine's two Boost libraries are now in
the repository (lab/learn/runtime) and travel with the plan; an iteration uses no apt unless the engine sources
changed (commits d779ead, dbc02e1, ccbcae5, d6bd24b; confirmed by iterations 85 to 88).

Jeremy's list of 11 Nov (Laggy's Dragons with Rengar, lab/decks/community/jeremy_2026-11-11) was measured on
spec_volibear_084_v0 against Ritualo's list and the search's best, 40 games per opponent on the same deals
(lab/results/20261111-165301-volibear-jeremy-20261111.json): 66.0% against 78.9% for Ritualo's, -12.9 points
[-22.1, -3.7]; the search's list 81.7%, +2.7 [-5.7, +11.1]. The net never trained on that list, so part of the
gap is pilot error; Jeremy chose not to add it to the training lists.
