/* Headless stubs: gui2 dialogs and related free functions.
 * All implementations are no-ops that compile cleanly against the real headers.
 */

#include <chrono>
#include <iostream>
#include "cursor.hpp"
#include "game_config_view.hpp"
#include "gui/widgets/settings.hpp"

// ---- modal_dialog base class ----
#include "gui/dialogs/modal_dialog.hpp"

namespace gui2::dialogs {

// modal_dialog constructor/destructor are defined in the real window class;
// we only need stubs for the pure-virtual window_id() on concrete subclasses.

} // namespace gui2::dialogs

// ---- loading_screen ----
#include "gui/dialogs/loading_screen.hpp"

namespace gui2::dialogs {

loading_screen* loading_screen::singleton_ = nullptr;

loading_screen::loading_screen(std::function<void()> /*f*/)
    : modal_dialog("loading_screen")
    , load_funcs_()
    , worker_result_()
    , cursor_setter_()
    , progress_stage_label_(nullptr)
    , animation_(nullptr)
    , animation_start_()
    , current_stage_(loading_stage::none)
    , visible_stages_()
    , current_visible_stage_(visible_stages_.end())
    , running_(false)
{}

loading_screen::~loading_screen() {}

void loading_screen::display(const std::function<void()>& f) { if(f) f(); }
void loading_screen::progress(loading_stage /*stage*/) {}
void loading_screen::spin() {}
void loading_screen::raise() {}

const std::string& loading_screen::window_id() const
{
    static const std::string id("loading_screen");
    return id;
}

void loading_screen::pre_show() {}
void loading_screen::post_show() {}
void loading_screen::process() {}
void loading_screen::layout() {}

} // namespace gui2::dialogs

// ---- campaign_difficulty ----
#include "gui/dialogs/campaign_difficulty.hpp"

namespace gui2::dialogs {

campaign_difficulty::campaign_difficulty(const config& /*campaign*/)
    : modal_dialog("campaign_difficulty")
    , difficulties_()
    , campaign_id_()
    , selected_difficulty_()
{}

const std::string& campaign_difficulty::window_id() const
{
    static const std::string id("campaign_difficulty");
    return id;
}

void campaign_difficulty::pre_show() {}
void campaign_difficulty::post_show() {}

config generate_difficulty_config(const config& /*source*/) { return config(); }

} // namespace gui2::dialogs

// ---- formula_debugger ----
#include "gui/dialogs/formula_debugger.hpp"

namespace gui2::dialogs {

const std::string& formula_debugger::window_id() const
{
    static const std::string id("formula_debugger");
    return id;
}

void formula_debugger::pre_show() {}
void formula_debugger::callback_continue_button() {}
void formula_debugger::callback_next_button() {}
void formula_debugger::callback_step_button() {}
void formula_debugger::callback_stepout_button() {}

} // namespace gui2::dialogs

// ---- game_load ----
#include "gui/dialogs/game_load.hpp"

namespace gui2::dialogs {

static const config s_empty_cache_cfg;
static const game_config_view s_empty_cache_view = game_config_view::wrap(s_empty_cache_cfg);
game_load::game_load(savegame::load_game_metadata& data)
    : modal_dialog("game_load")
    , filename_(data.filename)
    , save_index_manager_(data.manager)
    , change_difficulty_(nullptr)
    , show_replay_(nullptr)
    , cancel_orders_(nullptr)
    , summary_(data.summary)
    , games_()
    , cache_config_(s_empty_cache_view)
{}

bool game_load::execute(savegame::load_game_metadata& /*data*/)
{
    return false;
}

const std::string& game_load::window_id() const
{
    static const std::string id("game_load");
    return id;
}

void game_load::pre_show() {}
void game_load::set_save_dir_list(menu_button& /*dir_list*/) {}
void game_load::populate_game_list() {}
void game_load::apply_filter_text(const std::string& /*text*/) {}
void game_load::browse_button_callback() {}
void game_load::delete_button_callback() {}
void game_load::handle_dir_select() {}
void game_load::display_savegame_internal(const savegame::save_info& /*game*/) {}
void game_load::display_savegame() {}
void game_load::evaluate_summary_string(std::stringstream& /*str*/, const config& /*cfg_summary*/) {}
void game_load::key_press_callback(const SDL_Keycode /*key*/) {}

} // namespace gui2::dialogs

// ---- game_save / game_save_message / game_save_oos ----
#include "gui/dialogs/game_save.hpp"

namespace gui2::dialogs {

game_save::game_save(std::string& /*filename*/, const std::string& /*title*/)
    : modal_dialog("game_save")
{}

const std::string& game_save::window_id() const
{
    static const std::string id("game_save");
    return id;
}

game_save_message::game_save_message(std::string& /*filename*/,
                                     const std::string& /*title*/,
                                     const std::string& /*message*/)
    : modal_dialog("game_save_message")
{}

const std::string& game_save_message::window_id() const
{
    static const std::string id("game_save_message");
    return id;
}

game_save_oos::game_save_oos(bool& /*ignore_all*/,
                             std::string& /*filename*/,
                             const std::string& /*title*/,
                             const std::string& /*message*/)
    : modal_dialog("game_save_oos")
{}

const std::string& game_save_oos::window_id() const
{
    static const std::string id("game_save_oos");
    return id;
}

} // namespace gui2::dialogs

// ---- generator_settings ----
#include "gui/dialogs/editor/generator_settings.hpp"

namespace gui2::dialogs {

generator_settings::generator_settings(generator_data& /*data*/)
    : modal_dialog("generator_settings")
    , players_(nullptr)
    , width_(nullptr)
    , height_(nullptr)
{}

const std::string& generator_settings::window_id() const
{
    static const std::string id("generator_settings");
    return id;
}

void generator_settings::pre_show() {}
void generator_settings::adjust_minimum_size_by_players() {}

} // namespace gui2::dialogs

// ---- outro ----
#include "gui/dialogs/outro.hpp"

namespace gui2::dialogs {

outro::outro(const game_classification& /*info*/)
    : modal_dialog("outro")
    , text_()
    , text_index_(0)
    , display_duration_(std::chrono::milliseconds{0})
    , stage_(stage::fading_in)
    , stage_start_()
{}

const std::string& outro::window_id() const
{
    static const std::string id("outro");
    return id;
}

void outro::update() {}
void outro::pre_show() {}
double outro::get_fade_progress(const std::chrono::steady_clock::time_point& /*now*/) const { return 0.0; }

} // namespace gui2::dialogs

// ---- simple_item_selector ----
#include "gui/dialogs/simple_item_selector.hpp"

namespace gui2::dialogs {

simple_item_selector::simple_item_selector(const std::string& /*title*/,
                                           const std::string& /*message*/,
                                           const list_type& items,
                                           bool /*title_uses_markup*/,
                                           bool /*message_uses_markup*/)
    : modal_dialog("simple_item_selector")
    , index_(-1)
    , single_button_(false)
    , items_(items)
    , ok_label_()
    , cancel_label_()
{}

const std::string& simple_item_selector::window_id() const
{
    static const std::string id("simple_item_selector");
    return id;
}

void simple_item_selector::pre_show() {}
void simple_item_selector::post_show() {}

} // namespace gui2::dialogs

// story_viewer::display is implemented inline in gui/dialogs/story_viewer.hpp
// under HEADLESS_ENGINE — no stub needed here.

// ---- surrender_quit ----
#include "gui/dialogs/surrender_quit.hpp"

namespace gui2::dialogs {

surrender_quit::surrender_quit()
    : modal_dialog("surrender_quit")
{}

const std::string& surrender_quit::window_id() const
{
    static const std::string id("surrender_quit");
    return id;
}

} // namespace gui2::dialogs

// ---- synched_choice_wait ----
#include "gui/dialogs/multiplayer/synced_choice_wait.hpp"

namespace gui2::dialogs {

synched_choice_wait::synched_choice_wait(user_choice_manager& mgr)
    : modal_dialog("synched_choice_wait")
    , mgr_(mgr)
    , message_(nullptr)
{}

synched_choice_wait::~synched_choice_wait() {}

const std::string& synched_choice_wait::window_id() const
{
    static const std::string id("synched_choice_wait");
    return id;
}

void synched_choice_wait::pre_show() {}
void synched_choice_wait::handle_generic_event(const std::string& /*event_name*/) {}

} // namespace gui2::dialogs

// ---- unit_advance ----
#include "gui/dialogs/unit_advance.hpp"

namespace gui2::dialogs {

unit_advance::unit_advance(const std::vector<unit_const_ptr>& samples, std::size_t real)
    : modal_dialog("unit_advance")
    , previews_(samples)
    , selected_index_(0)
    , last_real_advancement_(real)
{}

const std::string& unit_advance::window_id() const
{
    static const std::string id("unit_advance");
    return id;
}

void unit_advance::pre_show() {}
void unit_advance::post_show() {}
void unit_advance::list_item_clicked() {}
void unit_advance::show_help() {}

} // namespace gui2::dialogs

// ---- gui2 free functions (timer, is_in_dialog, log_domain, etc.) ----
#include "gui/core/timer.hpp"
#include "gui/core/log.hpp"
#include "gui/core/static_registry.hpp"
#include "gui/dialogs/message.hpp"
#include "gui/dialogs/transient_message.hpp"
#include "gui/dialogs/file_dialog.hpp"
#include "gui/dialogs/theme_list.hpp"
#include "theme.hpp"
#include "gui/gui.hpp"

namespace gui2 {

lg::log_domain log_gui_draw("gui/draw");
lg::log_domain log_gui_event("gui/event");
lg::log_domain log_gui_general("gui/general");
lg::log_domain log_gui_iterator("gui/iterator");
lg::log_domain log_gui_layout("gui/layout");
lg::log_domain log_gui_lifetime("gui/lifetime");
lg::log_domain log_gui_parse("gui/parse");

std::size_t add_timer(const std::chrono::milliseconds& /*interval*/,
                      const std::function<void(std::size_t id)>& /*callback*/,
                      const bool /*repeat*/)
{
    return 0;
}

bool remove_timer(const std::size_t /*id*/)
{
    return false;
}

bool execute_timer(const std::size_t /*id*/)
{
    return false;
}

bool is_in_dialog()
{
    return false;
}

void remove_single_widget_definition(const std::string& /*widget_type*/,
                                     const std::string& /*definition_id*/)
{}

void show_error_message(const std::string& msg, bool /*message_use_markup*/)
{
    std::cerr << "ERROR: " << msg << "\n";
}

int show_message(const std::string& /*title*/,
                 const std::string& /*message*/,
                 const dialogs::message::button_style /*button_style*/,
                 bool /*message_use_markup*/,
                 bool /*title_use_markup*/)
{
    return 0;
}

void show_message(const std::string& /*title*/,
                  const std::string& /*message*/,
                  const std::string& /*button_caption*/,
                  const bool /*auto_close*/,
                  const bool /*message_use_markup*/,
                  const bool /*title_use_markup*/)
{}

void show_transient_error_message(const std::string& msg,
                                  const std::string& /*image*/,
                                  const bool /*message_use_markup*/)
{
    std::cerr << "TRANSIENT ERROR: " << msg << "\n";
}

void show_transient_message(const std::string& /*title*/,
                            const std::string& /*message*/,
                            const std::string& /*image*/,
                            const bool /*message_use_markup*/,
                            const bool /*title_use_markup*/)
{}

void switch_theme(const std::string& /*theme_id*/) {}

} // namespace gui2

// gui2::settings variables
namespace gui2::settings {
    std::chrono::milliseconds popup_show_delay{0};
}

// ---- gui2::dialogs::file_dialog ----
namespace gui2::dialogs {

file_dialog::file_dialog()
    : modal_dialog("file_dialog")
    , title_()
    , msg_()
    , ok_label_()
    , extension_()
    , current_entry_()
    , current_dir_()
    , read_only_(false)
    , save_mode_(false)
{}

const std::string& file_dialog::window_id() const
{
    static const std::string id("file_dialog");
    return id;
}

void file_dialog::pre_show() {}

file_dialog& file_dialog::set_filename(const std::string& fn)
{
    current_entry_ = fn;
    return *this;
}

file_dialog& file_dialog::set_path(const std::string& path)
{
    current_dir_ = path;
    return *this;
}

std::string file_dialog::path() const
{
    return current_dir_ + "/" + current_entry_;
}

} // namespace gui2::dialogs

// ---- gui2::dialogs::theme_list ----
namespace gui2::dialogs {

theme_list::theme_list(const std::vector<theme_info>& themes, int current)
    : modal_dialog("theme_list")
    , index_(current)
    , themes_(themes)
{}

const std::string& theme_list::window_id() const
{
    static const std::string id("theme_list");
    return id;
}

void theme_list::pre_show() {}
void theme_list::post_show() {}

} // namespace gui2::dialogs
