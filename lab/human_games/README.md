# Human games (RiftLite public replays)

Real 1v1 Riftbound games played by humans on the Atlas client and uploaded to
[RiftLite](https://www.riftlite.com/replays) with **Public visibility**, saved
here in a normalised form so the bot's play can be compared with human play
(move choices, tempo, mulligans, scoring patterns).

One file per replay: `<date>_<legendA>_vs_<legendB>_<rl2_id>.json`.
`raw/` keeps the unprocessed scraper output (and `raw/list.json`, the public
list) so the parser can be re-run offline with `--reparse`.

## What a file holds

| field | meaning |
|---|---|
| `source_url`, `id`, `date`, `client` | the public replay page, its id, the date and client shown on the list |
| `players`, `capture_player`, `opponent` | display names; the first player is the one whose client recorded the replay |
| `legends` | per player: the legend card and the champion card (name + card code) shown in the header |
| `format`, `result` | `BO1` / `BO3`, and Win / Loss / Draw / "Result pending" **as shown on the public list, from the capture player's point of view** |
| `decks` | `available` is false for every public replay: RiftLite shows "Match decks" only to the owner. The hands revealed at the end of a game (`kind: revealed_hand`) are the public trace of the lists |
| `games` | one entry per game inside the replay (a BO3 replay can hold two games): event index range, who took the first turn, turns ended per player, the score reconstructed from the log, the points on the final frame, and entries the players "rewound" (retracted) |
| `events` | the Match log in chronological order: `{game, t, seconds, text, actor, kind, ...}` |
| `kind_counts`, `event_count`, `parsed_count` | parser coverage per file |

Every event keeps the raw `text`. The parsed fields depend on the kind:
`card`, `from`, `to`, `battlefield`, `amount`, `cost`/`energy`/`power`/`detail`
(payments), `cards` (revealed or hidden hands), `recycled`/`redrawn`
(mulligan), `roll`, `first`, `phase`. Lines the parser does not recognise keep
`kind: "other"`. The actor of actor-less lines ("Played X from hand.",
"Marked Repeat for X.") is the capture player; those carry
`actor_inferred: true`.

Event kinds, in RiftLite's own words: `played` (from hand / trash / token,
with the destination), `moved` (to a battlefield = attack or move, to base,
to hand = bounce), `killed` (moved from the board to trash), `discarded`,
`exhausted`, `readied`, `recycled`, `channeled`, `paid` ("4E — Exhaust 4",
"5E / 1P — Float 1E · Exhaust 4 · Recycle 1 Calm rune"), `conquered`, `held`,
`scored` (turn-start points), `showdown` (responses ended), `passed_focus`,
`chain_resolved`, `drew`, `looked`, `revealed`, `revealed_hand`, `hid_hand`,
`hidden_card_placed`, `equipped`, `made_unit`, `buff_applied`,
`buff_consumed`, `token_used`, `ended_turn`, `ended_phase`, `mulligan`,
`sideboarding`, `battlefield_locked`, `roll`, `initiative`, `first_turn`,
`rewound`.

## Limits

- **Hidden information stays hidden.** Hands and decks are not visible except
  through plays, draws ("drew 1 card"), reveals and the end-of-game hand
  reveals. Decklists are not published.
- **One client's view.** Each replay is captured by one player's client; a few
  lines are phrased from that player's side (actor-less "Played X from hand.").
- **No damage or combat-assignment lines.** RiftLite logs attacks as unit
  moves to a battlefield, deaths as moves to trash and the showdown steps; it
  does not log damage numbers.
- **Timestamps are the replay clock** (mm:ss from the start of the capture,
  one-second resolution, continuing across games of a BO3). The first line of
  some replays shows a bogus clock value; `seconds` is `null` there.
- **The log panel is a 100-line window per game**, so the importer sweeps the
  replay with the progress slider and stitches the windows together. Lines a
  player rewound disappear from the site's log; they are kept in
  `games[].retracted_entries`. `sweep.gaps` > 0 would mean a stretch where a
  read could not be aligned (a `[capture gap ...]` marker is left in `events`).
- **Repeated lines.** A line identical to the previous one carries
  `repeat_of_previous: true`. Usually that is genuine (two runes exhausted in
  the same second, four draws in a row); the derived per-game score and turn
  counts count each run once. A few uploads (several Ziggs games) also
  re-emit one or two old lines on every seek of the player, which makes the
  site's panel grow copies of those lines and push real old lines out of
  view. The importer aligns each panel read against what it already holds
  (longest common subsequence), drops copies of lines it already has and keeps
  each run's first observed count; `sweep.repeat_growth` and
  `sweep.dropped_copies` > 0 mark the captures where this happened.
- **A game inside a replay can have an empty or tiny log** when the uploader's
  client did not record it (for example the first game of a BO3 whose other
  game was uploaded as a separate replay). Such stubs are kept as they are.
- **Game ends.** The panel is blanked by the game-over screen, so the last
  lines of a game are recovered by stepping back from the blank frame
  (`sweep.tail_reads`). A game that ends at 7 points with no final scoring
  line is normally a concession.
- "Partial capture" replays (missing the opening mulligan, a handful of
  messages) are skipped unless `--include-partial` is given.
- The result on BO3 replays is the match result as listed, not per game; use
  `games[].score_from_log` / `score_final_frame` for the games themselves.

## Contents

<!-- summary -->
45 public replays imported (73 games, 14645 log lines, 100.0% parsed into a kind other than `other`). Legends seen (as capture player or opponent): Kai'Sa (15), Ziggs (14), Evelynn (9), Orianna (5), Riven (5), Pyke (5), K'Sante (4), Mordekaiser (4), Vex (3), Ekko (3), Kennen (2), Fiora (2), Lillia (2), Nasus (2), Viktor (2), Volibear (2), Diana (2), LeBlanc (2), Jinx (1), Rumble (1), Ornn (1), Rek'Sai (1), Draven (1), Renata Glasc (1), Rengar (1).

| date | players | legends | format | result | games | events |
|---|---|---|---|---|---|---|
| 2026-10-27 | Courage vs bob | Vex vs Jinx | BO1 | Result pending | 2 | 46 |
| 2026-10-28 | Damus vs Lutios | K'Sante vs Kennen | BO1 | Win | 1 | 58 |
| 2026-10-28 | Damus vs Kong | K'Sante vs Orianna | BO1 | Loss | 1 | 183 |
| 2026-10-28 | Damus vs DD | K'Sante vs Rumble | BO1 | Loss | 1 | 137 |
| 2026-10-28 | Emp4 vs Commi | Kai'Sa vs Evelynn | BO3 | Win | 3 | 467 |
| 2026-10-28 | Emp4 vs Billie | Kai'Sa vs Fiora | BO3 | Loss | 3 | 580 |
| 2026-10-28 | Emp4 vs lll | Kai'Sa vs Kai'Sa | BO3 | Win | 3 | 568 |
| 2026-10-28 | Emp4 vs bertilQ | Kai'Sa vs Kennen | BO3 | Win | 2 | 562 |
| 2026-10-28 | Emp4 vs Milan | Kai'Sa vs Lillia | BO3 | Loss | 2 | 534 |
| 2026-10-28 | Emp4 vs Dantelush | Kai'Sa vs Nasus | BO3 | Draw | 3 | 839 |
| 2026-10-28 | Emp4 vs El Contador | Kai'Sa vs Ornn | BO3 | Win | 2 | 481 |
| 2026-10-28 | Emp4 vs W | Kai'Sa vs Viktor | BO3 | Loss | 3 | 860 |
| 2026-10-28 | Emp4 vs ponti | Kai'Sa vs Volibear | BO3 | Draw | 2 | 144 |
| 2026-10-28 | gHQCondor vs Receipt | Mordekaiser vs Orianna | BO1 | Result pending | 1 | 163 |
| 2026-10-28 | gHQCondor vs Reksai is Queen | Mordekaiser vs Rek'Sai | BO1 | Win | 1 | 259 |
| 2026-10-28 | gHQCondor vs Omi | Mordekaiser vs Vex | BO1 | Win | 1 | 4 |
| 2026-10-28 | ComboMAN vs Sziszu | Nasus vs Riven | BO3 | Loss | 3 | 541 |
| 2026-10-28 | Damus vs MonzyEXL | Ziggs vs Draven | BO1 | Loss | 1 | 209 |
| 2026-10-28 | Damus vs Linvail | Ziggs vs Ekko | BO1 | Loss | 1 | 214 |
| 2026-10-28 | Damus vs Silver Lining | Ziggs vs Evelynn | BO1 | Loss | 1 | 234 |
| 2026-10-28 | Damus vs hiphiphorhey | Ziggs vs Evelynn | BO1 | Loss | 1 | 160 |
| 2026-10-28 | tom vs Haku | Ziggs vs Fiora | BO1 | Win | 1 | 264 |
| 2026-10-28 | Damus vs Frecher Affe | Ziggs vs K'Sante | BO3 | Win | 3 | 576 |
| 2026-10-28 | tom vs Kay | Ziggs vs Kai'Sa | BO1 | Loss | 1 | 418 |
| 2026-10-28 | tom vs Kuzong | Ziggs vs Lillia | BO3 | Win | 2 | 530 |
| 2026-10-28 | Damus vs Truce baldazzi | Ziggs vs Mordekaiser | BO1 | Loss | 1 | 199 |
| 2026-10-28 | Damus vs CB | Ziggs vs Renata Glasc | BO1 | Loss | 1 | 104 |
| 2026-10-28 | Damus vs player#6969 | Ziggs vs Rengar | BO1 | Loss | 1 | 254 |
| 2026-10-28 | Damus vs Karni | Ziggs vs Volibear | BO1 | Win | 1 | 242 |
| 2026-10-28 | Damus vs Fazhou | Ziggs vs Ziggs | BO1 | Win | 1 | 355 |
| 2026-10-29 | Courage vs light | Diana vs Riven | BO1 | Loss | 1 | 278 |
| 2026-10-29 | Courage vs Bigfalafel | Diana vs Riven | BO1 | Win | 1 | 248 |
| 2026-10-29 | SergeantTV vs PLAY FAST | Evelynn vs Kai'Sa | BO3 | Loss | 1 | 189 |
| 2026-10-29 | SergeantTV vs PLAY FAST | Evelynn vs Kai'Sa | BO3 | Loss | 2 | 175 |
| 2026-10-29 | ComboMAN vs 3rd time playing riven | Kai'Sa vs Vex | BO3 | Result pending | 3 | 597 |
| 2026-10-29 | c vs Hob | LeBlanc vs Evelynn | BO3 | Win | 3 | 648 |
| 2026-10-29 | c vs Hob | LeBlanc vs Evelynn | BO3 | Win | 2 | 412 |
| 2026-10-29 | c vs Hob | Orianna vs Evelynn | BO3 | Result pending | 1 | 139 |
| 2026-10-29 | c vs Hob | Orianna vs Evelynn | BO3 | Result pending | 2 | 406 |
| 2026-10-29 | c vs Last | Orianna vs Riven | BO3 | Loss | 2 | 389 |
| 2026-10-29 | Tab vs MissSophia | Pyke vs Ekko | BO1 | Win | 1 | 157 |
| 2026-10-29 | Tab vs Nokt | Pyke vs Ekko | BO1 | Loss | 1 | 303 |
| 2026-10-29 | Tab vs BlackWell | Pyke vs Kai'Sa | BO1 | Loss | 1 | 136 |
| 2026-10-29 | Tab vs I hate herons | Pyke vs Riven | BO1 | Loss | 1 | 251 |
| 2026-10-29 | Tab vs etrnal | Pyke vs Viktor | BO1 | Loss | 1 | 132 |

Files to read with care (the site's log for these captures repeats lines or the window stitching reported gaps; see `sweep` and `games[].repeated_lines` in the file):

- `2026-10-28_nasus_vs_riven_rl2_e7f69092962ca374a07e2c2773b84939.json` — gaps=1
- `2026-10-28_ziggs_vs_draven_rl2_26a35fc3952315695ae001770b9e645c.json` — repeat_growth=13 dropped_copies=395
- `2026-10-28_ziggs_vs_ekko_rl2_ca242e9cb0aa35b5a7babcdf5e20100c.json` — repeat_growth=19 dropped_copies=28
- `2026-10-28_ziggs_vs_evelynn_rl2_0481ac0c70d94c285c071c95b03b44ed.json` — repeat_growth=12 dropped_copies=13
- `2026-10-28_ziggs_vs_rengar_rl2_c4d96616413b4a3233921bd8b8d5cf81.json` — repeat_growth=28 dropped_copies=4
- `2026-10-28_ziggs_vs_volibear_rl2_67c89a12e1ed120b90faa140761e8650.json` — repeat_growth=55 dropped_copies=80
- `2026-10-28_ziggs_vs_ziggs_rl2_6f857168c70e64797e68f5fabd16f689.json` — repeat_growth=27 dropped_copies=799

## Refresh

```bash
python3 lab/tools/import_riftlite.py --list          # what is public right now
python3 lab/tools/import_riftlite.py --all           # import new replays (cached ones are skipped)
python3 lab/tools/import_riftlite.py --id rl2_...    # one replay
python3 lab/tools/import_riftlite.py --reparse       # re-run the parser on raw/ after changing the rules
```

`lab/tools/riftlite_scrape.js` (Node + Playwright) opens the public pages one
at a time with a pause between them and reads only what a visitor sees: the
replay list, the header, the Match log panel and the Match decks panel. It
probes no API endpoints and needs no account. A 30-minute replay takes about
1.5 minutes to sweep (`--step` sets the slider step in seconds).
