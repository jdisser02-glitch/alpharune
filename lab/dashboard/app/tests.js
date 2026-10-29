/* Riven Lab — Tests view.
   TODO redesign: see DESIGN.md. This stub mounts the previous implementation (legacy.js) unchanged;
   replace it with a module that builds the screen from lib.js components. Router contract: render(container, params, ctx) / destroy().
   params.id selects a test (#/tests/<id>). */
import { mount, unmount } from "./legacy.js";

export function render(container, params) { mount("tests", container, params); }
export function destroy() { unmount("tests"); }
