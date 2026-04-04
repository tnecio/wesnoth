# Assessment: Wesnothlite vs. Full SDL/WASM Port

Two parallel approaches exist to port Wesnoth to the browser. This document assesses their relative viability across four dimensions: remaining effort, UI quality, long-term maintainability, and mobile-friendliness.

---

## Approach A — Wesnothlite (headless C++ engine + Svelte UI)

The C++ engine is exposed through a versioned C API (`wl_init`, `wl_step`, `wl_send`, `wl_query_*`). A Web Worker runs the engine and communicates game state to the main thread via typed messages (`protocol.ts`). The Svelte UI renders the game board on a canvas and uses HTML for menus, dialogs, and sidebars.

**What is complete:**
- All core gameplay commands: move, attack, recruit, recall, end turn, undo, choose option
- All state queries: map, units, teams, visibility, reach, attack options, recruit/recall lists
- All event types: unit movement/combat/death/advance, turn flow, narrative, village capture
- Save/load via serialised buffers; IDBFS-backed persistent savegames
- UI screens: campaign picker, game board, unit infobox, combat modal, recruit/recall panels, message/narrative modal, game-over screen, save/load manager

**What remains:**
- End-of-scenario stats/report screen
- Multiplayer / hotseat turn-handoff UI (engine supports N sides; UI assumes human vs. AI only)
- Audio: `SOUND` and `MUSIC_CHANGE` events are delivered but not acted on (Web Audio API work needed)
- Visual animations: move, attack, death — events are delivered; canvas playback not yet implemented
- Autosave browser / savegame management beyond the current basic save/load
- Edge-case pump events: `STORY`, `SCREEN_OVERLAY`, `SCROLL` (stubs; mostly cutscenes)
- Known stub: `specials_desc` is empty for attack specials (bitmask only)

All remaining items are standard web-development work. There are no unknown infrastructure blockers.

---

## Approach B — Full SDL/WASM (desktop Wesnoth compiled to WASM)

The full desktop Wesnoth binary is compiled with Emscripten. SDL2 drives rendering (WebGL via OFFSCREEN_FRAMEBUFFER), audio (SDL_mixer), and input. A minimal Svelte shell (`WasmFullShell.svelte`) bootstraps the module and shows a canvas.

**Current build flags of note:**
- `-sPROXY_TO_PTHREAD=1 -sOFFSCREEN_FRAMEBUFFER=1`: game runs in a Web Worker; GL calls proxied to main thread
- `-sUSE_FREETYPE=1 -sUSE_HARFBUZZ=1`: compiled in but not yet wired
- `-sMALLOC=emmalloc`: chosen for alignment guarantees (prevents atomic traps on Firefox)

**What is working:**
- WASM module loads; `main()` reaches game initialisation
- Virtual filesystem mounts; WML data bundle unpacks
- Game thread starts in a worker

**What is blocked (in order):**

| Phase | Blocker | Severity |
|-------|---------|----------|
| 1 — Rendering | `OFFSCREEN_FRAMEBUFFER` + GL proxy path: crashes in `emscripten_thread_mailbox_ref` (unaligned atomic in `em_task_queue_send → GLES2_RunCommandQueue`) on certain Emscripten builds | Hard crash; no workaround yet identified |
| 2 — Text | `text_wasm.cpp` is a complete stub; all text is blank. Pango/Cairo (used by desktop Wesnoth) does not exist in Emscripten. Must build a full FreeType + HarfBuzz text shaping + rasterisation pipeline from scratch to replace it | Large, open-ended implementation task |
| 3 — Input | SDL event loop expects mouse/keyboard events; browser delivers DOM events. No bridge exists yet | Moderate, but well-understood |
| 4 — Audio | `SDL_mixer` compiled in; `Mix_OpenAudio` path untested; requires AudioContext unlock gesture and correct thread proxying | Moderate, unknown interactions with ASYNCIFY |
| 5 — Nested event loops | Wesnoth uses nested `while(true) + SDL_WaitEvent` stacks for dialogs, animations, and multiplayer turns. ASYNCIFY must instrument every one; a single un-instrumented path causes a silent hang | High risk; cannot be validated until Phases 1–2 are unblocked |

---

## 1. Effort remaining to reach a working product

**Wesnothlite:** The remaining work is large but entirely finite and predictable. Each item is a self-contained web-development task with clear inputs and outputs. No unknown infrastructure dependencies.

Rough order of magnitude:
- Audio integration (Web Audio API): small
- Move/attack/death animations on canvas: medium
- Multiplayer/hotseat: medium
- Edge-case pump events (story screens, overlays): small–medium

**Full SDL/WASM:** The remaining work is open-ended. Phase 1 (rendering) is currently blocked by a crash inside Emscripten's own library. Phases 2–5 are sequential dependencies: text cannot be tested until rendering works, nested event loops cannot be validated until input works. Each phase may surface new Emscripten-internal issues.

**Verdict:** Wesnothlite has a clear, closeable backlog. The SDL/WASM port has an unknown backlog behind a stack of blocked phases.

---

## 2. User interface: performance, native feel, asset reuse

| | Wesnothlite | Full SDL/WASM |
|---|---|---|
| Rendering performance | Game logic in worker; main thread free for smooth UI. No risk of game tick blocking animation. | If ASYNCIFY works: game on main thread or proxied via OFFSCREEN_FRAMEBUFFER. Acceptable for a turn-based game, but no budget isolation. |
| Native web feel | HTML/CSS buttons, focus rings, tab navigation, screen readers, device-pixel-ratio scaling — all free | Frozen SDL window in a canvas. No browser affordances. Looks like a 2003 desktop app in a tab. |
| Asset reuse — sprites & tiles | Full: served directly as images from the existing bundle | Full: identical to desktop |
| Asset reuse — UI chrome | Partial: buttons/panels reimplemented in Svelte; sprites/portraits reused | Complete: identical to desktop |
| Typography | Browser-native text engine: crisp, DPI-aware, correctly hinted | Blank (Phase 2 not done). When done: FreeType rasterisation, potentially lower quality on HiDPI than browser text. |
| Animation fidelity | Custom canvas animations; can match desktop behaviour | Identical to desktop (if rendering works) |

**Verdict:** Full SDL/WASM wins on asset fidelity (zero UI reimplementation). Wesnothlite wins on everything that matters to a web user: legibility, accessibility, responsiveness, and actually working text.

---

## 3. Long-term maintainability as Wesnoth evolves

**Wesnothlite:**

The C API (`wesnothlite.h`) is an explicit contract. Upstream Wesnoth can be updated, recompiled, and redeployed as a new WASM binary; the JS side is unaffected unless the API contract changes.

Maintenance burden:
- A new game mechanic visible in the UI requires: a new `wl_query_*` or `wl_send` variant, a new protocol message type, and a UI component. This is explicit, proportional, and testable at the boundary.
- Risk: API drift — if C++ internals restructure in a way that makes existing `wl_*` functions silently incorrect, the UI receives wrong data. Requires integration tests at the API boundary.
- WASM binary is small (~1 MB WASM + ~3 MB data); incremental updates are cheap.

**Full SDL/WASM:**

In theory: recompile and redeploy. In practice:

- Any upstream change that adds new threading patterns, new SDL features, new OpenGL usage, or new platform-specific code can break the WASM port silently.
- Emscripten must stay in sync. The custom vcpkg triplet (`wasm32-emscripten-pthreads`) and all linker flags require re-validation on each Emscripten major version bump (which changes system library ABIs and JS glue).
- The current crash is _inside Emscripten's own library_. When Emscripten is updated, that crash may reappear or mutate unpredictably.
- WASM binary in release mode: estimated 40–80 MB. Every deployment requires a full re-download.

**Verdict:** Wesnothlite's maintenance burden is explicit and proportional to feature additions. Full SDL/WASM's maintenance burden is unpredictable and gated on external dependency stability (Emscripten, SDL, vcpkg).

---

## 4. Impact on mobile-friendly interface plans

**Wesnothlite:**

Mobile support is architecturally natural:
- Swipe-to-pan and pinch-to-zoom are standard canvas touch event handlers.
- Bottom-sheet dialogs, tap-to-confirm interactions, and adaptive portrait/landscape layouts are all standard Svelte/CSS work.
- Worker isolation means smooth touch-driven animation on the main thread even during long game-logic ticks.
- PWA support (installable, offline-capable) is straightforward with a service worker.
- Binary size is small; acceptable on mobile data.

**Full SDL/WASM:**

Mobile support is structurally incompatible with the current approach:
- SDL assumes mouse + keyboard. Wesnoth's C++ input layer assumes hover states and right-click context menus. None of these translate to touch without deep C++ modifications to Wesnoth itself.
- Canvas renders at a fixed SDL resolution. On a 390×844 phone it either renders tiny (unusable) or requires SDL viewport scaling (blurry).
- Text is blank. A mobile user sees sprites with no labels.
- SDL touch event bridging in Emscripten is untested in this codebase.
- A 40–80 MB release binary is unacceptable on mobile data connections.

Adding real mobile support to approach B would require modifying Wesnoth's C++ input handling for touch, building a responsive canvas scaling layer, and resolving all the above blockers first — essentially reimplementing most of what Wesnothlite provides, on top of existing WASM complexity.

**Verdict:** Wesnothlite supports mobile as a first-class target. Full SDL/WASM is structurally incompatible with mobile without major additional work.

---

## Overall recommendation

Wesnothlite is the practical path to a shippable, mobile-friendly, maintainable browser port. Its remaining work is large but entirely predictable standard web development.

The Full SDL/WASM port is valuable as a proof-of-concept and for desktop browser users who want the authentic pixel-perfect Wesnoth experience. But it should not be the primary investment path: it is currently blocked on Emscripten internals, has no viable path to mobile, carries unpredictable long-term maintenance risk, and has no working text rendering.

A reasonable strategy: continue Wesnothlite as the primary target, and keep the SDL/WASM branch as a secondary experiment to revisit once the Emscripten threading situation (specifically the `OFFSCREEN_FRAMEBUFFER` + GLES2 proxy crash) is resolved upstream.
