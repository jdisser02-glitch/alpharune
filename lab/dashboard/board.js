/* RiftBoard — draws one Riftbound game state like the physical table.
   window.RiftBoard = { mount(el, opts), render(frame, opts), destroy(), cardImage(url, w), situation(frame), parseOption(text), concerns(entry, sit) }

   frame: one replay frame from the engine ({board:{players, battlefields, chain, phase, turn, turn_player}, ...});
          an optional frame.legal = [{index, text, type?, card?, battlefield?}] highlights playable cards / destinations
          and renders a clickable list (opts.onPick(index, entry)); entries without card/battlefield are read from their text;
          an optional frame.options = ["Move X to Y", …] (a replay's recorded options) tells where the decision is, without a list;
          an optional frame.focus = [{card?, battlefield?}] marks cards / battlefields with a soft ring (the move being
          looked at in a replay) — no list, no click, independent of frame.legal;
          optional frame.decision_player (1 | 2), frame.passes ([1, 2]: who passed focus since the last play in the showdown) and
          frame.log (engine lines: "SHOWDOWN: BF#0 …", "COMBAT: BF#0 attacker=P1 …") refine the combat panel.
   opts:  cards      array (or Map) of card records {id, name, image_url, type, energy, power, might, domains, super, tags, text}
          art        the sprite map (art/art.json, data.art): cards are drawn from the bundled sheets; without it, from image_url
          viewer     1 | 2  seat drawn at the bottom (default 1)
          hands      "both" | "viewer"  whether the opponent's hand is face up (default "both")
          players    [{label, deck, runes:{calm:7, mind:5}, champion:"Riven, Broken Blade"}] per seat, cosmetic
          target     points to win (default 8)
          onPick     function(index, legalEntry)
          imageParams  true: ask the CDN for small webp renditions when a card has no sprite (default true)
          showText   battlefield rules text under its name (default true)
          animate    false disables the FLIP transition of units between frames (default true)
          banner / bannerKind   short status text in the top bar ("Bot is thinking…"), kind "me" | "bot" | "over"
          legalLabel / legalDisabled   caption of the options bar; disabled greys the buttons (while the bot moves)
   Plain DOM, no dependencies. Styles in board.css. */
(function () {
  "use strict";
  const DOMAINS = { fury: "#d4483b", calm: "#27a97e", mind: "#3f86e0", body: "#e38a2e", chaos: "#8e5cf0", order: "#e1b83a" };
  const esc = s => String(s ?? "").replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  const S = { el: null, opts: {}, frame: null, byId: new Map(), byName: new Map(), kw: new Map(), zoom: null, backdrop: null, z: [], rects: new Map(), handlers: [], touch: false, raf: 0, hoverKey: null };
  /* Keywords that decide a combat (CR 807 Assault, 809 Deflect, 814 Shield, 815 Tank, 826 Backline). A card prints them as
     "[Tank]" / "[Assault 2]" at the start of a line; a grant inside a sentence ("I have [Assault]") is conditional and not shown. */
  const KW = {
    tank: { label: "Tank", note: "takes combat damage first" },
    assault: { label: "Assault", note: "+X might while attacking" },
    shield: { label: "Shield", note: "+X might while defending" },
    deflect: { label: "Deflect", note: "targeting it costs X extra power" },
    backline: { label: "Backline", note: "takes combat damage last" },
  };
  const HOLD_HINT = "Hold: 1 point for the holder at the start of each of their turns. Conquer: 1 point for taking it.";

  // ── card lookup and images ─────────────────────────────────────────────────
  function indexCards(cards) {
    S.byId = new Map(); S.byName = new Map(); S.kw = new Map();
    const list = cards instanceof Map ? [...cards.values()] : Array.isArray(cards) ? cards : Object.values(cards || {});
    for (const c of list) {
      if (!c) continue;
      const id = c.id ?? c.card_id; if (id != null && !S.byId.has(+id)) S.byId.set(+id, c);
      const n = String(c.name || "").toLowerCase(); if (n && !S.byName.has(n)) S.byName.set(n, c);
      if (c.type === "legend" || c.card_type === "legend") for (const t of c.tags || []) { const k = `${t}, ${c.name}`.toLowerCase(); if (!S.byName.has(k)) S.byName.set(k, c); }
    }
  }
  function card(ref) {
    // ref: {card:id, name} | name | id
    if (ref == null) return null;
    if (typeof ref === "object") return (ref.card != null && S.byId.get(+ref.card)) || (ref.id != null && typeof ref.id !== "number" && S.byId.get(+ref.id)) || S.byName.get(String(ref.name || "").toLowerCase()) || null;
    if (typeof ref === "number") return S.byId.get(ref) || null;
    return S.byName.get(String(ref).toLowerCase()) || null;
  }
  const nameOf = ref => typeof ref === "string" ? ref : (ref && ref.name) || (card(ref) || {}).name || (typeof ref === "number" ? "#" + ref : "Unknown");
  const typeOf = c => c ? (c.type || c.card_type || "") : "";
  const costOf = c => c ? (c.energy ?? c.energy_cost ?? null) : null;
  function cardImage(url, w) {
    if (!url) return "";
    if (S.opts.imageParams === false || !/cmsassets\.rgpub\.io/.test(url)) return url;
    return url + (url.includes("?") ? "&" : "?") + `w=${w}&fm=webp&q=78`;
  }
  const imgUrl = (c, w) => c && (c.image_url || c.image) ? cardImage(c.image_url || c.image, w) : "";
  /** The sprite cell of a card in opts.art (same layout as lib.js `sprite`): {sheet, land, style} or null. */
  function sprite(c, large) {
    const A = S.opts.art, id = c && (c.id ?? c.card_id), e = A && id != null ? A.cards[String(id)] : null; if (!e) return null;
    const cell = large ? e.l : e.t, set = (large ? "large" : "thumb") + (e.land ? "_land" : ""), sh = cell && (A.sheets[set] || [])[cell[0]]; if (!sh) return null;
    const [file, cols, rows] = sh;
    const px = cols > 1 ? +(100 * cell[1] / (cols - 1)).toFixed(4) : 0, py = rows > 1 ? +(100 * cell[2] / (rows - 1)).toFixed(4) : 0;
    const sheet = "art/" + file;
    return { sheet, land: !!e.land, style: `background-image:url('${esc(sheet)}');background-size:${cols * 100}% ${rows * 100}%;background-position:${px}% ${py}%` };
  }
  function domStyle(c) {
    const d = (c && c.domains) || []; const a = DOMAINS[d[0]] || "#4a4466", b = DOMAINS[d[1]] || a;
    return `--d1:${a};--d2:${b}`;
  }
  function fallback(c, name, might, kind) {
    const cost = /^(unit|spell|gear)$/.test(typeOf(c)) ? costOf(c) : null;
    return `<div class="rb-fb" style="${domStyle(c)}">${cost != null ? `<span class="rb-fbc">${cost}</span>` : ""}${might != null ? `<span class="rb-fbm">${might}</span>` : ""}<div class="rb-fbn">${esc(name)}</div><div class="rb-fbt">${esc(typeOf(c) || kind || "card")}</div></div>`;
  }
  /** A card's picture: a sprite cell (div.rb-pic with the sheet as background) or, without a sprite, the CDN image; "" when neither. */
  function pic(c, name, w, cls = "") {
    const sp = sprite(c, w > 300);
    if (sp) return `<div class="rb-pic ${cls} ${sp.land ? "land" : ""}" style="${sp.style}" role="img" aria-label="${esc(name)}"></div>`;
    return imgUrl(c, w) ? `<img class="rb-pic ${cls}" src="${esc(imgUrl(c, w))}" alt="${esc(name)}" loading="lazy" decoding="async">` : "";
  }
  function zoomKey(data) { S.z.push(data); return S.z.length - 1; }
  /** The combat keywords a card prints: {tank:1, assault:2, …} (cached per card id). */
  function cardKeywords(c) {
    if (!c) return {};
    const id = c.id ?? c.card_id ?? c.name; if (S.kw.has(id)) return S.kw.get(id);
    const out = {};
    for (const line of String(c.text || c.ability_text || "").split("\n")) {
      const m = /^[ \t]*((?:\[[^\]\n]+\][ \t]*)+)/.exec(line); if (!m) continue;
      for (const k of m[1].matchAll(/\[(Tank|Assault|Shield|Deflect|Backline)(?: (\d+))?\]/g)) { const key = k[1].toLowerCase(); out[key] = (out[key] || 0) + (k[2] ? +k[2] : 1); }
    }
    S.kw.set(id, out); return out;
  }
  /** A unit's keywords on the table: the card's printed combat keywords plus what the state grants (u.keywords: ["Tank", "Assault 2"]).
      → [{key, label, value, note, combat}] with the combat ones first. */
  function unitKeywords(u, c) {
    const map = Object.assign({}, cardKeywords(c)), other = [];
    for (const kw of (u && u.keywords) || []) {
      const m = /^([A-Za-z-]+)(?: (\d+))?$/.exec(String(kw).trim()); const key = m ? m[1].toLowerCase() : "";
      if (KW[key]) map[key] = Math.max(map[key] || 0, m[2] ? +m[2] : 1); else other.push(String(kw));
    }
    const out = Object.keys(KW).filter(k => map[k]).map(k => ({ key: k, label: KW[k].label, value: map[k], note: KW[k].note.replace("X", map[k]), combat: true }));
    return out.concat(other.map(k => ({ key: k.toLowerCase().replace(/[^a-z0-9]+/g, "-"), label: k, value: 1, note: "", combat: false })));
  }
  const unitKwMap = u => { const m = {}; for (const k of unitKeywords(u, card(u))) if (k.combat) m[k.key] = k.value; return m; };

  // ── legal-action helpers ───────────────────────────────────────────────────
  /** "Riven, Broken Blade, Scuttle Crab" → the card names in it (names may hold commas; known names win, else one name per comma). */
  function splitNames(s) {
    const t = String(s || "").split(", "), out = [];
    for (let i = 0; i < t.length;) { let j = t.length; while (j > i + 1 && !S.byName.has(t.slice(i, j).join(", ").toLowerCase())) j--; out.push(t.slice(i, j).join(", ")); i = j; }
    return out;
  }
  /** What an option text (the engine's describeIntent) is about: {kind, card?, units?, battlefield?}. kind: play | hide | move | use |
      bring | focus | priority | damage | mulligan | end | choose | other. The destination "base" gives no battlefield. */
  function parseOption(text, type) {
    const t = String(text || ""), e = { text: t, type: type || "", kind: "other" }; let m;
    if ((m = /^(Play|Hide) (.+?)(?: (?:to|at) (.+?))?(?: targeting .+)?(?: \(alternative cost\))?$/.exec(t))) { e.kind = m[1] === "Hide" ? "hide" : "play"; e.card = m[2]; if (m[3] && m[3] !== "base") e.battlefield = m[3]; }
    else if ((m = /^Move (.+?) to (.+)$/.exec(t))) { e.kind = "move"; e.card = m[1]; e.units = splitNames(m[1]); if (m[2] !== "base") e.battlefield = m[2]; }
    else if ((m = /^(?:Use|Equip) (.+?)(?: ability \d+)?(?: (?:on|to) .+)?$/.exec(t))) { e.kind = "use"; e.card = m[1]; }
    else if ((m = /^Bring battlefield (.+)$/.exec(t))) { e.kind = "bring"; e.card = m[1]; }
    else if (/^Pass focus$/.test(t) || type === "PassFocus") e.kind = "focus";
    else if (/^Pass$/.test(t) || type === "PassPriority") e.kind = "priority";
    else if (/^Assign damage/.test(t)) e.kind = "damage";
    else if (/^(Keep hand|Mulligan\b)/.test(t)) e.kind = "mulligan";
    else if (/^End turn$/.test(t)) e.kind = "end";
    else if (/^Choose\b/.test(t)) e.kind = "choose";
    return e;
  }
  /** The frame's options with card / battlefield / kind filled in: the legal entries (clickable) or the recorded option texts. */
  function optionsOf(frame) {
    if (Array.isArray(frame.legal) && frame.legal.length) return frame.legal.map(e => { if (e.kind) return e; const p = parseOption(e.text, e.type); for (const k of ["card", "battlefield", "index"]) if (e[k] != null) p[k] = e[k]; return p; });
    if (Array.isArray(frame.options)) return frame.options.map(t => parseOption(t));
    return [];
  }
  function legalIndex(frame) {
    const legal = Array.isArray(frame.legal) && frame.legal.length ? optionsOf(frame) : [], focus = Array.isArray(frame.focus) ? frame.focus : [];
    const L = { cards: new Map(), bfs: new Map(), fcards: new Map(), fbfs: new Map(), movers: new Set(), any: legal.length > 0 || focus.length > 0 };
    const fill = (list, cards, bfs) => {
      for (const e of list) {
        const add = (m, k) => { if (k == null) return; k = String(k).toLowerCase(); if (!m.has(k)) m.set(k, e); };
        if (e.card != null) { add(cards, e.card); if (typeof e.card === "object") { add(cards, e.card.name); add(cards, e.card.id); } }
        for (const n of e.units || []) add(cards, n);
        if (e.battlefield != null) { add(bfs, e.battlefield); if (typeof e.battlefield === "object") { add(bfs, e.battlefield.name); add(bfs, e.battlefield.index); } }
      }
    };
    fill(legal, L.cards, L.bfs); fill(focus, L.fcards, L.fbfs);
    for (const e of legal) if (e.kind === "move" || e.type === "StandardMove") for (const n of e.units || (e.card ? [e.card] : [])) L.movers.add(String(n).toLowerCase());
    return L;
  }
  const EMPTY = new Map();
  /** The same index without the clickable (legal) part: the opponent's hand only takes focus rings. */
  const focusOnly = L => L && (L.fcards.size || L.fbfs.size) ? { cards: EMPTY, bfs: EMPTY, fcards: L.fcards, fbfs: L.fbfs, movers: new Set(), any: true } : null;
  const inFocus = (L, name, c, ref) => !!(L && L.fcards.size && (L.fcards.get(String(name).toLowerCase()) || (c && L.fcards.get(String(c.id))) || (ref && typeof ref === "object" && ref.card != null && L.fcards.get(String(ref.card)))));
  const pickAttr = e => e ? ` data-pick="${e.index}"` : "";
  /** The label of a seat as the caller named it: "you", "bot", a player's name; "P2" without one. */
  function who(p) { const l = String(((S.opts.players || [])[p - 1] || {}).label || "").trim(); return !l ? "P" + p : /^(you|viewer)$/i.test(l) ? "you" : l; }
  /** The seat and its label: "P2 bot", "P1 you". */
  const seat = p => who(p) === "P" + p ? "P" + p : `P${p} ${who(p)}`;
  /** The holder's name on the ribbon: the label alone, the seat too when both seats carry the same label ("Held by P2 bot"). */
  const holder = p => { const a = who(1).toLowerCase(), b = who(2).toLowerCase(); return a === b ? seat(p) : who(p); };

  // ── the situation: where the action is ────────────────────────────────────
  const unitsAt = (bf, p) => (bf["p" + p] || []).filter(u => u.might != null && !u.facedown);   // hidden cards are not units
  const sumMight = us => us.reduce((a, u) => a + (u.might || 0), 0);
  /** Who survives `dmg` damage assigned the way the rules force it (CR 467.2.c): Tank first, Backline last, lethal in full before
      the next unit; among equals the cheapest kill first. A rough prediction from raw might. */
  function survivors(targets, dmg) {
    const order = targets.map(u => ({ u, kw: unitKwMap(u) })).sort((a, b) => (b.kw.tank ? 1 : 0) - (a.kw.tank ? 1 : 0) || (a.kw.backline ? 1 : 0) - (b.kw.backline ? 1 : 0) || ((a.u.might || 0) - (a.u.damage || 0)) - ((b.u.might || 0) - (b.u.damage || 0)));
    let left = dmg; const alive = [];
    for (const x of order) { const need = Math.max(1, (x.u.might || 0) - (x.u.damage || 0)); if (left >= need) left -= need; else alive.push(x.u); }
    return alive;
  }
  /** The battlefield the game is at: a combat (the engine's flag), a showdown (focus passes, damage assignment or a chain at a contested
      battlefield), or a contested one (units of both players, CR 463). Also the attacker (CR 466.2.c), both sides' might, a verdict
      from raw might, the units involved, and, with no such battlefield, the zone the decision concerns ("base" | "hand").
      → {index, name, mode, attacker, defender, atk:{units, might}, def:{…}, outcome, verdict, note, units:Set, kinds:Set, zone, player} */
  function situation(frame) {
    const board = frame && frame.board; if (!board) return null;
    const bfs = board.battlefields || [], chain = board.chain || [], opts = optionsOf(frame), kinds = new Set(opts.map(e => e.kind));
    const player = frame.decision_player || frame.player || 0;
    let logBf = -1, logAtk = 0;
    for (const l of Array.isArray(frame.log) ? frame.log : []) { const m = /(?:SHOWDOWN|COMBAT): BF#(\d+)(?: attacker=P([12]))?/.exec(l); if (m) { logBf = +m[1]; logAtk = m[2] ? +m[2] : 0; } }
    const both = bf => unitsAt(bf, 1).length > 0 && unitsAt(bf, 2).length > 0;
    const contested = bf => [1, 2].some(p => unitsAt(bf, p).length && bf.controller !== p);   // a unit where its controller does not hold (CR 190.3.a)
    let index = bfs.findIndex(b => b.combat), mode = index >= 0 ? "combat" : "";
    if (index < 0 && (kinds.has("focus") || kinds.has("damage") || chain.length)) {
      const cands = bfs.map((b, i) => i).filter(i => contested(bfs[i]));
      if (cands.length) { index = cands.includes(logBf) ? logBf : (cands.find(i => both(bfs[i])) ?? cands[0]); mode = "showdown"; }
    }
    if (index < 0) { index = bfs.findIndex(both); if (index >= 0) mode = "contested"; }
    const sit = { index, mode, kinds, player, zone: null, units: new Set(), name: "", attacker: 0, defender: 0, atk: { units: [], might: 0 }, def: { units: [], might: 0 }, outcome: "", verdict: "", note: "" };
    if (index < 0) {
      if (kinds.has("mulligan")) sit.zone = "hand";
      else if (opts.length && !opts.some(e => e.battlefield) && opts.some(e => e.kind === "play" || e.kind === "use" || e.kind === "hide")) sit.zone = "base";
      return sit;
    }
    const bf = bfs[index], u1 = unitsAt(bf, 1), u2 = unitsAt(bf, 2);
    sit.name = bf.name;
    sit.attacker = logBf === index && logAtk ? logAtk : bf.controller ? 3 - bf.controller : u1.length && !u2.length ? 1 : u2.length && !u1.length ? 2 : board.turn_player || 1;
    sit.defender = 3 - sit.attacker;
    sit.atk = { units: sit.attacker === 1 ? u1 : u2, might: sumMight(sit.attacker === 1 ? u1 : u2) };
    sit.def = { units: sit.defender === 1 ? u1 : u2, might: sumMight(sit.defender === 1 ? u1 : u2) };
    for (const u of [...u1, ...u2]) sit.units.add(nameOf(u).toLowerCase());
    if (sit.atk.units.length && sit.def.units.length) {
      const aLeft = survivors(sit.atk.units, sit.def.might), dLeft = survivors(sit.def.units, sit.atk.might);
      let v;
      if (aLeft.length && !dLeft.length) { sit.outcome = "attackers"; v = "attackers win on might and conquer"; }
      else if (!aLeft.length && dLeft.length) { sit.outcome = "defenders"; v = "defenders win on might and hold"; }
      else if (aLeft.length) { sit.outcome = "stand"; v = "both sides keep units, so the attackers are recalled and the defenders hold"; }
      else { sit.outcome = "wipe"; v = "both sides are wiped out and the battlefield is left empty"; }
      sit.verdict = `Attackers ${sit.atk.might} vs defenders ${sit.def.might} — ${v}.`;
    } else if (mode === "showdown" || mode === "contested") {
      sit.note = `${cap(holder(sit.attacker))} moved in. ${cap(holder(sit.defender))} may answer with Action or Reaction cards; when both pass focus, ${holder(sit.attacker)} takes the battlefield.`;
    }
    return sit;
  }
  const cap = s => s.charAt(0).toUpperCase() + s.slice(1);
  /** Does an option (a legal entry or its text) concern the situation's battlefield? Everything played during a showdown or combat does
      (only Action and Reaction cards are legal then, CR 308.1.a); otherwise moves and plays to it, and abilities of units there. */
  function concerns(entry, sit) {
    if (!sit || sit.index < 0) return false;
    const e = entry && entry.kind ? entry : parseOption(typeof entry === "string" ? entry : entry && entry.text, entry && entry.type);
    if (e.kind === "focus" || e.kind === "damage") return true;
    if (e.battlefield && String(e.battlefield).toLowerCase() === String(sit.name).toLowerCase()) return true;
    if (e.card && sit.units.has(String(e.card).toLowerCase())) return true;
    if ((e.units || []).some(n => sit.units.has(String(n).toLowerCase()))) return true;
    if (sit.mode === "combat" || sit.mode === "showdown") return e.kind === "play" || e.kind === "use" || e.kind === "priority" || e.kind === "hide";
    return false;
  }

  // ── element builders ───────────────────────────────────────────────────────
  function cardEl(ref, p, cls, L, extraState) {
    const c = card(ref), name = nameOf(ref);
    const facedown = ref && typeof ref === "object" && ref.facedown;
    if (facedown) return `<div class="rb-back" role="img" aria-label="face-down card"></div>`;
    const legal = L && (L.cards.get(name.toLowerCase()) || (c && L.cards.get(String(c.id))) || (ref && ref.card != null && L.cards.get(String(ref.card))) || (ref && typeof ref === "object" && ref.id != null && L.cards.get(String(ref.id))));
    const k = zoomKey({ card: c, name, state: extraState || (typeOf(c) ? typeOf(c) + " in hand" : "in hand") });
    const cost = /^(unit|spell|gear)$/.test(typeOf(c)) ? costOf(c) : null, pw = c && (c.power ?? c.power_cost);
    const badge = cost != null && cls !== "legend" ? `<span class="rb-cost" aria-hidden="true">${cost}${pw ? `<i>${"•".repeat(Math.min(pw, 3))}</i>` : ""}</span>` : "";
    return `<div class="rb-card ${cls || ""} ${legal ? "rb-legal-hl" : ""} ${inFocus(L, name, c, ref) ? "rb-focus-hl" : ""}" data-z="${k}"${pickAttr(legal)} tabindex="0" role="img" aria-label="${esc(name)}${cost != null ? ", cost " + cost : ""}${legal ? ", playable" : ""}">${pic(c, name, 240) || fallback(c, name, c && (typeOf(c) === "unit") ? c.might : null)}${badge}</div>`;
  }
  /** A unit or gear thumb. role: "attacker" | "defender" | "" (its side in the combat at this battlefield). */
  function unitEl(u, p, L, where, isGear, role) {
    const c = card(u), name = nameOf(u);
    if (u.facedown) {   // a hidden card: its controller knows it, the other player sees a card back (the engine masks the name)
      const k = zoomKey({ card: c, name: c ? name : "Hidden card", kind: "hidden card", state: `hidden ${where}${c ? "" : " — only its controller knows which card"}` });
      return `<div class="rb-unit p${p} facedown" data-z="${k}"${u.id != null ? ` data-oid="${u.id}"` : ""} tabindex="0" role="img" aria-label="hidden card ${esc(where)}"><div class="rb-art"></div><div class="rb-name">hidden</div></div>`;
    }
    const champion = !!(c && (c.super === "champion" || c.super_type === "champion"));
    const legal = L && (L.cards.get(name.toLowerCase()) || (u.id != null && L.cards.get(String(u.id))) || (u.card != null && L.cards.get(String(u.card))));
    const canMove = !!(L && L.movers && (L.movers.has(name.toLowerCase()) || (u.id != null && L.movers.has(String(u.id)))));
    const kws = isGear ? [] : unitKeywords(u, c);
    const states = [];
    if (role) states.push(role); if (u.exhausted) states.push("exhausted"); if (u.stunned) states.push("stunned");
    if (u.damage) states.push(`${u.damage} damage`); if (u.buffs) states.push(`+${u.buffs} buff${u.buffs > 1 ? "s" : ""}`);
    if (u.gear && u.gear.length) states.push("equipped: " + u.gear.join(", "));
    for (const k of kws) if (k.combat) states.push(`${k.label}${k.value > 1 ? " " + k.value : ""}: ${k.note}`);
    if (canMove) states.push("can move now");
    const k = zoomKey({ card: c, name, kind: isGear ? "gear" : "unit token", state: `${isGear ? "gear" : champion ? "champion unit" : "unit"} ${where}${states.length ? " — " + states.join(", ") : ""}`, might: u.might });
    const tags = [];
    if (u.damage) tags.push(`<span class="rb-tag dmg">−${u.damage}</span>`);
    if (u.buffs) tags.push(`<span class="rb-tag buff">+${u.buffs}</span>`);
    if (u.stunned) tags.push(`<span class="rb-tag stun">STUN</span>`);
    const chips = kws.map(x => `<span class="rb-tag kw ${x.combat ? "cb " + x.key : ""} ${x.combat && role ? "on" : ""}" title="${esc(x.label + (x.value > 1 ? " " + x.value : "") + (x.note ? ": " + x.note : ""))}">${esc(x.combat ? x.label : String(x.label).slice(0, 10))}${x.combat && x.value > 1 ? ` ${x.value}` : ""}</span>`);
    const gear = (u.gear || []).map(g => `<i title="${esc(g)}"></i>`).join("");
    const aria = `${name}${!isGear && u.might != null ? ", " + u.might + " might" : ""}${states.length ? ", " + states.join(", ") : ""}`;
    return `<div class="rb-unit p${p} ${isGear ? "gear" : ""} ${u.exhausted ? "ex" : ""} ${u.stunned ? "stunned" : ""} ${champion ? "champion" : ""} ${role || ""} ${legal ? "rb-legal-hl" : ""} ${canMove ? "rb-can-move" : ""} ${inFocus(L, name, c, u) ? "rb-focus-hl" : ""}" data-z="${k}"${u.id != null ? ` data-oid="${u.id}"` : ""}${pickAttr(legal)} tabindex="0" role="img" aria-label="${esc(aria)}">
      <div class="rb-art">${pic(c, name, 240) || fallback(c, name, null, isGear ? "gear" : "unit token")}</div>
      ${tags.length ? `<div class="rb-tags">${tags.join("")}</div>` : ""}${gear ? `<div class="rb-gearline">${gear}</div>` : ""}
      ${canMove ? `<span class="rb-mv" aria-hidden="true" title="can move now"></span>` : ""}
      ${!isGear && u.might != null ? `<div class="rb-might ${u.buffs ? "buffed" : ""} ${u.damage ? "hurt" : ""}">${u.might}</div>` : ""}
      ${chips.length ? `<div class="rb-kws">${chips.join("")}</div>` : ""}
      <div class="rb-name">${esc(name)}</div></div>`;
  }
  function runeStrip(pl, info) {
    if (Array.isArray(pl.runes) && pl.runes.length) {          // live engine state: one entry per rune, with its card
      const runes = [...pl.runes].sort((a, b) => (a.exhausted ? 1 : 0) - (b.exhausted ? 1 : 0));
      const ready = runes.filter(r => !r.exhausted).length, shown = runes.slice(0, 14);
      const dom = r => { const c = card(r); return (c && c.domains && c.domains[0]) || String(r.name || "").toLowerCase().replace(/ rune$/, ""); };
      return `<div class="rb-runes"><span class="rb-zlabel">Runes</span><div class="rb-strip" role="img" aria-label="${ready} runes ready, ${runes.length - ready} exhausted">${shown.map(r => `<i class="rb-rune ${r.exhausted ? "ex" : ""}" style="--d:${DOMAINS[dom(r)] || "#8f7a3f"}" title="${esc(r.name || "rune")}${r.exhausted ? " (exhausted)" : ""}"></i>`).join("")}${runes.length > 14 ? `<span class="rb-rn">+${runes.length - 14}</span>` : ""}</div><div class="rb-rn">${ready} ready · ${runes.length - ready} exhausted${pl.power_pool ? ` · ${pl.power_pool} power` : ""}</div></div>`;
    }
    const ready = pl.runes_ready || 0, ex = pl.runes_exhausted || 0, total = ready + ex;
    const split = (info && info.runes) || {}; const doms = Object.entries(split).filter(([d, n]) => DOMAINS[d] && n > 0).sort((a, b) => b[1] - a[1]);
    const seq = []; const sum = doms.reduce((a, [, n]) => a + n, 0) || 1;
    doms.forEach(([d, n]) => { for (let i = 0; i < Math.round(12 * n / sum); i++) seq.push(d); });
    const dom = i => seq.length ? seq[i % seq.length] : null;
    const pips = []; const shown = Math.min(total, 14);
    for (let i = 0; i < shown; i++) pips.push(`<i class="rb-rune ${i >= ready ? "ex" : ""}" style="--d:${DOMAINS[dom(i)] || "#8f7a3f"}" title="${i >= ready ? "exhausted" : "ready"} rune"></i>`);
    return `<div class="rb-runes"><span class="rb-zlabel">Runes</span><div class="rb-strip" role="img" aria-label="${ready} runes ready, ${ex} exhausted">${pips.join("")}${total > shown ? `<span class="rb-rn">+${total - shown}</span>` : ""}</div><div class="rb-rn">${ready} ready · ${ex} exhausted</div></div>`;
  }
  function championStatus(board, i, info) {
    const p = i + 1; let unit = null, where = "";
    const pl = board.players[i] || {};
    // the deck's champion: named by the live state (players[].champion) or the caller (players[i].champion); else any champion unit
    const champName = ((pl.champion && pl.champion.name) || (info && info.champion) || "").toLowerCase();
    const isCh = u => { if (champName) return nameOf(u).toLowerCase() === champName; const c = card(u); return !!(c && (c.super === "champion" || c.super_type === "champion")); };
    for (const u of board.players[i].base_units || []) if (isCh(u)) { unit = u; where = "in base"; break; }
    if (!unit) for (const bf of board.battlefields) for (const u of bf["p" + p] || []) if (isCh(u)) { unit = u; where = "at " + bf.name; break; }
    const name = unit ? nameOf(unit) : (pl.champion && pl.champion.name) || (info && info.champion) || "";
    if (!name) return "";
    return `<div class="rb-champ"><span class="rb-zlabel">Champion</span><span class="rb-cn">${esc(name)}</span><span class="rb-cs ${unit ? "on" : ""}">${unit ? where : "not in play"}</span></div>`;
  }
  function rail(board, i, L, bottom, sit) {
    const pl = board.players[i], p = i + 1, info = (S.opts.players || [])[i] || {};
    const target = S.opts.target || 8, mine = bottom;
    const showHand = mine || (S.opts.hands || "both") === "both";
    const names = Array.isArray(pl.hand) ? pl.hand : [];
    const hand = Array.isArray(pl.hand_cards) && pl.hand_cards.length === names.length && names.every(h => h != null) ? pl.hand_cards : names;
    const handCount = pl.hand_count ?? hand.length;
    const hidden = !showHand || hand.some(h => h == null);
    const handHtml = hidden || !hand.length
      ? `<div class="rb-backs" role="img" aria-label="${handCount} cards in hand">${Array.from({ length: Math.min(handCount, 10) }, () => `<div class="rb-back"></div>`).join("")}</div>`
      : `<div class="rb-cards ${hand.length > 7 ? "fan" : ""}" style="--n:${hand.length}">${hand.map(h => `<div class="rb-hc">${cardEl(h, p, mine ? "" : "sm", mine ? L : focusOnly(L))}</div>`).join("")}</div>`;
    const nLegal = mine && L ? hand.filter(h => { const c = card(h), n = nameOf(h).toLowerCase(); return L.cards.get(n) || (c && L.cards.get(String(c.id))) || (h && typeof h === "object" && h.card != null && L.cards.get(String(h.card))); }).length : 0;
    const legend = pl.legend ? cardEl(pl.legend, p, "legend", null, "legend" + (pl.legend.exhausted ? ", exhausted" : "")) : "";
    const units = [...(pl.base_units || []).map(u => unitEl(u, p, L, "in base", false, "")), ...(pl.base_gear || []).map(g => unitEl(g, p, L, "in base", true, ""))];
    const pips = Array.from({ length: target }, (_, k) => `<i class="${k < (pl.score || 0) ? "on" : ""}"></i>`).join("");
    const active = board.turn_player === p;
    const ring = sit && sit.zone && (sit.player ? sit.player === p : mine) ? sit.zone : "";   // the zone the decision concerns
    return `<section class="rb-rail ${bottom ? "rb-me" : "rb-opp"} ${hidden || !hand.length ? "rb-hand-hidden" : ""}" aria-label="Player ${p}">
      <div class="rb-side p${p}">${active ? `<span class="rb-active">turn</span>` : ""}<span class="rb-who"><b>P${p}</b> ${esc(info.label || (bottom ? "viewer" : "opponent"))}</span><div class="rb-sc">${pl.score || 0}<small>/${target}</small></div><div class="rb-pips" aria-hidden="true">${pips}</div>${pl.energy_pool ? `<span class="rb-energy">energy <b>${pl.energy_pool}</b></span>` : ""}${info.deck ? `<span class="rb-deck" title="${esc(info.deck)}">${esc(info.deck)}</span>` : ""}</div>
      <div class="rb-hand ${ring === "hand" ? "rb-ring" : ""}"><span class="rb-hcount">Hand · <b>${handCount}</b>${nLegal ? `<span class="rb-hplay">· ${nLegal} playable</span>` : ""}</span>${handHtml}</div>
      <div class="rb-zone">
        <div class="rb-legendbox">${legend ? `<div><span class="rb-zlabel">Legend</span>${legend}</div>` : ""}${championStatus(board, i, info)}</div>
        <div class="rb-base ${ring === "base" ? "rb-ring" : ""}"><span class="rb-zlabel">Base</span><div class="rb-units">${units.join("") || `<span class="rb-none">no units in base</span>`}</div></div>
        ${runeStrip(pl, info)}
        <div class="rb-piles"><div class="rb-pile deck" title="cards left in the main deck"><b>${pl.deck ?? "?"}</b><span>deck</span></div><div class="rb-pile" title="cards in the trash"><b>${pl.trash ?? 0}</b><span>trash</span></div>${pl.banish != null ? `<div class="rb-pile" title="banished cards"><b>${pl.banish}</b><span>banish</span></div>` : ""}</div>
      </div></section>`;
  }
  /** The chain and the focus line under a battlefield in combat: what waits to resolve, who passed, who acts. */
  function chainList(board, sit) {
    const chain = board.chain || [], frame = S.frame || {}, dp = sit.player, passes = Array.isArray(frame.passes) ? frame.passes : [];
    const items = chain.map((ci, i) => {
      const c = card({ card: ci.card, name: ci.source }), name = ci.source || ci.name || "?", k = zoomKey({ card: c, name, state: `on the chain, played by P${ci.player}` });
      return `<li class="rb-cli p${ci.player} ${i === chain.length - 1 ? "last" : ""}" data-z="${k}" tabindex="0"><span class="rb-cliart">${c ? pic(c, name, 240) : ""}</span><span class="rb-clin">${esc(name)}</span><span class="rb-cliwho">${esc(seat(ci.player))}</span>${ci.resolving ? `<em>resolving</em>` : i === chain.length - 1 ? `<em>resolves next</em>` : ""}</li>`;
    });
    const act = !dp ? "" : sit.kinds.has("damage") ? `${esc(seat(dp))} assigns combat damage` : chain.length ? `${esc(seat(dp))} may respond` : sit.kinds.has("focus") ? `${esc(seat(dp))} has focus` : `${esc(seat(dp))} to act`;
    return `<div class="rb-bfchain"><div class="rb-bfchainhead"><span class="rb-zlabel">Chain</span>${items.length ? "" : `<span class="rb-none">empty — nothing waiting to resolve</span>`}</div>${items.length ? `<ol class="rb-clist">${items.join("")}</ol>` : ""}
      <div class="rb-passes">${passes.map(p => `<span class="rb-pass p${p}">${esc(seat(p))} passed focus</span>`).join("")}${act ? `<span class="rb-act p${dp}">${act}</span>` : ""}</div></div>`;
  }
  function battlefield(bf, idx, L, viewer, sit, board) {
    const c = card({ card: bf.card, name: bf.name });
    const opp = viewer === 1 ? 2 : 1;
    const legal = L && (L.bfs.get(String(bf.name).toLowerCase()) || L.bfs.get(String(idx)));
    const focus = L && L.fbfs.size && (L.fbfs.get(String(bf.name).toLowerCase()) || L.fbfs.get(String(idx)));
    const hot = sit && sit.index === idx ? sit : null, mode = hot ? hot.mode : "";
    const held = bf.controller ? `held-${bf.controller}` : "";
    const both = unitsAt(bf, 1).length > 0 && unitsAt(bf, 2).length > 0;
    const status = mode === "combat" ? "Combat" : mode === "showdown" ? "Showdown" : mode === "contested" || both ? "Contested" : bf.controller ? `Held by ${holder(bf.controller)}` : "Open";
    const ribbon = mode === "combat" ? "combat" : mode === "showdown" ? "showdown" : mode === "contested" || both ? "contested" : held || "open";
    const k = zoomKey({ card: c, name: bf.name, land: true, state: `battlefield · ${status.toLowerCase()}. ${HOLD_HINT}` });
    const sp = sprite(c, true), art = sp ? "" : imgUrl(c, 600);
    const role = p => hot && hot.atk.units.length && hot.def.units.length ? (p === hot.attacker ? "attacker" : "defender") : hot && p === hot.attacker && hot.atk.units.length ? "attacker" : "";
    const lane = (p, cls) => {
      const us = bf["p" + p] || [], mt = sumMight(unitsAt(bf, p)), r = role(p);
      return `<div class="rb-lane ${cls} p${p} ${r}"><div class="rb-lanehead"><span class="rb-lanewho"><b>P${p}</b> ${esc(who(p))}</span>${r ? `<span class="rb-lanerole">${r === "attacker" ? "attacking" : "defending"}</span>` : ""}<span class="rb-lanemight" title="the might of the units here, summed">${unitsAt(bf, p).length ? `might <b>${mt}</b>` : `<i>no units</i>`}</span></div>
        <div class="rb-laneunits">${us.map(u => unitEl(u, p, L, "at " + bf.name, u.might == null && !u.facedown, r)).join("")}</div></div>`;
    };
    const duel = hot && (hot.verdict || hot.note) ? `<div class="rb-duel ${esc(mode)} ${esc(hot.outcome)}">
        <div class="rb-duelrow"><span class="rb-dside atk p${hot.attacker}"><small>Attackers · ${esc(seat(hot.attacker))}</small><b>${hot.atk.might}</b></span><span class="rb-dvs" aria-hidden="true">⚔</span><span class="rb-dside def p${hot.defender}"><b>${hot.def.might}</b><small>Defenders · ${esc(seat(hot.defender))}</small></span></div>
        <div class="rb-verdict">${hot.verdict ? `${esc(hot.verdict)} <i>Abilities and responses on the chain can change it.</i>` : esc(hot.note)}</div></div>` : "";
    return `<article class="rb-bf ${held} ${esc(mode)} ${hot ? "rb-hot" : ""} ${legal ? "rb-legal-bf" : ""} ${focus ? "rb-focus-bf" : ""} ${sp || art ? "" : "noart"}"${pickAttr(legal)} aria-label="${esc(bf.name)}, ${esc(status.toLowerCase())}">
      ${sp ? `<div class="rb-bfart sprite" style="${sp.style}"></div>` : art ? `<img class="rb-bfart" src="${esc(art)}" alt="" loading="lazy" decoding="async">` : `<div class="rb-bfart"></div>`}
      <header class="rb-bfhead"><span class="rb-bfidx">${idx + 1}</span><span class="rb-bfname" data-z="${k}" tabindex="0" role="button" aria-label="${esc(bf.name)}, show card">${esc(bf.name)}</span>
        <span class="rb-ribbon ${esc(ribbon)}" title="${esc(HOLD_HINT)}">${esc(status)}</span><span class="rb-bfhint" role="tooltip">${esc(HOLD_HINT)}</span></header>
      ${S.opts.showText !== false && c && (c.text || c.ability_text) ? `<div class="rb-bftext">${esc(c.text || c.ability_text)}</div>` : ""}
      ${duel}
      ${lane(opp, "theirs")}
      <div class="rb-bfmid"></div>
      ${lane(viewer, "mine")}
      ${hot && mode !== "contested" ? chainList(board, hot) : ""}
      <div class="rb-bfhold"></div></article>`;
  }
  const phaseName = p => String(p || "").replace(/([a-z])([A-Z])/g, "$1 $2");
  function topBar(board, sit) {
    const chain = board.chain || [];
    const items = chain.map((ci, i) => `<span class="rb-ci p${ci.player} ${i === chain.length - 1 ? "last" : ""}"><i></i>${esc(ci.source || ci.name || "?")}</span>`);
    const banner = S.opts.banner ? `<span class="rb-banner ${esc(S.opts.bannerKind || "")}">${esc(S.opts.banner)}</span>` : "";
    const where = sit && sit.index >= 0 && sit.mode ? `<span class="rb-sit ${esc(sit.mode)}">${sit.mode === "combat" ? "Combat" : sit.mode === "showdown" ? "Showdown" : "Contested"} at ${esc(sit.name)}</span>` : "";
    return `<div class="rb-top"><div class="rb-turn"><span>Turn ${board.turn ?? "–"}</span><span class="rb-ph">${esc(phaseName(board.phase))}${board.turn_player ? ` · P${board.turn_player}'s turn` : ""}</span></div>${where}${banner}
      <div class="rb-chain"><span class="rb-lbl">Chain</span>${items.length ? items.join(`<span class="rb-arrow">›</span>`) : `<span class="rb-empty">empty</span>`}</div></div>`;
  }
  function legalBar(frame) {
    if (!Array.isArray(frame.legal) || !frame.legal.length) return "";
    const lbl = S.opts.legalLabel || "Your options";
    return `<div class="rb-legal ${S.opts.legalDisabled ? "off" : ""}"><span class="rb-lbl">${esc(lbl)}</span>${frame.legal.map((e, i) => `<button type="button" data-pick="${e.index}" class="${e.card || e.battlefield ? "hot" : ""}" ${S.opts.legalDisabled ? "disabled" : ""}>${i < 9 ? `<kbd class="rb-key">${i + 1}</kbd>` : ""}${esc(e.text || "option " + e.index)}</button>`).join("")}</div>`;
  }

  // ── transitions (FLIP on object ids) ───────────────────────────────────────
  function snapshot() { const m = new Map(); S.el.querySelectorAll("[data-oid]").forEach(e => m.set(e.dataset.oid, e.getBoundingClientRect())); return m; }
  function animate(prev) {
    if (!prev.size || !("animate" in Element.prototype) || matchMedia("(prefers-reduced-motion: reduce)").matches) return;
    const scroll = { x: window.scrollX, y: window.scrollY };
    S.el.querySelectorAll("[data-oid]").forEach(e => {
      const a = prev.get(e.dataset.oid);
      if (!a) { e.animate([{ opacity: 0, transform: "scale(.6)" }, { opacity: 1 }], { duration: 260, easing: "ease-out" }); return; }
      const b = e.getBoundingClientRect(); const dx = a.left - b.left, dy = a.top - b.top;
      if (Math.abs(dx) + Math.abs(dy) > 3 && Math.abs(dx) + Math.abs(dy) < 2000) e.animate([{ transform: `translate(${dx}px, ${dy}px)` }, { transform: "none" }], { duration: 380, easing: "cubic-bezier(.2,.7,.2,1)" });
    });
    void scroll;
  }

  // ── zoom preview ───────────────────────────────────────────────────────────
  function ensureZoom() {
    if (S.zoom) return;
    S.backdrop = document.createElement("div"); S.backdrop.className = "rb-zbackdrop";
    S.zoom = document.createElement("div"); S.zoom.className = "rb-zoom"; S.zoom.setAttribute("aria-hidden", "true");
    document.body.append(S.backdrop, S.zoom);
    S.backdrop.addEventListener("click", hideZoom);
  }
  function zoomHtml(d) {
    const c = d.card; const dom = (c && c.domains || []).map(x => `<i class="rb-dom" style="background:${DOMAINS[x] || "#888"}"></i>${x}`).join(" ");
    const cost = costOf(c); const pw = c && (c.power ?? c.power_cost);
    const land = d.land || (c && /1038x744|1039x744/.test(c.image_url || ""));
    const line = [typeOf(c), c && (c.super || c.super_type), cost != null ? `cost ${cost}${pw ? " + " + pw + " power" : ""}` : "", c && c.might != null && typeOf(c) === "unit" ? `${c.might} might` : "", (c && c.tags || []).join(", ")].filter(Boolean).join(" · ");
    return `<button type="button" class="rb-zclose" aria-label="close">×</button><div class="rb-zcard ${land ? "land" : ""}">${pic(c, d.name, land ? 800 : 600) || fallback(c, d.name, d.might, d.kind)}</div>
      <div class="rb-zinfo"><b>${esc(d.name)}</b>${line ? `<div>${esc(line)}</div>` : ""}${dom ? `<div>${dom}</div>` : ""}${d.state ? `<div class="rb-zstate">${esc(d.state)}${d.might != null && c && c.might != null && d.might !== c.might ? ` — might now ${d.might}` : ""}</div>` : ""}</div>`;
  }
  function showZoom(key, modal, x, y) {
    const d = S.z[key]; if (!d) return; ensureZoom();
    if (S.hoverKey !== key || modal) { S.zoom.innerHTML = zoomHtml(d); S.hoverKey = key; }
    S.zoom.classList.toggle("modal", !!modal); S.backdrop.classList.toggle("on", !!modal);
    S.zoom.classList.add("on"); S.zoom.setAttribute("aria-hidden", "false");
    if (!modal) placeZoom(x, y);
  }
  function placeZoom(x, y) {
    if (!S.zoom || S.zoom.classList.contains("modal")) return;
    const w = 260, h = S.zoom.offsetHeight || 440, vw = innerWidth, vh = innerHeight;
    let left = x + 22, top = y - h / 2;
    if (left + w > vw - 8) left = x - w - 22; if (left < 8) left = 8;
    top = Math.max(8, Math.min(vh - h - 8, top));
    S.zoom.style.left = left + "px"; S.zoom.style.top = top + "px";
  }
  function hideZoom() { if (!S.zoom) return; S.zoom.classList.remove("on", "modal"); S.backdrop.classList.remove("on"); S.zoom.setAttribute("aria-hidden", "true"); S.hoverKey = null; }

  // ── events (delegated) ─────────────────────────────────────────────────────
  function on(target, type, fn, o) { target.addEventListener(type, fn, o); S.handlers.push([target, type, fn, o]); }
  function bind() {
    const root = S.el;
    on(root, "pointerdown", e => { S.touch = e.pointerType === "touch"; });
    on(root, "mouseover", e => { if (S.touch) return; const t = e.target.closest("[data-z]"); if (t && root.contains(t)) showZoom(+t.dataset.z, false, e.clientX, e.clientY); });
    on(root, "mousemove", e => { if (S.touch || !S.zoom || !S.zoom.classList.contains("on") || S.zoom.classList.contains("modal")) return; if (S.raf) return; S.raf = requestAnimationFrame(() => { S.raf = 0; placeZoom(e.clientX, e.clientY); }); });
    on(root, "mouseout", e => { const t = e.target.closest("[data-z]"); if (t && !(e.relatedTarget && t.contains(e.relatedTarget)) && S.zoom && !S.zoom.classList.contains("modal")) hideZoom(); });
    on(root, "focusin", e => { const t = e.target.closest("[data-z]"); if (t) { const r = t.getBoundingClientRect(); showZoom(+t.dataset.z, false, r.right, r.top + r.height / 2); } });
    on(root, "focusout", e => { const t = e.target.closest("[data-z]"); if (t && S.zoom && !S.zoom.classList.contains("modal")) hideZoom(); });
    on(root, "click", e => {
      const pick = e.target.closest("[data-pick]");
      if (pick && root.contains(pick) && typeof S.opts.onPick === "function") {
        const idx = +pick.dataset.pick; const entry = (S.frame && S.frame.legal || []).find(x => x.index === idx) || { index: idx };
        hideZoom(); S.opts.onPick(idx, entry); e.preventDefault(); return;
      }
      const t = e.target.closest("[data-z]"); if (!t || !root.contains(t)) return;
      if (S.touch || e.detail === 0) showZoom(+t.dataset.z, true); // tap or keyboard: open the modal preview
    });
    on(root, "keydown", e => { if (e.key === "Escape") hideZoom(); const t = e.target.closest("[data-z],[data-pick]"); if (t && (e.key === "Enter" || e.key === " ")) { e.preventDefault(); t.click(); } });
    on(root, "error", e => { // image failed: swap in the styled frame
      const im = e.target; if (!(im instanceof HTMLImageElement)) return;
      if (im.classList.contains("rb-bfart")) { im.closest(".rb-bf").classList.add("noart"); im.replaceWith(Object.assign(document.createElement("div"), { className: "rb-bfart" })); return; }
      const host = im.closest("[data-z]"); const d = host && S.z[+host.dataset.z];
      const box = im.parentElement; im.remove(); if (box) box.insertAdjacentHTML("afterbegin", fallback(d && d.card, d ? d.name : im.alt, d ? d.might : null, d && d.kind));
    }, true);
    on(document, "keydown", e => { if (e.key === "Escape") hideZoom(); });
    on(document, "click", e => { if (S.zoom && S.zoom.classList.contains("modal") && e.target.closest(".rb-zclose")) hideZoom(); });
  }

  // ── public API ─────────────────────────────────────────────────────────────
  function mount(el, opts) {
    if (S.el && S.el !== el) destroy();
    S.el = el; S.opts = Object.assign({ viewer: 1, hands: "both", target: 8, imageParams: true, showText: true }, opts || {});
    if (S.opts.cards) indexCards(S.opts.cards);
    el.classList.add("rb"); el.setAttribute("role", "region"); el.setAttribute("aria-label", "game board");
    if (!S.handlers.length) bind();
    return API;
  }
  function render(frame, opts) {
    if (!S.el) throw new Error("RiftBoard.render before mount");
    if (opts) { const had = S.opts.cards; Object.assign(S.opts, opts); if (opts.cards && opts.cards !== had) indexCards(opts.cards); }
    S.frame = frame; S.z = []; hideZoom();
    const board = frame && frame.board; if (!board) { S.el.innerHTML = `<div class="rb-top"><span class="rb-turn">No game state</span></div>`; return; }
    const viewer = S.opts.viewer === 2 ? 2 : 1, me = viewer - 1, opp = 1 - me;
    const L = legalIndex(frame), sit = situation(frame);
    const prev = S.lastViewer === viewer && S.opts.animate !== false ? snapshot() : new Map(); S.lastViewer = viewer; // no flight across the table on a seat flip
    const bfs = board.battlefields || [];
    S.el.style.setProperty("--rb-nbf", Math.max(1, bfs.length));
    // the battlefield where the action is takes the room: 1.8 columns against 1 for each of the others
    S.el.style.setProperty("--rb-cols", bfs.map((_, i) => sit && sit.index === i ? "minmax(0, 1.8fr)" : "minmax(0, 1fr)").join(" ") || "minmax(0, 1fr)");
    S.el.classList.toggle("rb-in-combat", !!(sit && sit.index >= 0 && sit.mode));
    S.el.innerHTML = topBar(board, sit) + rail(board, opp, L.any ? L : null, false, sit)
      + `<section class="rb-field ${sit && sit.index >= 0 ? "rb-has-hot" : ""}" aria-label="battlefields">${bfs.map((bf, i) => battlefield(bf, i, L.any ? L : null, viewer, sit, board)).join("")}</section>`
      + rail(board, me, L.any ? L : null, true, sit) + legalBar(frame);
    animate(prev);
  }
  function destroy() {
    for (const [t, ty, fn, o] of S.handlers) t.removeEventListener(ty, fn, o);
    S.handlers = []; if (S.zoom) S.zoom.remove(); if (S.backdrop) S.backdrop.remove(); S.zoom = S.backdrop = null;
    if (S.el) { S.el.classList.remove("rb", "rb-in-combat"); S.el.innerHTML = ""; } S.el = null; S.frame = null; S.z = [];
  }
  const API = { mount, render, destroy, cardImage, sprite, card, situation, parseOption, concerns, keywords: unitKeywords, DOMAINS };
  window.RiftBoard = API;
})();
