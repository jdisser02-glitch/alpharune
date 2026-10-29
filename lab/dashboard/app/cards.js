/* Riven Lab — Cards: a browser of every card the engine knows. Search, filters (type, domain, set, cost, might, kind),
   sort, an image grid with the hover preview, and a detail drawer with the full card, its facts and the lab decks
   that play it. Router contract: render(container, params) / update(container, params) / destroy(); ?q=<text>
   prefills the search. Styles: app/cards.css (prefix .v-cards). */
import { data, esc, num, plural, cardImg, cardById, cardByName, pageHead, empty, badge, DOMAIN_NAMES, domainDot, cssOnce, setName,
         deckByPath, allDecks, deckShort, deckFace, portrait, groupDecks, $, $$ } from "./lib.js";

const CSS = new URL("./cards.css", import.meta.url).href;

const TYPES = ["all", "unit", "spell", "gear", "legend", "battlefield", "rune"];
const KINDS = [["all", "Any kind"], ["champion", "Champions"], ["signature", "Signature cards"], ["token", "Tokens"], ["playable", "No tokens"]];
const SORTS = [["cost", "Cost"], ["name", "Name"], ["might", "Might"], ["type", "Type"], ["set", "Set"]];
const TYPE_ORDER = { unit: 0, spell: 1, gear: 2, legend: 3, rune: 4, battlefield: 5 };
const PAGE = 96, COST_MAX = 8, MIGHT_MAX = 8;   // the last bucket is "8+"
let st = { q: "", type: "all", doms: new Set(), set: "all", cost: new Set(), might: new Set(), kind: "all", sort: "cost", shown: PAGE, open: null };
let root = null, shown = [], deckIdx = null, onKey = null;

export async function render(container, params = {}) {
  await cssOnce(CSS); root = container;
  if (params.q != null) st.q = params.q;
  const C = data().cards, sets = [...new Set(C.map(c => c.set).filter(Boolean))].sort();
  container.innerHTML = `<div class="v-cards">
    ${pageHead({ title: "Cards", sub: `Every card the engine can play, ${num(C.length)} in all. Hover a card to read it, click it for the details and the lab decks that play it.` })}
    <section class="panel v-cards-filters" aria-label="search and filters">
      <div class="toolbar">
        <label class="f search" style="flex:1 1 260px">Search<input type="search" id="c-q" value="${esc(st.q)}" placeholder="name, tag or rules text" autocomplete="off"></label>
        <label class="f">Sort by<select id="c-sort">${SORTS.map(([k, l]) => `<option value="${k}" ${st.sort === k ? "selected" : ""}>${l}</option>`).join("")}</select></label>
        <label class="f">Set<select id="c-set"><option value="all">All sets</option>${sets.map(s => `<option value="${esc(s)}" ${st.set === s ? "selected" : ""}>${esc(setName(s))}</option>`).join("")}</select></label>
        <label class="f">Kind<select id="c-kind">${KINDS.map(([k, l]) => `<option value="${k}" ${st.kind === k ? "selected" : ""}>${l}</option>`).join("")}</select></label>
      </div>
      <div class="v-cards-filterrow">
        <div class="segmented v-cards-types" id="c-type" role="group" aria-label="card type">${TYPES.map(t => `<button data-type="${t}" aria-pressed="${st.type === t}">${t === "all" ? "all types" : t + "s"}</button>`).join("")}</div>
        <div class="v-cards-doms" id="c-doms" role="group" aria-label="domains">${DOMAIN_NAMES.map(d => `<button class="btn-sm" data-dom="${d}" aria-pressed="${st.doms.has(d)}">${domainDot(d)}${d}</button>`).join("")}</div>
      </div>
      <div class="v-cards-filterrow">
        <div class="v-cards-range" role="group" aria-label="energy cost"><span class="v-cards-rlabel">Cost</span>${bucketButtons("cost", COST_MAX, st.cost)}</div>
        <div class="v-cards-range" role="group" aria-label="might"><span class="v-cards-rlabel">Might</span>${bucketButtons("might", MIGHT_MAX, st.might, 1)}</div>
      </div>
      <div class="v-cards-active" id="c-active"></div>
    </section>
    <div id="c-grid" class="v-cards-results"></div>
    <div id="c-drawer-host"></div>
  </div>`;
  wire(container);
  grid();
  if (st.open != null) openDrawer(st.open, false);
  if (!onKey) { onKey = e => { if (!root) return; if (e.key === "Escape" && st.open != null) closeDrawer(); if (st.open != null && (e.key === "ArrowRight" || e.key === "ArrowLeft") && !e.target.closest("input, select, textarea")) { step(e.key === "ArrowRight" ? 1 : -1); e.preventDefault(); } }; document.addEventListener("keydown", onKey); }
}
export function update(container, params = {}) { if (params.q != null && params.q !== st.q) { st.q = params.q; const i = $("#c-q", container); if (i) i.value = st.q; st.shown = PAGE; grid(); } }
export function destroy() { if (onKey) { document.removeEventListener("keydown", onKey); onKey = null; } document.body.classList.remove("v-cards-open"); root = null; }

function bucketButtons(key, max, set, from = 0) { let out = ""; for (let i = from; i <= max; i++) out += `<button class="btn-sm" data-${key}="${i}" aria-pressed="${set.has(i)}">${i === max ? `${i}+` : i}</button>`; return out; }
const bucket = (v, max) => Math.min(max, v || 0);

/* ── filtering and sorting ──────────────────────────────────────────────── */
function matches(c) {
  if (st.type !== "all" && c.type !== st.type) return false;
  if (st.set !== "all" && c.set !== st.set) return false;
  if (st.kind === "playable" ? c.super === "token" : st.kind !== "all" && c.super !== st.kind) return false;
  if (st.doms.size && !(c.domains || []).some(d => st.doms.has(d))) return false;
  if (st.cost.size && !st.cost.has(bucket(c.energy, COST_MAX))) return false;
  if (st.might.size && !(c.type === "unit" && st.might.has(bucket(c.might, MIGHT_MAX)))) return false;
  if (st.q) { const q = st.q.trim().toLowerCase(); if (q && !c.name.toLowerCase().includes(q) && !(c.tags || []).some(t => t.toLowerCase().includes(q)) && !(c.text || "").toLowerCase().includes(q)) return false; }
  return true;
}
const byName = (a, b) => a.name.localeCompare(b.name);
const SORT_FN = {
  cost: (a, b) => (TYPE_ORDER[a.type] ?? 9) - (TYPE_ORDER[b.type] ?? 9) || (a.energy ?? 0) - (b.energy ?? 0) || byName(a, b),
  name: byName,
  might: (a, b) => (b.might || 0) - (a.might || 0) || (a.energy || 0) - (b.energy || 0) || byName(a, b),
  type: (a, b) => (TYPE_ORDER[a.type] ?? 9) - (TYPE_ORDER[b.type] ?? 9) || byName(a, b),
  set: (a, b) => String(a.set).localeCompare(String(b.set)) || (a.id || 0) - (b.id || 0),
};
function activeFilters() {
  const f = [];
  if (st.q.trim()) f.push({ k: "q", label: `“${st.q.trim()}”` });
  if (st.type !== "all") f.push({ k: "type", label: st.type + "s" });
  st.doms.forEach(d => f.push({ k: "dom", v: d, label: `${domainDot(d)}${d}` }));
  if (st.set !== "all") f.push({ k: "set", label: setName(st.set) });
  if (st.kind !== "all") f.push({ k: "kind", label: KINDS.find(x => x[0] === st.kind)[1].toLowerCase() });
  [...st.cost].sort((a, b) => a - b).forEach(v => f.push({ k: "cost", v, label: `cost ${v === COST_MAX ? v + "+" : v}` }));
  [...st.might].sort((a, b) => a - b).forEach(v => f.push({ k: "might", v, label: `might ${v === MIGHT_MAX ? v + "+" : v}` }));
  return f;
}
function grid() {
  if (!root) return;
  const all = data().cards.filter(matches).sort(SORT_FN[st.sort] || SORT_FN.cost);
  shown = all;
  const host = $("#c-grid", root), active = $("#c-active", root), f = activeFilters();
  active.innerHTML = f.length ? `<span class="small muted">Filters:</span>${f.map(x => `<button class="chip v-cards-chip" data-clear="${x.k}" data-v="${esc(x.v ?? "")}" aria-label="remove filter ${esc(String(x.label).replace(/<[^>]+>/g, ""))}">${x.label}<span class="x">×</span></button>`).join("")}<button class="btn-ghost btn-sm" data-clear="all">Clear all</button>` : "";
  if (!all.length) { host.innerHTML = empty("No card matches", "Try a shorter search or clear a filter."); return; }
  const page = all.slice(0, st.shown);
  host.innerHTML = `<p class="small muted v-cards-count">${all.length > page.length ? `Showing ${num(page.length)} of ${plural(all.length, "card")}` : plural(all.length, "card")}${st.q.trim() ? ` matching “${esc(st.q.trim())}”` : ""}.</p>
    <div class="v-cards-grid">${page.map(c => tile(c)).join("")}</div>
    ${all.length > page.length ? `<div class="row v-cards-more"><button id="c-more">Show ${num(Math.min(PAGE, all.length - page.length))} more</button></div>` : ""}`;
  const more = $("#c-more", host); if (more) more.onclick = () => { st.shown += PAGE; grid(); };
}
function tile(c) {
  return `<button type="button" class="v-cards-tile ${st.open === c.id ? "sel" : ""}" data-id="${c.id}" data-card="${esc(c.name)}" aria-label="${esc(c.name)}, ${esc(c.type)}">
    ${cardImg(c, { size: "md", w: 300, hover: false })}
    <span class="v-cards-cap"><span class="n">${esc(c.name)}</span><span class="m">${/^(unit|spell|gear)$/.test(c.type) ? `<span class="mono">${c.energy}${c.power ? `+${c.power}` : ""}</span>` : ""}${c.type === "unit" ? `<span class="mono">${c.might} might</span>` : `<span>${esc(c.type)}</span>`}</span></span>
  </button>`;
}

/* ── events ─────────────────────────────────────────────────────────────── */
function wire(container) {
  const reset = () => { st.shown = PAGE; grid(); };
  $("#c-q", container).oninput = e => { st.q = e.target.value; reset(); };
  $("#c-sort", container).onchange = e => { st.sort = e.target.value; reset(); };
  $("#c-set", container).onchange = e => { st.set = e.target.value; reset(); };
  $("#c-kind", container).onchange = e => { st.kind = e.target.value; reset(); };
  container.addEventListener("click", e => {
    const b = e.target.closest("button"); if (!b) return;
    if (b.dataset.type) { st.type = b.dataset.type; $$("#c-type button", container).forEach(x => x.setAttribute("aria-pressed", x === b)); reset(); }
    else if (b.dataset.dom) { toggle(st.doms, b.dataset.dom, b); reset(); }
    else if (b.dataset.cost != null) { toggle(st.cost, +b.dataset.cost, b); reset(); }
    else if (b.dataset.might != null) { toggle(st.might, +b.dataset.might, b); reset(); }
    else if (b.dataset.clear) { clearFilter(b.dataset.clear, b.dataset.v); syncControls(container); reset(); }
    else if (b.classList.contains("v-cards-tile")) { e.stopPropagation(); openDrawer(+b.dataset.id); }
    else if (b.dataset.drawer === "close") closeDrawer();
    else if (b.dataset.drawer === "prev") step(-1);
    else if (b.dataset.drawer === "next") step(1);
  });
  container.addEventListener("keydown", e => { if ((e.key === "Enter" || e.key === " ") && e.target.closest(".v-cards-tile")) e.stopPropagation(); });
}
function toggle(set, v, b) { set.has(v) ? set.delete(v) : set.add(v); if (b) b.setAttribute("aria-pressed", set.has(v)); }
function clearFilter(k, v) {
  if (k === "all") { st = { ...st, q: "", type: "all", doms: new Set(), set: "all", cost: new Set(), might: new Set(), kind: "all" }; return; }
  if (k === "q") st.q = ""; else if (k === "type") st.type = "all"; else if (k === "set") st.set = "all"; else if (k === "kind") st.kind = "all";
  else if (k === "dom") st.doms.delete(v); else if (k === "cost") st.cost.delete(+v); else if (k === "might") st.might.delete(+v);
}
function syncControls(c) {
  $("#c-q", c).value = st.q; $("#c-set", c).value = st.set; $("#c-kind", c).value = st.kind;
  $$("#c-type button", c).forEach(b => b.setAttribute("aria-pressed", b.dataset.type === st.type));
  $$("#c-doms button", c).forEach(b => b.setAttribute("aria-pressed", st.doms.has(b.dataset.dom)));
  $$("[data-cost]", c).forEach(b => b.setAttribute("aria-pressed", st.cost.has(+b.dataset.cost)));
  $$("[data-might]", c).forEach(b => b.setAttribute("aria-pressed", st.might.has(+b.dataset.might)));
}

/* ── the detail drawer ──────────────────────────────────────────────────── */
function deckIndex() {
  if (deckIdx) return deckIdx;
  deckIdx = new Map();
  for (const d of allDecks()) {
    const L = d.list || {};
    for (const sec of ["legend", "champion", "maindeck", "battlefields", "runes", "sideboard"]) for (const [n, k] of Object.entries(L[sec] || {})) {
      const key = n.toLowerCase(), c = cardByName(n), id = c ? c.id : null;
      const arr = deckIdx.get(id != null ? "id:" + id : "n:" + key) || []; arr.push({ path: d.path, count: k, section: sec }); deckIdx.set(id != null ? "id:" + id : "n:" + key, arr);
    }
  }
  return deckIdx;
}
function decksWith(c) {
  const hits = deckIndex().get("id:" + c.id) || [], byPath = new Map();
  for (const h of hits) { const r = byPath.get(h.path) || { path: h.path, main: 0, side: 0 }; if (h.section === "sideboard") r.side += h.count; else r.main += h.count; byPath.set(h.path, r); }
  const groups = groupDecks([...byPath.keys()].map(deckByPath).filter(Boolean));
  return Object.entries(groups).filter(([, l]) => l.length).map(([title, l]) => ({ title, rows: l.map(d => byPath.get(d.path)) }));
}
function drawerHtml(c) {
  const i = shown.findIndex(x => x.id === c.id), n = shown.length;
  const cost = /^(unit|spell|gear)$/.test(c.type) ? `${c.energy} energy${c.power ? ` + ${c.power} power` : ""}` : null;
  const groups = decksWith(c), total = groups.reduce((a, g) => a + g.rows.length, 0);
  return `<div class="v-cards-dhead">
      <div class="row tight"><button class="btn-sm" data-drawer="prev" ${i > 0 ? "" : "disabled"} aria-label="previous card">←</button><span class="xs muted mono">${i >= 0 ? `${i + 1} / ${n}` : ""}</span><button class="btn-sm" data-drawer="next" ${i >= 0 && i < n - 1 ? "" : "disabled"} aria-label="next card">→</button></div>
      <button class="btn-ghost icon-btn" data-drawer="close" aria-label="close">×</button>
    </div>
    <div class="v-cards-dbody">
      <div class="v-cards-dart">${cardImg(c, { size: "xl", w: 600, hover: false })}</div>
      <div class="v-cards-dinfo stack tight">
        <h2>${esc(c.name)}</h2>
        <div class="row tight">${badge(c.type, "gold")}${c.super ? badge(c.super, c.super === "champion" ? "solid" : "neutral") : ""}${c.set ? badge(setName(c.set), "mono") : ""}</div>
        <dl class="v-cards-facts">
          ${cost ? `<div><dt>Cost</dt><dd>${esc(cost)}</dd></div>` : ""}
          ${c.type === "unit" ? `<div><dt>Might</dt><dd>${c.might}</dd></div>` : ""}
          <div><dt>Domains</dt><dd>${(c.domains || []).length ? (c.domains || []).map(d => `<span class="v-cards-dom">${domainDot(d)}${esc(d)}</span>`).join(" ") : "none (any legend can play it)"}</dd></div>
          ${(c.tags || []).length ? `<div><dt>Tags</dt><dd>${c.tags.map(t => `<button class="chip v-cards-tag" data-tag="${esc(t)}">${esc(t)}</button>`).join(" ")}</dd></div>` : ""}
          ${c.id != null ? `<div><dt>Engine id</dt><dd class="mono">${c.id}</dd></div>` : ""}
        </dl>
        ${c.text ? `<p class="v-cards-text">${esc(c.text)}</p>` : `<p class="small muted">No rules text: a vanilla card.</p>`}
        <h3>In lab decks ${total ? `<span class="muted">(${total})</span>` : ""}</h3>
        ${total ? groups.map(g => `<div class="v-cards-dgroup"><div class="xs muted">${esc(g.title)}</div><ul class="v-cards-decks">${g.rows.map(r => `<li><a href="#/decks/${encodeURIComponent(r.path)}">${portrait(deckFace(r.path) || deckShort(r.path), { size: "sm" })}<span class="n">${esc(deckShort(r.path))}</span><span class="k mono">${r.main ? `×${r.main}` : ""}${r.side ? `<span class="muted"> +${r.side} side</span>` : ""}</span></a></li>`).join("")}</ul></div>`).join("")
          : `<p class="small muted">No lab deck plays this card. The lists are in <a href="#/decks">Decks</a>.</p>`}
      </div>
    </div>`;
}
function openDrawer(id, focus = true) {
  const c = cardById(id); if (!c || !root) return;
  st.open = id;
  const host = $("#c-drawer-host", root);
  let dr = $(".v-cards-drawer", host);
  if (!dr) { host.innerHTML = `<div class="v-cards-dback" data-drawer-back></div><aside class="v-cards-drawer" role="dialog" aria-modal="false" aria-label="card details" tabindex="-1"></aside>`; dr = $(".v-cards-drawer", host); $(".v-cards-dback", host).onclick = closeDrawer; dr.addEventListener("click", e => { const t = e.target.closest("[data-tag]"); if (t) { st.q = t.dataset.tag; const i = $("#c-q", root); if (i) i.value = st.q; st.shown = PAGE; grid(); } }); }
  dr.innerHTML = drawerHtml(c); dr.scrollTop = 0;
  document.body.classList.add("v-cards-open");
  $$(".v-cards-tile", root).forEach(t => t.classList.toggle("sel", +t.dataset.id === id));
  if (focus) { const b = $("[data-drawer=close]", dr); if (b) b.focus({ preventScroll: true }); }
}
function closeDrawer() {
  st.open = null; const host = root && $("#c-drawer-host", root); if (host) host.innerHTML = "";
  document.body.classList.remove("v-cards-open");
  if (root) $$(".v-cards-tile.sel", root).forEach(t => t.classList.remove("sel"));
}
function step(d) {
  const i = shown.findIndex(x => x.id === st.open); if (i < 0) return;
  const next = shown[i + d]; if (!next) return;
  if (i + d >= st.shown) { st.shown += PAGE; grid(); }
  openDrawer(next.id);
  const t = $(`.v-cards-tile[data-id="${next.id}"]`, root); if (t) t.scrollIntoView({ block: "nearest" });
}
