#!/usr/bin/env node
// Smoke test for the WebAssembly engine (build-wasm/riftbound.js).
//
//   node tools/wasm_test.js [build-wasm/riftbound.js]
//
// Game 1: Riven vs Kennen, bot vs bot (human_seat = 0) with the value net
// loaded, rb_bot_move() until terminal. Game 2: human_seat = 1, the script
// answers every human decision with option 0 via rb_apply(). Prints turn
// counts, winners, wasm size and ms per bot move. Exits non-zero on any error.
'use strict';
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const jsPath = path.resolve(process.argv[2] || path.join(root, 'build-wasm', 'riftbound.js'));
const wasmPath = jsPath.replace(/\.js$/, '.wasm');
const deck1 = fs.readFileSync(path.join(root, 'decks', 'riven_sacramento.txt'), 'utf8');
const deck2 = fs.readFileSync(
    path.join(root, 'lab', 'decks', 'meta', 'rro25_2026-10-17', '01_simple_kennen.txt'), 'utf8');
const netPath = path.join(root, 'lab', 'learn', 'nets', 'net_000.rbvn');

function fail(msg) { console.error('FAIL: ' + msg); process.exit(1); }

(async () => {
  const createRiftbound = require(jsPath);
  // Node's fetch() rejects plain file paths, so hand the wasm bytes over.
  const Module = await createRiftbound({ wasmBinary: fs.readFileSync(wasmPath) });
  const api = {
    version: Module.cwrap('rb_version', 'string', []),
    lastError: Module.cwrap('rb_last_error', 'string', []),
    cardCount: Module.cwrap('rb_card_count', 'number', []),
    newGame: Module.cwrap('rb_new_game', 'number', ['string', 'string', 'number', 'number', 'string']),
    isTerminal: Module.cwrap('rb_is_terminal', 'number', []),
    decisionPlayer: Module.cwrap('rb_decision_player', 'number', []),
    stateJson: Module.cwrap('rb_state_json', 'string', ['number']),
    apply: Module.cwrap('rb_apply', 'number', ['number']),
    botMove: Module.cwrap('rb_bot_move', 'number', []),
    searchJson: Module.cwrap('rb_bot_last_search_json', 'string', []),
    resultJson: Module.cwrap('rb_result_json', 'string', []),
    loadNet: Module.cwrap('rb_load_value_net', 'number', ['number', 'number']),
    endGame: Module.cwrap('rb_end_game', null, []),
  };

  console.log('engine ' + api.version() + ', cards: ' + api.cardCount());
  console.log('wasm size: ' + (fs.statSync(wasmPath).size / 1024 / 1024).toFixed(2) + ' MB, js: ' +
              (fs.statSync(jsPath).size / 1024).toFixed(0) + ' KB');

  if (fs.existsSync(netPath)) {
    const bytes = fs.readFileSync(netPath);
    const ptr = Module._malloc(bytes.length);
    Module.HEAPU8.set(bytes, ptr);
    const rc = api.loadNet(ptr, bytes.length);
    Module._free(ptr);
    if (rc !== 0) fail('rb_load_value_net: ' + api.lastError());
    console.log('value net loaded (' + bytes.length + ' bytes)');
  } else {
    console.log('no value net at ' + netPath + ' — heuristic evaluation');
  }

  function playGame(label, humanSeat, seed, botSpec) {
    const rc = api.newGame(deck1, deck2, humanSeat, seed, botSpec);
    if (rc !== 0) fail('rb_new_game: ' + api.lastError());
    let botMoves = 0, botMs = 0, humanMoves = 0, maxMs = 0, searched = 0, replayed = 0;
    let guard = 0;
    while (!api.isTerminal()) {
      if (++guard > 5000) fail(label + ': no terminal after 5000 moves');
      const who = api.decisionPlayer();
      if (who === 0) fail(label + ': no decision player while not terminal');
      if (who === humanSeat) {
        const st = JSON.parse(api.stateJson(humanSeat));
        if (st.decision_player !== humanSeat) fail(label + ': decision_player mismatch');
        if (!Array.isArray(st.legal) || st.legal.length === 0) fail(label + ': no legal options');
        // The opponent's hand must be masked for the human's perspective.
        const oppHand = st.board.players[humanSeat === 1 ? 1 : 0].hand;
        if (oppHand.some((h) => h !== null)) fail(label + ': opponent hand leaked');
        const r = api.apply(0);
        if (r !== 0) fail(label + ': rb_apply: ' + api.lastError());
        humanMoves++;
      } else {
        const t0 = process.hrtime.bigint();
        const idx = api.botMove();
        const ms = Number(process.hrtime.bigint() - t0) / 1e6;
        if (idx < 0) fail(label + ': rb_bot_move: ' + api.lastError());
        const search = JSON.parse(api.searchJson());
        if (search && search.options && search.options.length > 1) { searched++; if (search.replayed) replayed++; }
        botMoves++;
        botMs += ms;
        if (ms > maxMs) maxMs = ms;
      }
    }
    const res = JSON.parse(api.resultJson());
    const st = JSON.parse(api.stateJson(0));
    console.log(`${label}: winner P${res.winner} scores ${res.scores.join('-')} turns ${res.turns} ` +
                `decisions ${res.decisions} (${res.reason})`);
    console.log(`  bot moves ${botMoves} (searched ${searched}, via replay ${replayed}), ` +
                `avg ${(botMs / Math.max(1, botMoves)).toFixed(1)} ms, max ${maxMs.toFixed(0)} ms` +
                (humanMoves ? `, human moves ${humanMoves}` : '') +
                `, state json ${(st && JSON.stringify(st).length / 1024).toFixed(1)} KB`);
    if (!res.terminal) fail(label + ': result not terminal');
    api.endGame();
    return res;
  }

  playGame('game 1 (bot vs bot)', 0, 12345, 'greedy');
  playGame('game 2 (human seat 1 answers option 0)', 1, 777, 'greedy');
  console.log('OK');
})().catch((e) => { console.error(e && e.stack || e); process.exit(1); });
