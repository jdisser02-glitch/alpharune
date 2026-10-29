/* Riven Lab — app shell: hash router and view lifecycle.

   Routes (hash):  #/home (default) · #/tests · #/tests/<id> · #/decks · #/decks/<path> · #/replays · #/replays/<file>
                   #/play · #/bot · #/launch · #/cards        — plus an optional ?query=string after the path.
   A view is a module app/<view>.js exporting:
     render(container, params, ctx)   build the screen inside `container` (sync or async); params holds the path
                                      parameter under the view's name (id | name | file) and as `rest`, plus query keys
     destroy()                        optional; release timers, workers, listeners before the next view renders
     update(container, params, ctx)   optional; called instead of destroy+render when only the params changed
   ctx = { data, params, route: {view, params}, navigate }.
   Modules are imported on first use (lazy) and cached. Legacy "#tests" hashes are accepted. */
import { loadData, installCardPreview, navigate, fmtDateTime, errorBox, $, $$ } from "./lib.js";

const VIEWS = { home: {}, tests: { param: "id" }, decks: { param: "name" }, replays: { param: "file" }, play: {}, bot: {}, launch: {}, cards: {} };
const TITLES = { home: "Riven Lab", tests: "Tests", decks: "Decks", replays: "Replays", play: "Play the bot", bot: "The bot", launch: "Launch a test", cards: "Cards" };
const container = $("#view");
const mods = new Map();
let current = { view: null, params: {}, mod: null };
let token = 0;

export function parseHash(hash) {
  const h = String(hash || "").replace(/^#\/?/, "");
  const [path, q = ""] = h.split("?");
  const [view, ...rest] = path.split("/");
  const name = VIEWS[view] ? view : "home";
  const params = {};
  if (rest.length) { try { params.rest = decodeURIComponent(rest.join("/")); } catch (e) { params.rest = rest.join("/"); } if (VIEWS[name].param) params[VIEWS[name].param] = params.rest; }
  for (const [k, v] of new URLSearchParams(q)) params[k] = v;
  return { view: name, params };
}

async function route() {
  const { view, params } = parseHash(location.hash);
  const my = ++token;
  const data = await loadData().catch(e => { container.innerHTML = errorBox(`Could not load the lab data (${e.message || e}).`); return null; });
  if (!data || my !== token) return;
  $$("nav.primary a").forEach(a => a.setAttribute("aria-current", a.dataset.view === view ? "page" : "false"));
  document.body.dataset.view = view;
  document.title = view === "home" ? "Riven Lab" : `${TITLES[view]} · Riven Lab`;
  const ctx = { data, params, route: { view, params }, navigate };
  const sameView = current.view === view && current.mod;
  try {
    if (sameView && typeof current.mod.update === "function") { current.params = params; await current.mod.update(container, params, ctx); return; }
    if (current.mod && typeof current.mod.destroy === "function") { try { current.mod.destroy(); } catch (e) { console.warn(`${current.view}.destroy failed`, e); } }
    if (!sameView) window.scrollTo(0, 0);
    container.innerHTML = "";
    let mod = mods.get(view);
    if (!mod) { mod = await import(`./${view}.js`); mods.set(view, mod); }
    if (my !== token) return;
    current = { view, params, mod };
    await mod.render(container, params, ctx);
  } catch (e) {
    console.error(e);
    container.innerHTML = errorBox(`This screen failed to render: ${e.message || e}`) + `<p class="small muted" style="margin-top:12px"><a href="#/home">Back to home</a></p>`;
  }
}

installCardPreview();
window.addEventListener("hashchange", route);
loadData().then(d => { const m = $("#foot-meta"); if (m) m.textContent = `engine ${d.engine_commit} · data ${fmtDateTime(d.generated_utc)}`; }).catch(() => {});
route();
