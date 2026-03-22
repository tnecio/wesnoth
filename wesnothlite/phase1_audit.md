Phase 1 Audit — Build Surface Area & SDL Dependency Map
========================================================
Date: 2026-03-16

## 1. Build System Summary

CMake configure succeeds. Key external libraries pulled in by the `wesnoth` target:

  Keeper (we still need these):
    Boost (iostreams, program_options, regex, system, random, coroutine, locale, filesystem)
    ICU (uc, i18n, data)
    zlib                   -- via Boost.iostreams; needed for save compression
    Lua                    -- embedded submodule (modules/lua/)

  Drop (SDL/display/audio tier):
    SDL2, SDL2_image, SDL2_mixer
    pangocairo, pango, cairo, Fontconfig
    VorbisFile (audio)
    libcurl                -- only for addon/network downloads; can drop
    dbus-1                 -- desktop notifications only; drop
    readline/history       -- Lua REPL; drop
    X11                    -- pulled in by SDL; drops with it
    OpenSSL (Crypto, SSL)  -- only needed for MP/addon; can drop

Linker target breakdown:
  wesnoth-common  (static) -- libwesnoth_core sources; no SDL
  wesnoth-client  (static) -- libwesnoth + libwesnoth_sdl + wesnoth source list; SDL-heavy
  wesnoth-widgets (static) -- gui/ widget sources; SDL-heavy
  wesnoth         (exe)    -- links all three + external libs

Target for headless: replace wesnoth-client + wesnoth-widgets with
  wesnoth-headless (static) -- core + patched game logic + stubs; NO SDL link


## 2. SDL Gateway Headers

These headers include SDL directly and are included widely. They are the "contagion vectors."

  color.hpp          -> SDL2/SDL_pixels.h
                        Because: color_t : SDL_Color (just {r,g,b,a})
                        Fix: provide a minimal SDL_Color mock struct OR
                             patch color_t to not inherit SDL_Color.
                        Impact: color_t used throughout config/serialization/units — must fix.

  events.hpp         -> SDL2/SDL_events.h
                        Because: events::handler uses SDL_Event
                        Fix: stub events.hpp entirely; remove events::handler base from controller_base
                        Impact: controller_base inherits from it; must patch controller_base.

  video.hpp          -> SDL2/SDL_render.h
                        Because: SDL_Renderer*, SDL_Window* exposed
                        Fix: stub video.hpp; video::headless() always true; video::faked() always true
                        Impact: 10+ keeper files check video::headless()/faked() — stub returns true.

  sdl/surface.hpp    -> SDL2/SDL_surface.h    (rendering only; keeper files don't use surfaces)
  sdl/texture.hpp    -> SDL2/SDL_render.h     (rendering only)
  sdl/rect.hpp       -> SDL2/SDL_rect.h       (sdl::rect is used in display, theme, map/label)
  sdl/point.hpp      -> SDL2/SDL_rect.h       (sdl::point used in display)
  save_blocker.hpp   -> SDL2/SDL_mutex.h
                        Because: uses SDL_mutex for thread sync
                        Fix: replace SDL_mutex with std::mutex
  key.hpp            -> SDL2/SDL_keyboard.h   (keyboard input; fully stub)
  controller_base.hpp-> (via .hpp file itself referencing SDL through base class events::handler)
  theme.hpp          -> SDL2/SDL_rect.h       (SDL_Rect used for layout)
                        Fix: replace SDL_Rect with our own rect type or forward-declare
  map/label.hpp      -> SDL2/SDL_rect.h       (label screen position)
                        Fix: same as theme.hpp
  hotkey/command_executor.hpp -> SDL2/SDL_events.h (via events.hpp)
  mouse_handler_base.hpp      -> SDL2/SDL_events.h
  mouse_events.hpp            -> SDL2/SDL_events.h


## 3. The display_context Key Insight

display_context.hpp is FULLY SDL-FREE. It is a pure abstract C++ interface providing:
  virtual const std::vector<team>&    teams() const = 0;
  virtual const gamemap&              map()   const = 0;
  virtual const unit_map&             units() const = 0;
  + helper methods (village_owner, get_visible_unit, unit_orb_status, etc.)

display : public display_context (among others). display::get_singleton() returns display*.

Files that #include "display.hpp" just to call display::get_singleton()->get_units() or similar
only need display_context. We provide a headless_context singleton that:
  - implements display_context using game_board data
  - is assigned at startup: display_context* g_headless_ctx = new headless_context(board)
  - replaces display::get_singleton() calls with headless_context::get()

This covers: units/filter.cpp, units/abilities.cpp, units/unit.cpp,
             pathfind/pathfind.cpp, ai/default/recruitment.cpp, ai/formula/function_table.cpp


## 4. SDL_timer Usage (Trivial)

SDL_GetTicks() is used ONLY for profiling log output and animation timing.

Files and usage:
  game_state.cpp         -- LOG_NG timing (profiling only)
  play_controller.cpp    -- LOG_NG timing (profiling only); ticks_ member init
  playsingle_controller.cpp -- LOG_NG timing; SDL_Delay(10) in idle loop
  ai/default/ca.cpp      -- SDL_timer for move timing
  ai/manager.cpp         -- SDL_timer for AI timing
  animated.cpp           -- animation frame timing (file will be stubbed anyway)
  scripting/lua_mathx.cpp -- SDL_GetTicks exposed to Lua
  draw_manager.cpp       -- frame timing (file will be stubbed anyway)

Fix for keeper files: replace SDL_GetTicks() with a headless_ticks() function that returns
  std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count()
Replace SDL_Delay(10) with no-op (headless loop doesn't idle-spin).


## 5. Complete File Classification

### 5a. STUB ENTIRELY — Pure display/audio/input (implement as no-op + stdout)

These files have zero useful headless logic. Create one stub per file providing
the same symbol signatures but doing nothing (or printing to stdout).

  Rendering/display:
    display.cpp / display.hpp        -- headless_display stub implements display_context
    game_display.cpp / .hpp          -- inherits headless_display; all methods no-op
    draw.cpp / draw.hpp              -- no-op
    draw_manager.cpp / .hpp          -- no-op
    halo.cpp / halo.hpp              -- no-op
    cursor.cpp / cursor.hpp          -- no-op
    floating_label.cpp / .hpp        -- no-op
    minimap.cpp / minimap.hpp        -- no-op
    picture.cpp / picture.hpp        -- no-op (image cache)
    image_modifications.cpp          -- no-op
    video.cpp / video.hpp            -- headless(): true, faked(): true, all else no-op

  Unit display (no game logic, pure animation):
    units/animation.cpp / .hpp       -- no-op
    units/animation_component.cpp / .hpp -- no-op
    units/drawer.cpp / .hpp          -- no-op
    units/udisplay.cpp / .hpp        -- no-op (move_unit, reset_helpers, etc. all no-op)
    units/frame.cpp / .hpp           -- no-op

  Audio:
    sound.cpp / sound.hpp            -- all calls print [SOUND]/[MUSIC] to stdout
    sound_music_track.cpp / .hpp     -- stdout stub
    soundsource.cpp / .hpp           -- no-op (positional audio)
    scripting/lua_audio.cpp          -- wesnoth.play_sound() -> stdout

  Input/events:
    events.cpp / events.hpp          -- stub; pump() = no-op; no SDL event loop
    key.cpp / key.hpp                -- no-op
    sdl/input.cpp                    -- no-op
    sdl/surface.cpp / .hpp           -- empty surface type
    sdl/texture.cpp / .hpp           -- empty texture type
    sdl/window.cpp / .hpp            -- no-op
    sdl/exception.cpp / .hpp         -- keep (used for error handling) or stub
    filesystem_sdl.cpp               -- stub (SDL_RWops file ops; use plain fstream instead)

  GUI/widgets/help:
    gui/**                           -- all stubs; dialogs print to stdout, read from stdin
    widgets/*.cpp                    -- no-op stubs
    help/*.cpp                       -- no-op stubs (help browser is display-only)
    font/*.cpp                       -- stub; no Pango/SDL_ttf
    theme.cpp / theme.hpp            -- stub; load WML section but no rendering layout
    hotkey/*.cpp (handler files)     -- no-op stubs
    tooltips.cpp / .hpp              -- no-op
    show_dialog.cpp / .hpp           -- print to stdout + read from stdin
    mouse_handler_base.cpp / .hpp    -- no-op (no mouse in headless)
    mouse_events.cpp / .hpp          -- no-op

  Story/editor/addons/desktop/MP-only:
    storyscreen/*.cpp                -- text to stdout; no rendering
    editor/**                        -- fully excluded from build
    addon/**                         -- fully excluded
    desktop/**                       -- fully excluded (notifications, clipboard, etc.)
    whiteboard/**                    -- fully excluded
    chat*.cpp                        -- fully excluded
    display_chat_manager.cpp         -- stub (no-op)
    scripting/application_lua_kernel.cpp -- may need partial keep for plugin system; evaluate
    scripting/plugins/**             -- fully excluded

### 5b. KEEPER FILES NEEDING SURGICAL PATCHING

Each entry lists what needs removing/replacing. SDL call count in parens.

  game_state.cpp (1)
    - Remove #include <SDL2/SDL_timer.h>
    - Replace SDL_GetTicks() with headless_ticks() in LOG_NG calls

  play_controller.cpp (9)
    - Remove: floating_label.hpp, preferences/display.hpp, display_chat_manager.hpp
    - Remove: sound.hpp, soundsource.hpp, video.hpp
    - Remove: mouse_handler_, menu_handler_, whiteboard_manager_ members
    - Remove: help::manager, hotkey::command_executor base, events::observer base
    - Remove: tooltips::manager member
    - Replace SDL_GetTicks() with headless_ticks()
    - Keep: game_state init, turn sequencing, event pump integration, AI invocation

  playsingle_controller.cpp (12)
    - Remove: sound.hpp, soundsource.hpp, video.hpp, display_chat_manager.hpp
    - Remove: storyscreen calls -> text to stdout
    - Remove: whiteboard references
    - Replace SDL_Delay(10) -> no-op
    - Replace SDL_GetTicks() -> headless_ticks()
    - Replace victory/defeat GUI dialogs -> [VICTORY]/[DEFEAT] to stdout
    - Replace "waiting for human input" GUI loop -> run_stdin_turn() call
    - Keep: single-player turn flow, AI invocation, scenario end logic

  controller_base.cpp (7)
    - Remove: #include "display.hpp", "soundsource.hpp", "video.hpp"
    - Remove: events::handler base class (SDL event polling)
    - Keep: base game loop logic (thin shell after removal)

  synced_user_choice.cpp (2)
    - Remove: display.hpp, floating_label.hpp, font/standard_colors.hpp
    - Replace GUI choice wait -> check choice_fn callback; fallback to stdout prompt + stdin read

  synced_commands.cpp (2)
    - Remove: units/udisplay.hpp, font/standard_colors.hpp
    - Replace unit_display calls -> no-op

  replay.cpp (2)
    - Remove: game_display.hpp, display_chat_manager.hpp
    - Replace chat/display calls -> no-op or stdout

  saved_game.cpp (1)
    - Remove: cursor.hpp
    - Remove cursor::setter usages (no-op in headless)

  savegame.cpp (2)
    - Remove: cursor.hpp, video.hpp
    - Replace gui2::show_save_dialog -> [PROMPT:yesno] to stdout, read from stdin
    - Replace cursor::setter -> no-op

  game_config_manager.cpp (4)
    - Remove: cursor.hpp, picture.hpp, sound.hpp, theme.hpp
    - Replace loading screen progress -> [LOG] to stdout
    - Replace cursor calls -> no-op
    - Replace sound preload -> no-op
    - Replace picture preload -> no-op
    - Keep: WML loading, preprocessing, config caching

  actions/attack.cpp (7)
    - Remove: units/udisplay.hpp
    - Replace unit_display::unit_attack() -> no-op
    - Keep: hit/miss calculation, HP modification, kill logic

  actions/move.cpp (8)
    - Remove: units/udisplay.hpp, font/standard_colors.hpp
    - Replace unit_display::move_unit() -> no-op
    - Keep: movement logic, ZOC, vision/shroud updates

  actions/advancement.cpp (4)
    - Remove: units/udisplay.hpp, video.hpp
    - Replace display advancement calls -> [LOG] advancement to stdout
    - Replace video::faked() check -> always true in headless
    - Keep: advancement logic, experience check

  actions/heal.cpp (1)
    - Remove: units/udisplay.hpp
    - Replace unit_display::unit_healing() -> no-op

  actions/create.cpp (2)
    - Remove: display.hpp, units/udisplay.hpp
    - Replace display calls -> no-op
    - Keep: unit placement logic

  actions/shroud_clearing_action.cpp (1)
    - Remove: units/udisplay.hpp
    - Replace display calls -> no-op

  actions/undo.cpp / undo_move_action.cpp / undo_recall_action.cpp / undo_recruit_action.cpp (2-3 each)
    - Remove: game_display.hpp, units/udisplay.hpp
    - Replace display/animation calls -> no-op
    - Keep: undo state restoration logic

  actions/unit_creator.cpp (2)
    - Remove: display.hpp, units/udisplay.hpp
    - Replace display calls -> no-op

  game_events/action_wml.cpp (3)
    - Remove: game_display.hpp, soundsource.hpp, units/udisplay.hpp
    - Replace game_display::new_turn() -> no-op
    - Replace unit_display::move_unit() -> no-op
    - Replace soundsource add/remove -> no-op
    - Replace display::reload_map() -> no-op
    - Keep: all WML action logic ([move], [kill], [teleport], [store_unit], etc.)

  game_events/pump.cpp (6)
    - Remove: video.hpp
    - Replace video::faked() checks -> always true / always headless

  game_events/handlers.cpp (1)
    - Remove: sound.hpp
    - Replace sound::play_*() -> [SOUND] to stdout

  game_initialization/playcampaign.cpp (2)
    - Remove: sound.hpp, video.hpp
    - Replace sound calls -> no-op
    - Replace video calls -> no-op

  playturn.cpp (7)
    - Remove: game_display.hpp
    - Replace display calls -> no-op
    - Keep: turn processing logic

  countdown_clock.cpp (2)
    - Remove: sound.hpp
    - Replace timer sound -> no-op

  pathfind/pathfind.cpp (1)
    - Remove: display.hpp
    - Replace display::get_singleton() -> headless_context::get()
    - Keep: pathfinding algorithms

  ai/contexts.cpp (1)
    - Remove: game_display.hpp, display_chat_manager.hpp
    - Replace chat message display -> [LOG] to stdout
    - Keep: all AI context logic

  ai/default/recruitment.cpp (1)
    - Remove: display.hpp
    - Replace display::get_singleton()->labels() -> no-op label calls
    - Keep: all recruitment calculation logic

  ai/formula/ai.cpp (1)
    - Remove: game_display.hpp, display_chat_manager.hpp
    - Replace chat message -> [LOG] to stdout
    - Keep: formula AI logic

  ai/formula/function_table.cpp (1)
    - Remove: display.hpp
    - Replace display::get_singleton() -> headless_context::get()
    - Keep: formula functions

  ai/lua/core.cpp (1)
    - Remove: game_display.hpp
    - Replace display-using calls -> no-op
    - Keep: Lua AI core

  scripting/game_lua_kernel.cpp (9)
    - Remove: game_display.hpp, display_chat_manager.hpp, floating_label.hpp
    - Remove: sound.hpp, soundsource.hpp, units/udisplay.hpp, video.hpp
    - Remove: SDL_timer.h
    - Replace game_display calls -> no-op or headless stubs
    - Replace sound calls -> [SOUND] to stdout
    - Replace video::faked() -> true
    - Replace SDL_GetTicks -> headless_ticks()
    - Keep: all game logic Lua bindings (units, map, events, teams, etc.)

  scripting/lua_common.cpp (2)
    - Remove: game_display.hpp
    - Replace display calls -> no-op

  scripting/lua_team.cpp (2)
    - Remove: game_display.hpp
    - Replace display calls -> no-op

  scripting/lua_fileops.cpp (1)
    - Remove: picture.hpp
    - Replace image path resolution -> stub returning empty path

  scripting/lua_mathx.cpp (1)
    - Remove: SDL_timer.h
    - Replace SDL_GetTicks() -> headless_ticks()

  units/unit.cpp (1)
    - Remove: display.hpp
    - Replace display::get_singleton() call -> headless_context::get()

  units/filter.cpp (2)
    - Remove: display.hpp, display_context.hpp (keep display_context.hpp)
    - Replace display::get_singleton() -> headless_context::get()

  units/abilities.cpp (3)
    - Remove: display.hpp, font/text_formatting.hpp
    - Replace display::get_singleton() -> headless_context::get()
    - Replace font::format calls -> plain strings

  formula/function.cpp (7)
    - Remove: game_display.hpp
    - Replace float_label -> [LOG] to stdout
    - Replace get_chat_manager().add_chat_message -> [LOG] to stdout
    - Keep: formula functions

  formula/debugger.cpp (1)
    - Remove: game_display.hpp
    - Replace display null check -> always-null path

### 5c. FILES EXCLUDED ENTIRELY FROM HEADLESS BUILD (no stubs needed)

  editor/**           -- map editor; 80+ files; no useful headless logic
  addon/**            -- add-on client/manager; network-only
  whiteboard/**       -- planning overlay; display-only feature
  server/**           -- game server and campaign server
  storyscreen/**      -- will be text-stubbed, not fully excluded
  gui/**              -- 130+ GUI files; entirely excluded (stubs replace symbols)
  playmp_controller.cpp
  playturn_network_adapter.cpp
  syncmp_handler.cpp
  wesnothd_connection.cpp
  network_asio.cpp
  network_download_file.cpp
  tls_root_store.cpp
  mp_game_settings.cpp
  mp_ui_alerts.cpp
  game_initialization/connect_engine.cpp
  game_initialization/create_engine.cpp   (evaluate: may have useful SP logic)
  game_initialization/lobby_data.cpp
  game_initialization/lobby_info.cpp
  game_initialization/multiplayer.cpp
  game_initialization/depcheck.cpp
  chat_command_handler.cpp
  chat_events.cpp
  chat_log.cpp
  display_chat_manager.cpp   (excluded; calls go to [LOG] in keeper files)
  about.cpp                  (achievements display only)
  achievements.cpp           (display only)
  build_info.cpp             (startup info dialog)
  game_launcher.cpp          (main menu; replaced by headless launcher)
  hotkey/hotkey_handler.cpp  (UI key bindings; excluded)
  hotkey/hotkey_handler_mp.cpp
  hotkey/hotkey_handler_sp.cpp
  preferences/display.cpp    (display preferences; excluded)
  preferences/lobby.cpp
  preferences/advanced.cpp
  preferences/editor.cpp
  menu_events.cpp            (menu bar actions; excluded)
  floating_textbox.cpp       (in-game text input widget; excluded)
  desktop/**                 (OS desktop integration; excluded)
  fake_unit_manager.cpp      (EVALUATE: used in whiteboard; may be needed)
  sdl/point.cpp              (keep: point math; no SDL calls)
  sdl/utils.cpp              (evaluate)
  scripting/plugins/**       (plugin system for external tools; excluded)
  scripting/application_lua_kernel.cpp  (app-level Lua; evaluate if game_lua_kernel is enough)


## 6. Patching Approach for color_t / SDL_Color

color_t inherits SDL_Color which is struct { uint8_t r, g, b, a; }.
color_t is used in config attribute values, unit attributes, label colors — it is pervasive.

Recommended fix: add a thin compatibility header in src/headless/sdl_color_mock.hpp:
  #pragma once
  struct SDL_Color { unsigned char r, g, b, a; };
  constexpr unsigned char SDL_ALPHA_OPAQUE = 255;

Then in the headless build, make SDL2/SDL_pixels.h resolve to this mock.
Approach: -I flag pointing to a directory that contains SDL2/SDL_pixels.h which includes the mock.
This lets color.hpp compile unchanged.


## 7. Headless Singleton Architecture

The replacement for display::get_singleton() in keeper files:

  // src/headless/headless_context.hpp
  class headless_context : public display_context {
  public:
      static headless_context* get();           // replaces display::get_singleton()
      static void init(game_board* board);      // called at game start
      const std::vector<team>& teams() const;
      const gamemap&            map()   const;
      const unit_map&           units() const;
      ...
  private:
      static headless_context* instance_;
      game_board* board_;
  };

display::get_singleton() in the stub returns headless_context::get() cast to display*.
This requires display to be declared as an alias or the stub display class inherits headless_context.


## 8. Summary Statistics

  Total source files in wesnoth source list:   415
  Files excluded entirely (editor/gui/MP/etc):  ~195
  Files stubbed (display/audio/input):          ~80
  Files needing surgical patching:              ~41
  Files clean (libwesnoth_core + pure logic):   ~99

  SDL direct #includes in keeper .hpp files:    2  (color.hpp, events.hpp)
  SDL direct #includes in keeper .cpp files:    6  (game_state, play_controller, etc.)
  All resolved by: SDL_timer -> chrono, SDL_Color -> mock, events.hpp -> stub

  Estimated new files to create:
    src/stubs/          ~25 stub .cpp files
    src/headless/       ~6 new files (engine API, context, launcher, main, stdin driver)
    src/headless/sdl2/  ~3 mock headers (SDL_pixels.h, SDL_timer.h, SDL_rect.h)


## 9. Next Steps (Phase 2)

Priority order for Phase 2 (stubs) and Phase 3 (patches):

  Step 1: Create SDL mock headers (SDL_pixels, SDL_timer, SDL_rect, SDL_mutex)
           -> unblocks color.hpp, save_blocker.hpp, theme.hpp to compile clean
  Step 2: Stub video.hpp/video.cpp (headless()=true, faked()=true)
           -> unblocks ~10 keeper files that check video::headless()
  Step 3: Stub units/udisplay.hpp/cpp
           -> unblocks actions/* (largest keeper group)
  Step 4: Stub game_display.hpp/cpp + display.hpp/cpp with headless_context
           -> unblocks AI files, replay.cpp, scripting/lua_*.cpp
  Step 5: Stub sound.hpp/soundsource.hpp
           -> unblocks play_controller, game_events/handlers
  Step 6: Stub events.hpp; patch controller_base
           -> unblocks controller hierarchy
  Step 7: Patch play_controller + playsingle_controller
           -> core game loop operational
  Step 8: Wire up CMake target wesnoth-headless; attempt first compile
  Step 9: Fix all remaining link errors
  Step 10: Implement headless_engine API + stdin command loop
