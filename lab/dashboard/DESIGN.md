# Riven Lab — design system and writing guide

The page is a card table: dark navy felt, gold chrome, parchment ink, and the real card
illustrations wherever a card is named. Every screen is built from the components in
`app.css` and the helpers in `app/lib.js`. This file is the contract the screens follow.

```
lab/dashboard/
  index.html        shell: header, nav, <main id="view">, card-preview layer, footer
  app.css           tokens + components (this document)
  board.css/js      RiftBoard, the table renderer — untouched, has its own tokens (--rb-*)
  engine.worker.js  WebAssembly engine worker (Play)
  home.yml          editable copy for Home (→ dist/home.json)
  app/main.js       hash router + view lifecycle
  app/lib.js        data, formatting, cards, imagery, stats, components
  app/<view>.js     one module per screen: home tests decks replays play bot launch cards
  app/<view>.css    that screen's own classes (prefix .v-<view>), loaded by the module with cssOnce()
  build.py          assembles dist/ (data.json, replays, engine, home.json, app/)
```

## 1. Module contract

Every screen is `app/<view>.js` and exports:

```js
export async function render(container, params, ctx)  // build the screen inside container
export function destroy()                              // optional: stop timers/workers/listeners
export function update(container, params, ctx)        // optional: same view, new params (else destroy+render)
```

- `params`: the path segment after the view under its name (`tests → id`, `decks → name`,
  `replays → file`) and as `rest`, plus any `?query` keys. Encode path params with
  `encodeURIComponent` (`#/replays/${encodeURIComponent(file)}`).
- `ctx = { data, params, route, navigate }`; `data` is the parsed `data.json`.
- Modules are lazy-imported on first use and cached; `render` may be called many times.
- All URLs are relative (`data.json`, `replays/x.json`, `engine.worker.js`): the page is served
  from the root of a static host, never assume a base path.
- Keep screen state in module scope if it should survive navigation (a game in progress does).
- Render by building an HTML string with the helpers and setting `container.innerHTML`, then
  wire events and call `enhance(container)` (sortable tables). No framework, no build step.

Routes: `#/home` (default) `#/tests` `#/tests/<id>` `#/decks` `#/decks/<path>` `#/replays`
`#/replays/<file>` (`?test=<id>` filters) `#/play` `#/bot` `#/launch` `#/cards` (`?q=` search).

### What `lib.js` exports

| Group | Exports |
| --- | --- |
| DOM | `$`, `$$`, `esc`, `html` (tagged template, escapes; `raw()` opts out), `el`, `store(key[, value])` (localStorage, `null` removes), `navigate(hash)`, `sleep`, `cssOnce(url)` (loads a stylesheet once; await it before painting) |
| Data | `loadData()` (promise, once), `data()` (sync after load), `runtime()` → `{db, user, mcp, meId}` on claude.ai (nulls elsewhere) |
| Format | `pct(x, digits=1)`, `pct0`, `num`, `interval([lo,hi])` → "41–63%", `halfWidth`, `fmtDate`, `fmtDateTime`, `ago`, `duration`, `plural` |
| Stats | `wilson(wins, n)`, `overlaps(ciA, ciB)`, `pooled(deck)` → `{games, wins, rate, ci, weighted}` |
| Colour | `DOMAINS`, `DOMAIN_NAMES`, `domainColor`, `domainDot`, `domainDots`, `rateTone(r)` → win/warn/loss/neutral, `rateColor` |
| Cards | `cardById`, `cardByName` (also "Tag, Name" legends), `card(ref)`, `legendShort(name)` → the champion's name ("Kennen" for "Heart of the Tempest"), `isLandscape`, `SETS`, `setName(code)`, `findCards(text)`, `chipText(text, {cls, preview})` (card names in free text → chips) |
| Decks | `deckByPath`, `allDecks`, `deckLabel`, `deckShort(path)` → "Kennen (Dylan Sumner)", `deckHero`, `deckPilot(deck)` → `{place, pilot, name}`, `deckEvent(path)` → slug, `eventLabel(slug)` → "Sacramento showdown, 24 Oct 2026", `groupDecks`, `deckFace(deck|path)` (legend art, else champion), `legendFace(name)` |
| Tests | `isPlumbing(test)`, `testsByDate()`, `testById`, `bestList()` (ranked by the interval's lower bound), `testConclusion(test)` (one sentence), `testFaces(test)` → `{mine, theirs}`, `botSpec(raw)` → `{kind, sims, net, label, short, text}`, `botBadge(raw)`, `netName(path)` |
| Imagery | `cardImage(url, w)` (CDN webp rendition), `fallbackFrame`, `cardImg(ref, {size, w, crop, hover, count, cls})`, `portrait(ref, {size})`, `portraits(refs, {size, max})`, `cardChip(name, {count, cls, preview})` (`preview:false` inside buttons: no focus stop of its own), `cardGrid(list, {champion, w, captions})`, `installCardPreview()` (hides the hover preview when its element leaves the DOM), `showPreview`, `hidePreview` |
| Components | `kpi({...})`, `badge(text, kind)`, `chip(html)`, `pageHead({title, sub, actions, eyebrow})`, `empty(title, hint)`, `loadingBox`, `errorBox`, `notice(html, kind)`, `rateBar(rate, ci, w)`, `rate(rate, ci)`, `table(columns, rows, opts)`, `enhance(root)`, `toast(msg, kind)`, `modal({title, body, actions})` |

`cardImage` is the one implementation of the CDN rule for app code (`?w=&fm=webp&q=78` on
`cmsassets.rgpub.io`); `board.js` carries its own copy because it is a standalone script.

## 2. Tokens (`:root` in app.css)

**Surfaces** (dark to light): `--s0` page `#0a0e1a` · `--s1` panel `#11162a` · `--s2` raised /
sunk field, zebra `#181e34` · `--s3` hover `#212842` · `--glass` header. The page background
is explicit on `body` with a soft gold glow at the top, a vignette and a faint diagonal
texture, the same recipe as the table felt. There is no light theme.

**Lines**: `--line` (8% white) for every border, `--line-strong` (16%) for inputs and sticky
headers, `--chrome` (gold 28%) only for the header rule and "gilt" panels.

**Ink**: `--ink` `#efe6cf` body text · `--ink-2` secondary · `--ink-dim` labels, captions ·
`--ink-faint` timestamps · `--parch` `#e9dcb8` headings and emphasised numbers.

**Accent**: `--gold` `#d6b25e` (`--gold-2` lighter for links/active, `--gold-dim` for hairlines,
`--gold-soft` 12% fill). Gold means "interactive or selected"; use it sparingly.

**Meaning**

| Token | Use |
| --- | --- |
| `--win` green | win rate ≥ 55%, a positive delta, bot agrees, adopted |
| `--loss` red | win rate ≤ 45%, a negative delta, defeat, rejected |
| `--warn` yellow | 45–55% (undecided), caution notices |
| `--disagree` orange | bot disagrees with the human |
| `--info` blue | neutral facts (counts) |
| `--neutral` grey | unknown, plumbing tests, not compared |
| `--p1` teal / `--p2` violet | player 1 / player 2 on the table and in chain items |

`rateTone(rate)` gives the token for a win rate; never pick these by hand.

**Domains** (Riftbound's six): `--fury #d4483b` `--calm #27a97e` `--mind #3f86e0`
`--body #e38a2e` `--chaos #8e5cf0` `--order #e1b83a`. Shown as dots (`.dom.<name>`), as the
gradient of a fallback card frame, and in rune strips. Do not use domain colours for meaning.

**Type**: `--display` Chakra Petch for headings, labels, buttons and numbers in tiles;
`--text` Source Sans 3 for everything readable; `--mono` JetBrains Mono for numbers in tables,
ids, seeds, commands, timestamps. Numbers always use `font-variant-numeric: tabular-nums`.

Scale: `.display-xl` 34–52px (hero only) · `h1` 32 · `h2` 22 · `h3` 15 uppercase tracked
(section labels in panels) · `h4` 15 · body 15 · `.small` 13 · `.xs` 12 · `.mono` 12.5 ·
`.lede` 18 · `.eyebrow` 11.5 uppercase gold.

**Spacing**: an 8px scale, `--sp-1` 4 · `--sp-2` 8 · `--sp-3` 12 · `--sp-4` 16 · `--sp-5` 24 ·
`--sp-6` 32 · `--sp-7` 48 · `--sp-8` 64. Page gutter 16px (`.wrap`, safe-area aware); content
max width 1200px, 1560px on Replays and Play (`body[data-view]`).

**Shape and elevation**: radii `--r-1` 6 (controls) · `--r-2` 10 (panels, tiles) · `--r-3` 14
(hero, modal). Elevation is a border plus a soft shadow: `--sh-1` resting, `--sh-2` raised,
`--sh-3` floating (previews, modals). Focus is a gold ring (`--focus`).

**Motion**: hover transitions ≤ 150ms, bars 250ms. Everything is disabled under
`prefers-reduced-motion: reduce`.

## 3. Components

Layout: `.wrap` · `.stack` (column, 16px; `.tight` 8, `.loose` 32) · `.row` (wrap, 16px) ·
`.grid2` `.grid3` (collapse at 760/960px) · `.grid-auto` (`--min` column width) · `.section` +
`.section-head` (h2 + optional `.more` link on the right) · `.spacer`.

| Component | Markup / helper | Notes |
| --- | --- | --- |
| Page header | `pageHead({title, sub, actions, eyebrow})` → `.page-head` | One per screen, title in sentence case |
| Panel | `.panel` (+ `.raised`, `.sunk`, `.gilt`), `.panel-head` with `.actions` | `details.panel` for collapsible sections |
| KPI tile | `kpi({label, value, unit, sub, tone, art, href})` → `.kpi` in a `.kpis` grid | Value in display 30px; `tone` colours the left bar; `art` takes a `portrait()` |
| Badge | `badge(text, kind)` → `.badge.win/.loss/.warn/.disagree/.neutral/.gold/.bot/.p1/.p2/.solid/.mono` | Uppercase, 10.5px; one or two words. `botBadge(spec)` names a bot readably |
| Chip | `chip(html)` → `.chip` | Facts in a row: "seed 42", "20 games per matchup" |
| Card chip | `cardChip(name, {count})` → `.card-chip` | Tiny round art crop + name; hover/tap shows the card. Use wherever a card is named in text or a table |
| Card image | `cardImg(ref, {size: xs 44·sm 72·md 120·lg 200·xl 280, crop, count})` → `.card-img` | Full card; `crop` gives a square art crop; battlefields are landscape automatically |
| Portrait | `portrait(ref, {size: sm 28·md 40·lg 56·xl 88})`, `portraits([...], {max})` → `.portrait`, `.portraits` | Round art crop of a legend or champion; use on test rows, matchups, KPI art. `.versus` with `.vs` between two groups |
| Card grid | `cardGrid(list, {champion, w, captions})` → `.card-grid` | Deck lists as real images with `×n` count badges; champion gets a gold ring |
| Preview layer | `#card-preview` + `#card-backdrop` in index.html, installed by `installCardPreview()` | Any element with `data-card="Name"` (and `tabindex="0"`) gets hover (desktop), focus and tap (modal) preview. Inside the table, RiftBoard's own zoom applies |
| Buttons | `button` / `.btn` secondary (default), `.primary` / `.btn-primary` gold, `.btn-ghost`, `.btn-danger`, `.btn-sm`, `.btn-lg`, `.icon-btn` | One primary per panel. Labels are verbs: "Start", "Send to Claude", "Show 96 more" |
| Inputs | `label.f` (uppercase label above control), `select`, `input`, `textarea`, `.search` (icon), `.toolbar` (wrapping row aligned at the bottom) | |
| Segmented / tabs | `.segmented > button[aria-pressed]`, `.tabs > [aria-selected]` | Segmented for filters, tabs for sub-views |
| Table | `table(columns, rows, {rowAttrs, cls, sort, empty})` → `.tablewrap > table.data`; `enhance(root)` | Sticky header, zebra rows, `num` columns right-aligned in mono, `sort: true` headers (click to sort, `data-v` carries the value). `tr.click` rows, `tr.sel` selected, `td.lead` + `.sub` for title/subtitle cells. `.tablewrap.tall` caps height |
| Rate bar | `rateBar(rate, ci, w)`, `rate(rate, ci)` → `.rate` | Dot at the rate, 50% tick, the 95% interval as a translucent band with whiskers; colour from `rateTone` |
| Progress | `.prog > i`, `.bar > i` | Width in %, gold |
| States | `empty(title, hint)`, `loadingBox(msg)`, `errorBox(msg)`, `notice(html, "ok|error|info")` | Empty states say what would fill them and link to where |
| Toast | `toast(msg, "ok|error")` | Confirmation of an action; 4s |
| Modal | `modal({title, body, actions:[{label, value, primary}]})` → promise | Esc / backdrop dismiss with `null` |
| Keys | `.kbd` | Keyboard hints |
| Log | `.log` | Mono, scrollable, for commands and engine output |

**Card image sizes**: chips 22px crop (`w=120` from the CDN); portraits 28–88px (`w=200`);
`.card-img` xs 44 / sm 72 / md 120 / lg 200 / xl 280 wide (`w=120…600`); the preview
260px (`w=600`, 800 for battlefields); the table (board.js) 200–240 for hand and units, 600
for battlefield art. Always `loading="lazy" decoding="async" referrerpolicy="no-referrer"`;
the helpers do this. When no image is known or it fails, `.cfb` draws a frame in the card's
domain colours with its name, cost and type — never a broken image icon.

## 4. Layout rules

- Phone first: 16px gutters, one column under 760px, no horizontal page scroll. Tables scroll
  inside `.tablewrap`; long select options are fine, long pills are not (wrap or shorten).
- The header is sticky and 60px; anchor scroll padding accounts for it.
- One `h1` per screen (the page header), `h2` for sections, `h3` for labels inside panels.
- Dense data goes in tables, not in chips. Chips are for a handful of facts.
- Put the real card wherever a card name appears: a chip inline, a grid for a list, a
  portrait for a legend or champion on a row. Text-only card names are a bug.

## 5. Writing

The reader plays Riftbound and is not an engineer. The voice is clear and confident.

- Short sentences. Plain words. Sentence case everywhere, including buttons and labels.
- No exclamation marks. No emojis. No "please". No marketing adjectives.
- Explain a term the first time it appears in a screen, in one clause: "the 95% interval,
  the range the true rate most likely sits in".
- Numbers carry their uncertainty: a win rate shows its interval and its game count.
  "55% (40–69%, 40 games)" not "55%".
- Say what a thing means, not what the code did: "the bot could not see the opponent's hand",
  not "hands masked per perspective".
- Lead with the conclusion, then the evidence: "Cutting Heron costs about 3 points. 52.5% vs
  55.2% over 980 games; the intervals overlap."
- Dates as "29 Oct 2026" and relative ("3 hours ago") where recency matters; times in UTC.
- Labels are nouns ("Games per matchup"), buttons are verbs ("Start", "Send to Claude").
- Empty states say what would fill them: "No test yet. Set one up in Launch."
- Errors say what happened and what to do: "Request not sent: the connector is not allowed
  here. Copy the request and paste it to Claude."

Good: "Riven (Nam Dang) wins 55% against two Kennen lists over 40 games, interval 40–69%."
Bad: "ismcts:sims=40 achieved WR=0.55 on 2 matchups!"

Good: "Agreement: how often the bot's first choice is the move the human made."
Bad: "Agreement = argmax(policy) == human_action."

Good: "Plumbing check: 800 games with random players ran without error. Says nothing about
deck strength."
Bad: "smoke test OK".
