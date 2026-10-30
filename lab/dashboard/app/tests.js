/* Riven Lab — Tests: every simulated test, and one test in detail.
   #/tests        page head, KPIs, the list of tests (plumbing checks de-emphasised), the human-game comparison table
   #/tests/<id>   conclusion first, facts, opponents and their weights, per tested deck the matchup table + card grid,
                  the optimizer's round-by-round standings when the test ran rounds, commands and raw spec for the curious.
   Everything is derived from data.json through lib.js; screen-specific styles are in tests.css (injected once). */
import { data, esc, pct0, num, interval, fmtDate, fmtDateTime, ago, duration, plural, kpi, badge, chip, pageHead, empty, notice,
         portrait, portraits, cardChip, cardGrid, rateBar, rate, rateTone, table, enhance, wilson, pooled, cssOnce, botSpec, botBadge,
         isPlumbing, testsByDate, testById, bestList, testConclusion, testFaces, deckFace, deckByPath, deckHero, legendShort } from "./lib.js";

const CSS = new URL("./tests.css", import.meta.url).href;

/** Both sides of a test in one phrase: "the bot (40 continuations per move, network 0) on both sides" / "… against a random player". */
function sidesText(t) {
  const a = botSpec(t.agent), b = botSpec(t.opp_agent || t.agent);
  return a.raw === b.raw ? `${a.text} on both sides` : `${a.text} against ${b.text}`;
}

// ── shared numbers ──────────────────────────────────────────────────────────
const testGames = t => (t.decks || []).reduce((a, d) => a + pooled(d).games, 0);
const ranked = t => (t.decks || []).filter(d => d.weighted_win_rate != null).sort((a, b) => b.weighted_win_rate - a.weighted_win_rate);
const isOptimize = t => !!(t.optimize && (t.optimize.rounds || []).length);
const isControl = label => /\b(control|base)\b/i.test(label || "");
/** A tested list's label; the ladder tests label their side with the raw agent spec, which reads better as the bot's name. */
const labelOf = d => /^(ismcts|mcts|random|greedy)/i.test(d.label || "") ? botSpec(d.label).label : (d.label || "");
const hasReplays = t => (data().replays || []).some(r => r.test_id === t.id);
const opponentsOf = t => {
  const seen = new Map();
  for (const d of t.decks || []) for (const m of d.matchups || []) if (!seen.has(m.opponent_deck || m.opponent)) seen.set(m.opponent_deck || m.opponent, { label: m.opponent, deck: m.opponent_deck, weight: m.weight ?? 1 });
  return [...seen.values()];
};
/** "−1 Scuttle Crab, +1 Baron Nashor" as card chips with the delta. */
function changeChips(changes) {
  const md = (changes && changes.maindeck) || {}; const es = Object.entries(md).sort((a, b) => a[1] - b[1]);
  if (!es.length) return "";
  return `<span class="t-changes">${es.map(([n, d]) => `<span class="t-delta ${d > 0 ? "add" : "rem"}">${d > 0 ? "+" : "−"}${Math.abs(d)}</span>${cardChip(n, { cls: "sm" })}`).join("")}</span>`;
}
/** Seat split "P1 4/10 · P2 7/10" from a matchup. */
function seatSplit(m) {
  if (!(m.as_p1 && m.as_p2)) return `<span class="muted">—</span>`;
  const one = (k, s) => `<span class="t-seat"><span class="badge ${k}">${k.toUpperCase()}</span><span class="mono">${s.wins}/${s.games}</span></span>`;
  return one("p1", m.as_p1) + one("p2", m.as_p2);
}

// ── list ────────────────────────────────────────────────────────────────────
function renderList(container) {
  const D = data(), tests = testsByDate(), real = tests.filter(t => !isPlumbing(t));
  const games = tests.reduce((a, t) => a + testGames(t), 0);
  const best = bestList(), latest = tests[0];
  const kpis = [
    kpi({ label: "Tests", value: num(tests.length), tone: "info", sub: real.length === tests.length ? "all with a thinking bot" : `${num(real.length)} with a thinking bot, ${num(tests.length - real.length)} plumbing` }),
    kpi({ label: "Games played", value: num(games), tone: "info", sub: real.length ? `${num(real.reduce((a, t) => a + testGames(t), 0))} of them with a thinking bot` : "none with a thinking bot yet" }),
    kpi({ label: "Best list so far", value: best ? pct0(best.deck.weighted_win_rate) : "—", tone: best ? rateTone(best.deck.weighted_win_rate) : "neutral",
          sub: best ? `${esc(best.deck.label)} in ${esc(best.test.name)} · interval ${interval(best.pooled.ci)} over ${plural(best.pooled.games, "game")}` : "No test with a thinking bot yet",
          art: best ? portrait(deckFace(best.deck.path) || best.deck.label, { size: "lg" }) : "", href: best ? `#/tests/${encodeURIComponent(best.test.id)}` : "" }),
    kpi({ label: "Latest test", value: latest ? ago(latest.started_utc) : "—", tone: "neutral", sub: latest ? `${esc(fmtDate(latest.started_utc))} · ${esc(latest.name)}` : "Set one up in Launch", href: latest ? `#/tests/${encodeURIComponent(latest.id)}` : "" }),
  ].join("");

  const rows = tests.map(t => {
    const faces = testFaces(t), plumb = isPlumbing(t), opt = isOptimize(t), rk = ranked(t), shown = rk.slice(0, 3), g = testGames(t);
    const per = t.games_per_matchup ? `${num(t.games_per_matchup)} per matchup` : "";
    const results = plumb
      ? `<div class="t-res-note">${esc(testConclusion(t))}</div>`
      : shown.length ? `<div class="t-results">${shown.map(d => { const p = pooled(d); return `<div class="t-res"><span class="t-res-l" title="${esc(labelOf(d))}">${isControl(d.label) ? badge("control", "neutral") + " " : ""}${esc(labelOf(d))}</span>${rateBar(d.weighted_win_rate, p.ci, 110)}<span class="mono t-res-n">${pct0(d.weighted_win_rate)} <span class="muted">${interval(p.ci)} · ${num(p.games)} games</span></span></div>`; }).join("")}${rk.length > shown.length ? `<div class="xs muted">and ${rk.length - shown.length} more lists</div>` : ""}</div>` : `<div class="t-res-note muted">No result recorded.</div>`;
    return `<a class="t-row ${plumb ? "plumb" : ""}" href="#/tests/${encodeURIComponent(t.id)}" aria-label="${esc(t.name)}">
      <div class="t-who"><span class="versus">${portraits(faces.mine.length ? faces.mine : [null], { size: "md", max: 2 })}<span class="vs">vs</span>${portraits(faces.theirs.length ? faces.theirs : [null], { size: "md", max: 3 })}</span></div>
      <div class="t-main">
        <div class="t-title">${esc(t.description || t.name)}</div>
        <div class="t-meta"><span class="mono faint">${esc(t.name)}</span>${plumb ? badge("plumbing", "neutral") : botBadge(t.agent)}${!plumb && t.opp_agent && t.opp_agent !== t.agent ? `<span class="xs muted">vs</span>${botBadge(t.opp_agent)}` : ""}${opt ? badge(`${t.optimize.rounds.length} rounds`, "gold") : ""}</div>
      </div>
      <div class="t-side">${results}</div>
      <div class="t-foot"><span class="mono">${num(g)} games${per ? ` · ${per}` : ""}</span><span class="mono faint">${esc(fmtDate(t.started_utc))}</span>${hasReplays(t) ? badge("replays", "gold") : ""}</div>
    </a>`;
  }).join("");

  container.innerHTML = `<div class="v-tests stack loose">
    ${pageHead({ title: "Tests", eyebrow: "Simulated matches", sub: `Each test plays one or more Riven lists against a set of tournament decks, half the games on the play and half on the draw. A win rate is only as good as the bot playing both sides, so every number carries its 95% interval, the range the true rate most likely sits in, and its game count. Tests marked <span class="badge neutral">plumbing</span> use random players: they check that the engine runs and say nothing about deck strength.` })}
    <div class="kpis">${kpis}</div>
    <section class="section" aria-labelledby="t-list-h">
      <div class="section-head"><h2 id="t-list-h">All tests</h2><span class="small muted">newest first</span></div>
      ${tests.length ? `<div class="t-list">${rows}</div>` : empty("No test yet", `Set one up in <a href="#/launch">Launch</a>.`)}
    </section>
    ${humanGames(D)}
  </div>`;
  enhance(container);
}

/** Human games replayed through the engine: does the bot play like a person? Lives here as in the old page; Replays opens each game. */
function humanGames(D) {
  const hr = D.human_review, games = (hr && hr.games) || [];
  if (!games.length) return "";
  const faces = g => { const l = g.legends || {}; return `<span class="versus">${portrait(l.P1 || "?", { size: "sm" })}<span class="vs">vs</span>${portrait(l.P2 || "?", { size: "sm" })}</span>`; };
  const cols = [
    { key: "date", label: "Game", sort: true, render: g => `<span class="mono">${esc(g.date)}${g.game > 1 ? ` · game ${g.game}` : ""}</span>` },
    { key: "players", label: "Players", render: g => `<span class="t-players">${faces(g)}<span><b>${esc(g.capture_player)}</b> <span class="muted">(${esc(legendShort((g.legends || {})[g.capture_seat === 2 ? "P2" : "P1"]))})</span> vs ${esc((g.players || []).find(p => p !== g.capture_player) || "")} <span class="muted">(${esc(legendShort((g.legends || {})[g.capture_seat === 2 ? "P1" : "P2"]))})</span></span></span>` },
    { key: "decisions_compared", label: "Compared", num: true, sort: true, render: g => `${num(g.decisions_compared)}<span class="muted">/${num(g.decisions)}</span>` },
    { key: "agreement_pct", label: "Bot agrees", sort: true, render: g => { const n = g.decisions_compared || 0, k = Math.round(n * (g.agreement_pct || 0) / 100); return n ? rate(k / n, wilson(k, n)) : "—"; } },
    { key: "agreement_clean_pct", label: "Clean", num: true, sort: true, render: g => g.agreement_clean_pct != null ? `${g.agreement_clean_pct.toFixed(0)}% <span class="muted">(${num(g.compared_clean)})</span>` : "—" },
    { key: "desyncs", label: "Desyncs", num: true, sort: true },
    { key: "stop_reason", label: "Stopped", render: g => `<span class="small muted">${esc(g.stop_reason || "")}</span>` },
    { key: "open", label: "", render: g => (D.replays || []).some(x => x.file === g.replay) ? `<a href="#/replays/${encodeURIComponent(g.replay)}">Open</a>` : "" },
  ];
  return `<section class="section" aria-labelledby="t-human-h">
    <div class="section-head"><h2 id="t-human-h">Real games: does the bot play like a person?</h2><a class="more" href="#/replays">Replays →</a></div>
    <p class="muted">Each row is a real RiftLite game replayed through the engine. At every decision of the captured player the bot (${botBadge(hr.agent)}) was asked what it would do. Agreement is the share of those decisions where the bot's first choice is the move the human made, with its 95% interval; "clean" counts only decisions taken right after the engine's state was checked against the log. With 5 to 20 legal moves a random player agrees around 10 to 20%, so a figure near that means the bot has no opinion yet. Desyncs are points where the engine could not follow the log; the replay stops after a few.</p>
    ${table(cols, games, { empty: "No human game compared yet." })}
  </section>`;
}

// ── detail ──────────────────────────────────────────────────────────────────
/** A short paragraph: the conclusion first, then the evidence. */
function summary(t) {
  const games = testGames(t), plumb = isPlumbing(t), rk = ranked(t), opps = opponentsOf(t);
  const lead = testConclusion(t);
  if (plumb) return `${lead} ${num(games)} games in all, ${num(t.games_per_matchup)} per matchup.`;
  const parts = [lead];
  if (isOptimize(t)) {
    const r = t.optimize.rounds, first = r[0], last = r[r.length - 1];
    parts.push(`${num(first.standings.length)} candidate lists started; each round played the survivors against the gauntlet with more games (${r.map(x => num(x.games_per_opponent)).join(", then ")} per opponent) and kept the top half, down to ${num(last.standings.length)} finalists after ${plural(r.length, "round")}${t.optimize.complete === false ? ", not yet complete" : ""}.`);
  }
  if (t.kind === "ga" || t.ga) {
    const g = t.ga || {}, gens = (g.generations || []).length, pop = (t.spec && t.spec.population) || (g.population || []).length || 16;
    parts.push(`How it works: a <b>generation</b> is a group of ${num(pop)} versions of the list that all play the same opponents with the same card deals. The best ones survive unchanged; the others are replaced by children, made by mixing two good versions and swapping one or two cards. ${plural(gens, "generation")} ran. A version that survives keeps adding games, so its win rate is measured better and better.`);
    parts.push(`How to read a row: each version is described by its difference with the unchanged list, <b>−1</b> means one copy of that card was removed, <b>+1</b> one copy added; "base (control)" is the unchanged list itself. The win rate counts all the games that version played, against the whole jury.`);
  }
  parts.push(`${num(games)} games in all${t.games_per_matchup ? `, ${num(t.games_per_matchup)} per matchup` : ""}, half on the play and half on the draw; ${sidesText(t)}.`);
  if (opps.length > 1) {
    const w = opps.reduce((a, o) => a + (o.weight || 0), 0), top = [...opps].sort((a, b) => b.weight - a.weight)[0];
    parts.push(w && top && top.weight / w > 1 / opps.length + 0.05 ? `The headline rate is weighted by how often each opponent shows up in the field: ${esc(top.label)} counts for ${pct0(top.weight / w)} of it.` : `The headline rate weights the ${opps.length} opponents equally.`);
  }
  if (rk.length > 1) {
    const top = rk[0], base = rk.find(d => isControl(d.label)) || rk[rk.length - 1];
    if (base !== top) { const tp = pooled(top), bp = pooled(base); parts.push(`${esc(top.label)}: ${pct0(top.weighted_win_rate)} (${interval(tp.ci)}, ${num(tp.games)} games) against ${isControl(base.label) ? "the control" : esc(base.label)} at ${pct0(base.weighted_win_rate)} (${interval(bp.ci)}, ${num(bp.games)} games).`); }
  }
  return parts.join(" ");
}

function matchupTable(d, t) {
  const cols = [
    { key: "opponent", label: "Opponent", sort: true, render: m => { const dk = deckByPath(m.opponent_deck); return `<span class="t-opp">${portrait(deckFace(m.opponent_deck) || legendShort(m.opponent), { size: "md" })}<span><span class="t-opp-l">${esc(m.opponent)}</span>${dk ? `<a class="sub" href="#/decks/${encodeURIComponent(dk.path)}">Open the list</a>` : ""}</span></span>`; } },
    { key: "weight", label: "Weight", num: true, sort: true, render: m => num(m.weight ?? 1, (m.weight ?? 1) % 1 ? 1 : 0) },
    { key: "games", label: "Games", num: true, sort: true },
    { key: "wins", label: "Wins", num: true, sort: true },
    { key: "win_rate", label: "Win rate and 95% interval", sort: true, render: m => rate(m.win_rate, m.ci95 || wilson(m.wins, m.games)) },
    { key: "seats", label: "On the play · on the draw", render: m => `<span class="t-seats">${seatSplit(m)}</span>` },
    { key: "avg_turns", label: "Turns", num: true, sort: true, render: m => m.avg_turns != null ? num(m.avg_turns) : "—" },
    { key: "wall_s", label: "Time", num: true, sort: true, render: m => m.wall_s != null ? duration(m.wall_s) : "—" },
  ];
  return table(cols, d.matchups || [], { empty: "No matchup recorded." });
}

function deckSection(d, t, i, open) {
  const p = pooled(d), dk = deckByPath(d.path), face = deckFace(d.path) || d.label;
  const list = dk ? dk.list : null;
  const grid = list ? cardGrid({ ...(list.maindeck || {}) }, { champion: list.champion || {}, w: 88 }) : d.stats && d.stats.list ? cardGrid(Object.fromEntries(d.stats.list.map(c => [c.name, c.count])), { w: 88 }) : "";
  const champ = list ? Object.keys(list.champion || {}) : [];
  const seats = (d.matchups || []).reduce((a, m) => { if (m.as_p1 && m.as_p2) { a.p1g += m.as_p1.games; a.p1w += m.as_p1.wins; a.p2g += m.as_p2.games; a.p2w += m.as_p2.wins; } return a; }, { p1g: 0, p1w: 0, p2g: 0, p2w: 0 });
  const head = `<summary class="t-deck-head">
      ${portrait(face, { size: "lg" })}
      <div class="t-deck-title"><h2>${esc(labelOf(d))}${isControl(d.label) ? ` ${badge("control", "neutral")}` : ""}</h2>
        <div class="t-deck-sub">${dk ? `<a href="#/decks/${encodeURIComponent(dk.path)}">${esc(deckHero(dk))}${dk.path.startsWith("lab/variants/") ? ` · ${esc(dk.path.split("/").pop().replace(/\.txt$/, ""))}` : ""} · open the list</a>` : `<span class="mono muted">${esc(d.path || "")}</span>`}${changeChips(d.changes)}</div></div>
      <div class="t-deck-rate">${d.weighted_win_rate != null ? `${rateBar(d.weighted_win_rate, p.ci, 150)}<span class="t-deck-num">${pct0(d.weighted_win_rate)}</span><span class="xs muted">${interval(p.ci)} · ${plural(p.games, "game")}${t.decks.some(x => (x.matchups || []).length > 1) ? " · weighted" : ""}</span>` : `<span class="muted">no result</span>`}</div>
    </summary>`;
  return `<details class="panel t-deck" ${open ? "open" : ""} id="deck-${i}">${head}
    <div class="stack" style="margin-top:var(--sp-4)">
      ${d.errors && d.errors.length ? notice(`Not a playable list: ${d.errors.map(esc).join("; ")}.`, "error") : ""}
      ${seats.p1g ? `<div class="row tight small muted"><span>${num(p.wins)} wins in ${num(p.games)} games</span><span>·</span><span>on the play ${pct0(seats.p1w / seats.p1g)} (${seats.p1w}/${seats.p1g})</span><span>·</span><span>on the draw ${pct0(seats.p2w / seats.p2g)} (${seats.p2w}/${seats.p2g})</span></div>` : ""}
      ${matchupTable(d, t)}
      ${grid ? `<div class="stack tight"><h3>The list${champ.length ? ` · champion ${champ.map(c => cardChip(c, { cls: "sm" })).join(" ")}` : ""}</h3>${grid}</div>` : ""}
    </div></details>`;
}

function opponentsPanel(t) {
  const opps = opponentsOf(t); if (!opps.length) return "";
  const w = opps.reduce((a, o) => a + (o.weight || 0), 0) || 1;
  return `<section class="panel"><div class="panel-head"><h3>Opponents and their weights</h3><span class="small muted">${opps.length === 1 ? "one opponent" : `${opps.length} opponents, weight = share of the headline rate`}</span></div>
    <div class="t-opps">${[...opps].sort((a, b) => b.weight - a.weight).map(o => `<a class="t-opp-card" href="${o.deck && deckByPath(o.deck) ? `#/decks/${encodeURIComponent(o.deck)}` : "#/decks"}">${portrait(deckFace(o.deck) || legendShort(o.label), { size: "lg" })}<span class="t-opp-card-l">${esc(o.label)}</span><span class="t-opp-w"><i style="width:${(100 * o.weight / w).toFixed(0)}%"></i></span><span class="xs muted mono">weight ${num(o.weight, o.weight % 1 ? 1 : 0)} · ${pct0(o.weight / w)}</span></a>`).join("")}</div></section>`;
}

/** Optimizer: one row per candidate, one column per round (rate + games), survivors highlighted, control marked, overall result last. */
function roundsPanel(t) {
  const rounds = t.optimize.rounds, nOpp = (t.spec && t.spec.opponents && t.spec.opponents.length) || opponentsOf(t).length || 1;
  const labels = [...new Set(rounds.flatMap(r => r.standings.map(s => s.label)))];
  const byLabel = new Map((t.decks || []).map(d => [d.label, d]));
  const survived = labels.map(l => rounds.filter(r => r.standings.some(s => s.label === l)).length);
  const order = labels.map((l, i) => ({ l, i })).sort((a, b) => survived[b.i] - survived[a.i] || ((byLabel.get(b.l) || {}).weighted_win_rate ?? -1) - ((byLabel.get(a.l) || {}).weighted_win_rate ?? -1) || (rounds[0].standings.find(s => s.label === a.l) || {}).rate - (rounds[0].standings.find(s => s.label === b.l) || {}).rate);
  const cell = (l, ri) => {
    const r = rounds[ri], s = r.standings.find(x => x.label === l);
    if (!s) return `<td class="t-rnd out"><span class="muted">—</span></td>`;
    const g = r.games_per_opponent * nOpp, wins = Math.round(s.rate * g), ci = wilson(wins, g), next = rounds[ri + 1], on = next ? next.standings.some(x => x.label === l) : true;
    const pos = r.standings.findIndex(x => x.label === l) + 1;
    return `<td class="t-rnd ${on ? "on" : "cut"}" data-v="${s.rate}"><span class="t-rnd-top"><span class="mono t-rnd-pos">${pos}.</span>${rateBar(s.rate, ci, 90)}<span class="mono">${pct0(s.rate)}</span></span><span class="xs muted">${interval(ci)} · ${num(g)} games${!on ? " · cut" : next ? " · on" : ""}</span></td>`;
  };
  const rows = order.map(({ l }) => {
    const d = byLabel.get(l), p = d ? pooled(d) : null, ctrl = isControl(l), fin = survived[labels.indexOf(l)] === rounds.length;
    return `<tr class="${fin ? "finalist" : ""} ${ctrl ? "control" : ""}"><td class="lead t-cand"><span>${esc(l)}</span>${ctrl ? badge("control", "neutral") : ""}${fin ? badge("finalist", "gold") : ""}${d ? changeChips(d.changes) : ""}</td>
      ${rounds.map((_, ri) => cell(l, ri)).join("")}
      <td class="t-rnd" data-v="${d ? d.weighted_win_rate : ""}">${d && d.weighted_win_rate != null ? `<span class="t-rnd-top">${rateBar(d.weighted_win_rate, p.ci, 90)}<span class="mono">${pct0(d.weighted_win_rate)}</span></span><span class="xs muted">${interval(p.ci)} · ${num(p.games)} games</span>` : `<span class="muted">—</span>`}</td></tr>`;
  }).join("");
  const r1 = rounds[0], last = rounds[rounds.length - 1];
  return `<section class="panel t-rounds-panel" aria-labelledby="t-rounds-h">
    <div class="panel-head"><h2 id="t-rounds-h">Round by round</h2><span class="small muted">${num(r1.standings.length)} candidates → ${num(last.standings.length)} finalists in ${plural(rounds.length, "round")}</span></div>
    <p class="muted small" style="margin-top:8px">Every round plays each surviving list against the ${nOpp === 1 ? "opponent" : `${nOpp} opponents`} with more games than the last, then keeps the top half. Early rounds are cheap and noisy: a list cut in round 1 on ${num(r1.games_per_opponent * nOpp)} games may simply have been unlucky. The last column pools every game a list played, with its 95% interval. Rows in gold reached the final round; the control is the unchanged base list.</p>
    <div class="tablewrap"><table class="data t-rounds"><thead><tr><th>Candidate</th>${rounds.map(r => `<th data-sort="num">Round ${r.round} <span class="faint">· ${num(r.games_per_opponent * nOpp)} games</span></th>`).join("")}<th data-sort="num">Overall (weighted)</th></tr></thead><tbody>${rows}</tbody></table></div>
  </section>`;
}

function renderDetail(container, id) {
  const t = testById(id);
  if (!t) { container.innerHTML = `<div class="v-tests stack">${pageHead({ title: "Test not found", sub: `No test is recorded under <span class="mono">${esc(id)}</span>.` })}${empty("Nothing here", `See <a href="#/tests">all tests</a>.`)}</div>`; return; }
  const plumb = isPlumbing(t), faces = testFaces(t), games = testGames(t), opt = isOptimize(t);
  const dur = t.finished_utc && t.started_utc ? (new Date(t.finished_utc) - new Date(t.started_utc)) / 1000 : null;
  const decks = opt ? [...t.decks].sort((a, b) => (b.weighted_win_rate ?? -1) - (a.weighted_win_rate ?? -1)) : t.decks || [];
  const actions = `${hasReplays(t) ? `<a class="btn btn-primary" href="#/replays?test=${encodeURIComponent(t.id)}">Watch the replays</a>` : `<span class="small muted">No replay recorded for this test</span>`}<a class="btn" href="#/tests">All tests</a>`;
  const facts = [
    chip(`${num(games)} games`), t.games_per_matchup ? chip(`${num(t.games_per_matchup)} per matchup`) : "", chip(`started ${esc(fmtDateTime(t.started_utc))}`),
    dur != null && dur > 0 ? chip(`took ${esc(duration(dur))}`) : "", t.threads ? chip(`on ${num(t.threads)} cores`) : "", t.seed != null ? chip(`seed <span class="mono">${esc(t.seed)}</span>`) : "", t.engine_commit ? chip(`engine <span class="mono">${esc(t.engine_commit)}</span>`) : "",
  ].filter(Boolean).join("");
  const commands = (t.decks || []).flatMap(d => (d.matchups || []).flatMap(m => m.commands || []));
  container.innerHTML = `<div class="v-tests stack loose">
    ${pageHead({ eyebrow: `${plumb ? "Plumbing check" : opt ? "Optimizer test" : "Bot test"} · ${esc(fmtDate(t.started_utc))}`, title: t.name, actions, sub: `<span class="t-head-sub"><span class="t-head-faces"><span class="versus">${portraits(faces.mine.length ? faces.mine : [null], { size: "md", max: 3 })}<span class="vs">vs</span>${portraits(faces.theirs.length ? faces.theirs : [null], { size: "md", max: 5 })}</span>${plumb ? badge("plumbing", "neutral") : botBadge(t.agent)}${!plumb && t.opp_agent && t.opp_agent !== t.agent ? `<span class="xs muted">vs</span>${botBadge(t.opp_agent)}` : ""}</span>${t.description ? `<span class="t-desc">${esc(t.description)}</span>` : ""}</span>` })}
    <section class="panel gilt t-summary"><h3>What this test says</h3><p class="t-conclusion">${summary(t)}</p><div class="row tight" style="margin-top:12px">${facts}</div></section>
    ${opt ? roundsPanel(t) : ""}
    ${opponentsPanel(t)}
    <section class="section" aria-labelledby="t-decks-h"><div class="section-head"><h2 id="t-decks-h">${decks.length === 1 ? "The tested list" : `The ${decks.length} tested lists`}</h2>${decks.length > 1 ? `<span class="small muted">best first · open a list for its matchups and cards</span>` : ""}</div>
      ${decks.map((d, i) => deckSection(d, t, i, decks.length <= 3 || i === 0)).join("")}</section>
    <details class="panel t-raw"><summary><b>Commands and raw spec</b> <span class="small muted">for rerunning this test</span></summary>
      <div class="stack" style="margin-top:12px">
        ${commands.length ? `<div class="stack tight"><h3>Exact commands</h3><div class="log">${commands.map(esc).join("\n")}</div></div>` : ""}
        <div class="stack tight"><h3>Spec (lab/specs format)</h3><div class="log">${esc(JSON.stringify(t.spec || {}, null, 1))}</div></div>
        <div class="row tight small muted"><span>bot: <span class="mono">${esc(t.agent)}</span></span>${t.opp_agent && t.opp_agent !== t.agent ? `<span>opponent bot: <span class="mono">${esc(t.opp_agent)}</span></span>` : ""}<span>test id <span class="mono">${esc(t.id)}</span></span></div>
      </div></details>
  </div>`;
  enhance(container);
}

// ── module contract ─────────────────────────────────────────────────────────
let lastId;
export async function render(container, params = {}) {
  await cssOnce(CSS); lastId = params.id || "";
  if (params.id) renderDetail(container, params.id); else renderList(container);
}
export function update(container, params = {}) {
  const id = params.id || "";
  if (id !== lastId) window.scrollTo(0, 0);
  return render(container, params);
}
export function destroy() {}
