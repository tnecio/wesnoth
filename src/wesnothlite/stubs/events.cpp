/* Headless stub: events.cpp
 * Provides no-op implementations for the real events.hpp declarations.
 * The real events.hpp is used (via #include_next in stubs/events.hpp).
 */
#include "events.hpp"
#include "menu_events.hpp"
#include "mouse_events.hpp"
#include "mouse_handler_base.hpp"
#include "chat_events.hpp"
#include "pathfind/pathfind.hpp"
#include "map/location.hpp"
#include "units/map.hpp"
#include "game_config_view.hpp"
#include "config.hpp"
#include <SDL2/SDL_events.h>
#include <chrono>
#include <functional>
#include <vector>
#include <string>

namespace events {

// context
context::~context() {}
void context::add_handler(sdl_handler*) {}
bool context::has_handler(const sdl_handler*) const { return false; }
bool context::remove_handler(sdl_handler*) { return false; }
void context::cycle_focus() {}
void context::set_focus(const sdl_handler*) {}
void context::add_staging_handlers() {}

// sdl_handler
sdl_handler::sdl_handler(bool) : has_joined_(false), has_joined_global_(false) {}
sdl_handler::sdl_handler(const sdl_handler&) : has_joined_(false), has_joined_global_(false) {}
sdl_handler& sdl_handler::operator=(const sdl_handler&) { return *this; }
sdl_handler::~sdl_handler() {}
void sdl_handler::join() {}
void sdl_handler::join(context&) {}
void sdl_handler::join_same(sdl_handler*) {}
void sdl_handler::leave() {}
void sdl_handler::join_global() {}
void sdl_handler::leave_global() {}

// event_context
event_context::event_context() {}
event_context::~event_context() {}

// pump_monitor
pump_monitor::pump_monitor() {}
pump_monitor::~pump_monitor() {}

// Global functions
void focus_handler(const sdl_handler*) {}
bool has_focus(const sdl_handler*, const SDL_Event*) { return false; }
void set_main_thread() {}
bool is_in_main_thread() { return true; }
void call_in_main_thread(const std::function<void()>& f) { f(); }
void pump() {}
void draw() {}
void raise_process_event() {}
void raise_resize_event() {}
void process_tooltip_strings(int, int) {}
bool is_input(const SDL_Event&) { return false; }
bool is_touch(const SDL_MouseButtonEvent&) { return false; }
bool is_touch(const SDL_MouseMotionEvent&) { return false; }
void discard_input() {}

// command_disabler
int commands_disabled = 0;
command_disabler::command_disabler() { ++commands_disabled; }
command_disabler::~command_disabler() { --commands_disabled; }

// mouse_handler_base
// Note: mouse_handler_base now has pure virtual gui() - it cannot be constructed directly.
// The derived mouse_handler provides the implementation.

bool mouse_handler_base::dragging_started() const { return false; }
void mouse_handler_base::mouse_motion_event(const SDL_MouseMotionEvent&, const bool) {}
void mouse_handler_base::mouse_update(const bool, map_location) {}
void mouse_handler_base::touch_motion_event(const SDL_TouchFingerEvent&, const bool) {}

// mouse_handler
mouse_handler* mouse_handler::singleton_ = nullptr;
mouse_handler::mouse_handler(play_controller& pc)
    : gui_(nullptr), pc_(pc)
    , previous_hex_(), previous_free_hex_(), selected_hex_(), next_unit_()
    , current_route_(), current_paths_()
    , unselected_paths_(false), unselected_reach_(false)
    , path_turns_(0), side_num_(1)
    , over_route_(false), reachmap_invalid_(false), show_partial_move_(false)
    , teleport_selected_(false)
    , preventing_units_highlight_(false)
{
    singleton_ = this;
}
mouse_handler::~mouse_handler() { if(singleton_ == this) singleton_ = nullptr; }
void mouse_handler::set_side(int side_number) { side_num_ = side_number; }
map_location mouse_handler::current_unit_attacks_from(const map_location&) const { return map_location(); }
void mouse_handler::deselect_hex() {}
unit_map::iterator mouse_handler::selected_unit() { return unit_map::iterator(); }
void mouse_handler::select_hex(const map_location&, const bool, const bool, const bool, const bool) {}
void mouse_handler::set_current_paths(const pathfind::paths&) {}

// chat_handler
chat_handler::chat_handler() {}
chat_handler::~chat_handler() {}
void chat_handler::add_chat_room_message_received(const std::string& /*room*/,
    const std::string& /*speaker*/, const std::string& /*message*/) {}
void chat_handler::add_chat_room_message_sent(const std::string& /*room*/,
    const std::string& /*message*/) {}
void chat_handler::add_whisper_received(const std::string& /*sender*/,
    const std::string& /*message*/) {}
void chat_handler::add_whisper_sent(const std::string& /*receiver*/,
    const std::string& /*message*/) {}
void chat_handler::send_chat_room_message(const std::string& /*room*/,
    const std::string& /*message*/) {}
void chat_handler::send_whisper(const std::string& /*receiver*/,
    const std::string& /*message*/) {}
void chat_handler::user_relation_changed(const std::string& /*name*/) {}

// menu_handler
static const config s_empty_menu_cfg;
static const game_config_view s_empty_menu_view = game_config_view::wrap(s_empty_menu_cfg);
menu_handler::menu_handler(play_controller& pc)
    : gui_(nullptr), pc_(pc), game_config_(s_empty_menu_view)
    , textbox_info_(), last_search_(), last_search_hit_()
{}
menu_handler::~menu_handler() {}
void menu_handler::do_ai_formula(const std::string&, int, mouse_handler&) {}
void menu_handler::do_command(const std::string&) {}
void menu_handler::do_search(const std::string&) {}
bool menu_handler::do_speak() { return false; }
bool menu_handler::end_turn(int) { return true; }
void menu_handler::execute_gotos(mouse_handler&, int) {}
std::vector<std::string> menu_handler::get_commands_list() { return {}; }
gui::floating_textbox& menu_handler::get_textbox() { return textbox_info_; }
void menu_handler::save_map() {}
void menu_handler::add_chat_message(const std::chrono::system_clock::time_point& /*time*/,
    const std::string& /*speaker*/, int /*side*/,
    const std::string& /*message*/,
    events::chat_handler::MESSAGE_TYPE /*type*/) {}
void menu_handler::clear_messages() {}
void menu_handler::send_chat_message(const std::string& /*message*/,
    bool /*allies_only*/) {}
void menu_handler::send_to_server(const config& /*cfg*/) {}

// mouse_handler_base non-default virtual methods
bool mouse_handler_base::mouse_button_event(const SDL_MouseButtonEvent& /*event*/, uint8_t /*button*/,
    map_location /*loc*/, bool /*click*/) { return false; }
bool mouse_handler_base::left_click(int /*x*/, int /*y*/, const bool /*browse*/)
{
    return false;
}
void mouse_handler_base::left_drag_end(int /*x*/, int /*y*/, const bool /*browse*/) {}
void mouse_handler_base::mouse_press(const SDL_MouseButtonEvent& /*event*/, const bool /*browse*/) {}
void mouse_handler_base::mouse_wheel(int /*xscroll*/, int /*yscroll*/, bool /*browse*/) {}
void mouse_handler_base::right_mouse_up(int /*x*/, int /*y*/, const bool /*browse*/) {}

// mouse_handler virtual overrides
int mouse_handler::drag_threshold() const { return 14; }
void mouse_handler::mouse_motion(int /*x*/, int /*y*/, const bool /*browse*/,
    bool /*update*/, map_location /*new_loc*/) {}
void mouse_handler::move_action(bool /*browse*/) {}
bool mouse_handler::right_click_show_menu(int /*x*/, int /*y*/, const bool /*browse*/)
{
    return true;
}
void mouse_handler_base::touch_action(const map_location /*hex*/, bool /*browse*/) {}
void mouse_handler::touch_action(const map_location /*hex*/, bool /*browse*/) {}
void mouse_handler::touch_motion(int /*x*/, int /*y*/, const bool /*browse*/,
    bool /*update*/, map_location /*new_loc*/) {}
bool mouse_handler::mouse_button_event(const SDL_MouseButtonEvent& /*event*/, uint8_t /*button*/,
    map_location /*loc*/, bool /*click*/) { return false; }

} // namespace events
