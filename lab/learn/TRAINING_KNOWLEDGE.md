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
