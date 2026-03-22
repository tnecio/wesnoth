/* Headless stub: display.cpp
 * Provides no-op implementations for display methods not needed in headless mode.
 * This replaces the real display.cpp in the headless build.
 */
#include "display.hpp"
#include "map/label.hpp"
#include "color.hpp"
#include "config.hpp"
#include "map/location.hpp"
#include "sdl/rect.hpp"
#include "team.hpp"
#include "time_of_day.hpp"
#include "units/unit.hpp"
// Need complete types for unique_ptr destructors
#include "fake_unit_manager.hpp"
#include "terrain/builder.hpp"
#include "gui/core/tracked_drawable.hpp"

// tracked_drawable stubs
namespace gui2 {
tracked_drawable::tracked_drawable()
    : frametimes_(60)
    , render_count_(0)
    , render_counter_(0)
    , last_lap_()
    , last_render_()
{}
tracked_drawable::~tracked_drawable() {}
void tracked_drawable::process() {}
void tracked_drawable::update_count() {}
auto tracked_drawable::get_info() const -> utils::optional<frame_info> { return {}; }
auto tracked_drawable::get_times() const -> times { return {}; }
} // namespace gui2

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

display* display::singleton_ = nullptr;
unsigned int display::zoom_ = 72;       // game_config::tile_size default
unsigned int display::last_zoom_ = 72;

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

display::display(const display_context* dc,
		std::weak_ptr<wb::manager> wb,
		reports& reports_object,
		const std::string& /*theme_id*/,
		const config& /*level*/)
	: dc_(dc)
	, halo_man_()
	, wb_(wb)
	, exclusive_unit_draw_requests_()
	, viewing_team_index_(0)
	, dont_show_all_(false)
	, viewport_origin_()
	, view_locked_(false)
	, theme_(config(), SDL_Rect{0, 0, 800, 600})
	, zoom_index_(0)
	, fake_unit_man_(nullptr)
	, builder_(nullptr)
	, minimap_renderer_()
	, minimap_location_()
	, redraw_background_(false)
	, invalidateAll_(false)
	, diagnostic_label_(0)
	, invalidateGameStatus_(false)
	, map_labels_(nullptr)
	, reports_object_(&reports_object)
	, scroll_event_("scrolled")
	, animate_map_(false)
	, animate_water_(false)
	, playing_team_index_(0)
	, map_screenshot_(false)
	, reach_map_changed_(false)
	, reach_map_team_index_(0)
	, invalidated_hexes_(0)
	, drawn_hexes_(0)
{
	blindfold_ctr_ = 0;
	assert(singleton_ == nullptr);
	singleton_ = this;
}

display::~display()
{
	singleton_ = nullptr;
}

// ---------------------------------------------------------------------------
// display method stubs
// ---------------------------------------------------------------------------

bool display::add_exclusive_draw(const map_location& /*loc*/, const unit& /*u*/)
{
	return false;
}

std::string display::remove_exclusive_draw(const map_location& /*loc*/)
{
	return {};
}

const theme::action* display::action_pressed()
{
	return nullptr;
}

void display::add_overlay(const map_location& /*loc*/, overlay&& /*ov*/)
{
}

void display::adjust_color_overlay(int /*r*/, int /*g*/, int /*b*/)
{
}

void display::announce(const std::string& /*msg*/, const color_t& /*color*/,
	const display::announce_options& /*options*/)
{
}

void display::blindfold(bool flag)
{
	if(flag) {
		++blindfold_ctr_;
	} else {
		--blindfold_ctr_;
	}
}

void display::change_display_context(const display_context* dc)
{
	dc_ = dc;
}

void display::fade_to(const color_t& /*color*/, const std::chrono::milliseconds& /*duration*/)
{
}

std::shared_ptr<gui::button> display::find_action_button(const std::string& /*id*/)
{
	return nullptr;
}

std::shared_ptr<gui::button> display::find_menu_button(const std::string& /*id*/)
{
	return nullptr;
}

bool display::fogged(const map_location& /*loc*/) const
{
	return false;
}

point display::get_location(const map_location& /*loc*/) const
{
	return {0, 0};
}

const time_of_day& display::get_time_of_day(const map_location& /*loc*/) const
{
	static const time_of_day default_tod;
	return default_tod;
}

void display::invalidate_all()
{
}

bool display::invalidate(const map_location& /*loc*/)
{
	return false;
}

bool display::invalidate(const std::set<map_location>& /*locs*/)
{
	return false;
}

bool display::is_blindfolded() const
{
	return blindfold_ctr_ > 0;
}

map_labels& display::labels()
{
	static map_labels* stub_labels_ = nullptr;
	if(!stub_labels_) {
		stub_labels_ = new map_labels(nullptr);
	}
	return *stub_labels_;
}

const map_labels& display::labels() const
{
	return const_cast<display*>(this)->labels();
}

rect display::map_area() const
{
	return {0, 0, 800, 600};
}

rect display::map_outside_area() const
{
	return {0, 0, 800, 600};
}

const theme::menu* display::menu_pressed()
{
	return nullptr;
}

void display::queue_repaint()
{
}

void display::queue_rerender()
{
}

void display::recalculate_minimap()
{
}

void display::redraw_minimap()
{
}

void display::reinit_flags_for_team(const team& /*t*/)
{
}

void display::reload_map()
{
}

void display::remove_overlay(const map_location& /*loc*/)
{
}

void display::remove_single_overlay(const map_location& /*loc*/, const std::string& /*toDelete*/)
{
}

bool display::scroll(const point& /*amount*/, bool /*force*/)
{
	return false;
}

void display::scroll_to_tile(const map_location& /*loc*/, display::SCROLL_TYPE /*scroll_type*/,
	bool /*check_fogged*/, bool /*force*/)
{
}

void display::scroll_to_tiles(map_location /*loc1*/, map_location /*loc2*/,
	SCROLL_TYPE /*scroll_type*/, bool /*check_fogged*/,
	double /*add_spacing*/, bool /*force*/)
{
}

void display::scroll_to_tiles(const std::vector<map_location>& /*locs*/,
	SCROLL_TYPE /*scroll_type*/, bool /*check_fogged*/,
	bool /*only_if_possible*/, double /*add_spacing*/, bool /*force*/)
{
}

void display::set_diagnostic(const std::string& /*msg*/)
{
}

void display::set_fade(const color_t& /*color*/)
{
}

void display::set_playing_team_index(std::size_t team)
{
	playing_team_index_ = team;
}

void display::set_viewing_team_index(std::size_t team, bool /*observe*/)
{
	viewing_team_index_ = team;
}

void display::set_theme(const std::string& /*new_theme*/)
{
}

bool display::set_zoom(unsigned int /*amount*/, const bool /*validate_value_and_set_index*/)
{
	return false;
}

bool display::set_zoom(bool /*increase*/)
{
	return false;
}

bool display::shrouded(const map_location& /*loc*/) const
{
	return false;
}

double display::turbo_speed() const
{
	return 1.0;
}

void display::update_tod(const time_of_day* /*tod_override*/)
{
}

void display::write(config& /*cfg*/) const
{
}

void display::select_hex(map_location /*hex*/)
{
}

void display::highlight_hex(map_location /*hex*/)
{
}

void display::init_flags()
{
}

// top_level_drawable interface stubs
void display::update()
{
}

void display::layout()
{
}

void display::render()
{
}

bool display::expose(const rect& /*region*/)
{
	return false;
}

rect display::screen_location()
{
	return {0, 0, 800, 600};
}

// rect_of_hexes iterator stubs
display::rect_of_hexes::iterator& display::rect_of_hexes::iterator::operator++()
{
	return *this;
}

display::rect_of_hexes::iterator display::rect_of_hexes::begin() const
{
	return iterator(map_location(), *this);
}

display::rect_of_hexes::iterator display::rect_of_hexes::end() const
{
	return iterator(map_location(), *this);
}

const display::rect_of_hexes display::hexes_under_rect(const rect& /*r*/) const
{
	rect_of_hexes h;
	h.left = h.right = 0;
	h.top[0] = h.top[1] = 0;
	h.bottom[0] = h.bottom[1] = -1; // empty range
	return h;
}

bool display::outside_area(const rect& /*area*/, const int /*x*/, const int /*y*/)
{
	return false;
}

// Remaining methods called from headless paths
void display::add_redraw_observer(const std::function<void(display&)>& /*f*/)
{
}

void display::clear_redraw_observers()
{
}

rect display::max_map_area() const
{
	return {0, 0, 800, 600};
}

const rect& display::minimap_area() const
{
	static const rect r{0, 0, 0, 0};
	return r;
}

const rect& display::palette_area() const
{
	static const rect r{0, 0, 0, 0};
	return r;
}

const rect& display::unit_image_area() const
{
	static const rect r{0, 0, 0, 0};
	return r;
}

void display::drawing_buffer_add(const drawing_layer /*layer*/, const map_location& /*loc*/,
	decltype(draw_helper::do_draw) /*draw_func*/)
{
}

void display::invalidate_animations()
{
}

void display::reset_standing_animations()
{
}

void display::rebuild_all()
{
}

void display::bounds_check_position()
{
}

void display::bounds_check_position(int& /*xpos*/, int& /*ypos*/) const
{
}

bool display::tile_fully_on_screen(const map_location& /*loc*/) const
{
	return false;
}

bool display::tile_nearly_on_screen(const map_location& /*loc*/) const
{
	return false;
}

void display::fade_tod_mask(const std::string& /*old*/, const std::string& /*new_*/)
{
}

void display::add_arrow(arrow& /*a*/)
{
}

void display::remove_arrow(arrow& /*a*/)
{
}

void display::update_arrow(arrow& /*a*/)
{
}

bool display::zoom_at_max()
{
	return false;
}

bool display::zoom_at_min()
{
	return false;
}

void display::toggle_default_zoom()
{
}

bool display::propagate_invalidation(const std::set<map_location>& /*locs*/)
{
	return false;
}

bool display::invalidate_locations_in_rect(const rect& /*rect*/)
{
	return false;
}

bool display::invalidate_visible_locations_in_rect(const rect& /*rect*/)
{
	return false;
}

void display::invalidate_animations_location(const map_location& /*loc*/)
{
}

surface display::screenshot(bool /*map_screenshot*/)
{
	return surface();
}

map_location display::minimap_location_on(int /*x*/, int /*y*/)
{
	return map_location();
}

map_location display::hex_clicked_on(int /*x*/, int /*y*/) const
{
	return map_location();
}

map_location display::pixel_position_to_hex(int /*x*/, int /*y*/) const
{
	return map_location();
}

void display::create_buttons()
{
}

void display::layout_buttons()
{
}

void display::draw_buttons()
{
}

void display::refresh_report(const std::string& /*report_name*/, const config* /*new_cfg*/)
{
}

void display::draw_report(const std::string& /*report_name*/, bool /*test_run*/)
{
}

bool display::draw_reports(const rect& /*region*/)
{
	return false;
}

void display::draw_minimap_units()
{
}

void display::draw_text_in_hex(const map_location& /*loc*/,
	const drawing_layer /*layer*/, const std::string& /*text*/, std::size_t /*font_size*/,
	color_t /*color*/, double /*x_in_hex*/, double /*y_in_hex*/)
{
}

rect display::get_clip_rect() const
{
	return map_area();
}

void display::draw_invalidated()
{
}

void display::draw_hex(const map_location& /*loc*/)
{
}

submerge_data display::get_submerge_data(const rect& /*dest*/, double /*submerge*/,
	const point& /*size*/, uint8_t /*alpha*/, bool /*hreverse*/, bool /*vreverse*/)
{
	return submerge_data{};
}

void display::set_prevent_draw(bool /*pd*/)
{
}

bool display::get_prevent_draw()
{
	return false;
}

bool display::unit_can_draw_here(const map_location& /*loc*/, const unit& /*u*/) const
{
	return true;
}

rect display::get_location_rect(const map_location& /*loc*/) const
{
	return {0, 0, 0, 0};
}

const team& display::playing_team() const
{
	static const team dummy_team{};
	return dummy_team;
}

const team& display::viewing_team() const
{
	static const team dummy_team{};
	return dummy_team;
}
