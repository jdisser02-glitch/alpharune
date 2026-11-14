#!/usr/bin/env python3
"""One screen of state for the hourly watchdog: the training tracks and the post-ban list searches.

    python3 lab/tools/watch_status.py            (after `git pull`)

Training: for each track of TRACKS, its last iteration, when it was committed, whether the last was kept, the net
in the repository, and STALE when nothing was committed for more than twice the time an iteration takes on its
share of the runners (then: read the last run's failed job before dispatching the chain again, see
lab/learn/TRAINING_KNOWLEDGE.md section 5). Searches: for each spec of SEARCHES, which stage's files exist and
the command of the next step (lab/OPTIMISER_KNOWLEDGE.md section 7).
"""
import datetime as dt
import glob
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# track -> minutes one iteration is expected to take on its share of the runners (13 Nov: 35 minutes on 20)
TRACKS = {"evelynn": 150, "riven": 150, "mordekaiser": 180, "volibear": 300, "kai_sa": 240, "kennen": 240}
SEARCHES = ["mordekaiser-postban", "evelynn-postban", "volibear-postban", "riven-postban-shattered", "riven-postban-deserter"]
STAGES = ["race1", "race2", "confirm", "confirm2"]


def sh(*cmd):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True).stdout.strip()


def main():
    now = dt.datetime.now(dt.timezone.utc)
    print(f"UTC {now:%d %b %H:%M}")
    print("TRAINING (track: last iteration, minutes since its commit, verdict, committed net)")
    for name, expect in TRACKS.items():
        d = ROOT / "lab/learn/specialists" / name
        lad = json.loads((d / "ladder.json").read_text())
        last = sh("git", "log", "-1", "--format=%cI|%s", f"--grep=train (spec-{name.replace('_', '-')}): iteration")
        if not last:
            last = sh("git", "log", "-1", "--format=%cI|%s", f"--grep=train (spec-{name}): iteration")
        when, _, subject = last.partition("|")
        age = (now - dt.datetime.fromisoformat(when)).total_seconds() / 60 if when else None
        flag = "PAUSED" if (d / "PAUSE").exists() else ("STALE" if age is None or age > 2 * expect + 30 else "ok")
        print(f"  {name:12s} {flag:6s} it {len(lad['iterations']) - 1:3d}  {'%4d min' % age if age is not None else '   never'}"
              f" (expected every {expect})  {subject.split(': ', 1)[-1][:34]:34s} net {Path(lad.get('current_net') or '-').stem}")
    print("SEARCHES (spec: explored lists; then each stage: requests written / results in / ranked)")
    for name in SEARCHES:
        spec_path = ROOT / "lab/specs/deckopt" / f"{name}.json"
        if not spec_path.exists():
            continue
        d = ROOT / "lab/deckopt" / name
        shards = sorted(d.glob("explore-*.json"))
        runner = [f for f in shards if int(f.stem.split("-")[1]) < 10]
        lists = sum(len(json.loads(f.read_text())["lists"]) for f in shards)
        line = f"  {name:26s} {len(shards)} shards ({len(runner)} from the runners), {lists} lists"
        nxt = None
        if not (d / "candidates.json").exists():
            # a runner shard, or enough lists from the session's own machine
            nxt = ("model, propose, postban.py check, wave --stage race1, dispatch" if runner or lists >= 1500
                   else "wait for the runner shard (explore-0N.json), or 1,500 lists from the session's machine")
        for st in STAGES:
            reqs = glob.glob(str(ROOT / "lab/specs/deckopt" / f"{name}.{st}.*.json"))
            res = [f for f in glob.glob(str(ROOT / "lab/results" / f"*-{name}-{st}-*.json"))]
            ranked = (d / f"{st}-rank.json").exists()
            if reqs or res:
                line += f" | {st} {len(reqs)}/{len(res)}/{'ranked' if ranked else '-'}"
            if nxt is None:
                if not reqs:
                    nxt = f"wave --stage {st}, commit, push, dispatch its requests" if st != "confirm2" else "confirm2 only once a newer net is committed for this legend"
                elif len(res) < len(reqs):
                    nxt = f"wait for {len(reqs) - len(res)} result(s) of {st} (dispatched? see section 7's log)"
                elif not ranked:
                    nxt = f"rank --stage {st}" + (", merge_results.py --write, log, report" if st.startswith("confirm") else "")
        print(line + f"\n      next: {nxt or 'nothing'}")
    meta = sorted(glob.glob(str(ROOT / "lab/meta/postban_meta_*.json")))
    print("META MATRIX:", ", ".join(Path(m).name for m in meta) or "none yet (lab/tools/meta_matrix.py)")


if __name__ == "__main__":
    main()
