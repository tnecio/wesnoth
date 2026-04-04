
Plan: Extract Wesnoth Headless Core Engine
==========================================

## Overview

The goal was to make Wesnoth's singleplayer engine embeddable — driveable from a web
frontend (or any other host) without a display, audio, or input system. The strategy:

1. Compile the real Wesnoth source into a shared library, subclassing rather than
   patching upstream files wherever possible
2. Expose a clean C ABI (`wesnothlite.h`) safe to call from C, C++, JS (Emscripten cwrap), Python, etc.
3. Drive the engine via a typed event queue (`wl_step`) and a command channel (`wl_send`)
4. Build a Svelte + Web Worker frontend that renders the game in the browser

This replaced the original plan's approach of a separate `src/stubs/` directory and a
`wesnoth-headless` CMake target. Instead, `libwesnothlite.so` links the full SDL-enabled
Wesnoth and runs headlessly by simply never opening a window or playing audio.

---

## What Was Built

### C engine library (`src/wesnothlite/`)

| File | Role |
|------|------|
| `wesnothlite.h` | Public C ABI — the only file external code includes |
| `wesnothlite.cpp` | Engine init/shutdown, `wl_step`, `wl_send`, `wl_query_*`, save/load |
| `wl_controller.cpp` | `WLController` subclasses `playsingle_controller`; overrides `play_human_turn()` to block on the command channel instead of the SDL event loop |
| `wl_channel.cpp` | Thread-safe queue connecting the game thread (producer of events, consumer of commands) to the API thread (consumer of events, producer of commands) |
| `wl_lua.cpp` | Lua hook injected into `game_lua_kernel` to intercept game events (unit moves, combat, messages, etc.) and post them as `WLEventInternal` structs |
| `wl_impl.hpp` | Internal types shared across the above files |
| `wl_cli.cpp` | Standalone CLI (`wl-cli`): feeds stdin commands, prints events — used for development and integration tests |

### C API surface (`wesnothlite.h`)

- **Lifecycle:** `wl_init(data_path, userdata_path, options)` → `WL_Engine*`, `wl_free_engine()`
- **Event pump:** `wl_step()` → `WL_Event*` (returns one event per call; NULL when waiting for input)
- **Commands:** `wl_send(engine, cmd)` — move, attack, recruit, recall, end_turn, undo, choose
- **State queries:** `wl_query_units()`, `wl_query_map()`, `wl_query_teams()`, `wl_query_reach()`,
  `wl_query_attack_options()`, `wl_query_recruit_list()`, `wl_query_recall_list()`
- **Save/load:** `wl_save_buffer()` / `wl_load_buffer()` (in-memory; no filesystem required in WASM)
- **Memory:** all snapshots are contiguous heap blocks freed with `wl_free()`
- **Options:** `wl_init` accepts space-separated Wesnoth CLI flags (e.g. `--rng-seed=42`)

### Event types delivered by `wl_step`

`WL_EVENT_LOADING_CONFIG`, `WL_EVENT_SCENARIO_START`, `WL_EVENT_WAITING_FOR_INPUT`,
`WL_EVENT_UNIT_MOVE`, `WL_EVENT_UNIT_SPAWN`, `WL_EVENT_UNIT_DIE`, `WL_EVENT_UNIT_ADVANCE`,
`WL_EVENT_COMBAT`, `WL_EVENT_VILLAGE_CAPTURE`, `WL_EVENT_TURN_START`, `WL_EVENT_TURN_END`,
`WL_EVENT_SIDE_DONE`, `WL_EVENT_NARRATIVE` (WML messages/dialogs), `WL_EVENT_SOUND`,
`WL_EVENT_MUSIC_CHANGE`, `WL_EVENT_SCENARIO_END`

### CMake targets

- `wesnothlite` — shared library (`libwesnothlite.so` / `.wasm`)
- `wl-cli` — native CLI binary for development and testing

### Svelte frontend (`frontend/src/`)

| Layer | What it does |
|-------|-------------|
| `engine/wasm-api.ts` | Loads the WASM module, wraps `wl_init/wl_step/wl_send/wl_query_*` via Emscripten `cwrap` |
| `engine/worker.ts` | Web Worker: runs the pump loop, translates `WL_Event` structs into typed `protocol.ts` messages, forwards commands from main thread |
| `engine/protocol.ts` | TypeScript discriminated union of all engine↔worker messages |
| `controller/` | Game controller: owns worker lifetime, drives the pump, manages pending command promises |
| `board/` | Canvas renderer: hex grid, unit sprites, terrain tiles, reach/attack overlays |
| `components/` | Recruit panel, recall panel, unit infobox, combat preview modal, narrative modal, save/load manager |
| `pages/` | Campaign picker, game screen, game-over screen |

All UI screens are complete and functional for singleplayer human-vs-AI campaigns.

### Test infrastructure

- `wl-cli` supports `--` separator: everything after `--` is forwarded to `wl_init` as options
  (e.g. `wl-cli --data=data --campaign=WL_Test -- --rng-seed=42`)
- `test_wl_cli.py` — YAML-driven integration test runner; run via `uv run --with pyyaml test_wl_cli.py <test.yaml>`
- `tests/test_basic.yaml` — end-to-end test: scenario start → units query → recruit → attack → victory
- YAML format supports `output` (strict consecutive match) and `output_match` (regex scan-forward match)

---

## What Remains

### Engine / API
- `specials_desc` in attack snapshots is empty (bitmask only; text names not yet populated)
- `WL_EVENT_STORY` / `WL_EVENT_SCREEN_OVERLAY` / `WL_EVENT_SCROLL` are delivered but contain
  minimal data (storyscreen text, overlay images, scroll targets not yet extracted)
- Multiplayer/hotseat: engine supports N human sides; untested beyond side=1

### Frontend
- **Audio:** `WL_EVENT_SOUND` and `WL_EVENT_MUSIC_CHANGE` are received in the worker but not
  acted on — needs Web Audio API integration
- **Animations:** move, attack, death events are delivered with full path/combat data but the
  canvas plays them back instantaneously — sprite animation not yet implemented
- **End-of-scenario report:** victory/defeat screen exists; the detailed unit stats report shown
  in desktop Wesnoth is not yet rendered
- **Autosave / savegame browser:** basic save/load works; no autosave on turn end, no save slot
  management UI beyond the current modal

### Testing
- Only one YAML test exists (`test_basic.yaml`); coverage of edge cases (ZOC, advancement
  dialogs, multi-scenario campaigns, fog/shroud) is absent
- No CI integration — tests are run manually
