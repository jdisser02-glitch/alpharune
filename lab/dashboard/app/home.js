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
const since = n => n ? ` · ${n} engine change${n > 1 ? "s" : ""} since, not re-measured` : "";
const fmt1 = v => v == null ? "—" : (Math.round(v * 10) / 10).toString();


export async function render(container) {
  css();
  const D = data(), H = await copy(), S = H.status || {};
  const clamp = f => Math.max(0, Math.min(1, f));
  // 1. The engine follows real games (live from the replay review when it ran without a bot, else home.yml).
  const hr = D.human_review || {}, hg = hr.games || [];
  const E = S.engine || {}, eHist = E.history || [], eT = E.target ?? 3, eStart = eHist.length ? eHist[0].value : null;
  // Newest first: a replay of the RiftLite games through the current engine (data.measured.engine_replay) when it is
  // newer than the bot review, else the review when it ran without a bot, else home.yml's history.
  const ER = (D.measured || {}).engine_replay, erNewer = ER && (!hr.generated || String(ER.generated) > String(hr.generated));
  const eNow = erNewer ? ER.median : hr.agent === "random" && hg.length ? median(hg.map(g => g.desyncs_engine ?? g.desyncs ?? 0)) : (eHist.length ? eHist[eHist.length - 1].value : null);
  // Not passed until the median is at the target: progress is capped at 90% before that.
  const p1 = eNow == null || eStart == null ? 0 : eNow <= eT ? 1 : Math.min(0.9, clamp((eStart - eNow) / (eStart - eT)));
  // 2. The bot is a credible jury: share of simulated matchups inside real players' 95% interval (data.measured,
  //    from lab/meta/compare_meta-*.json), halved when it was measured with an older net than the one in use.
  const M = D.measured || {}, C = M.credibility || {}, Cv = M.coverage || {}, T = M.training || {};
  // Since 7 Nov the step is scored on the right favourite (lab/tools/favorites.py): the share of Riven's matchups
  // where the simulation favours the same side as real players. The field measure (10+ legends) counts when its
  // pilots are the nets in use, else Riven's specialist jury (current net, fewer legends).
  const F = M.favorites || {}, Fs = F.specialists || {};
  const R = F.field && !F.field.stale && F.field.total ? F.field : (Fs.riven && Fs.riven.total ? Fs.riven : F.field);
  const p2 = R && R.rate != null ? clamp(R.rate * (R.stale ? 0.5 : 1)) : C.rate == null ? 0 : clamp(C.rate * (C.stale ? 0.5 : 1));
  const favOf = s => `${s.right}/${s.total}`;
  const others = Object.entries(Fs).filter(([k, s]) => k !== "riven" && s.total).map(([k, s]) => `${esc(s.legend)} ${favOf(s)}${s.engine_commits_since ? ` (before ${s.engine_commits_since} engine change${s.engine_commits_since > 1 ? "s" : ""})` : ""}`).join(", ");
  const cParts = [C.stale ? `${esc(C.net)}, an older bot` : "", C.engine_commits_since ? `before ${C.engine_commits_since} engine change${C.engine_commits_since > 1 ? "s" : ""}` : ""].filter(Boolean);
  const cTag = cParts.length ? ` (${cParts.join("; ")})` : "";
  const netNo = s => { const m = /net[ _](\d+)/.exec(String(s || "")); return m ? +m[1] : null; };
  const A = S.agreement || {}, aHist = A.history || [], aNow = aHist.length ? aHist[aHist.length - 1] : null;
  const G = T.general || {}, specs = Object.entries(T.specialists || {});
  const stateOf = s => s.state || (s.running ? "training" : "paused");
  const train = [`General net ${esc(G.current || "—")} ${stateOf(G)}, ${G.rejected_since_adoption ?? 0} rejected since its adoption`]
    .concat(specs.map(([k, s]) => `${esc(k)} specialist ${stateOf(s)}: ${s.adopted || 0} of ${s.iterations} adopted${s.rejected_since_adoption ? `, ${s.rejected_since_adoption} rejected since` : ""}`)).join(" · ");
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
  const tidy = l => String(l || "").replace(/\s*\([+\u2212-]\d[^)]*\)/, "");
  const compLine = lastComp ? `Newest check (${esc(lastComp.result.replace(/^\d{8}-\d{6}-/, ""))}, ${esc(lastComp.net)}), against ${esc(lastComp.reference)}: `
    + lastComp.lists.map(x => `${esc(tidy(x.label))} ${sgn(x.diff)} points [${sgn(x.ci95[0])}, ${sgn(x.ci95[1])}]`).join("; ")
    + since(((M.list || {}).best || {}).engine_commits_since) : "";
  const p4 = Math.min(conf.length ? (p2 >= 0.8 ? 1 : 0.5) : 0, p1, p2, p3);
  const steps = [
    { n: 1, title: "Engine follows the real rules", pct: p1,
      line: `${fmt1(eNow)} errors per real game (median) · target ${eT} · ${eNow != null && eNow <= eT ? "passed" : "not passed"}`,
      sub: erNewer ? `Replay of ${ER.games} RiftLite games through the engine, ${esc(fmtDate(ER.generated))} (all desyncs, mean ${fmt1(ER.mean)}; the start value ${fmt1(eStart)} counted engine-side desyncs only)${since(ER.engine_commits_since)}` : `${E.measured ? `Measured ${esc(E.measured)}` : ""}${since(hr.engine_commits_since)}`, href: "#/replays" },
    { n: 2, title: "Bot judges matchups like real players", pct: p2,
      line: R && R.total ? `${R.right} of ${R.total} Riven matchups with real players' favourite (target ${pct0(R.target ?? 0.8)}) · ${esc(R.source)}, ${esc(R.net || "")}${R.stale ? ": counted half" : ""}${(R.pilots_replaced || []).length ? ` · opponent pilot(s) replaced since: ${esc(R.pilots_replaced.join(", "))}` : ""}${R.spearman != null ? ` · rank correlation with real rates ${R.spearman}` : ""}${since(R.engine_commits_since)}` : "Not measured",
      sub: `${R && R.wrong && R.wrong.length ? `Wrong favourite: ${esc(R.wrong.join(", "))}. ` : ""}${others ? `Other legends' juries: ${others}. ` : ""}${C.total ? `Older, stricter measure: ${C.within} of ${C.total} inside real players' 95% range${cTag}. ` : ""}${train}${aNow ? ` · ${fmt1(aNow.value)}% same first move as humans (${esc(aNow.label)}${netNo(aNow.label) != null && netNo(G.current) != null && netNo(aNow.label) !== netNo(G.current) ? ", a net no longer in use" : ""}, partly its own training games${since(hr.engine_commits_since).replace(" · ", "; ")})` : ""}`, href: "#/bot" },
    { n: 3, title: "Opponents are today's meta", pct: p3,
      line: Cv.share == null ? "Not measured" : `The jury's legends meet Riven in ${pct0(Cv.share)} of its real games (${Cv.games} of ${Cv.of})${Cl.share != null ? `; the newest list test's jury (${esc((Cl.legends || []).join(", "))}) in ${pct0(Cl.share)}` : ""}`,
      sub: `${Cv.missing_top && Cv.missing_top.length ? `Missing: ${esc(Cv.missing_top.join(", "))}. ` : ""}${Cl.missing_top && Cl.missing_top.length ? `Missing from the newest list test: ${esc(Cl.missing_top.join(", "))}. ` : ""}${metaN} tournament decks · ${events.size} events imported${missing ? `, ${missing} newer to add` : ""}`, href: "#/decks" },
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
