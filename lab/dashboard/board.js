/* RiftBoard — draws one Riftbound game state like the physical table.
   window.RiftBoard = { mount(el, opts), render(frame, opts), destroy(), cardImage(url, w) }

   frame: one replay frame from the engine ({board:{players, battlefields, chain, phase, turn, turn_player}, ...});
          an optional frame.legal = [{index, text, card?, battlefield?}] highlights playable cards / destinations
          and renders a clickable list (opts.onPick(index, entry)).
   opts:  cards      array (or Map) of card records {id, name, image_url, type, energy, power, might, domains, super, tags}
          viewer     1 | 2  seat drawn at the bottom (default 1)
          hands      "both" | "viewer"  whether the opponent's hand is face up (default "both")
          players    [{label, deck, runes:{calm:7, mind:5}, champion:"Riven, Broken Blade"}] per seat, cosmetic
          target     points to win (default 8)
          onPick     function(index, legalEntry)
          imageParams  true: ask the CDN for small webp renditions (default true)
          showText   battlefield rules text under its name (default true)
          animate    false disables the FLIP transition of units between frames (default true)
   Plain DOM, no dependencies. Styles in board.css. */
(function () {
  "use strict";
  const DOMAINS = { fury: "#d4483b", calm: "#27a97e", mind: "#3f86e0", body: "#e38a2e", chaos: "#8e5cf0", order: "#e1b83a" };
  const esc = s => String(s ?? "").replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  const S = { el: null, opts: {}, frame: null, byId: new Map(), byName: new Map(), zoom: null, backdrop: null, z: [], rects: new Map(), handlers: [], touch: false, raf: 0, hoverKey: null };

  // ── card lookup and images ─────────────────────────────────────────────────
  function indexCards(cards) {
    S.byId = new Map(); S.byName = new Map();
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
  function domStyle(c) {
    const d = (c && c.domains) || []; const a = DOMAINS[d[0]] || "#4a4466", b = DOMAINS[d[1]] || a;
    return `--d1:${a};--d2:${b}`;
  }
  function fallback(c, name, might, kind) {
    const cost = /^(unit|spell|gear)$/.test(typeOf(c)) ? costOf(c) : null;
    return `<div class="rb-fb" style="${domStyle(c)}">${cost != null ? `<span class="rb-fbc">${cost}</span>` : ""}${might != null ? `<span class="rb-fbm">${might}</span>` : ""}<div class="rb-fbn">${esc(name)}</div><div class="rb-fbt">${esc(typeOf(c) || kind || "card")}</div></div>`;
  }
  const img = (c, name, w, extra = "") => imgUrl(c, w) ? `<img src="${esc(imgUrl(c, w))}" alt="${esc(name)}" referrerpolicy="no-referrer" loading="lazy" decoding="async" ${extra}>` : "";
  function zoomKey(data) { S.z.push(data); return S.z.length - 1; }

  // ── legal-action helpers ───────────────────────────────────────────────────
  function legalIndex(frame) {
    const L = { cards: new Map(), bfs: new Map(), any: Array.isArray(frame.legal) && frame.legal.length > 0 };
    for (const e of frame.legal || []) {
      const add = (m, k) => { if (k == null) return; k = String(k).toLowerCase(); if (!m.has(k)) m.set(k, e); };
      if (e.card != null) { add(L.cards, e.card); if (typeof e.card === "object") { add(L.cards, e.card.name); add(L.cards, e.card.id); } }
      if (e.battlefield != null) { add(L.bfs, e.battlefield); if (typeof e.battlefield === "object") { add(L.bfs, e.battlefield.name); add(L.bfs, e.battlefield.index); } }
    }
    return L;
  }
  const pickAttr = e => e ? ` data-pick="${e.index}"` : "";

  // ── element builders ───────────────────────────────────────────────────────
  function cardEl(ref, p, cls, L, extraState) {
    const c = card(ref), name = nameOf(ref);
    const facedown = ref && typeof ref === "object" && ref.facedown;
    if (facedown) return `<div class="rb-back" role="img" aria-label="face-down card"></div>`;
    const legal = L && (L.cards.get(name.toLowerCase()) || (c && L.cards.get(String(c.id))) || (ref && ref.card != null && L.cards.get(String(ref.card))));
    const k = zoomKey({ card: c, name, state: extraState || (typeOf(c) ? typeOf(c) + " in hand" : "in hand") });
    const cost = costOf(c);
    return `<div class="rb-card ${cls || ""} ${legal ? "rb-legal-hl" : ""}" data-z="${k}"${pickAttr(legal)} tabindex="0" role="img" aria-label="${esc(name)}${cost != null ? ", cost " + cost : ""}">${img(c, name, 200) || fallback(c, name, c && (typeOf(c) === "unit") ? c.might : null)}</div>`;
  }
  function unitEl(u, p, L, where, isGear) {
    const c = card(u), name = nameOf(u);
    const champion = !!(c && (c.super === "champion" || c.super_type === "champion"));
    const legal = L && (L.cards.get(name.toLowerCase()) || (u.id != null && L.cards.get(String(u.id))) || (u.card != null && L.cards.get(String(u.card))));
    const states = [];
    if (u.exhausted) states.push("exhausted"); if (u.stunned) states.push("stunned"); if (u.facedown) states.push("face down");
    if (u.damage) states.push(`${u.damage} damage`); if (u.buffs) states.push(`+${u.buffs} buff${u.buffs > 1 ? "s" : ""}`);
    if (u.gear && u.gear.length) states.push("equipped: " + u.gear.join(", "));
    const k = zoomKey({ card: c, name, kind: isGear ? "gear" : "unit token", state: `${isGear ? "gear" : champion ? "champion unit" : "unit"} ${where}${states.length ? " — " + states.join(", ") : ""}`, might: u.might });
    const tags = [];
    if (u.damage) tags.push(`<span class="rb-tag dmg">−${u.damage}</span>`);
    if (u.buffs) tags.push(`<span class="rb-tag buff">+${u.buffs}</span>`);
    if (u.stunned) tags.push(`<span class="rb-tag stun">STUN</span>`);
    for (const kw of u.keywords || []) tags.push(`<span class="rb-tag kw">${esc(String(kw).slice(0, 10))}</span>`);
    const gear = (u.gear || []).map(g => `<i title="${esc(g)}"></i>`).join("");
    const aria = `${name}${!isGear && u.might != null ? ", " + u.might + " might" : ""}${states.length ? ", " + states.join(", ") : ""}`;
    return `<div class="rb-unit p${p} ${isGear ? "gear" : ""} ${u.exhausted ? "ex" : ""} ${u.stunned ? "stunned" : ""} ${champion ? "champion" : ""} ${u.facedown ? "facedown" : ""} ${legal ? "rb-legal-hl" : ""}" data-z="${k}"${u.id != null ? ` data-oid="${u.id}"` : ""}${pickAttr(legal)} tabindex="0" role="img" aria-label="${esc(aria)}">
      <div class="rb-art">${u.facedown ? "" : img(c, name, 240) || fallback(c, name, null, isGear ? "gear" : "unit token")}</div>
      ${tags.length ? `<div class="rb-tags">${tags.join("")}</div>` : ""}${gear ? `<div class="rb-gearline">${gear}</div>` : ""}
      ${!isGear && u.might != null && !u.facedown ? `<div class="rb-might ${u.buffs ? "buffed" : ""} ${u.damage ? "hurt" : ""}">${u.might}</div>` : ""}
      <div class="rb-name">${u.facedown ? "face down" : esc(name)}</div></div>`;
  }
  function runeStrip(pl, info) {
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
    const champName = (info && info.champion || "").toLowerCase();
    const isCh = u => { const c = card(u); return (c && (c.super === "champion" || c.super_type === "champion")) || (champName && nameOf(u).toLowerCase() === champName); };
    for (const u of board.players[i].base_units || []) if (isCh(u)) { unit = u; where = "in base"; break; }
    if (!unit) for (const bf of board.battlefields) for (const u of bf["p" + p] || []) if (isCh(u)) { unit = u; where = "at " + bf.name; break; }
    const name = unit ? nameOf(unit) : (info && info.champion) || "";
    if (!name) return "";
    return `<div class="rb-champ"><span class="rb-zlabel">Champion</span><span class="rb-cn">${esc(name)}</span><span class="rb-cs ${unit ? "on" : ""}">${unit ? where : "not in play"}</span></div>`;
  }
  function rail(board, i, L, bottom) {
    const pl = board.players[i], p = i + 1, info = (S.opts.players || [])[i] || {};
    const target = S.opts.target || 8, mine = bottom;
    const showHand = mine || (S.opts.hands || "both") === "both";
    const hand = Array.isArray(pl.hand) ? pl.hand : [];
    const handCount = pl.hand_count ?? hand.length;
    const hidden = !showHand || hand.some(h => h == null);
    const handHtml = hidden || !hand.length
      ? `<div class="rb-backs" role="img" aria-label="${handCount} cards in hand">${Array.from({ length: Math.min(handCount, 10) }, () => `<div class="rb-back"></div>`).join("")}</div>`
      : `<div class="rb-cards">${hand.map(h => cardEl(h, p, mine ? "" : "sm", mine ? L : null)).join("")}</div>`;
    const legend = pl.legend ? cardEl(pl.legend, p, "legend", null, "legend" + (pl.legend.exhausted ? ", exhausted" : "")) : "";
    const units = [...(pl.base_units || []).map(u => unitEl(u, p, L, "in base", false)), ...(pl.base_gear || []).map(g => unitEl(g, p, L, "in base", true))];
    const pips = Array.from({ length: target }, (_, k) => `<i class="${k < (pl.score || 0) ? "on" : ""}"></i>`).join("");
    const active = board.turn_player === p;
    return `<section class="rb-rail ${bottom ? "rb-me" : "rb-opp"}" aria-label="Player ${p}">
      <div class="rb-side p${p}">${active ? `<span class="rb-active">turn</span>` : ""}<span class="rb-who"><b>P${p}</b> ${esc(info.label || (bottom ? "viewer" : "opponent"))}</span><div class="rb-sc">${pl.score || 0}<small>/${target}</small></div><div class="rb-pips" aria-hidden="true">${pips}</div>${pl.energy_pool ? `<span class="rb-energy">energy <b>${pl.energy_pool}</b></span>` : ""}${info.deck ? `<span class="rb-deck" title="${esc(info.deck)}">${esc(info.deck)}</span>` : ""}</div>
      <div class="rb-hand"><span class="rb-hcount">Hand <b>${handCount}</b></span>${handHtml}</div>
      <div class="rb-zone">
        <div class="rb-legendbox">${legend ? `<div><span class="rb-zlabel">Legend</span>${legend}</div>` : ""}${championStatus(board, i, info)}</div>
        <div class="rb-base"><span class="rb-zlabel">Base</span><div class="rb-units">${units.join("") || `<span class="rb-none">no units in base</span>`}</div></div>
        ${runeStrip(pl, info)}
        <div class="rb-piles"><div class="rb-pile deck" title="cards left in the main deck"><b>${pl.deck ?? "?"}</b><span>deck</span></div><div class="rb-pile" title="cards in the trash"><b>${pl.trash ?? 0}</b><span>trash</span></div>${pl.banish != null ? `<div class="rb-pile" title="banished cards"><b>${pl.banish}</b><span>banish</span></div>` : ""}</div>
      </div></section>`;
  }
  function battlefield(bf, idx, L, viewer) {
    const c = card({ card: bf.card, name: bf.name });
    const opp = viewer === 1 ? 2 : 1;
    const theirs = bf["p" + opp] || [], mine = bf["p" + viewer] || [];
    const legal = L && (L.bfs.get(String(bf.name).toLowerCase()) || L.bfs.get(String(idx)));
    const held = bf.controller ? `held-${bf.controller}` : "";
    const contested = theirs.length && mine.length;
    const k = zoomKey({ card: c, name: bf.name, land: true, state: `battlefield ${bf.controller ? "held by P" + bf.controller : "unheld"}${bf.combat ? ", combat" : ""}` });
    const art = imgUrl(c, 600);
    return `<article class="rb-bf ${held} ${bf.combat ? "combat" : ""} ${legal ? "rb-legal-bf" : ""} ${art ? "" : "noart"}"${pickAttr(legal)} aria-label="${esc(bf.name)}">
      ${art ? `<img class="rb-bfart" src="${esc(art)}" alt="" referrerpolicy="no-referrer" loading="lazy" decoding="async">` : `<div class="rb-bfart"></div>`}
      <header class="rb-bfhead"><span class="rb-bfidx">${idx + 1}</span><span class="rb-bfname" data-z="${k}" tabindex="0" role="button" aria-label="${esc(bf.name)}, show card">${esc(bf.name)}</span>
        ${bf.combat ? `<span class="rb-badge combat">combat</span>` : ""}
        <span class="rb-badge ${held || (contested ? "contested" : "")}">${bf.controller ? "held by P" + bf.controller : contested ? "contested" : "open"}</span></header>
      ${S.opts.showText !== false && c && (c.text || c.ability_text) ? `<div class="rb-bftext">${esc(c.text || c.ability_text)}</div>` : ""}
      <div class="rb-lane theirs"><span class="rb-lanelbl">P${opp}</span>${theirs.map(u => unitEl(u, opp, L, "at " + bf.name, false)).join("")}</div>
      <div class="rb-bfmid"></div>
      <div class="rb-lane mine"><span class="rb-lanelbl">P${viewer}</span>${mine.map(u => unitEl(u, viewer, L, "at " + bf.name, false)).join("")}</div>
      <div class="rb-bfhold"></div></article>`;
  }
  const phaseName = p => String(p || "").replace(/([a-z])([A-Z])/g, "$1 $2");
  function topBar(board) {
    const chain = board.chain || [];
    const items = chain.map((ci, i) => `<span class="rb-ci p${ci.player} ${i === chain.length - 1 ? "top" : ""}"><i></i>${esc(ci.source || ci.name || "?")}</span>`);
    return `<div class="rb-top"><div class="rb-turn"><span>Turn ${board.turn ?? "–"}</span><span class="rb-ph">${esc(phaseName(board.phase))}${board.turn_player ? ` · P${board.turn_player}'s turn` : ""}</span></div>
      <div class="rb-chain"><span class="rb-lbl">Chain</span>${items.length ? items.join(`<span class="rb-arrow">›</span>`) : `<span class="rb-empty">empty</span>`}</div></div>`;
  }
  function legalBar(frame) {
    if (!Array.isArray(frame.legal) || !frame.legal.length) return "";
    return `<div class="rb-legal"><span class="rb-lbl">Your options</span>${frame.legal.map(e => `<button type="button" data-pick="${e.index}" class="${e.card || e.battlefield ? "hot" : ""}">${esc(e.text || "option " + e.index)}</button>`).join("")}</div>`;
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
    const src = imgUrl(c, land ? 800 : 600);
    const line = [typeOf(c), c && (c.super || c.super_type), cost != null ? `cost ${cost}${pw ? " + " + pw + " power" : ""}` : "", c && c.might != null && typeOf(c) === "unit" ? `${c.might} might` : "", (c && c.tags || []).join(", ")].filter(Boolean).join(" · ");
    return `<button type="button" class="rb-zclose" aria-label="close">×</button><div class="rb-zcard ${land ? "land" : ""}">${src ? `<img src="${esc(src)}" alt="${esc(d.name)}" referrerpolicy="no-referrer" decoding="async">` : fallback(c, d.name, d.might, d.kind)}</div>
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
      const box = im.parentElement; im.remove(); if (box) box.insertAdjacentHTML("beforeend", fallback(d && d.card, d ? d.name : im.alt, d ? d.might : null, d && d.kind));
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
    const L = legalIndex(frame);
    const prev = S.lastViewer === viewer && S.opts.animate !== false ? snapshot() : new Map(); S.lastViewer = viewer; // no flight across the table on a seat flip
    S.el.style.setProperty("--rb-nbf", Math.max(1, (board.battlefields || []).length));
    S.el.innerHTML = topBar(board) + rail(board, opp, L.any ? L : null, false)
      + `<section class="rb-field" aria-label="battlefields">${(board.battlefields || []).map((bf, i) => battlefield(bf, i, L.any ? L : null, viewer)).join("")}</section>`
      + rail(board, me, L.any ? L : null, true) + legalBar(frame);
    animate(prev);
  }
  function destroy() {
    for (const [t, ty, fn, o] of S.handlers) t.removeEventListener(ty, fn, o);
    S.handlers = []; if (S.zoom) S.zoom.remove(); if (S.backdrop) S.backdrop.remove(); S.zoom = S.backdrop = null;
    if (S.el) { S.el.classList.remove("rb"); S.el.innerHTML = ""; } S.el = null; S.frame = null; S.z = [];
  }
  const API = { mount, render, destroy, cardImage, card, DOMAINS };
  window.RiftBoard = API;
})();
