/* Riven Lab — Replays view.
   TODO redesign: see DESIGN.md. This stub mounts the previous implementation (legacy.js) unchanged;
   replace it with a module that builds the screen from lib.js components. Router contract: render(container, params, ctx) / destroy().
   params.file loads a replay (#/replays/<file>), ?test=<id> filters the list. */
import { mount, unmount } from "./legacy.js";

export function render(container, params) { mount("replays", container, params); }
export function destroy() { unmount("replays"); }
