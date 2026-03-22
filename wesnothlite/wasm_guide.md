# WesnothLite WASM Build and Usage Guide

## Overview

The WASM build compiles `wesnothlite` — the C headless-engine API — into a
self-contained WebAssembly module.  The output is two files:

| File | Size (Release) | Purpose |
|------|---------------|---------|
| `wesnothlite.js` | ~110 KB | Emscripten JS glue + inlined pthread worker |
| `wesnothlite.wasm` | ~8 MB | WebAssembly binary |

Both files must be served from the same origin.  The JS file doubles as its own
pthread worker script; no separate worker file is emitted.

---

## 1. Prerequisites

### 1.1 Emscripten SDK (emsdk)

Version **5.0.3** is the tested version.

```bash
git clone https://github.com/emscripten-core/emsdk.git /emsdk
cd /emsdk
./emsdk install 5.0.3
./emsdk activate 5.0.3
```

Activate the environment in every shell that will run the build:

```bash
source /emsdk/emsdk_env.sh
```

### 1.2 vcpkg with the `wasm32-emscripten` triplet

vcpkg provides compiled Boost libraries for WASM (Emscripten's own Boost port is
headers-only).

```bash
git clone https://github.com/microsoft/vcpkg.git /usr/local/vcpkg
/usr/local/vcpkg/bootstrap-vcpkg.sh
```

Set `VCPKG_ROOT` so the configure script can find vcpkg:

```bash
export VCPKG_ROOT=/usr/local/vcpkg
```

Install the required Boost components.  Run from any directory outside the
wesnoth repo (to avoid vcpkg manifest-mode picking up `vcpkg.json`):

```bash
cd /tmp
$VCPKG_ROOT/vcpkg install \
    boost-filesystem \
    boost-iostreams \
    boost-locale \
    boost-random \
    boost-system \
    boost-thread \
    boost-bimap \
    boost-circular-buffer \
    boost-logic \
    boost-ptr-container \
    boost-math \
    boost-format \
    boost-asio \
    boost-config \
    boost-program-options \
    boost-regex \
    boost-graph \
    --triplet wasm32-emscripten
```

Note: `boost-coroutine` is intentionally excluded — it is not supported on
Emscripten, and the network code that uses it is not compiled into wesnothlite.
The `CMakeLists.txt` already skips `coroutine` when `EMSCRIPTEN` is set.

This takes 10–20 minutes the first time; the result is cached in
`$VCPKG_ROOT/installed/wasm32-emscripten/`.

### 1.3 CMake ≥ 3.20

Standard system cmake is fine.  The build uses the Emscripten CMake toolchain
which is bundled with emsdk.

### 1.4 Wesnoth data

The engine loads WML data at runtime; the source tree's `data/` directory is
used directly for local testing.  No separate install step is required.

---

## 2. Building

### 2.1 Configure

```bash
cd /wesnoth_wl
export VCPKG_ROOT=/usr/local/vcpkg   # adjust if vcpkg is elsewhere
source /emsdk/emsdk_env.sh           # activate Emscripten in current shell
bash wesnothlite/configure_wasm.sh   # runs cmake configure into build-wasm/
```

`configure_wasm.sh` passes the following key flags:

| CMake flag | Purpose |
|-----------|---------|
| `CMAKE_TOOLCHAIN_FILE` | vcpkg toolchain (chains to Emscripten) |
| `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` | Emscripten toolchain |
| `VCPKG_TARGET_TRIPLET=wasm32-emscripten` | vcpkg Boost for WASM |
| `VCPKG_MANIFEST_MODE=OFF` | use the pre-installed vcpkg packages |
| `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH` | required for `find_package(Boost)` to locate vcpkg-installed headers via HINTS |
| `ENABLE_GAME/SERVER/TESTS/TOOLS=OFF` | build only `wesnothlite` |

Extra arguments after the script name are forwarded to cmake (e.g.,
`bash configure_wasm.sh -DCMAKE_BUILD_TYPE=Debug`).

### 2.2 Build

```bash
cmake --build build-wasm --target wesnothlite -j4
```

Use `-j4` or fewer jobs to avoid OOM in memory-constrained environments.

Output lands in `build-wasm/`:

```
build-wasm/
  wesnothlite.js    # JS glue
  wesnothlite.wasm  # WASM binary
```

### 2.3 Rebuild after source changes

Re-running the build command is sufficient; cmake detects changed files.
If you change `CMakeLists.txt` options, re-run `configure_wasm.sh` first.

---

## 3. Threading note

The game loop runs on a `std::thread`.  Emscripten implements this using
`SharedArrayBuffer` + Web Workers.  Consequences:

- **Node.js** (v18+): works out of the box — Node supports `SharedArrayBuffer`
  without special headers.
- **Browser**: the page must be served with two HTTP response headers:
  ```
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp
  ```
  Most CDN and dev-server configurations support adding these headers.
  Without them the browser disables `SharedArrayBuffer` and the engine will
  crash on `wl_start_campaign`.

---

## 4. Loading the module

### 4.1 Node.js

```js
// CommonJS
const factory = require('./wesnothlite.js');

// The factory is async when pthread is enabled; await it.
const Module = await factory({
    noInitialRun: true,
    // Tell the module where to find wesnothlite.wasm
    locateFile: (filename) => `/path/to/build-wasm/${filename}`,
    onRuntimeInitialized() {
        // Called when the WASM heap and runtime are ready.
        // Mount game data before calling wl_init.
        const NODEFS = this.FS.filesystems['NODEFS'];

        // Mount the wesnoth repo root; pass the data/ subpath to wl_init.
        this.FS.mkdir('/game');
        this.FS.mount(NODEFS, { root: '/path/to/wesnoth' }, '/game');

        this.FS.mkdir('/userdata');
        this.FS.mount(NODEFS, { root: '/tmp/wl_userdata' }, '/userdata');

        // Then: wl_init('/game/data', '/userdata')
    },
});
```

`locateFile` must point to the directory containing `wesnothlite.wasm`.  When
the JS and WASM files are in the same directory as the script, omit it and rely
on the default (same directory as `wesnothlite.js`).

### 4.2 Browser (ES module)

```html
<!-- Must be served with COOP + COEP headers (see section 3) -->
<script type="module">
import WesnothLite from './wesnothlite.js';

const Module = await WesnothLite({
    noInitialRun: true,
    onRuntimeInitialized() {
        // Mount data via a Fetch-backed FS or preload files here.
        // See section 6 for the Fetch FS approach.
    },
});
</script>
```

### 4.3 Wrapping the C API

All `wl_*` functions are exported as `_wl_*` on the module object, but calling
them via `cwrap` is more ergonomic and handles argument marshalling:

```js
const wl_init = Module.cwrap('wl_init', 'number', ['string', 'string']);
const wl_shutdown = Module.cwrap('wl_shutdown', null, ['number']);
const wl_last_error = Module.cwrap('wl_last_error', 'string', ['number']);
const wl_list_campaigns = Module.cwrap('wl_list_campaigns', 'number', ['number']);
const wl_start_campaign = Module.cwrap('wl_start_campaign', 'number',
    ['number', 'string', 'string']);
const wl_step = Module.cwrap('wl_step', 'number', ['number']);
const wl_move = Module.cwrap('wl_move', 'number', ['number', 'number', 'number']);
const wl_attack = Module.cwrap('wl_attack', 'number',
    ['number', 'number', 'number', 'number']);
const wl_recruit = Module.cwrap('wl_recruit', 'number', ['number', 'string', 'number']);
const wl_end_turn = Module.cwrap('wl_end_turn', 'number', ['number']);
const wl_choose = Module.cwrap('wl_choose', 'number', ['number', 'number']);
const wl_free = Module.cwrap('wl_free', null, ['number']);
```

`WL_Loc` is passed as a single `i32` = `(y << 16) | x` (see `wesnothlite.h`).

---

## 5. Automated gameplay test

`wesnothlite/test_gameplay.mjs` is a ready-made integration test that exercises
all core gameplay actions.  Run it from the repo root after building:

```bash
node wesnothlite/test_gameplay.mjs
```

It covers 26 checks across 10 phases:

| Phase | What is tested |
|-------|----------------|
| 1 | `wl_init` — engine initialises successfully |
| 2 | `wl_list_campaigns` — 18 campaigns returned |
| 3 | `wl_start_campaign("Two_Brothers", "EASY")` |
| 4 | Event pump: SCENARIO\_START → WAITING\_FOR\_INPUT (turn 1, side 1) |
| 5 | `wl_query_units` / `wl_query_team` — leader, movable units, gold |
| 6 | `wl_move` — unit moved, UNIT\_MOVE event emitted |
| 7 | `wl_recruit` — Bowman recruited, UNIT\_RECRUIT event emitted |
| 8 | `wl_attack` probe (enemies out of range on turn 1 — skipped) |
| 9 | `wl_end_turn` — AI plays its turn, engine returns to WAITING\_FOR\_INPUT turn 2 |
| 10 | `wl_attack` probe on turn 2 (reports closest enemy distance) |

The test allocates `WL_Loc` structs in WASM heap via `Module._malloc` /
`Module._free` and uses proper Wesnoth staggered-hex adjacency for
attack-pair detection.  Exit code is 0 on full pass, 1 on any failure.

---

## 5b. Manual testing with Node.js

The following self-contained script verifies the basic lifecycle: init →
list campaigns → start a scenario → pump events to the first input prompt.

Save it anywhere and run with `node test.mjs` (from the `build-wasm/` directory,
or adjust the paths).

```js
// test.mjs
import { createRequire } from 'module';
const require = createRequire(import.meta.url);

// ── 1. Load module ─────────────────────────────────────────────────────────
const factory = require('./wesnothlite.js');       // adjust path as needed
const WESNOTH_ROOT = '/path/to/wesnoth';           // repo root (parent of data/)
const USER_DATA    = '/tmp/wl_userdata';

import { mkdirSync } from 'fs';
mkdirSync(USER_DATA, { recursive: true });

const m = await new Promise((resolve, reject) => {
    factory({
        noInitialRun: true,
        onRuntimeInitialized() { resolve(this); },
    }).catch?.(reject);
});

// ── 2. Mount filesystem ────────────────────────────────────────────────────
// Mount the repo root so that /wesnoth/data/ is accessible as /game/data/.
// wl_init receives the data/ subdirectory; it detects cores.cfg there and
// resolves the game root to the parent directory automatically.
const NODEFS = m.FS.filesystems['NODEFS'];
m.FS.mkdir('/game');    m.FS.mount(NODEFS, { root: WESNOTH_ROOT }, '/game');
m.FS.mkdir('/userdata'); m.FS.mount(NODEFS, { root: USER_DATA  }, '/userdata');

// ── 3. Bind API ────────────────────────────────────────────────────────────
const wl_init            = m.cwrap('wl_init',            'number', ['string','string']);
const wl_shutdown        = m.cwrap('wl_shutdown',        null,     ['number']);
const wl_last_error      = m.cwrap('wl_last_error',      'string', ['number']);
const wl_list_campaigns  = m.cwrap('wl_list_campaigns',  'number', ['number']);
const wl_start_campaign  = m.cwrap('wl_start_campaign',  'number', ['number','string','string']);
const wl_step            = m.cwrap('wl_step',            'number', ['number']);
const wl_free            = m.cwrap('wl_free',            null,     ['number']);

// ── 4. Init engine ─────────────────────────────────────────────────────────
// wl_init expects the path to the data/ subdirectory (the one that contains
// cores.cfg and _main.cfg).  It detects cores.cfg there and resolves the
// parent as the game root — so you pass /game/data, not /game.
const engine = wl_init('/game/data', '/userdata');
const initErr = wl_last_error(engine);
if (initErr) { console.error('wl_init failed:', initErr); process.exit(1); }
console.log('wl_init OK');

// ── 5. List campaigns ──────────────────────────────────────────────────────
// WL_CampaignList layout (wasm32): { WL_CampaignInfo* campaigns; int count; }
// Pointer = 4 bytes, int = 4 bytes.
const listPtr = wl_list_campaigns(engine);
const count   = m.getValue(listPtr + 4, 'i32');    // count is at offset 4
console.log(`wl_list_campaigns: ${count} campaigns`);

// WL_CampaignInfo layout: 5 char* + char*[8] + int + char* = 15×4 = 60 bytes
const campaignsPtr = m.getValue(listPtr, 'i32');
for (let i = 0; i < Math.min(count, 5); i++) {
    const base = campaignsPtr + i * 60;
    const id   = m.UTF8ToString(m.getValue(base +  0, 'i32'));
    const name = m.UTF8ToString(m.getValue(base +  4, 'i32'));
    console.log(`  [${i}] ${id} — ${name}`);
}
wl_free(listPtr);

// ── 6. Start a campaign ────────────────────────────────────────────────────
const EVENT_NAMES = [
    'SCENARIO_START','SCENARIO_END','TURN_START','SIDE_TURN_START',
    'SIDE_TURN_END','WAITING_FOR_INPUT','UNIT_MOVE','UNIT_ATTACK',
    'UNIT_RECRUIT','UNIT_RECALL','UNIT_DISMISS','UNIT_DIE','UNIT_ADVANCE',
    'UNIT_XP','UNIT_HEAL','UNIT_STATUS','VILLAGE_CAPTURE','MESSAGE',
    'STORY','OBJECTIVES_UPDATE','CHOICE_NEEDED','SOUND','MUSIC_CHANGE',
];

// Two_Brothers is a short two-scenario campaign — good for quick tests.
const status = wl_start_campaign(engine, 'Two_Brothers', 'EASY');
if (status !== 0) {
    console.error('wl_start_campaign failed:', wl_last_error(engine));
    process.exit(1);
}
console.log('wl_start_campaign OK');

// ── 7. Pump events until WAITING_FOR_INPUT ────────────────────────────────
for (let i = 0; i < 50; i++) {
    const evPtr = wl_step(engine);
    if (!evPtr) { console.log('null event'); break; }
    const type = m.getValue(evPtr, 'i32');
    const name = EVENT_NAMES[type] ?? `UNKNOWN(${type})`;
    console.log('event:', name);
    if (name === 'WAITING_FOR_INPUT') {
        console.log('\nPASS — game is ready for player input');
        break;
    }
}

// ── 8. Cleanup ─────────────────────────────────────────────────────────────
wl_shutdown(engine);
console.log('wl_shutdown OK');
```

Expected output:

```
wl_init OK
wl_list_campaigns: 18 campaigns
  [0] Dead_Water — Dead Water
  [1] Delfadors_Memoirs — Delfador's Memoirs
  ...
wl_start_campaign OK
event: SCENARIO_START
event: WAITING_FOR_INPUT

PASS — game is ready for player input
wl_shutdown OK
```

---

## 6. Browser data mounting

Browsers have no native filesystem.  Two approaches:

### 6.1 Preloaded files (small datasets / testing)

Emscripten's `--preload-file` embed files directly into the JS bundle at build
time.  Not practical for the full Wesnoth data tree (~200 MB), but useful for
small custom scenarios.

### 6.2 Fetch-backed FS (production)

Register a custom FS that transparently fetches files from a CDN on first
access.  Skeleton:

```js
// fetchfs.js — minimal lazy-fetch FS for Emscripten
function registerFetchFS(FS, baseUrl) {
    FS.mkdir('/data');
    // Register a custom FS that proxies read() to fetch().
    // Full implementation: see Emscripten FS API docs.
    // Simplest viable approach: override FS.open / FS.read.
}
```

A complete implementation is outside the scope of this guide; the Emscripten
documentation covers `FS.registerFS` and the `WORKERFS` API in detail.

### 6.3 IDBFS for user data (saves + preferences)

Mount IndexedDB-backed storage at the userdata path so saves persist across
page reloads:

```js
Module.onRuntimeInitialized = function() {
    this.FS.mkdir('/userdata');
    this.FS.mount(this.FS.filesystems['IDBFS'], {}, '/userdata');
    // Restore persisted state from IndexedDB.
    this.FS.syncfs(true, (err) => {
        if (err) console.error('syncfs restore failed:', err);
        startGame(this);
    });
};

function flushUserdata(FS) {
    // Call after wl_save() to push changes to IndexedDB.
    FS.syncfs(false, (err) => {
        if (err) console.error('syncfs flush failed:', err);
    });
}
```

`FS.syncfs(true, cb)` populates the in-memory FS from IndexedDB on startup;
`FS.syncfs(false, cb)` flushes in-memory writes back to IndexedDB.  Call the
flush after every `wl_save()`.

---

## 7. Localisation

Translated strings are loaded from `.mo` files using the standard gettext layout:

```
<translations_path>/
  pl/LC_MESSAGES/wesnoth.mo
  pl/LC_MESSAGES/wesnoth-lib.mo
  pl/LC_MESSAGES/wesnoth-tutorial.mo
  …
```

The translations directory is **not** bundled into the engine — it is treated as an asset, just like `data/`.  Pass it explicitly to `wl_set_locale`.

### Native (`wl-cli`)

```sh
wl-cli --data=/path/to/data \
       --locale=pl_PL \
       --translations=/path/to/translations \
       --campaign=Two_Brothers
```

`--translations` defaults to `<data>/../translations`, which matches the Wesnoth source-tree layout (`data/` and `translations/` are siblings).

### WASM / JavaScript

Mount the translations directory into the VFS and then call `wl_set_locale`:

```js
const wl_set_locale = m.cwrap('wl_set_locale', 'number',
                               ['number', 'string', 'string']);

// Mount translations alongside data
FS.mkdir('/translations');
FS.mount(NODEFS, { root: '/path/to/translations' }, '/translations');

const engine = wl_init('/game/data', '/userdata');
wl_set_locale(engine, 'pl_PL', '/translations');
```

Pass `null` for either argument to use the default (system locale / `<data>/../translations`).

---

## 8. Complete API reference

See `wesnothlite/wesnothlite.h` for struct definitions and full documentation.
The exported functions are:

| Function | Description |
|----------|-------------|
| `wl_init(data_path, userdata_path)` | Load game data, return engine handle |
| `wl_shutdown(engine)` | Tear down engine and free memory |
| `wl_last_error(engine)` | Return last error string (empty = no error) |
| `wl_set_locale(engine, locale, translations_path)` | Set locale and translations directory (call after `wl_init`, before starting a scenario) |
| `wl_list_campaigns(engine)` | Return `WL_CampaignList*` with all playable campaigns |
| `wl_start_campaign(engine, id, difficulty)` | Start a campaign from its first scenario |
| `wl_start_scenario(engine, campaign_id, scenario_id, difficulty)` | Start at a specific scenario |
| `wl_load_save(engine, path)` | Load a savegame from the VFS |
| `wl_load_from_buffer(engine, buf, size)` | Load a savegame from a byte buffer |
| `wl_save(engine, path)` | Write current game state to the VFS |
| `wl_save_to_buffer(engine, &size)` | Serialise current game state to a heap buffer |
| `wl_step(engine)` | Advance game state; return next `WL_Event*` |
| `wl_move(engine, from, to)` | Move unit |
| `wl_attack(engine, attacker, defender, weapon_index)` | Attack with specified weapon |
| `wl_recruit(engine, unit_type_id, at)` | Recruit a unit |
| `wl_recall(engine, unit_id, at)` | Recall a unit from the recall list |
| `wl_dismiss(engine, unit_id)` | Dismiss a unit from the recall list |
| `wl_end_turn(engine)` | End the current side's turn |
| `wl_choose(engine, option_index)` | Respond to a `WL_EVENT_CHOICE_NEEDED` event |
| `wl_undo(engine)` | Undo last action |
| `wl_query_game(engine)` | Current scenario metadata |
| `wl_query_map(engine)` | Full terrain and village data |
| `wl_query_visibility(engine, side)` | Fog-of-war map for a side |
| `wl_query_units(engine)` | All units on the map |
| `wl_query_unit_at(engine, loc)` | Unit at a specific hex |
| `wl_query_unit_type(engine, type_id)` | Static unit type data |
| `wl_query_recall_list(engine, side)` | A side's recall list |
| `wl_query_recruit_list(engine, side)` | A side's available recruits |
| `wl_query_team(engine, side)` | Team state (gold, income, …) |
| `wl_query_reach(engine, loc)` | Movement reach from a hex |
| `wl_query_attack_options(engine, attacker, defender)` | All valid attack matchups |
| `wl_free(ptr)` | Free any pointer returned by a `wl_query_*` function |
