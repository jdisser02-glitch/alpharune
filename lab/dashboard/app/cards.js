/* Riven Lab — Cards: a browser of every card the engine knows, with real images.
   TODO redesign: see DESIGN.md. First version: search, type and domain filters, image grid; hover or tap a card for the preview.
   Router contract: render(container, params) / destroy(); ?q=<text> prefills the search. */
import { data, esc, cardImg, pageHead, empty, DOMAIN_NAMES, domainDot, num, $, $$ } from "./lib.js";

const TYPES = ["all", "unit", "spell", "gear", "legend", "battlefield", "rune"];
const PAGE = 96;
let st = { q: "", type: "all", doms: new Set(), set: "all", shown: PAGE };

export function render(container, params = {}) {
  if (params.q != null) st.q = params.q;
  container.innerHTML = `${pageHead({ title: "Cards", sub: `Every card the engine can play — ${num(data().cards.length)} in all. Hover a card to read it, tap on a phone.` })}
    <div class="panel"><div class="toolbar">
      <label class="f search" style="flex:1 1 240px">Search<input type="search" id="cq" value="${esc(st.q)}" placeholder="name or tag"></label>
      <label class="f">Set<select id="cset"><option value="all">all sets</option>${[...new Set(data().cards.map(c => c.set).filter(Boolean))].sort().map(s => `<option value="${esc(s)}" ${st.set === s ? "selected" : ""}>${esc(s)}</option>`).join("")}</select></label>
    </div>
    <div class="row tight" style="margin-top:12px">
      <div class="segmented" id="ctype" role="group" aria-label="card type">${TYPES.map(t => `<button data-type="${t}" aria-pressed="${st.type === t}">${t}</button>`).join("")}</div>
      <div class="row tight" id="cdoms" role="group" aria-label="domains">${DOMAIN_NAMES.map(d => `<button class="btn-sm" data-dom="${d}" aria-pressed="${st.doms.has(d)}">${domainDot(d)}${d}</button>`).join("")}</div>
    </div></div>
    <div id="cgrid" style="margin-top:16px"></div>`;
  $("#cq", container).oninput = e => { st.q = e.target.value; st.shown = PAGE; grid(container); };
  $("#cset", container).onchange = e => { st.set = e.target.value; st.shown = PAGE; grid(container); };
  $$("#ctype button", container).forEach(b => b.onclick = () => { st.type = b.dataset.type; st.shown = PAGE; $$("#ctype button", container).forEach(x => x.setAttribute("aria-pressed", x === b)); grid(container); });
  $$("#cdoms button", container).forEach(b => b.onclick = () => { const d = b.dataset.dom; st.doms.has(d) ? st.doms.delete(d) : st.doms.add(d); b.setAttribute("aria-pressed", st.doms.has(d)); b.classList.toggle("primary", st.doms.has(d)); st.shown = PAGE; grid(container); });
  $$("#cdoms button", container).forEach(b => b.classList.toggle("primary", st.doms.has(b.dataset.dom)));
  grid(container);
}

function matches(c) {
  if (st.type !== "all" && c.type !== st.type) return false;
  if (st.set !== "all" && c.set !== st.set) return false;
  if (st.doms.size && !(c.domains || []).some(d => st.doms.has(d))) return false;
  if (st.q) { const q = st.q.toLowerCase(); if (!c.name.toLowerCase().includes(q) && !(c.tags || []).some(t => t.toLowerCase().includes(q))) return false; }
  return true;
}
function grid(container) {
  const order = { unit: 0, spell: 1, gear: 2, legend: 3, rune: 4, battlefield: 5 };   // battlefields are landscape: grouped last so grid rows stay even
  const all = data().cards.filter(matches).sort((a, b) => (order[a.type] ?? 9) - (order[b.type] ?? 9) || (a.energy ?? 0) - (b.energy ?? 0) || a.name.localeCompare(b.name));
  const host = $("#cgrid", container);
  if (!all.length) { host.innerHTML = empty("No card matches", "Try a shorter search or clear a filter."); return; }
  const shown = all.slice(0, st.shown);
  host.innerHTML = `<p class="small muted" style="margin-bottom:10px">${num(all.length)} card${all.length === 1 ? "" : "s"}${all.length > shown.length ? `, showing ${shown.length}` : ""}</p>
    <div class="card-grid" style="--w:${window.innerWidth < 480 ? 96 : 128}px">${shown.map(c => `<figure>${cardImg(c, { size: "md", w: 300 })}<figcaption title="${esc(c.name)}">${esc(c.name)}</figcaption></figure>`).join("")}</div>
    ${all.length > shown.length ? `<div class="row" style="justify-content:center;margin-top:16px"><button id="cmore">Show ${Math.min(PAGE, all.length - shown.length)} more</button></div>` : ""}`;
  const more = $("#cmore", host); if (more) more.onclick = () => { st.shown += PAGE; grid(container); };
}

export function destroy() {}
