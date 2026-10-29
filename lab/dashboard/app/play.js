/* Riven Lab — Play: a full game against the bot, in the browser.
   The WebAssembly engine runs in engine.worker.js (rb_bot_move blocks, so never on the page thread). Worker protocol, unchanged:
     init {base, net} → {version, cards, net} · newGame {deck1, deck2, humanSeat, seed, bot} → state · state {perspective} → state
     apply {index} → state · botMove → {index, text, type, player, options, search, ms, state} · result → {winner, scores, turns,
     decisions, reason} · end → true; while downloading, {type:"progress", what:"wasm"|"net", loaded, total} events.
   The table is RiftBoard (board.js, a global) with the human's seat at the bottom and the bot's hand face down; the options are
   the board's gold highlights plus the list under the table. The game lives in module scope: leaving the screen and coming back
   finds it where it was. Router contract: render(container) / destroy(); no params. Styles: app/play.css (prefix .v-play). */
import { data, esc, $, $$, el, store, card, cardByName, cardImage, cardImg, fallbackFrame, portrait, domainDots, pageHead, chip,
         empty, notice, modal, toast, hidePreview, deckByPath, deckShort, deckLabel, deckFace, groupDecks, legendShort, num, fmtDate, plural } from "./lib.js";

const CSS_ID = "v-play-css";
const DEFAULTS = { me: "decks/riven_sacramento.txt", bot: "lab/decks/meta/sacramento_showdown_2026-10-24/06_dylan_sumner_kennen.txt", seat: "random", seed: "", level: "quick", autopay: true };
/* Bot presets: what rb_new_game's bot spec accepts is "greedy[:det=<redeals>,budget=<ms>,steps=<n>,wide=<n>]" or "random". */
const LEVELS = {
  quick:   { label: "Quick",   spec: "greedy",                   help: "Redeals what it cannot see 3 times and answers within about 1.5 s." },
  careful: { label: "Careful", spec: "greedy:det=5,budget=3000", help: "Five redeals and up to 3 s per move: steadier choices, slower turns." },
};
const SEATS = { "1": "Player 1, on the play", "2": "Player 2, on the draw", random: "Random" };
const BOT_PAUSE = { undecided: 450, forced: 160 }, AUTO_PAUSE = { pass: 220, pay: 100 }, LOG_MAX = 400;

/* Module state: survives navigation. `host` is the container while the screen is mounted, null otherwise. */
const G = {
  host: null, root: null, boardEl: null, ui: { group: {}, logOpen: false, sheet: true },
  cfg: { ...DEFAULTS, ...(store("rl.play") || {}) },
  worker: null, pending: new Map(), nextId: 1, ready: null, info: null, progress: {},
  stage: "setup", st: null, seat: 1, seed: 0, autopay: true, level: "quick",
  busy: false, thinking: false, thinkStart: 0, timer: 0, lastBot: null, botMoves: [], myMoves: 0, log: [], loggedN: -1,
  tally: null, result: null, lastHint: "", error: null,
};
let keysBound = false, nameRe = null;

// ── engine worker ───────────────────────────────────────────────────────────
function call(cmd, args) {
  if (!G.worker) {
    G.worker = new Worker("engine.worker.js");
    G.worker.onmessage = e => {
      const m = e.data;
      if (m.type === "progress") { G.progress[m.what] = m; paintProgress(); return; }
      const p = G.pending.get(m.id); if (!p) return; G.pending.delete(m.id);
      m.ok ? p.resolve(m.result) : p.reject(new Error(m.error));
    };
    G.worker.onerror = e => {
      const err = new Error("The engine stopped: " + (e.message || "unknown error") + ". Start again to reload it.");
      for (const p of G.pending.values()) p.reject(err);
      G.pending.clear(); try { G.worker.terminate(); } catch (x) { /* already gone */ }
      G.worker = null; G.ready = null; G.info = null; G.error = err.message; G.stage = "setup"; G.busy = false; G.thinking = false; stopTimer(); draw();
    };
  }
  return new Promise((resolve, reject) => { const id = G.nextId++; G.pending.set(id, { resolve, reject }); G.worker.postMessage({ id, cmd, ...(args || {}) }); });
}
function engineReady() {
  if (!G.ready) {
    const E = data().engine, base = new URL(E.js, location.href).href.replace(/riftbound\.js$/, "");
    G.ready = call("init", { base, net: E.net ? new URL(E.net, location.href).href : "" })
      .then(i => { G.info = i; store("rl.play.engineSeen", true); return i; }).catch(e => { G.ready = null; throw e; });
  }
  return G.ready;
}

// ── decks ───────────────────────────────────────────────────────────────────
const playable = () => data().decks.filter(d => !(d.errors || []).length && d.text);
/** Groups of playable decks as the Decks screen names them, empty groups dropped. */
function deckGroups() { const g = groupDecks(playable()); for (const k of Object.keys(g)) if (!g[k].length) delete g[k]; return g; }
const groupOf = path => Object.entries(deckGroups()).find(([, ds]) => ds.some(d => d.path === path))?.[0] || Object.keys(deckGroups())[0];
function prettyEvent(slug) {
  const m = String(slug).match(/^(.*?)_?(\d{4}-\d{2}-\d{2})$/), name = (m ? m[1] : String(slug)).replace(/_/g, " ").trim();
  const cap = name ? name[0].toUpperCase() + name.slice(1) : ""; return m ? `${cap}${cap ? ", " : ""}${fmtDate(m[2])}` : cap;
}
/** {name, sub} for a deck tile: "Riven (Nam Dang)" + "#1 · Sacramento showdown, 24 Oct 2026". */
function deckMeta(d) {
  const cs = d.comments || [], head = cs[0] || "", m = head.match(/^(\d+) - (.+?) - (.+)$/);
  const ev = (cs.find(c => /^Event:/.test(c)) || "").replace(/^Event:\s*/, "");
  if (m) return { name: `${m[3]} (${m[2]})`, sub: `#${m[1]}${ev ? " · " + prettyEvent(ev) : ""}` };
  if (d.path.startsWith("lab/variants/")) return { name: deckShort(d.path), sub: "variant of the reference list" };
  if (head.includes(" — ")) return { name: deckShort(d.path), sub: head.split(" — ").slice(1).join(" — ").replace(/[.\s]+$/, "") };
  return { name: deckShort(d.path), sub: "engine test deck" };
}
const deckDomains = d => Object.entries((d.stats || {}).rune_split || {}).sort((a, b) => b[1] - a[1]).map(([k]) => k);
const champOf = d => d.champion || Object.keys((d.list || {}).champion || {})[0] || "";
/** Legend, champion and the three most-played cards: the deck at a glance. */
function keyCards(d) {
  const main = Object.entries((d.list || {}).maindeck || {}).map(([n, k]) => ({ n, k, c: card(n) }))
    .sort((a, b) => b.k - a.k || ((b.c && b.c.energy) ?? 0) - ((a.c && a.c.energy) ?? 0) || a.n.localeCompare(b.n)).slice(0, 3);
  return { legend: deckFace(d), champion: cardByName(champOf(d)), main };
}

// ── legal options ───────────────────────────────────────────────────────────
/** Ties an option to a card / battlefield on the table from its text (the engine gives text + type only). */
function legalHints(legal) {
  return (legal || []).map(l => {
    const e = { index: l.index, text: l.text, type: l.type }; let m;
    if (/^(PlayCard|PlayReaction|PlayActionCard|HideCard)$/.test(l.type) && (m = l.text.match(/^(?:Play|Hide) (.+?)(?: (?:to|at) (.+?))?(?: targeting .+)?(?: \(alternative cost\))?$/))) { e.card = m[1]; if (m[2] && m[2] !== "base") e.battlefield = m[2]; }
    else if (l.type === "StandardMove" && (m = l.text.match(/^Move (.+?) to (.+)$/))) { e.card = m[1]; if (m[2] !== "base") e.battlefield = m[2]; }
    else if (/^Activate/.test(l.type) && (m = l.text.match(/^(?:Use|Equip) (.+?)(?: ability \d+)?(?: (?:on|to) .+)?$/))) e.card = m[1];
    else if (l.type === "ChooseBattlefield" && (m = l.text.match(/^Bring battlefield (.+)$/))) e.card = m[1];
    return e;
  });
}
/** Option text with every card it names shown as an art chip (longest names first, whole words only). */
function chipText(text, cls = "") {
  if (!nameRe) {
    const names = [...new Set(data().cards.map(c => c.name))].filter(n => n.length >= 4).sort((a, b) => b.length - a.length);
    nameRe = new RegExp("(?<![\\w'’])(" + names.map(n => n.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")).join("|") + ")(?![\\w'’])", "g");
  }
  const s = String(text ?? ""), parts = []; let last = 0;
  for (const m of s.matchAll(nameRe)) { parts.push(esc(s.slice(last, m.index)), artChip(m[1], cls)); last = m.index + m[0].length; }
  parts.push(esc(s.slice(last)));
  return parts.join("");
}
/** A card chip without its own focus stop (it sits inside an option button); hover still previews the card. */
function artChip(name, cls = "") {
  const c = card(name); if (!c) return esc(name);
  const src = c.image_url ? cardImage(c.image_url, 120) : "", d = c.domains || [];
  return `<span class="card-chip ${cls}" data-card="${esc(c.name)}"><span class="cc-art" style="--d1:${esc(domColor(d[0]))};--d2:${esc(domColor(d[1] || d[0]))}">${src ? `<img src="${esc(src)}" alt="" referrerpolicy="no-referrer" loading="lazy" decoding="async">` : fallbackFrame(c, c.name)}</span><span class="cc-name">${esc(c.name)}</span></span>`;
}
const domColor = d => ({ fury: "#d4483b", calm: "#27a97e", mind: "#3f86e0", body: "#e38a2e", chaos: "#8e5cf0", order: "#e1b83a" })[String(d || "").toLowerCase()] || "#4a4466";

// ── game flow ───────────────────────────────────────────────────────────────
function noteLog(st) {
  if (!st || st.n === G.loggedN) return; G.loggedN = st.n;
  for (const l of st.log || []) {
    G.log.push(l);
    const m = /SCORE: P([12]) \+(\d+) via (Conquer|Hold|effect)/.exec(l);   // what the points came from, for the result hint
    if (m && G.tally) G.tally[+m[1]][m[3].toLowerCase()] += +m[2];
  }
  if (G.log.length > LOG_MAX) G.log.splice(0, G.log.length - LOG_MAX);
}
async function start() {
  const c = G.cfg; store("rl.play", c);
  G.stage = "loading"; G.error = null; G.progress = {}; draw();
  try {
    await engineReady();
    G.seat = c.seat === "random" ? 1 + Math.floor(Math.random() * 2) : +c.seat;
    G.seed = /^\d+$/.test(c.seed) ? (+c.seed >>> 0) : Math.floor(Math.random() * 2 ** 31);
    G.autopay = !!c.autopay; G.level = LEVELS[c.level] ? c.level : "quick";
    const me = deckByPath(c.me), bot = deckByPath(c.bot);
    if (!me || !bot) throw new Error("Pick a deck for both seats.");
    const st = await call("newGame", { deck1: G.seat === 1 ? me.text : bot.text, deck2: G.seat === 1 ? bot.text : me.text, humanSeat: G.seat, seed: G.seed, bot: LEVELS[G.level].spec });
    Object.assign(G, { st, stage: "playing", busy: false, thinking: false, lastBot: null, botMoves: [], myMoves: 0, log: [], loggedN: -1, result: null, error: null,
                       tally: { 1: { conquer: 0, hold: 0, effect: 0 }, 2: { conquer: 0, hold: 0, effect: 0 } } });
    G.ui.sheet = true; draw(); advance();
  } catch (e) { G.error = e.message; G.stage = "setup"; draw(); }
}
/** The loop: paint, then wait for the human, answer a forced step for them, or let the bot move (with a pause so each move is seen). */
async function advance() {
  if (G.stage !== "playing" || G.busy) return;
  const st = G.st; noteLog(st);
  if (st.game_over) {
    G.busy = true; paint();
    try { G.result = await call("result"); G.st = await call("state", { perspective: 0 }); noteLog(G.st); G.lastHint = decidingLine(G.result).replace(/\.$/, ""); } catch (e) { G.error = e.message; }
    G.busy = false; G.stage = "over"; draw(); return;
  }
  if (st.decision_is_human) {
    // no real choice: a cost step with auto-pay on, or a lone Pass (priority / focus) — answered after a short beat
    const lonePass = st.legal.length === 1 && /^Pass/.test(st.legal[0].type || "");
    if ((st.paying_cost && G.autopay && st.legal.length) || lonePass) {
      G.busy = true; paint(); await sleep(lonePass ? AUTO_PAUSE.pass : AUTO_PAUSE.pay);
      try { G.st = await call("apply", { index: st.legal[0].index }); G.myMoves++; } catch (e) { G.error = e.message; }
      G.busy = false; return advance();
    }
    paint(); return;
  }
  G.busy = true; G.thinking = true; G.thinkStart = performance.now(); paint();
  try {
    const mv = await call("botMove");
    G.lastBot = mv; G.botMoves.push(mv.ms); G.st = mv.state; G.thinking = false; stopTimer(); paint();
    await sleep(mv.search && mv.search.options && mv.search.options.length > 1 ? BOT_PAUSE.undecided : BOT_PAUSE.forced);
  } catch (e) { G.error = e.message; G.thinking = false; G.busy = false; stopTimer(); paint(); return; }
  G.busy = false; advance();
}
async function pick(index) {
  if (G.stage !== "playing" || G.busy || !G.st.decision_is_human || !G.st.legal.some(l => l.index === index)) return;
  G.busy = true; paint();
  try { G.st = await call("apply", { index }); G.myMoves++; G.error = null; } catch (e) { G.error = e.message; }
  G.busy = false; advance();
}
async function endGame(again) {
  try { await call("end"); } catch (e) { /* no game to end */ }
  stopTimer(); G.stage = "setup"; G.st = null; G.busy = false; G.thinking = false;
  if (again) start(); else draw();
}
const sleep = ms => new Promise(r => setTimeout(r, ms));
function stopTimer() { if (G.timer) clearInterval(G.timer); G.timer = 0; }
function tickTimer() {
  if (G.timer || !G.host) return;
  G.timer = setInterval(() => { const t = $("#vp-think", G.host); if (!t || !G.thinking) { stopTimer(); return; } t.textContent = ((performance.now() - G.thinkStart) / 1000).toFixed(1) + " s"; }, 100);
}

// ── rendering ───────────────────────────────────────────────────────────────
export async function render(container) {
  if (!$("#" + CSS_ID)) document.head.append(el("link", { id: CSS_ID, rel: "stylesheet", href: "app/play.css" }));
  G.host = container; bindKeys(); draw();
}
export function destroy() {
  stopTimer();
  if (G.boardEl && G.host && G.host.contains(G.boardEl)) RiftBoard.destroy();
  G.host = null; G.root = null; G.boardEl = null;
}
function draw() {
  if (!G.host) return;
  const E = data().engine;
  if (!E || !E.js) { G.host.innerHTML = `<div class="v-play">${pageHead({ title: "Play the bot" })}${empty("The in-browser engine is not part of this build", "build-wasm/riftbound.wasm was missing when the page was assembled. Rebuild the dashboard with the WebAssembly engine to play here.")}</div>`; return; }
  G.host.innerHTML = `<div class="v-play"></div>`; G.root = $(".v-play", G.host);
  if (G.stage === "setup" || G.stage === "loading") drawSetup(); else drawGame();
}

// ── setup ───────────────────────────────────────────────────────────────────
function drawSetup() {
  const c = G.cfg, ok = playable(), loading = G.stage === "loading";
  if (!ok.some(d => d.path === c.me)) c.me = ok.some(d => d.path === DEFAULTS.me) ? DEFAULTS.me : (ok[0] || {}).path;
  if (!ok.some(d => d.path === c.bot)) c.bot = ok.some(d => d.path === DEFAULTS.bot) ? DEFAULTS.bot : (ok[1] || ok[0] || {}).path;
  if (!LEVELS[c.level]) c.level = "quick";
  G.ui.group.me = G.ui.group.me || groupOf(c.me); G.ui.group.bot = G.ui.group.bot || groupOf(c.bot);
  const firstVisit = !store("rl.play.engineSeen") && !G.info;
  const r = G.result;
  G.root.innerHTML = `
    ${pageHead({ title: "Play the bot", sub: "A full game against the lab's bot, played in your browser. The bot looks at every legal move, redeals what it cannot see a few times and keeps the move with the best estimated win chance. It never sees your hand." })}
    <section class="panel gilt vp-matchup" id="vp-matchup" aria-label="matchup">${matchupHtml()}</section>
    <div class="grid2">
      ${pickerHtml("me", "Your deck")}
      ${pickerHtml("bot", "The bot's deck")}
    </div>
    <section class="panel">
      <h3>Game</h3>
      <div class="vp-fields">
        <div class="field">Your seat<div class="segmented" role="group" aria-label="seat">${Object.entries(SEATS).map(([k, v]) => `<button type="button" data-seat="${k}" aria-pressed="${c.seat === k}">${k === "random" ? "Random" : "P" + k}</button>`).join("")}</div><span class="f-help">${esc(SEATS[c.seat] || "")}${c.seat === "random" ? ": the seat is drawn when the game starts" : ""}.</span></div>
        <div class="field">Bot level<div class="segmented" role="group" aria-label="bot level">${Object.entries(LEVELS).map(([k, v]) => `<button type="button" data-level="${k}" aria-pressed="${c.level === k}">${esc(v.label)}</button>`).join("")}</div><span class="f-help" id="vp-level-help">${esc(LEVELS[c.level].help)}</span></div>
        <label class="f">Seed<input type="text" class="seed" id="vp-seed" value="${esc(c.seed)}" placeholder="random" inputmode="numeric" autocomplete="off"><span class="f-help">The same seed with the same decks and seat replays the same shuffle.</span></label>
        <div class="field">Rune costs<label class="switch"><input type="checkbox" id="vp-autopay" ${c.autopay ? "checked" : ""}><span>Pay automatically</span></label><span class="f-help">The engine pays rune by rune. On, those steps are answered for you; off, you choose which runes to exhaust.</span></div>
      </div>
    </section>
    <section class="panel ${loading ? "raised" : ""}">
      <div class="vp-start">
        <button type="button" class="primary btn-lg" id="vp-start" ${loading || !c.me || !c.bot ? "disabled" : ""}>${loading ? "Loading the engine…" : "Start"}</button>
        <span class="engine" id="vp-engine">${engineLine()}</span>
      </div>
      <div id="vp-prog" style="margin-top:12px">${progressHtml()}</div>
      ${!loading && (firstVisit || !G.info) ? `<p class="hint" style="margin-top:10px">The first start downloads the engine, about 10 MB, once. Your browser keeps it for the next games.</p>` : ""}
      ${G.error ? `<div style="margin-top:12px">${notice(esc(G.error), "error")}</div>` : ""}
      ${r ? `<div class="vp-last" style="margin-top:12px">${notice(`Last game: <b>${r.winner === G.seat ? "you won" : r.winner ? "the bot won" : "a draw"}</b> ${r.scores.join("–")} in ${plural(r.turns, "turn")}${G.lastHint ? ` — ${esc(G.lastHint)}` : ""}.`, r.winner === G.seat ? "ok" : "")}</div>` : ""}
    </section>
    <details class="panel" ${firstVisit ? "open" : ""}>
      <summary><h3 style="margin:0">How to play</h3></summary>
      <div class="vp-how">
        <div><h4>Your options</h4><p>Cards you can play and battlefields you can reach light up in gold on the table. The full list sits under the table; some options are not tied to a card (End turn, Pass, mulligan choices). Click an option or press its number; <span class="kbd">Enter</span> takes the first one.</p></div>
        <div><h4>Reading the table</h4><p>Hover a card to read it, tap on a phone. Your hand is at the bottom, the bot's is face down. The chain in the top bar lists spells and abilities waiting to resolve, last in first out.</p></div>
        <div><h4>The bot</h4><p>Its last move is explained in the side panel with the win chance it gave each option; cards it kept in hand are not named. A game is about 10 to 15 turns.</p></div>
      </div>
    </details>`;
  wireSetup();
}
function matchupHtml() {
  const side = (path, who, cls) => {
    const d = deckByPath(path); if (!d) return `<div class="vp-seat ${cls}"><div><div class="who">${who}</div><div class="name">No deck</div></div></div>`;
    const m = deckMeta(d), face = deckFace(d);
    return `<div class="vp-seat ${cls}">${cardImg(face || d.legend, { size: "sm", w: 300, alt: d.legend })}<div class="min0"><div class="who">${who}</div><div class="name">${esc(m.name)}</div><div class="sub">${esc(m.sub)}</div><div class="sub">${domainDots(deckDomains(d))} ${esc(legendShort(d.legend))} · ${esc(champOf(d))}</div></div></div>`;
  };
  return side(G.cfg.me, "You", "me") + `<div class="vp-vs" aria-hidden="true">VS</div>` + side(G.cfg.bot, "The bot", "bot");
}
function pickerHtml(side, title) {
  const groups = deckGroups(), cur = G.ui.group[side];
  return `<section class="panel vp-picker" data-side="${side}" aria-label="${esc(title)}">
    <h3>${esc(title)}</h3>
    <div class="segmented" role="group" aria-label="deck group">${Object.entries(groups).map(([k, ds]) => `<button type="button" data-group="${esc(k)}" aria-pressed="${k === cur}">${esc(k)} <span class="muted">${ds.length}</span></button>`).join("")}</div>
    <div class="vp-tiles" role="radiogroup" aria-label="${esc(title)}">${tilesHtml(side)}</div>
  </section>`;
}
function tilesHtml(side) {
  const ds = deckGroups()[G.ui.group[side]] || [], sel = G.cfg[side];
  if (!ds.length) return empty("No deck in this group");
  return ds.map(d => { const m = deckMeta(d), face = deckFace(d);
    return `<button type="button" class="vp-tile" role="radio" aria-checked="${d.path === sel}" data-pick-deck="${esc(d.path)}" title="${esc(deckLabel(d.path))}">${cardImg(face || d.legend, { size: "xs", w: 200, hover: false, alt: d.legend })}<span class="t-body"><span class="t-name">${esc(m.name)}</span><span class="t-sub">${esc(m.sub)}</span><span class="t-doms">${domainDots(deckDomains(d))}</span></span><span class="t-check" aria-hidden="true">✓</span></button>`; }).join("");
}
function engineLine() {
  const E = data().engine;
  if (G.info) return `Engine ${esc(G.info.version)} · ${num(G.info.cards)} cards · ${G.info.net ? "value net loaded" : "no value net: the bot uses a heuristic"}`;
  return E.net_source ? `Bot value net: ${esc(E.net_source.split("/").pop())}` : "";
}
function progressHtml() {
  if (G.stage !== "loading") return "";
  const E = data().engine, mb = x => (x / 1e6).toFixed(1);
  const row = (what, label) => { const p = G.progress[what]; const pct = p ? (p.total ? p.loaded / p.total : 1) : 0;
    return `<div class="lbl"><span>${label}</span><span class="mono">${p ? `${mb(p.loaded)}${p.total ? " / " + mb(p.total) : ""} MB` : "waiting…"}</span></div><div class="prog" role="progressbar" aria-valuenow="${Math.round(100 * pct)}" aria-valuemin="0" aria-valuemax="100" aria-label="${label}"><i style="width:${(100 * pct).toFixed(0)}%"></i></div>`; };
  return `<div class="vp-prog">${row("wasm", "Engine")}${E.net ? row("net", "Bot value net") : ""}<div class="hint">${G.info ? "Shuffling the decks…" : "Downloaded once and kept by your browser."}</div></div>`;
}
function paintProgress() { const p = G.host && $("#vp-prog", G.host); if (p) p.innerHTML = progressHtml(); }
function wireSetup() {
  const root = G.root, c = G.cfg, save = () => store("rl.play", c);
  root.addEventListener("click", e => {
    const t = e.target.closest("[data-pick-deck], [data-group], [data-seat], [data-level], #vp-start"); if (!t) return;
    if (t.dataset.pickDeck) {
      const side = t.closest(".vp-picker").dataset.side; c[side] = t.dataset.pickDeck; save();
      $$(".vp-tile", t.closest(".vp-tiles")).forEach(b => b.setAttribute("aria-checked", b === t));
      $("#vp-matchup", root).innerHTML = matchupHtml();
    } else if (t.dataset.group) {
      const pk = t.closest(".vp-picker"), side = pk.dataset.side; G.ui.group[side] = t.dataset.group;
      $$("[data-group]", pk).forEach(b => b.setAttribute("aria-pressed", b === t)); $(".vp-tiles", pk).innerHTML = tilesHtml(side);
    } else if (t.dataset.seat) {
      c.seat = t.dataset.seat; save(); const g = t.parentNode; $$("button", g).forEach(b => b.setAttribute("aria-pressed", b === t));
      g.nextElementSibling.textContent = `${SEATS[c.seat]}${c.seat === "random" ? ": the seat is drawn when the game starts" : ""}.`;
    } else if (t.dataset.level) {
      c.level = t.dataset.level; save(); $$("button", t.parentNode).forEach(b => b.setAttribute("aria-pressed", b === t)); $("#vp-level-help", root).textContent = LEVELS[c.level].help;
    } else if (t.id === "vp-start") start();
  });
  $("#vp-seed", root).addEventListener("change", e => { c.seed = e.target.value.trim(); save(); });
  $("#vp-autopay", root).addEventListener("change", e => { c.autopay = e.target.checked; save(); });
}

// ── game ────────────────────────────────────────────────────────────────────
function players() {
  const info = (path, label) => { const d = deckByPath(path); return { label, deck: d ? deckMeta(d).name : "", runes: d ? d.stats.rune_split : {}, champion: d ? champOf(d) : "" }; };
  const me = info(G.cfg.me, "you"), bot = info(G.cfg.bot, "bot");
  return G.seat === 1 ? [me, bot] : [bot, me];
}
function drawGame() {
  G.root.innerHTML = `<div class="vp-game" id="vp-game">
      <div class="vp-main">
        <div class="vp-bar" id="vp-bar" role="status" aria-live="polite"></div>
        <div id="vp-result"></div>
        <div class="vp-board" id="vp-board"></div>
        <section class="panel vp-opts" id="vp-opts" aria-label="your options"></section>
      </div>
      <aside class="vp-aside" id="vp-aside" aria-label="game panel"></aside>
    </div>`;
  G.boardEl = $("#vp-board", G.root);
  RiftBoard.mount(G.boardEl, { cards: data().cards, viewer: G.seat, hands: "viewer", onPick: idx => pick(idx), players: players() });
  G.root.addEventListener("click", e => {
    const opt = e.target.closest(".po-opt[data-pick]");
    if (opt) { e.stopPropagation(); /* the chip inside must not open the card preview on tap */ pick(+opt.dataset.pick); return; }
    const t = e.target.closest("#vp-sheet, #vp-concede, #vp-again, #vp-new, #vp-log-sum"); if (!t) return;
    if (t.id === "vp-sheet") { G.ui.sheet = !G.ui.sheet; $("#vp-game", G.root).classList.toggle("sheet-closed", !G.ui.sheet); t.textContent = G.ui.sheet ? "Hide" : "Show"; t.setAttribute("aria-expanded", G.ui.sheet); }
    else if (t.id === "vp-concede") concede();
    else if (t.id === "vp-again") endGame(true);
    else if (t.id === "vp-new") endGame(false);
    else if (t.id === "vp-log-sum") setTimeout(() => { G.ui.logOpen = t.parentNode.open; }, 0);
  });
  G.root.addEventListener("change", e => { if (e.target.id === "vp-autopay2") { G.autopay = e.target.checked; G.cfg.autopay = G.autopay; store("rl.play", G.cfg); if (G.autopay) advance(); } });
  paint();
}
async function concede() {
  const v = await modal({ title: "Concede this game?", body: `<p>The game ends and counts as a loss for you. The table is cleared and you are back at the setup.</p>`, actions: [{ label: "Keep playing", value: null }, { label: "Concede", value: true, primary: true }] });
  if (v) { await endGame(false); toast("Game conceded.", ""); }
}
/** Repaint the bar, options and side panel from the current state; render the table. */
function paint() {
  const st = G.st; if (!st || !G.host || !G.root) return;
  const game = $("#vp-game", G.root); if (!game) return;
  const over = G.stage === "over", human = !over && st.decision_is_human && !G.busy;
  const legal = !over && st.decision_is_human ? legalHints(st.legal) : [];
  const banner = over ? "Game over" : G.thinking ? "Bot is thinking…" : G.busy ? (st.paying_cost ? "Paying…" : st.decision_is_human ? "Passing…" : "…") : human ? (st.paying_cost ? "Pay the cost" : "Your move") : "";
  if (G.boardEl && document.contains(G.boardEl)) RiftBoard.render({ board: st.board, legal }, { viewer: G.seat, hands: over ? "both" : "viewer", banner, bannerKind: over ? "over" : human ? "me" : "bot", legalDisabled: !human, legalLabel: st.paying_cost ? "Pay with" : "Your options", players: players() });
  game.dataset.turn = over ? "over" : human ? "human" : G.thinking ? "bot" : "wait";
  game.classList.toggle("sheet-closed", !G.ui.sheet);
  $("#vp-bar", game).innerHTML = barHtml(st, over, human);
  $("#vp-result", game).innerHTML = over ? resultHtml() : "";
  const opts = $("#vp-opts", game); opts.hidden = over; if (!over) opts.innerHTML = optionsHtml(st, legal, human);
  $("#vp-aside", game).innerHTML = asideHtml(st, over);
  hidePreview();                                   // a hovered chip may have gone with the repaint
  if (G.thinking) tickTimer();
}
const phaseName = p => String(p || "").replace(/([a-z])([A-Z])/g, "$1 $2").toLowerCase();
function barHtml(st, over, human) {
  const me = G.seat, opp = 3 - me, b = st.board, sc = st.scores || [b.players[0].score, b.players[1].score];
  const faceMe = deckFace(G.cfg.me), faceBot = deckFace(G.cfg.bot);
  const who = over ? `<span class="who over">Game over</span>`
    : human ? `<span class="who me">${st.paying_cost ? "Pay the cost" : "Your move"}</span>`
    : G.thinking ? `<span class="who bot">Bot is thinking <span class="mono" id="vp-think">${((performance.now() - G.thinkStart) / 1000).toFixed(1)} s</span></span>`
    : `<span class="who wait">${st.paying_cost ? "Paying" : "One moment"}</span>`;
  return `<span class="turn"><b>Turn ${b.turn ?? "–"}</b><span>${esc(phaseName(b.phase))}${b.turn_player ? ` · ${b.turn_player === me ? "your" : "the bot's"} turn` : ""}</span></span>${who}
    <span class="seats">${faceMe ? portrait(faceMe, { size: "sm" }) : ""}<span class="lbl"><b>You</b> P${me}</span><span class="sc p${me}">${sc[me - 1] ?? 0}</span><span class="dash">–</span><span class="sc p${opp}">${sc[opp - 1] ?? 0}</span><span class="lbl">P${opp} <b>Bot</b></span>${faceBot ? portrait(faceBot, { size: "sm" }) : ""}</span>`;
}
function optionsHtml(st, legal, human) {
  const title = human && st.paying_cost ? "Pay with" : "Your options";
  const autoPay = st.decision_is_human && st.paying_cost && G.autopay;
  const state = human ? (st.paying_cost ? "pick the rune to exhaust" : plural(legal.length, "option")) : G.thinking ? "the bot is choosing" : autoPay ? "paying the cost for you" : st.paying_cost ? "the bot is paying" : G.busy ? "one moment" : "waiting for the engine";
  const list = legal.length && !autoPay ? `<div class="po-list">${legal.map((e, i) => `<button type="button" class="po-opt ${e.card || e.battlefield ? "hot" : ""}" data-pick="${e.index}" ${human ? "" : "disabled"}>${i < 9 ? `<kbd class="kbd">${i + 1}</kbd>` : `<kbd class="kbd blank">·</kbd>`}<span class="po-text">${chipText(e.text || "option " + e.index)}</span></button>`).join("")}</div>`
    : `<div class="po-none">${G.thinking ? "The bot is thinking. Your options appear here when it is your move." : autoPay ? "Rune costs are paid for you; turn that off in the side panel to pick the runes yourself." : "Nothing to choose right now."}</div>`;
  return `<header class="po-head"><h3>${title}</h3><span class="state">${esc(state)}</span><span class="keys"><span class="kbd">1</span>–<span class="kbd">9</span> pick · <span class="kbd">Enter</span> first</span><button type="button" class="btn-sm po-toggle" id="vp-sheet" aria-expanded="${G.ui.sheet}">${G.ui.sheet ? "Hide" : "Show"}</button></header>${list}
    ${human && st.paying_cost ? `<p class="hint" style="margin-top:8px">The engine pays costs rune by rune. Turn automatic payment on in the side panel to skip these steps.</p>` : ""}`;
}
function asideHtml(st, over) {
  const lb = G.lastBot, sh = lb && lb.search, shares = (sh && sh.options || []).slice().sort((a, b) => b.score - a.score), max = Math.max(1e-9, ...shares.map(o => o.score));
  const ms = G.botMoves, avg = ms.length ? ms.reduce((a, b) => a + b, 0) / ms.length : 0, mx = ms.length ? Math.max(...ms) : 0;
  const last = lb ? `<div class="lb-move">${chipText(lb.text || "option " + lb.index)}</div>
      <div class="lb-meta">${lb.ms.toFixed(0)} ms${sh && sh.worlds ? ` · ${plural(sh.worlds, "redeal")}` : ""}${lb.options > 1 ? ` · chose among ${lb.options}` : " · the only option"}</div>
      ${shares.length > 1 ? `<div class="shares">${shares.slice(0, 8).map(o => `<div class="share ${o.index === lb.index ? "chosen" : ""}"><span class="txt">${chipText(o.text || "option " + o.index, "sm")}</span><span class="bar"><i style="width:${(100 * o.score / max).toFixed(0)}%"></i></span><span class="pc">${(100 * o.score).toFixed(0)}%</span></div>`).join("")}</div><p class="hint" style="margin-top:8px">Estimated win chance of each move; cards the bot kept in hand are not named.</p>` : ""}`
    : `<p class="hint">${over ? "The bot did not move." : G.seat === 1 ? "You start. The bot's moves are explained here." : "The bot starts. Its moves are explained here."}</p>`;
  return `<section class="panel"><h3>Bot's last move</h3>${last}</section>
    <section class="panel"><h3>This game</h3>
      <div class="counters">${chip(`your moves <b>${G.myMoves}</b>`)}${chip(`bot moves <b>${ms.length}</b>`)}${ms.length ? chip(`bot avg <b>${avg.toFixed(0)} ms</b> · max <b>${mx.toFixed(0)} ms</b>`, "mono") : ""}${chip(`you are <b>P${G.seat}</b>`)}${chip(`bot <b>${esc(LEVELS[G.level].label.toLowerCase())}</b>`)}${chip(`seed ${G.seed}`, "mono")}</div>
      ${G.error ? `<div style="margin-top:10px">${notice(esc(G.error), "error")}</div>` : ""}
      ${over ? "" : `<div class="vp-actions"><label class="switch" style="margin:0"><input type="checkbox" id="vp-autopay2" ${G.autopay ? "checked" : ""}><span>Pay rune costs automatically</span></label></div>
      <div class="vp-actions"><button type="button" class="btn-danger" id="vp-concede">Concede</button><span class="hint">Keys: <span class="kbd">1</span>–<span class="kbd">9</span> pick an option, <span class="kbd">Enter</span> the first one, <span class="kbd">Esc</span> closes a card.</span></div>`}
    </section>
    ${G.log.length ? `<details class="panel" ${G.ui.logOpen ? "open" : ""}><summary id="vp-log-sum"><h3 style="margin:0">Engine log</h3><span class="n">${num(G.log.length)} lines</span></summary><div class="log">${G.log.slice(-80).map(esc).join("\n")}</div></details>` : ""}`;
}
/** One line on what decided the game, from the points tally when it says something, else the engine's reason. */
function decidingLine(r) {
  if (!r) return "";
  const w = r.winner, l = 3 - w, who = p => p === G.seat ? "You" : "The bot", whose = p => p === G.seat ? "your" : "the bot's";
  const reason = String(r.reason || "");
  if (!w) return /max turns/i.test(reason) ? "The turn limit was reached with the scores level." : "Neither side reached 8 points.";
  if (/conceded/i.test(reason)) return `${who(l)} conceded.`;
  if (/burn-out/i.test(reason)) return `${who(w)} won on the burn-out point: ${whose(l)} deck ran out of cards.`;
  const T = G.tally || {}, tw = T[w] || {}, tl = T[l] || {};
  if ((tw.conquer || 0) > (tl.conquer || 0)) return `${who(w)} conquered more: ${tw.conquer} points from conquests against ${tl.conquer || 0}.`;
  if ((tw.hold || 0) > (tl.hold || 0)) return `${who(w)} held battlefields longer: ${tw.hold} points from holds against ${tl.hold || 0}, conquests level at ${tw.conquer || 0}.`;
  if ((tw.effect || 0) > (tl.effect || 0)) return `Card effects made the difference: ${tw.effect} points for ${who(w).toLowerCase()} against ${tl.effect || 0}.`;
  if (/max turns/i.test(reason)) return `The turn limit was reached with ${who(w).toLowerCase()} ahead.`;
  return reason ? reason.replace(/^P(?:layer)?([12])\b/, (_, p) => who(+p)).replace(/\.?$/, ".") : "";
}
function resultHtml() {
  const r = G.result; if (!r) return notice("The game is over but the engine gave no result.", "error");
  const won = r.winner === G.seat, draw = !r.winner, me = G.seat, opp = 3 - me;
  const hint = decidingLine(r);
  const deck = (path, who, seat, winner) => {
    const d = deckByPath(path); if (!d) return "";
    const k = keyCards(d);
    return `<div class="r-deck ${winner ? "winner" : ""}"><div class="r-who">${who} · P${seat}${winner ? " · winner" : ""}</div><div class="r-name">${esc(deckMeta(d).name)}</div>
      <div class="cards">${k.legend && k.legend !== k.champion ? cardImg(k.legend, { size: "sm", w: 300, cls: "key", alt: d.legend }) : ""}${k.champion ? cardImg(k.champion, { size: "sm", w: 300, cls: "key" }) : ""}${k.main.map(x => cardImg(x.c || x.n, { size: "sm", w: 200, count: x.k, alt: x.n })).join("")}</div></div>`;
  };
  const ms = G.botMoves, avg = ms.length ? ms.reduce((a, b) => a + b, 0) / ms.length : 0;
  return `<section class="panel gilt vp-result" aria-label="result">
    <div><div class="eyebrow">Game over</div><h2 class="r-title ${won ? "win" : draw ? "draw" : "loss"}">${won ? "Victory" : draw ? "Draw" : "Defeat"}</h2>
      <div class="r-score"><small>You</small><span class="p${me}">${r.scores[me - 1]}</span><span class="dash">–</span><span class="p${opp}">${r.scores[opp - 1]}</span><small>Bot</small></div></div>
    ${hint ? `<p class="r-hint">${esc(hint)}</p>` : ""}
    <div class="r-decks">${deck(G.cfg.me, "You", me, won)}<div class="vp-vs" aria-hidden="true">VS</div>${deck(G.cfg.bot, "The bot", opp, !draw && !won)}</div>
    <div class="r-facts">${chip(`<b>${plural(r.turns, "turn")}</b>`)}${chip(`${plural(r.decisions, "decision")}`)}${chip(`your moves <b>${G.myMoves}</b>`)}${chip(`bot moves <b>${ms.length}</b>${ms.length ? `, ${avg.toFixed(0)} ms each` : ""}`)}${chip(`bot ${esc(LEVELS[G.level].label.toLowerCase())}`)}${chip(`seed ${G.seed}`, "mono")}</div>
    <div class="r-actions"><button type="button" class="primary" id="vp-again">Play again</button><button type="button" id="vp-new">New game</button></div>
    <p class="hint">Both hands are face up on the table below.</p>
  </section>`;
}

// ── keyboard: 1–9 pick an option, Enter the first one ───────────────────────
function bindKeys() {
  if (keysBound) return; keysBound = true;
  document.addEventListener("keydown", e => {
    if (!G.host || G.stage !== "playing" || G.busy || !G.st || !G.st.decision_is_human || e.altKey || e.ctrlKey || e.metaKey) return;
    if (/^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName) || e.target.isContentEditable || $(".modal-backdrop")) return;
    const L = G.st.legal; let k = -1;
    if (/^[1-9]$/.test(e.key)) k = +e.key - 1; else if (e.key === "Enter" && !e.target.closest("button, a, [data-card], [data-z]")) k = 0;
    if (k >= 0 && L[k]) { e.preventDefault(); pick(L[k].index); }
  });
}
