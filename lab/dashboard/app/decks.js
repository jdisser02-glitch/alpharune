/* Riven Lab — Decks view.
   TODO redesign: see DESIGN.md. This stub mounts the previous implementation (legacy.js) unchanged;
   replace it with a module that builds the screen from lib.js components. Router contract: render(container, params, ctx) / destroy().
   params.name selects a deck path (#/decks/<path>). */
import { mount, unmount } from "./legacy.js";

export function render(container, params) { mount("decks", container, params); }
export function destroy() { unmount("decks"); }
