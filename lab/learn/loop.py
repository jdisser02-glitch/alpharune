#!/usr/bin/env python3
"""Self-play learning loop for the Riftbound bot.

Each iteration:
  1. self-play: the current bot plays itself on random pairs of decks from the
     pool, recording every decision of both players with the result and the
     final score margin (riftbound --value-data, RBV2 records);
  2. training: a new value network is trained on a rolling replay buffer of the
     last --buffer iterations, recent data weighted more (train_value.py) — or,
     with --policy, a two-head policy + value network (train_policy.py) from the
     searches' visit distributions and the human imitation set, which the bot
     then uses as its PUCT prior (pmcts:eval=policy:<file>);
  3. gating: the new bot plays the current one on the Riven mirror, seats
     swapped, in blocks, under a sequential probability ratio test (H0 50% vs
     H1 60%, alpha = beta = 0.05): it stops as soon as the candidate is clearly
     better or clearly worse, otherwise after --gate-max games, when the plain
     win rate decides (>= --accept). A fixed panel of matchups (PANEL: Riven
     against Kennen, Kai'Sa, Akali, Orianna, and a non-Riven pairing) guards
     the mirror: the candidate plays both decks of each, and is refused when it
     scores below --panel-floor there, so a net that only learns the mirror is
     not adopted;
  4. yardsticks: the candidate also plays random, the fixed reference net and
     a few frozen older nets (ladder "anchor_nets") so iterations stay
     comparable, and a Bradley-Terry Elo over every match ever recorded (random
     anchored at 0) is written per net into ladder.json.

Seeds: every batch takes its own range of game seeds (seed_base: one slice per
iteration, per kind of match and per block), so no two blocks replay the same
deals; the two seat orders of one block share a range on purpose (the same
deals with the agents swapped, a paired comparison).

Everything is logged to lab/learn/ladder.json (read by the dashboard); networks
go to lab/learn/nets/. Iteration 0's data comes from random play.

usage: loop.py [--iterations 3] [--games 200] [--sims 30] [--threads N] [--agent pmcts|ismcts]
               [--policy [--human data/learn/human.rbv3] [--human-weight 0.3]]
"""
import argparse
import hashlib
import datetime as dt
import json
import math
import os
import random
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LEARN = ROOT / "lab" / "learn"
# RIFTBOUND_LEARN_DIR redirects the ladder, nets and data (a dry run, a fork).
_OUT = Path(os.environ.get("RIFTBOUND_LEARN_DIR", LEARN))
DATA = (ROOT / "data" / "learn") if "RIFTBOUND_LEARN_DIR" not in os.environ else _OUT / "data"
NETS = _OUT / "nets"
LADDER = _OUT / "ladder.json"
ENGINE = Path(os.environ.get("RIFTBOUND_BIN", ROOT / "build" / "riftbound"))
sys.path.insert(0, str(ROOT / "lab"))
import riftlab  # noqa: E402

GATE_DECK = "decks/riven_sacramento.txt"
_META = "lab/decks/meta/"
# Fixed matchups that guard the mirror gate (decks the jury uses). Changing the
# list changes what "panel" means in ladder.json: append, don't reorder.
PANEL = [
    ("riven-kennen", GATE_DECK, _META + "s5_xian_city_challenge_2026-11-01/10_yizhenglong_kennen.txt"),
    ("riven-kaisa", GATE_DECK, _META + "ldxp_sacramento_radiance_win_a_case_samedi_2026-10-24/01_jason_tian_kai_sa.txt"),
    ("riven-akali", GATE_DECK, _META + "ldxp_sacramento_challenge_2026-10-25/01_vincent_nguyen_akali.txt"),
    ("riven-orianna", GATE_DECK, _META + "s5_xian_city_challenge_2026-11-01/07_luoluoluofu_orianna.txt"),
    ("kennen-kaisa", _META + "rro25_2026-10-17/01_simple_kennen.txt", _META + "rro25_2026-10-17/02_art3mis_kai_sa.txt"),
]

# Specialists (stages.py --specialist NAME): one net per legend, trained on its
# own decisions with that legend against a jury of meta decks and judged in its
# legend's mirror against its previous version. The jury is piloted by the
# general net (ladder.json) or, for a legend that has its own specialist, by
# that specialist's current net. Own ladder: lab/learn/specialists/NAME/.
_RIVEN_BEST = "lab/variants/riven-multistart-ga-2--cef30c16fe.txt"
SPECIALISTS = {
    "kennen": {"legend": "Kennen",
               "decks": [_META + "radiance_release_2026-10-24/07_zombro_kennen.txt",
                         _META + "s5_xian_city_challenge_2026-11-01/10_yizhenglong_kennen.txt",
                         _META + "sacramento_showdown_2026-10-24/06_dylan_sumner_kennen.txt",
                         _META + "rro25_2026-10-17/01_simple_kennen.txt"],
               "jury": ["decks/riven_best_field.txt",
                        _META + "coolstuffcon_orlando_25k_2026-11-01/01_tabris_riven.txt",
                        _META + "ldxp_sacramento_radiance_win_a_case_samedi_2026-10-24/01_jason_tian_kai_sa.txt",
                        _META + "ldxp_sacramento_challenge_2026-10-25/01_vincent_nguyen_akali.txt",
                        _META + "s5_xian_city_challenge_2026-11-01/07_luoluoluofu_orianna.txt"]},
    "akali": {"legend": "Akali",
              "decks": [_META + "ldxp_sacramento_challenge_2026-10-25/01_vincent_nguyen_akali.txt",
                        _META + "sacramento_showdown_2026-10-24/03_alex_gorica_akali.txt"],
              "jury": [_RIVEN_BEST,
                       _META + "coolstuffcon_orlando_25k_2026-11-01/01_tabris_riven.txt",
                       _META + "radiance_release_2026-10-24/07_zombro_kennen.txt",
                       _META + "ldxp_sacramento_radiance_win_a_case_samedi_2026-10-24/01_jason_tian_kai_sa.txt",
                       _META + "s5_xian_city_challenge_2026-11-01/07_luoluoluofu_orianna.txt"]},
    "riven": {"legend": "Riven",
              "decks": [GATE_DECK, _RIVEN_BEST,
                        _META + "coolstuffcon_orlando_25k_2026-11-01/01_tabris_riven.txt",
                        _META + "rift_tour_finale_paris_2026-10-24/01_nuitwltuiazewikx_riven.txt",
                        # 6 Nov: more varied lists, so the net learns the cards it misplayed (Jeremy's exhaust
                        # package: Ki Burst, Meditation, Life from Stone) and the field search's finalists.
                        "decks/riven_best_field.txt", "lab/variants/riven-field-5--001ea89136.txt",
                        "decks/jeremy/tournament_d.txt", _META + "convergence_iv_2026-10-31/01_wojciech_cetner_riven.txt"],
              "jury": [_META + "radiance_release_2026-10-24/07_zombro_kennen.txt",
                       _META + "s5_xian_city_challenge_2026-11-01/10_yizhenglong_kennen.txt",
                       _META + "ldxp_sacramento_challenge_2026-10-25/01_vincent_nguyen_akali.txt",
                       _META + "ldxp_sacramento_radiance_win_a_case_samedi_2026-10-24/01_jason_tian_kai_sa.txt",
                       _META + "s5_xian_city_challenge_2026-11-01/07_luoluoluofu_orianna.txt",
                       _META + "vegas_singles_tuesday_nexus_night_2026-10-27/01_dunc_evelynn.txt",
                       # 7 Nov: Mordekaiser's specialist beats the Riven pilot far more often than real players
                       # do (55% simulated vs 29% real): Riven trains and is judged against it too.
                       _META + "ka_zhi_yu_tournoi_bo3_2026-10-24/04_neal_mordekaiser.txt",
                       # 8 Nov (Jeremy): riven and riven_h merged; Volibear Dragons (and its Black Market) joins the jury.
                       "lab/decks/community/deck_profiles_2026-11-07/ritualo_volibear_dragons.txt"]},
    # 8 Nov (Jeremy): Riven started again from the net that replays the human games (replay_imitate.py,
    # replay_base), trained by playing (the reward of winning) while pulled toward that human net.
    "riven_h": {"legend": "Riven", "human_slug": "riven",
              "decks": [GATE_DECK, _RIVEN_BEST,
                        _META + "coolstuffcon_orlando_25k_2026-11-01/01_tabris_riven.txt",
                        _META + "rift_tour_finale_paris_2026-10-24/01_nuitwltuiazewikx_riven.txt",
                        # 6 Nov: more varied lists, so the net learns the cards it misplayed (Jeremy's exhaust
                        # package: Ki Burst, Meditation, Life from Stone) and the field search's finalists.
                        "decks/riven_best_field.txt", "lab/variants/riven-field-5--001ea89136.txt",
                        "decks/jeremy/tournament_d.txt", _META + "convergence_iv_2026-10-31/01_wojciech_cetner_riven.txt"],
              "jury": [_META + "radiance_release_2026-10-24/07_zombro_kennen.txt",
                       _META + "s5_xian_city_challenge_2026-11-01/10_yizhenglong_kennen.txt",
                       _META + "ldxp_sacramento_challenge_2026-10-25/01_vincent_nguyen_akali.txt",
                       _META + "ldxp_sacramento_radiance_win_a_case_samedi_2026-10-24/01_jason_tian_kai_sa.txt",
                       _META + "s5_xian_city_challenge_2026-11-01/07_luoluoluofu_orianna.txt",
                       _META + "vegas_singles_tuesday_nexus_night_2026-10-27/01_dunc_evelynn.txt",
                       # 7 Nov: Mordekaiser's specialist beats the Riven pilot far more often than real players
                       # do (55% simulated vs 29% real): Riven trains and is judged against it too.
                       _META + "ka_zhi_yu_tournoi_bo3_2026-10-24/04_neal_mordekaiser.txt",
                       # 8 Nov: Jeremy's Volibear Dragons brings Black Market ([A][A]: draw 1, either player, once a
                       # turn), which the Riven bot had never met and used every turn, recycling its runes away.
                       "lab/decks/community/deck_profiles_2026-11-07/ritualo_volibear_dragons.txt"]},
    "kai_sa": {"legend": "Kai'Sa",
               "decks": [_META + "ldxp_sacramento_radiance_win_a_case_samedi_2026-10-24/01_jason_tian_kai_sa.txt",
                         _META + "radiance_release_2026-10-24/02_icrazyfool_kai_sa.txt",
                         _META + "rro25_2026-10-17/02_art3mis_kai_sa.txt"],
               "jury": [_RIVEN_BEST,
                        _META + "coolstuffcon_orlando_25k_2026-11-01/01_tabris_riven.txt",
                        _META + "radiance_release_2026-10-24/07_zombro_kennen.txt",
                        _META + "vegas_singles_tuesday_nexus_night_2026-10-27/01_dunc_evelynn.txt",
                        _META + "s5_xian_city_challenge_2026-11-01/07_luoluoluofu_orianna.txt"]},
    "evelynn": {"legend": "Evelynn",
                "decks": [_META + "vegas_singles_tuesday_nexus_night_2026-10-27/01_dunc_evelynn.txt"],
                "jury": [_RIVEN_BEST,
                         _META + "coolstuffcon_orlando_25k_2026-11-01/01_tabris_riven.txt",
                         _META + "radiance_release_2026-10-24/07_zombro_kennen.txt",
                         _META + "ldxp_sacramento_radiance_win_a_case_samedi_2026-10-24/01_jason_tian_kai_sa.txt",
                         _META + "s5_xian_city_challenge_2026-11-01/07_luoluoluofu_orianna.txt"]},
    # 6 Nov: Jeremy's next legends (8 runners each). Lists from openrift.app (Radiance top finishes); juries are the
    # legends they meet most on RiftLite (lab/meta/riftlite_matrix_2026-11-06.json): Riven, Kai'Sa, Kennen, Evelynn,
    # and each other.
    "orianna": {"legend": "Orianna",
                "decks": [_META + "s5_xian_city_challenge_2026-11-01/07_luoluoluofu_orianna.txt",
                          _META + "s5_shanghai_city_challenge_2026-11-01/03_youjike_sishumoling_orianna.txt",
                          _META + "ka_zhi_yu_tournoi_bo3_2026-10-24/03_dongdong_kapai_orianna.txt",
                          _META + "s5_fuzhou_city_challenge_2026-10-31/19_liuxin_veil_orianna.txt",
                          # 7 Nov: Western Showdown lists (riftbound.zone, riftwatch.gg): more varied builds
                          "lab/decks/community/orianna_showdowns_2026-10-31/kevinyin_sacramento_showdown_134th.txt",
                          "lab/decks/community/orianna_showdowns_2026-10-31/brandonwolfgang_convergence4_142nd.txt",
                          "lab/decks/community/orianna_showdowns_2026-10-31/jimmythefish7_melbourne_showdown_22nd.txt",
                          "lab/decks/community/orianna_showdowns_2026-10-31/burrsir_orlando_showdown_59th.txt"],
                "jury": [# 8 Nov (Jeremy): the Stuttgart Regional Championship Europe field (291 decks: Riven 43%, Kai'Sa 17.5%,
                         # Kennen 15%, Master Yi 5.5%); two Riven lists, --spec-riven-weight 1.5 in self-play.
                         # 9 Nov: the Stuttgart Top 8 lists themselves (riftwatch.gg open decklists).
                         _META + "stuttgart_rc_europe_2026-11-08/02_sonecon_riven.txt",
                         _META + "stuttgart_rc_europe_2026-11-08/03_diamondhat_riven.txt",
                         _META + "stuttgart_rc_europe_2026-11-08/05_dziden_kai_sa.txt",
                         _META + "stuttgart_rc_europe_2026-11-08/01_evansrhim_kennen.txt",
                         _META + "s5_shanghai_city_challenge_2026-11-01/01_pcq_master_yi.txt"]},
    "mordekaiser": {"legend": "Mordekaiser",
                    "decks": [_META + "ka_zhi_yu_tournoi_bo3_2026-10-24/04_neal_mordekaiser.txt",
                              _META + "ldxp_sacramento_challenge_2026-10-25/09_calvin_mordekaiser.txt",
                              _META + "future_cup_fsw_evolution_cards_2026-11-01/09_cijian_baiwen_mordekaiser.txt",
                              _META + "s5_shenzhen_city_challenge_2026-10-31/17_3x_tianzi_mordekaiser.txt"],
                    "jury": [# 8 Nov (Jeremy): the Stuttgart Regional Championship Europe field (291 decks: Riven 43%, Kai'Sa 17.5%,
                         # Kennen 15%, Master Yi 5.5%); two Riven lists, --spec-riven-weight 1.5 in self-play.
                         # 9 Nov: the Stuttgart Top 8 lists themselves (riftwatch.gg open decklists).
                         _META + "stuttgart_rc_europe_2026-11-08/02_sonecon_riven.txt",
                         _META + "stuttgart_rc_europe_2026-11-08/03_diamondhat_riven.txt",
                         _META + "stuttgart_rc_europe_2026-11-08/05_dziden_kai_sa.txt",
                         _META + "stuttgart_rc_europe_2026-11-08/01_evansrhim_kennen.txt",
                         _META + "s5_shanghai_city_challenge_2026-11-01/01_pcq_master_yi.txt"]},
    # 7 Nov: Jeremy asked for a Volibear specialist (Ritualo's Volibear Dragons deck profile; the general net piloted
    # it at 3-34%, casting removal on its own units). Jury: the legends Volibear meets most in today's meta.
    "volibear": {"legend": "Volibear",
                 "decks": ["lab/decks/community/deck_profiles_2026-11-07/ritualo_volibear_dragons.txt",
                           # Radiance Volibear lists from riftbound.zone (none placed high in this period)
                           "lab/decks/community/volibear_radiance_2026-10-31/soldog_convergence4_20261031.txt",
                           "lab/decks/community/volibear_radiance_2026-10-31/aechkay_sacramento_showdown_20261024.txt",
                           "lab/decks/community/volibear_radiance_2026-10-31/mustafa_sacramento_showdown_20261024.txt",
                           "lab/decks/community/volibear_radiance_2026-10-31/oryzias_aserty_invitational_20261010.txt"],
                 "jury": [# 8 Nov (Jeremy): the Stuttgart Regional Championship Europe field (291 decks: Riven 43%, Kai'Sa 17.5%,
                         # Kennen 15%, Master Yi 5.5%); two Riven lists, --spec-riven-weight 1.5 in self-play.
                         # 9 Nov: the Stuttgart Top 8 lists themselves (riftwatch.gg open decklists).
                         _META + "stuttgart_rc_europe_2026-11-08/02_sonecon_riven.txt",
                         _META + "stuttgart_rc_europe_2026-11-08/03_diamondhat_riven.txt",
                         _META + "stuttgart_rc_europe_2026-11-08/05_dziden_kai_sa.txt",
                         _META + "stuttgart_rc_europe_2026-11-08/01_evansrhim_kennen.txt",
                         _META + "s5_shanghai_city_challenge_2026-11-01/01_pcq_master_yi.txt"]},
    "ziggs": {"legend": "Ziggs",
              "decks": ["lab/decks/meta/community_2026-11-05/01_lavenchie_ziggs.txt"],
              "jury": [_RIVEN_BEST,
                       _META + "coolstuffcon_orlando_25k_2026-11-01/01_tabris_riven.txt",
                       _META + "radiance_release_2026-10-24/07_zombro_kennen.txt",
                       _META + "ldxp_sacramento_radiance_win_a_case_samedi_2026-10-24/01_jason_tian_kai_sa.txt",
                       _META + "vegas_singles_tuesday_nexus_night_2026-10-27/01_dunc_evelynn.txt"]},
}
for _s in SPECIALISTS.values():
    _s.setdefault("mirror", _s["decks"][0])
MAIN_LADDER = LADDER
NET_PREFIX = "net_"
TRACK = None


def use_track(name):
    """Points the ladder and the net names at specialist `name` (None: the general net)."""
    global LADDER, NET_PREFIX, TRACK
    if not name:
        LADDER, NET_PREFIX, TRACK = MAIN_LADDER, "net_", None
        return
    if name not in SPECIALISTS:
        raise SystemExit(f"unknown specialist {name}: {sorted(SPECIALISTS)}")
    LADDER, NET_PREFIX, TRACK = _OUT / "specialists" / name / "ladder.json", f"spec_{name}_", name


def specialist_of_deck(path):
    """The specialist whose legend plays `path` (by its deck lists, else by the file name), or None."""
    p = str(path)
    for name, s in SPECIALISTS.items():
        if p in s["decks"] or name in Path(p).name.lower():
            return name
    return None


def specialist_pilots():
    """Current net of every specialist that adopted one: {name: net}."""
    out = {}
    for name in SPECIALISTS:
        f = _OUT / "specialists" / name / "ladder.json"
        if f.exists():
            net = json.loads(f.read_text()).get("current_net")
            if net and (ROOT / net).exists():
                out[name] = net
    return out


# Seed plan. The engine plays game N of a batch with seed base+N, so a batch
# owns [base, base + SEED_BLOCK). Each iteration owns SEED_IT_STRIDE seeds,
# split into one slot per kind of match; a slot holds (next slot - slot) /
# SEED_BLOCK blocks. Seeds stay below 2^31 (the engine masks to 31 bits).
SEED_IT_STRIDE = 1_000_000
SEED_BLOCK = 1_000
SEED_SLOT = {"selfplay": 0, "screen": 450_000, "gate": 500_000, "panel": 650_000, "anchor": 800_000,
             "vs_random": 900_000, "vs_reference": 950_000}


def seed_base(it, label, block=0):
    """First seed of batch `block` of kind `label` in iteration `it`."""
    slots = sorted(SEED_SLOT.values()) + [SEED_IT_STRIDE]
    start = SEED_SLOT[label]
    room = (slots[slots.index(start) + 1] - start) // SEED_BLOCK
    if not 0 <= block < room:
        raise ValueError(f"seed plan: block {block} of {label} exceeds its {room} blocks")
    base = 1 + it * SEED_IT_STRIDE + start + block * SEED_BLOCK
    if base + SEED_BLOCK >= 2 ** 31:
        raise ValueError(f"seed plan: iteration {it} runs past 31-bit seeds")
    return base


def deck_pool():
    reg = riftlab.Registry.load()
    pool = []
    for p in sorted((ROOT / "decks").glob("riven_*.txt")) + sorted((ROOT / "lab" / "decks" / "meta").rglob("*.txt")):
        if not riftlab.validate(riftlab.parse_deck(p), reg):
            pool.append(str(p.relative_to(ROOT)))
    return pool


BATCH_TIMEOUT_S = int(os.environ.get("RIFTBOUND_BATCH_TIMEOUT", 20 * 60))


def run_batch(deck1, deck2, a1, a2, games, threads, seed, value_data=None, value_player=0):
    if max(2, games) >= SEED_BLOCK:
        raise ValueError(f"{games} games do not fit one seed block of {SEED_BLOCK}")
    cmd = [str(ENGINE), "--deck1", deck1, "--deck2", deck2, "--agent1", a1, "--agent2", a2,
           "--games", str(max(2, games)), "--threads", str(threads), "--seed", str(seed),
           "--render-html", "off", "--web", "off"]
    if value_data:
        cmd += ["--value-data", str(value_data), "--value-samples", "0"]
        if value_player:
            cmd += ["--value-player", str(value_player)]
    # A batch that runs far longer than its games should (an engine loop in
    # one game: 2026-10-30, Ekko, Revolutionary) is killed and skipped, so one
    # game cannot stall a night of training.
    try:
        out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                             timeout=BATCH_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        print(f"batch timed out after {BATCH_TIMEOUT_S}s, skipped: {' '.join(cmd)}", flush=True)
        return 0, 0
    if out.returncode != 0:
        raise RuntimeError(out.stderr[-1500:])
    w1 = int(re.search(r"P1 wins:\s+(\d+)", out.stdout).group(1))
    w2 = int(re.search(r"P2 wins:\s+(\d+)", out.stdout).group(1))
    return w1, w2


class Loop:
    def __init__(self, a):
        self.a = a
        self.batches = []     # (seed, games, deck1, deck2, agent1, agent2) of every batch run, for the seed tests

    def batch(self, deck1, deck2, a1, a2, games, seed, value_data=None, value_player=0):
        self.batches.append((seed, max(2, games), deck1, deck2, a1, a2))
        return run_batch(deck1, deck2, a1, a2, games, self.a.threads, seed, value_data, value_player)

    def material_bot(self, sims=None):
        """The search with the hand-tuned material count as its evaluation and the
        action-type prior (no net): a league opponent whose judgement owes
        nothing to self-play, so it does not share the nets' Riven bias."""
        settle = f",settle={self.a.settle}" if self.a.settle > 0 else ""
        return f"pmcts:sims={sims or self.a.sims},eval=material,prior=heuristic,worlds={self.a.worlds}{settle}"

    def bot(self, net, sims=None):
        """Agent spec for a network (None = random play). A .rbpn (two-head)
        network is used by pmcts as value head + policy prior; a .rbvn as the
        value head alone."""
        if not net:
            return "random"
        sims = sims or self.a.sims
        policy = str(net).endswith(".rbpn")
        settle = f",settle={self.a.settle}" if self.a.settle > 0 else ""
        if getattr(self.a, "vtemp", 1.0) != 1.0:
            settle += f",vtemp={self.a.vtemp}"
        if self.a.agent == "ismcts" and not policy:
            return f"ismcts:sims={sims},eval=net:{net}{settle}"
        kind = "policy" if policy else "net"
        return f"pmcts:sims={sims},eval={kind}:{net},worlds={self.a.worlds}{settle}"

    @staticmethod
    def key(spec):
        """Elo key of an agent spec: the network (or 'random')."""
        m = re.search(r"eval=(?:net|policy):([^,]+)", spec)
        return f"net:{m.group(1)}" if m else spec

    def match(self, ladder, it, a1, a2, games, seed, label, deck1=GATE_DECK, deck2=GATE_DECK, record=True):
        """`games` games split over both seat orders on the same seeds (the
        same deals, agents swapped): a1 plays deck1 then deck2. Records the
        pairing for Elo."""
        half = max(1, games // 2)
        w1, l1 = self.batch(deck1, deck2, a1, a2, half, seed)
        l2, w2 = self.batch(deck1, deck2, a2, a1, half, seed)
        n = 2 * max(2, half)
        wins = w1 + w2
        losses = l1 + l2
        if record:
            ladder.setdefault("matches", []).append({"iteration": it, "label": label, "a": self.key(a1), "b": self.key(a2),
                                                     "games": n, "wins_a": wins, "wins_b": losses})
        return wins, losses, n

    def panel(self, ladder, it, cand, current):
        """Candidate vs current on every PANEL matchup: each deck on each seat,
        the candidate on both decks. Returns (wins, games, per-matchup rows)."""
        per = max(4, self.a.panel_games // 2)     # games per seat order of the decks
        wins = losses = n = 0
        rows = {}
        for m, (name, d_a, d_b) in enumerate(PANEL):
            w1, l1, g1 = self.match(ladder, it, cand, current, per, seed_base(it, "panel", 2 * m), "panel:" + name,
                                    d_a, d_b, record=False)
            w2, l2, g2 = self.match(ladder, it, cand, current, per, seed_base(it, "panel", 2 * m + 1), "panel:" + name,
                                    d_b, d_a, record=False)
            rows[name] = {"games": g1 + g2, "wins": w1 + w2, "rate": round((w1 + w2) / (g1 + g2), 4)}
            wins += w1 + w2
            losses += l1 + l2
            n += g1 + g2
        ladder.setdefault("matches", []).append({"iteration": it, "label": "panel", "a": self.key(cand),
                                                 "b": self.key(current), "games": n, "wins_a": wins,
                                                 "wins_b": losses})
        return wins, n, rows

    def gate(self, ladder, it, cand, current):
        """Mirror SPRT in blocks, each block on its own seeds. Returns the
        vs_current row."""
        a = self.a
        wins = losses = n = 0
        llr, decision = 0.0, None
        upper, lower = math.log((1 - 0.05) / 0.05), math.log(0.05 / (1 - 0.05))
        blocks = 0
        while n < a.gate_max:
            w, l, g = self.match(ladder, it, self.bot(cand), self.bot(current), a.gate_block,
                                 seed_base(it, "gate", blocks), "gate")
            wins += w; losses += l; n += g; blocks += 1
            llr = sprt_llr(wins, losses)
            if blocks >= 2 and llr >= upper:
                decision = "better"
                break
            if blocks >= 2 and llr <= lower:
                decision = "worse"
                break
            if current is None and n >= a.gate_block:
                break           # the first net replaces random play without a contest
        score = wins / n
        lo, hi = riftlab.wilson(wins, n)
        if decision is None:
            decision = "rate" if n >= a.gate_min else "short"
        return {"opponent": self.bot(current), "games": n, "wins": wins, "rate": round(score, 4),
                "ci95": [round(lo, 4), round(hi, 4)], "llr": round(llr, 3), "sprt": decision}

    def self_play_pairs(self, rng, pool, count):
        """Deck pairs for self-play: half from the panel (the decks the jury and
        the gate use, so the value head knows them), half from the whole pool."""
        focus = [(d_a, d_b) for _, d_a, d_b in PANEL] + [(GATE_DECK, GATE_DECK)]
        out = []
        for k in range(count):
            if k % 2 == 0:
                d1, d2 = rng.choice(focus)
                out.append((d1, d2) if rng.random() < 0.5 else (d2, d1))
            else:
                out.append((rng.choice(pool), rng.choice(pool)))
        return out


def sprt_llr(wins, losses, p0=0.5, p1=0.6):
    """Log-likelihood ratio of H1 (p1) against H0 (p0); draws are ignored."""
    return wins * math.log(p1 / p0) + losses * math.log((1 - p1) / (1 - p0))


def elo_fit(matches, anchor="random", iters=200):
    """Bradley-Terry strengths by minorisation-maximisation (Hunter 2004) over
    all pairings, with half a virtual win each way per pairing so a perfect
    record stays finite. Elo = 400 log10(gamma / gamma_anchor)."""
    players = sorted({m["a"] for m in matches} | {m["b"] for m in matches} | {anchor})
    ix = {p: i for i, p in enumerate(players)}
    k = len(players)
    w = [[0.0] * k for _ in range(k)]     # w[i][j] = wins of i over j
    for m in matches:
        i, j = ix[m["a"]], ix[m["b"]]
        draws = m["games"] - m["wins_a"] - m["wins_b"]
        w[i][j] += m["wins_a"] + 0.5 * draws + 0.5
        w[j][i] += m["wins_b"] + 0.5 * draws + 0.5
    gamma = [1.0] * k
    for _ in range(iters):
        new = []
        for i in range(k):
            wi = sum(w[i])
            denom = sum((w[i][j] + w[j][i]) / (gamma[i] + gamma[j]) for j in range(k) if j != i and (w[i][j] + w[j][i]) > 0)
            new.append(wi / denom if denom > 0 else gamma[i])
        s = new[ix[anchor]] or 1.0
        gamma = [g / s for g in new]
    return {p: round(400 * math.log10(max(gamma[ix[p]], 1e-9)), 1) for p in players}


def load_ladder():
    if LADDER.exists():
        return json.loads(LADDER.read_text())
    return {"description": "Self-play learning ladder: each row is one iteration.", "iterations": []}


def pick_anchors(ladder, n=2):
    """Frozen older nets the candidate is measured against every iteration:
    adopted nets at even spacing through the history (not the reference, which
    has its own yardstick). Chosen once, then kept in ladder["anchor_nets"]."""
    if ladder.get("anchor_nets"):
        return [p for p in ladder["anchor_nets"] if (ROOT / p).exists()]
    adopted = [r["net"] for r in ladder.get("iterations", []) if r.get("accepted") and r.get("net")]
    adopted = [p for p in adopted if p != ladder.get("reference_net") and (ROOT / p).exists()]
    if len(adopted) < n + 1:
        return []
    picks = sorted({adopted[(len(adopted) * (k + 1)) // (n + 1) - 1] for k in range(n)})
    ladder["anchor_nets"] = picks
    return picks


MATERIAL = "material"   # league entry for Loop.material_bot


def league_opponents(ladder, current, n=4):
    """Self-play league: the last `n` adopted nets before the current one, the
    anchor nets, and the material bot. Older nets keep the current one from
    forgetting how to beat play it no longer meets; the material bot judges
    positions by a count instead of a net."""
    adopted = [r["net"] for r in ladder.get("iterations", []) if r.get("accepted") and r.get("net")]
    adopted = [p for p in adopted if p != current and (ROOT / p).exists()]
    out = list(dict.fromkeys(adopted[-n:] + pick_anchors(ladder)))
    return [p for p in out if p != current] + [MATERIAL]


def league_schedule(seed, it, n_pairs, share, league):
    """Per self-play pair: None (current net against itself) or (opponent, seat
    of the current net). Drawn for every pair from one stream so every shard
    agrees; the current net takes seat 1 and seat 2 in turn."""
    rng = random.Random(seed * 7919 + it * 104729 + 1)
    out, flip = [], 0
    for _ in range(n_pairs):
        if league and share > 0 and rng.random() < share:
            out.append((rng.choice(league), 1 + flip))
            flip ^= 1
        else:
            out.append(None)
    return out


def rbpn_hidden(path) -> int:
    """Hidden-layer width of an RBPN file (header: magic, version, input dim, hidden)."""
    import struct
    with open(path, "rb") as f:
        head = f.read(16)
    return struct.unpack("<iii", head[4:16])[2] if head[:4] == b"RBPN" else -1


def train_candidate(a, it, current, row, suffix=""):
    """Trains net_<it> on the replay buffer (oldest first; recency-weighted by
    file, so one file per iteration), fills the training fields of `row` and
    returns the candidate's path."""
    files = [str(DATA / f"iter_{j:03d}.rbvd") for j in range(max(0, it - a.buffer + 1), it + 1)
             if (DATA / f"iter_{j:03d}.rbvd").exists()]
    if a.policy:
        new_net = NETS / f"{NET_PREFIX}{it:03d}{suffix}.rbpn"
        cmd = [sys.executable, str(LEARN / "train_policy.py"), str(new_net), *files,
               "--epochs", str(a.epochs), "--recency", str(a.recency), "--human-weight", str(a.human_weight),
               "--lr", str(a.lr),
               "--q-weight", str(getattr(a, "q_weight", 0.0)),
               "--hidden", str(a.hidden),
               "--entropy", str(a.entropy)]
        legend_human = specialist_human_files(a) if TRACK else None
        if legend_human:
            # Imitation inside every iteration: the specialist's own legend's human decisions weigh
            # --spec-human-weight, every other human decision --human-weight (train_policy --human-weights).
            cmd += ["--human", *legend_human, "--human-weights", str(a.spec_human_weight), str(a.human_weight)]
        elif a.human and Path(a.human).exists():
            cmd += ["--human", a.human]
        # --human-anchor (Jeremy, 8 Nov): the pull goes to a fixed net that replays the human games (replay_imitate.py)
        # instead of the previous net, so the reward of winning improves play without unlearning the human moves.
        anchor = a.human_anchor if getattr(a, "human_anchor", "") and (ROOT / a.human_anchor).exists() else ""
        if anchor and a.prev_weight > 0:
            cmd += ["--prev", str(ROOT / anchor), "--prev-weight", str(a.prev_weight)]
            row["human_anchor"] = anchor
        elif current and str(current).endswith(".rbpn") and a.prev_weight > 0:
            cmd += ["--prev", str(ROOT / current), "--prev-weight", str(a.prev_weight)]
        # Continuous training: start from the current net instead of from scratch, when it has this width.
        if a.warm and current and str(current).endswith(".rbpn") and rbpn_hidden(ROOT / current) <= a.hidden:   # equal, or widened (net2net)
            cmd += ["--init-value", str(ROOT / current)]
            row["warm_start"] = current
    else:
        new_net = NETS / f"{NET_PREFIX}{it:03d}{suffix}.rbvn"
        cmd = [sys.executable, str(LEARN / "train_value.py"), str(new_net), *files,
               "--epochs", str(a.epochs), "--recency", str(a.recency)]
    tr = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    print(tr.stdout[-1200:], flush=True)
    if tr.returncode != 0:
        raise RuntimeError(tr.stderr[-1500:])
    report = json.loads(new_net.with_suffix(".json").read_text())
    row["records"] = report["records"]
    row["games_in_buffer"] = report.get("games")
    row["val_loss"] = report["best_val_loss"]
    row["val_acc"] = report["best_val_acc"]
    row["val_margin_mse"] = report.get("best_val_margin_mse")
    row["baseline_val_loss"] = report["baseline_val_loss"]
    if a.policy:
        row["policy"] = {k: report.get(k) for k in ("search_target_rows", "policy_val_rows", "policy_top1", "policy_top3",
                                                     "policy_ce", "policy_entropy", "target_entropy",
                                                     "human_records", "human_val_rows", "human_top1", "human_top3",
                                                     "human_clean_rows", "human_clean_top1", "human_clean_top3")}
        by_file = report.get("human_by_file") or {}
        if legend_human and legend_human[0] in by_file:
            row["human_legend"] = dict(by_file[legend_human[0]], file=Path(legend_human[0]).name)
    cand = str(new_net.relative_to(ROOT)) if new_net.is_relative_to(ROOT) else str(new_net)
    return cand


def specialist_human_files(a):
    """[human_<legend>.rbv3, human_others_<legend>.rbv3] split from the shared human data for the current
    specialist (lab/tools/split_human_by_legend.py into DATA/human, again whenever the shared data changes), or None when the legend has
    no human decision."""
    src, summary = Path(a.human), LEARN / "human.json"
    if not TRACK or not src.exists() or not summary.exists():
        return None
    out = DATA / "human"
    slug = SPECIALISTS.get(TRACK, {}).get("human_slug", TRACK)
    own, rest = out / f"human_{slug}.rbv3", out / f"human_others_{slug}.rbv3"
    # DATA is cached between runs: split again whenever the shared human data changes (new Play games).
    stamp = out / "source.sha1"
    digest = hashlib.sha1(src.read_bytes() + summary.read_bytes()).hexdigest()
    if not own.exists() or not stamp.exists() or stamp.read_text().strip() != digest:
        r = subprocess.run([sys.executable, str(ROOT / "lab" / "tools" / "split_human_by_legend.py"), "--data", str(src),
                            "--summary", str(summary), "--out-dir", str(out), "--min", "50"], capture_output=True, text=True)
        if r.returncode != 0:     # train on the shared human data rather than fail the iteration
            print(f"human split failed, training without the legend's own human weight:\n{r.stderr[-800:]}", flush=True)
            return None
        stamp.write_text(digest)
    return [str(own), str(rest)] if own.exists() and rest.exists() else None


def make_parser():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iterations", type=int, default=3)
    ap.add_argument("--games", type=int, default=200, help="self-play games per iteration")
    ap.add_argument("--sims", type=int, default=30)
    ap.add_argument("--selfplay-sims", type=int, default=0,
                    help="simulations per move in the self-play games only (0 = --sims); the gates keep --sims")
    ap.add_argument("--agent", choices=["pmcts", "ismcts"], default="pmcts")
    ap.add_argument("--worlds", type=int, default=8, help="pmcts determinizations per decision")
    ap.add_argument("--max-minutes", type=float, default=0, help="stop before an iteration that would end past this many minutes (0 = no limit)")
    ap.add_argument("--settle", type=int, default=0, help="play a leaf in a chain, showdown or combat on for up to N decisions before evaluating it (0 = off)")
    ap.add_argument("--gate-min", type=int, default=200, help="games before the plain win rate may decide")
    ap.add_argument("--gate-max", type=int, default=400)
    ap.add_argument("--gate-block", type=int, default=40, help="games per gate block (half per seat order)")
    ap.add_argument("--accept", type=float, default=0.55)
    ap.add_argument("--yardstick-games", type=int, default=60, help="games vs random and vs the reference net")
    ap.add_argument("--panel-games", type=int, default=16, help="games per PANEL matchup (both decks, both seats)")
    ap.add_argument("--panel-floor", type=float, default=0.45, help="refuse a candidate scoring below this on the panel")
    ap.add_argument("--anchor-games", type=int, default=20, help="games vs each frozen older net (ladder anchor_nets)")
    ap.add_argument("--buffer", type=int, default=6, help="iterations of data in the replay buffer")
    ap.add_argument("--recency", type=float, default=0.7, help="weight multiplier per iteration of age")
    ap.add_argument("--epochs", type=int, default=8)
    ap.add_argument("--lr", type=float, default=1e-3, help="--policy: Adam learning rate")
    ap.add_argument("--specialist", default="", help="stages.py: train the specialist net of this legend "
                    f"({', '.join(SPECIALISTS)}) on its own ladder instead of the general net")
    ap.add_argument("--spec-jury-games", type=int, default=48,
                    help="--specialist: games per jury task for the candidate and again for the current specialist "
                         "(one task per jury deck and legend deck); the jury decides adoption")
    ap.add_argument("--spec-jury-accept", type=float, default=0.03,
                    help="--specialist: adopted when it beats the current specialist on the jury by this much")
    ap.add_argument("--spec-mirror-games", type=int, default=120,
                    help="--specialist: games of the legend mirror against the current specialist (a guard)")
    ap.add_argument("--spec-mirror-floor", type=float, default=0.45,
                    help="--specialist: the candidate may not win less than this in the mirror")
    ap.add_argument("--spec-human-weight", type=float, default=3.0,
                    help="--specialist: weight of its own legend's human decisions in training (the others keep --human-weight)")
    ap.add_argument("--spec-human-margin", type=float, default=0.02,
                    help="--specialist: a candidate may agree with its legend's held-out human moves at most this much "
                         "less than the current specialist did (checked when 200+ held-out decisions)")
    ap.add_argument("--spec-panel-margin", type=float, default=0.03,
                    help="--specialist: the candidate may trail the current specialist against the jury by this much")
    ap.add_argument("--spec-riven-weight", type=float, default=1.0,
                    help="specialist self-play: Riven jury decks are drawn this many times as often as the others")
    ap.add_argument("--spec-mirror", type=float, default=0.2,
                    help="--specialist: share of self-play pairs played in the legend's mirror (both seats recorded)")
    ap.add_argument("--league", type=float, default=0.0,
                    help="stages.py (parallel training): share of self-play pairs played against a league opponent (older adopted nets, the anchors, "
                         "the material bot); only the current net's decisions are recorded in those")
    ap.add_argument("--league-nets", type=int, default=4, help="--league: adopted nets taken from the end of the ladder")
    ap.add_argument("--q-weight", type=float, default=0.0,
                    help="--policy: mix the search's value of each position into the value target (train_policy.py --q-weight)")
    ap.add_argument("--hidden", type=int, default=64, help="--policy: width of the net's hidden layer")
    ap.add_argument("--warm", action="store_true", help="--policy: start each candidate from the current net (same width) instead of from scratch")
    ap.add_argument("--vtemp", type=float, default=1.0, help="value temperature of the search (pmcts vtemp=; 1 = off)")
    ap.add_argument("--policy", action="store_true",
                    help="train a two-head policy + value net (train_policy.py) and search with it as the prior")
    ap.add_argument("--human", default=str(ROOT / "data" / "learn" / "human.rbv3"),
                    help="human imitation set for --policy (lab/tools/build_human_dataset.py); '' for none")
    ap.add_argument("--human-weight", type=float, default=0.3)
    ap.add_argument("--entropy", type=float, default=0.01, help="--policy: entropy bonus")
    ap.add_argument("--prev-weight", type=float, default=0.1, help="--policy: pull toward the previous net's policy")
    ap.add_argument("--human-anchor", default="", help="--policy: pull toward this fixed human-replay net instead of the previous net")
    ap.add_argument("--threads", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--seed", type=int, default=1)
    return ap


def main(argv):
    ap = make_parser()
    a = ap.parse_args(argv)
    DATA.mkdir(parents=True, exist_ok=True)
    NETS.mkdir(parents=True, exist_ok=True)
    ladder = load_ladder()
    pool = deck_pool()
    rng = random.Random(a.seed + len(ladder["iterations"]))
    L = Loop(a)
    print(f"deck pool: {len(pool)} decks; agent {a.agent}", flush=True)

    current = ladder.get("current_net")
    if current and not (ROOT / current).exists():
        current = None
    reference = ladder.get("reference_net")
    if reference and not (ROOT / reference).exists():
        reference = None
    _OUT.mkdir(parents=True, exist_ok=True)
    t_start, last_minutes = time.time(), 0.0
    for _ in range(a.iterations):
        # A time budget (a CI job has a hard limit): stop before an iteration that would not finish in it.
        if a.max_minutes and (time.time() - t_start) / 60 + 1.15 * last_minutes > a.max_minutes:
            print(f"time budget: {a.max_minutes} min reached, stopping before the next iteration", flush=True)
            break
        it = len(ladder["iterations"])
        t0 = time.time()
        row = {"iteration": it, "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
               "player": L.bot(current), "self_play_games": a.games, "sims": a.sims, "agent": a.agent,
               "trainer": "train_policy" if a.policy else "train_value"}
        data_file = DATA / f"iter_{it:03d}.rbvd"
        if data_file.exists():
            data_file.unlink()
        # 1. self-play over random deck pairs, a few games per pair
        per_pair = 4
        games = a.games * 3 if current is None else a.games   # random play is cheap
        for k, (d1, d2) in enumerate(L.self_play_pairs(rng, pool, max(1, games // per_pair))):
            L.batch(d1, d2, L.bot(current), L.bot(current), per_pair, seed_base(it, "selfplay", k), data_file)
        row["self_play_minutes"] = round((time.time() - t0) / 60, 1)
        # 2. train on the replay buffer (oldest first; train_value.py weights by recency)
        cand = train_candidate(a, it, current, row)
        # 3. gate: candidate vs current, Riven mirror, seats swapped, SPRT in blocks; then the panel guard
        row["vs_current"] = L.gate(ladder, it, cand, current)
        decision, score = row["vs_current"]["sprt"], row["vs_current"]["rate"]
        mirror_ok = current is None or decision == "better" or (decision == "rate" and score >= a.accept)
        panel_ok = True
        if current is not None and mirror_ok and a.panel_games > 0:
            p_w, p_n, p_rows = L.panel(ladder, it, L.bot(cand), L.bot(current))
            p_lo, p_hi = riftlab.wilson(p_w, p_n)
            panel_ok = p_w / p_n >= a.panel_floor
            row["panel"] = {"games": p_n, "wins": p_w, "rate": round(p_w / p_n, 4),
                            "ci95": [round(p_lo, 4), round(p_hi, 4)], "floor": a.panel_floor,
                            "ok": panel_ok, "matchups": p_rows}
        # 4. yardsticks: random, the fixed reference net and the frozen older nets
        r_w, _, r_n = L.match(ladder, it, L.bot(cand), "random", a.yardstick_games, seed_base(it, "vs_random"), "vs_random")
        row["vs_random"] = {"games": r_n, "wins": r_w, "rate": round(r_w / r_n, 4)}
        if reference and reference != cand:
            f_w, _, f_n = L.match(ladder, it, L.bot(cand), L.bot(reference), a.yardstick_games,
                                  seed_base(it, "vs_reference"), "vs_reference")
            row["vs_reference"] = {"opponent": L.bot(reference), "games": f_n, "wins": f_w, "rate": round(f_w / f_n, 4)}
        if a.anchor_games > 0:
            anchors = {}
            for j, anc in enumerate(x for x in pick_anchors(ladder) if x != cand):
                g_w, _, g_n = L.match(ladder, it, L.bot(cand), L.bot(anc), a.anchor_games,
                                      seed_base(it, "anchor", j), "vs_anchor")
                anchors[anc] = {"games": g_n, "wins": g_w, "rate": round(g_w / g_n, 4)}
            if anchors:
                row["vs_anchors"] = anchors
        accepted = mirror_ok and panel_ok
        row["net"] = cand
        row["accepted"] = accepted
        if accepted:
            current = cand
            ladder["current_net"] = cand
        if reference is None:
            reference = cand
            ladder["reference_net"] = cand
        elo = elo_fit(ladder.get("matches", []))
        ladder["elo"] = elo
        row["elo"] = elo.get(f"net:{cand}")
        row["elo_current"] = elo.get(f"net:{current}") if current else 0.0
        row["minutes"] = round((time.time() - t0) / 60, 1)
        last_minutes = row["minutes"]
        ladder["iterations"].append(row)
        LADDER.write_text(json.dumps(ladder, indent=1))
        print(json.dumps(row), flush=True)


if __name__ == "__main__":
    main(sys.argv[1:])
