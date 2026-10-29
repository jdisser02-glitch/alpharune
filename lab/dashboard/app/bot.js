/* Riven Lab — Bot view.
   TODO redesign: see DESIGN.md. This stub mounts the previous implementation (legacy.js) unchanged;
   replace it with a module that builds the screen from lib.js components. Router contract: render(container, params, ctx) / destroy().
   no params. */
import { mount, unmount } from "./legacy.js";

export function render(container, params) { mount("bot", container, params); }
export function destroy() { unmount("bot"); }
