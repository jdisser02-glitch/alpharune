/* Riven Lab — Home: the landing page, written for a Riftbound player who is not an engineer.
   Sections: hero · right now (KPIs) · latest results · what's next (home.json, edited in lab/dashboard/home.yml) ·
   how to read the numbers. Everything is derived from data.json through lib.js. */
import { data, esc, pct0, num, interval, ago, fmtDate, kpi, portrait, portraits, cardImg, badge, rateBar, rateTone,
         bestList, testsByDate, testConclusion, testFaces, isPlumbing, pooled, deckFace, deckShort, plural } from "./lib.js";

let homeCopy = null;
async function copy() {
  if (homeCopy) return homeCopy;
  try { const r = await fetch("home.json"); homeCopy = r.ok ? await r.json() : {}; } catch (e) { homeCopy = {}; }
  return homeCopy;
}

export async function render(container) {
  const D = data(), H = await copy();
  const hero = H.hero || {};
  const title = hero.subtitle || "A testing ground for the Riven, The Exile deck.";
  const lede = hero.lede || "The lab plays thousands of simulated Riftbound games between deck lists and reports which changes hold up against the tournament field.";

  // ── numbers for "Right now" ──
  const best = bestList();
  const ladder = D.ladder.iterations || [], adopted = ladder.filter(r => r.accepted).slice(-1)[0], latest = ladder.slice(-1)[0];
  const tests = D.tests || [], real = tests.filter(t => !isPlumbing(t));
  const games = tests.reduce((a, t) => a + (t.decks || []).reduce((b, d) => b + pooled(d).games, 0), 0);
  const hg = (D.human_review && D.human_review.games) || [];
  const compared = hg.reduce((a, g) => a + (g.decisions_compared || 0), 0);
  const agree = compared ? hg.reduce((a, g) => a + (g.decisions_compared || 0) * (g.agreement_pct || 0) / 100, 0) / compared : null;
  const riven = D.decks.find(d => d.path === "decks/riven_sacramento.txt");

  const kpis = [
    kpi({ label: "Best list so far", value: best ? pct0(best.deck.weighted_win_rate) : "—", tone: best ? rateTone(best.deck.weighted_win_rate) : "neutral",
          sub: best ? `${esc(best.deck.label)} · interval ${interval(best.pooled.ci)} over ${num(best.pooled.games)} games` : "No test with a thinking bot yet",
          art: best ? portrait(deckFace(best.deck.path) || best.deck.label, { size: "lg" }) : "", href: best ? `#/tests/${encodeURIComponent(best.test.id)}` : "#/tests" }),
    kpi({ label: "Bot strength", value: adopted ? pct0(adopted.vs_random.rate) : "—", tone: adopted ? (adopted.vs_random.rate >= 0.75 ? "win" : "warn") : "neutral",
          sub: adopted ? `wins against a random player · iteration ${adopted.iteration} in use${latest && latest.iteration !== adopted.iteration ? `, iteration ${latest.iteration} tried and not adopted` : ""}` : "The ladder has not produced a bot yet",
          href: "#/bot" }),
    kpi({ label: "Games played", value: num(games), tone: "info", sub: `across ${plural(tests.length, "test")}, ${real.length} with a thinking bot`, href: "#/tests" }),
    kpi({ label: "Agreement with humans", value: agree == null ? "—" : pct0(agree), tone: agree == null ? "neutral" : agree >= 0.5 ? "win" : "warn",
          sub: hg.length ? `${plural(hg.length, "real game")} replayed, ${num(compared)} decisions compared` : "No human game compared yet", href: "#/replays" }),
  ].join("");

  // ── latest results ──
  const latestTests = testsByDate().slice(0, 3);
  const results = latestTests.map(t => {
    const faces = testFaces(t), ranked = (t.decks || []).filter(d => d.weighted_win_rate != null).sort((a, b) => b.weighted_win_rate - a.weighted_win_rate), top = ranked[0], tp = top && pooled(top);
    const plumbing = isPlumbing(t);
    return `<a class="result-card" href="#/tests/${encodeURIComponent(t.id)}">
      <div class="rc-head"><span class="versus">${portraits(faces.mine.length ? faces.mine : [null], { size: "md", max: 2 })}<span class="vs">vs</span>${portraits(faces.theirs.length ? faces.theirs : [null], { size: "md", max: 3 })}</span>
        ${plumbing ? badge("plumbing", "neutral") : badge("bot test", "gold")}<span class="rc-when">${esc(ago(t.started_utc))}</span></div>
      <div><div class="rc-title">${esc(t.description || t.name)}</div>${t.description ? `<div class="xs faint mono">${esc(t.name)}</div>` : ""}</div>
      <div class="rc-line">${esc(testConclusion(t))}</div>
      <div class="rc-foot">${top && !plumbing ? `${rateBar(top.weighted_win_rate, tp.ci, 110)}<span class="mono small">${pct0(top.weighted_win_rate)} · ${interval(tp.ci)}</span>` : ""}<span class="spacer"></span><span class="xs muted">${num((t.decks || []).reduce((a, d) => a + pooled(d).games, 0))} games · ${esc(fmtDate(t.started_utc))}</span></div>
    </a>`;
  }).join("");

  // ── what's next ──
  const next = (H.next || []).slice(0, 3).map((s, i) => `<div class="step"><span class="n">${i + 1}</span><div><h4>${esc(s.title || "")}</h4><p>${esc(s.text || "")}</p></div></div>`).join("");

  container.innerHTML = `
    <section class="hero" aria-labelledby="hero-title">
      <div class="hero-copy">
        <div class="eyebrow">${esc(hero.title || "Riven Lab")}</div>
        <h1 id="hero-title" class="display-xl">${esc(title.replace(/\.$/, ""))}</h1>
        <p class="lede">${esc(lede)}</p>
        <div class="hero-actions"><a class="btn btn-primary btn-lg" href="#/tests">See the test results</a><a class="btn btn-lg" href="#/play">Play against the bot</a><a class="btn btn-ghost btn-lg" href="#how">How to read the numbers</a></div>
        ${riven ? `<p class="small muted">Reference list: ${esc(deckShort(riven.path))} — ${esc(((riven.comments || [])[0] || "").replace(/^Riven, The Exile — /, "").split(" (")[0])}.</p>` : ""}
      </div>
      <div class="hero-art" aria-hidden="true">${cardImg("Blade of the Exile", { size: "lg", cls: "a1", hover: false })}${cardImg("Riven, Broken Blade", { size: "lg", cls: "a2", hover: false })}${cardImg("Riven, Deserter", { size: "lg", cls: "a3", hover: false })}</div>
    </section>

    <section class="section" style="margin-top:var(--sp-6)" aria-labelledby="now">
      <div class="section-head"><h2 id="now">Right now</h2><span class="small muted">data from ${esc(fmtDate(D.generated_utc))}</span></div>
      <div class="kpis">${kpis}</div>
    </section>

    <section class="section" style="margin-top:var(--sp-6)" aria-labelledby="latest">
      <div class="section-head"><h2 id="latest">Latest results</h2><a class="more" href="#/tests">All ${tests.length} tests →</a></div>
      ${latestTests.length ? `<div class="results">${results}</div>` : `<div class="empty"><b>No test yet</b><div class="hint">Set one up in <a href="#/launch">Launch</a>.</div></div>`}
    </section>

    ${next ? `<section class="section" style="margin-top:var(--sp-6)" aria-labelledby="next"><div class="section-head"><h2 id="next">What's next</h2></div><div class="next">${next}</div></section>` : ""}

    <section class="section" id="how" style="margin-top:var(--sp-6)" aria-labelledby="how-title">
      <div class="section-head"><h2 id="how-title">How to read the numbers</h2></div>
      <div class="explain">
        <div class="term"><h4>Win rate</h4><p>The share of simulated games a list won. Every pairing is played half on the play and half on the draw, so the seat does not tilt the number. A rate is only as good as the bot playing both sides: while the bot is weak, treat rates as hints, not verdicts.</p></div>
        <div class="term"><h4>95% interval</h4><p>Games are a coin with an unknown bias; the interval is the range the true rate most likely sits in. It shrinks with more games: about ±10 points at 100 games, ±5 at 400. Two lists whose intervals overlap are not separated yet, however different their rates look.</p>${ciIllustration()}</div>
        <div class="term"><h4>Weighted gauntlet</h4><p>A list is tested against several tournament decks, not one. Each opponent counts in proportion to how often it shows up in the field, so a good result against the most played deck matters more than one against a rare pick. The "weighted" rate is that average.</p></div>
        <div class="term"><h4>Agreement</h4><p>Real games from RiftLite are replayed through the engine, and at each of the captured player's decisions the bot is asked what it would do. Agreement is how often its first choice matches the move the human made. With 5 to 20 legal moves, a random player agrees around 10 to 20%, so a figure near that means the bot has no opinion yet.</p></div>
      </div>
    </section>`;
}

export function destroy() {}

/** Three lists with their intervals: A and B overlap (not separated), C is clear of both. */
function ciIllustration() {
  const W = 360, H = 118, L = 64, R = 16, x = v => L + (W - L - R) * (v - 0.3) / 0.5;   // 30%..80%
  const row = (y, label, lo, hi, p, color) => `<text x="${L - 10}" y="${y + 4}" text-anchor="end" style="font-size:11px">${label}</text>
    <line x1="${x(lo)}" x2="${x(hi)}" y1="${y}" y2="${y}" stroke="${color}" stroke-width="2" opacity=".9"/>
    <line x1="${x(lo)}" x2="${x(lo)}" y1="${y - 5}" y2="${y + 5}" stroke="${color}" stroke-width="2"/><line x1="${x(hi)}" x2="${x(hi)}" y1="${y - 5}" y2="${y + 5}" stroke="${color}" stroke-width="2"/>
    <circle cx="${x(p)}" cy="${y}" r="4.5" fill="${color}" stroke="var(--s1)" stroke-width="1.5"/>`;
  return `<svg class="illus" viewBox="0 0 ${W} ${H}" width="100%" role="img" aria-label="Three lists with their 95% intervals: lists A and B overlap and are not separated; list C is clear of both.">
    <line x1="${x(0.5)}" x2="${x(0.5)}" y1="6" y2="${H - 26}" stroke="var(--line-strong)" stroke-dasharray="3 3"/>
    <rect x="${x(0.48)}" y="18" width="${x(0.55) - x(0.48)}" height="38" fill="var(--warn)" opacity=".10" rx="3"/>
    ${row(26, "List A", 0.48, 0.63, 0.555, "var(--warn)")}
    ${row(48, "List B", 0.41, 0.55, 0.48, "var(--warn)")}
    ${row(74, "List C", 0.59, 0.71, 0.65, "var(--win)")}
    <text x="${x(0.515)}" y="${H - 30}" text-anchor="middle" style="font-size:10.5px">A and B overlap: not separated</text>
    ${[0.3, 0.4, 0.5, 0.6, 0.7, 0.8].map(v => `<text x="${x(v)}" y="${H - 6}" text-anchor="middle">${Math.round(100 * v)}%</text>`).join("")}
  </svg>`;
}
