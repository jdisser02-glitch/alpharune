#!/usr/bin/env node
// Bot audit: replays a game (one of the user's games from the Play page, or a
// bot-vs-bot game it plays itself) in the browser engine and checks what the
// bot did at every real decision.
//
//   node lab/tools/audit_bot.js --game rec.json [--out report.json]
//   node lab/tools/audit_bot.js --selfplay DECK_A DECK_B --seed 7 [--spec search:sims=50,worlds=8,settle=60]
//
// A game record is the Play page's: {me, opponent, seat, seed, bot_spec,
// decisions: [{chosen, who}]}. Each real decision is probed: the game is
// replayed to it and the bot searches it again with a stronger budget
// (--probe, default search:sims=400,worlds=8,settle=60). Flags:
//   second_opinion  the stronger search clearly prefers another move (the bot's
//                   move holds < 15% of its visits and is >= 0.15 worse);
//   saturated       the net is almost sure of the result (|v| >= 0.9) while the
//                   score is close early on: every option looks the same to it;
//   incoherent      both players' evaluations of nearby positions say the same
//                   side wins or both lose (v1 + v2 far from 0);
//   losing_attack   the bot moved units into a fight that killed them all,
//                   killed nothing and conquered nothing;
//   idle_turn       the bot ended its turn with 4+ ready runes, cards it could
//                   play and no unit on the board;
//   loop            the same action chosen 12+ times in a row, or a bot-vs-bot
//                   game cut at the decision cap: an action that changes
//                   nothing is being offered (an engine bug, not a bot one).
// Needs the built dashboard (lab/dashboard/build.py -> dist) for the engine,
// the decks and the nets; --dist overrides the location.
const fs = require('fs'), path = require('path'), zlib = require('zlib');

function arg(name, def) { const i = process.argv.indexOf('--' + name); return i > 0 ? process.argv[i + 1] : def; }
const DIST = arg('dist', path.join(__dirname, '..', 'dashboard', 'dist'));
const PROBE = arg('probe', 'search:sims=400,worlds=8,settle=60');
const OUT = arg('out', null);

async function loadEngine() {
  const M = await require(path.join(DIST, 'engine/riftbound.js'))({ wasmBinary: fs.readFileSync(path.join(DIST, 'engine/riftbound.wasm')), print: () => {}, printErr: () => {} });
  const c = (n, r, a) => M.cwrap(n, r, a);
  const api = {
    newGame: c('rb_new_game', 'number', ['string', 'string', 'number', 'number', 'string']),
    stateJson: c('rb_state_json', 'string', ['number']), apply: c('rb_apply', 'number', ['number']),
    botMove: c('rb_bot_move', 'number', []), searchJson: c('rb_bot_last_search_json', 'string', []),
    lastError: c('rb_last_error', 'string', []),
  };
  const load = (file, fn, gz) => {
    const p = path.join(DIST, 'engine', file); if (!fs.existsSync(p)) return null;
    const j = JSON.parse(fs.readFileSync(p, 'utf8')); let b = Buffer.from(j.base64, 'base64'); if (gz || j.encoding === 'gzip') b = zlib.gunzipSync(b);
    const ptr = M._malloc(b.length); M.HEAPU8.set(b, ptr); const r = M.cwrap(fn, 'number', ['number', 'number'])(ptr, b.length); M._free(ptr);
    return r === 0 ? j.source || file : null;
  };
  api.net = load('value_net.json', 'rb_load_value_net');
  api.pnet = M._rb_load_policy_net ? load('policy_net.json', 'rb_load_policy_net') : null;
  return api;
}

const deckText = (() => {
  let decks = null;
  return p => {
    decks = decks || JSON.parse(fs.readFileSync(path.join(DIST, 'data.json'), 'utf8')).decks;
    const d = decks.find(x => x.path === p);
    if (d) return d.text;
    const f = path.join(__dirname, '..', '..', p);
    if (fs.existsSync(f)) return fs.readFileSync(f, 'utf8');
    throw new Error('deck not found: ' + p);
  };
})();

function startGame(api, rec, spec) {
  const seat = rec.seat || 1, me = deckText(rec.me), op = deckText(rec.opponent);
  if (api.newGame(seat === 1 ? me : op, seat === 1 ? op : me, seat, rec.seed >>> 0, spec || rec.bot_spec) !== 0) throw new Error(api.lastError());
}

// Plays a bot-vs-bot game and returns it as a game record.
function selfPlay(api, deckA, deckB, seed, spec) {
  const rec = { me: deckA, opponent: deckB, seat: 1, seed, bot_spec: spec, decisions: [], selfplay: true };
  startGame(api, rec, spec);
  for (let n = 0; ; n++) {
    const st = JSON.parse(api.stateJson(0)); if (st.game_over || !st.legal.length) break;
    if (n >= 1500) { rec.capped = true; break; }
    let idx = 0;
    if (st.legal.length > 1) { api.botMove(); idx = JSON.parse(api.searchJson()).chosen; }
    else api.apply(0);
    rec.decisions.push({ chosen: idx, who: st.decision_player === 1 ? 'me' : 'bot', player: st.decision_player, text: (st.legal.find(l => l.index === idx) || {}).text });
  }
  return rec;
}

const isTrivial = (st) => st.legal.length < 2 || st.legal.every(l => / Rune$/.test(l.text) || /^Pass/.test(l.text));

function probe(api, rec, i) {
  startGame(api, rec, PROBE);
  for (let k = 0; k < i; k++) if (api.apply(rec.decisions[k].chosen) !== 0) throw new Error('replay failed at ' + k + ': ' + api.lastError());
  const st = JSON.parse(api.stateJson(0));
  api.botMove();
  const s = JSON.parse(api.searchJson());
  const opts = (s.options || []).map((o, k) => ({ index: o.index, share: o.score, value: (s.stats && s.stats.values) ? s.stats.values[k] : null }));
  const tot = opts.reduce((a, o) => a + o.share, 0) || 1;
  const root = opts.reduce((a, o) => a + (o.value || 0) * o.share, 0) / tot;
  return { st, opts, root, chosen: s.chosen };
}

async function audit(api, rec) {
  const seatOf = d => d.player || (d.who === 'me' ? rec.seat || 1 : 3 - (rec.seat || 1));
  const botSeat = 3 - (rec.seat || 1);
  const isBot = d => rec.selfplay || d.who === 'bot';
  const text = (st, idx) => (st.legal.find(l => l.index === idx) || {}).text;
  const flags = [], probes = [];
  // 1. walk the game once, collecting logs per decision and the decisions worth probing
  startGame(api, rec);
  const steps = [];
  for (let i = 0; i < rec.decisions.length; i++) {
    const st = JSON.parse(api.stateJson(0)); const d = rec.decisions[i];
    if (api.apply(d.chosen) !== 0) throw new Error('replay failed at ' + i + ': ' + api.lastError());
    const after = JSON.parse(api.stateJson(0));
    steps.push({ i, st, d, log: after.log || [], text: text(st, d.chosen), seat: seatOf(d) });
  }
  // loops: a run of the same action (probing each repeat would only cost time)
  const repeated = new Set();
  for (let k = 0, run = 1; k < steps.length; k++) {
    run = k > 0 && steps[k].text === steps[k - 1].text && steps[k].seat === steps[k - 1].seat ? run + 1 : 1;
    if (run >= 3) repeated.add(k);
    if (run === 12) flags.push({ kind: 'loop', i: steps[k].i, turn: steps[k].st.board.turn, move: steps[k].text });
  }
  if (rec.selfplay && rec.capped) flags.push({ kind: 'loop', i: steps.length - 1, turn: steps[steps.length - 1].st.board.turn, move: 'decision cap reached' });
  // 2. probes with the stronger search
  const t0 = Date.now();
  for (const [k, s] of steps.entries()) {
    if (isTrivial(s.st) || repeated.has(k)) continue;
    const p = probe(api, rec, s.i);
    const b = p.st.board, me = b.players[s.seat - 1], op = b.players[2 - s.seat];
    const row = { i: s.i, turn: b.turn, seat: s.seat, bot: isBot(s.d), move: s.text, root: +p.root.toFixed(3), score: [me.score, op.score] };
    probes.push(row);
    if (row.bot) {
      const mine = p.opts.find(o => o.index === s.d.chosen), best = p.opts.reduce((a, o) => (o.value != null && (!a || o.share > a.share) ? o : a), null);
      if (mine && best && best.index !== mine.index && mine.share < 0.15 && best.value - (mine.value || -1) >= 0.15)
        flags.push({ kind: 'second_opinion', i: s.i, turn: b.turn, move: s.text, better: text(p.st, best.index),
                     gap: +(best.value - (mine.value || -1)).toFixed(2), share: +mine.share.toFixed(2) });
      if (Math.abs(p.root) >= 0.9 && Math.abs(me.score - op.score) <= 1 && Math.max(me.score, op.score) <= 5 && b.turn <= 8)
        flags.push({ kind: 'saturated', i: s.i, turn: b.turn, move: s.text, value: row.root, score: row.score });
    }
  }
  // incoherence: a probe of each side close together (same turn, a few decisions apart)
  for (let k = 0; k < probes.length; k++) for (let j = k + 1; j < probes.length && probes[j].i - probes[k].i <= 8; j++) {
    const a = probes[k], c = probes[j];
    if (a.seat === c.seat || a.turn !== c.turn) continue;
    if (Math.abs(a.root + c.root) >= 0.8) { flags.push({ kind: 'incoherent', i: c.i, turn: c.turn, values: [a.root, c.root], seats: [a.seat, c.seat] }); k = j; break; }
  }
  // 3. rules on the log: losing attacks and idle turns of the bot
  for (let k = 0; k < steps.length; k++) {
    const s = steps[k]; if (!isBot(s.d)) continue;
    const P = 'P' + s.seat;
    if (/^Move .+ to (?!base)/.test(s.text || '')) {
      const moved = (s.text.match(/^Move (.+) to /) || [])[1] || '';
      let killedMine = 0, killedTheirs = 0, conquered = false, fought = false, names = moved.split(/, /);
      for (let j = k; j < Math.min(steps.length, k + 12); j++) {
        for (const l of steps[j].log) {
          if (l.includes(`COMBAT:`) && l.includes(`attacker=${P}`)) fought = true;
          const m = l.match(/KILL: (.+?) \(id=/); if (m) { if (names.includes(m[1])) killedMine++; else killedTheirs++; }
          if (l.includes(`CONQUER: ${P} `)) conquered = true;
        }
        if (j > k && /^(Move|End turn)/.test(steps[j].text || '') && isBot(steps[j].d)) break;
      }
      const b = s.st.board, opScore = b.players[2 - s.seat].score;
      if (fought && killedMine >= names.length && killedTheirs === 0 && !conquered)
        flags.push({ kind: 'losing_attack', i: s.i, turn: b.turn, move: s.text, desperate: opScore >= 6 });
    }
    if (s.text === 'End turn') {
      const me = s.st.board.players[s.seat - 1];
      const onBoard = (me.base_units || []).length + s.st.board.battlefields.reduce((a, bf) => a + ((bf['p' + s.seat] || []).length), 0);
      const plays = s.st.legal.filter(l => /^Play /.test(l.text)).length;
      if ((me.runes_ready || 0) >= 4 && plays > 0 && onBoard === 0)
        flags.push({ kind: 'idle_turn', i: s.i, turn: s.st.board.turn, ready_runes: me.runes_ready, playable: plays });
    }
  }
  flags.sort((a, b) => a.i - b.i);
  return { game: rec.id || null, me: rec.me, opponent: rec.opponent, bot_spec: rec.bot_spec, probe: PROBE, net: api.pnet || api.net,
           result: rec.result || null, decisions: rec.decisions.length, probed: probes.length, minutes: +((Date.now() - t0) / 60000).toFixed(1),
           flags, probes, summary: summarize(flags, probes, rec) };
}

const LABEL = { loop: 'boucle (bug moteur)', second_opinion: 'meilleur coup ignoré', saturated: 'évaluation saturée', incoherent: 'évaluations incohérentes', losing_attack: 'attaque perdante', idle_turn: 'tour passé sans jouer' };
function summarize(flags, probes, rec) {
  const count = {}; for (const f of flags) count[f.kind] = (count[f.kind] || 0) + 1;
  const sat = probes.filter(p => p.bot && Math.abs(p.root) >= 0.9).length, botProbes = probes.filter(p => p.bot).length;
  const ordered = [...flags.filter(f => f.kind === "loop"), ...flags.filter(f => f.kind !== "loop")];
  const lines = ordered.slice(0, 12).map(f => {
    const where = `T${f.turn} #${f.i}`;
    if (f.kind === 'second_opinion') return `${where} ${LABEL[f.kind]} : « ${f.move} » au lieu de « ${f.better} » (écart ${f.gap})`;
    if (f.kind === 'saturated') return `${where} ${LABEL[f.kind]} : ${f.value} à ${f.score.join('-')} (« ${f.move} »)`;
    if (f.kind === 'incoherent') return `${where} ${LABEL[f.kind]} : P${f.seats[0]} ${f.values[0]}, P${f.seats[1]} ${f.values[1]}`;
    if (f.kind === 'loop') return `${where} ${LABEL[f.kind]} : « ${f.move} » répété`;
    if (f.kind === 'losing_attack') return `${where} ${LABEL[f.kind]} : « ${f.move} »${f.desperate ? ' (désespérée, adversaire à 6+)' : ''}`;
    return `${where} ${LABEL[f.kind]} : ${f.ready_runes} runes prêtes, ${f.playable} cartes jouables`;
  });
  return { counts: count, saturated_share: botProbes ? +(sat / botProbes).toFixed(2) : 0, lines };
}

(async () => {
  const api = await loadEngine();
  let rec;
  if (arg('game')) { rec = JSON.parse(fs.readFileSync(arg('game'), 'utf8')); rec = rec.data || rec; }
  else if (process.argv.includes('--selfplay')) {
    const i = process.argv.indexOf('--selfplay');
    rec = selfPlay(api, process.argv[i + 1], process.argv[i + 2], +arg('seed', 1), arg('spec', 'search:sims=50,worlds=8,settle=60'));
  } else { console.error('usage: audit_bot.js --game rec.json | --selfplay DECK_A DECK_B [--seed N]'); process.exit(2); }
  const report = await audit(api, rec);
  if (rec.selfplay) report.selfplay_record = rec;
  const txt = JSON.stringify(report, null, 1);
  if (OUT) fs.writeFileSync(OUT, txt);
  console.log(JSON.stringify({ flags: report.summary.counts, saturated_share: report.summary.saturated_share, probed: report.probed, minutes: report.minutes }));
  for (const l of report.summary.lines) console.log('  ' + l);
})().catch(e => { console.error('FAIL', e.stack || e); process.exit(1); });
