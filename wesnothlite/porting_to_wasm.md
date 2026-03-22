# Porting libwesnothlite to WebAssembly

## Overview

The goal is to compile `libwesnothlite` — the C API wrapper around the Wesnoth headless engine — as a WebAssembly module. This would let a JavaScript/TypeScript host load and run Wesnoth game sessions in a browser or Node.js environment, driving turns through the `wesnothlite.h` API.

The module's dependency chain is:

```
wesnothlite.cpp
  └─ wesnoth-headless (static library, ~250 source files)
       ├─ ICU (Unicode support)
       ├─ Boost (filesystem, locale, iostreams, program_options, regex, random, system)
       ├─ Lua (embedded, compiled from source)
       └─ SDL2 (headers only — just SDL_GetTicks / SDL_events)
```

Note: OpenSSL does not appear in this chain — see §1 below.

## Available Tooling

| Tool | Version | Location |
|------|---------|----------|
| Emscripten SDK | 5.0.3 | `/emsdk` |
| vcpkg | 2026-03-04 | `/vcpkg` (has `wasm32-emscripten` community triplet) |

Emscripten provides official ports for: ICU, zlib, bzip2, Boost headers (1.83.0, header-only), SDL2, freetype, harfbuzz, and others. Crucially, **compiled Boost libraries are not Emscripten ports** — only headers.

vcpkg's `wasm32-emscripten` triplet can cross-compile many packages, including the compiled Boost libraries we need.

---

## Dependency Analysis

### 1. OpenSSL — **Remove from headless entirely**

**Currently used in**: `hash.cpp` (MD5 via EVP), `preferences/credentials.cpp` (AES-256-CBC for stored passwords).

**Assessment**: The headless build never supports multiplayer login, so neither use case belongs in `wesnoth-headless` regardless of platform. Rather than gating behind `#ifdef __EMSCRIPTEN__`, remove the OpenSSL dependency from the headless static library unconditionally:

- **`hash.cpp` MD5**: Replace the OpenSSL EVP path with a self-contained ~80-line MD5 implementation. The Apple `CommonCrypto` path already exists; add a third branch (not `__APPLE__` and not needing OpenSSL at all) that uses the standalone impl. The non-headless builds (game, server) continue to use OpenSSL normally.
- **`credentials.cpp`**: Wrap the full implementation in `#ifndef HEADLESS_ENGINE` (already defined for the headless static lib) and provide no-op stubs in the `#else` branch. The symbol `php_crypt_blowfish_rn` used by `bcrypt::hash_pw` is already stubbed in `misc_stubs.cpp`.

**CMake change**: Remove `OpenSSL::Crypto` and `OpenSSL::SSL` from `target_link_libraries(wesnoth-headless …)`. The `find_package(OpenSSL)` in the root `CMakeLists.txt` remains (still needed for the game and server targets); just don't link it into the headless library.

This also simplifies the regular (non-WASM) headless build.

---

### 2. ICU — **Emscripten Port**

**Used in**: `gettext.cpp`, `serialization/unicode.cpp`, `serialization/unicode_cast.cpp`, `commandline_options.cpp`, and many others. ICU provides UTF-8/Unicode support that is pervasive throughout the WML parser and AI.

**Assessment**: Emscripten's `icu` port (`-sUSE_ICU=1`) builds ICU for WASM and is proven to work. This is the cleanest solution — no source changes needed.

**Plan**:
- Pass `-sUSE_ICU=1` as a compile+link flag when `EMSCRIPTEN` is set in CMakeLists.
- Skip `find_package(ICU)` and instead let the Emscripten port supply headers and libraries.
- The ICU build (~80 seconds the first time, cached after) happens automatically.

---

### 3. Boost Compiled Libraries

This is the central challenge. The headless build links against 7 compiled Boost components. Emscripten only provides Boost headers, not compiled libraries.

vcpkg's `wasm32-emscripten` triplet can build compiled Boost for WASM. This is the recommended path for most components. Some lightweight ones can be replaced with C++17/C++11 standard library equivalents.

#### 3a. `boost::filesystem` — **Replace with `std::filesystem`**

Used in `filesystem.cpp` heavily. C++17 `std::filesystem` has essentially the same API as Boost.Filesystem v3 (it was standardised from it). Emscripten's libc++ implements `std::filesystem` on top of POSIX, which Emscripten provides.

Key API differences to handle:
- `bfs::regular_file` → `std::filesystem::file_type::regular`
- `bfs::directory_file` → `std::filesystem::file_type::directory`
- `bfs::last_write_time()` returns `time_t` in Boost, `file_time_type` in std — needs a conversion shim
- `boost::system::error_code` → `std::error_code`
- `boost::filesystem::path::imbue()` — Windows-only, not needed on WASM
- `boost::iostreams::file_descriptor_source/sink` — used in `istream_file` / `ostream_file` for buffered I/O; replace with `std::ifstream` / `std::ofstream`
- `boost::process::search_path` in `get_exe_dir()` — guard with `#ifndef __EMSCRIPTEN__`

**Plan**: Create a compatibility header at `src/compat/boost_filesystem_wasm.hpp` that maps the `boost::filesystem` namespace to `std::filesystem` plus the necessary shims. Include it from `filesystem.cpp` under `#ifdef __EMSCRIPTEN__` instead of the real Boost headers. The `src/compat/` location is deliberate — this is a real, working adapter, not a stub.

#### 3b. `boost::locale` — **vcpkg build**

Used in `gettext.cpp` extensively: locale generation, message format, `bl::to_lower`, `bl::as::ftime`, `bl::info` facet. The `gettext.hpp` public header also exposes `const boost::locale::info&`, making a pure stub complex.

`boost::locale` with the ICU backend delegates to ICU (which we already have via Emscripten port). vcpkg's `boost-locale` port should build for `wasm32-emscripten` with the ICU feature enabled.

**Alternative**: If vcpkg's boost-locale WASM build proves unreliable, stub `gettext.cpp` with an English-only locale that returns strings unchanged. The headless engine needs text to work but doesn't strictly need i18n to be functional.

**Plan**: Try vcpkg first. Fall back to stub if the build fails.

#### 3c. `boost::program_options` — **Stub**

Used only in `commandline_options.cpp`. WASM has no command line — the `commandline_options` struct is populated via the `wl_open` API parameters.

**Plan**: Wrap `commandline_options.cpp` implementation in `#ifndef __EMSCRIPTEN__`, provide a stub `commandline_options` constructor that sets safe defaults (data path, userdata path, etc.) from the `wl_open` call.

#### 3d. `boost::iostreams` — **vcpkg build (with zlib/bzip2)**

Used in:
- `serialization/binary_or_text.cpp` — gzip/bzip2 compression for writing
- `serialization/parser.cpp` — decompression when reading `.gz` / `.bz2` WML files
- `config_cache.cpp` — caching with compression
- `log.cpp` — tee-ing log output to a file
- `savegame.cpp`, `save_index.cpp` — save file compression

The `log.cpp` usage (tee-ing to a log file) can be guarded with `#ifndef __EMSCRIPTEN__` trivially.

For the serialization and save paths: standard Wesnoth campaign data files are gzip-compressed. Supporting decompression is essential for loading any stock campaign. Both `zlib` and `bzip2` are available as Emscripten ports and as vcpkg packages.

**Plan**: Build `boost-iostreams` via vcpkg with `zlib` and `bzip2` features. Guard `log.cpp` tee with `#ifndef __EMSCRIPTEN__`.

#### 3e. `boost::random` (random_device) — **Replace with `std::random_device`**

Used in `random.cpp` and `seed_rng.cpp` only as `boost::random_device`. The original comment warns against `std::random_device` on MinGW, but Emscripten's `std::random_device` is backed by `/dev/urandom` via Emscripten's POSIX layer and is fine.

**Plan**: Guard with `#ifndef __EMSCRIPTEN__` / `#else std::random_device` in both files.

#### 3f. `boost::regex` — **Replace with `std::regex`**

Used in exactly one headless file: `ai/composite/component.cpp`, for parsing AI `[modify_ai]` paths. C++11 `std::regex` and `std::sregex_token_iterator` are drop-in replacements.

**Plan**: Guard with `#ifdef __EMSCRIPTEN__` aliasing `std::regex` into the `boost` namespace for that file.

#### 3g. `boost::system` — **Comes for free**

`boost::system::error_code` is used in `filesystem.cpp` but will be replaced by `std::error_code` as part of the `std::filesystem` migration (3a above).

---

### 4. SDL2 — **Emscripten Port (headers + timer only)**

The headless engine uses SDL2 for:
- `SDL_GetTicks()` — millisecond timing in AI and game state initialization
- `SDL_events.h` — included in `src/stubs/events.cpp`
- `SDL_GetPrefPath()` — iOS-only preference path, guarded by iOS define

SDL2 is **not linked** into `wesnoth-headless` — only headers are needed at compile time. Emscripten's SDL2 port (`-sUSE_SDL=2`) provides the headers and implements `SDL_GetTicks()` via `emscripten_get_now()`.

**Plan**: Pass `-sUSE_SDL=2` for WASM builds. Since SDL2 `find_package` is gated on `ENABLE_GAME OR ENABLE_TESTS` (both OFF for our WASM build), explicitly add the SDL2 include path from the Emscripten port cache when configuring the headless target.

---

### 5. Lua — **Compiles Cleanly**

Lua is embedded from source (under `src/modules/lua/`). It is pure C, and emcc handles C files correctly. No changes expected.

---

## WASM-Specific Architectural Challenges

### Challenge 1: SHARED Library → Standalone WASM Module

Emscripten does not support conventional dynamic linking (`.so` files). The CMakeLists.txt currently builds `wesnothlite` as a `SHARED` library (CMake already warns about this for WASM targets). The agreed approach is a **standalone WASM module**: everything is statically linked into a single `.wasm` + `.js` glue pair. The host calls into the module via Emscripten's `cwrap`/`ccall` or a hand-written JS wrapper.

All public functions from `wesnothlite.h` must be marked `EMSCRIPTEN_KEEPALIVE` (or listed in `-sEXPORTED_FUNCTIONS`) to prevent the dead-code eliminator from removing them.

In CMakeLists, for the WASM target:
```cmake
if(EMSCRIPTEN)
    # Build as an "executable" (Emscripten produces .js + .wasm)
    set_target_properties(wesnothlite PROPERTIES SUFFIX ".js")
    target_link_options(wesnothlite PRIVATE
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=256MB"
        "-sMAXIMUM_MEMORY=1GB"
        "-sDISABLE_EXCEPTION_CATCHING=0"
    )
endif()
```

### Challenge 2: Virtual Filesystem

The WASM module needs to read WML data files (campaigns, core data). The primary deployment target is the **browser**, which has no native filesystem access.

**For browser deployment**: Use a **Fetch-backed custom FS** that lazily fetches files from a CDN or game server on demand. Emscripten provides `FS.registerFS` and the `WORKERFS`/custom FS APIs. This is the only practical approach for production — the Wesnoth data directory is ~200MB, ruling out preloading the entire tree.

**For local/Node.js testing**: Mount a real directory with **NODEFS**. This is straightforward and sufficient for development iteration:
```js
EM_ASM({
    FS.mkdir('/data');
    FS.mount(NODEFS, { root: '/path/to/wesnoth/data' }, '/data');
});
```

The plan is: implement with NODEFS first for testing, then add the Fetch-backed FS for browser delivery as a separate step. The game engine code itself needs no changes for this — it just calls `filesystem::*` which routes through Emscripten's POSIX layer.

### Challenge 3: Persistence — Savegames and Preferences

Savegames and user preferences need to survive across WASM module restarts. Two viable approaches:

**Option A: IDBFS** — Emscripten can mount an IndexedDB-backed filesystem at the userdata path. Writes go through the normal `ostream_file` path; Emscripten's IDBFS syncs to IndexedDB. Simple from the engine side, but IndexedDB writes are asynchronous and require explicit `FS.syncfs()` calls to flush.

**Option B: `wl_save_to_buffer` / `wl_load_from_buffer`** — Delegate persistence entirely to JS. The WASM module serializes game state to a buffer, the JS host stores it wherever it wants (IndexedDB, localStorage, a server). This gives the JS host full control and avoids IDBFS sync complexity. Both functions are currently stubbed in `wesnothlite.cpp`.

Both options should be implemented. Option A for preferences (lightweight, fire-and-forget), Option B for savegames (gives JS control over the save slot UI and remote sync).

### Challenge 4: Memory

The Wesnoth engine loads substantial amounts of data:
- Core WML data (~50MB loaded text)
- Campaign data
- Unit type and terrain databases

Emscripten's default initial WASM heap is 16MB. We need:
- `-sINITIAL_MEMORY=256MB` (minimum viable)
- `-sALLOW_MEMORY_GROWTH=1` (to handle larger maps/campaigns)
- `-sMAXIMUM_MEMORY=1GB` (as ceiling)

This is acceptable in modern browsers (WASM supports up to 4GB).

### Challenge 5: Threading

The headless engine uses `std::mutex` in `log.cpp`. Emscripten supports `std::mutex` when compiled with `-pthread`, but pthread in WASM requires `SharedArrayBuffer`, which needs specific HTTP headers (`Cross-Origin-Opener-Policy`, `Cross-Origin-Embedder-Policy`).

Since `wesnoth-headless` only uses a single mutex (for log output), we can compile without `-pthread` and use a no-op mutex — the log mutex is not critical for correctness in a single-threaded environment. Add `-pthread` support later if truly needed.

### Challenge 6: Exception Handling

Emscripten by default disables C++ exceptions (`-fignore-exceptions`). Wesnoth uses exceptions extensively: `game_end_exception`, `quit_game_exception`, `wml_exception`, synced context throws, etc.

**Plan**: Enable exception handling with `-sDISABLE_EXCEPTION_CATCHING=0` (equivalently `-fexceptions`). This is already included in the CMake snippet above. The overhead is acceptable.

### Challenge 7: `commandline_options` Initialization

`wesnothlite.cpp` constructs `commandline_options` to initialize the engine. For WASM, this needs to be constructible without program_options parsing. A stub sets `data_path`, `userdata_path`, etc. directly from the `wl_open()` parameters (see §3c above).

---

## `wl_choose` — Synced User Choice

`wl_choose` is **not** out of scope — there are no WASM-specific obstacles to implementing it. It is a pre-existing TODO in the core `wesnothlite` implementation.

`wl_choose` is the mechanism for resolving in-game decisions that require player input during a synced game action:
- **Unit advancement**: when a unit levels up, the player picks from a list of unit type options.
- **WML `[message]` with `[option]` children**: dialog choices scripted in campaign WML.
- **WML-driven recruit overrides**: rare but used in some campaigns.

When the engine needs a choice, it is expected to post a `WL_EVENT_CHOICE_NEEDED` event and block further commands until the host calls `wl_choose(engine, option_index)`, which returns `WL_ERR_BLOCKED` to any other command in the interim.

The implementation requires hooking into Wesnoth's `synced_context` user-choice mechanism. The engine side calls `synced_context::get_user_choice` which currently has no wesnothlite integration. The hook point is in `wesnothlite.cpp`'s game thread: intercept user-choice requests, post the event to the channel, and block until `wl_choose` delivers the selected index.

This should be implemented as part of the initial port so that single-player campaigns with level-ups work correctly.

---

## Implementation Plan

### Phase 1: CMake WASM Configuration

1. Remove OpenSSL from `target_link_libraries(wesnoth-headless …)` (also benefits the non-WASM build).
2. Add WASM-specific CMake block:
   ```cmake
   if(EMSCRIPTEN)
       # Use Emscripten ports for ICU, SDL2, zlib, bzip2
       set(EMSCRIPTEN_PORT_FLAGS "-sUSE_ICU=1 -sUSE_SDL=2 -sUSE_ZLIB=1 -sUSE_BZIP2=1")
       string(APPEND CMAKE_CXX_FLAGS " ${EMSCRIPTEN_PORT_FLAGS}")
       string(APPEND CMAKE_EXE_LINKER_FLAGS " ${EMSCRIPTEN_PORT_FLAGS}")
       # Skip find_package for OpenSSL, ICU, SDL2, Boost (vcpkg supplies Boost)
   endif()
   ```
3. Configure the build directory:
   ```bash
   source /emsdk/emsdk_env.sh
   cmake /wesnoth -B /wesnoth/build-wasm \
     -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
     -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake \
     -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
     -DENABLE_GAME=OFF -DENABLE_SERVER=OFF \
     -DENABLE_CAMPAIGN_SERVER=OFF -DENABLE_TESTS=OFF \
     -DCMAKE_BUILD_TYPE=Release
   ```
4. Build target: `wesnothlite` → produces `wesnothlite.js` + `wesnothlite.wasm`.

### Phase 2: Source Changes (in dependency order)

1. **OpenSSL removal**: standalone MD5 in `hash.cpp` (all platforms, headless only) + `HEADLESS_ENGINE` guard in `credentials.cpp`.
2. **`boost::random` stubs**: `random.cpp` + `seed_rng.cpp` → `std::random_device`.
3. **`boost::regex` stub**: `ai/composite/component.cpp` → `std::regex`.
4. **`boost::filesystem` compat**: `src/compat/boost_filesystem_wasm.hpp` adapter + `filesystem.cpp` `#ifdef __EMSCRIPTEN__` switch.
5. **`log.cpp` iostreams guard**: tee functionality guarded with `#ifndef __EMSCRIPTEN__`.
6. **`commandline_options` stub**: WASM-mode constructor using `wl_open` parameters.
7. **`wl_choose` implementation**: hook into `synced_context` user-choice mechanism.
8. **`wl_save_to_buffer` / `wl_load_from_buffer`**: implement save serialization to/from a caller-supplied buffer.
9. **`EMSCRIPTEN_KEEPALIVE`**: mark all public `wesnothlite.h` functions.
10. **JS glue**: `wesnothlite.js` — thin async wrapper exposing the C API with Promise-based event polling.

### Phase 3: vcpkg Packages for WASM

Install via vcpkg with the `wasm32-emscripten` triplet:
- `boost-locale` (with `icu` feature — delegates to the ICU Emscripten port)
- `boost-iostreams` (with `zlib` and `bzip2` features)
- `boost-filesystem` + `boost-program-options` (for header compatibility only; runtime uses std::filesystem / stubs)

Use Emscripten ports (handled automatically via `-s` flags):
- ICU, SDL2, zlib, bzip2

### Phase 4: Testing

1. Build and run with Node.js + NODEFS for data files. Drive a Two Brothers playthrough via the C API, verify event stream including `WL_EVENT_CHOICE_NEEDED` on level-up.
2. Measure binary size and memory usage.
3. Implement Fetch-backed FS and test in-browser.
4. Implement IDBFS for preferences persistence.

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| `boost-locale` fails to build for wasm32-emscripten via vcpkg | Medium | Medium | Fall back to English-only locale stub |
| `boost-iostreams` WASM build fails (zlib/bzip2 linking) | Low | Medium | Use Emscripten zlib/bzip2 ports directly |
| Memory exhaustion loading large campaigns | Medium | High | `ALLOW_MEMORY_GROWTH` + lazy Fetch-backed FS |
| Exception handling overhead too large | Low | Low | Profile; modern emcc handles `-fexceptions` well |
| `std::filesystem` missing functionality vs boost | Low | Low | The subset used in `filesystem.cpp` is well-covered by C++17 |
| SDL_GetTicks → emscripten_get_now precision differences | Low | Low | Minor timing differences; acceptable |
| IDBFS sync timing issues with savegames | Medium | Low | Use `wl_save_to_buffer` path instead |

---

## Not In Scope (for initial port)

- Multiplayer networking
- Audio
- In-browser rendering/display
