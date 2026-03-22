/* Headless stub: hotkey_stubs.cpp
 * Provides no-op implementations for hotkey and play_controller::hotkey_handler symbols.
 */
#include "hotkey/command_executor.hpp"
#include "hotkey/hotkey_command.hpp"
#include "hotkey/hotkey_item.hpp"
#include "hotkey/hotkey_handler.hpp"
#include "hotkey/hotkey_handler_sp.hpp"
#include "config.hpp"
#include "tstring.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <bitset>

namespace hotkey {

static const hotkey_command& stub_null_command()
{
    static hotkey_command null_cmd(
        HOTKEY_NULL, "null", t_string("null"),
        /*hidden=*/true, /*toggle=*/false,
        hk_scopes(0), HKCAT_PLACEHOLDER, t_string(""));
    return null_cmd;
}

hotkey_command::hotkey_command(HOTKEY_COMMAND cmd, const std::string& id_,
    const t_string& desc, bool hidden_, bool toggle_,
    hk_scopes scope_, HOTKEY_CATEGORY category_, const t_string& tooltip_)
    : command(cmd)
    , id(id_)
    , description(desc)
    , hidden(hidden_)
    , toggle(toggle_)
    , scope(scope_)
    , category(category_)
    , tooltip(tooltip_)
{}

// command_executor
void command_executor::execute_quit_command() {}
void command_executor::execute_action(const std::vector<std::string>& /*items_arg*/) {}
void command_executor::lua_console() {}
void command_executor::surrender_game() {}
void command_executor::show_menu(const std::vector<config>& /*items_arg*/, const point& /*menu_loc*/, bool /*context_menu*/) {}

// command_executor_default
bool command_executor::do_execute_command(const hotkey::ui_command& /*cmd*/, bool /*press*/, bool /*release*/) { return false; }

void command_executor_default::set_button_state() {}
void command_executor_default::recalculate_minimap() {}
void command_executor_default::lua_console() {}
void command_executor_default::zoom_in() {}
void command_executor_default::zoom_out() {}
void command_executor_default::zoom_default() {}
void command_executor_default::map_screenshot() {}

// hotkey_command free functions
const hotkey_command& get_hotkey_command(const std::string& /*command*/)
{
    return stub_null_command();
}

const hotkey_command& get_hotkey_command(std::string_view /*command*/)
{
    return stub_null_command();
}

const hotkey_command& get_hotkey_command(HOTKEY_COMMAND /*command*/)
{
    return stub_null_command();
}

std::string get_names(const std::string& /*id*/)
{
    return std::string();
}

void jbutton_event(const SDL_Event& /*event*/, command_executor* /*executor*/) {}
void jhat_event(const SDL_Event& /*event*/, command_executor* /*executor*/) {}
void key_event(const SDL_Event& /*event*/, command_executor* /*executor*/) {}
void keyup_event(const SDL_Event& /*event*/, command_executor* /*executor*/) {}
void mbutton_event(const SDL_Event& /*event*/, command_executor* /*executor*/) {}

void load_custom_hotkeys(const game_config_view& /*cfg*/) {}
void load_default_hotkeys(const game_config_view& /*cfg*/) {}
void reset_default_hotkeys() {}
void run_events(command_executor* /*executor*/) {}
void save_hotkeys(config& /*cfg*/) {}

// scope_changer
scope_changer::scope_changer()
    : prev_scope_active_(), restore_(false)
{}

scope_changer::scope_changer(hk_scopes /*new_scopes*/, bool restore)
    : prev_scope_active_(), restore_(restore)
{}

scope_changer::~scope_changer() {}

// wml_hotkey_record
wml_hotkey_record::wml_hotkey_record(
    const std::string& /*id*/,
    const t_string& /*description*/,
    const config& /*default_hotkey*/)
{}

wml_hotkey_record::~wml_hotkey_record() {}

} // namespace hotkey

// play_controller::hotkey_handler
const std::string play_controller::hotkey_handler::wml_menu_hotkey_prefix = "wml_menu_";

play_controller::hotkey_handler::hotkey_handler(play_controller& pc, saved_game& sg)
    : play_controller_(pc)
    , menu_handler_(pc.get_menu_handler())
    , mouse_handler_(pc.get_mouse_handler_base())
    , saved_game_(sg)
{}

play_controller::hotkey_handler::~hotkey_handler() {}

// Implement all pure/virtual overrides declared in play_controller::hotkey_handler
void play_controller::hotkey_handler::objectives() {}
void play_controller::hotkey_handler::show_statistics() {}
void play_controller::hotkey_handler::unit_list() {}
void play_controller::hotkey_handler::move_action() {}
void play_controller::hotkey_handler::select_and_action() {}
void play_controller::hotkey_handler::touch_hex() {}
void play_controller::hotkey_handler::select_hex() {}
void play_controller::hotkey_handler::deselect_hex() {}
void play_controller::hotkey_handler::status_table() {}
void play_controller::hotkey_handler::save_game() {}
void play_controller::hotkey_handler::save_replay() {}
void play_controller::hotkey_handler::save_map() {}
void play_controller::hotkey_handler::load_game() {}
void play_controller::hotkey_handler::preferences() {}
void play_controller::hotkey_handler::speak() {}
void play_controller::hotkey_handler::show_chat_log() {}
void play_controller::hotkey_handler::show_help() {}
void play_controller::hotkey_handler::cycle_units() {}
void play_controller::hotkey_handler::cycle_back_units() {}
void play_controller::hotkey_handler::undo() {}
void play_controller::hotkey_handler::redo() {}
void play_controller::hotkey_handler::show_enemy_moves(bool /*ignore_units*/) {}
void play_controller::hotkey_handler::goto_leader() {}
void play_controller::hotkey_handler::unit_description() {}
void play_controller::hotkey_handler::terrain_description() {}
void play_controller::hotkey_handler::toggle_ellipses() {}
void play_controller::hotkey_handler::toggle_grid() {}
void play_controller::hotkey_handler::search() {}
void play_controller::hotkey_handler::toggle_accelerated_speed() {}
void play_controller::hotkey_handler::scroll_up(bool /*on*/) {}
void play_controller::hotkey_handler::scroll_down(bool /*on*/) {}
void play_controller::hotkey_handler::scroll_left(bool /*on*/) {}
void play_controller::hotkey_handler::scroll_right(bool /*on*/) {}
hotkey::action_state play_controller::hotkey_handler::get_action_state(const hotkey::ui_command&) const { return hotkey::action_state::stateless; }
bool play_controller::hotkey_handler::in_context_menu(const hotkey::ui_command& /*cmd*/) const { return false; }
bool play_controller::hotkey_handler::can_execute_command(const hotkey::ui_command& /*cmd*/) const { return false; }
bool play_controller::hotkey_handler::do_execute_command(const hotkey::ui_command& /*cmd*/, bool /*press*/, bool /*release*/) { return false; }
void play_controller::hotkey_handler::show_menu(const std::vector<config>& /*items_arg*/, const point& /*menu_loc*/, bool /*context_menu*/) {}

// playsingle_controller::hotkey_handler
playsingle_controller::hotkey_handler::hotkey_handler(
    playsingle_controller& pc, saved_game& sg)
    : play_controller::hotkey_handler(pc, sg)
    , playsingle_controller_(pc)
    , whiteboard_manager_(pc.get_whiteboard())
{}

playsingle_controller::hotkey_handler::~hotkey_handler() {}

void playsingle_controller::hotkey_handler::recruit() {}
void playsingle_controller::hotkey_handler::repeat_recruit() {}
void playsingle_controller::hotkey_handler::recall() {}
bool playsingle_controller::hotkey_handler::can_execute_command(const hotkey::ui_command& /*cmd*/) const { return false; }
void playsingle_controller::hotkey_handler::toggle_shroud_updates() {}
void playsingle_controller::hotkey_handler::update_shroud_now() {}
void playsingle_controller::hotkey_handler::end_turn() {}
void playsingle_controller::hotkey_handler::rename_unit() {}
void playsingle_controller::hotkey_handler::create_unit() {}
void playsingle_controller::hotkey_handler::change_side() {}
void playsingle_controller::hotkey_handler::kill_unit() {}
void playsingle_controller::hotkey_handler::label_terrain(bool /*team_only*/) {}
void playsingle_controller::hotkey_handler::clear_labels() {}
void playsingle_controller::hotkey_handler::label_settings() {}
void playsingle_controller::hotkey_handler::continue_move() {}
void playsingle_controller::hotkey_handler::unit_hold_position() {}
void playsingle_controller::hotkey_handler::end_unit_turn() {}
void playsingle_controller::hotkey_handler::user_command() {}
void playsingle_controller::hotkey_handler::custom_command() {}
void playsingle_controller::hotkey_handler::ai_formula() {}
void playsingle_controller::hotkey_handler::clear_messages() {}
void playsingle_controller::hotkey_handler::whiteboard_toggle() {}
void playsingle_controller::hotkey_handler::whiteboard_execute_action() {}
void playsingle_controller::hotkey_handler::whiteboard_execute_all_actions() {}
void playsingle_controller::hotkey_handler::whiteboard_delete_action() {}
void playsingle_controller::hotkey_handler::whiteboard_bump_up_action() {}
void playsingle_controller::hotkey_handler::whiteboard_bump_down_action() {}
void playsingle_controller::hotkey_handler::whiteboard_suppose_dead() {}
void playsingle_controller::hotkey_handler::select_teleport() {}
void playsingle_controller::hotkey_handler::replay_exit() {}
void play_controller::hotkey_handler::load_autosave(const std::string& /*filename*/, bool /*start_replay*/) {}
void playsingle_controller::hotkey_handler::load_autosave(const std::string& /*filename*/, bool /*start_replay*/) {}
hotkey::action_state playsingle_controller::hotkey_handler::get_action_state(const hotkey::ui_command&) const { return hotkey::action_state::stateless; }
