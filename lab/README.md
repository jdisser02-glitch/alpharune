# Riven test lab

Everything needed to compare Riftbound deck lists by simulated win rate.

```
lab/
  riftlab.py          variants, deck statistics, validation, test runner
  optimize.py         list search: successive halving over swaps / variants
  learn/              self-play value-net training loop (ladder.json, nets/)
  dashboard/          the Riven Lab page (build.py assembles dist/ for publishing)
  specs/              test specs and variant specs kept in the repository
  requests/           drop a test spec here (push) to have GitHub Actions run it
  requests/done/      requests already run
  results/            one JSON per test run (config, engine commit, results)
  variants/           deck files generated from variant specs (do not edit)
  decks/meta/<event>/ tournament decklists imported from public pages
  tools/              import_decklists.py and other helpers
```

## Variants

A variant is a base deck plus changes. Example
(`lab/specs/sacramento_heronless.json`):

```json
{
  "name": "sacramento-heronless",
  "base": "decks/riven_sacramento.txt",
  "note": "3 Astral Heron out, 2 Riven, Deserter + 1 Blitzcrank in.",
  "changes": {"maindeck": {"Astral Heron": -3, "Riven, Deserter": 2, "Blitzcrank, Impassive": 1}}
}
```

`changes` may also hold `runes`, `battlefields`, `sideboard` (same
name → delta form), `legend` and `champion` (a card name).

```bash
python3 lab/riftlab.py variant lab/specs/sacramento_heronless.json   # build + validate + stats
python3 lab/riftlab.py stats decks/riven_sacramento.txt              # curve, types, power symbols
python3 lab/riftlab.py validate lab/decks/meta/*/*.txt               # rules + unimplemented cards
```

## Tests

A test plays every deck of `decks` against every entry of `opponents`,
`games` games per pairing, half on each seat, and reports win rates with 95%
Wilson intervals and an opponent-weighted average.

```json
{
  "name": "heronless-vs-gauntlet",
  "description": "What does cutting Heron cost?",
  "agent": "ismcts:sims=100,eval=board",
  "opp_agent": "ismcts:sims=100,eval=board",
  "games": 200,
  "seed": 1,
  "decks": ["decks/riven_sacramento.txt", {"variant": "lab/specs/sacramento_heronless.json"}],
  "opponents": [{"deck": "lab/decks/meta/.../06_dylan_sumner_kennen.txt", "label": "Kennen", "weight": 0.08}]
}
```

```bash
python3 lab/riftlab.py run lab/specs/smoke_random.json
```

Agents: `random` (only checks that games run), `mcts:sims=N` (sees hidden
information — for reference only), `ismcts:sims=N` (honest: samples the
hidden cards each simulation). Options after `sims=N`: `eval=score|board`,
`c=X`, `final=max|sample`, `det=0` (disable sampling), `eval=net:FILE`
(a value network from `lab/learn/nets/`), `settle=N` (a leaf in the middle
of a chain, showdown or combat is played on with passes / first options for
at most N decisions before it is evaluated; off by default). `pmcts:sims=N`
(the engine's own search, used by the learning loop) takes the same options
plus `eval=policy:FILE`, `worlds=W`, `threads=T`, `prior=`, `root=`.
In any spec, `@current` stands for the training ladder's current network
(`lab/learn/ladder.json` → `current_net`), so a test always runs the best
adopted bot: use `pmcts:sims=50,eval=policy:@current,worlds=8,settle=60`, the
setting the training loop itself plays with.
`lab/specs/settle_ab.json` is an A/B of two agents (`agent` against
`opp_agent`) on two decks played by both.

## List search

`optimize.py` tries many variants of one list cheaply and spends games only
on the promising ones. Every candidate (plus the base list as a control)
plays the gauntlet a few games per opponent; the better half survives and
plays again with twice the games, and so on. All candidates of a round share
seeds, so differences come from the lists rather than the deal.

```bash
python3 lab/optimize.py lab/specs/optimize/riven_flex_1.json --dry-run   # candidates + game budget
python3 lab/optimize.py lab/specs/optimize/riven_flex_1.json
```

Candidates are explicit (`candidates`) or generated one-for-one swaps
(`swap.out` x `swap.in`, illegal ones skipped). The result is a normal test
file in `lab/results/` (shown on the dashboard) with an `optimize` block per
round. Read it with the intervals in mind: with a few hundred games, only
differences of several points are real.

## Learning loop

`learn/loop.py` alternates self-play (the current bot plays itself on random
pairs of pool decks, recording positions and results), training a value
network on recent data (`learn/train_value.py`) and gating (the new network
must beat the current one to replace it). Each iteration is a row in
`learn/ladder.json`, shown on the dashboard's Bot tab.

## Running a test without this machine

Commit a test spec to `lab/requests/` on the work branch. The workflow
`.github/workflows/riftlab.yml` builds the engine, runs it on a GitHub
runner (4 cores, up to ~6 h), and commits the result JSON to `lab/results/`.

## Confirming genetic-search results

GA search scores are exploratory. At the end, the top **surviving** candidates
(default 3) and the unchanged base play a separate confirmation batch:
300 games per opponent per list by default, equally split between seats.
Set `"confirmation": {"finalists": 3, "games": 1000}` in the GA spec to
choose the budget **before** running. This is additional to the search budget;
confirmation runs sequentially and can be expensive with a strong bot.

Confirmation uses fresh seed ranges and never feeds its scores back into the
search. Opponent weights apply to both seats equally. It records a simultaneous
95% Hoeffding interval for each candidate's win-rate difference from the base,
with a union bound across all tested lists and opponent/seat strata.
These bounds are conservative, account for testing several finalists, and do
not require independence between candidates sharing seeds. They assume independent
games within each stratum. Draws count as non-wins, as in existing lab reports.
Do not repeatedly extend the budget until an interval passes: that invalidates
the fixed-budget guarantee.

Only a strictly positive lower bound produces
`lab/variants/<name>-confirmed.txt`. Otherwise the result is `inconclusive`;
the base has not been shown inferior. Old `-best.txt` files are historical
exploration outputs, not recommendations. The dashboard excludes GA search
scores from its global best-list widget and shows confirmation status in the
test conclusion. Confirmation details live in the result's `ga.confirmation`.
A gain in simulation still requires a faithful engine and a competent bot;
this does not automatically pass the separate human-game validation gates.

Resume checkpoints now contain SHA-256 fingerprints of the executable, resolved
network files, decks, search code and full specification. Legacy or incompatible
checkpoints are rejected. The exact random-generator state and the next generation
are saved, so interruption does not silently restart from a different population.
Changing the configuration requires a new search; modifying inputs during a run
also causes rejection before saving the next checkpoint.

Card-impact IWD remains an **observational association**, useful for proposing
swaps, not estimating their causal benefit. Compare a frozen base and proposed
swap directly on independent games before making a recommendation.

Run the Python regression suite (no C++ build or training required):
`python3 -m unittest discover -s lab/tests -v`.
