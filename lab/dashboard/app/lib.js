/* Riven Lab — shared library for every view module.
   Data (data.json), formatting, card lookup and imagery, colours, statistics, small HTML components.
   Views import what they need: `import { data, esc, cardChip, kpi, table } from "./lib.js";`
   Everything that renders returns an HTML string; `enhance(root)` wires the behaviours (sortable tables) after insert.
   See DESIGN.md for the components and their class names. */

// ── DOM / strings ───────────────────────────────────────────────────────────
export const $ = (sel, el = document) => el.querySelector(sel);
export const $$ = (sel, el = document) => [...el.querySelectorAll(sel)];
export const esc = s => String(s ?? "").replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
export const attr = s => esc(s);
/** html`...` — a tagged template that escapes interpolations unless they are marked raw() or are arrays of raw. */
export function html(strings, ...vals) {
  return strings.reduce((out, s, i) => {
    const v = vals[i - 1];
    const str = v == null || v === false ? "" : Array.isArray(v) ? v.map(x => (x && x.__raw) ? x.s : esc(x)).join("") : (v && v.__raw) ? v.s : esc(v);
    return out + str + s;
  });
}
export const raw = s => ({ __raw: true, s: String(s ?? "") });
export const el = (tag, props = {}, children = []) => { const e = document.createElement(tag); Object.assign(e, props); for (const c of [].concat(children)) e.append(c); return e; };
export function store(k, v) { try { if (v === undefined) return JSON.parse(localStorage.getItem(k)); if (v === null) localStorage.removeItem(k); else localStorage.setItem(k, JSON.stringify(v)); } catch (e) { return null; } }
export const navigate = hash => { location.hash = hash.startsWith("#") ? hash : "#" + hash; };
export const sleep = ms => new Promise(r => setTimeout(r, ms));
/** Loads a stylesheet once (a view's own CSS: `cssOnce(new URL("./tests.css", import.meta.url).href)`).
    Resolves when the sheet is in (or immediately when it already is), so a view can await it before painting. */
const cssLoaded = new Map();
export function cssOnce(url) {
  const href = new URL(url, location.href).href;
  if (cssLoaded.has(href)) return cssLoaded.get(href);
  const p = new Promise(resolve => {
    const l = el("link", { rel: "stylesheet", href }); l.dataset.cssOnce = "1";
    l.onload = l.onerror = () => resolve(); document.head.append(l);
    setTimeout(resolve, 1500);                                   // never block a render on a slow host
  });
  cssLoaded.set(href, p); return p;
}

// ── data ────────────────────────────────────────────────────────────────────
let D = null, loading = null;
/** The lab data (data.json). Resolves once; later calls return the same object. */
export function loadData() {
  if (!loading) loading = fetch("data.json").then(r => { if (!r.ok) throw new Error(`data.json: HTTP ${r.status}`); return r.json(); }).then(d => { D = d; index(d); return d; });
  return loading;
}
/** Synchronous access after loadData() resolved (the router awaits it before rendering a view). */
export const data = () => D;
let byName = new Map(), byId = new Map(), byPath = new Map(), champNames = null, nameList = null;
function index(d) {
  byName = new Map(); byId = new Map(); byPath = new Map(); champNames = null; nameList = null;
  for (const c of d.cards || []) {
    if (c.id != null && !byId.has(+c.id)) byId.set(+c.id, c);
    const n = c.name.toLowerCase(); if (!byName.has(n)) byName.set(n, c);
    if (c.type === "legend") for (const t of c.tags || []) { const k = `${t}, ${c.name}`.toLowerCase(); if (!byName.has(k)) byName.set(k, c); }
  }
  for (const dk of d.decks || []) byPath.set(dk.path, dk);
}

// ── formatting ──────────────────────────────────────────────────────────────
export const pct = (x, digits = 1) => x == null || Number.isNaN(x) ? "—" : (100 * x).toFixed(digits) + "%";
export const pct0 = x => pct(x, 0);
export const num = (x, digits = 0) => x == null ? "—" : Number(x).toLocaleString("en-US", { maximumFractionDigits: digits, minimumFractionDigits: digits });
/** "41–63%" from a [lo, hi] pair of rates. */
export const interval = ci => ci && ci.length === 2 ? `${(100 * ci[0]).toFixed(0)}–${(100 * ci[1]).toFixed(0)}%` : "—";
/** "±9 pts" half-width of an interval. */
export const halfWidth = ci => ci && ci.length === 2 ? `±${(50 * (ci[1] - ci[0])).toFixed(0)} pts` : "";
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
/** "29 Oct 2026" from an ISO date or datetime. */
export function fmtDate(iso) { if (!iso) return "—"; const d = new Date(iso); if (Number.isNaN(+d)) return String(iso).slice(0, 10); return `${d.getUTCDate()} ${MONTHS[d.getUTCMonth()]} ${d.getUTCFullYear()}`; }
/** "29 Oct 2026, 16:17 UTC" */
export function fmtDateTime(iso) { if (!iso) return "—"; const d = new Date(iso); if (Number.isNaN(+d)) return String(iso).replace("T", " ").slice(0, 16); return `${fmtDate(iso)}, ${String(d.getUTCHours()).padStart(2, "0")}:${String(d.getUTCMinutes()).padStart(2, "0")} UTC`; }
/** "3 hours ago", "yesterday", "12 days ago" */
export function ago(iso, now = Date.now()) {
  if (!iso) return "—"; const t = +new Date(iso); if (Number.isNaN(t)) return String(iso);
  const s = Math.max(0, (now - t) / 1000), m = s / 60, h = m / 60, d = h / 24;
  if (m < 2) return "just now"; if (h < 1) return `${Math.round(m)} min ago`; if (h < 36) return `${Math.round(h)} hour${Math.round(h) === 1 ? "" : "s"} ago`;
  if (d < 2) return "yesterday"; if (d < 30) return `${Math.round(d)} days ago`; return fmtDate(iso);
}
/** "1 h 20 min", "45 s" from seconds. */
export function duration(sec) { if (sec == null) return "—"; if (sec < 60) return `${Math.round(sec)} s`; const m = sec / 60; if (m < 90) return `${Math.round(m)} min`; const h = Math.floor(m / 60); return `${h} h ${Math.round(m - 60 * h)} min`; }
export const plural = (n, one, many = one + "s") => `${num(n)} ${n === 1 ? one : many}`;

// ── statistics ──────────────────────────────────────────────────────────────
/** Wilson 95% interval [lo, hi] for `wins` out of `n` (the lab's riftlab.py uses the same). */
export function wilson(wins, n, z = 1.96) {
  if (!n) return null; const p = wins / n, z2 = z * z, den = 1 + z2 / n, c = p + z2 / (2 * n), h = z * Math.sqrt(p * (1 - p) / n + z2 / (4 * n * n));
  return [Math.max(0, (c - h) / den), Math.min(1, (c + h) / den)];
}
/** Do two intervals overlap? (overlapping = the lists are not separated) */
export const overlaps = (a, b) => !!(a && b) && a[0] <= b[1] && b[0] <= a[1];
/** Pool a deck's matchups: {games, wins, rate, ci} unweighted, plus the weighted rate the test reports. */
export function pooled(deck) {
  const ms = deck.matchups || []; const games = ms.reduce((a, m) => a + (m.games || 0), 0), wins = ms.reduce((a, m) => a + (m.wins || 0), 0);
  return { games, wins, rate: games ? wins / games : null, ci: wilson(wins, games), weighted: deck.weighted_win_rate };
}

// ── colours ─────────────────────────────────────────────────────────────────
export const DOMAINS = { fury: "#d4483b", calm: "#27a97e", mind: "#3f86e0", body: "#e38a2e", chaos: "#8e5cf0", order: "#e1b83a" };
export const DOMAIN_NAMES = Object.keys(DOMAINS);
export const domainColor = d => DOMAINS[String(d || "").toLowerCase()] || "#4a4466";
export const domainDot = d => `<i class="dom ${esc(String(d).toLowerCase())}" title="${esc(d)}"></i>`;
export const domainDots = ds => `<span class="doms">${(ds || []).map(domainDot).join("")}</span>`;
/** Colour for a win rate: win (≥55%), loss (≤45%), warn between, neutral when unknown. */
export const rateTone = r => r == null ? "neutral" : r >= 0.55 ? "win" : r <= 0.45 ? "loss" : "warn";
export const rateColor = r => `var(--${rateTone(r)})`;

// ── cards and decks ─────────────────────────────────────────────────────────
export const cardById = id => byId.get(+id) || null;
/** Find a card by name; legends also answer to "Tag, Name" ("Riven, The Exile"). */
export function cardByName(name) {
  if (!name) return null; const k = String(name).toLowerCase();
  if (byName.has(k)) return byName.get(k);
  const i = k.indexOf(", "); if (i > 0 && byName.has(k.slice(i + 2))) return byName.get(k.slice(i + 2));
  return null;
}
export const card = ref => ref == null ? null : typeof ref === "object" ? ref : typeof ref === "number" ? cardById(ref) : cardByName(ref);
/** First names of the champion cards ("Kennen", "Kai'Sa"): the tag a legend is named after. */
function championNames() {
  if (!champNames) { champNames = new Set(); for (const c of (D && D.cards) || []) if (c.super === "champion") champNames.add(String(c.name).split(",")[0].trim().toLowerCase()); }
  return champNames;
}
/** The champion's name for a legend: "Kennen" for "Heart of the Tempest", "Kennen, Heart of the Tempest" or "Dark Child - Starter"
    (the legend's champion tag, never a region or race tag), else the name up to the first comma. */
export function legendShort(name) {
  const s = String(name || "?"), c = cardByName(s);
  if (c && c.type === "legend") {
    const tags = c.tags || [], i = s.indexOf(", ");
    if (i > 0 && tags.some(t => t.toLowerCase() === s.slice(0, i).toLowerCase())) return s.slice(0, i);
    const hit = tags.find(t => championNames().has(t.toLowerCase()));
    return hit || tags[0] || s.split(", ")[0];
  }
  return s.split(", ")[0];
}
/** Deck records by path, and a readable label built from the file's first comment line. */
export const deckByPath = p => byPath.get(p) || null;
export const allDecks = () => (D && D.decks) || [];
/** Tournament provenance from the deck file's first comment ("1 - Simple - Kennen"): {place, pilot, name} or null. */
export function deckPilot(deckOrPath) {
  const d = typeof deckOrPath === "string" ? deckByPath(deckOrPath) : deckOrPath;
  const m = (((d && d.comments) || [])[0] || "").match(/^(\d+) - (.+?) - (.+)$/);
  if (m) return { place: +m[1], pilot: m[2], name: m[3] };
  // "Riven, The Exile — Nam Dang, 1st of 755, Riftbound Showdown … (2026-10-24)." (the reference list's own header)
  const r = (((d && d.comments) || [])[0] || "").match(/^.+? — ([^,]+), (\d+)(?:st|nd|rd|th) of \d+/);
  return r ? { place: +r[2], pilot: r[1], name: legendShort(d.legend) } : null;
}
/** Event slug of a tournament list ("sacramento_showdown_2026-10-24"), "" for the others. */
export const deckEvent = p => { const m = String(typeof p === "string" ? p : (p && p.path) || "").match(/^lab\/decks\/meta\/([^/]+)\//); return m ? m[1] : ""; };
/** "Sacramento showdown, 24 Oct 2026" from an event slug; {date:false} drops the date. Short tokens with digits stay upper case ("RRO25"). */
export function eventLabel(slug, opts = {}) {
  const s = String(slug || ""); if (!s) return "";
  const m = s.match(/^(.*?)_?(\d{4}-\d{2}-\d{2})$/), words = (m ? m[1] : s).split("_").filter(Boolean);
  const name = words.map((w, i) => /\d/.test(w) && w.length <= 6 ? w.toUpperCase() : i === 0 ? w[0].toUpperCase() + w.slice(1) : w).join(" ");
  return m && opts.date !== false ? `${name}${name ? ", " : ""}${fmtDate(m[2])}` : name;
}
export const prettyEvent = eventLabel;
/** The champion's name a deck is built around: from its legend ("Kennen"), else from its champion card. */
export function deckHero(deckOrPath) {
  const d = typeof deckOrPath === "string" ? deckByPath(deckOrPath) : deckOrPath; if (!d) return String(deckOrPath || "?");
  if (d.legend) return legendShort(d.legend);
  const ch = d.champion || Object.keys((d.list || {}).champion || {})[0] || ""; return ch.split(", ")[0] || "?";
}
export function deckLabel(p) {
  const d = deckByPath(p); if (!d) return p;
  const m = deckPilot(d);
  if (m && deckEvent(p)) return `${m.name} — ${m.pilot} (#${m.place}, ${p.split("/")[3] || ""})`;
  return p.replace(/^decks\//, "").replace(/^lab\/variants\//, "variant ").replace(/\.txt$/, "");
}
/** "Kennen (Dylan Sumner)": the champion's name plus the pilot when the file says so; a variant's stem; else the champion's name. */
export function deckShort(p) {
  const d = deckByPath(p); if (!d) return String(p || "?").split("/").pop().replace(/\.txt$/, "");
  const m = deckPilot(d);
  if (m) return `${m.name} (${m.pilot})`;
  if (d.path.startsWith("lab/variants/")) return d.path.split("/").pop().replace(/\.txt$/, "");
  return deckHero(d);
}
/** Set codes → names (UNL stays a code: the engine's unlisted cards). */
export const SETS = { OGN: "Origins", OGS: "Origins starter", SFD: "Spiritforged", VEN: "Vendetta", RAD: "Radiance" };
export const setName = code => SETS[code] ? `${SETS[code]} (${code})` : String(code || "");
/** "net_004" from "lab/learn/nets/net_004.rbvn". */
export const netName = p => String(p || "").split("/").pop().replace(/\.rbvn$/, "") || "—";
/** Reads an agent spec into {kind, sims, eval, net, raw, label, short, text}.
    "ismcts:sims=40,eval=net:lab/learn/nets/net_004.rbvn" → label "Thinking bot · 40 sims · net 4" (badges), text "the bot (40
    continuations per move, network 4)" (prose); "random" → "Random player" / "a random player"; "human:Name" → the name. */
export function botSpec(raw) {
  const s = String(raw || ""), out = { raw: s, kind: "unknown", sims: null, eval: null, net: null };
  if (!s) return { ...out, label: "unknown player", short: "unknown", text: "an unknown player" };
  if (/^human:/.test(s)) { const n = s.slice(6); return { ...out, kind: "human", label: n, short: n, text: n }; }
  if (/^random$/i.test(s)) return { ...out, kind: "random", label: "Random player", short: "random", text: "a random player" };
  if (/^(ismcts|mcts)/i.test(s)) {
    out.kind = "ismcts";
    const sims = s.match(/sims=(\d+)/); if (sims) out.sims = +sims[1];
    const ev = s.match(/eval=([a-z]+)(?::([^,]+))?/i); if (ev) { out.eval = ev[1]; if (ev[2]) out.net = ev[2]; }
    const parts = ["Thinking bot"], prose = []; if (out.sims) { parts.push(`${out.sims} sims`); prose.push(`${out.sims} continuations per move`); }
    if (out.eval === "net") { const n = (out.net || "").match(/net_(\d+)/); parts.push(n ? `net ${+n[1]}` : "value net"); prose.push(n ? `network ${+n[1]}` : "a value network"); }
    else if (out.eval === "board") { parts.push("board eval"); prose.push("board heuristic"); } else if (out.eval) { parts.push(`${out.eval} eval`); prose.push(`${out.eval} heuristic`); } else { parts.push("score eval"); prose.push("score heuristic"); }
    return { ...out, label: parts.join(" · "), short: parts.slice(0, 2).join(" · "), text: `the bot (${prose.join(", ")})` };
  }
  if (/^greedy/i.test(s)) return { ...out, kind: "greedy", label: "One-ply bot", short: "greedy", text: "the one-ply bot" };
  return { ...out, label: s, short: s, text: s };
}
/** A readable badge for a bot, the raw spec in the tooltip. */
export function botBadge(raw, cls = "") {
  const b = botSpec(raw), kind = b.kind === "random" ? "neutral" : b.kind === "human" ? "gold" : "bot";
  return `<span class="badge ${kind} ${cls}" title="${esc(b.raw)}">${esc(b.label)}</span>`;
}
export function groupDecks(decks = allDecks()) {
  const g = { "Riven lists": [], "Variants": [], "Meta (tournaments)": [], "Engine test decks": [] };
  decks.forEach(d => {
    if (d.path.startsWith("lab/variants/")) g["Variants"].push(d);
    else if (d.path.startsWith("lab/decks/meta/")) g["Meta (tournaments)"].push(d);
    else if (/riven_/.test(d.path)) g["Riven lists"].push(d);
    else g["Engine test decks"].push(d);
  });
  return g;
}
/** The legend card of a deck, or its champion when the legend has no art, or null. */
export function deckFace(deckOrPath) {
  const d = typeof deckOrPath === "string" ? deckByPath(deckOrPath) : deckOrPath; if (!d) return null;
  const leg = cardByName(d.legend); if (leg && leg.image_url) return leg;
  const ch = cardByName(d.champion || Object.keys((d.list || {}).champion || {})[0]); return ch || leg || null;
}
/** Face card for a legend name as human games record it ("The Exile", "Curator of the Sands"). */
export function legendFace(name) { const c = cardByName(name); if (c && c.image_url) return c; const tag = c && (c.tags || [])[0]; if (tag) { const ch = (D.cards || []).find(x => x.super === "champion" && (x.tags || []).includes(tag) && x.image_url); if (ch) return ch; } return c; }
export const isPlumbing = t => /^random$/.test(t.agent || "") && /^random$/.test(t.opp_agent || t.agent || "");
/** Tests newest first. */
export const testsByDate = () => [...(D.tests || [])].sort((a, b) => String(b.started_utc).localeCompare(String(a.started_utc)));
export const testById = id => (D.tests || []).find(t => t.id === id) || null;
/** Best deck across tests with a thinking bot: {test, deck, pooled} or null. Ranked by the lower bound of the 95% interval, so a
    list with many games beats one with the same rate over a handful (an optimizer candidate cut after round 1 cannot be "best"). */
export function bestList() {
  let best = null, bestLo = -1;
  for (const t of D.tests || []) if (!isPlumbing(t) && !/random/.test(t.opp_agent || "")) for (const d of t.decks || []) {
    if (d.weighted_win_rate == null) continue;
    const p = pooled(d), lo = p.ci ? p.ci[0] : -1;
    if (!best || lo > bestLo || (lo === bestLo && d.weighted_win_rate > best.deck.weighted_win_rate)) { best = { test: t, deck: d, pooled: p }; bestLo = lo; }
  }
  return best;
}
/** A one-line conclusion for a test, written for a player. */
export function testConclusion(t) {
  const decks = t.decks || [], n = decks.length, games = decks.reduce((a, d) => a + pooled(d).games, 0);
  if (isPlumbing(t)) return `Plumbing check: ${num(games)} games with random players ran through the engine without error. Says nothing about deck strength.`;
  const ranked = decks.filter(d => d.weighted_win_rate != null).sort((a, b) => b.weighted_win_rate - a.weighted_win_rate);
  if (!ranked.length) return "No result recorded.";
  const top = ranked[0], tp = pooled(top), opps = (top.matchups || []).length;
  if (n === 1) {
    const vs = opps === 1 ? `against ${top.matchups[0].opponent}` : `against ${opps} opponents`;
    if (/^(ismcts|mcts|greedy)/i.test(top.label || "")) { const b = botSpec(top.label).text; return `${b[0].toUpperCase()}${b.slice(1)} wins ${pct0(top.weighted_win_rate)} of ${num(tp.games)} games ${/random/.test(t.opp_agent || "") ? "against a random player" : vs}, interval ${interval(tp.ci)}.`; }
    if (/random/.test(t.opp_agent || "")) { const b = botSpec(t.agent).text; return `${b[0].toUpperCase()}${b.slice(1)} wins ${pct0(top.weighted_win_rate)} of ${num(tp.games)} games against a random player, interval ${interval(tp.ci)}.`; }
    return `${top.label} wins ${pct0(top.weighted_win_rate)} ${vs} over ${num(tp.games)} games, interval ${interval(tp.ci)}.`;
  }
  const base = ranked.find(d => /base|control/i.test(d.label)) || ranked[ranked.length - 1];
  const bp = pooled(base), sep = overlaps(tp.ci, bp.ci) ? "their intervals overlap, so the gap is not yet established" : "the intervals do not overlap";
  return `Best of ${n} lists: ${top.label} at ${pct0(top.weighted_win_rate)}${base !== top ? `, ${/base|control/i.test(base.label) ? "the base list" : base.label} at ${pct0(base.weighted_win_rate)}; ${sep}` : ""}.`;
}
/** Legends on each side of a test: {mine:[card], theirs:[card]} (deduplicated). */
export function testFaces(t) {
  const uniq = arr => { const seen = new Set(), out = []; for (const c of arr) if (c && !seen.has(c.name)) { seen.add(c.name); out.push(c); } return out; };
  const mine = uniq((t.decks || []).map(d => deckFace(d.path)));
  const theirs = uniq((t.decks || []).flatMap(d => (d.matchups || []).map(m => deckFace(m.opponent_deck))));
  return { mine, theirs };
}

// ── card imagery ────────────────────────────────────────────────────────────
/** CDN rendition: small webp from cmsassets.rgpub.io (same rule as board.js). */
export function cardImage(url, w) {
  if (!url) return "";
  if (!/cmsassets\.rgpub\.io/.test(url)) return url;
  return url + (url.includes("?") ? "&" : "?") + `w=${w}&fm=webp&q=78`;
}
export const isLandscape = c => !!(c && (c.type === "battlefield" || /10[34]\dx744/.test(c.image_url || "")));
const domStyle = c => { const d = (c && c.domains) || []; const a = domainColor(d[0]), b = d[1] ? domainColor(d[1]) : a; return `--d1:${a};--d2:${b}`; };
/** The styled frame shown when a card has no image (or it fails to load). */
export function fallbackFrame(c, name) {
  const cost = c && /^(unit|spell|gear)$/.test(c.type) ? c.energy : null;
  return `<div class="cfb" style="${domStyle(c)}">${cost != null ? `<span class="cfb-c">${cost}</span>` : ""}<div class="cfb-n">${esc(name || (c && c.name) || "?")}</div><div class="cfb-t">${esc((c && c.type) || "card")}</div></div>`;
}
const SIZE_W = { xs: 120, sm: 200, md: 300, lg: 400, xl: 600 };
/** A card picture. ref: card | name | id. opts: size xs|sm|md|lg|xl, w (CDN px), crop (square art crop), hover (preview), count, cls, alt. */
export function cardImg(ref, opts = {}) {
  const c = card(ref), name = opts.alt || (c && c.name) || (typeof ref === "string" ? ref : "card");
  const size = opts.size || "md", w = opts.w || SIZE_W[size] || 300;
  const src = c && c.image_url ? cardImage(c.image_url, w) : "";
  const cls = ["card-img", size, opts.crop ? "crop" : "", isLandscape(c) && !opts.crop ? "land" : "", opts.hover !== false && c ? "hover" : "", opts.cls || ""].filter(Boolean).join(" ");
  const preview = opts.hover !== false && c ? ` data-card="${esc(c.name)}" tabindex="0" role="img" aria-label="${esc(name)}"` : ` role="img" aria-label="${esc(name)}"`;
  return `<span class="${cls}"${preview} style="${domStyle(c)}">${src ? `<img src="${esc(src)}" alt="" referrerpolicy="no-referrer" loading="lazy" decoding="async" data-name="${esc(name)}">` : fallbackFrame(c, name)}${opts.count != null ? `<span class="cg-n">×${opts.count}</span>` : ""}</span>`;
}
/** Round art crop for a legend / champion. size sm|md|lg|xl. */
export function portrait(ref, opts = {}) {
  const c = typeof ref === "string" && !card(ref) ? legendFace(ref) : card(ref);
  const name = opts.alt || (c && c.name) || String(ref || "?");
  const src = c && c.image_url ? cardImage(c.image_url, opts.w || 200) : "";
  return `<span class="portrait ${opts.size || "md"} ${opts.cls || ""}" title="${esc(name)}" role="img" aria-label="${esc(name)}"${c ? ` data-card="${esc(c.name)}" tabindex="0"` : ""} style="${domStyle(c)}">${src ? `<img src="${esc(src)}" alt="" referrerpolicy="no-referrer" loading="lazy" decoding="async" data-name="${esc(name)}">` : fallbackFrame(c, legendShort(name))}</span>`;
}
/** Several portraits overlapping; `max` shown, the rest counted. */
export function portraits(refs, opts = {}) {
  const max = opts.max || 4, shown = refs.slice(0, max);
  return `<span class="portraits" style="--w:${{ sm: 28, md: 40, lg: 56, xl: 88 }[opts.size || "md"]}px">${shown.map(r => portrait(r, opts)).join("")}${refs.length > max ? `<span class="more">+${refs.length - max}</span>` : ""}</span>`;
}
/** Inline chip: tiny art crop + name (+ count). Hover / tap opens the card preview. {preview:false} drops the chip's own focus
    stop (for a chip inside a button or link: hover still previews, the parent keeps the keyboard). */
export function cardChip(name, opts = {}) {
  const c = card(name), label = (c && c.name) || String(name);
  const src = c && c.image_url ? cardImage(c.image_url, 120) : "";
  const hook = c ? ` data-card="${esc(c.name)}"${opts.preview === false ? "" : ` tabindex="0"`}` : "";
  return `<span class="card-chip ${opts.cls || ""}"${hook}><span class="cc-art" style="${domStyle(c)}">${src ? `<img src="${esc(src)}" alt="" referrerpolicy="no-referrer" loading="lazy" decoding="async">` : fallbackFrame(c, label)}</span><span class="cc-name">${esc(label)}</span>${opts.count != null ? `<span class="cc-n">×${opts.count}</span>` : ""}</span>`;
}
/** Card names (3+ letters) longest first, for finding them inside free text. */
function nameIndex() {
  if (nameList) return nameList; const seen = new Set(); nameList = [];
  for (const c of (D && D.cards) || []) { const l = String(c.name || "").toLowerCase(); if (l.length < 3 || seen.has(l)) continue; seen.add(l); nameList.push({ l, c }); }
  nameList.sort((a, b) => b.l.length - a.l.length); return nameList;
}
const isWordCh = ch => /[a-z0-9]/i.test(ch || "");
/** Card names found in a text (whole words, longest match first, non-overlapping): [{start, end, card}] in text order. */
export function findCards(text) {
  const t = String(text || "").toLowerCase(), out = []; if (!t) return out;
  const taken = new Uint8Array(t.length);
  for (const e of nameIndex()) {
    let from = 0, i;
    while ((i = t.indexOf(e.l, from)) >= 0) {
      from = i + 1; const j = i + e.l.length;
      if (isWordCh(t[i - 1]) || isWordCh(t[j])) continue;
      let free = true; for (let k = i; k < j; k++) if (taken[k]) { free = false; break; }
      if (!free) continue;
      taken.fill(1, i, j); out.push({ start: i, end: j, card: e.c });
    }
  }
  return out.sort((a, b) => a.start - b.start);
}
/** Free text (an option, a log line) with every card it names turned into a card chip. opts: {cls: "sm", preview} as cardChip. */
export function chipText(text, opts = {}) {
  const s = String(text ?? ""), m = findCards(s); if (!m.length) return esc(s);
  let out = "", pos = 0;
  for (const x of m) { out += esc(s.slice(pos, x.start)) + cardChip(x.card, { cls: opts.cls ?? "sm", preview: opts.preview }); pos = x.end; }
  return out + esc(s.slice(pos));
}
export const optionHtml = chipText;
/** Grid of real card images from a {name: count} list, champion first then by cost. */
export function cardGrid(list, opts = {}) {
  const champ = new Set(Object.keys(opts.champion || {}));
  const rows = Object.entries(list || {}).map(([n, k]) => ({ n, k, c: card(n) }));
  rows.sort((a, b) => (champ.has(b.n) - champ.has(a.n)) || ((a.c && a.c.energy) ?? 99) - ((b.c && b.c.energy) ?? 99) || a.n.localeCompare(b.n));
  return `<div class="card-grid" style="--w:${opts.w || 96}px">${rows.map(r => `<figure>${cardImg(r.c || r.n, { size: "md", w: 300, count: r.k, cls: champ.has(r.n) ? "champion" : "", alt: r.n })}${opts.captions ? `<figcaption>${esc(r.n)}</figcaption>` : ""}</figure>`).join("")}</div>`;
}

// ── global card preview layer (index.html provides #card-preview + #card-backdrop) ──
let pv = null, pvBack = null, pvName = null, pvAnchor = null, touch = false, raf = 0, pvInstalled = false;
function previewHtml(c) {
  const land = isLandscape(c), src = c.image_url ? cardImage(c.image_url, land ? 800 : 600) : "";
  const line = [c.type, c.super, c.energy != null && /^(unit|spell|gear)$/.test(c.type) ? `cost ${c.energy}${c.power ? " + " + c.power + " power" : ""}` : "", c.type === "unit" && c.might != null ? `${c.might} might` : "", (c.tags || []).join(", ")].filter(Boolean).join(" · ");
  return `<button type="button" class="cp-close" aria-label="close">×</button><div class="cp-card ${land ? "land" : ""}" style="${domStyle(c)}">${src ? `<img src="${esc(src)}" alt="${esc(c.name)}" referrerpolicy="no-referrer" decoding="async">` : fallbackFrame(c, c.name)}</div>
    <div class="cp-info"><b>${esc(c.name)}</b>${line ? `<div>${esc(line)}</div>` : ""}${(c.domains || []).length ? `<div>${(c.domains || []).map(d => `${domainDot(d)}${esc(d)}`).join(" ")}</div>` : ""}${c.text ? `<div class="small muted">${esc(c.text)}</div>` : ""}</div>`;
}
function placePreview(x, y) {
  if (!pv || pv.classList.contains("modal")) return;
  const w = 260, h = pv.offsetHeight || 440, vw = innerWidth, vh = innerHeight;
  let left = x + 22, top = y - h / 2;
  if (left + w > vw - 8) left = x - w - 22; if (left < 8) left = 8;
  top = Math.max(8, Math.min(vh - h - 8, top));
  pv.style.left = left + "px"; pv.style.top = top + "px";
}
export function showPreview(name, { modal = false, x = 0, y = 0, anchor = null } = {}) {
  const c = card(name); if (!c || !pv) return;
  if (pvName !== c.name || modal) { pv.innerHTML = previewHtml(c); pvName = c.name; }
  pvAnchor = anchor;
  pv.classList.toggle("modal", modal); pvBack.classList.toggle("on", modal);
  pv.classList.add("on"); pv.setAttribute("aria-hidden", "false");
  if (!modal) placePreview(x, y);
}
export function hidePreview() { if (!pv) return; pv.classList.remove("on", "modal"); pvBack.classList.remove("on"); pv.setAttribute("aria-hidden", "true"); pvName = null; pvAnchor = null; }
/** Installs the delegated hover / focus / tap handlers for [data-card] elements and the image-error fallback. Idempotent. */
export function installCardPreview() {
  if (pvInstalled) return; pvInstalled = true;
  pv = $("#card-preview"); pvBack = $("#card-backdrop");
  if (!pv) { pv = el("div", { id: "card-preview", className: "card-preview" }); pvBack = el("div", { id: "card-backdrop", className: "card-backdrop" }); document.body.append(pvBack, pv); }
  pv.setAttribute("aria-hidden", "true");
  const inBoard = t => !!t.closest(".rb");   // RiftBoard has its own zoom layer
  document.addEventListener("pointerdown", e => { touch = e.pointerType === "touch"; }, true);
  document.addEventListener("mouseover", e => { if (touch) return; const t = e.target.closest("[data-card]"); if (t && !inBoard(t)) showPreview(t.dataset.card, { x: e.clientX, y: e.clientY, anchor: t }); });
  document.addEventListener("mousemove", e => { if (touch || !pv.classList.contains("on") || pv.classList.contains("modal") || raf) return; raf = requestAnimationFrame(() => { raf = 0; placePreview(e.clientX, e.clientY); }); });
  document.addEventListener("mouseout", e => { const t = e.target.closest("[data-card]"); if (t && !(e.relatedTarget && t.contains(e.relatedTarget)) && !pv.classList.contains("modal")) hidePreview(); });
  document.addEventListener("focusin", e => { const t = e.target.closest("[data-card]"); if (t && !inBoard(t)) { const r = t.getBoundingClientRect(); showPreview(t.dataset.card, { x: r.right, y: r.top + r.height / 2, anchor: t }); } });
  document.addEventListener("focusout", e => { const t = e.target.closest("[data-card]"); if (t && !pv.classList.contains("modal")) hidePreview(); });
  document.addEventListener("click", e => {
    if (e.target.closest(".cp-close") || e.target === pvBack) { hidePreview(); return; }
    const t = e.target.closest("[data-card]"); if (!t || inBoard(t) || t.closest("a[href]")) return;
    if (touch || e.detail === 0) { showPreview(t.dataset.card, { modal: true, anchor: t }); e.preventDefault(); }
  });
  document.addEventListener("keydown", e => { if (e.key === "Escape") hidePreview(); const t = e.target.closest && e.target.closest("[data-card]"); if (t && !inBoard(t) && (e.key === "Enter" || e.key === " ")) { e.preventDefault(); showPreview(t.dataset.card, { modal: true, anchor: t }); } });
  pvBack.addEventListener("click", hidePreview);
  // the hovered / focused element was re-rendered away (a repaint under the pointer): drop a hover preview that has no anchor left
  new MutationObserver(() => { if (pvAnchor && !pvAnchor.isConnected && pv.classList.contains("on") && !pv.classList.contains("modal")) hidePreview(); })
    .observe(document.body, { childList: true, subtree: true });
  // image failed: swap in the styled frame (outside the table; board.js handles its own)
  document.addEventListener("error", e => {
    const im = e.target; if (!(im instanceof HTMLImageElement) || im.closest(".rb")) return;
    const box = im.closest(".card-img, .portrait, .cc-art, .cp-card"); if (!box) return;
    const name = im.dataset.name || im.alt || box.getAttribute("aria-label") || box.dataset.card || "?";
    im.remove(); box.insertAdjacentHTML("afterbegin", fallbackFrame(card(box.dataset.card || box.closest("[data-card]")?.dataset.card || name), name));
  }, true);
}

// ── components (return HTML) ────────────────────────────────────────────────
/** KPI tile. {label, value, unit, sub, tone: win|loss|warn|info|neutral, art (html), href} */
export function kpi({ label, value, unit = "", sub = "", tone = "", art = "", href = "" }) {
  const inner = `<span class="kpi-label">${esc(label)}</span><span class="kpi-value">${value == null ? "—" : esc(value)}${unit ? `<small>${esc(unit)}</small>` : ""}</span>${sub ? `<span class="kpi-sub">${sub}</span>` : ""}${art ? `<span class="kpi-art">${art}</span>` : ""}`;
  return href ? `<a class="kpi ${tone}" href="${esc(href)}">${inner}</a>` : `<div class="kpi ${tone}">${inner}</div>`;
}
export const badge = (text, kind = "") => `<span class="badge ${kind}">${esc(text)}</span>`;
export const chip = (html, cls = "") => `<span class="chip ${cls}">${html}</span>`;
/** Page header: title, subtitle, actions (html). */
export const pageHead = ({ title, sub = "", actions = "", eyebrow = "" }) => `<header class="page-head"><div>${eyebrow ? `<div class="eyebrow">${esc(eyebrow)}</div>` : ""}<h1>${esc(title)}</h1>${sub ? `<p class="page-sub">${sub}</p>` : ""}</div>${actions ? `<div class="page-actions">${actions}</div>` : ""}</header>`;
export const empty = (title, hint = "") => `<div class="empty"><b>${esc(title)}</b>${hint ? `<div class="hint">${hint}</div>` : ""}</div>`;
export const loadingBox = (msg = "Loading…") => `<div class="loading"><span class="spinner"></span>${esc(msg)}</div>`;
export const errorBox = msg => `<div class="notice error">${esc(msg)}</div>`;
export const notice = (html, kind = "") => `<div class="notice ${kind}">${html}</div>`;
/** Win-rate bar: a dot at the rate on a 0–100 track, the 95% interval as a translucent band, 50% marked. */
export function rateBar(rate, ci, w = 120) {
  const x = v => Math.max(0, Math.min(w, v * w)), color = rateColor(rate);
  return `<svg width="${w}" height="14" viewBox="0 0 ${w} 14" role="img" aria-label="rate ${pct(rate)}${ci ? ", interval " + interval(ci) : ""}">
    <rect x="0" y="5" width="${w}" height="4" rx="2" fill="var(--s0)" stroke="var(--line)"/>
    <line x1="${w / 2}" x2="${w / 2}" y1="1" y2="13" stroke="var(--line-strong)"/>
    ${ci ? `<rect x="${x(ci[0])}" y="3" width="${Math.max(1, x(ci[1]) - x(ci[0]))}" height="8" rx="4" fill="${color}" opacity=".28"/><line x1="${x(ci[0])}" x2="${x(ci[0])}" y1="3" y2="11" stroke="${color}" opacity=".8"/><line x1="${x(ci[1])}" x2="${x(ci[1])}" y1="3" y2="11" stroke="${color}" opacity=".8"/>` : ""}
    ${rate == null ? "" : `<circle cx="${x(rate)}" cy="7" r="4" fill="${color}" stroke="var(--s0)" stroke-width="1.5"/>`}</svg>`;
}
/** Rate with bar, value and interval text. */
export const rate = (r, ci, w = 120) => `<span class="rate">${rateBar(r, ci, w)}<span class="rate-v">${pct(r)}</span>${ci ? `<span class="rate-ci">${interval(ci)}</span>` : ""}</span>`;
/** Data table. columns: [{key, label, num, sort: true|fn(row), render(row) -> html, cls, width}]. rows: objects.
    opts: {rowAttrs(row) -> string, cls, sticky:true, sort: {key, dir}, empty: "message"}. Sorting is wired by enhance(root). */
export function table(columns, rows, opts = {}) {
  const sortVal = (c, r) => typeof c.sort === "function" ? c.sort(r) : r[c.key];
  const head = columns.map(c => `<th class="${c.num ? "num" : ""} ${c.cls || ""}" ${c.sort ? `data-sort="${c.num ? "num" : "text"}"` : ""} ${opts.sort && opts.sort.key === c.key ? `aria-sort="${opts.sort.dir === "asc" ? "ascending" : "descending"}"` : ""} ${c.width ? `style="width:${c.width}"` : ""}>${esc(c.label)}</th>`).join("");
  const body = rows.length ? rows.map(r => `<tr ${opts.rowAttrs ? opts.rowAttrs(r) : ""}>${columns.map(c => { const v = sortVal(c, r); return `<td class="${c.num ? "num" : ""} ${c.cls || ""}" ${c.sort ? `data-v="${esc(v == null ? "" : v)}"` : ""}>${c.render ? c.render(r) : esc(v == null ? "—" : v)}</td>`; }).join("")}</tr>`).join("")
    : `<tr><td colspan="${columns.length}"><div class="empty" style="border:0">${esc(opts.empty || "Nothing to show.")}</div></td></tr>`;
  return `<div class="tablewrap ${opts.cls || ""}"><table class="data"><thead><tr>${head}</tr></thead><tbody>${body}</tbody></table></div>`;
}
/** Wire behaviours inside a freshly rendered root: sortable headers. Call after setting innerHTML. */
export function enhance(root = document) {
  $$("th[data-sort]", root).forEach(th => { if (th.dataset.wired) return; th.dataset.wired = "1"; th.addEventListener("click", () => sortTable(th)); });
}
function sortTable(th) {
  const table = th.closest("table"), tbody = table.tBodies[0], idx = [...th.parentNode.children].indexOf(th);
  const dir = th.getAttribute("aria-sort") === "ascending" ? "descending" : "ascending";
  [...th.parentNode.children].forEach(x => x.removeAttribute("aria-sort")); th.setAttribute("aria-sort", dir);
  const numeric = th.dataset.sort === "num";
  const key = tr => { const td = tr.children[idx]; const v = td && (td.dataset.v ?? td.textContent.trim()); return numeric ? (v === "" || v == null ? -Infinity : +v) : String(v || "").toLowerCase(); };
  const rows = [...tbody.rows].filter(r => r.children.length > 1 || !r.querySelector(".empty"));
  rows.sort((a, b) => { const x = key(a), y = key(b); return (x < y ? -1 : x > y ? 1 : 0) * (dir === "ascending" ? 1 : -1); });
  rows.forEach(r => tbody.append(r));
}
/** Toast at the bottom of the page. kind: ok | error | "" */
export function toast(msg, kind = "", ms = 4000) {
  let host = $(".toast-host"); if (!host) { host = el("div", { className: "toast-host" }); document.body.append(host); }
  const t = el("div", { className: `toast ${kind}`, textContent: msg, role: "status" }); host.append(t);
  setTimeout(() => t.remove(), ms); return t;
}
/** Modal dialog. Returns a promise resolved with the clicked action's value (or null on dismiss).
    actions: [{label, value, primary}] */
export function modal({ title, body = "", actions = [{ label: "Close", value: null }] }) {
  return new Promise(resolve => {
    const back = el("div", { className: "modal-backdrop" });
    back.innerHTML = `<div class="modal" role="dialog" aria-modal="true" aria-label="${esc(title)}"><button class="btn-ghost icon-btn modal-close" aria-label="close">×</button><h2>${esc(title)}</h2><div>${body}</div><div class="modal-actions">${actions.map((a, i) => `<button data-i="${i}" class="${a.primary ? "primary" : ""}">${esc(a.label)}</button>`).join("")}</div></div>`;
    const done = v => { back.remove(); document.removeEventListener("keydown", onKey); resolve(v); };
    const onKey = e => { if (e.key === "Escape") done(null); };
    back.addEventListener("click", e => { if (e.target === back || e.target.closest(".modal-close")) done(null); const b = e.target.closest("button[data-i]"); if (b) done(actions[+b.dataset.i].value); });
    document.addEventListener("keydown", onKey); document.body.append(back); back.querySelector("button.primary, button[data-i]")?.focus();
  });
}
/** A small inline SVG line/bar chart helper is deliberately not here: draw charts by hand in the view (see Bot view) with svg text styled by app.css. */

// ── claude.ai runtime (db, user, mcp) — null outside claude.ai ──────────────
let rt = null;
export function runtime() {
  if (rt) return rt;
  const c = window.claude;
  if (!(c && c.use)) return (rt = Promise.resolve({ db: null, user: null, mcp: null, meId: "" }));
  rt = Promise.all([c.use("db").catch(() => null), c.use("user").catch(() => null), c.use("mcp").catch(() => null)]).then(async ([db, user, mcp]) => {
    let meId = ""; if (user) { try { meId = await user.id(); } catch (e) { meId = ""; } }
    return { db, user, mcp, meId };
  });
  return rt;
}
