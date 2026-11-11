# Card domain audit against the printed cards (11 Nov 2026)

The official gallery metadata (`cards/raw/gallery_raw.json`, playriftbound.com) carries wrong
domains for a number of Radiance cards, and the card classes were authored from it. After The
Good Stuff (RAD-106) and Blossoming Dreamroot (RAD-024) were caught by real tournament lists,
every unit, spell and gear card was compared with the domain printed on its image.

**Result: 22 more Radiance cards had a wrong or missing domain. All are fixed. No card of
Origins, Proving Grounds, Spiritforged, Unleashed or Vendetta disagrees with its image.**

The full list of cards checked is `docs/card-domain-audit-2026-11.csv` (982 rows).

## Method

`scripts/check_card_domains.py` reads every `src/cards/{units,spells,gear}/*.cpp`, downloads the
image named by `d.image_url`, and classifies the hue of the domain symbol in the bottom-right
corner (two symbols on signature cards) and of the name banner. Runes, battlefields and legends
are not checked.

| | cards |
|---|---|
| unit, spell and gear cards in the engine | 982 |
| image read, symbol colour equals `d.domains` | 955 before the fix, 977 after |
| image read, symbol and banner agree on another domain | 22 before, 0 after |
| ambiguous reading, checked by eye | 4 (BANNED stamp over the symbol; banner agrees) |
| no official image | 1 (Shadow Clone token) |

122 cards were also checked by eye on the image itself, among them the 22 mismatches, the 4
ambiguous readings, 69 alt-art and signature cards a first, looser classifier could not
read, the 22 cards whose image was missing from the repo's gallery copy, and Neeko, Blending
In (printed neutral).

The Radiance images also confirm each other: read in collector order they form six unbroken
blocks of 23, Fury 001-023, Calm 024-046, Mind 047-069, Body 070-092, Chaos 093-115,
Order 116-138. Every fix below puts its card back in its block.

## Fixed

| Card | Code | Was | Printed |
|---|---|---|---|
| Amateur Demolitionist | RAD-001 | Chaos | Fury |
| Brawl | RAD-004 | Chaos | Fury |
| Scorched Earth | RAD-006 | none | Fury |
| Antagonistic Apparatus | RAD-009 | none | Fury |
| Runeblade Anvil | RAD-012 | Chaos | Fury |
| Coalesced Energy | RAD-016 | Mind | Fury |
| Detonation Amplifier | RAD-017 | Chaos | Fury |
| Entrenched Boar | RAD-026 | none | Calm |
| Hush | RAD-041 | none | Calm |
| Fanfare | RAD-047 | none | Mind |
| Industrious Mechsmith | RAD-049 | none | Mind |
| Supernova | RAD-059 | Order | Mind |
| Gravity Field | RAD-062 | Calm | Mind |
| Clocktower Guardian | RAD-067 | Fury | Mind |
| Nazuman Ntofo | RAD-071 | Order | Body |
| Pillaged Armory | RAD-073 | Calm | Body |
| Elnuk Herd | RAD-081 | Fury | Body |
| Disposal Expert | RAD-093 | Fury | Chaos |
| Demon Shade | RAD-102 | Fury | Chaos |
| Bushwhack Trap | RAD-108 | Fury | Chaos |
| Mechanical Bandit | RAD-111 | Fury | Chaos |
| One-Two Punch | RAD-121 | none | Order |

A card with no domain pays its Power with any rune, so the seven "none" cards were also
playable in any deck and paid too easily: Hush costs [1] and two Calm, Scorched Earth and
Antagonistic Apparatus one Fury. Runeblade Anvil's "[2] + rune, exhaust: Kill this" prints a
Fury rune; its `power_domain` follows.

The live gallery (fetched 11 Nov) still carries the wrong values for all of these, and for
The Good Stuff, Blossoming Dreamroot and Bilgewater Tracker.

## Images added

22 Radiance cards had no `d.image_url` because the repo's gallery copy predates them. The live
gallery has them now; the URL was added to each card class and all 22 agree with their
declared domain: RAD-010, 011, 022, 031, 032, 042, 044, 053, 054, 065, 066, 078, 080, 092,
094, 103, 104, 113, 123, 124, 134, 135. The gallery's own domain field is wrong or empty for
five of them (Burn Away, Pitiless Stalker, Protector's Shield, Raucous Anthem, Whirling
Twirligig), so the engine was already right where the gallery is not.

## Decks

`python3 lab/riftlab.py validate` over the 129 lists under `lab/decks/` and `decks/`: all OK
before and after.

`validate` does not read the sideboards kept in the lists' comment headers. Checked
separately (2,413 main-deck and 708 sideboard entries against the legend's domains):

- before: two sideboards were outside their legend's domains, 2 Disposal Expert in X's Vex
  (Calm/Chaos, Fuzhou) and 1 Elnuk Herd in Dunc's Evelynn (Body/Chaos, Vegas);
- after: none. The real lists agree with every fix they touch: 38 lists play or sideboard
  Hush, all under a Calm legend; Entrenched Boar is in four Riven lists, Elnuk Herd in two
  Rengar lists and one Evelynn sideboard, Scorched Earth in a Volibear list, Antagonistic
  Apparatus in an Ekko list, Amateur Demolitionist and Runeblade Anvil in a Ziggs list.

## What the wrong data had let through

548 generated lists in `lab/variants/` (families `jeremy-owned`, `jeremy-buy`, `riven-field`,
`riven-stuttgart`) are Riven (Calm/Chaos) lists that play one or more of Pillaged Armory,
Amateur Demolitionist, Scorched Earth, Antagonistic Apparatus, Industrious Mechsmith, Fanfare,
Brawl, One-Two Punch, Runeblade Anvil. None of these nine is legal with Riven. `lab/index.json`
now records the error on each of them. The lists themselves and their results are left as
they were; the searches that produced them (`lab/specs/ga/jeremy-*.json`, `riven-field-*.json`,
`riven-stuttgart-*.json`) name these cards in their pools.

`lab/learn/loop.py` trained the Riven specialists on one of them
(`riven-field-5--001ea89136`, 1 Industrious Mechsmith); it was removed from both deck lists.
All 106 remaining specialist deck and jury entries validate. `decks/jeremy/*.txt` and
`decks/riven_best_field.txt` are legal.

## Re-running

```bash
python3 scripts/check_card_domains.py --csv /tmp/domains.csv
```

Needs Pillow and numpy; images are cached in `build/card_images`. Exit status 1 on a mismatch.
