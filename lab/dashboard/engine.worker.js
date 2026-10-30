/* Web Worker around the WebAssembly engine (engine/riftbound.js + riftbound.wasm).
   The page posts {id, cmd, ...} and gets {id, ok:true, result} or {id, ok:false, error};
   while downloading it also gets {type:"progress", what:"wasm"|"net", loaded, total}.

   cmd init     {base, net}            base = URL prefix of riftbound.js/.wasm, net = URL of the value net (optional)
                                       → {version, cards, net: true|false}
   cmd newGame  {deck1, deck2, humanSeat, seed, bot}   deck texts (Piltover Archive format), seat 1|2 (0 = bot vs bot)
                                       → state (see below)
   cmd state    {perspective?}         → state as rb_state_json sees it: the human's perspective by default,
                                         everything revealed (0) only once the game is over
   cmd apply    {index}                → state after the human's choice
   cmd botMove  {}                     → {index, text, player, search, ms, state}; runs rb_bot_move (blocking here, not on the page)
   cmd result   {}                     → rb_result_json
   cmd end      {}                     → true
   The bot's hand never leaves this worker: botMove reads the bot's own perspective only to recover the chosen option's text. */
"use strict";
let M = null, api = null, humanSeat = 0;

async function fetchBytes(url, what) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${what}: HTTP ${r.status} for ${url}`);
  const total = +r.headers.get("content-length") || 0;
  if (!r.body || !r.body.getReader) { const b = new Uint8Array(await r.arrayBuffer()); postMessage({ type: "progress", what, loaded: b.length, total: b.length }); return b; }
  const reader = r.body.getReader(), chunks = []; let loaded = 0, last = 0;
  for (;;) {
    const { done, value } = await reader.read(); if (done) break;
    chunks.push(value); loaded += value.length;
    if (loaded - last > 200000 || loaded === total) { last = loaded; postMessage({ type: "progress", what, loaded, total }); }
  }
  const out = new Uint8Array(loaded); let o = 0; for (const c of chunks) { out.set(c, o); o += c.length; }
  postMessage({ type: "progress", what, loaded, total: loaded });
  return out;
}
function parse(s) { try { return JSON.parse(s); } catch (e) { throw new Error("engine returned invalid JSON: " + String(s).slice(0, 120)); } }
function stateFor(perspective) {
  const over = api.isTerminal() !== 0;
  const p = perspective != null ? perspective : over ? 0 : humanSeat;
  // never hand the page a perspective that reveals the bot's hand during play
  const safe = over || humanSeat === 0 ? p : humanSeat;
  return parse(api.stateJson(safe));
}

const CMDS = {
  async init({ base, net }) {
    const wasm = await fetchBytes(base + "riftbound.wasm", "wasm");
    importScripts(base + "riftbound.js");
    M = await createRiftbound({ wasmBinary: wasm, locateFile: f => base + f, print: () => {}, printErr: s => console.warn("[engine] " + s) });
    const c = (n, r, a) => M.cwrap(n, r, a);
    api = { version: c("rb_version", "string", []), lastError: c("rb_last_error", "string", []), cardCount: c("rb_card_count", "number", []),
            newGame: c("rb_new_game", "number", ["string", "string", "number", "number", "string"]), isTerminal: c("rb_is_terminal", "number", []),
            decisionPlayer: c("rb_decision_player", "number", []), stateJson: c("rb_state_json", "string", ["number"]), apply: c("rb_apply", "number", ["number"]),
            botMove: c("rb_bot_move", "number", []), searchJson: c("rb_bot_last_search_json", "string", []), resultJson: c("rb_result_json", "string", []),
            loadNet: c("rb_load_value_net", "number", ["number", "number"]), endGame: c("rb_end_game", null, []) };
    let netOk = false;
    if (net) {
      try {
        let bytes = await fetchBytes(net, "net");
        if (/\.json$/i.test(net)) {           // base64 inside JSON (the hosting serves no raw binaries)
          const b64 = JSON.parse(new TextDecoder().decode(bytes)).base64, bin = atob(b64);
          bytes = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
        }
        const ptr = M._malloc(bytes.length); M.HEAPU8.set(bytes, ptr);
        const rc = api.loadNet(ptr, bytes.length); M._free(ptr);
        if (rc !== 0) console.warn("[engine] value net rejected: " + api.lastError()); else netOk = true;
      } catch (e) { console.warn("[engine] value net not loaded: " + e.message); }
    }
    return { version: api.version(), cards: api.cardCount(), net: netOk };
  },
  async newGame({ deck1, deck2, humanSeat: seat, seed, bot }) {
    if (!api) throw new Error("engine not initialised");
    humanSeat = seat | 0;
    const rc = api.newGame(deck1, deck2, humanSeat, seed >>> 0, bot || "greedy");
    if (rc !== 0) throw new Error(api.lastError() || "rb_new_game failed");
    return stateFor();
  },
  async state({ perspective } = {}) { return stateFor(perspective); },
  async apply({ index }) {
    if (api.isTerminal()) throw new Error("the game is over");
    if (api.decisionPlayer() !== humanSeat) throw new Error("not the human's decision");
    const rc = api.apply(index | 0);
    if (rc !== 0) throw new Error(api.lastError() || "illegal option " + index);
    return stateFor();
  },
  async botMove() {
    if (api.isTerminal()) throw new Error("the game is over");
    const who = api.decisionPlayer();
    if (who === humanSeat) throw new Error("it is the human's decision");
    const legal = (parse(api.stateJson(who)).legal || []);     // stays in the worker
    const t0 = performance.now();
    const index = api.botMove();
    const ms = performance.now() - t0;
    if (index < 0) throw new Error(api.lastError() || "rb_bot_move failed");
    const opt = legal.find(l => l.index === index);
    let search = null; try { search = parse(api.searchJson()); } catch (e) {}
    // name the options the search scored, except cards the bot kept in hand (hidden information)
    if (search && Array.isArray(search.options)) for (const o of search.options) {
      const l = legal.find(x => x.index === o.index);
      o.text = !l ? "option " + o.index : (/^(PlayCard|PlayReaction|HideCard)$/.test(l.type) && l.index !== index) ? "a card from hand" : l.text;
    }
    // A card the bot hides stays secret: the move reads "Hide a card at <battlefield>" and the engine's own log lines
    // naming it are masked the same way.
    const hidden = opt && opt.type === "HideCard";
    const text = !opt ? "" : hidden ? opt.text.replace(/^Hide .+? at /, "Hide a card at ").replace(/^Hide (?!a card).+$/, "Hide a card") : opt.text;
    const state = stateFor();
    if (hidden && Array.isArray(state.log)) state.log = state.log.map(l => String(l).replace(/(HIDE|HIDDEN): .+? (at BF#|hidden at BF#)/, "$1: a card $2"));
    return { index, text, type: opt ? opt.type : "", player: who, options: legal.length, search, ms, state };
  },
  async result() { return parse(api.resultJson()); },
  async end() { if (api) api.endGame(); return true; },
};

onmessage = async e => {
  const { id, cmd } = e.data || {};
  try {
    if (!CMDS[cmd]) throw new Error("unknown command " + cmd);
    postMessage({ id, ok: true, result: await CMDS[cmd](e.data) });
  } catch (err) {
    postMessage({ id, ok: false, error: (err && err.message) || String(err) });
  }
};
