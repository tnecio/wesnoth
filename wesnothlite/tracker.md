# Issue tracker

This file contains a list of the highest priority issues affecting gameplay experience as of right now.
When you start working on an issue, move it from "OPEN" to "IN PROGRESS". Once done, move it into "PENDING VERIFICATION" (and add a note describing how the issue was addressed) and start working on another issue, rinse and repeat.
I will verify if the issue is appropriately addressed; if not I will move it back to OPEN with a note describing what's missing. If the issue is fixed I will move it to DONE.

Try to address issues one-by-one, updating the tracker after each is handled. This way I can verify some of the fixes while you are working on the next ones.

### OPEN

- Narration:
  - Missing background images in `story` parts (the background is black)

- Navigation — start new campaign after Back button:
  - Root cause: `launch_game_thread` called `e.game_thread.join()` while the game thread was blocked in `wait_for_command()` (waiting on `cmd_cv`), but `set_done()` only notifies `ev_cv`. The join deadlocked, preventing any new campaign from starting.
  - Fix: `launch_game_thread` now sends `WL_CMD_QUIT` through the channel when `channel->game_waiting` is true. The game thread receives it in `play_human_turn`, posts `WL_OK`, and throws `quit_game_exception()`. The game thread then exits, allowing `join()` to complete.
  - Issue: After starting one campaign, and clicking Back to Menu at the Victory screen, the next campaign fails with `Uncaught TypeError: can't access property "instanceCount", geometry is null`

- Engine:
  - After finishing the scenario, the console logs this warning for each unit that remained alive at the end: `[Game] reconcileUnits: removing unit wl_test_leader not seen in snapshot (expected UNIT_DIE)`

- Messages/Objectives:
  - When starting a scenario, the `message` tags from the `start` event in WML do not appear. Similarily the pop-up with scenario objectives also does not appear.

- Animation:
  - Enemy units' move animation is missing during their turn.
  - Combat animation is missing.

- Map:
  - Terrain display should be resolved using code from terrain_layers.cpp


### IN PROGRESS

### PENDING VERIFICATION

### DONE

- UI — MenuBar overlays top of sidebar:
  - Root cause: `.sidebar` had `grid-row: 1 / 3` (spanning both rows) while `MenuBar` also claimed row 1 with `grid-column: 1 / 3` and `z-index: 1`, visually covering the sidebar's top portion.
  - Fix: changed `.sidebar` to `grid-row: 2` only. The MenuBar already spans the full width in row 1.

- Navigation — Back button:
  - Root cause: `returnToMenu()` didn't reset `loadProgress` (so the loading bar re-appeared) and didn't invalidate the in-flight FULL_STATE from the worker. When the FULL_STATE arrived after `returnToMenu()`, `applyState()` switched the page back to `'game'`.
  - Fix: Introduced a `_sessionId` / `_activeSessionId` counter. Each new game session (startCampaign, startScenario) increments `_sessionId` and syncs `_activeSessionId`. `returnToMenu()` only increments `_sessionId`, creating a mismatch that causes LOAD_PROGRESS, FULL_STATE, INCREMENTAL_UPDATE, and CHOICE_NEEDED messages from the old session to be silently ignored. Also clears `loadProgress`, `pendingState`, `eventQueue` in `returnToMenu()`.

- Narration:
  - Root cause: Two bugs combined. (1) Events were dropped before GamePage mounted — fixed by draining NARRATIVE at GameController level. (2) `WL_EVENT` enum constants in `protocol.ts` were off by 2 starting from position 9 — `UNIT_RECRUIT` and `UNIT_RECALL` (9, 10) don't exist in C++; C++ has `UNIT_SPAWN=9` and `UNIT_DISMISS=10`. This shifted `STORY=18` in C++ to match `MESSAGE=18` in JS, causing the background image path to appear as text.
  - Fix: Updated `WL_EVENT` constants in `protocol.ts` to exactly match `WL_EventType` in `wesnothlite.h`. Updated `_decodeEvent` in `wasm-api.ts` to use `UNIT_SPAWN` (no separate UNIT_RECALL case). Modified `wl_lua.cpp` to resolve the background image path using `filesystem::get_binary_file_location("images", ...)` so JS gets an absolute path it can convert to a URL. Rebuilt WASM. Also replaced the small MessageModal panel for story variant with a full-screen background-image slide with text overlay at the bottom, matching the reference screenshot.

- Savegames — empty buffer on save:
  - Root cause: `ingame_savegame` was created directly from the API thread with `*engine->impl->state`, which holds the initial scenario config but NOT a live game snapshot. `get_starting_point().validate_wml()` or the lack of a snapshot caused the save to fail silently, writing nothing to disk. A previous fix also had `launch_game_thread` joining the old game thread while it was blocked in `wait_for_command()`, causing a deadlock that prevented any subsequent campaign start.
  - Fix: (1) Added `WL_CMD_SAVE` (value 8) — `wl_save_to_buffer` now sends this command through the channel. The game thread handles it in `WLController::process_command` by calling `get_saved_game().set_snapshot(to_config())` (same as `play_controller::save_game_auto`), then runs `ingame_savegame::save_game_automatic`. The API thread reads the file back. (2) Added `WL_CMD_QUIT` (value 9) — `launch_game_thread` checks `channel->game_waiting` and sends WL_CMD_QUIT before `set_done()`/`join()` so the blocked game thread exits cleanly.

- Progression:
  - Root cause: `SCENARIO_END` events were received by the controller but ignored — `drain()` passed them to `game.handleEvent` which has no case for it. The `GAME_OVER` StateUpdate was never sent from the worker.
  - Fix: Added `SCENARIO_END` handling in `GameController.drain()` — sets `gameOver` with winner + nextScenario. Extended `gameOver` state to include `nextScenario`. Added `continueToNextScenario()` that calls `START_SCENARIO` with the stored campaign/difficulty from `gameState.game`. Updated `GameOverModal` to show a "Continue Campaign" button when there is a next scenario.

- Engine warning — unit not seen in snapshot:
  - Root cause: Same `WL_EVENT` enum mismatch. C++'s `UNIT_DIE=11` was decoded as `UNIT_DISMISS=11` in JS (which returns null), so die events were dropped. C++'s `UNIT_ADVANCE=12` was decoded as `UNIT_DIE=12`, causing advancing units to be incorrectly removed.
  - Fix: Enum mismatch corrected (see Narration fix above) — `UNIT_DIE` now correctly matches C++'s value 11.
