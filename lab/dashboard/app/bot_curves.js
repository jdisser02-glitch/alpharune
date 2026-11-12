/* Riven Lab — Bot: the learning curves of each legend's specialist net.
   Data: data.learning.tracks (build.py, learning_tracks): one compact row per iteration,
     {i, t, ok, c?, k?, elo, m:[wins,games], ms, p:[wins,games], ps, pc, vl, hu:[hits,rows], g:[wins,games], min}.
   Five small charts share one x axis (the iteration) and one hover: Elo, the mirror against the run's own frozen
   earlier nets, the net in place against tournament lists, the value loss, the share of human first moves found.
   Each point is one iteration (hollow = rejected); the line is the mean of the last 8 iterations, pooled over
   their games for win rates (with its 95% interval as a band), and it restarts where the opponents changed
   (ms / ps number the stretches that share one set of frozen nets / one panel).
   The charts are drawn at the pixel size of their cell, so text keeps its size on a phone; a ResizeObserver redraws.
   Used by app/bot.js: learningSection(D) → html, wireLearning(root), unwireLearning(). Styles: app/bot.css (.v-bot-lc). */
import { esc, el, pct, pct0, num, fmtDate, fmtDateTime, ago, plural, wilson, interval, badge, chip, empty, table, enhance,
         portrait, store, $, $$ } from "./lib.js";

const WINDOW = 8;                 // iterations in the running mean (the training notes read "last 8 against the 8 before")
const KEY = "bot.track";
let slug = store(KEY) || null;    // the track shown; kept across navigation and visits
let root = null, observer = null, tip = null, plots = [], redrawTimer = 0, lastWidth = 0;

const rateOf = a => (a && a[1] ? a[0] / a[1] : null);
const signed = (x, d = 1) => `${x > 0 ? "+" : x < 0 ? "−" : ""}${Math.abs(x).toFixed(d)}`;
const hours = m => (m >= 90 ? `${Math.round(m / 60)} hours` : `${Math.round(m)} minutes`);
const possessive = t => `${t.legend}${t.human_games ? " (human-games variant)" : ""}`;

/* The five measures. kind "rate": get → [wins, games]; kind "value": get → number. */
const CHARTS = [
  { id: "elo", wide: true, kind: "value", title: "Strength (Elo)", get: r => r.elo, fmt: v => String(Math.round(v)), unit: "Elo",
    what: () => "One rating per iteration, from every match its net played in this run. Higher is stronger: 100 points more is about 64% of the games against the lower net." },
  { id: "mirror", kind: "rate", title: "Against its own earlier nets", get: r => r.m, seg: r => r.ms, mid: 0.5,
    what: t => t.mirror_nets.length ? `Same list on both sides, against ${t.mirror_nets.length} nets frozen earlier in this run. 50% would mean no stronger than they were.` : "Same list on both sides, against nets frozen earlier in the run.",
    none: "Measured only under the continuous method (since 10 Nov 2026). This run stopped before." },
  { id: "panel", kind: "rate", title: "Against tournament lists", get: r => r.p, seg: r => r.ps,
    what: t => `The net in place piloting its list against ${t.panel_lists.length ? plural(t.panel_lists.length, "tournament list") : "tournament lists"}${t.panel_held_out ? " it never trains against" : ""}${t.panel_lists.length ? ` (${esc([...new Set(t.panel_lists)].join(", "))})` : ""}.` },
  { id: "loss", kind: "value", lower: true, title: "Calling the winner (value loss)", get: r => r.vl, fmt: v => v.toFixed(3), unit: "",
    what: () => "How wrong the net is about who wins, on games it did not train on. Lower is better; 0.69 is guessing." },
  { id: "human", kind: "rate", title: "Same first move as humans", get: r => r.hu,
    what: t => { const n = (t.rows.filter(r => r.hu).slice(-1)[0] || {}).hu; return `How often its first choice is the move a person made in real games of this legend${n ? ` (${num(n[1])} decisions, so each point moves by several points on its own)` : ""}.`; },
    none: "No real game of this legend was set aside to compare with." },
];

/* Points, running mean and (for rates) its interval, per chart. */
function series(t, c) {
  const R = t.rows, pts = R.map(r => { const x = c.get(r); if (x == null) return null; return c.kind === "rate" ? (x[1] ? { v: x[0] / x[1], w: x[0], g: x[1] } : null) : { v: x }; });
  const segs = R.map((r, i) => (pts[i] ? (c.seg ? c.seg(r) : 1) : null));
  const have = pts.filter(Boolean).length, need = Math.min(3, have);
  const smooth = R.map((_, i) => {
    if (!pts[i]) return null;
    const win = []; for (let j = i; j >= 0 && win.length < WINDOW; j--) if (pts[j] && segs[j] === segs[i]) win.push(pts[j]);
    if (win.length < need) return null;
    if (c.kind === "rate") { const w = win.reduce((a, p) => a + p.w, 0), g = win.reduce((a, p) => a + p.g, 0); return { v: w / g, ci: wilson(w, g), n: win.length, g }; }
    return { v: win.reduce((a, p) => a + p.v, 0) / win.length, n: win.length };
  });
  let last = -1; for (let i = R.length - 1; i >= 0; i--) if (smooth[i]) { last = i; break; }
  // the same mean one window earlier, in the same stretch: what "now" is compared with
  let prev = -1; if (last >= 0) { let seen = 0; for (let j = last - 1; j >= 0; j--) if (pts[j] && segs[j] === segs[last] && ++seen === WINDOW) { prev = smooth[j] && smooth[j].n === WINDOW && smooth[last].n === WINDOW ? j : -1; break; } }
  return { pts, segs, smooth, last, prev, have };
}

/* "now" line above a chart: the running mean, and how it moved against the window before. */
function nowLine(t, c, S) {
  if (S.last < 0) return "";
  const now = S.smooth[S.last], show = c.kind === "rate" ? pct(now.v) : c.fmt(now.v);
  const tr = c.id === "mirror" ? t.trend.mirror : c.id === "panel" ? t.trend.panel : null;
  let delta = "";
  if (tr) {
    const z = tr.se ? tr.diff / tr.se : 0, tone = Math.abs(z) < 2 ? "" : z > 0 ? "up" : "down";
    delta = `<span class="v-bot-lc-delta ${tone}">${signed(tr.diff * 100)} points</span><span class="muted"> against the ${WINDOW} iterations before, standard error ${(tr.se * 100).toFixed(1)}</span>`;
  } else if (S.prev >= 0) {
    const d = now.v - S.smooth[S.prev].v;
    delta = `<span class="v-bot-lc-delta">${c.kind === "rate" ? `${signed(d * 100)} points` : signed(d, c.id === "elo" ? 0 : 3)}</span><span class="muted"> against the ${WINDOW} iterations before</span>`;
  }
  return `<div class="v-bot-lc-now"><b>${show}</b><span class="small">${c.kind === "rate" && now.ci ? `<span class="muted">${interval(now.ci)} over the last ${plural(now.n, "iteration")}${delta ? " · " : ""}</span>` : `<span class="muted">mean of the last ${plural(now.n, "iteration")}${delta ? " · " : ""}</span>`}${delta}</span></div>`;
}

/* One sentence on where the run stands, from the two measures that have a standard error. */
function verdict(t) {
  const parts = [], zs = [];
  for (const [k, label] of [["mirror", "against its own earlier nets"], ["panel", "against tournament lists"]]) {
    const tr = t.trend[k]; if (!tr) continue;
    zs.push(tr.se ? tr.diff / tr.se : 0);
    parts.push(`${signed(tr.diff * 100)} points ${label} (standard error ${(tr.se * 100).toFixed(1)})`);
  }
  if (!parts.length) return "";
  const up = zs.filter(z => z >= 2).length, down = zs.filter(z => z <= -2).length, clear = zs.some(z => Math.abs(z) >= 3);
  const live = t.state === "training";
  const head = up && !down ? (live ? (clear ? "It is still getting stronger." : "It may still be getting stronger.") : (clear ? "It was still getting stronger when the run stopped." : "It may still have been getting stronger when the run stopped."))
    : down && !up ? (live ? (clear ? "It is getting weaker." : "It has stopped improving and may be slipping.") : (clear ? "It was getting weaker when the run stopped." : "It had stopped improving when the run stopped."))
    : up && down ? "The two measures disagree." : live ? "It has stopped improving for now." : "It had stopped improving when the run stopped.";
  return `<b>${head}</b> Last ${WINDOW} iterations against the ${WINDOW} before: ${parts.join(", ")}. A move under twice its standard error is noise.`;
}

function status(t) {
  const R = t.rows, last = R[R.length - 1], eloRows = R.filter(r => r.elo != null);
  const S = series(t, CHARTS[0]), now = S.last >= 0 ? S.smooth[S.last].v : null;
  const state = t.state === "training" ? badge("training", "win") : t.state === "paused" ? badge("paused", "neutral") : badge("stopped", "warn");
  const when = t.state === "training" ? `iteration ${last.i} started ${esc(ago(last.t))}` : `last iteration ${esc(fmtDate(last.t))}`;
  const elo = eloRows.length > 1 && now != null ? ` Its rating went from ${Math.round(eloRows[0].elo)} at the first iteration to about ${Math.round(now)} now.` : "";
  return `<div class="v-bot-lc-status">
      <div class="v-bot-lc-who">${portrait(t.title || t.legend, { size: "lg" })}<div><div class="v-bot-lc-name">${esc(possessive(t))} ${state}</div>
        <div class="small muted">${plural(R.length, "iteration")} since ${esc(fmtDate(t.first_started))}, ${when} · ${hours(t.minutes)} of training</div></div></div>
      <p>${verdict(t)}${elo}</p>
      <div class="row tight">${chip(`net in use <b class="mono">${esc(t.current)}</b>`)}${t.head && t.head !== t.current ? chip(`newest net <b class="mono">${esc(t.head)}</b>, not in use yet`) : ""}${chip(`${t.kept} of ${R.length} iterations kept`)}${t.continuous_since != null ? chip(`continuous method since iteration ${t.continuous_since}`) : ""}</div>
    </div>`;
}

function body(t) {
  const cells = CHARTS.map(c => {
    const S = series(t, c);
    const inner = S.have ? `${nowLine(t, c, S)}<div class="v-bot-lc-plot" data-chart="${c.id}"></div>` : `<p class="small muted v-bot-lc-none">${esc(c.none || "Not measured in this run.")}</p>`;
    return `<figure class="v-bot-lc-cell${c.wide ? " wide" : ""}"><figcaption><h3>${esc(c.title)}</h3><p class="xs muted">${c.what(t)}</p></figcaption>${inner}</figure>`;
  }).join("");
  const breaks = [...new Set(t.rows.filter((r, i) => i && r.ps && t.rows[i - 1].ps && r.ps !== t.rows[i - 1].ps).map(r => r.i))];
  return `${status(t)}
    <div class="v-bot-lc-grid">${cells}</div>
    <div class="v-bot-legend" aria-hidden="true">
      <span><i class="lc-line"></i>mean of the last ${WINDOW} iterations (fewer at the start of a line)</span>
      <span><i class="lc-band"></i>its 95% interval, the range the true rate most likely sits in</span>
      <span><i class="lc-dot"></i>one iteration, kept &nbsp;<i class="lc-dot hollow"></i>rejected</span>
      ${t.continuous_since != null ? `<span><i class="lc-rule"></i>continuous method from iteration ${t.continuous_since}: every new net is kept unless clearly worse</span>` : ""}
    </div>
    ${breaks.length ? `<p class="xs muted">The tournament lists changed at iteration${breaks.length > 1 ? "s" : ""} ${breaks.join(", ")}; that line restarts there, and rates before and after are not comparable.</p>` : ""}
    <details class="v-bot-lc-table"><summary>All ${plural(t.rows.length, "iteration")} as a table</summary>${iterTable(t)}</details>`;
}

function iterTable(t) {
  const r2 = a => (a && a[1] ? a[0] / a[1] : -1);
  const cell = a => (a && a[1] ? `${pct(a[0] / a[1])}<span class="sub">${a[0]}/${a[1]}</span>` : "—");
  const cols = [
    { key: "i", label: "#", num: true, sort: true, render: r => `<span class="mono">${r.i}</span>` },
    { key: "t", label: "Started", sort: true, render: r => `<span class="mono">${esc(fmtDateTime(r.t))}</span>` },
    { key: "ok", label: "Kept", sort: r => (r.ok ? 1 : 0), render: r => badge(r.ok ? "yes" : "no", r.ok ? "win" : "loss") },
    { key: "elo", label: "Elo", num: true, sort: r => r.elo ?? -1e9, render: r => (r.elo == null ? "—" : Math.round(r.elo)) },
    { key: "m", label: "Earlier nets", num: true, sort: r => r2(r.m), render: r => cell(r.m) },
    { key: "p", label: "Tournament lists", num: true, sort: r => r2(r.p), render: r => cell(r.p) },
    { key: "vl", label: "Value loss", num: true, sort: r => r.vl ?? 9, render: r => (r.vl == null ? "—" : r.vl.toFixed(3)) },
    { key: "hu", label: "Human first move", num: true, sort: r => r2(r.hu), render: r => cell(r.hu) },
    { key: "g", label: "Vs the net before", num: true, sort: r => r2(r.g), render: r => cell(r.g) },
    { key: "min", label: "Minutes", num: true, sort: r => r.min ?? 0, render: r => (r.min == null ? "—" : num(r.min)) },
  ];
  return table(cols, [...t.rows].reverse(), { sort: { key: "i", dir: "desc" }, cls: "tall", empty: "No iteration yet." });
}

/* ── drawing ────────────────────────────────────────────────────────────── */
/* The round step (1, 2 or 5 times a power of ten) whose number of intervals over `span` is closest to `target`. */
function niceStep(span, target = 4) {
  const mag = Math.pow(10, Math.floor(Math.log10(span / target)));
  return [1, 2, 5, 10, 20].map(k => k * mag).reduce((a, b) => (Math.abs(span / b - target) < Math.abs(span / a - target) ? b : a));
}
function draw(plot, t, c) {
  const S = series(t, c), R = t.rows, n = R.length;
  const W = Math.max(260, Math.floor(plot.clientWidth)), H = c.wide ? 232 : 196, L = 44, RM = 54, T = 14, B = 38;
  // The scale follows the running mean and the bulk of the points: a lone far point (the untrained net of iteration 0)
  // would flatten everything else, so the outer 3% on each side are drawn at the edge as small triangles.
  const raw = S.pts.filter(Boolean).map(p => p.v).sort((a, b) => a - b), cut = raw.length >= 20 ? Math.floor(raw.length * 0.03) : 0;
  const vals = raw.slice(cut, raw.length - cut); S.smooth.forEach(s => { if (s) { vals.push(s.v); if (s.ci) vals.push(s.ci[0], s.ci[1]); } });
  if (c.mid != null) vals.push(c.mid);
  let lo = Math.min(...vals), hi = Math.max(...vals); if (hi - lo < 1e-9) { lo -= 1; hi += 1; }
  const step = niceStep((hi - lo) * 1.08, 4); lo = Math.floor(lo / step + 1e-9) * step; hi = Math.ceil(hi / step - 1e-9) * step;
  if (c.kind === "rate") { lo = Math.max(0, lo); hi = Math.min(1, hi); }
  const x = i => L + (n === 1 ? (W - L - RM) / 2 : i * (W - L - RM) / (n - 1));
  const y = v => T + (H - T - B) * (1 - (v - lo) / (hi - lo));
  const f = v => v.toFixed(1), tick = v => (c.kind === "rate" ? `${Math.round(v * 100)}%` : c.id === "loss" ? v.toFixed(2) : String(Math.round(v)));
  const label = `${c.title} per iteration for ${t.legend}${S.last >= 0 ? `: ${c.kind === "rate" ? pct(S.smooth[S.last].v) : c.fmt(S.smooth[S.last].v)} over the last iterations` : ""}.`;
  let s = `<svg class="v-bot-lc-svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-label="${esc(label)}">`;
  for (let v = lo; v <= hi + step / 2; v += step) s += `<line class="grid" x1="${L}" x2="${W - RM}" y1="${f(y(v))}" y2="${f(y(v))}"/><text class="tick" x="${L - 8}" y="${f(y(v) + 4)}" text-anchor="end">${tick(v)}</text>`;
  if (c.mid != null && c.mid > lo && c.mid < hi) s += `<line class="mid" x1="${L}" x2="${W - RM}" y1="${f(y(c.mid))}" y2="${f(y(c.mid))}"/>`;
  // x ticks: iteration numbers, about one every 70 px
  const xstep = Math.max(1, Math.round(niceStep(Math.max(n - 1, 1), Math.max(2, (W - L - RM) / 80))));
  R.forEach((r, i) => { if (r.i % xstep === 0 || n <= 8) s += `<text class="tick" x="${f(x(i))}" y="${H - B + 16}" text-anchor="middle">${r.i}</text>`; });
  s += `<text class="axis" x="${f((L + W - RM) / 2)}" y="${H - 5}" text-anchor="middle">iteration</text>`;
  // where the method changed, and where this measure's opponents changed
  const ci = t.continuous_since == null ? -1 : R.findIndex(r => r.i === t.continuous_since);
  if (ci > 0) { const right = x(ci) > L + (W - L - RM) * 0.7; s += `<line class="rule" x1="${f(x(ci))}" x2="${f(x(ci))}" y1="${T}" y2="${H - B}"/>${c.wide ? `<text class="note" x="${f(x(ci) + (right ? -6 : 6))}" y="${T + 11}"${right ? ` text-anchor="end"` : ""}>continuous method</text>` : ""}`; }
  if (c.seg) R.forEach((r, i) => { if (i && S.segs[i] != null && S.segs[i - 1] != null && S.segs[i] !== S.segs[i - 1] && i !== ci) s += `<line class="rule faint" x1="${f(x(i))}" x2="${f(x(i))}" y1="${T}" y2="${H - B}"><title>Other opponents from iteration ${r.i}</title></line>`; });
  // the running mean, one path (and one band) per stretch
  const runs = []; let cur = null;
  S.smooth.forEach((m, i) => { if (!m) { cur = null; return; } if (!cur || cur.seg !== S.segs[i]) { cur = { seg: S.segs[i], idx: [] }; runs.push(cur); } cur.idx.push(i); });
  for (const run of runs) {
    if (c.kind === "rate" && run.idx.length > 1) s += `<polygon class="band" points="${run.idx.map(i => `${f(x(i))},${f(y(S.smooth[i].ci[1]))}`).join(" ")} ${[...run.idx].reverse().map(i => `${f(x(i))},${f(y(S.smooth[i].ci[0]))}`).join(" ")}"/>`;
  }
  S.pts.forEach((p, i) => {
    if (!p) return; const cls = `pt${R[i].ok ? "" : " no"}`, cx = x(i);
    if (p.v > hi + 1e-9) s += `<path class="${cls}" d="M${f(cx - 3.5)},${T + 6} L${f(cx + 3.5)},${T + 6} L${f(cx)},${T} Z"><title>Iteration ${R[i].i}: above the scale</title></path>`;
    else if (p.v < lo - 1e-9) s += `<path class="${cls}" d="M${f(cx - 3.5)},${H - B - 6} L${f(cx + 3.5)},${H - B - 6} L${f(cx)},${H - B} Z"><title>Iteration ${R[i].i}: below the scale</title></path>`;
    else s += `<circle class="${cls}" cx="${f(cx)}" cy="${f(y(p.v))}" r="2.6"/>`;
  });
  for (const run of runs) s += `<path class="mean" d="${run.idx.map((i, k) => `${k ? "L" : "M"}${f(x(i))},${f(y(S.smooth[i].v))}`).join(" ")}"/>`;
  if (S.last >= 0) { const m = S.smooth[S.last]; s += `<circle class="end" cx="${f(x(S.last))}" cy="${f(y(m.v))}" r="4.5"/><text class="endv" x="${f(x(S.last) + 9)}" y="${f(y(m.v) + 4)}">${c.kind === "rate" ? pct0(m.v) : c.fmt(m.v)}</text>`; }
  s += `<line class="cross" x1="0" x2="0" y1="${T}" y2="${H - B}"/><circle class="hover" r="4.5" cx="0" cy="0"/>`;
  s += `<rect class="hit" x="${L - 6}" y="${T}" width="${W - L - RM + 12}" height="${H - T - B}" fill="transparent"/></svg>`;
  plot.innerHTML = s;
  return { plot, c, S, x, y, n, L, RM, W, lo, hi };
}

function redraw() {
  if (!root) return; const t = current(); if (!t) return;
  plots = $$(".v-bot-lc-plot", root).map(p => draw(p, t, CHARTS.find(c => c.id === p.dataset.chart)));
}

/* ── hover: one crosshair across the five charts, one tooltip ───────────── */
function showAt(i, clientX, clientY) {
  const t = current(); if (!t) return; const r = t.rows[i]; if (!r) return;
  for (const P of plots) {
    const svg = $("svg", P.plot), cross = $(".cross", svg), dot = $(".hover", svg), m = P.S.smooth[i], p = P.S.pts[i];
    cross.setAttribute("x1", P.x(i)); cross.setAttribute("x2", P.x(i)); cross.classList.add("on");
    const at = m || p; if (at) { dot.setAttribute("cx", P.x(i)); dot.setAttribute("cy", P.y(Math.max(P.lo, Math.min(P.hi, at.v)))); dot.classList.add("on"); } else dot.classList.remove("on");
  }
  const line = (label, a, extra = "") => (a && a[1] ? `<div>${label}: <span class="mono">${pct(a[0] / a[1])} (${a[0]}/${a[1]})</span>${extra}</div>` : "");
  tip.innerHTML = `<b>Iteration ${r.i}</b> · ${esc(fmtDateTime(r.t))} ${badge(r.ok ? "kept" : "rejected", r.ok ? "win" : "loss")}
    ${r.elo != null ? `<div>Elo: <span class="mono">${Math.round(r.elo)}</span></div>` : ""}
    ${line("Earlier nets", r.m)}${line("Tournament lists, net in place", r.p, r.pc != null ? ` · this candidate <span class="mono">${pct(r.pc)}</span>` : "")}
    ${r.vl != null ? `<div>Value loss: <span class="mono">${r.vl.toFixed(3)}</span></div>` : ""}${line("Human first move", r.hu)}
    ${line("Vs the net before", r.g)}<div class="muted">${r.min != null ? `${num(r.min)} minutes` : ""}${r.k ? " · its net was published for tests and Play" : ""}</div>`;
  tip.classList.add("on");
  const w = tip.offsetWidth || 280, h = tip.offsetHeight || 120;
  let left = clientX + 16, top = clientY - h - 12; if (left + w > innerWidth - 8) left = clientX - w - 16; if (left < 8) left = 8; if (top < 8) top = clientY + 16;
  tip.style.left = `${left}px`; tip.style.top = `${top}px`;
}
function hideHover() { if (tip) tip.classList.remove("on"); if (root) $$(".v-bot-lc-svg .on", root).forEach(e => e.classList.remove("on")); }
function indexAt(e) {
  const hit = e.target.closest && e.target.closest(".v-bot-lc-svg .hit"); if (!hit) return -1;
  const P = plots.find(p => p.plot.contains(hit)); if (!P) return -1;
  const box = $("svg", P.plot).getBoundingClientRect(), px = (e.clientX - box.left) * (P.W / box.width);
  return Math.max(0, Math.min(P.n - 1, Math.round((px - P.L) / ((P.W - P.L - P.RM) / Math.max(1, P.n - 1)))));
}

/* ── section ────────────────────────────────────────────────────────────── */
let tracks = [];
const current = () => tracks.find(t => t.slug === slug) || tracks[0] || null;

export function learningSection(D) {
  tracks = ((D.learning || {}).tracks || []).filter(t => t.rows && t.rows.length);
  if (!tracks.length) return `<section class="v-bot panel stack" aria-labelledby="bot-curves"><h2 id="bot-curves">Learning curves</h2>${empty("No specialist net yet", "Each legend's own net appears here after its first training iteration.")}</section>`;
  const t = current(), running = tracks.filter(x => x.state === "training");
  const sub = running.length ? `${running.map(x => esc(possessive(x))).join(" and ")} ${running.length > 1 ? "are" : "is"} training now` : "No net is training right now";
  return `<section class="v-bot panel stack v-bot-lc" aria-labelledby="bot-curves">
      <div class="panel-head"><h2 id="bot-curves">Learning curves</h2><span class="small muted">${sub}</span></div>
      <p class="small muted">Each legend has its own net, trained by playing itself. One iteration is a round of self-play games, a training pass and a check of the new net. Pick a legend to see every iteration of its run; hover a chart for the numbers.</p>
      <div class="segmented v-bot-lc-tracks" role="group" aria-label="Legend">${tracks.map(x => `<button type="button" data-track="${esc(x.slug)}" aria-pressed="${x.slug === t.slug}">${esc(x.legend)}${x.human_games ? " (human)" : ""}${x.state === "training" ? `<i class="live" title="training now"></i>` : ""}</button>`).join("")}</div>
      <div class="v-bot-lc-body">${body(t)}</div>
    </section>`;
}

export function wireLearning(container) {
  root = $(".v-bot-lc", container); if (!root) return;
  tip = $(".v-bot-tip"); if (!tip) { tip = el("div", { className: "v-bot-tip", role: "tooltip" }); document.body.append(tip); }
  root.addEventListener("click", e => {
    const b = e.target.closest("[data-track]");
    if (b) { slug = b.dataset.track; store(KEY, slug); $$("[data-track]", root).forEach(x => x.setAttribute("aria-pressed", String(x === b))); hideHover(); $(".v-bot-lc-body", root).innerHTML = body(current()); enhance(root); redraw(); return; }
    const i = indexAt(e); if (i >= 0) { showAt(i, e.clientX, e.clientY); clearTimeout(showAt.t); showAt.t = setTimeout(hideHover, 4000); }
  });
  root.addEventListener("mousemove", e => { const i = indexAt(e); if (i >= 0) showAt(i, e.clientX, e.clientY); else hideHover(); });
  root.addEventListener("mouseleave", hideHover);
  redraw();
  lastWidth = root.clientWidth;
  if (typeof ResizeObserver === "function") {
    observer = new ResizeObserver(() => { if (!root || root.clientWidth === lastWidth) return; lastWidth = root.clientWidth; clearTimeout(redrawTimer); redrawTimer = setTimeout(() => { hideHover(); redraw(); }, 120); });
    observer.observe(root);
  }
}

export function unwireLearning() {
  if (observer) observer.disconnect(); observer = null; clearTimeout(redrawTimer); clearTimeout(showAt.t); hideHover(); root = null; plots = [];
}
