#!/usr/bin/env python3
"""realism — the simulated Riven matchups with the nets in use, against real players' win rates.

Plays Riven lists against the meta decks of the legends real players' data covers, each side piloted by the
net that plays that legend best today: its specialist's current net (lab/learn/specialists/<name>/ladder.json)
when one was adopted, else the general net (lab/learn/ladder.json). Writes
lab/meta/compare_meta-<date>-specialists.json in compare_matrix.py's format (rows with the simulated rate, the
real rate and interval, the gap and a verdict), which the dashboard's credibility step reads as the newest
comparison. With --loop it measures again whenever a net in use changes, and commits the file.

    python3 lab/tools/realism.py [--games 100] [--threads 4] [--sims 50] [--loop] [--commit]
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "lab"))
sys.path.insert(0, str(ROOT / "lab" / "learn"))
import riftlab  # noqa: E402
import loop  # noqa: E402

MATRIX = sorted((ROOT / "lab" / "meta").glob("riftdecks_winrate_*.json"))[-1]
LISTS = [("Best list (GA, unconfirmed)", "lab/variants/riven-multistart-ga-2--cef30c16fe.txt"),
         ("Sacramento base (Nam Dang)", "decks/riven_sacramento.txt")]
_M = "lab/decks/meta/"
OPPONENTS = [("Kennen (zombro)", "Kennen", _M + "radiance_release_2026-10-24/07_zombro_kennen.txt"),
             ("Kennen (Xi'an 10th)", "Kennen", _M + "s5_xian_city_challenge_2026-11-01/10_yizhenglong_kennen.txt"),
             ("Kai'Sa (Jason Tian)", "Kai'Sa", _M + "ldxp_sacramento_radiance_win_a_case_samedi_2026-10-24/01_jason_tian_kai_sa.txt"),
             ("Akali (Sacramento Challenge 1st)", "Akali", _M + "ldxp_sacramento_challenge_2026-10-25/01_vincent_nguyen_akali.txt"),
             ("Evelynn (Vegas)", "Evelynn", _M + "vegas_singles_tuesday_nexus_night_2026-10-27/01_dunc_evelynn.txt"),
             ("Orianna (Xi'an 7th)", "Orianna", _M + "s5_xian_city_challenge_2026-11-01/07_luoluoluofu_orianna.txt")]


def nets_in_use() -> dict:
    """{'general': net, '<specialist>': net, ...} — the nets the measure is played with."""
    out = {"general": json.loads((ROOT / "lab/learn/ladder.json").read_text()).get("current_net")}
    for f in sorted((ROOT / "lab/learn/specialists").glob("*/ladder.json")):
        net = json.loads(f.read_text()).get("current_net")
        if net and (ROOT / net).exists():
            out[f.parent.name] = net
    return out


def pilot(nets: dict, legend: str) -> str:
    key = re.sub(r"[^a-z0-9]+", "_", legend.lower()).strip("_")
    return nets.get(key) or nets["general"]


def agent(net: str, sims: int) -> str:
    return f"pmcts:sims={sims},eval=policy:{net},worlds=8,settle=60"


def play(d1, d2, a1, a2, games, threads, seed):
    cmd = [str(loop.ENGINE), "--deck1", d1, "--deck2", d2, "--agent1", a1, "--agent2", a2, "--games", str(games),
           "--threads", str(threads), "--seed", str(seed), "--render-html", "off", "--web", "off"]
    out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True).stdout
    return (int(re.search(r"P1 wins:\s+(\d+)", out).group(1)), int(re.search(r"P2 wins:\s+(\d+)", out).group(1)))


def measure(a) -> dict:
    nets = nets_in_use()
    real = json.loads(MATRIX.read_text())["matchups"]["Riven"]
    rows = []
    riven = pilot(nets, "Riven")
    for label, path in LISTS:
        for k, (olabel, olegend, odeck) in enumerate(OPPONENTS):
            opp = pilot(nets, olegend)
            half = max(2, a.games // 2)
            w1, _ = play(path, odeck, agent(riven, a.sims), agent(opp, a.sims), half, a.threads, a.seed + 1000 * k)
            _, w2 = play(odeck, path, agent(opp, a.sims), agent(riven, a.sims), half, a.threads, a.seed + 1000 * k)
            n = 2 * max(2, half)
            sim = (w1 + w2) / n
            r = real.get(olegend)
            lo, hi = riftlab.wilson(r["win_rate"] * r["games"], r["games"])
            s_lo, s_hi = riftlab.wilson(w1 + w2, n)
            verdict = "far above" if sim > hi + 0.05 else "far below" if sim < lo - 0.05 else "within"
            rows.append({"deck": label, "legend": "Riven", "opponent": olabel, "opponent_legend": olegend,
                         "sim": round(sim, 3), "sim_games": n, "sim_ci95": [round(s_lo, 3), round(s_hi, 3)],
                         "real": r["win_rate"], "real_games": r["games"], "real_ci95": [round(lo, 3), round(hi, 3)],
                         "gap": round(sim - r["win_rate"], 3), "verdict": verdict,
                         "pilots": {"Riven": riven, olegend: opp}})
            print(f"{label[:28]:<28} vs {olabel:<34} {100 * sim:5.1f}%  real {100 * r['win_rate']:4.1f}%  {verdict}", flush=True)
    return {"description": "Simulated Riven matchups with the nets in use (each legend piloted by its specialist when it has one, "
                           "else the general net), against real players' rates (lab/tools/realism.py).",
            "measured_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"), "matrix": str(MATRIX.relative_to(ROOT)),
            "nets": nets, "net": Path(riven).stem, "games_per_pairing": 2 * max(2, a.games // 2), "sims": a.sims, "rows": rows}


def write(doc) -> Path:
    out = ROOT / "lab" / "meta" / f"compare_meta-{doc['measured_utc'][:10]}-specialists.json"
    out.write_text(json.dumps(doc, indent=1))
    within = sum(r["verdict"] == "within" for r in doc["rows"])
    print(f"wrote {out.relative_to(ROOT)}: {within} of {len(doc['rows'])} within real players' range", flush=True)
    return out


def commit(path: Path, doc) -> None:
    within = sum(r["verdict"] == "within" for r in doc["rows"])
    msg = (f"meta: realism with the nets in use, {within} of {len(doc['rows'])} matchups within real players' range\n\n"
           f"lab/tools/realism.py, {doc['games_per_pairing']} games per pairing, nets: "
           + ", ".join(f"{k} {Path(v).stem}" for k, v in doc["nets"].items() if v)
           + "\n\nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\n"
             "Claude-Session: https://claude.ai/code/session_01F68srW6k6CBYFTm9oVwNVu")
    run = lambda *c: subprocess.run(c, cwd=ROOT, capture_output=True, text=True)  # noqa: E731
    run("git", "pull", "-q", "--no-rebase", "origin", "claude/zen-goodall-hb2ssu")
    run("git", "add", str(path))
    if run("git", "commit", "-q", "-m", msg).returncode == 0:
        for i in range(6):
            if run("git", "push", "-q", "-u", "origin", "claude/zen-goodall-hb2ssu").returncode == 0:
                break
            time.sleep(10 * (i + 1))
            run("git", "pull", "-q", "--no-rebase", "origin", "claude/zen-goodall-hb2ssu")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--sims", type=int, default=50)
    ap.add_argument("--seed", type=int, default=9105)
    ap.add_argument("--loop", action="store_true", help="measure again whenever a net in use changes (checks every 10 min)")
    ap.add_argument("--commit", action="store_true")
    a = ap.parse_args(argv)
    last = None
    while True:
        if a.loop:
            subprocess.run(["git", "pull", "-q", "--no-rebase", "origin", "claude/zen-goodall-hb2ssu"], cwd=ROOT, capture_output=True)
        nets = nets_in_use()
        if nets != last:
            doc = measure(a)
            path = write(doc)
            if a.commit:
                commit(path, doc)
            last = doc["nets"]
            a.seed += 7
        if not a.loop:
            return
        time.sleep(600)


if __name__ == "__main__":
    main(sys.argv[1:])
