#!/usr/bin/env node
// Playwright helper for import_riftlite.py. Reads only what a visitor sees on
// RiftLite's public pages: the public replay list and a replay's own page
// (header, "Match log" panel, "Match decks" panel), one page at a time.
//
//   node riftlite_scrape.js list                 -> JSON array of public replays
//   node riftlite_scrape.js replay <id> [--step S] [--until S] [--verbose] [--quiet]
//                                                -> JSON with header + full match log
//
// The match log panel shows roughly the last 100 lines of the current game, so
// the replay is swept with the progress slider in steps of S seconds (default
// 30). Each read is collapsed into runs of identical lines and merged into the
// log gathered so far with a longest-common-subsequence alignment: new lines
// are appended or inserted, lines the site removed after a player "rewound"
// are recorded as retracted, and copies that some captures re-emit on every
// seek are dropped. The panel is blanked by the game-over screen, so the last
// lines of a game are recovered by stepping back from the blank frame. A read
// that cannot be aligned at all halves the step; at 1 s it leaves a
// "[capture gap ...]" marker and keeps only lines never seen before.
// RL_DEBUG=1 prints diagnostics for unaligned reads and large retractions.
'use strict';
const {chromium} = require('playwright');

const BASE = 'https://www.riftlite.com';
const LOG_SEL = '[role=tabpanel] article, article[class*="logEntry"]';
const SLIDER_SEL = 'input[aria-label="Replay progress"]';
const WINDOW_FULL = 95; // >= this many entries: assume the window is truncated

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const log = (...a) => { if (!process.argv.includes('--quiet')) console.error(...a); };

function argValue(flag, dflt) {
  const i = process.argv.indexOf(flag);
  return i >= 0 && process.argv[i + 1] ? process.argv[i + 1] : dflt;
}

async function withBrowser(fn) {
  const browser = await chromium.launch();
  try {
    const page = await browser.newPage({viewport: {width: 1500, height: 1000}});
    return await fn(page);
  } finally {
    await browser.close();
  }
}

// ---------------------------------------------------------------- list ----
async function scrapeList(page) {
  await page.goto(`${BASE}/replays`, {waitUntil: 'networkidle', timeout: 90000});
  await page.waitForTimeout(3000);
  for (let i = 0; i < 10; i++) {
    const more = await page.$('text=/load more/i');
    if (!more) break;
    await more.click();
    await page.waitForTimeout(2500);
  }
  return page.evaluate(() => {
    const out = [];
    const links = [...document.querySelectorAll('a,button')]
      .filter((e) => /watch replay/i.test(e.innerText || ''));
    for (const bt of links) {
      let el = bt;
      for (let k = 0; k < 6 && el && !(el.innerText || '').includes('messages'); k++) el = el.parentElement;
      const href = bt.getAttribute('href') || (bt.closest('a') || {}).getAttribute?.('href') || null;
      const lines = (el ? el.innerText : '').split(/\n+/).map((s) => s.trim()).filter(Boolean);
      out.push({href, id: href ? href.split('/').pop() : null, lines});
    }
    return out;
  });
}

// -------------------------------------------------------------- replay ----
async function readLog(page) {
  return page.$$eval(LOG_SEL, (es) => es.map((e) => ({
    t: (e.querySelector('span') || {}).innerText || '',
    text: (e.querySelector('p') || e).innerText || '',
  })).filter((x) => x.text));
}

async function readLabels(page) {
  return page.evaluate(() => {
    const leaves = [...document.querySelectorAll('body *')].filter((e) => e.children.length === 0);
    const find = (re) => { for (const e of leaves) { const m = (e.textContent || '').trim().match(re); if (m) return m[1]; } return null; };
    const sl = document.querySelector('input[aria-label="Replay progress"]');
    return {game: find(/^GAME (\d+)$/i), turn: find(/^TURN (.+)$/i), slider: sl ? +sl.value : null};
  });
}

async function setSlider(page, v) {
  await page.evaluate((v) => {
    const el = document.querySelector('input[aria-label="Replay progress"]');
    const set = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
    set.call(el, String(v));
    el.dispatchEvent(new Event('input', {bubbles: true}));
    el.dispatchEvent(new Event('change', {bubbles: true}));
  }, v);
}

// Wait until the slider has reached the target and the log stopped changing.
async function settle(page, target) {
  let last = null, stable = 0;
  for (let i = 0; i < 14; i++) {
    await sleep(250);
    const lb = await readLabels(page);
    const L = await readLog(page);
    const sig = `${lb.slider}|${lb.game}|${L.length}|${L[0] ? L[0].t + L[0].text : ''}`;
    if (sig === last) { stable++; if (stable >= 2 && Math.abs(lb.slider - target) < 1500) return {labels: lb, entries: L, polls: i + 1}; }
    else { stable = 0; last = sig; }
  }
  return {labels: await readLabels(page), entries: await readLog(page), polls: 14};
}

// Align the window `win` (chronological, the last ~100 entries of the game log)
// with the accumulated `master`: find the smallest i such that master[i:] equals
// a prefix of win. When no such i exists, allow a SMALL tail of master (at most
// MAX_DROP entries, the lines a player "rewound") to be missing from win; the
// smallest drop wins. Returns null when nothing aligns (a burst of more than a
// window of new lines; the caller marks a gap). Logs can hold long runs of
// identical lines, so matches shorter than 3 entries are never trusted.
async function captureHeader(page) {
  return page.evaluate(() => {
    const scene = document.querySelector('[class*="matchupScene"]');
    if (!scene) return null;
    const heroes = [...scene.querySelectorAll('[class*="scenePlayerHero"]')].map((h) => ({
      role: (h.querySelector(':scope > span') || {}).innerText || '',
      name: (h.querySelector('h2') || {}).innerText || '',
      legend: (h.querySelector('p') || {}).innerText || '',
      cards: [...h.querySelectorAll('button[data-card-code]')].map((b) => ({
        name: b.getAttribute('aria-label'), code: b.getAttribute('data-card-code'),
      })),
    }));
    const vs = scene.querySelector('[class*="versusMark"]');
    return {heroes, game_label: vs ? vs.innerText.replace(/\s+/g, ' ').trim() : null, text: scene.innerText};
  });
}

async function captureDecks(page) {
  const btn = await page.$('button.replay-history-decks-button, button:has-text("Match decks")');
  if (!btn) return {available: false, text: null};
  await btn.click();
  await page.waitForTimeout(1200);
  const res = await page.evaluate(() => {
    const close = document.querySelector('button[aria-label="Close match decks"]');
    if (!close) return {available: false, text: null};
    // the panel is the nearest ancestor that holds a heading "Match decks"
    let el = close;
    for (let k = 0; k < 6 && el; k++) {
      el = el.parentElement;
      if (el && /Match decks/.test(el.innerText) && el.innerText.length > 60) break;
    }
    const text = el ? el.innerText : '';
    const cut = text.indexOf('CURRENT CARD');
    const panel = (cut > 0 ? text.slice(0, cut) : text).trim();
    const available = !/available to the replay owner/i.test(panel) && /\d+\s*[x×]?\s*\w/.test(panel);
    return {available, text: panel.slice(0, 20000)};
  });
  const close = await page.$('button[aria-label="Close match decks"]');
  if (close) await close.click().catch(() => {});
  return res;
}

// Consecutive identical lines are collapsed into runs {t, text, n}. Some
// captures re-emit one message on every seek, so the panel grows copies of a
// line at its original position; aligning on runs (ignoring n) keeps the
// stitching stable, and a run keeps the count from its first observation,
// which is the least polluted one. Runs are expanded back to lines on output.
function collapse(win) {
  const runs = [];
  for (const e of win) {
    const last = runs[runs.length - 1];
    if (last && last.t === e.t && last.text === e.text) last.n++;
    else runs.push({t: e.t, text: e.text, n: 1});
  }
  return runs;
}
function expand(runs) {
  const out = [];
  for (const r of runs) for (let i = 0; i < r.n; i++) out.push({t: r.t, text: r.text});
  return out;
}

// Last resort when a window cannot be aligned: keep a gap marker and append
// only the runs whose line master has never seen, so an unexplained read never
// duplicates a whole window.
function appendUnseen(cur, win) {
  const key = (e) => `${e.t}\u0001${e.text}`;
  const known = new Set(cur.entries.map(key));
  const fresh = collapse(win).filter((r) => !known.has(key(r)));
  cur.entries = cur.entries.concat([{t: '', text: '[capture gap: log window could not be aligned]', n: 1}], fresh);
}

// Longest common subsequence of two key arrays, as ordered index pairs.
function lcsPairs(A, B) {
  const n = A.length, m = B.length;
  const dp = Array.from({length: n + 1}, () => new Uint16Array(m + 1));
  for (let i = n - 1; i >= 0; i--) for (let j = m - 1; j >= 0; j--) {
    dp[i][j] = A[i] === B[j] ? dp[i + 1][j + 1] + 1 : Math.max(dp[i + 1][j], dp[i][j + 1]);
  }
  const pairs = [];
  let i = 0, j = 0;
  while (i < n && j < m) {
    if (A[i] === B[j]) { pairs.push([i, j]); i++; j++; }
    else if (dp[i + 1][j] >= dp[i][j + 1]) i++;
    else j++;
  }
  return pairs;
}

const HEAD_RUNS = 40; // window runs that may belong to a stale block at the head of the panel

// Merge a window read (raw lines, chronological) into the current game, whose
// entries are runs. The window is the site's current view, so from the point
// where it starts covering master it is authoritative: new lines it inserts
// are kept, master lines it no longer shows are recorded as retracted (a
// player "rewound"), and each run keeps the count from its first observation.
// Some captures re-emit one or two old lines on every seek; those copies sit
// at the head of the panel (sorted by timestamp) and push the real old lines
// out. They are recognised as (a) unmatched window runs whose line master
// already holds, which are dropped, and (b) a "scroll gap" (master lines
// skipped between two matches) inside the head of the window, after which the
// window really starts covering master. Returns false when the window does not
// cover master's tail at all (the step was too large).
function mergeWindow(cur, win, stats) {
  const runs = collapse(win);
  if (!cur.entries.length) { cur.entries = runs; return true; }
  const key = (e) => `${e.t}\u0001${e.text}`;
  const A = cur.entries.map(key), B = runs.map(key);
  let pairs = lcsPairs(A, B);
  if (!pairs.length) return false;
  // Master lines missing between two matches are either evicted from the panel
  // (the re-emission artefact keeps old copies and evicts the other old lines,
  // anywhere in the window) or retracted by a rewind. A rewind always shows a
  // new "rewound" line in the same read, so without one every gap is an
  // eviction and the window starts covering master after the last gap; with
  // one, only gaps inside the head of the window count as evictions.
  const seen = new Set(A);
  if (runs.every((r) => seen.has(key(r)))) return true; // nothing new in this read (post-game panels hold only stale copies)
  const rewindNow = runs.some((r) => !seen.has(key(r)) && /rewound to before|restore the previous timeline/.test(r.text));
  let q = 0;
  for (let x = 1; x < pairs.length; x++) {
    if (pairs[x][0] - pairs[x - 1][0] > 1 && (!rewindNow || pairs[x][1] <= HEAD_RUNS)) q = x;
  }
  pairs = pairs.slice(q);
  const [i0, w0] = pairs[0];
  const tailLen = A.length - i0;
  if (pairs.length < Math.min(2, tailLen) || pairs.length < 0.5 * tailLen) return false;
  const first = new Map();
  for (const r of cur.entries) if (!first.has(key(r))) first.set(key(r), r.n);
  const matchedM = new Set(pairs.map((p) => p[0]));
  const matchedW = new Map(pairs.map((p) => [p[1], p[0]]));
  const gone = [];
  for (let i = i0; i < A.length; i++) if (!matchedM.has(i)) gone.push(cur.entries[i]);
  const keep = new Set(A.slice(0, i0));
  for (const i of matchedM) keep.add(A[i]);
  const out = [];
  let dropped = 0;
  for (let j = 0; j < runs.length; j++) {
    const r = runs[j];
    if (matchedW.has(j)) {
      const c = cur.entries[matchedW.get(j)].n;
      if (j >= runs.length - 10) r.n = Math.max(r.n, c); // still at the live tail: a genuine repeat may be arriving
      else { if (r.n > c) stats.repeat_growth++; r.n = Math.min(r.n, c); } // older: copies only grow by re-emission
      out.push(r);
    } else if (!keep.has(key(r))) {
      out.push(r); // a line master has never seen
    } else {
      dropped++;   // a copy of a known line: the re-emission artefact
    }
  }
  if (dropped) stats.dropped_copies = (stats.dropped_copies || 0) + dropped;
  if (gone.length > 4 && process.env.RL_DEBUG) {
    const k = (e) => `${e.t}|${e.text.slice(0, 28)}${e.n > 1 ? ' x' + e.n : ''}`;
    log(`  RETRACT ${gone.length}: master=${A.length} win=${B.length} runs, anchor=(${i0},${w0}) q=${q}`);
    log('   win head: ' + runs.slice(0, 6).map(k).join(' || '));
    log('   gone head: ' + gone.slice(0, 5).map(k).join(' || '));
  }
  if (gone.length) {
    stats.retracted += gone.length;
    cur.retracted.push(...gone.map((e) => ({t: e.t, text: e.text, after_index: i0})));
  }
  cur.entries = cur.entries.slice(0, i0).concat(out);
  return true;
}

// The panel is blanked by the game-over screen (and by the next game's intro),
// so when a read no longer shows the current game, walk back from `target`
// towards `from` in small steps to the last frame that still shows it.
async function recoverTail(page, cur, from, target, stats) {
  for (let t = target - 2000; t > from; t -= 2000) {
    await setSlider(page, t);
    const {labels, entries} = await settle(page, t);
    stats.reads++; stats.tail_reads = (stats.tail_reads || 0) + 1;
    const win = entries.slice().reverse();
    if (!win.length || (labels.game && cur.label && labels.game !== cur.label)) continue;
    if (!mergeWindow(cur, win, stats)) { stats.gaps++; log(`  gap in tail before ${t}ms`); appendUnseen(cur, win); }
    return;
  }
}

async function scrapeReplay(page, id, stepS) {
  const url = `${BASE}/replays/${id}`;
  await page.goto(url, {waitUntil: 'networkidle', timeout: 120000});
  await page.waitForTimeout(3000);
  const title = await page.title();
  const header = await captureHeader(page);
  const body0 = await page.evaluate(() => document.body.innerText);
  const slider = await page.$eval(SLIDER_SEL, (e) => ({max: +e.max, value: +e.value})).catch(() => null);
  if (!slider) throw new Error('no progress slider on page (not a replay page?)');
  const decks = await captureDecks(page);

  const tab = await page.$('[role=tab]:has-text("Match log")');
  if (tab) await tab.click();
  await page.waitForTimeout(600);

  const stepMs = Math.max(1000, Math.round(stepS * 1000));
  const games = [];           // [{index, label, first_seen_ms, entries, retracted}]
  let cur = null;
  const stats = {reads: 0, fallbacks: 0, gaps: 0, retracted: 0, tail_reads: 0, repeat_growth: 0, step_s: stepS};
  const bump = (lb, ms) => {
    cur = {index: games.length + 1, label: lb.game, first_seen_ms: ms, entries: [], retracted: []};
    games.push(cur);
  };

  let pos = 0, step = stepMs, prevEmpty = false;
  const until = parseFloat(argValue('--until', '0')) * 1000;
  const last = until > 0 ? Math.min(slider.max, until) : slider.max;
  const t0 = Date.now();
  const stop = last - 1; // avoid the very end: the panel is cleared there
  while (pos < stop) {
    const target = Math.min(stop, pos + step);
    const tr = Date.now();
    await setSlider(page, target);
    const {labels, entries, polls} = await settle(page, target);
    stats.reads++;
    if (process.argv.includes('--verbose')) log(`  read ${stats.reads} target=${target} slider=${labels.slider} game=${labels.game} n=${entries.length} polls=${polls} ${Date.now() - tr}ms (${((Date.now() - t0) / 1000).toFixed(0)}s)`);
    const win = entries.slice().reverse(); // panel is newest first
    const gameChanged = !!(cur && labels.game && cur.label && labels.game !== cur.label);
    if (cur && cur.entries.length && !prevEmpty && (!win.length || gameChanged)) {
      // the game ended (or the replay did) somewhere in (pos, target]: pick up its last lines
      await recoverTail(page, cur, pos, target, stats);
    }
    prevEmpty = !win.length;
    if (gameChanged) {
      // new game: the panel restarts from scratch
      if (win.length) { bump(labels, target); mergeWindow(cur, win, stats); }
      else { cur = null; } // intro scene; bump on the next non-empty read
      pos = target; step = stepMs; continue;
    }
    if (!win.length) { pos = target; step = stepMs; continue; }
    if (!cur) { bump(labels, target); mergeWindow(cur, win, stats); pos = target; step = stepMs; continue; }

    if (!mergeWindow(cur, win, stats)) {
      if (process.env.RL_DEBUG) {
        const k = (e) => `${e.t}|${e.text.slice(0, 40)}${e.n > 1 ? ' x' + e.n : ''}`;
        const runs = collapse(win);
        log(`  NOALIGN target=${target} master=${cur.entries.length} runs, win=${win.length} lines/${runs.length} runs`);
        log('   master tail: ' + cur.entries.slice(-6).map(k).join(' || '));
        log('   win head:    ' + runs.slice(0, 4).map(k).join(' || '));
        log('   win tail:    ' + runs.slice(-6).map(k).join(' || '));
        const wk = runs.map((e) => `${e.t}\u0001${e.text}`);
        log('   master last 14 -> window index: ' + cur.entries.slice(-14).map((e) => `${e.t}:${wk.indexOf(`${e.t}\u0001${e.text}`)}`).join(' '));
        const mk = cur.entries.map((e) => `${e.t}\u0001${e.text}`);
        log('   window runs missing from master (idx:key): ' + runs.map((e, i) => [i, e]).filter(([i, e]) => !mk.includes(`${e.t}\u0001${e.text}`)).slice(0, 12).map(([i, e]) => `${i}:${k(e)}`).join(' || '));
      }
      if (step > 1000) { stats.fallbacks++; step = Math.max(1000, Math.floor(step / 2)); continue; }
      stats.gaps++; log(`  gap before ${target}ms`);
      appendUnseen(cur, win);
    }
    pos = target; step = stepMs;
  }
  // final state + end screen
  await setSlider(page, last);
  await page.waitForTimeout(2500);
  const bodyEnd = await page.evaluate(() => document.body.innerText);
  const endLabels = await readLabels(page);

  for (const g of games) g.entries = expand(g.entries);
  const total = games.reduce((n, g) => n + g.entries.length, 0);
  log(`  ${id}: ${games.length} game(s), ${total} log entries, ${stats.reads} reads, ${stats.fallbacks} fallbacks, ${stats.gaps} gaps, ${stats.retracted} retracted`);
  return {
    id, url, title, scraped_at: new Date().toISOString(),
    header, duration_ms: slider.max, decks,
    games, sweep: stats,
    end_labels: endLabels,
    end_screen_text: bodyEnd.slice(0, 3000),
    start_screen_text: body0.slice(0, 3000),
  };
}

// ---------------------------------------------------------------- main ----
(async () => {
  const cmd = process.argv[2];
  if (cmd === 'list') {
    const rows = await withBrowser(scrapeList);
    process.stdout.write(JSON.stringify(rows, null, 1) + '\n');
  } else if (cmd === 'replay') {
    const id = process.argv[3];
    if (!id) throw new Error('usage: riftlite_scrape.js replay <rl2_id> [--step S]');
    const stepS = parseFloat(argValue('--step', '30'));
    const res = await withBrowser((p) => scrapeReplay(p, id, stepS));
    process.stdout.write(JSON.stringify(res, null, 1) + '\n');
  } else {
    console.error('usage: riftlite_scrape.js list | replay <rl2_id> [--step S] [--quiet]');
    process.exit(2);
  }
})().catch((e) => { console.error('ERR', e.stack || e.message); process.exit(1); });
