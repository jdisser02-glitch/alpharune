/* Riven Lab — Home: where the lab stands, in as few words as possible.
   Three rows: can the lab be trusted (the two gates) · training (the bot's network, the search) · the deck search.
   Live values come from data.json; the history of hand-run measurements comes from home.yml's `status` block
   (dist/home.json). Styles: app/home.css (prefix .v-home). */
import { data, esc, pct0, fmtDate } from "./lib.js";

const CSS = new URL("./home.css", import.meta.url).href;
let homeCopy = null;
async function copy() {
  if (homeCopy) return homeCopy;
  try { const r = await fetch("home.json"); homeCopy = r.ok ? await r.json() : {}; } catch (e) { homeCopy = {}; }
  return homeCopy;
}
function css() { if (!document.querySelector(`link[href="${CSS}"]`)) { const l = document.createElement("link"); l.rel = "stylesheet"; l.href = CSS; document.head.append(l); } }
const median = a => { const s = [...a].sort((x, y) => x - y), n = s.length; return n ? (n % 2 ? s[(n - 1) / 2] : (s[n / 2 - 1] + s[n / 2]) / 2) : null; };
const fmt1 = v => v == null ? "—" : (Math.round(v * 10) / 10).toString();


export async function render(container) {
  css();
  const D = data(), H = await copy(), S = H.status || {};
  const clamp = f => Math.max(0, Math.min(1, f));
  // 1. The engine follows real games (live from the replay review when it ran without a bot, else home.yml).
  const hr = D.human_review || {}, hg = hr.games || [];
  const E = S.engine || {}, eHist = E.history || [], eT = E.target ?? 3, eStart = eHist.length ? eHist[0].value : null;
  const eNow = hr.agent === "random" && hg.length ? median(hg.map(g => g.desyncs_engine ?? g.desyncs ?? 0)) : (eHist.length ? eHist[eHist.length - 1].value : null);
  const p1 = eNow == null || eStart == null ? 0 : eNow <= eT ? 1 : clamp((eStart - eNow) / (eStart - eT));
  // 2. The bot plays like a good player (agreement with human moves, from home.yml until a bot review is in data.json).
  const A = S.agreement || {}, aHist = A.history || [], aT = A.target ?? 40, aNow = aHist.length ? aHist[aHist.length - 1].value : null;
  const p2 = aNow == null ? 0 : clamp(aNow / aT);
  const L = D.ladder || {}, netName = String(L.current_net || "").replace(/^.*\//, "").replace(/\.\w+$/, "");
  const st = S.settle || {};
  // 3. The opponents are today's meta: newest tournament date among the meta lists.
  const metaDates = (D.decks || []).map(d => (/_(\d{4}-\d{2}-\d{2})\//.exec(d.path) || [])[1]).filter(Boolean).sort();
  const newest = metaDates[metaDates.length - 1], metaN = (D.decks || []).filter(d => d.path.startsWith("lab/decks/meta/")).length;
  const age = newest ? Math.max(0, (Date.now() - Date.parse(newest)) / 864e5) : null;
  const events = new Set((D.decks || []).map(d => (/^lab\/decks\/meta\/([^/]+)\//.exec(d.path) || [])[1]).filter(Boolean));
  const missing = ((S.meta || {}).missing || []).length;
  const p3 = age == null ? 0 : clamp((age <= 14 ? 1 : 1 - (age - 14) / 28) * events.size / Math.max(1, events.size + missing));
  // 4. The best Riven list: waits for steps 1 and 2.
  const Dk = S.decks || {}, p4 = typeof Dk.pct === "number" ? Dk.pct : 0;
  const steps = [
    { n: 1, title: "Engine follows the real rules", pct: p1, line: `${fmt1(eNow)} errors per real game · target ${eT}`, href: "#/replays" },
    { n: 2, title: "Bot plays like a good player", pct: p2, line: `${aNow == null ? "—" : fmt1(aNow)}% same moves as humans · target ${aT}%`, sub: `Training paused · ${esc(netName)}${st.rate != null ? ` · combat look-ahead ${pct0(st.rate)} wins, ${esc(st.state || "to confirm")}` : ""}`, href: "#/bot" },
    { n: 3, title: "Opponents are today's meta", pct: p3, line: `${metaN} tournament decks · ${events.size} events imported${missing ? `, ${missing} newer to add` : ""}`, href: "#/decks" },
    { n: 4, title: "Best Riven list found", pct: p4, line: esc(Dk.line || "Starts when steps 1 and 2 reach 100%"), href: "#/decks" },
  ];
  const overall = steps.reduce((a, s) => a + s.pct, 0) / steps.length;
  const bar = f => `<div class="h-bar" role="img" aria-label="${Math.round(f * 100)}%"><i style="width:${(f * 100).toFixed(0)}%"></i></div>`;
  container.innerHTML = `<div class="v-home">
    <header class="h-top"><div><div class="h-goal-l">Goal</div><h1>The best Riven list against today's meta</h1></div><div class="h-all"><b>${Math.round(overall * 100)}%</b>${bar(overall)}</div></header>
    <ol class="h-steps">${steps.map(s => `<li><a class="h-step ${s.pct >= 1 ? "done" : s.pct === 0 ? "todo" : ""}" href="${s.href}">
      <span class="h-n">${s.n}</span>
      <span class="h-body"><span class="h-t">${esc(s.title)}</span><span class="h-l">${s.line}</span>${s.sub ? `<span class="h-s">${s.sub}</span>` : ""}</span>
      <span class="h-pct"><b>${Math.round(s.pct * 100)}%</b>${bar(s.pct)}</span></a></li>`).join("")}</ol>
    <p class="h-date muted small">Updated ${esc(fmtDate(D.generated_utc))}</p>
  </div>`;
}

export function destroy() {}
