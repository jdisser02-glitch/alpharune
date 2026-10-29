# Stage 1 — build and baseline (2026-10-29)

Upstream base: `b05e51d` (chorlick/alpharune, 2026-10-10). No source change was
needed to build; this file is the only addition of stage 1.

## Environment

- Ubuntu 24.04 cloud container, 4 vCPU Intel Xeon @ 2.10 GHz
- g++ 13, CMake, Ninja 1.11.1, Boost 1.83 (`libboost-all-dev`), `nlohmann-json3-dev`, ccache
- `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` (+ ccache launcher)

## Build

- Configure 22.6 s (clones OpenSpiel v1.6.14, abseil, json, pybind11_json, DDS).
- `cmake --build build -j4`: 1029 steps, wall 15 min 21 s, exit 0, no error.
  9 compiler warnings, all in fetched dependencies or pre-existing.

## Unit tests

`RIFTBOUND_ROOT=. ./build/riftbound_tests`

- 1495 tests from 129 suites ran, **1495 passed, 0 failed**, 1 disabled
  (`DISABLED_BaronNashor_AuraGivesFriendlyUnitsPlus2M`, disabled upstream).
- The README's "855 unit tests" is out of date; the suite has grown since.
- Wall time 2.9 s.

## Throughput, random vs random

Deck pair: `decks/miss_fortune_test.txt` (P1) vs `decks/kaisa_test.txt` (P2,
Viktor legend despite the file name), seed base 42.

| Games | Threads | Wall (process) | Reported rate | Games/s/thread | P1–P2 |
| ---: | ---: | ---: | ---: | ---: | --- |
| 100 | 1 | 0.43 s | 236.9 games/s | ~237 | 62–38 |
| 100 | 4 | 0.13 s | 834.9 games/s | ~209 | 62–38 |
| 1000 | 4 | 1.03 s | 983.3 games/s | ~246 | 588–412 |

Average game: 18 turns, ~300 agent decisions. Process wall time includes
loading the 955 card classes (~0.1 s).
