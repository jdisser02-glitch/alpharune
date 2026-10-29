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
(a value network from `lab/learn/nets/`).

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
