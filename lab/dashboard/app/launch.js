/* Riven Lab — Launch: set up a test and hand it to Claude, who runs it on GitHub Actions and republishes the page.
   Two kinds of request: compare lists (lab/riftlab.py run) and search for a better list (lab/optimize.py, successive
   halving over one-for-one swaps). The draft lives in localStorage under "rl.launch" and is shared with Decks, which
   pushes variants into `draft.variants` ({name, base, note, changes}). Sent requests are kept in the db collection
   "requests" on claude.ai (locally under "rl.requests" elsewhere).
   Router contract: render(container, params, ctx) / destroy(). No params. Styles: app/launch.css (prefix .v-launch). */
import { data, esc, store, num, halfWidth, duration, ago, plural, wilson, badge, pageHead, notice, empty, cssOnce, deckEvent, eventLabel,
         portrait, cardChip, deckByPath, allDecks, deckShort, deckLabel, groupDecks, deckFace, cardByName, legendShort, runtime, toast, $, $$ } from "./lib.js";

const CSS = new URL("./launch.css", import.meta.url).href;

const DEFAULT_DECK = "decks/riven_sacramento.txt";
const PRESETS = {
  plumbing: { name: "Quick check", agent: "random", sec: 0.005, line: "Random players on both sides. Runs in seconds and checks that the lists are legal and the engine plays them. Says nothing about deck strength." },
  standard: { name: "Standard bot", agent: "ismcts:sims=30,eval=net:@current", sec: 30, line: "The bot searches 30 continuations per decision with the network in use. About 30 seconds per game per core; the usual choice." },
  strong:   { name: "Strong bot", agent: "ismcts:sims=100,eval=net:@current", sec: 120, line: "100 continuations per decision. Plays better but four times slower; for a final check of a shortlist." },
};
// Default gauntlet: matches against Riven per opponent in the RiftDecks matrix (study, 26 Oct), scaled to a 0–10 weight.
const META = { kennen: 10, kai_sa: 7.2, riven: 8.4, akali: 4.6, rengar: 3.4, lillia: 3.3, leblanc: 2.9, ekko: 2.2, viktor: 1.7, ornn: 1.3 };
const CORES = 4, MAX_HOURS = 6, QUICK = [20, 50, 100, 200, 400];
const ROUNDS = ["4, 8, 16", "6, 12, 24", "8, 16, 32, 64"];

let draft = null, root = null, rt = null;

/* ── draft ──────────────────────────────────────────────────────────────── */
function loadDraft() {
  const d = store("rl.launch") || {};
  draft = { name: "", description: "", mode: "compare", decks: [DEFAULT_DECK], variants: [], opponents: [], agent: "standard", games: 100, ...d };
  draft.search = { base: DEFAULT_DECK, out: [], in: [], rounds: ROUNDS[0], keep: 0.5, ...(d.search || {}) };
  if (!Array.isArray(draft.decks)) draft.decks = [];
  if (!Array.isArray(draft.variants)) draft.variants = [];
  if (!Array.isArray(draft.opponents)) draft.opponents = [];
  if (!PRESETS[draft.agent]) draft.agent = "standard";
  if (!deckByPath(draft.search.base)) draft.search.base = deckByPath(DEFAULT_DECK) ? DEFAULT_DECK : (allDecks()[0] || {}).path || "";
}
const save = () => store("rl.launch", draft);
function metaGauntlet() {
  const out = [], seen = new Set();
  allDecks().filter(d => d.path.startsWith("lab/decks/meta/") && !(d.errors || []).length).forEach(d => {
    const k = Object.keys(META).find(k => d.path.includes("_" + k)); if (!k || seen.has(k)) return; seen.add(k);
    out.push({ deck: d.path, label: shortLabel(d.path), weight: META[k] });
  });
  return out.sort((a, b) => b.weight - a.weight);
}
const shortLabel = deckShort;   // "Kennen (Simple)": the champion and pilot from the deck file's comment line
const gauntlet = () => draft.opponents.length ? draft.opponents : metaGauntlet();
const ownGauntlet = () => { if (!draft.opponents.length) draft.opponents = metaGauntlet(); return draft.opponents; };
const slug = s => String(s || "").trim().toLowerCase().replace(/[^a-z0-9-]+/g, "-").replace(/^-+|-+$/g, "");
const currentNet = () => (data().ladder && data().ladder.current_net) || "lab/learn/nets/net_000.rbvn";
const agentOf = () => PRESETS[draft.agent].agent.replace("@current", currentNet());

function buildSpec() {
  const opponents = gauntlet().filter(o => o.weight > 0).map(o => ({ deck: o.deck, label: o.label || shortLabel(o.deck), weight: +o.weight }));
  const base = { name: slug(draft.name) || "test", description: draft.description || "", agent: agentOf(), opponents, threads: CORES, seed: seedOf() };
  if (draft.mode === "search") {
    const s = draft.search, spec = { ...base, base: s.base };
    if (draft.variants.length) spec.candidates = draft.variants.map(v => ({ name: v.name, changes: v.changes }));
    if (s.out.length && s.in.length) spec.swap = { out: [...s.out], in: [...s.in], max: Math.min(24, s.out.length * s.in.length) };
    spec.rounds = roundsOf(); spec.keep = +s.keep;
    return spec;
  }
  return { ...base, games: +draft.games, review_games: 1,
    decks: [...draft.decks.map(p => ({ deck: p, label: shortLabel(p) })), ...draft.variants.map(v => ({ name: v.name, base: v.base, note: v.note || "", changes: v.changes }))] };
}
let seed = 0; const seedOf = () => seed || (seed = Math.floor(Math.random() * 1e6));
const roundsOf = () => { const r = String(draft.search.rounds).split(/[^0-9]+/).map(Number).filter(n => n >= 2); return r.length ? r : [4, 8, 16]; };

/* What the request will cost and buy. */
function estimate(spec) {
  const per = PRESETS[draft.agent].sec, opps = spec.opponents.length;
  if (draft.mode === "search") {
    const cands = candidateCount(), keep = +draft.search.keep || 0.5; let games = 0, alive = cands;
    roundsOf().forEach((g, k) => { if (k) alive = Math.max(2, Math.ceil(alive * keep)); games += alive * opps * g; });
    const last = roundsOf().reduce((a, b) => a + b, 0) * opps;
    return { games, cands, hours: games * per / CORES / 3600, finalCi: halfWidth(wilson(last / 2, last)), finalGames: last };
  }
  const pairs = spec.decks.length * opps, games = pairs * spec.games, pooled = spec.games * opps;
  return { games, pairs, hours: games * per / CORES / 3600, ci: halfWidth(wilson(spec.games / 2, spec.games)), pooledCi: halfWidth(wilson(pooled / 2, pooled)), pooled };
}
function candidateCount() {
  const s = draft.search, base = deckByPath(s.base), md = base && base.list && base.list.maindeck || {};
  const pairs = s.out.filter(o => md[o] > 0).length * s.in.length;
  return 1 + draft.variants.length + Math.min(24, pairs);
}
function problems(spec) {
  const p = [];
  if (!slug(draft.name)) p.push("give the test a name");
  if (draft.mode === "compare" && !spec.decks.length) p.push("add a list to test");
  if (draft.mode === "search" && !spec.swap && !spec.candidates) p.push("pick at least one card to go out and one to come in, or add a variant from Decks");
  if (!spec.opponents.length) p.push("add an opponent with a weight above zero");
  if (draft.mode === "compare" && !(spec.games >= 2)) p.push("play at least 2 games per matchup");
  return p;
}

/* ── render ─────────────────────────────────────────────────────────────── */
export async function render(container) {
  await cssOnce(CSS); root = container; loadDraft(); save();
  rt = rt || runtime();
  paint();
}
export function destroy() { root = null; }

function paint() {
  if (!root) return;
  const spec = buildSpec(), est = estimate(spec), probs = problems(spec);
  root.innerHTML = `
    ${pageHead({ title: "Launch a test", sub: "Describe what you want to know, pick the opponents and how many games, and send it. Claude runs the test on GitHub Actions and the result shows up in Tests after the page is rebuilt, usually within a few hours." })}
    <div class="v-launch">
      <div class="v-launch-form stack">
        <section class="panel v-launch-step" aria-labelledby="ls-0">
          <div class="v-launch-stephead"><span class="n">1</span><h2 id="ls-0">The question</h2></div>
          <div class="grid2 v-launch-fields">
            <label class="f">Test name<input type="text" id="l-name" value="${esc(draft.name)}" placeholder="e.g. heron-or-deserter" autocomplete="off" spellcheck="false"><span class="xs muted">becomes <span class="mono">${esc(slug(draft.name) || "test")}</span> in the results</span></label>
            <label class="f">What you want to know<input type="text" id="l-desc" value="${esc(draft.description || "")}" placeholder="e.g. What does cutting Heron cost against the field?"></label>
          </div>
        </section>

        <section class="panel v-launch-step" aria-labelledby="ls-1">
          <div class="v-launch-stephead"><span class="n">2</span><h2 id="ls-1">What to test</h2>
            <div class="segmented" role="group" aria-label="kind of test"><button data-mode="compare" aria-pressed="${draft.mode === "compare"}">Compare lists</button><button data-mode="search" aria-pressed="${draft.mode === "search"}">Search for a better list</button></div></div>
          ${draft.mode === "search" ? searchStep() : compareStep()}
        </section>

        <section class="panel v-launch-step" aria-labelledby="ls-2">
          <div class="v-launch-stephead"><span class="n">3</span><h2 id="ls-2">Opponents</h2><span class="small muted">${draft.opponents.length ? "your gauntlet" : "the meta gauntlet"}</span>
            <div class="actions">${draft.opponents.length ? `<button class="btn-sm" data-act="reset-opps">Reset to the meta gauntlet</button>` : ""}</div></div>
          <p class="small muted">Every list under test plays each opponent. The weight is how much that matchup counts in the weighted win rate: the default follows how often each deck meets Riven in tournament play.</p>
          ${gauntletPicker()}
          <div class="toolbar"><label class="f" style="flex:1 1 260px">Add an opponent<select id="l-add-opp"><option value="">choose a list…</option>${deckOptions(new Set(gauntlet().map(o => o.deck)))}</select></label></div>
        </section>

        <section class="panel v-launch-step" aria-labelledby="ls-3">
          <div class="v-launch-stephead"><span class="n">4</span><h2 id="ls-3">${draft.mode === "search" ? "Rounds" : "How many games"}</h2></div>
          ${draft.mode === "search" ? roundsStep(est) : gamesStep(est)}
        </section>

        <section class="panel v-launch-step" aria-labelledby="ls-4">
          <div class="v-launch-stephead"><span class="n">5</span><h2 id="ls-4">Bot level</h2><span class="small muted">network in use: <span class="mono">${esc(currentNet().split("/").pop())}</span></span></div>
          <div class="v-launch-presets" role="radiogroup" aria-label="bot level">
            ${Object.entries(PRESETS).map(([k, p]) => `<button type="button" role="radio" aria-checked="${draft.agent === k}" data-preset="${k}" class="v-launch-preset"><span class="t">${esc(p.name)}</span><span class="d">${esc(p.line)}</span><span class="mono xs">${esc(p.agent.replace("@current", currentNet().split("/").pop()))}</span></button>`).join("")}
          </div>
        </section>
      </div>

      <aside class="v-launch-side stack" id="l-side">${side(spec, est, probs)}</aside>
    </div>`;
  wire();
  renderHistory();
}

function compareStep() {
  const rows = [
    ...draft.decks.map((p, i) => { const d = deckByPath(p); return `<div class="v-launch-row">${portrait(deckFace(p) || legendShort(d ? d.legend : p), { size: "md" })}<div class="v-launch-rowtxt"><b>${esc(shortLabel(p))}</b><span class="sub">${esc(d ? deckLabel(p) : p)}${d && (d.errors || []).length ? ` · ${badge("not legal", "loss")}` : ""}</span></div><button class="btn-ghost btn-sm" data-rm-deck="${i}" aria-label="remove ${esc(deckShort(p))}">Remove</button></div>`; }),
    ...draft.variants.map((v, i) => `<div class="v-launch-row">${portrait(deckFace(v.base) || v.base, { size: "md" })}<div class="v-launch-rowtxt"><b>${esc(v.name)} ${badge("variant", "gold")}</b><span class="sub">from ${esc(deckShort(v.base))}${v.note ? ` — ${esc(v.note)}` : ""}</span><span class="v-launch-changes">${changeChips(v.changes)}</span></div><button class="btn-ghost btn-sm" data-rm-var="${i}" aria-label="remove variant ${esc(v.name)}">Remove</button></div>`),
  ];
  return `<p class="small muted">Each list plays the whole gauntlet, half the games on the play and half on the draw. Variants drafted in <a href="#/decks">Decks</a> arrive here with their changes.</p>
    ${rows.length ? `<div class="v-launch-rows">${rows.join("")}</div>` : empty("No list yet", "Add one below, or build a variant in <a href=\"#/decks\">Decks</a> and send it here.")}
    <div class="toolbar"><label class="f" style="flex:1 1 260px">Add a list<select id="l-add-deck"><option value="">choose a list…</option>${deckOptions(new Set(draft.decks))}</select></label></div>`;
}
function changeChips(changes) {
  const md = (changes && changes.maindeck) || {};
  return Object.entries(md).map(([n, d]) => `<span class="v-launch-delta ${d > 0 ? "add" : "rem"}">${cardChip(n, { cls: "sm" })}<b>${d > 0 ? "+" : ""}${d}</b></span>`).join("") || `<span class="xs muted">no change from the base list</span>`;
}
function searchStep() {
  const s = draft.search, base = deckByPath(s.base), md = (base && base.list && base.list.maindeck) || {};
  const doms = new Set(((base && cardByName(base.legend)) || {}).domains || []);
  const pool = data().cards.filter(c => /^(unit|spell|gear)$/.test(c.type) && c.super !== "token" && (!c.domains.length || c.domains.every(d => doms.has(d))) && !md[c.name] && !s.in.includes(c.name)).sort((a, b) => a.energy - b.energy || a.name.localeCompare(b.name));
  const cands = candidateCount();
  return `<p class="small muted">Starting from a base list, every one-for-one swap of a card that may go out for a card that may come in is a candidate (24 at most). All candidates and the base play the gauntlet a few games; the better half plays again with twice the games, and so on until a few remain. Same deals for every candidate, so the differences come from the lists, not the draw.</p>
    <div class="toolbar"><label class="f" style="flex:1 1 260px">Base list<select id="l-base">${deckOptions(new Set(), s.base)}</select></label></div>
    <h3>Cards that may go out <span class="muted">(${s.out.length} chosen)</span></h3>
    <div class="v-launch-pool">${Object.entries(md).sort((a, b) => ((cardByName(a[0]) || {}).energy ?? 9) - ((cardByName(b[0]) || {}).energy ?? 9) || a[0].localeCompare(b[0])).map(([n, k]) => `<button type="button" class="v-launch-pick" aria-pressed="${s.out.includes(n)}" data-out="${esc(n)}">${cardChip(n, { count: k, cls: "sm plain" })}</button>`).join("")}</div>
    <h3>Cards that may come in <span class="muted">(${s.in.length} chosen)</span></h3>
    <div class="v-launch-pool">${s.in.map(n => `<button type="button" class="v-launch-pick" aria-pressed="true" data-in-rm="${esc(n)}" aria-label="remove ${esc(n)}">${cardChip(n, { cls: "sm plain" })}<span class="x">×</span></button>`).join("") || `<span class="small muted">None yet: pick from the legend's domains below.</span>`}</div>
    <div class="toolbar"><label class="f" style="flex:1 1 260px">Add a card<input list="l-pool" id="l-add-in" placeholder="type a name…" autocomplete="off"><datalist id="l-pool">${pool.slice(0, 600).map(c => `<option value="${esc(c.name)}">${c.energy} · ${esc(c.type)}${c.set === "RAD" ? " · Radiance" : ""}</option>`).join("")}</datalist></label><button id="l-add-in-btn">Add</button></div>
    ${draft.variants.length ? `<p class="small">${plural(draft.variants.length, "variant")} from Decks will be tried as candidates too: ${draft.variants.map(v => `<b>${esc(v.name)}</b>`).join(", ")}. <button class="btn-ghost btn-sm" data-act="clear-vars">Drop them</button></p>` : ""}
    <p class="small muted">${cands} candidate${cands === 1 ? "" : "s"} including the base list as control.</p>`;
}
function gauntletPicker() {
  const g = gauntlet(), sum = g.reduce((a, o) => a + Math.max(0, +o.weight || 0), 0), max = Math.max(10, ...g.map(o => +o.weight || 0));
  if (!g.length) return empty("No opponent", "Add a tournament list below.");
  return `<div class="v-launch-gauntlet">${g.map((o, i) => { const d = deckByPath(o.deck), share = sum ? (100 * Math.max(0, o.weight) / sum) : 0;
    return `<div class="v-launch-opp ${o.weight > 0 ? "" : "off"}">
      ${portrait(deckFace(o.deck) || (d ? legendShort(d.legend) : o.label), { size: "lg" })}
      <div class="v-launch-opptxt"><b>${esc(o.label || shortLabel(o.deck))}</b><span class="sub">${esc(eventOf(o.deck))}</span></div>
      <button class="btn-ghost icon-btn v-launch-x" data-rm-opp="${i}" aria-label="remove ${esc(o.label || shortLabel(o.deck))}">×</button>
      <label class="v-launch-w"><span class="xs muted">weight <b class="mono" data-wv="${i}">${num(o.weight, 1)}</b> · <span data-ws="${i}">${share.toFixed(0)}%</span> of the result</span><input type="range" min="0" max="${max}" step="0.5" value="${o.weight}" data-w="${i}" aria-label="weight of ${esc(o.label || shortLabel(o.deck))}"></label>
    </div>`; }).join("")}</div>`;
}
function gamesStep(est) {
  return `<div class="v-launch-games">
      <label class="f">Games per matchup<input type="number" id="l-games" min="2" step="2" value="${draft.games}"></label>
      <div class="segmented" role="group" aria-label="quick picks">${QUICK.map(n => `<button data-games="${n}" aria-pressed="${+draft.games === n}">${n}</button>`).join("")}</div>
    </div>
    <div class="v-launch-est" id="l-est">${estimateHtml(est)}</div>`;
}
function roundsStep(est) {
  const s = draft.search;
  return `<div class="v-launch-games">
      <label class="f">Games per candidate per opponent, round by round<input type="text" id="l-rounds" value="${esc(s.rounds)}" placeholder="4, 8, 16"></label>
      <div class="segmented" role="group" aria-label="quick picks">${ROUNDS.map(r => `<button data-rounds="${esc(r)}" aria-pressed="${roundsOf().join(", ") === r}">${esc(r)}</button>`).join("")}</div>
      <label class="f">Kept after each round<select id="l-keep">${[[0.5, "the better half"], [0.34, "the best third"], [0.25, "the best quarter"]].map(([v, t]) => `<option value="${v}" ${+s.keep === v ? "selected" : ""}>${t}</option>`).join("")}</select></label>
    </div>
    <div class="v-launch-est" id="l-est">${estimateHtml(est)}</div>`;
}
function estimateHtml(est) {
  const time = est.hours * 60 < 1 ? "under a minute" : duration(est.hours * 3600);
  const over = est.hours > MAX_HOURS;
  if (draft.mode === "search") return `<div class="v-launch-estline"><b>${num(est.games)} games</b> over ${plural(roundsOf().length, "round")} for ${plural(est.cands, "candidate")}, about <b>${time}</b> on ${CORES} cores.</div>
    <div class="small muted">A finalist that survives every round has played ${num(est.finalGames)} games, which pins its rate to about <b>${esc(est.finalCi)}</b>. Early rounds only sort the field; do not read their rates as verdicts.</div>${over ? notice(`Longer than the ${MAX_HOURS} hours a GitHub run allows: use fewer candidates, smaller rounds or a quicker bot.`, "error") : ""}`;
  return `<div class="v-launch-estline"><b>${num(est.games)} games</b> (${plural(est.pairs, "matchup")}), about <b>${time}</b> on ${CORES} cores.</div>
    <div class="small muted">${num(draft.games)} games per matchup give an interval of about <b>${esc(est.ci)}</b> on each matchup; pooled over the ${plural(gauntlet().filter(o => o.weight > 0).length, "opponent")} (${num(est.pooled)} games) the weighted rate is known to about <b>${esc(est.pooledCi)}</b>. Halving the width takes four times the games.</div>${over ? notice(`Longer than the ${MAX_HOURS} hours a GitHub run allows: lower the games per matchup, drop an opponent or pick a quicker bot.`, "error") : ""}`;
}
function side(spec, est, probs) {
  const ready = !probs.length;
  return `<section class="panel gilt stack v-launch-send" aria-labelledby="ls-send">
      <h2 id="ls-send">Send the request</h2>
      <div class="v-launch-sum">
        <span class="versus">${portraitsOf(draft.mode === "search" ? [draft.search.base] : [...draft.decks, ...draft.variants.map(v => v.base)])}<span class="vs">vs</span>${portraitsOf(gauntlet().filter(o => o.weight > 0).map(o => o.deck), 4)}</span>
        <div class="small">${draft.mode === "search" ? `Search from ${esc(deckShort(draft.search.base))}, ${plural(est.cands, "candidate")}` : `${plural(spec.decks.length, "list")} against ${plural(spec.opponents.length, "opponent")}`} · ${esc(PRESETS[draft.agent].name.toLowerCase())} · ${num(est.games)} games</div>
      </div>
      ${ready ? "" : `<div class="notice info small">Before sending: ${probs.map(esc).join("; ")}.</div>`}
      <div class="row tight"><button class="primary" id="l-send" ${ready ? "" : "disabled"}>Send to Claude</button><button id="l-copy">Copy request</button></div>
      <div id="l-status" aria-live="polite"></div>
      <p class="xs muted">Sending passes the request to the lab's Claude session, which commits it to <span class="mono">lab/requests/</span>. GitHub Actions builds the engine and plays the games (${CORES} cores, up to ${MAX_HOURS} hours); Claude then rebuilds this page and the result appears in <a href="#/tests">Tests</a>. If the session is closed, a new one is opened for it.</p>
      <details class="v-launch-json"><summary class="small muted">Request as sent (lab/specs format)</summary><div class="log" id="l-json">${esc(JSON.stringify(spec, null, 1))}</div></details>
    </section>
    <section class="panel stack" aria-labelledby="ls-hist"><div class="panel-head"><h2 id="ls-hist">Sent requests</h2></div><div id="l-hist">${empty("Nothing sent yet", "Requests you send appear here with their status.")}</div></section>`;
}
function portraitsOf(paths, max = 3) {
  const seen = new Set(), refs = [];
  for (const p of paths) { const f = deckFace(p); const k = f ? f.name : p; if (!seen.has(k)) { seen.add(k); refs.push(f || deckShort(p)); } }
  if (!refs.length) refs.push(null);
  return `<span class="portraits" style="--w:40px">${refs.slice(0, max).map(r => portrait(r, { size: "md" })).join("")}${refs.length > max ? `<span class="more">+${refs.length - max}</span>` : ""}</span>`;
}
function refreshSide() {
  const spec = buildSpec(), est = estimate(spec), probs = problems(spec), s = $("#l-side", root);
  if (s) { const hist = $("#l-hist", root); const keep = hist ? hist.innerHTML : ""; s.innerHTML = side(spec, est, probs); const h2 = $("#l-hist", root); if (h2 && keep) h2.innerHTML = keep; wireSide(); }
  const e = $("#l-est", root); if (e) e.innerHTML = estimateHtml(est);
}

/* ── labels ─────────────────────────────────────────────────────────────── */
/** Where a list comes from, in a few words: the event for a tournament list, else the file's stem. */
function eventOf(path) {
  const ev = deckEvent(path); if (ev) return eventLabel(ev);
  return path.startsWith("lab/variants/") ? "lab variant" : path.replace(/^decks\//, "").replace(/\.txt$/, "");
}
function deckOptions(exclude, selected = "") {
  const g = groupDecks();
  return Object.entries(g).filter(([, list]) => list.length).map(([title, list]) => `<optgroup label="${esc(title)}">${list.filter(d => !exclude.has(d.path)).map(d => `<option value="${esc(d.path)}" ${d.path === selected ? "selected" : ""}>${esc(shortLabel(d.path))}${title.startsWith("Meta") ? ` — ${esc(eventOf(d.path))}` : ""}${(d.errors || []).length ? " (not legal)" : ""}</option>`).join("")}</optgroup>`).join("");
}

/* ── events ─────────────────────────────────────────────────────────────── */
function wire() {
  const on = (sel, ev, fn) => { const n = $(sel, root); if (n) n.addEventListener(ev, fn); };
  on("#l-name", "input", e => { draft.name = e.target.value; save(); refreshSide(); const s = e.target.parentNode.querySelector(".mono"); if (s) s.textContent = slug(draft.name) || "test"; });
  on("#l-desc", "input", e => { draft.description = e.target.value; save(); refreshSide(); });
  on("#l-add-deck", "change", e => { if (e.target.value) { draft.decks.push(e.target.value); save(); paint(); } });
  on("#l-add-opp", "change", e => { if (e.target.value) { ownGauntlet().push({ deck: e.target.value, label: shortLabel(e.target.value), weight: 5 }); save(); paint(); } });
  on("#l-base", "change", e => { draft.search.base = e.target.value; draft.search.out = []; save(); paint(); });
  const addIn = () => { const i = $("#l-add-in", root), n = i && i.value.trim(); if (!n) return; const c = cardByName(n); if (!c) { toast(`"${n}" is not a card the engine knows.`, "error"); return; } if (!draft.search.in.includes(c.name)) draft.search.in.push(c.name); save(); paint(); };
  on("#l-add-in-btn", "click", addIn);
  on("#l-add-in", "keydown", e => { if (e.key === "Enter") { e.preventDefault(); addIn(); } });
  on("#l-games", "input", e => { draft.games = Math.max(2, Math.round(+e.target.value || 2)); save(); $$("[data-games]", root).forEach(b => b.setAttribute("aria-pressed", +b.dataset.games === +draft.games)); refreshSide(); });
  on("#l-rounds", "input", e => { draft.search.rounds = e.target.value; save(); $$("[data-rounds]", root).forEach(b => b.setAttribute("aria-pressed", b.dataset.rounds === roundsOf().join(", "))); refreshSide(); });
  on("#l-keep", "change", e => { draft.search.keep = +e.target.value; save(); refreshSide(); });
  root.addEventListener("input", e => {
    const w = e.target.closest("[data-w]"); if (!w) return;
    const g = ownGauntlet(); g[+w.dataset.w].weight = +w.value; save();
    const sum = g.reduce((a, o) => a + Math.max(0, +o.weight || 0), 0);
    g.forEach((o, i) => { const v = $(`[data-wv="${i}"]`, root), s = $(`[data-ws="${i}"]`, root); if (v) v.textContent = num(o.weight, 1); if (s) s.textContent = (sum ? 100 * Math.max(0, o.weight) / sum : 0).toFixed(0) + "%"; const card = w.closest(".v-launch-opp"); if (card && i === +w.dataset.w) card.classList.toggle("off", !(o.weight > 0)); });
    refreshSide();
  });
  root.addEventListener("click", e => {
    const b = e.target.closest("button"); if (!b || !root.contains(b)) return;
    if (b.dataset.mode) { draft.mode = b.dataset.mode; save(); paint(); }
    else if (b.dataset.preset) { draft.agent = b.dataset.preset; save(); $$("[data-preset]", root).forEach(x => x.setAttribute("aria-checked", x === b)); refreshSide(); }
    else if (b.dataset.games) { draft.games = +b.dataset.games; save(); paint(); }
    else if (b.dataset.rounds) { draft.search.rounds = b.dataset.rounds; save(); paint(); }
    else if (b.dataset.rmDeck != null) { draft.decks.splice(+b.dataset.rmDeck, 1); save(); paint(); }
    else if (b.dataset.rmVar != null) { draft.variants.splice(+b.dataset.rmVar, 1); save(); paint(); }
    else if (b.dataset.rmOpp != null) { ownGauntlet().splice(+b.dataset.rmOpp, 1); save(); paint(); }
    else if (b.dataset.out != null) { const s = draft.search.out, n = b.dataset.out, i = s.indexOf(n); i >= 0 ? s.splice(i, 1) : s.push(n); save(); paint(); }
    else if (b.dataset.inRm != null) { draft.search.in = draft.search.in.filter(n => n !== b.dataset.inRm); save(); paint(); }
    else if (b.dataset.act === "reset-opps") { draft.opponents = []; save(); paint(); }
    else if (b.dataset.act === "clear-vars") { draft.variants = []; save(); paint(); }
  });
  wireSide();
}
function wireSide() {
  const send = $("#l-send", root), copy = $("#l-copy", root);
  if (send) send.onclick = () => sendRequest(buildSpec());
  if (copy) copy.onclick = async () => {
    const t = JSON.stringify(buildSpec(), null, 1);
    try { await navigator.clipboard.writeText(t); status("Request copied. Paste it into a conversation with Claude, or drop it into lab/requests/ on the work branch.", "ok"); remember(buildSpec(), "copied"); }
    catch (e) { const r = document.createRange(); r.selectNodeContents($("#l-json", root)); getSelection().removeAllRanges(); getSelection().addRange(r); status("The clipboard is not available here: the request is selected below, copy it with Ctrl+C.", "info"); }
  };
}
function status(msg, kind) { const s = $("#l-status", root); if (s) s.innerHTML = notice(esc(msg), kind); }

/* ── sending ────────────────────────────────────────────────────────────── */
function messageFor(spec) {
  const how = draft.mode === "search" ? "Run this list search with lab/optimize.py (or drop it into lab/requests/ for GitHub Actions, which detects the \"rounds\" key)" : "Run this test with lab/riftlab.py (or by dropping it into lab/requests/ for GitHub Actions)";
  return `[Request from the Riven Lab dashboard]\n${how}, then rebuild and republish the dashboard with the result.\n\n${JSON.stringify(spec, null, 1)}`;
}
async function sendRequest(spec) {
  const D = data(), btn = $("#l-send", root); if (btn) btn.disabled = true;
  const { mcp } = await rt;
  const message = messageFor(spec);
  if (!mcp) { status("Request not sent: this page is not running on claude.ai, so it cannot reach the lab session. Copy the request and paste it to Claude.", "error"); if (btn) btn.disabled = false; return; }
  let how = "";
  try {
    await mcp.callTool("Claude Code Remote", "send_message", { session_id: D.session_id, message });
    how = "sent to the lab session";
  } catch (e) {
    try {
      await mcp.callTool("Claude Code Remote", "create_session", { environment_id: D.environment_id, source_url: "https://github.com/" + D.repo, source_revision: D.branch, title: "Riven Lab: " + spec.name, prompt: message + `\n\nWork on branch ${D.branch} of repo ${D.repo}; read lab/README.md first.` });
      how = "new Claude Code session opened";
    } catch (e2) {
      const code = e2.code || e.code;
      status(code === "not_in_manifest" || code === "not_granted" ? "Request not sent: the Claude Code connector is not allowed for this page. Allow it in the Permissions menu, or copy the request and paste it to Claude."
        : code === "server_not_connected" ? "Request not sent: the Claude Code Remote connector is not connected here. Copy the request and paste it to Claude."
        : `Request not sent (${e2.message || code || "unknown error"}). Copy the request and paste it to Claude.`, "error");
      if (btn) btn.disabled = false; return;
    }
  }
  await remember(spec, how);
  status(how === "sent to the lab session" ? "Sent to the lab session. Claude will commit it to lab/requests/; the result appears in Tests once the page is rebuilt." : "The lab session was closed, so a new Claude Code session was opened with this request. The result appears in Tests once the page is rebuilt.", "ok");
  toast(how === "sent to the lab session" ? "Request sent to the lab session" : "New session opened for the request", "ok");
  if (btn) btn.disabled = false; seed = 0;
  renderHistory();
}
async function remember(spec, how) {
  const rec = { spec, sent: how, at: new Date().toISOString(), author: "" };
  const { db, meId } = await rt; rec.author = meId || "";
  if (db) { try { await db.collection("requests").add(rec); return; } catch (e) { /* fall through to local */ } }
  const local = store("rl.requests") || []; local.unshift(rec); store("rl.requests", local.slice(0, 20));
}
async function renderHistory() {
  const h = $("#l-hist", root); if (!h) return;
  let recs = null;
  const { db } = await rt;
  if (db) { try { const snap = await db.collection("requests").orderBy("at", "desc").limit(12).get(); recs = snap.docs.map(d => d.data()); } catch (e) { recs = null; } }
  if (!recs) recs = store("rl.requests") || [];
  if (!$("#l-hist", root)) return;
  if (!recs.length) { $("#l-hist", root).innerHTML = empty("Nothing sent yet", db ? "Requests you send appear here with their status." : "Requests sent from this browser appear here; on claude.ai the list is shared with everyone who can open the page."); return; }
  const tests = data().tests || [];
  $("#l-hist", root).innerHTML = `<ul class="v-launch-hist">${recs.map(r => {
    const name = r.spec && r.spec.name || "?", done = tests.filter(t => t.name === name && String(t.started_utc) >= String(r.at || "")).sort((a, b) => String(b.started_utc).localeCompare(String(a.started_utc)))[0];
    const kind = r.sent === "sent to the lab session" ? badge("lab session", "gold") : /new/.test(r.sent || "") ? badge("new session", "bot") : badge(r.sent || "sent", "neutral");
    const state = done ? `<a href="#/tests/${encodeURIComponent(done.id)}">${badge("result in", "win")}</a>` : r.sent === "copied" ? "" : badge("waiting for a rebuild", "warn");
    const s = r.spec || {}, what = s.rounds ? `search from ${esc(deckShort(s.base || ""))}` : `${plural((s.decks || []).length, "list")} vs ${plural((s.opponents || []).length, "opponent")}, ${num(s.games)} games each`;
    return `<li><div class="v-launch-histtop"><b>${esc(name)}</b>${kind}${state}<span class="spacer"></span><span class="xs faint mono" title="${esc(r.at || "")}">${esc(ago(r.at))}</span></div><div class="xs muted">${what}${s.description ? ` — ${esc(s.description)}` : ""}</div></li>`; }).join("")}</ul>`;
}
