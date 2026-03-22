/* Headless stubs: miscellaneous subsystems.
 * All implementations are no-ops.
 */

#include <algorithm>
#include <chrono>
#include <string>
#include <set>
#include <functional>
#include <iostream>
#include <sstream>

// ---- about ----
#include "about.hpp"

namespace about {

void set_about(const game_config_view& /*cfg*/) {}

} // namespace about

// ---- achievements ----
#include "achievements.hpp"

achievements::achievements() : achievement_list_() {}
void achievements::reload() {}

// ---- CKey ----
#include "key.hpp"

CKey::CKey() : key_list(nullptr) {}
bool CKey::operator[](int /*k*/) const { return false; }
bool CKey::is_uncomposable(const SDL_KeyboardEvent& /*event*/) { return false; }

// ---- desktop::battery_info ----
#include "desktop/battery_info.hpp"

namespace desktop {
namespace battery_info {

bool does_device_have_battery() { return false; }
double get_battery_percentage() { return -1.0; }

} // namespace battery_info
} // namespace desktop

// ---- font::floating_label ----
#include "floating_label.hpp"

namespace font {

floating_label_context::floating_label_context() {}
floating_label_context::~floating_label_context() {}

floating_label::floating_label(const std::string& text)
    : tex_()
    , screen_loc_()
    , alpha_(255)
    , fadeout_(0)
    , time_start_()
    , text_(text)
    , font_size_(14)
    , color_(color_t(255, 255, 255, 255))
    , bgcolor_(color_t(0, 0, 0, 0))
    , xpos_(0.0)
    , ypos_(0.0)
    , xmove_(0.0)
    , ymove_(0.0)
    , lifetime_(-1)
    , width_(-1)
    , height_(-1)
    , clip_rect_(SDL_Rect{0, 0, 0, 0})
    , visible_(true)
    , align_(CENTER_ALIGN)
    , border_(0)
    , scroll_(ANCHOR_LABEL_SCREEN)
    , use_markup_(false)
{}

void floating_label::set_lifetime(const std::chrono::milliseconds& lifetime,
                                   const std::chrono::milliseconds& fadeout)
{
    lifetime_ = lifetime;
    fadeout_ = fadeout;
}

int add_floating_label(const floating_label& /*flabel*/) { return 0; }
void move_floating_label(int /*handle*/, double /*xmove*/, double /*ymove*/) {}
void remove_floating_label(int /*handle*/, const std::chrono::milliseconds& /*fadeout*/) {}
void show_floating_label(int /*handle*/, bool /*show*/) {}

} // namespace font

// ---- addon helpers ----
#include "addon/manager.hpp"

bool have_addon_pbl_info(const std::string& /*addon_name*/) { return false; }
config get_addon_pbl_info(const std::string& /*addon_name*/, bool /*do_validate*/) { return config(); }
void refresh_addon_version_info_cache() {}

// ---- get_current_animation_tick ----
#include "animated.hpp"

std::chrono::steady_clock::time_point get_current_animation_tick()
{
    return std::chrono::steady_clock::time_point{};
}

// gui::in_dialog() was removed upstream; gui2::is_in_dialog() is now used instead.

// ---- gui::button::set_check ----
#include "widgets/button.hpp"

void gui::button::set_check(bool /*check*/) {}

// ---- gui::textbox methods ----
#include "widgets/textbox.hpp"

void gui::textbox::set_text(const std::string& /*text*/, const color_t& /*color*/) {}
const std::string gui::textbox::text() const { return {}; }

// ---- gui::floating_textbox ----
#include "floating_textbox.hpp"

void gui::floating_textbox::close() {}
void gui::floating_textbox::memorize_command(const std::string& /*command*/) {}
void gui::floating_textbox::tab(const std::set<std::string>& /*dictionary*/) {}

// ---- help ----
#include "help/help.hpp"

namespace help {

// Define implementation so unique_ptr destructor can compile.
class help_manager::implementation {};

help_manager::help_manager() : impl_(std::make_unique<implementation>()) {}
help_manager::~help_manager() {}

std::shared_ptr<help_manager> help_manager::get_instance() { return nullptr; }

} // namespace help

// ---- lua_widget ----
#include "scripting/lua_widget.hpp"

namespace lua_widget {
void register_metatable(lua_State* /*L*/) {}
} // namespace lua_widget

// ---- mp_game_settings ----
#include "mp_game_settings.hpp"

mp_game_settings::mp_game_settings()
    : name()
    , password()
    , hash()
    , mp_era_name()
    , mp_scenario()
    , mp_scenario_name()
    , mp_campaign()
    , side_users()
    , num_turns(0)
    , village_gold(1)
    , village_support(1)
    , xp_modifier(100)
    , mp_countdown_init_time(270)
    , mp_countdown_reservoir_time(330)
    , mp_countdown_turn_bonus(60)
    , mp_countdown_action_bonus(13)
    , mp_countdown(false)
    , use_map_settings(false)
    , random_start_time(false)
    , fog_game(false)
    , shroud_game(false)
    , allow_observers(true)
    , private_replay(false)
    , shuffle_sides(false)
    , saved_game(saved_game_mode::type::no)
    , mode(random_faction_mode::type::independent)
    , options()
    , addons()
{}

mp_game_settings::mp_game_settings(const config& /*cfg*/)
    : mp_game_settings()
{}

config mp_game_settings::to_config() const { return config(); }
void mp_game_settings::update_addon_requirements(const config& /*addon_cfg*/) {}

void mp_game_settings::addon_version_info::write(config& /*cfg*/) const {}

// ---- mp::goto_mp_staging / goto_mp_wait ----
#include "game_initialization/multiplayer.hpp"

namespace mp {

bool goto_mp_staging(ng::connect_engine& /*engine*/) { return false; }
bool goto_mp_wait(bool /*observe*/) { return false; }

} // namespace mp

// ---- playturn_network_adapter ----
#include "playturn_network_adapter.hpp"

playturn_network_adapter::playturn_network_adapter(source_type source)
    : network_reader_(source)
    , data_(1)  // always has one empty config
    , data_front_()
    , next_(data_.front().ordered_begin())
    , next_command_num_(0)
{}

playturn_network_adapter::~playturn_network_adapter() {}

bool playturn_network_adapter::read(config& /*dst*/) { return false; }

// ---- plugins_context ----
#include "scripting/plugins/context.hpp"

plugins_context::plugins_context(const std::string& name)
    : callbacks_()
    , accessors_()
    , name_(name)
    , execute_kernel_(nullptr)
{}

void plugins_context::play_slice() {}

void plugins_context::set_callback(const std::string& name,
                                   const std::function<void(config)>& function,
                                   bool /*preserves_context*/)
{
    callbacks_[name] = [function](config cfg) { function(cfg); return true; };
}

void plugins_context::set_accessor_string(const std::string& name,
                                          const std::function<std::string(config)>& f)
{
    accessors_[name] = [f](config cfg) { config r; r["value"] = f(cfg); return r; };
}

void plugins_context::set_accessor_int(const std::string& name,
                                       const std::function<int(config)>& f)
{
    accessors_[name] = [f](config cfg) { config r; r["value"] = f(cfg); return r; };
}

// ---- sdl::get_mouse_state ----
#include "sdl/input.hpp"

namespace sdl {

uint32_t get_mouse_state(int* x, int* y)
{
    if(x) *x = 0;
    if(y) *y = 0;
    return 0;
}

} // namespace sdl

// ---- soundsource::manager ----
#include "soundsource.hpp"

namespace soundsource {

manager::manager(const display& disp) : disp_(disp) {}
manager::~manager() {}
positional_source::~positional_source() {}
void manager::handle_generic_event(const std::string& /*event_name*/) {}
void manager::add(const sourcespec& /*spec*/) {}
void manager::update() {}
void manager::write_sourcespecs(config& /*cfg*/) const {}

} // namespace soundsource

// syncmp_handler / syncmp_registry were removed upstream.

// ---- tooltips ----
#include "tooltips.hpp"

namespace tooltips {

manager::manager() {}
manager::~manager() {}
void manager::layout() {}
bool manager::expose(const rect& /*region*/) { return false; }
rect manager::screen_location() { return rect(); }

int add_tooltip(const rect& /*rect*/, const std::string& /*message*/, const std::string& /*action*/)
{
    return 0;
}

bool update_tooltip(int /*id*/, const rect& /*rect*/, const std::string& /*message*/)
{
    return false;
}

void remove_tooltip(int /*id*/) {}
void clear_tooltips() {}
void clear_tooltips(const SDL_Rect& /*rect*/) {}
void process(int /*mousex*/, int /*mousey*/) {}
bool click(int /*mousex*/, int /*mousey*/) { return false; }

} // namespace tooltips

// ---- gui2::top_level_drawable ----
#include "gui/core/top_level_drawable.hpp"

namespace gui2 {

top_level_drawable::top_level_drawable() {}
top_level_drawable::~top_level_drawable() {}
top_level_drawable::top_level_drawable(const top_level_drawable&) {}
top_level_drawable& top_level_drawable::operator=(const top_level_drawable&) { return *this; }
top_level_drawable::top_level_drawable(top_level_drawable&&) noexcept {}
top_level_drawable& top_level_drawable::operator=(top_level_drawable&&) noexcept { return *this; }

} // namespace gui2

// ---- draw_manager ----
#include "draw_manager.hpp"

namespace draw_manager {

void invalidate_region(const rect& /*region*/) {}
void invalidate_all() {}
void sparkle() {}
std::chrono::milliseconds get_frame_length() { return std::chrono::milliseconds{16}; }
void register_drawable(gui2::top_level_drawable* /*tld*/) {}
void deregister_drawable(gui2::top_level_drawable* /*tld*/) {}
void raise_drawable(gui2::top_level_drawable* /*tld*/) {}

} // namespace draw_manager

// ---- gui::floating_textbox constructor ----
// (close/tab/memorize_command already defined above)

gui::floating_textbox::floating_textbox()
    : box_()
    , check_()
    , mode_(TEXTBOX_NONE)
    , label_string_()
    , label_(0)
    , command_history_()
{}

// ---- fake_unit_manager ----
#include "fake_unit_manager.hpp"

void fake_unit_manager::place_temporary_unit(internal_ptr_type u)
{
    fake_units_.push_back(u);
}

int fake_unit_manager::remove_temporary_unit(internal_ptr_type u)
{
    auto it = std::find(fake_units_.begin(), fake_units_.end(), u);
    if(it != fake_units_.end()) {
        fake_units_.erase(it);
        return 1;
    }
    return 0;
}

// ---- image:: functions ----
#include "picture.hpp"
#include "sdl/point.hpp"

namespace image {

bool exists(const locator& /*i_locator*/) { return false; }
void flush_cache() {}
point get_size(const locator& /*i_locator*/, bool /*skip_cache*/) { return {0, 0}; }
bool is_empty_hex(const locator& /*i_locator*/) { return true; }
bool precached_file_exists(const std::string& /*file*/) { return false; }
void precache_file_existence(const std::string& /*subdir*/) {}

locator::locator(const std::string& filename)
    : type_(FILE)
    , filename_(filename)
{}

locator::locator(const std::string& filename, const std::string& modifications)
    : type_(SUB_FILE)
    , filename_(filename)
    , modifications_(modifications)
{}

locator::locator(const std::string& filename, const map_location& loc,
    int center_x, int center_y, const std::string& modifications)
    : type_(SUB_FILE)
    , filename_(filename)
    , modifications_(modifications)
    , loc_(loc)
    , center_x_(center_x)
    , center_y_(center_y)
{}

} // namespace image

// ---- surface methods ----
// sdl/surface.cpp is in libwesnoth_sdl, not compiled into headless.
// Provide minimal stubs for all non-inline surface methods.
#include "sdl/surface.hpp"
#include "sdl/point.hpp"

surface::surface(SDL_Surface* surf) : surface_(surf) {}
surface::surface(int /*w*/, int /*h*/) : surface_(nullptr) {}
surface::surface(const surface& s) : surface_(s.surface_) { if(surface_) ++surface_->refcount; }
surface::surface(surface&& s) noexcept : surface_(s.surface_) { s.surface_ = nullptr; }
surface::~surface() { if(surface_) SDL_FreeSurface(surface_); }

surface& surface::operator=(const surface& s)
{
    if(surface_) SDL_FreeSurface(surface_);
    surface_ = s.surface_;
    if(surface_) ++surface_->refcount;
    return *this;
}

surface& surface::operator=(surface&& s) noexcept
{
    if(surface_) SDL_FreeSurface(surface_);
    surface_ = s.surface_;
    s.surface_ = nullptr;
    return *this;
}

surface surface::clone() const { return surface{}; }
point surface::size() const { return {0, 0}; }
std::size_t surface::area() const { return 0; }

std::ostream& operator<<(std::ostream& s, const surface& /*surf*/) { return s; }

// ---- wb::side_actions_container ----
#include "whiteboard/side_actions.hpp"

namespace wb {

side_actions_container::side_actions_container()
    : actions_()
{}

} // namespace wb

// ---- plugins_manager ----
#include "scripting/plugins/manager.hpp"

plugins_manager* plugins_manager::get() { return nullptr; }
void plugins_manager::notify_event(const std::string& /*name*/, const config& /*data*/) {}

// ---- plugins_context additional methods ----
void plugins_context::set_callback_execute(lua_kernel_base& /*kernel*/) {}
void plugins_context::set_accessor_bool(const std::string& name,
                                        const std::function<bool(config)>& f)
{
    accessors_[name] = [f](config cfg) { config r; r["value"] = f(cfg); return r; };
}

// ---- font::INACTIVE_COLOR ----
#include "font/standard_colors.hpp"

namespace font {
    const color_t INACTIVE_COLOR{181, 181, 181, 255};
}

// ---- markup::help_to_pango_markup ----
#include "serialization/markup.hpp"

namespace markup {
std::string help_to_pango_markup(const std::string& help_markup)
{
    return help_markup;
}
}

// ---- help::get_unit_type_help_id / show_* ----
// help_manager is already stubbed; show_* functions are stubs
namespace help {
std::string get_unit_type_help_id(const unit_type& /*t*/) { return {}; }
void show_help(const std::string& /*show_topic*/) {}
void show_unit_description(const unit_type& /*t*/) {}
void show_unit_description(const unit& /*u*/) {}
void show_terrain_description(const terrain_type& /*t*/) {}
}

// ---- mp::send_to_server ----
#include "game_initialization/multiplayer.hpp"

namespace mp {
void send_to_server(const config& /*data*/) {}
}

// ---- ng::depcheck::manager ----
#include "game_initialization/depcheck.hpp"

namespace ng::depcheck {

manager::manager(const game_config_view& /*gamecfg*/, bool /*mp*/)
    : era_(), scenario_(), mods_(), prev_era_(), prev_scenario_(), prev_mods_()
{}

void manager::try_era(const std::string& id, bool /*force*/) { era_ = id; }
void manager::try_scenario(const std::string& id, bool /*force*/) { scenario_ = id; }
void manager::try_modifications(const std::vector<std::string>& ids, bool /*force*/) { mods_ = ids; }
void manager::try_modification_by_id(const std::string& id, bool activate, bool /*force*/)
{
    auto it = std::find(mods_.begin(), mods_.end(), id);
    if(activate && it == mods_.end()) mods_.push_back(id);
    else if(!activate && it != mods_.end()) mods_.erase(it);
}
void manager::try_era_by_index(int index, bool /*force*/) { (void)index; }
void manager::try_scenario_by_index(int index, bool /*force*/) { (void)index; }
bool manager::is_modification_active(int /*index*/) const { return false; }
bool manager::is_modification_active(const std::string& /*id*/) const { return false; }
int manager::get_era_index() const { return 0; }
int manager::get_era_index(const std::string& /*id*/) const { return 0; }
int manager::get_scenario_index() const { return 0; }
void manager::insert_element(component_type /*type*/, const config& /*data*/, int /*index*/) {}

} // namespace ng::depcheck

// ---- unit_animation_component::reset_affect_adjacent ----
#include "units/animation_component.hpp"

void unit_animation_component::reset_affect_adjacent(const unit_map& /*units*/) {}

// ---- php_crypt_blowfish_rn (extern "C") ----
extern "C" {
#include "crypt_blowfish/crypt_blowfish.h"

char* php_crypt_blowfish_rn(const char* /*key*/, const char* /*setting*/,
    char* output, int /*size*/)
{
    if(output) output[0] = '\0';
    return output;
}
} // extern "C"

// ---- markup::img ----
#include "serialization/markup.hpp"

namespace markup {
std::string img(const std::string& /*src*/, const std::string& /*align*/, bool /*floating*/)
{
    return std::string();
}
} // namespace markup

// ---- wb::future_map / wb::future_map_if_active ----
#include "whiteboard/manager.hpp"

namespace wb {
future_map::future_map() {}
future_map::~future_map() {}

future_map_if_active::future_map_if_active() {}
future_map_if_active::~future_map_if_active() {}
} // namespace wb
