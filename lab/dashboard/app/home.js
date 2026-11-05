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
  // Not passed until the median is at the target: progress is capped at 90% before that.
  const p1 = eNow == null || eStart == null ? 0 : eNow <= eT ? 1 : Math.min(0.9, clamp((eStart - eNow) / (eStart - eT)));
  // 2. The bot is a credible jury: share of simulated matchups inside real players' 95% interval (data.measured,
  //    from lab/meta/compare_meta-*.json), halved when it was measured with an older net than the one in use.
  const M = D.measured || {}, C = M.credibility || {}, Cv = M.coverage || {}, T = M.training || {};
  const p2 = C.rate == null ? 0 : clamp(C.rate * (C.stale ? 0.5 : 1));
  const A = S.agreement || {}, aHist = A.history || [], aNow = aHist.length ? aHist[aHist.length - 1] : null;
  const G = T.general || {}, specs = Object.entries(T.specialists || {});
  const train = [`General net ${esc(G.current || "—")} ${G.running ? "training" : "paused"}, ${G.rejected_since_adoption ?? 0} rejected since its adoption`]
    .concat(specs.map(([k, s]) => `${esc(k)} specialist ${s.running ? "training" : "paused"}: ${s.adopted || 0} of ${s.iterations} adopted${s.rejected_since_adoption ? `, ${s.rejected_since_adoption} rejected since` : ""}`)).join(" · ");
  // 3. The opponents are today's meta: newest tournament date among the meta lists.
  const metaDates = (D.decks || []).map(d => (/_(\d{4}-\d{2}-\d{2})\//.exec(d.path) || [])[1]).filter(Boolean).sort();
  const newest = metaDates[metaDates.length - 1], metaN = (D.decks || []).filter(d => d.path.startsWith("lab/decks/meta/")).length;
  const age = newest ? Math.max(0, (Date.now() - Date.parse(newest)) / 864e5) : null;
  const events = new Set((D.decks || []).map(d => (/^lab\/decks\/meta\/([^/]+)\//.exec(d.path) || [])[1]).filter(Boolean));
  const missing = ((S.meta || {}).missing || []).length;
  // Share of Riven's real (non-mirror) games played against the jury's legends, times freshness.
  const fresh = age == null ? 0 : age <= 14 ? 1 : clamp(1 - (age - 14) / 28);
  // The newest list test's own jury counts too: a deck verdict is only as broad as the opponents it was played against.
  const Cl = M.coverage_latest_test || {};
  const share3 = Cv.share == null ? null : Math.min(Cv.share, Cl.share ?? Cv.share);
  const p3 = share3 == null ? 0 : clamp(share3 * fresh);
  // 4. The best Riven list: only a list whose difference against the reference excludes 0 counts, and never for more than
  //    the steps it rests on (an engine, a jury and opponents it can be trusted with).
  const Dk = S.decks || {}, conf = (M.list || {}).confirmed || [], comps = (M.list || {}).comparisons || [];
  const lastComp = comps[comps.length - 1];
  const sgn = v => `${v >= 0 ? "+" : "−"}${Math.abs(Math.round(v * 1000) / 10)}`;
  const compLine = lastComp ? `Newest check (${esc(lastComp.result.replace(/^\d{8}-\d{6}-/, ""))}, ${esc(lastComp.net)}), against ${esc(lastComp.reference)}: `
    + lastComp.lists.map(x => `${esc(x.label)} ${sgn(x.diff)} points [${sgn(x.ci95[0])}, ${sgn(x.ci95[1])}]`).join("; ") : "";
  const p4 = Math.min(conf.length ? (p2 >= 0.8 ? 1 : 0.5) : 0, p1, p2, p3);
  const steps = [
    { n: 1, title: "Engine follows the real rules", pct: p1,
      line: `${fmt1(eNow)} errors per real game (median) · target ${eT} · ${eNow != null && eNow <= eT ? "passed" : "not passed"}`,
      sub: E.measured ? `Measured ${esc(E.measured)}` : "", href: "#/replays" },
    { n: 2, title: "Bot judges matchups like real players", pct: p2,
      line: C.total ? `${C.within} of ${C.total} simulated matchups inside real players' range${C.stale ? ` · measured with ${esc(C.net)}, an older bot than ${esc(C.current)}: counted half` : ""}` : "Not measured",
      sub: `${C.far && C.far.length ? `Off: ${esc(C.far.join(", "))}. ` : ""}${train}${aNow ? ` · ${fmt1(aNow.value)}% same first move as humans (${esc(aNow.label)}, partly its own training games)` : ""}`, href: "#/bot" },
    { n: 3, title: "Opponents are today's meta", pct: p3,
      line: Cv.share == null ? "Not measured" : `The jury's legends meet Riven in ${pct0(Cv.share)} of its real games (${Cv.games} of ${Cv.of})${Cl.share != null ? `; the newest list test's jury (${esc((Cl.legends || []).join(", "))}) in ${pct0(Cl.share)}` : ""}`,
      sub: `${Cv.missing_top && Cv.missing_top.length ? `Missing: ${esc(Cv.missing_top.join(", "))}. ` : ""}${metaN} tournament decks · ${events.size} events imported${missing ? `, ${missing} newer to add` : ""}`, href: "#/decks" },
    { n: 4, title: "Best Riven list found", pct: p4,
      line: conf.length ? `${conf.length} list${conf.length > 1 ? "s" : ""} beat the reference (interval above 0)` : "No list shown better than the reference",
      sub: [compLine, ((M.list || {}).stale_confirmations || []).length ? `${M.list.stale_confirmations.length} older confirmation(s) by a bot no longer in use not counted` : "", esc(Dk.line || "")].filter(Boolean).join(". "), href: "#/decks" },
  ];
  // Each step rests on the ones before it: the goal is as far along as its weakest step.
  const overall = Math.min(...steps.map(s => s.pct)), weakest = steps.reduce((a, s) => s.pct < a.pct ? s : a, steps[0]);
  const bar = f => `<div class="h-bar" role="img" aria-label="${Math.round(f * 100)}%"><i style="width:${(f * 100).toFixed(0)}%"></i></div>`;
  container.innerHTML = `<div class="v-home">
    <header class="h-top"><div><div class="h-goal-l">Goal</div><h1>The best Riven list against today's meta</h1></div><div class="h-all"><b>${Math.round(overall * 100)}%</b>${bar(overall)}<span class="h-s">limited by step ${weakest.n}</span></div></header>
    <ol class="h-steps">${steps.map(s => `<li><a class="h-step ${s.pct >= 1 ? "done" : s.pct === 0 ? "todo" : ""}" href="${s.href}">
      <span class="h-n">${s.n}</span>
      <span class="h-body"><span class="h-t">${esc(s.title)}</span><span class="h-l">${s.line}</span>${s.sub ? `<span class="h-s">${s.sub}</span>` : ""}</span>
      <span class="h-pct"><b>${Math.round(s.pct * 100)}%</b>${bar(s.pct)}</span></a></li>`).join("")}</ol>
    <p class="h-date muted small">Updated ${esc(fmtDate(D.generated_utc))}</p>
  </div>`;
}

export function destroy() {}
