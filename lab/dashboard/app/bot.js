/* Riven Lab — Bot: how the bot learns, the self-play ladder (chart + table), the network in use and how often the
   bot agrees with human players. Everything comes from data.json (`ladder`, `engine`, `tests`, `human_review`).
   Router contract: render(container, params, ctx) / destroy(). No params. Styles: app/bot.css (prefix .v-bot). */
import { data, esc, el, pct, pct0, num, interval, fmtDate, fmtDateTime, ago, plural, wilson, kpi, badge, chip, pageHead, empty,
         rate, table, enhance, portrait, legendFace, legendShort, $ } from "./lib.js";

const CSS = new URL("./bot.css", import.meta.url).href;
function css() { if (!document.querySelector(`link[href="${CSS}"]`)) document.head.append(el("link", { rel: "stylesheet", href: CSS })); }

const netName = p => String(p || "").split("/").pop().replace(/\.rbvn$/, "") || "—";
const usesNet = (agent, net) => !!(net && String(agent || "").includes(net));

export function render(container) {
  css();
  const D = data(), ladder = D.ladder || {}, it = [...(ladder.iterations || [])].sort((a, b) => a.iteration - b.iteration);
  const net = ladder.current_net || "";
  const current = it.find(r => r.net === net) || it.filter(r => r.accepted).slice(-1)[0] || null;
  const latest = it.slice(-1)[0] || null;
  const tests = (D.tests || []).filter(t => usesNet(t.agent, net) || usesNet(t.opp_agent, net));
  const playUsesIt = !!(D.engine && D.engine.net && (!net || D.engine.net_source === net));
  const hr = D.human_review || {}, hg = hr.games || [];
  const compared = hg.reduce((a, g) => a + (g.decisions_compared || 0), 0);
  const agreed = hg.reduce((a, g) => a + (g.decisions_compared || 0) * (g.agreement_pct || 0) / 100, 0);
  const cleanN = hg.reduce((a, g) => a + (g.compared_clean || 0), 0);
  const cleanAgreed = hg.reduce((a, g) => a + (g.compared_clean || 0) * (g.agreement_clean_pct || 0) / 100, 0);
  const vsRand = current && current.vs_random, vsRandCi = vsRand ? wilson(vsRand.wins, vsRand.games) : null;

  const kpis = [
    kpi({ label: "Network in use", value: current ? `Iteration ${current.iteration}` : "—", tone: "info",
          sub: current ? `${esc(netName(current.net))}, adopted ${esc(fmtDate(current.started_utc))}${latest && latest.iteration !== current.iteration ? ` · iteration ${latest.iteration} tried since and not adopted` : ""}` : "The ladder has not produced a network yet" }),
    kpi({ label: "Against a random player", value: vsRand ? pct0(vsRand.rate) : "—", tone: vsRand ? (vsRand.rate >= 0.75 ? "win" : "warn") : "neutral",
          sub: vsRand ? `${vsRand.wins} of ${vsRand.games} games, interval ${interval(vsRandCi)}` : "No measurement yet" }),
    kpi({ label: "Iterations", value: it.length, tone: "info",
          sub: it.length ? `${it.filter(r => r.accepted).length} adopted, ${it.filter(r => !r.accepted).length} rejected · ${num(it.reduce((a, r) => a + (r.minutes || 0), 0))} minutes of training in all` : "Training has not started" }),
    kpi({ label: "Agreement with humans", value: compared ? pct0(agreed / compared) : "—", tone: compared ? (agreed / compared >= 0.5 ? "win" : "warn") : "neutral",
          sub: compared ? `${num(compared)} decisions compared in ${plural(hg.length, "real game")}` : "No human game compared yet", href: "#/replays?test=human" }),
  ].join("");

  container.innerHTML = `
    ${pageHead({ title: "The bot", sub: "The player that pilots both sides of every simulated game. It gets stronger by playing itself; this page shows how far it has come and how far it has to go." })}
    <div class="kpis">${kpis}</div>

    <div class="v-bot grid2 v-bot-top">
      <section class="panel stack" aria-labelledby="bot-how">
        <h2 id="bot-how">How the bot learns</h2>
        <p>The bot learns in a loop of three steps. <b>Self-play</b>: the current bot plays a few hundred games against itself, searching a handful of continuations at every decision and redealing what it cannot see (your hand, the deck order) on each try. <b>Train</b>: every position from those games, labelled with who went on to win, teaches a small neural network to guess the winner from a board. <b>Gate</b>: the bot with the new network plays the bot with the old one; the new one is adopted only if it wins the match, otherwise it is thrown away and the loop starts again from the old network. The network in use is the last one that passed the gate.</p>
        <dl class="v-bot-terms">
          <div><dt>Vs previous bot</dt><dd>The gate match: the share of games the new network won against the network in use at the time, with the 95% interval, the range the true rate most likely sits in. Above 50% and the new one is adopted.</dd></div>
          <div><dt>Vs random</dt><dd>The same new bot against a player that picks moves at random. A yardstick of raw strength: a good bot should be near 100%.</dd></div>
          <div><dt>Val loss, val accuracy</dt><dd>How well the network predicts the winner on games it did not train on. Loss falls as it learns (0.69 means it is guessing); accuracy is the share of positions it calls right.</dd></div>
          <div><dt>Positions</dt><dd>How many board states the network trained on in that iteration. Minutes is the wall-clock time of the whole iteration on the lab machine.</dd></div>
        </dl>
        <p class="small muted">Until the bot clearly beats a random player, treat deck test results as hints rather than verdicts: both sides of every test are played by this bot.</p>
      </section>

      <section class="panel gilt stack v-bot-current" aria-labelledby="bot-current">
        <h2 id="bot-current">Current bot</h2>
        ${current ? `
          <div class="v-bot-net"><span class="v-bot-netname mono">${esc(netName(current.net))}</span>${badge("in use", "gold")}</div>
          <p>The network from iteration ${current.iteration}, trained on ${num(current.records)} positions from ${num(current.self_play_games)} self-play games and adopted on ${esc(fmtDate(current.started_utc))}. In the gate it won ${pct0(current.vs_current.rate)} against ${current.vs_current.opponent === "random" ? "a random player" : "the previous network"} (${current.vs_current.wins} of ${current.vs_current.games}, interval ${interval(current.vs_current.ci95)}).</p>
          <p>At each decision the bot searches a number of continuations (its <i>sims</i>: 20 to 100 in tests, 300 when compared with humans) and scores the positions it reaches with this network.</p>
          <h3>Where it is used</h3>
          <ul class="v-bot-uses">
            <li>${badge(playUsesIt ? "yes" : "no", playUsesIt ? "win" : "neutral")} <a href="#/play">Play tab</a>: ${playUsesIt ? "the in-browser bot evaluates with this network" : D.engine && D.engine.net ? `the in-browser bot still runs ${esc(netName(D.engine.net_source))}` : "the engine is not built for the browser in this version"}</li>
            <li>${badge(tests.length ? String(tests.length) : "none", tests.length ? "info" : "neutral")} <a href="#/tests">Tests</a>: ${tests.length ? `${plural(tests.length, "test")} played with it${tests.length <= 3 ? ` (${tests.map(t => esc(t.name)).join(", ")})` : ""}` : "no test has used it yet; new tests launched from Launch will"}</li>
            <li>${badge(usesNet(hr.agent, net) ? "yes" : "no", usesNet(hr.agent, net) ? "win" : "neutral")} <a href="#/replays?test=human">Human games</a>: ${hr.agent ? `compared with ${esc(hr.agent.replace(/,eval=net:.*/, "").replace("ismcts:sims=", "a bot searching ")) } continuations per move, network ${esc(netName((hr.agent.match(/eval=net:(\S+)/) || [])[1]))}` : "no comparison yet"}</li>
          </ul>
          ${latest && latest.iteration !== current.iteration ? `<p class="small muted">Iteration ${latest.iteration} was tried ${esc(ago(latest.started_utc))} and rejected: it won ${pct0(latest.vs_current.rate)} of its gate (interval ${interval(latest.vs_current.ci95)}).</p>` : ""}`
        : empty("No network yet", "The first ladder iteration produces one; see lab/learn in the repository.")}
      </section>
    </div>

    <section class="v-bot panel stack" aria-labelledby="bot-ladder">
      <div class="panel-head"><h2 id="bot-ladder">The ladder</h2><span class="small muted">${it.length ? `${plural(it.length, "iteration")}, ${esc(fmtDate(it[0].started_utc))} to ${esc(fmtDate(latest.started_utc))}` : ""}</span></div>
      ${it.length ? `<p class="small muted">Each point is one iteration. Filled points were adopted, hollow ones rejected. The band is the 95% interval of the gate match; the dashed line is 50%, even with the previous bot. Hover a column for the numbers.</p>${chart(it)}` : empty("No iteration yet", "Training has not produced a row in the ladder.")}
    </section>

    <section class="v-bot panel" aria-labelledby="bot-table">
      <div class="panel-head"><h2 id="bot-table">Iterations</h2><span class="small muted">click a header to sort</span></div>
      ${iterationTable(it)}
    </section>

    <section class="v-bot panel stack" aria-labelledby="bot-human">
      <div class="panel-head"><h2 id="bot-human">Does it play like a person?</h2><a class="more" href="#/replays?test=human">Human games in Replays →</a></div>
      ${hg.length ? `
        <p>Real games from RiftLite are replayed through the engine. At each decision of the captured player the bot is asked what it would do; agreement is how often its first choice is the move the human made. Across ${plural(hg.length, "game")} the bot agrees <b>${pct0(agreed / compared)}</b> of the time (${num(Math.round(agreed))} of ${num(compared)} decisions, interval ${interval(wilson(agreed, compared))}).${cleanN ? ` Counting only decisions where the engine's state was verified against the log just before, it agrees ${pct0(cleanAgreed / cleanN)} (${num(cleanN)} decisions).` : ""} With 5 to 20 legal moves a random player agrees around 10 to 20%, so a figure near that means the bot has no opinion yet.</p>
        <div class="row tight">${chip(`bot: <b>${esc(hr.agent ? hr.agent.replace(/,eval=net:.*/, "").replace("ismcts:sims=", "") + " continuations per move" : "—")}</b>`)}${chip(`${num(hg.reduce((a, g) => a + (g.desyncs || 0), 0))} desyncs, points where the engine could not follow the log`)}${hr.generated ? chip(`compared ${esc(ago(hr.generated))}`) : ""}</div>
        ${humanTable(hg)}`
      : empty("No human game compared yet", "Captured RiftLite games are replayed through the engine by lab/tools/human_review.py; the results appear here and in <a href=\"#/replays\">Replays</a>.")}
    </section>`;
  enhance(container);
  wireChart(container);
}

export function destroy() { const t = $(".v-bot-tip"); if (t) t.remove(); }

/* ── the ladder chart ───────────────────────────────────────────────────── */
function chart(it) {
  const W = 680, H = 250, L = 46, R = 44, T = 18, B = 40, n = it.length;
  const x = i => L + (n === 1 ? (W - L - R) / 2 : i * (W - L - R) / (n - 1));
  const y = v => T + (H - T - B) * (1 - v);
  const path = key => it.map((r, i) => { const v = key(r); return v == null ? "" : `${i ? "L" : "M"}${x(i).toFixed(1)},${y(v).toFixed(1)}`; }).join(" ");
  const band = it.map((r, i) => `${x(i).toFixed(1)},${y(r.vs_current && r.vs_current.ci95 ? r.vs_current.ci95[1] : r.vs_current.rate).toFixed(1)}`).join(" ")
    + " " + [...it].reverse().map((r, k) => { const i = n - 1 - k; return `${x(i).toFixed(1)},${y(r.vs_current && r.vs_current.ci95 ? r.vs_current.ci95[0] : r.vs_current.rate).toFixed(1)}`; }).join(" ");
  const marker = (cx, cy, color, filled, square) => square
    ? `<rect x="${(cx - 4.5).toFixed(1)}" y="${(cy - 4.5).toFixed(1)}" width="9" height="9" transform="rotate(45 ${cx.toFixed(1)} ${cy.toFixed(1)})" fill="${filled ? color : "var(--s1)"}" stroke="${color}" stroke-width="2"/>`
    : `<circle cx="${cx.toFixed(1)}" cy="${cy.toFixed(1)}" r="4.5" fill="${filled ? color : "var(--s1)"}" stroke="${color}" stroke-width="2"/>`;
  let s = `<svg class="v-bot-svg" viewBox="0 0 ${W} ${H}" width="100%" role="img" aria-label="Win rate per iteration against a random player and against the previous bot, adopted iterations filled, rejected hollow.">`;
  [0, .25, .5, .75, 1].forEach(v => { s += `<line x1="${L}" x2="${W - R}" y1="${y(v)}" y2="${y(v)}" stroke="var(--line)"${v === .5 ? ` stroke-dasharray="4 4" style="stroke:var(--line-strong)"` : ""}/><text x="${L - 8}" y="${y(v) + 4}" text-anchor="end">${v * 100}%</text>`; });
  s += `<polygon points="${band}" fill="var(--info)" opacity=".12"/>`;
  s += `<path d="${path(r => r.vs_current && r.vs_current.rate)}" fill="none" stroke="var(--info)" stroke-width="2" stroke-linejoin="round"/>`;
  s += `<path d="${path(r => r.vs_random && r.vs_random.rate)}" fill="none" stroke="var(--gold)" stroke-width="2" stroke-linejoin="round"/>`;
  let hits = "";
  it.forEach((r, i) => {
    const cx = x(i), col = n === 1 ? W - L - R : (W - L - R) / Math.max(1, n - 1);
    hits += `<rect class="v-bot-hit" data-i="${i}" x="${(cx - col / 2).toFixed(1)}" y="${T}" width="${col.toFixed(1)}" height="${H - T - B}" fill="transparent"/>`;
    if (r.vs_current) s += marker(cx, y(r.vs_current.rate), "var(--info)", r.accepted, true);
    if (r.vs_random) s += marker(cx, y(r.vs_random.rate), "var(--gold)", r.accepted, false);
    s += `<text x="${cx}" y="${H - 20}" text-anchor="middle" class="v-bot-xl">${r.iteration}</text>`;
    s += `<text x="${cx}" y="${H - 6}" text-anchor="middle" class="v-bot-xs ${r.accepted ? "ok" : "no"}">${r.accepted ? "adopted" : "rejected"}</text>`;
  });
  s += hits + `</svg>`;   // hit columns last so they sit above the markers
  return `<div class="v-bot-chart">${s}
    <div class="v-bot-legend" aria-hidden="true">
      <span><i class="dot gold"></i>vs random player</span>
      <span><i class="dot info square"></i>vs previous bot (gate)</span>
      <span><i class="band"></i>95% interval of the gate</span>
      <span><i class="dash"></i>50%: even with the previous bot</span>
      <span><i class="dot gold"></i>adopted &nbsp;<i class="dot gold hollow"></i>rejected</span>
    </div></div>`;
}
function wireChart(root) {
  const host = $(".v-bot-chart", root); if (!host) return;
  const it = [...(data().ladder.iterations || [])].sort((a, b) => a.iteration - b.iteration);
  let tip = $(".v-bot-tip"); if (!tip) { tip = el("div", { className: "v-bot-tip", role: "tooltip" }); document.body.append(tip); }
  const show = (i, x, yy) => {
    const r = it[i]; if (!r) return;
    const vr = r.vs_random, vc = r.vs_current;
    tip.innerHTML = `<b>Iteration ${r.iteration}</b> · ${esc(fmtDate(r.started_utc))} · ${badge(r.accepted ? "adopted" : "rejected", r.accepted ? "win" : "loss")}
      <div>vs random: <span class="mono">${vr ? `${pct0(vr.rate)} (${vr.wins}/${vr.games}, ${interval(wilson(vr.wins, vr.games))})` : "—"}</span></div>
      <div>vs ${vc && vc.opponent === "random" ? "random (first gate)" : "previous bot"}: <span class="mono">${vc ? `${pct0(vc.rate)} (${vc.wins}/${vc.games}, ${interval(vc.ci95)})` : "—"}</span></div>
      <div class="muted">${num(r.self_play_games)} self-play games · ${num(r.records)} positions · val acc ${pct0(r.val_acc)} · ${num(r.minutes)} min</div>`;
    tip.classList.add("on");
    const w = tip.offsetWidth || 260, h = tip.offsetHeight || 90;
    let left = x + 16, top = yy - h - 12; if (left + w > innerWidth - 8) left = x - w - 16; if (left < 8) left = 8; if (top < 8) top = yy + 16;
    tip.style.left = left + "px"; tip.style.top = top + "px";
  };
  host.addEventListener("mousemove", e => { const h = e.target.closest(".v-bot-hit"); if (h) show(+h.dataset.i, e.clientX, e.clientY); else tip.classList.remove("on"); });
  host.addEventListener("mouseleave", () => tip.classList.remove("on"));
  host.addEventListener("click", e => { const h = e.target.closest(".v-bot-hit"); if (h) { show(+h.dataset.i, e.clientX, e.clientY); setTimeout(() => tip.classList.remove("on"), 3000); } });
}

/* ── tables ─────────────────────────────────────────────────────────────── */
function iterationTable(it) {
  const cols = [
    { key: "iteration", label: "#", num: true, sort: true, render: r => `<span class="mono">${r.iteration}</span>` },
    { key: "started_utc", label: "Date", sort: true, render: r => `<span class="mono" title="${esc(fmtDateTime(r.started_utc))}">${esc(fmtDate(r.started_utc))}</span>` },
    { key: "self_play_games", label: "Self-play games", num: true, sort: true, render: r => `${num(r.self_play_games)}<span class="sub">${r.player === "random" ? "random players" : `${r.sims} sims, ${esc(netName((String(r.player).match(/eval=net:(\S+)/) || [])[1]))}`}</span>` },
    { key: "records", label: "Positions", num: true, sort: true, render: r => num(r.records) },
    { key: "val_loss", label: "Val loss", num: true, sort: true, render: r => r.val_loss == null ? "—" : num(r.val_loss, 3) },
    { key: "val_acc", label: "Val acc", num: true, sort: true, render: r => pct(r.val_acc) },
    { key: "vs_current", label: "Vs previous bot", sort: r => r.vs_current ? r.vs_current.rate : -1, num: true, cls: "v-bot-rate", render: r => r.vs_current ? `${rate(r.vs_current.rate, r.vs_current.ci95, 90)}<span class="sub">${r.vs_current.wins}/${r.vs_current.games}${r.vs_current.opponent === "random" ? ", vs random (first gate)" : ""}</span>` : "—" },
    { key: "vs_random", label: "Vs random", sort: r => r.vs_random ? r.vs_random.rate : -1, num: true, cls: "v-bot-rate", render: r => r.vs_random ? `${rate(r.vs_random.rate, wilson(r.vs_random.wins, r.vs_random.games), 90)}<span class="sub">${r.vs_random.wins}/${r.vs_random.games}</span>` : "—" },
    { key: "accepted", label: "Adopted", sort: r => r.accepted ? 1 : 0, num: true, render: r => badge(r.accepted ? "yes" : "no", r.accepted ? "win" : "loss") },
    { key: "minutes", label: "Minutes", num: true, sort: true, render: r => num(r.minutes) },
  ];
  return table(cols, [...it].reverse(), { sort: { key: "iteration", dir: "desc" }, empty: "No iteration yet." });
}
function humanTable(hg) {
  const rows = [...hg].sort((a, b) => String(b.date).localeCompare(String(a.date)) || (a.game || 0) - (b.game || 0));
  const cols = [
    { key: "players", label: "Game", sort: r => `${r.date} ${r.game}`, render: r => {
        const L = r.legends || {}, cap = r.capture_seat === 2 ? 1 : 0, names = r.players || ["?", "?"];
        return `<a class="v-bot-game" href="#/replays/${encodeURIComponent(r.replay)}"><span class="versus">${portrait(legendFace(L.P1) || L.P1, { size: "sm" })}<span class="vs">vs</span>${portrait(legendFace(L.P2) || L.P2, { size: "sm" })}</span><span><span class="lead">${esc(names[0])} vs ${esc(names[1])}</span><span class="sub">${esc(legendShort(L.P1))} vs ${esc(legendShort(L.P2))} · captured by ${esc(names[cap])}${r.game > 1 ? ` · game ${r.game}` : ""}</span></span></a>`; } },
    { key: "date", label: "Date", sort: true, render: r => `<span class="mono">${esc(fmtDate(r.date))}</span>` },
    { key: "decisions_compared", label: "Compared", num: true, sort: true, render: r => `${num(r.decisions_compared)}<span class="sub">of ${num(r.decisions)} decisions</span>` },
    { key: "agreement_pct", label: "Bot agrees", num: true, sort: true, cls: "v-bot-rate", render: r => r.decisions_compared ? rate(r.agreement_pct / 100, wilson(Math.round(r.decisions_compared * r.agreement_pct / 100), r.decisions_compared), 90) : "—" },
    { key: "agreement_clean_pct", label: "Clean", num: true, sort: true, render: r => r.agreement_clean_pct == null ? "—" : `${pct0(r.agreement_clean_pct / 100)}<span class="sub">${num(r.compared_clean)} decisions</span>` },
    { key: "desyncs", label: "Desyncs", num: true, sort: true, render: r => num(r.desyncs) },
  ];
  return table(cols, rows, { empty: "No game compared yet." });
}
