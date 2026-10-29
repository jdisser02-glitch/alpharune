/* Riven Lab — Replays: a library of recorded games and a viewer that steps through one decision by decision.
   Library (#/replays, ?test=<id>): every game as a card (legends, decks, result, turns; agreement for real games),
   filtered by test, real/bot, opponent legend and result. Viewer (#/replays/<file>): the table (RiftBoard) full width,
   a transport bar with autoplay, a turn timeline, and the decision panel: options with card chips and the bot's search
   shares, "played" / "bot" marks for real games, "me" votes and comments saved with the claude.ai db.
   Real games (lab/human_replays, RiftLite logs replayed through the engine) add the comparison strip and the About panel.
   Router contract: render(container, params, ctx) · update(container, params, ctx) · destroy(). Styles: app/replays.css (.v-replays). */
import { data, esc, $, $$, store, pct, num, fmtDate, badge, chip, pageHead, empty, loadingBox, errorBox, notice, portrait, cardChip, cardImg, cardGrid,
         legendShort, legendFace, deckByPath, deckLabel, deckShort, deckFace, testById, testsByDate, runtime, domainDot, table, enhance, toast, rateBar,
         cssOnce, botSpec, botBadge, findCards, chipText } from "./lib.js";

// ── state (module scope: survives navigation within the view) ───────────────
const st = {
  file: null, rep: null, error: null, frame: 0, viewer: 1, hands: "both",
  playing: false, speed: 1, timer: 0,
  filter: { kind: "all", test: "", legend: "", result: "any" },
  db: null, meId: "", annotations: new Map(), rtStarted: false,
};
let host = null, keyHandler = null, loadToken = 0, boardMounted = false;
const SPEEDS = [0.5, 1, 2, 4];

// ── lifecycle ───────────────────────────────────────────────────────────────
const CSS = new URL("./replays.css", import.meta.url).href;
export async function render(container, params = {}) {
  await cssOnce(CSS);
  if (!keyHandler) { keyHandler = onKey; document.addEventListener("keydown", keyHandler); }
  ensureRuntime();
  return update(container, params);
}
export function update(container, params = {}) {
  host = container; container.classList.add("v-replays");
  if (params.file) { if (params.file !== st.file || (!st.rep && !st.error)) return openGame(params.file); return renderViewer(); }
  // ?test=<id> narrows to one test; ?test=human (the Bot screen's link) means the real games
  if (params.test === "human") { st.filter.kind = "human"; st.filter.test = ""; } else st.filter.test = params.test || "";
  stopAutoplay(); return renderLibrary();
}
export function destroy() {
  stopAutoplay();
  if (keyHandler) document.removeEventListener("keydown", keyHandler); keyHandler = null;
  unmountBoard(); host.classList.remove("v-replays"); host = null;
}
function unmountBoard() { if (boardMounted && window.RiftBoard) { try { RiftBoard.destroy(); } catch (e) { /* already gone */ } } boardMounted = false; }
async function ensureRuntime() {
  if (st.rtStarted) return; st.rtStarted = true;
  const { db, meId } = await runtime(); st.db = db; st.meId = meId || "";
  if (db) { try { const snap = await db.collection("annotations").get(); snap.docs.forEach(d => { const v = d.data(); st.annotations.set(v.replay + "|" + v.frame, v); }); } catch (e) { /* read-only visitor */ } }
  if (st.rep && host && host.isConnected && $("#decision", host)) renderFrame();
}

// ── game metadata helpers ───────────────────────────────────────────────────
const metaOf = file => (data().replays || []).find(r => r.file === file) || null;
const humanGameOf = file => ((data().human_review || {}).games || []).find(g => g.replay === file) || null;
const isCompared = f => !!(f && f.human_known && f.agree != null);
const isReal = f => !!(f && ((f.search && f.search.length > 1) || (!f.search && f.options && f.options.length > 1)));
const phaseName = p => String(p || "").replace(/([a-z])([A-Z])/g, "$1 $2").toLowerCase();
/** Prose name of a player: "the bot (40 continuations per move, network 0)", "a random player", a human's name. */
const agentLabel = a => botSpec(a).text;
/** Both players in one line: "the bot (…) on both sides" or "the bot (…) as P1 against a random player". */
const sidesLine = (a, b, seatA) => a === b ? `${agentLabel(a)} on both sides` : `${agentLabel(a)}${seatA ? ` as P${seatA}` : ""} against ${agentLabel(b)}`;
const shortLegend = legendShort, shortDeck = deckShort;
/** Seat of the side the game is about: the list under test (bot games) or the captured player (real games). */
function featuredSeat(meta) { if (!meta) return 1; if (meta.human) return meta.capture_seat || 1; const m = /seat(\d)/.exec(meta.seat || ""); return m ? +m[1] : 1; }
/** Both sides of a game as {seat, name, legend, face, deck, agent}, featured first. */
function sides(meta) {
  const fs = featuredSeat(meta);
  const side = p => {
    const agent = p === 1 ? meta.agent1 : meta.agent2;
    if (meta.human) {
      const legend = (meta.legends || {})["P" + p] || "", player = (meta.players || [])[p - 1] || "P" + p;
      return { seat: p, name: `${player} (${shortLegend(legend)})`, player, legend: shortLegend(legend), legendName: legend, face: legendFace(legend), deck: `${shortLegend(legend)} · inferred list`, agent };
    }
    const path = p === 1 ? meta.deck1 : meta.deck2, d = deckByPath(path);
    return { seat: p, name: shortDeck(path), player: shortDeck(path), legend: shortLegend(d ? d.legend : ""), legendName: d ? d.legend : "", face: deckFace(path), deck: deckLabel(path), agent };
  };
  return { featured: side(fs), opp: side(3 - fs), fs };
}
/** "won" | "lost" | "draw" from the featured side's point of view, and its scores first. */
function resultOf(meta) {
  const fs = featuredSeat(meta); let sc = meta.scores || [], winner = meta.winner, source = "";
  if (meta.human) {   // the real result is the log's; the engine's score is where the replay stopped
    const hg = humanGameOf(meta.file), ls = hg && hg.log_scores;
    if (ls && ls.length === 2) { sc = ls; winner = ls[0] === ls[1] ? 0 : ls[0] > ls[1] ? 1 : 2; source = "final score from the RiftLite log"; }
  }
  return { outcome: !winner ? "draw" : winner === fs ? "won" : "lost", scores: sc.length === 2 ? [sc[fs - 1], sc[2 - fs]] : sc, source };
}
const resultBadge = meta => { const r = resultOf(meta); return `<span title="${esc(r.source)}">${badge(`${r.outcome}${r.scores.length ? " " + r.scores.join("–") : ""}`, r.outcome === "won" ? "win" : r.outcome === "lost" ? "loss" : "neutral")}</span>`; };
const gameDate = meta => meta.human ? meta.date : (testById(meta.test_id) || {}).started_utc;
/** The library URL for the current filter: ?test=<id>, or ?test=human for the real games alone. */
const qs = () => st.filter.test ? `?test=${encodeURIComponent(st.filter.test)}` : st.filter.kind === "human" ? "?test=human" : "";
const optionHtml = text => chipText(text, { cls: "sm" });
/** The move a frame ended with, as RiftBoard focus entries (cards + battlefield named by the option). */
function frameFocus(f) {
  const i = f.human_known && f.human_choice != null && f.human_choice >= 0 ? f.human_choice : f.chosen;
  const text = (f.options || [])[i] || f.chosen_text || ""; if (!text) return [];
  return findCards(text).map(x => x.card.type === "battlefield" ? { battlefield: x.card.name } : { card: x.card.name });
}

// ── library ─────────────────────────────────────────────────────────────────
function renderLibrary() {
  unmountBoard();
  const D = data(), all = D.replays || [], f = st.filter;
  if (!all.length) { host.innerHTML = pageHead({ title: "Replays", sub: "Recorded games, decision by decision." }) + empty("No game recorded yet", `Run a test in <a href="#/launch">Launch</a>: its games show up here.`); return; }
  const legends = [...new Set(all.map(r => sides(r).opp.legend).filter(Boolean))].sort();
  const testIds = testsByDate().map(t => t.id).filter(id => all.some(r => r.test_id === id));
  for (const r of all) if (!r.human && r.test_id && !testIds.includes(r.test_id)) testIds.push(r.test_id);
  const shown = all.filter(r => (f.kind === "all" || (f.kind === "human") === !!r.human) && (!f.test || r.test_id === f.test)
    && (!f.legend || sides(r).opp.legend === f.legend) && (f.result === "any" || resultOf(r).outcome === f.result));
  const humans = shown.filter(r => r.human).sort((a, b) => String(b.date).localeCompare(String(a.date)) || (a.game || 0) - (b.game || 0));
  const byTest = new Map(); shown.filter(r => !r.human).forEach(r => { const k = r.test_id || ""; if (!byTest.has(k)) byTest.set(k, []); byTest.get(k).push(r); });
  const order = [...testIds, ""].filter(k => byTest.has(k));
  const hr = D.human_review || {};
  const testOpt = id => { const t = testById(id); return `<option value="${esc(id)}" ${f.test === id ? "selected" : ""}>${esc(t ? (t.description || t.name) : id)}</option>`; };

  host.innerHTML = `
    ${pageHead({ title: "Replays", sub: `Every recorded game, decision by decision. Real games from RiftLite come with the bot's opinion at each move of the captured player; bot games show what the bot weighed before each move. Results are from the point of view of the list under test, or of the captured player.` })}
    <div class="panel v-replays-filters">
      <div class="toolbar">
        <div class="segmented" id="rf-kind" role="group" aria-label="kind of game">${[["all", "All games"], ["human", "Real games"], ["bot", "Bot games"]].map(([k, l]) => `<button data-kind="${k}" aria-pressed="${f.kind === k}">${l}</button>`).join("")}</div>
        <label class="f" style="flex:1 1 240px">Test<select id="rf-test"><option value="">All tests</option>${testIds.map(testOpt).join("")}</select></label>
        <label class="f">Opponent legend<select id="rf-legend"><option value="">Any</option>${legends.map(l => `<option value="${esc(l)}" ${f.legend === l ? "selected" : ""}>${esc(l)}</option>`).join("")}</select></label>
        <label class="f">Result<select id="rf-result"><option value="any">Any</option><option value="won" ${f.result === "won" ? "selected" : ""}>Won</option><option value="lost" ${f.result === "lost" ? "selected" : ""}>Lost</option></select></label>
        <span class="small muted rf-count">${num(shown.length)} of ${num(all.length)} games</span>
        ${f.test || f.legend || f.result !== "any" || f.kind !== "all" ? `<button class="btn-sm" id="rf-clear">Clear filters</button>` : ""}
      </div>
    </div>
    ${!shown.length ? empty("No game matches", `Clear a filter, or run a test in <a href="#/launch">Launch</a>: its games show up here.`) : ""}
    ${humans.length ? `<section class="section v-replays-group" aria-labelledby="rg-human">
      <div class="section-head"><h2 id="rg-human">Real games</h2><span class="small muted">${humans.length} from RiftLite, replayed through the engine and compared with ${esc(agentLabel(hr.agent))}</span></div>
      <p class="small muted">At each move of the captured player the bot was asked what it would do. Agreement is how often its first choice was the move the human made; with 5 to 20 legal moves a random player agrees around 10 to 20%.</p>
      <div class="v-replays-games">${humans.map(gameCard).join("")}</div></section>` : ""}
    ${order.map(id => {
      const t = testById(id), games = byTest.get(id).sort((a, b) => String(a.matchup).localeCompare(String(b.matchup)) || String(a.seat).localeCompare(String(b.seat)) || (a.game || 0) - (b.game || 0));
      const title = t ? (t.description || t.name) : (id || "Other games");
      return `<section class="section v-replays-group" aria-label="${esc(title)}">
        <div class="section-head"><h2>${esc(title)}</h2>${t ? `<a class="more" href="#/tests/${encodeURIComponent(id)}">Test details →</a>` : ""}</div>
        <div class="row tight small muted">${t && t.description ? `<span class="mono xs faint">${esc(t.name)}</span>` : ""}${t ? `<span>${esc(sidesLine(t.agent, t.opp_agent || t.agent))}</span><span>${esc(fmtDate(t.started_utc))}</span>` : ""}<span>${games.length} of the test's games recorded</span></div>
        <div class="v-replays-games">${games.map(gameCard).join("")}</div></section>`; }).join("")}`;

  $$("#rf-kind button", host).forEach(b => b.onclick = () => { f.kind = b.dataset.kind; history.replaceState(null, "", "#/replays" + qs()); renderLibrary(); });
  $("#rf-test", host).onchange = e => { f.test = e.target.value; history.replaceState(null, "", "#/replays" + qs()); renderLibrary(); };
  $("#rf-legend", host).onchange = e => { f.legend = e.target.value; renderLibrary(); };
  $("#rf-result", host).onchange = e => { f.result = e.target.value; renderLibrary(); };
  const clear = $("#rf-clear", host); if (clear) clear.onclick = () => { Object.assign(f, { kind: "all", test: "", legend: "", result: "any" }); history.replaceState(null, "", "#/replays"); renderLibrary(); };
}
function gameCard(r) {
  const s = sides(r), res = resultOf(r), when = gameDate(r);
  const agree = r.human && r.agreement_pct != null ? r.agreement_pct / 100 : null;
  return `<a class="v-replays-game ${res.outcome}" href="#/replays/${encodeURIComponent(r.file)}">
    <div class="g-head">
      <span class="versus">${portrait(s.featured.face || s.featured.legendName, { size: "lg" })}<span class="vs">vs</span>${portrait(s.opp.face || s.opp.legendName, { size: "lg" })}</span>
      <span class="g-badges">${r.human ? badge("real game", "gold") : badge("bot game", "bot")}${resultBadge(r)}</span>
    </div>
    <div class="g-title">${esc(s.featured.name)} <span class="g-vs">vs</span> ${esc(s.opp.name)}</div>
    <div class="g-sub">${r.human ? `${esc(s.featured.player)} captured the game · seat P${s.fs}` : `${esc(sidesLine(s.featured.agent, s.opp.agent, s.fs))}${r.game ? ` · game ${r.game}` : ""}`}</div>
    <div class="g-foot">
      ${agree != null ? `<span class="g-agree" title="how often the bot's first choice was the human's move"><span class="bar"><i style="width:${(100 * agree).toFixed(0)}%"></i></span><b class="mono">${pct(agree, 0)}</b><span class="xs muted">bot agrees · ${r.decisions_compared || 0} compared</span></span>` : ""}
      <span class="spacer"></span>
      <span class="xs muted mono">${r.turns != null ? `${r.turns} turns` : ""} · ${num(r.decisions || r.frames || 0)} decisions${when ? ` · ${esc(fmtDate(when))}` : ""}</span>
    </div></a>`;
}

// ── viewer ──────────────────────────────────────────────────────────────────
async function openGame(file) {
  const my = ++loadToken; stopAutoplay();
  Object.assign(st, { file, rep: null, error: null, frame: 0 });
  renderViewer();
  try {
    const r = await fetch(file); if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const rep = await r.json(); if (my !== loadToken) return; st.rep = rep;
  } catch (e) { if (my !== loadToken) return; st.error = `Could not load this game (${e.message || e}). It may have been dropped from the published set: rebuild the dashboard.`; renderViewer(); return; }
  const frames = st.rep.frames || [], meta = metaOf(file);
  st.viewer = featuredSeat(meta) || (st.rep.capture_seat || 1);
  st.frame = Math.max(0, st.rep.guided ? frames.findIndex(isCompared) : frames.findIndex(isReal));
  window.scrollTo(0, 0);
  renderViewer();
}
function playerInfo(rep, p) {
  const hg = rep.guided && humanGameOf(st.file);
  if (hg) {
    const dk = ((hg.summary || {}).decks || {})["P" + p] || {}, runes = {};
    for (const [n, k] of Object.entries((dk.list && dk.list.runes) || {})) runes[n.toLowerCase().replace(/ rune$/, "")] = k;
    return { label: (hg.players || [])[p - 1] || "P" + p, deck: `${shortLegend(dk.legend || (hg.legends || {})["P" + p])} · inferred list`, runes, champion: dk.champion || "" };
  }
  const path = p === 1 ? rep.deck1 : rep.deck2, d = deckByPath(path), agent = p === 1 ? rep.agent1 : rep.agent2;
  return { label: /random/.test(agent || "") ? "random" : "bot", deck: deckShort(path), runes: d ? d.stats.rune_split : {}, champion: d ? Object.keys(d.list.champion || {})[0] : "" };
}
const ICON = {
  first: `<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M3 2v12" class="s"/><path d="M13 2 5 8l8 6z"/></svg>`,
  prev: `<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M12 2 4 8l8 6z"/></svg>`,
  play: `<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M4 2l10 6-10 6z"/></svg>`,
  pause: `<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M4 2h3v12H4zM9 2h3v12H9z"/></svg>`,
  next: `<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M4 2l8 6-8 6z"/></svg>`,
  last: `<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M13 2v12" class="s"/><path d="M3 2l8 6-8 6z"/></svg>`,
};
/** Turns as segments over the frames: [{turn, player, start, count}]. */
function turnSegments(frames) {
  const segs = [];
  frames.forEach((f, i) => { const b = f.board || {}, last = segs[segs.length - 1]; if (last && last.turn === b.turn) last.count++; else segs.push({ turn: b.turn, player: b.turn_player, start: i, count: 1 }); });
  return segs;
}
/** Frames where a player scored: [{i, player}]. */
function scoreEvents(frames) {
  const out = [];
  for (let i = 1; i < frames.length; i++) { const a = (frames[i - 1].board || {}).players || [], b = (frames[i].board || {}).players || []; for (let p = 0; p < 2; p++) if (b[p] && a[p] && (b[p].score || 0) > (a[p].score || 0)) out.push({ i, player: p + 1 }); }
  return out;
}
function renderViewer() {
  const meta = metaOf(st.file), hg = humanGameOf(st.file), rep = st.rep, frames = (rep && rep.frames) || [];
  const s = meta ? sides(meta) : null, n = frames.length, ready = rep && !st.error && n > 0;
  const title = s ? `${esc(s.featured.name)} <span class="vs-word">vs</span> ${esc(s.opp.name)}` : esc(String(st.file || "").split("/").pop());
  const seatLabel = p => s ? (p === s.featured.seat ? s.featured : s.opp).player : "P" + p;
  const t = meta && !meta.human ? testById(meta.test_id) : null;
  const metaRow = meta ? [meta.human ? badge("real game", "gold") : badge("bot game", "bot"), resultBadge(meta),
    chip(`${meta.turns != null ? meta.turns : "?"} turns`), chip(`${num(n || meta.decisions || 0)} decisions`),
    rep && rep.seed != null && !meta.human ? chip(`seed <b class="mono">${esc(rep.seed)}</b>`) : "",
    t ? `<a class="chip" href="#/tests/${encodeURIComponent(t.id)}" title="${esc(t.description || "")}">test <b>${esc(t.name)}</b> →</a>` : "",
    hg && hg.source_url ? `<a class="chip" href="${esc(hg.source_url)}" target="_blank" rel="noopener">RiftLite replay ↗</a>` : "",
    gameDate(meta) ? `<span class="small muted">${esc(fmtDate(gameDate(meta)))}</span>` : ""].filter(Boolean).join("") : "";
  host.innerHTML = `<div class="v-replays-viewer">
    <nav class="v-replays-crumb"><a href="#/replays${qs()}">← All games</a>${st.filter.test ? `<span class="muted small">filtered by test</span>` : ""}</nav>
    <header class="v-replays-head">
      <div class="vh-vs">
        ${s ? `<span class="versus">${portrait(s.featured.face || s.featured.legendName, { size: "xl" })}<span class="vs">vs</span>${portrait(s.opp.face || s.opp.legendName, { size: "xl" })}</span>` : ""}
        <div class="vh-text"><div class="eyebrow">${meta && meta.human ? "Real game, replayed through the engine" : "Bot game"}</div><h1>${title}</h1><div class="vh-meta">${metaRow}</div></div>
      </div>
      ${ready ? `<div class="vh-controls">
        <label class="f">Seat at the bottom<div class="segmented" id="v-seat">${[1, 2].map(p => `<button data-seat="${p}" aria-pressed="${st.viewer === p}"><span class="badge p${p}">P${p}</span> ${esc(seatLabel(p))}</button>`).join("")}</div></label>
        <label class="f">Hands<div class="segmented" id="v-hands"><button data-hands="both" aria-pressed="${st.hands === "both"}">Both shown</button><button data-hands="viewer" aria-pressed="${st.hands === "viewer"}">Bottom seat only</button></div></label>
      </div>` : ""}
    </header>
    ${!rep && !st.error ? loadingBox("Loading the game…") : st.error ? errorBox(st.error) : !n ? empty("This record has no decisions", "The game ended before the first choice was recorded.") : `
    <section class="v-replays-stage" aria-label="table and transport">
      <div id="rb-host"></div>
      <div class="v-replays-transport">
        <div class="tr-buttons" role="group" aria-label="transport">
          <button class="tr-btn" id="t-first" title="First decision (Home)" aria-label="first decision">${ICON.first}</button>
          <button class="tr-btn" id="t-prev" title="Previous decision (←)" aria-label="previous decision">${ICON.prev}</button>
          <button class="tr-btn tr-play" id="t-play" title="Play / pause (Space)" aria-label="play" aria-pressed="false">${ICON.play}</button>
          <button class="tr-btn" id="t-next" title="Next decision (→)" aria-label="next decision">${ICON.next}</button>
          <button class="tr-btn" id="t-last" title="Last decision (End)" aria-label="last decision">${ICON.last}</button>
          <select id="t-speed" aria-label="autoplay speed" title="autoplay speed">${SPEEDS.map(x => `<option value="${x}" ${st.speed === x ? "selected" : ""}>${x}×</option>`).join("")}</select>
        </div>
        <div class="tr-slider"><input type="range" id="t-range" min="0" max="${n - 1}" value="${st.frame}" aria-label="decision"><div class="tr-ticks" aria-hidden="true">${turnSegments(frames).filter(sg => sg.turn > 0).map(sg => `<i style="left:${(100 * sg.start / Math.max(1, n - 1)).toFixed(2)}%">${sg.count / n >= 0.045 ? `<span>${sg.turn}</span>` : ""}</i>`).join("")}</div></div>
        <div class="tr-pos mono" id="t-pos"></div>
        <div class="tr-jumps">${hg
          ? `<button class="btn-sm" id="j-cmp" title="next decision where the bot was asked">Next compared</button><button class="btn-sm" id="j-dis" title="next decision where the bot would have played differently (D)">Next disagreement</button><button class="btn-sm" id="j-des" title="next point where the engine lost the log">Next desync</button>`
          : `<button class="btn-sm" id="j-real" title="next decision with more than one option in the bot's search (D)">Next real decision</button>`}</div>
      </div>
      <div class="v-replays-now" id="t-now"></div>
      <div class="v-replays-timeline" id="t-tl" aria-label="turn timeline"></div>
    </section>
    <div class="v-replays-below">
      <section class="panel v-replays-decision" id="decision" aria-live="polite"></section>
      <aside class="stack v-replays-aside">
        ${hg ? `<section class="panel v-replays-cmp" id="cmp" aria-label="comparison so far"></section>` : ""}
        ${explainPanel(!!hg)}
        <section class="panel" id="take"></section>
        <div id="before"></div>
        <p class="xs muted v-replays-keys"><span class="kbd">←</span> <span class="kbd">→</span> step · <span class="kbd">Home</span> <span class="kbd">End</span> first / last · <span class="kbd">Space</span> play · <span class="kbd">D</span> ${hg ? "next disagreement" : "next real decision"} · hover or tap a card to read it</p>
      </aside>
    </div>
    ${hg ? aboutPanel(hg, frames) : ""}`}
  </div>`;
  if (!ready) return;
  const go = k => { st.frame = Math.max(0, Math.min(n - 1, k)); renderFrame(); };
  $("#t-first", host).onclick = () => go(0); $("#t-last", host).onclick = () => go(n - 1);
  $("#t-prev", host).onclick = () => go(st.frame - 1); $("#t-next", host).onclick = () => go(st.frame + 1);
  $("#t-play", host).onclick = toggleAutoplay;
  $("#t-speed", host).onchange = e => { st.speed = +e.target.value; if (st.playing) { stopAutoplay(); startAutoplay(); } };
  $("#t-range", host).oninput = e => go(+e.target.value);
  if (hg) {
    const ds = desyncSet(hg);
    $("#j-cmp", host).onclick = () => jump(isCompared, "No later compared decision.");
    $("#j-dis", host).onclick = () => jump(f => isCompared(f) && !f.agree, "No later disagreement.");
    $("#j-des", host).onclick = () => jump(f => ds.has(f.n), "No later desync.");
  } else $("#j-real", host).onclick = () => jump(isReal, "No later decision with a real choice.");
  $$("#v-seat button", host).forEach(b => b.onclick = () => { st.viewer = +b.dataset.seat; $$("#v-seat button", host).forEach(x => x.setAttribute("aria-pressed", x === b)); renderFrame(); });
  $$("#v-hands button", host).forEach(b => b.onclick = () => { st.hands = b.dataset.hands; $$("#v-hands button", host).forEach(x => x.setAttribute("aria-pressed", x === b)); renderFrame(); });
  const ex = $(".v-replays-explain", host); if (ex) ex.addEventListener("toggle", () => store("rl.replays.explain", ex.open ? "open" : "closed"));
  $$("[data-goto]", host).forEach(b => b.onclick = () => { const k = frames.findIndex(f => f.n === +b.dataset.goto); if (k >= 0) { go(k); $("#rb-host", host).scrollIntoView({ behavior: "smooth", block: "start" }); } });
  $("#t-tl", host).addEventListener("click", e => { const r = e.currentTarget.getBoundingClientRect(); go(Math.round((e.clientX - r.left) / Math.max(1, r.width) * (n - 1))); });
  enhance(host);
  RiftBoard.mount($("#rb-host", host), { cards: data().cards, players: [playerInfo(rep, 1), playerInfo(rep, 2)] }); boardMounted = true;
  renderFrame();
}
function jump(pred, none) {
  const frames = st.rep.frames; for (let k = st.frame + 1; k < frames.length; k++) if (pred(frames[k])) { st.frame = k; renderFrame(); return; }
  toast(none, "");
}
const desyncSet = hg => new Set((((hg || {}).summary || {}).desync_list || []).map(d => d.decision));
function explainPanel(human) {
  const pref = store("rl.replays.explain"), open = pref !== "closed";
  return `<details class="panel v-replays-explain" ${open ? "open" : ""}><summary><h3>What the bars mean</h3></summary>
    <p class="small">Before each move the bot plays out many possible continuations: its search. Each bar is the share of that search it spent on an option. A clear leader is a confident choice; even bars mean it had no opinion.${human
      ? ` In a real game the bot does not play: <span class="badge win">played</span> marks the human's move and <span class="badge bot">bot</span> the bot's first choice. They agree when both land on the same row.`
      : ` The highlighted row is the move that was played.`} The ring on the table marks the card or battlefield of that move.</p></details>`;
}
function renderFrame() {
  const rep = st.rep, frames = (rep && rep.frames) || [], f = frames[st.frame], n = frames.length;
  const board = $("#rb-host", host), dec = $("#decision", host); if (!f || !board || !dec) return;
  const hg = rep.guided ? humanGameOf(st.file) : null, meta = metaOf(st.file), s = meta ? sides(meta) : null;
  const range = $("#t-range", host); if (range && +range.value !== st.frame) range.value = st.frame;
  const b = f.board || {};
  $("#t-pos", host).innerHTML = `<b>${st.frame + 1}</b><span class="muted">/${n}</span> · turn ${b.turn ?? "–"} · ${esc(phaseName(b.phase))}`;
  const compared = hg && isCompared(f);
  RiftBoard.render({ ...f, focus: frameFocus(f) }, { viewer: st.viewer, hands: st.hands, banner: compared ? (f.agree ? "Bot agrees" : "Bot disagrees") : "", bannerKind: compared ? (f.agree ? "agree" : "disagree") : "" });
  // the current move, one line under the transport
  const who = hg ? ((hg.players || [])[f.player - 1] || "P" + f.player) : (s ? (f.player === s.featured.seat ? s.featured : s.opp).player : "P" + f.player);
  const playedIdx = hg && f.human_known && f.human_choice >= 0 ? f.human_choice : f.chosen;
  const playedText = (f.options || [])[playedIdx] || f.chosen_text || "";
  $("#t-now", host).innerHTML = `<span class="badge p${f.player}">P${f.player}</span><b>${esc(who)}</b>${playedText ? `<span class="now-move">${optionHtml(playedText)}</span>` : ""}${compared ? badge(f.agree ? "bot agrees" : "bot disagrees", f.agree ? "win" : "disagree") : ""}${!hg && f.options && f.options.length > 1 && !isReal(f) ? badge("no search", "neutral") : ""}`;
  renderTimeline(frames, hg);
  if (hg) renderComparison(frames, hg);
  // decision panel
  const key = st.file + "|" + f.n, ann = st.annotations.get(key), shares = new Map((f.search || []).map(([i, x]) => [i, x]));
  const agent = f.player === 1 ? rep.agent1 : rep.agent2;
  const head = hg ? humanHead(f, hg, who) : `<div class="d-head"><div class="eyebrow">Decision ${f.n} · turn ${b.turn ?? "–"} · ${esc(phaseName(b.phase))}</div><h2>${esc(who)}'s move ${botBadge(agent)}</h2>
      <p class="small muted">${shares.size ? "Bars: the share of the bot's search per option." : /random/.test(agent || "") ? "A random player: no search, the option was drawn at random." : f.options.length < 2 ? "Only one option: nothing to choose." : "No search recorded for this decision."}</p></div>`;
  const rows = (f.options || []).map((o, i) => {
    const played = hg ? (f.human_known && i === f.human_choice) : i === f.chosen, bot = hg && f.bot_choice === i, mine = ann && ann.would_play === i;
    const share = shares.get(i), cls = ["opt", played ? "played" : "", bot && !played ? "botpick" : "", bot ? (f.agree ? "agree" : "disagree") : "", mine ? "mine" : ""].filter(Boolean).join(" ");
    return `<li class="${cls}"><span class="o-i mono">${i + 1}</span><span class="o-what">${optionHtml(o)}</span>
      <span class="o-bar ${bot ? (f.agree ? "agree" : "disagree") : ""}" aria-hidden="true"><i style="width:${share != null ? (100 * share).toFixed(0) : 0}%"></i></span><span class="o-pct mono">${share != null ? (100 * share).toFixed(0) + "%" : ""}</span>
      <span class="o-marks">${played ? badge("played", hg ? "win" : "gold") : ""}${bot ? badge("bot", f.agree ? "win" : "disagree") : ""}${mine ? badge("me", "p1") : ""}</span>
      <button class="btn-sm o-me" data-would="${i}" title="I would have played this" aria-pressed="${!!mine}">me</button></li>`;
  }).join("");
  dec.innerHTML = `${head}<ol class="v-replays-opts">${rows || `<li class="muted small">No options recorded.</li>`}</ol>${!hg && f.chosen < 0 && f.chosen_text ? `<p class="small">Played: ${optionHtml(f.chosen_text)}</p>` : ""}`;
  $$("button[data-would]", dec).forEach(bt => bt.onclick = () => saveAnnotation(f, +bt.dataset.would));
  // your take + what happened before
  $("#take", host).innerHTML = `<h3>Your take on this move</h3>${st.db
    ? `<textarea id="a-comment" placeholder="What you would have done and why">${esc(ann ? ann.comment || "" : "")}</textarea><div class="toolbar"><button class="primary" id="a-save">Save</button>${ann ? `<span class="small muted">Saved${ann.would_play != null ? ` — you would have played ${esc((f.options || [])[ann.would_play] || "")}` : ""}</span>` : ""}</div>`
    : `<p class="small muted">Votes ("me") and comments are saved when the page is opened on claude.ai with your account.</p>`}`;
  const as = $("#a-save", host); if (as) as.onclick = () => saveAnnotation(f, ann ? ann.would_play : null);
  $("#before", host).innerHTML = f.log && f.log.length ? `<details class="panel"><summary><h3>What happened before this decision</h3></summary><div class="log">${f.log.map(esc).join("\n")}</div></details>` : "";
  const play = $("#t-play", host); if (play) { play.innerHTML = st.playing ? ICON.pause : ICON.play; play.setAttribute("aria-pressed", st.playing); play.setAttribute("aria-label", st.playing ? "pause" : "play"); }
}
function humanHead(f, hg, who) {
  const b = f.board || {}, compared = isCompared(f), isCapture = f.player === hg.capture_seat;
  const ds = (((hg.summary || {}).desync_list) || []).find(d => d.decision === f.n);
  const title = compared ? `${esc(who)}'s move ${badge(f.agree ? "bot agrees" : "bot disagrees", f.agree ? "win" : "disagree")}`
    : f.forced ? `Engine-forced choice ${badge(f.note || "forced", "neutral")}`
    : f.human_known ? `${esc(who)}'s move ${badge("not compared", "neutral")}` : `Player ${f.player} ${badge(f.note || "move not in the log", "neutral")}`;
  return `<div class="d-head"><div class="eyebrow">Decision ${f.n} · turn ${b.turn ?? "–"} · ${esc(phaseName(b.phase))}${f.kind ? ` · ${esc(String(f.kind).replace(/_/g, " "))}` : ""}</div><h2>${title}</h2>
    ${f.event_text ? `<p class="small d-log"><span class="muted">In the log:</span> ${esc(f.event_text)}</p>` : ""}
    ${ds ? notice(`<b>Desync here.</b> ${esc(ds.event_text)} — ${esc(ds.reason)}. The engine re-synchronises when it can.`, "") : ""}
    ${f.forced ? `<p class="small muted">The engine needed an answer the log does not record (${esc(f.note || "a cost payment or similar")}); it took the first option. Not compared.</p>` : ""}
    ${!f.human_known && !f.forced ? `<p class="small muted">${esc(f.note || "The log has no move for this decision")}; the engine took the first option. Not compared.</p>` : ""}
    ${f.human_known && !compared && !f.forced ? `<p class="small muted">${(f.options || []).length < 2 ? "Only one option: nothing to compare." : isCapture ? "The bot was not asked here." : `Opponent's move: the bot is only asked for ${esc(hg.capture_player)}.`}</p>` : ""}
    ${compared ? `<p class="small muted">${f.clean_boundary ? "Clean boundary: the log and the engine agreed on the state before this move." : "In flight: the engine's state may differ slightly from the real game here."}</p>` : ""}</div>`;
}
function renderTimeline(frames, hg) {
  const tl = $("#t-tl", host); if (!tl) return;
  const n = frames.length, segs = turnSegments(frames), ds = hg ? desyncSet(hg) : null, scores = scoreEvents(frames);
  const pos = i => (100 * (i + 0.5) / n).toFixed(2);
  const marks = [];
  for (const e of scores) marks.push(`<i class="m-score p${e.player}" style="left:${pos(e.i)}%" title="P${e.player} scores a point (decision ${frames[e.i].n})"></i>`);
  if (hg) frames.forEach((f, i) => { if (isCompared(f)) marks.push(`<i class="m-cmp ${f.agree ? "agree" : "disagree"}" style="left:${pos(i)}%" title="decision ${f.n}: bot ${f.agree ? "agrees" : "disagrees"}"></i>`); if (ds.has(f.n)) marks.push(`<i class="m-desync" style="left:${pos(i)}%" title="desync at decision ${f.n}"></i>`); });
  tl.innerHTML = `<div class="tl-track">${segs.map(sg => `<div class="tl-turn ${sg.player ? "p" + sg.player : ""} ${st.frame >= sg.start && st.frame < sg.start + sg.count ? "cur" : ""}" style="flex:${sg.count} 1 0" title="${sg.turn ? `Turn ${sg.turn}${sg.player ? ", P" + sg.player : ""}` : "Setup and mulligans"} · ${sg.count} decisions">${sg.count / n >= 0.03 ? `<span>${sg.turn ? sg.turn : "setup"}</span>` : ""}</div>`).join("")}</div>
    <div class="tl-marks">${marks.join("")}</div><div class="tl-cursor" style="left:${pos(st.frame)}%"></div>
    <div class="tl-legend xs muted"><span><i class="k p1"></i>P1's turn</span><span><i class="k p2"></i>P2's turn</span><span><i class="k score"></i>point scored</span>${hg ? `<span><i class="k agree"></i>bot agrees</span><span><i class="k disagree"></i>disagrees</span><span><i class="k desync"></i>desync</span>` : ""}</div>`;
}
function renderComparison(frames, hg) {
  const el = $("#cmp", host); if (!el) return;
  let k = 0, agree = 0, total = 0, agreeTotal = 0; const cells = [];
  frames.forEach((x, i) => { if (!isCompared(x)) return; total++; if (x.agree) agreeTotal++; if (i <= st.frame) { k++; if (x.agree) agree++; }
    cells.push(`<button class="c ${x.agree ? "agree" : "disagree"} ${i === st.frame ? "cur" : ""} ${i < st.frame ? "past" : ""}" data-k="${i}" title="decision ${x.n}: bot ${x.agree ? "agrees" : "disagrees"}" aria-label="decision ${x.n}, bot ${x.agree ? "agrees" : "disagrees"}"></button>`); });
  const f = frames[st.frame];
  el.innerHTML = `<h3>Comparison so far</h3>
    <div class="cmp-kpis">
      <div><b>${isCompared(f) ? k : k + "+"}<small>/${total}</small></b><span>decisions compared</span></div>
      <div><b class="${k ? (agree / k >= 0.5 ? "win" : "warn") : ""}">${k ? (100 * agree / k).toFixed(0) + "%" : "—"}</b><span>bot agrees so far</span></div>
      <div><b>${agreeTotal}<small>/${total}</small></b><span>over the game (${hg.agreement_pct != null ? hg.agreement_pct.toFixed(0) + "%" : "—"})</span></div>
      <div><b>${hg.agreement_clean_pct != null ? hg.agreement_clean_pct.toFixed(0) + "%" : "—"}</b><span>clean boundary</span></div>
      <div><b>${hg.desyncs ?? 0}</b><span>desyncs</span></div>
    </div>
    <div class="cmp-strip" role="group" aria-label="compared decisions in order">${cells.join("")}</div>
    <p class="xs muted">One square per compared decision, in order; click one to jump to it.</p>`;
  $$(".cmp-strip .c", el).forEach(c => c.onclick = () => { st.frame = +c.dataset.k; renderFrame(); });
}
function aboutPanel(hg, frames) {
  const s = hg.summary || {}, ag = s.agreement || {}, res = s.result || {}, dk = s.decks || {};
  const kinds = Object.entries(ag.by_kind || {}).map(([k, v]) => ({ kind: k.replace(/_/g, " "), n: v.n, agree: v.agree, rate: v.rate })).sort((a, b) => b.n - a.n);
  const deckCol = seat => {
    const d = dk[seat] || {}, list = { ...((d.list || {}).champion || {}), ...((d.list || {}).maindeck || {}) }, runes = (d.list || {}).runes || {};
    const facts = [d.legal ? badge("legal list", "win") : badge("not a legal list", "warn"), d.filler_cards ? chip(`${d.filler_cards} cards filled in from ${esc(String(d.filler || "").split("/").pop().replace(/\.txt$/, ""))}`) : "",
      (d.dropped || []).length ? chip(`dropped ${d.dropped.map(x => esc(x.card)).join(", ")}`) : "", (d.unknown_cards || []).length ? chip(`unknown: ${d.unknown_cards.map(esc).join(", ")}`) : ""].filter(Boolean).join("");
    return `<div class="about-deck">
      <div class="ad-head">${portrait(legendFace(d.legend || "") || d.legend || "?", { size: "lg" })}<div><h3>P${seat.slice(1)} · ${esc(d.player || "")}</h3><div class="ad-legend">${esc(d.legend || "unknown legend")}${d.champion ? ` · champion ${cardChip(d.champion, { cls: "sm" })}` : ""}</div></div></div>
      <div class="row tight">${facts}</div>
      ${Object.keys(list).length ? cardGrid(list, { champion: (d.list || {}).champion || {}, w: 76 }) : `<p class="small muted">No list inferred.</p>`}
      <div class="ad-foot"><span class="small muted">Runes</span> ${Object.entries(runes).map(([r, k]) => `<span class="chip">${domainDot(r.toLowerCase().replace(/ rune$/, ""))}${k} ${esc(r.replace(/ Rune$/, "").toLowerCase())}</span>`).join("") || "—"}</div>
      <div class="ad-foot"><span class="small muted">Battlefields${d.battlefield_brought ? ` (brought ${esc(d.battlefield_brought)})` : ""}</span><div class="ad-bfs">${(d.battlefields || []).map(bf => cardImg(bf, { size: "sm", w: 300, alt: bf })).join("") || "—"}</div></div>
    </div>`;
  };
  const desyncs = s.desync_list || [];
  return `<details class="panel v-replays-about" id="about"><summary><h2>About this comparison</h2><span class="small muted">source, inferred decks, desyncs, limits</span></summary>
    <div class="stack" style="margin-top:var(--sp-4)">
      <div class="row tight">${chip(`<b>${ag.n || 0}</b> decisions compared, <b>${ag.agree || 0}</b> agree (${pct(ag.rate, 0)})`)}${chip(`clean boundary ${ag.clean_boundary ? `<b>${ag.clean_boundary.agree}/${ag.clean_boundary.n}</b> (${pct(ag.clean_boundary.rate, 0)})` : "—"}`)}${chip(`<b>${hg.desyncs ?? 0}</b> desyncs`)}${chip(`stopped: ${esc(hg.stop_reason || "—")}`)}${chip(`engine score <b class="mono">${(res.engine_scores || hg.engine_scores || []).join("–")}</b> · log score <b class="mono">${(res.log_scores || hg.log_scores || []).join("–")}</b>`)}${hg.first_player ? chip(`${esc(hg.first_player)} went first`) : ""}${hg.source_url ? `<a class="chip" href="${esc(hg.source_url)}" target="_blank" rel="noopener">RiftLite replay ↗</a>` : ""}</div>
      <div class="grid2 about-grid">
        <div class="stack tight"><h3>What this shows</h3>
          <p class="small">The engine re-creates the game from the RiftLite log. At each decision of ${esc(hg.capture_player)}, ${esc(agentLabel(hg.bot))} is asked what it would do; it agrees when its first choice is the move the human made.</p>
          <p class="small">Limits: the log hides hands (a card joins the hand when it is played, and the opponent's hand is a guess), both decks are inferred from the cards seen and filled up from a lab list, and the engine can lose the log (a desync): after one it re-synchronises when it can and stops after a few. "Clean boundary" counts only the decisions taken right after the log and the engine agreed on the state.</p></div>
        <div class="stack tight"><h3>Agreement by kind of decision</h3>${kinds.length ? table([
          { key: "kind", label: "Decision", sort: true, render: r => `<span class="lead">${esc(r.kind)}</span>` },
          { key: "n", label: "Compared", num: true, sort: true },
          { key: "agree", label: "Agree", num: true, sort: true },
          { key: "rate", label: "Rate", sort: true, render: r => `<span class="rate">${rateBar(r.rate, null, 90)}<span class="rate-v">${pct(r.rate, 0)}</span></span>` },
        ], kinds, { cls: "" }) : `<p class="small muted">No compared decision.</p>`}</div>
      </div>
      <div class="grid2 about-grid">${deckCol("P1")}${deckCol("P2")}</div>
      ${desyncs.length ? `<div class="stack tight"><h3>Desyncs <span class="muted">(${Math.min(desyncs.length, 12)} of ${hg.desyncs ?? desyncs.length})</span></h3>
        <ol class="v-replays-desyncs">${desyncs.map(d => `<li><div class="ds-when mono">turn ${d.turn} · ${esc(phaseName(d.phase))} · decision ${d.decision}</div><div class="ds-what">${esc(d.event_text || "")}</div><div class="ds-why small muted">${esc(d.reason || "")}</div>${frames.some(f => f.n === d.decision) ? `<button class="btn-sm" data-goto="${d.decision}">Show</button>` : ""}</li>`).join("")}</ol>
        ${(hg.top_desync_causes || []).length ? `<p class="small muted">Most common causes: ${hg.top_desync_causes.map(c => `${esc(c.cause)} (${c.n})`).join("; ")}.</p>` : ""}</div>` : ""}
    </div></details>`;
}

// ── autoplay, keys, annotations ─────────────────────────────────────────────
function startAutoplay() {
  const n = ((st.rep || {}).frames || []).length; if (!n) return;
  if (st.frame >= n - 1) st.frame = 0;
  st.playing = true; st.timer = setInterval(() => { if (!host || !host.isConnected || !$("#t-play", host)) return stopAutoplay(); if (st.frame >= n - 1) { stopAutoplay(); return; } st.frame++; renderFrame(); }, 900 / st.speed);
  renderFrame();
}
function stopAutoplay() { if (st.timer) clearInterval(st.timer); st.timer = 0; const was = st.playing; st.playing = false; if (was && host && $("#t-play", host)) renderFrame(); }
function toggleAutoplay() { st.playing ? stopAutoplay() : startAutoplay(); }
function onKey(e) {
  if (!st.rep || !host || !host.isConnected || !$("#rb-host", host) || e.altKey || e.ctrlKey || e.metaKey) return;
  const tag = e.target.tagName; if (/^(INPUT|TEXTAREA|SELECT)$/.test(tag)) return;
  const n = (st.rep.frames || []).length; if (!n) return;
  const go = k => { st.frame = Math.max(0, Math.min(n - 1, k)); renderFrame(); e.preventDefault(); };
  if (e.key === "ArrowRight") go(st.frame + 1); else if (e.key === "ArrowLeft") go(st.frame - 1);
  else if (e.key === "Home") go(0); else if (e.key === "End") go(n - 1);
  else if (e.key === " " && tag !== "BUTTON") { toggleAutoplay(); e.preventDefault(); }
  else if (e.key === "d" || e.key === "D") { const hg = st.rep.guided ? humanGameOf(st.file) : null; hg ? jump(f => isCompared(f) && !f.agree, "No later disagreement.") : jump(isReal, "No later decision with a real choice."); e.preventDefault(); }
}
async function saveAnnotation(f, would) {
  if (!st.db) { toast("Votes are saved when the page is opened on claude.ai with your account.", ""); return; }
  const comment = ($("#a-comment", host) || {}).value || "";
  const id = (st.file + "_" + f.n).replace(/[^A-Za-z0-9_-]+/g, "_").slice(-140);
  const doc = { replay: st.file, frame: f.n, turn: (f.board || {}).turn, player: f.player, options: f.options, bot_choice: f.chosen, would_play: would, comment, author: st.meId || "", at: new Date().toISOString() };
  try { await st.db.collection("annotations").doc(id).set(doc); st.annotations.set(st.file + "|" + f.n, doc); renderFrame(); toast("Saved.", "ok"); }
  catch (e) { toast("Not saved: " + (e.message || e.code || "the database refused the write") + ". Try again from claude.ai.", "error"); }
}
