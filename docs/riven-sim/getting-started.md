# Running the Riven lab on your own computer

Nothing in the lab *needs* your computer: the dashboard is published, the
engine runs inside it for the Play tab, and heavy simulations run on GitHub
Actions (drop a spec into `lab/requests/` and push). This page is for when you
want the whole thing locally, e.g. to work on it with Claude Code or to run long
searches on your own CPU.

## The easy way: let Claude Code do it

1. Install the Claude desktop app (or Claude Code in a terminal).
2. Start a session **on your computer** and tell it:
   > Clone https://github.com/jdisser02-glitch/alpharune, branch
   > `claude/zen-goodall-hb2ssu`, install the build dependencies for this
   > machine, build it, run the tests, then run `python3 lab/riftlab.py run
   > lab/specs/smoke_random.json` to check that simulations work. Read
   > `lab/README.md` and `docs/riven-sim/getting-started.md` first.
3. From then on you can ask that session for new deck tests, list searches or
   human-game reviews, and it will commit the results to the same branch so
   the dashboard can be rebuilt from them.

## By hand

**Dependencies.** macOS: `brew install cmake ninja boost nlohmann-json python
node`. Windows: use WSL2 (Ubuntu) and then, as on Linux:
`sudo apt install cmake ninja-build g++ libboost-all-dev nlohmann-json3-dev python3 nodejs`.
The first build downloads OpenSpiel and takes 10–20 minutes.

```bash
git clone https://github.com/jdisser02-glitch/alpharune
cd alpharune && git checkout claude/zen-goodall-hb2ssu
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
RIFTBOUND_ROOT=. ./build/riftbound_tests        # ~1900 tests
```

**What lives where**

| Want to… | Run |
|---|---|
| compare deck lists | `python3 lab/riftlab.py run lab/specs/<test>.json` (see `lab/README.md`) |
| search one-card swaps | `python3 lab/optimize.py lab/specs/optimize/riven_flex_1.json` |
| train the bot | `python3 lab/learn/loop.py --iterations 3` (hours; uses all cores) |
| re-import RiftLite human games | `python3 lab/tools/import_riftlite.py --all` (needs `npm i playwright` + Chromium) |
| compare the bot with the humans | `python3 lab/tools/human_review.py` |
| rebuild the dashboard | `python3 lab/dashboard/build.py` → `lab/dashboard/dist/` (serve it with `python3 -m http.server`) |
| play against the bot locally | `./build/riftbound --deck1 decks/riven_sacramento.txt --deck2 <deck> --agent1 human --agent2 "ismcts:sims=100,eval=net:lab/learn/nets/net_004.rbvn"` then open http://127.0.0.1:8080 |
| the browser engine (Play tab) | needs Emscripten: `emcmake cmake -B build-wasm -DRIFTBOUND_WASM=ON && cmake --build build-wasm --target riftbound_wasm` |

Results are JSON files under `lab/results/`, `lab/learn/ladder.json`,
`lab/human_replays/`; everything the dashboard shows comes from them.

## Into a notes vault ("second brain")

The lab's outputs are plain JSON and Markdown. `docs/riven-sim/*.md` are the
reports; `lab/human_games/README.md` and `lab/README.md` explain the data. To
keep a vault in sync, copy `docs/riven-sim/` and the Markdown reports you want
into it, or ask Claude to export a Markdown summary of `lab/results/*.json`
(decks, win rates with intervals, matchups) into your vault's folder.
