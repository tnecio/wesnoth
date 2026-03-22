
Plan: Extract Wesnoth Headless Core Engine

Overview

The core is already almost all in the wesnoth binary source list. The problem is tight coupling: game logic files pull in display, mouse_events, hotkey,
sound, gui2, and SDL throughout. The strategy is not to delete files but to:

1. Stub all rendering/audio/input/GUI code (same interface, text output instead)
2. Minimally patch controller/game files to drop hard GUI dependencies
3. Create a new CMake target wesnoth-headless (static lib + test executable)
4. Expose a clean API via a new headless_engine class
5. Provide a stdin/stdout wire protocol so the engine can be driven interactively or by a piped program

Interaction Model
-----------------
All text that would appear in a GUI window is printed to stdout with a structured prefix.
All user input that would come from mouse clicks, dialog buttons, or keyboard is read from stdin.
This applies to two distinct categories:

  A) Prompted choices — the engine is mid-action and needs a decision before it can continue
     (unit advancement, weapon selection, WML [message]/[choose] dialogs, victory/defeat ack).
     The engine blocks on stdin until the answer arrives.

  B) Player turn commands — it is a human side's turn and the engine is waiting for the next
     game action (move, attack, recruit, end_turn, etc.).
     The engine emits [WAITING] and blocks on stdin reading command lines.

This makes the engine fully driveable by a piped program, a test harness, or a human at a
terminal, without any GUI.

---
Phase 1 — Understand the Build & Current SDL Surface Area

Goal: map exactly which files compile without SDL and what drags it in

1. Do a clean CMake configure; examine all target_link_libraries for the wesnoth target
2. Scan source_lists/libwesnoth_core — these are already SDL-free; confirm they compile standalone
3. Audit the key "gateway" headers that pull SDL in everywhere:
- video.hpp → SDL_video.h
- sdl/surface.hpp → SDL_surface.h
- sdl/texture.hpp → SDL_render.h
- draw.hpp → textures
- events.hpp → SDL event loop
4. List all files in source_lists/wesnoth that #include "display.hpp" or #include "video.hpp" directly — these are the ones needing surgery

---
Phase 2 — Create Stub Implementations

Goal: provide no-op / text-output replacements for every SDL/display/audio/GUI dependency

Create src/stubs/ directory. Each stub provides the same class/function signatures as the real file but does nothing or emits to std::cout.

┌───────────────────────────────┬──────────────────────────────────────────────────────────────────────┬─────────────────────────────────────────────────┐
│           Stub file           │                               Replaces                               │              What it does instead               │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/display_stub.cpp        │ display.cpp                                                          │ All draw() / redraw() = no-op; game state       │
│                               │                                                                      │ changes log to stdout                           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/game_display_stub.cpp   │ game_display.cpp                                                     │ Inherits headless display, all rendering =      │
│                               │                                                                      │ no-op                                           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/video_stub.cpp          │ video.cpp                                                            │ init() = no-op, headless() = true, no           │
│                               │                                                                      │ SDL_Window                                      │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/sound_stub.cpp          │ sound.cpp + soundsource.cpp                                          │ play_sound(name) → [SOUND] name to stdout       │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/music_stub.cpp          │ sound_music_track.cpp                                                │ play_music(name) → [MUSIC] name to stdout       │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/events_stub.cpp         │ events.cpp                                                           │ No SDL_PollEvent; pump = no-op                  │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/font_stub.cpp           │ font/*.cpp                                                           │ All functions return empty/zero; no             │
│                               │                                                                      │ Pango/SDL_ttf                                   │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/gui2_stub.cpp           │ all of gui/                                                          │ All gui2::show_* dialogs print content to       │
│                               │                                                                      │ stdout and read choice/ack from stdin           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/cursor_stub.cpp         │ cursor.cpp                                                           │ All functions = no-op                           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/halo_stub.cpp           │ halo.cpp                                                             │ No-op                                           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/picture_stub.cpp        │ picture.cpp                                                          │ All texture lookups return empty                │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/floating_label_stub.cpp │ floating_label.cpp                                                   │ No-op                                           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/minimap_stub.cpp        │ minimap.cpp                                                          │ No-op                                           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/hotkey_stub.cpp         │ hotkey/*.cpp                                                         │ All functions = no-op                           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/theme_stub.cpp          │ theme.cpp                                                            │ Loads WML config section only (theme logic      │
│                               │                                                                      │ stripped)                                       │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/draw_stub.cpp           │ draw.cpp + draw_manager.cpp                                          │ No-op                                           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/units_display_stub.cpp  │ units/drawer.cpp + units/udisplay.cpp + units/animation.cpp +        │ All no-op                                       │
│                               │ units/animation_component.cpp                                        │                                                 │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/sdl_stub.cpp            │ sdl/*.cpp                                                            │ Point/rect math kept; surface/texture/window =  │
│                               │                                                                      │ no-op                                           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/lua_gui2_stub.cpp       │ scripting/lua_gui2.cpp + scripting/lua_widget*.cpp                   │ wesnoth.message() → stdout, ack from stdin;     │
│                               │                                                                      │ wesnoth.show_dialog() → print options, choice   │
│                               │                                                                      │ read from stdin                                 │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/lua_audio_stub.cpp      │ scripting/lua_audio.cpp                                              │ wesnoth.play_sound() → stdout                   │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/help_stub.cpp           │ help/*.cpp                                                           │ All no-op                                       │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/whiteboard_stub.cpp     │ whiteboard/*.cpp                                                     │ All no-op                                       │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/storyscreen_stub.cpp    │ storyscreen/*.cpp                                                    │ [story] text → stdout                           │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/desktop_stub.cpp        │ desktop/*.cpp                                                        │ All no-op                                       │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/addon_stub.cpp          │ addon/*.cpp                                                          │ All no-op                                       │
├───────────────────────────────┼──────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
│ stubs/widgets_stub.cpp        │ widgets/*.cpp                                                        │ All no-op                                       │
└───────────────────────────────┴──────────────────────────────────────────────────────────────────────┴─────────────────────────────────────────────────┘

---
Phase 3 — Patch Core Files

Goal: remove hard GUI/display/input deps from files we want to KEEP

These files are in the keeper set but reference GUI types in their headers or implementations:

play_controller.hpp/.cpp — biggest job:
- Remove member game_display& gui_ → replace with headless display reference
- Remove #include "help/help.hpp", "hotkey/command_executor.hpp", "menu_events.hpp", "mouse_events.hpp", "tooltips.hpp"
- Remove members mouse_handler_, menu_handler_, whiteboard_manager_
- Keep: game state management, turn sequencing, event pump integration

playsingle_controller.hpp/.cpp:
- Remove whiteboard manager
- Remove storyscreen calls → print story text to stdout, read Enter/ack from stdin
- Victory/defeat: print [VICTORY] or [DEFEAT] to stdout, then read ack from stdin
- When it is a human side's turn and the engine reaches the input-wait point:
  emit [WAITING] to stdout and enter the stdin command loop (see wire protocol below)
- Keep: single-player turn flow, AI invocation, end conditions

controller_base.hpp/.cpp:
- Remove SDL event handler inheritance (events::handler)
- Provide minimal headless event loop (just calls game event pump)

savegame.hpp/.cpp:
- Replace gui2::show_* confirmation dialogs: print prompt to stdout, read y/n from stdin
- Keep: WML serialization, file I/O

game_config_manager.hpp/.cpp:
- Replace loading screen calls with progress logging to stdout
- Keep: WML file loading, caching, config preprocessing

scripting/game_lua_kernel.cpp:
- Keep all game logic Lua bindings
- Skip registration of lua_gui2 module (or register stub version)
- Skip registration of lua_audio (or register stub version)

synced_user_choice.hpp/.cpp:
- Add a headless choice mechanism with two layers:
  1. If a C++ choice_fn callback is registered (programmatic API), call it
  2. Otherwise print the prompt + options to stdout and block reading a line from stdin
- Format for stdout: [PROMPT:choice] <description>\n  0: <opt0>\n  1: <opt1>\n...
- Format for stdin:  just the integer index, e.g. "1"
- This handles: weapon selection, unit advancement, [choose] WML dialogs

actions/attack.cpp:
- Remove floating label and animation calls
- Keep: hit/miss calculation, HP modification, unit advancement trigger

actions/move.cpp:
- Remove unit movement animation calls
- Keep: movement logic, ZOC, vision/shroud updates

game_initialization/singleplayer.cpp and playcampaign.cpp:
- Remove GUI campaign/scenario selection dialogs
- Keep: scenario loading, game state initialization sequence

---
Phase 4 — Files to Remove Entirely

These have no useful logic for the headless engine; don't stub, just exclude from build

- addon/ — add-on manager/client (all 6 files)
- chat_command_handler.cpp, chat_events.cpp, chat_log.cpp, display_chat_manager.cpp
- editor/ — all 80+ files
- game_initialization/connect_engine.cpp, lobby_data.cpp, lobby_info.cpp
- game_initialization/multiplayer.cpp
- game_initialization/depcheck.cpp
- mp_game_settings.cpp, mp_ui_alerts.cpp
- network_asio.cpp, network_download_file.cpp
- playmp_controller.cpp, playturn_network_adapter.cpp
- sdl/input.cpp (keyboard/mouse SDL polling)
- syncmp_handler.cpp, wesnothd_connection.cpp, tls_root_store.cpp
- server/ — entire directory
- about.cpp, achievements.cpp (UI-only)
- build_info.cpp (startup GUI info)
- game_launcher.cpp (main menu launcher, replaced by headless launcher)

---
Phase 4.5 — Stdin/Stdout Wire Protocol

All stdout lines are prefixed so a driving program can parse them reliably.
The engine never mixes prefix categories on one line.

Engine → stdout:

  [MSG] <speaker>: <text>          — WML [message] tag content
  [STORY] <text>                   — Story screen paragraph
  [PROMPT:ok] <text>               — Dialog with only an OK/close button
                                     (engine blocks; stdin: empty line or "ok")
  [PROMPT:yesno] <text>            — Yes/No confirmation
                                     (engine blocks; stdin: "y" or "n")
  [PROMPT:choice] <description>    — Multiple-choice prompt, followed by:
      0: <option text>             —   one line per option
      1: <option text>             —   ...
      ...                          —   (engine blocks; stdin: integer index)
  [SOUND] <filename>               — Sound effect triggered
  [MUSIC] <filename>               — Music track changed
  [LOG] <text>                     — Informational engine log line
  [EVENT] <name> [key=val ...]     — Named game event fired (enter village, attack, etc.)
  [WAITING] turn=<N> side=<N>      — Human player's turn; engine is ready for a command
  [VICTORY]                        — Scenario won (engine then emits [PROMPT:ok])
  [DEFEAT]                         — Scenario lost (engine then emits [PROMPT:ok])
  [SCENARIO_END] next=<id>         — Scenario finished, next scenario id (or "none")
  [STATE] <wml-snippet>            — Response to a "state" command; WML game state dump

stdin → Engine (during [WAITING]):

  move <x1>,<y1> <x2>,<y2>        — Move unit at x1,y1 toward x2,y2
  attack <ax>,<ay> <dx>,<dy> [w]  — Attack; w = weapon index (default: best)
  recruit <type> <x>,<y>          — Recruit unit_type at hex
  recall <id> <x>,<y>             — Recall unit by id at hex
  end_turn                         — End current side's turn
  save <filename>                  — Save game to file
  state                            — Dump full game state as WML to stdout ([STATE] block)
  units                            — Print unit list (id, type, location, hp)
  map                              — Print ASCII map with unit positions

Rules:
- The engine only reads commands during [WAITING] or an active [PROMPT:*].
- If a command is illegal (move to occupied hex, etc.) the engine prints
  [ERROR] <reason> and re-emits [WAITING] without consuming the turn.
- All coordinates are 1-indexed (x,y) matching Wesnoth's WML convention.

---
Phase 5 — New Build Target

Create source_lists/wesnoth_headless:

# Core (already SDL-free):
[everything in libwesnoth_core]

# Game logic (patched):
actions/advancement.cpp
actions/attack.cpp
actions/create.cpp
actions/heal.cpp
actions/move.cpp
actions/shroud_clearing_action.cpp
actions/undo.cpp
actions/unit_creator.cpp
actions/vision.cpp
ai/**/*.cpp   (all AI files)
attack_prediction.cpp
carryover.cpp
controller_base.cpp      ← patched
formula/**/*.cpp
game_board.cpp
game_classification.cpp
game_config_manager.cpp  ← patched
game_data.cpp
game_events/**/*.cpp
game_initialization/configure_engine.cpp
game_initialization/flg_manager.cpp
game_initialization/mp_game_utils.cpp
game_initialization/playcampaign.cpp    ← patched
game_initialization/singleplayer.cpp   ← patched
game_state.cpp
map/location.cpp
map/map.cpp
movetype.cpp
pathfind/pathfind.cpp
pathfind/teleport.cpp
pathutils.cpp
persist_context.cpp
persist_manager.cpp
persist_var.cpp
play_controller.cpp      ← patched
playsingle_controller.cpp ← patched
random_deterministic.cpp
random_synced.cpp
recall_list_manager.cpp
replay.cpp
replay_controller.cpp
replay_helper.cpp
replay_recorder_base.cpp
resources.cpp
save_blocker.cpp
save_index.cpp
saved_game.cpp
savegame.cpp             ← patched
scripting/game_lua_kernel.cpp ← patched
scripting/lua_common.cpp
scripting/lua_cpp_function.cpp
scripting/lua_fileops.cpp
scripting/lua_formula_bridge.cpp
scripting/lua_kernel_base.cpp
scripting/lua_map_location_ops.cpp
scripting/lua_mathx.cpp
scripting/lua_race.cpp
scripting/lua_rng.cpp
scripting/lua_stringx.cpp
scripting/lua_team.cpp
scripting/lua_terrainfilter.cpp
scripting/lua_terrainmap.cpp
scripting/lua_unit.cpp
scripting/lua_unit_attacks.cpp
scripting/lua_unit_type.cpp
scripting/lua_wml.cpp
scripting/mapgen_lua_kernel.cpp
side_filter.cpp
statistics.cpp
statistics_record.cpp
synced_checkup.cpp
synced_commands.cpp
synced_context.cpp
synced_user_choice.cpp   ← patched (callback injection)
team.cpp
teambuilder.cpp
terrain/filter.cpp
terrain/terrain.cpp
terrain/translation.cpp
terrain/type_data.cpp
time_of_day.cpp
tod_manager.cpp
units/abilities.cpp
units/attack_type.cpp
units/filter.cpp
units/formula_manager.cpp
units/helper.cpp
units/id.cpp
units/make.cpp
units/map.cpp
units/orb_status.cpp
units/race.cpp
units/types.cpp
units/unit.cpp
utils/config_filters.cpp
utils/context_free_grammar_generator.cpp
utils/irdya_datetime.cpp
utils/markov_generator.cpp
utils/name_generator_factory.cpp
variable.cpp
variable_info.cpp

# Stubs (all new files in src/stubs/):
stubs/display_stub.cpp
stubs/game_display_stub.cpp
stubs/video_stub.cpp
stubs/sound_stub.cpp
... etc

# Headless engine API:
headless/headless_engine.cpp
headless/headless_launcher.cpp
headless/headless_main.cpp

Add to CMakeLists.txt:
add_library(wesnoth-headless STATIC ${wesnoth_headless_sources})
target_compile_definitions(wesnoth-headless PUBLIC HEADLESS_ENGINE=1)
target_link_libraries(wesnoth-headless Boost::... lua ...)
# No SDL2, no SDL2_mixer, no SDL2_ttf, no Pango, no CURL, no Boost.ASIO

---
Phase 6 — Headless Engine Public API

src/headless/headless_engine.hpp:

    class headless_engine {
    public:
        void init(const std::string& data_path,
                const std::string& userdata_path = "/tmp/wesnoth_headless");
        void shutdown();

        // Scenario/save loading
        void start_scenario(const std::string& campaign_id,
                            const std::string& scenario_id,
                            const std::string& difficulty = "NORMAL");
        void load_save(const std::string& save_file_path);

        // State queries
        int            current_side() const;
        int            turn() const;
        const gamemap& map() const;
        const team&    get_team(int side) const;   // sides are 1-indexed
        unit_map&      units();

        // Actions (return bool success, emit events to log)
        bool move_unit(map_location from, map_location to);
        bool attack(map_location attacker, map_location defender, int weapon_index = -1);
        bool recruit(const std::string& unit_type, map_location hex);
        bool recall(const std::string& unit_id, map_location hex);
        void end_turn();

        // AI
        void run_ai();   // run AI for current side; noop if human side

        // Save
        void save(const std::string& path);

        // User-choice injection (advancements, weapon picks by WML [message choose], etc.)
        // If not set, the engine falls back to printing the prompt on stdout and reading
        // the answer (integer index) from stdin.
        using choice_fn = std::function<int(const std::string& type, const config& options)>;
        void set_choice_handler(choice_fn fn);

        // Drain structured output lines produced since last call.
        // Each entry is a prefixed string per the wire protocol (e.g. "[MSG] Konrad: ...").
        // Only useful when driving the engine programmatically; in stdin/stdout mode the
        // lines are written directly to stdout as they occur.
        std::vector<std::string> drain_output();

        // Enter the stdin/stdout command loop for the current human side's turn.
        // Reads commands from stdin, executes them, writes results to stdout.
        // Returns when end_turn is received or the scenario ends.
        // This is what playsingle_controller calls internally during a human turn.
        void run_stdin_turn();
    };

---
Phase 7 — Test Harness

Two modes are demonstrated:

Mode A — fully scripted (no stdin needed, choice_fn drives everything):

    int main() {
        headless_engine engine;
        engine.init("./data");

        // Inject answers programmatically: always pick option 0
        engine.set_choice_handler([](const std::string&, const config&) { return 0; });

        engine.start_scenario("tutorial", "01_The_Elves_Besieged");

        while (!engine.is_game_over()) {
            if (engine.current_side_is_ai()) {
                engine.run_ai();
            } else {
                // Drive human side manually via API
                engine.move_unit({5,3}, {5,4});
                engine.end_turn();
            }
            for (auto& line : engine.drain_output())
                std::cout << line << "\n";
        }
        engine.save("/tmp/test.gz");
    }

Mode B — interactive stdin/stdout (human at terminal or piped program):

    int main(int argc, char** argv) {
        headless_engine engine;
        engine.init("./data");
        // No set_choice_handler → defaults to stdin/stdout for all prompts

        engine.start_scenario("tutorial", "01_The_Elves_Besieged");
        // From here the engine drives itself:
        // - AI sides execute automatically
        // - Human sides emit [WAITING] and block reading commands from stdin
        // - All dialogs, messages, story text go to stdout
        // - run() returns when the scenario ends
        engine.run();
    }

Sample terminal session for Mode B:

    [LOG] Loading scenario: The Elves Besieged
    [MSG] Konrad: It is time to move out.
    [PROMPT:ok] Press enter to continue
    <user presses Enter>
    [WAITING] turn=1 side=1
    > units
    [LOG] id=Konrad  type=Elvish Lord    loc=5,3  hp=46/46
    [LOG] id=unit_2  type=Elvish Fighter loc=6,4  hp=33/33
    > move 5,3 5,4
    [EVENT] unit_moved id=Konrad from=5,3 to=5,4
    [WAITING] turn=1 side=1
    > end_turn
    [EVENT] turn_end side=1
    [EVENT] turn_start side=2
    [LOG] AI side 2 thinking...
    [EVENT] attack attacker=Orcish Grunt at=8,6 defender=Elvish Fighter at=6,4
    [SOUND] sword.ogg
    [WAITING] turn=2 side=1

---
Execution Order & Rough Sizing

┌───────┬────────────────────────────────────────────────────────────────┬──────────────────┐
│ Phase │                              Work                              │ Estimated effort │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ 1     │ Build audit, dependency map                                    │ 1–2 hours        │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ 2     │ Write all stub files (new files, no existing code changed yet) │ 4–6 hours        │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ 3     │ CMake target + compile stubs only                              │ 1–2 hours        │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ 4     │ Patch play_controller, playsingle_controller, controller_base  │ 3–5 hours        │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ 5     │ Patch savegame, game_config_manager, game_initialization       │ 2–3 hours        │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ 6     │ Patch scripting/game_lua_kernel, synced_user_choice            │ 2–3 hours        │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ 7     │ Patch actions/attack, actions/move, unit display stubs         │ 1–2 hours        │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ 8     │ Write headless engine API + launcher                           │ 3–4 hours        │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ 9     │ Wire everything up, fix link errors                            │ 3–6 hours        │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ 10    │ Test harness, run a scenario end-to-end                        │ 2–3 hours        │
├───────┼────────────────────────────────────────────────────────────────┼──────────────────┤
│ Total │                                                                │ ~24–36 hours     │
└───────┴────────────────────────────────────────────────────────────────┴──────────────────┘

---
Key Risks & Mitigations

┌───────────────────────────────────────────────────────────────────────┬────────────────────────────────────────────────────────────────────────────────┐
│                                 Risk                                  │                                   Mitigation                                   │
├───────────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤
│ display is a 3000-line class; many files friend it or call it         │ Inherit headless_display from display; override virtual methods; assign to     │
│ directly                                                              │ resources::screen                                                              │
├───────────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤
│ synced_user_choice blocks waiting for network/GUI input in MP code    │ Already has a local-choice path; wire headless callback through that.          │
│                                                                       │ Fallback: print [PROMPT:choice] to stdout, block reading integer from stdin     │
├───────────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤
│ Lua scripts call wesnoth.message() / wesnoth.show_dialog()            │ lua_gui2_stub prints to stdout and reads ack/choice from stdin; works for     │
│ extensively in campaign WML                                           │ all WML; no silent discard of game content                                     │
├───────────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤
│ game_config_manager calls advance_loading_screen() 50+ times during   │ One-line stub; progress goes to stdout counter                                 │
│ data load                                                             │                                                                                │
├───────────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤
│ AI code (ai/) references display for drawing debug info               │ #ifdef HEADLESS_ENGINE guards or stub methods in headless display              │
├───────────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤
│ terrain/builder.cpp contains rendering logic but is also needed for   │ Keep the file; all actual SDL texture operations are behind resources::screen  │
│ map validity                                                          │ calls which stub handles                                                       │
├───────────────────────────────────────────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────┤
│ font::pango_line_size() called in non-display code (report            │ Stub returns {0, 0}; reports disabled in headless mode                         │
│ generation)                                                           │                                                                                │
└───────────────────────────────────────────────────────────────────────┴────────────────────────────────────────────────────────────────────────────────┘
