/* Headless stub: game_display.cpp
 * Provides no-op implementations for game_display and display_chat_manager methods.
 * This replaces the real game_display.cpp in the headless build.
 */
#include "game_display.hpp"
#include "display_chat_manager.hpp"
#include "color.hpp"
#include "game_board.hpp"
#include "map/location.hpp"
#include "overlay.hpp"
#include "pathfind/pathfind.hpp"
#include "terrain/translation.hpp"
#include "time_of_day.hpp"
// Need complete types for unique_ptr destructors
#include "fake_unit_manager.hpp"
#include "terrain/builder.hpp"

// ---------------------------------------------------------------------------
// game_display constructor / destructor
// ---------------------------------------------------------------------------

game_display::game_display(game_board& board,
		std::weak_ptr<wb::manager> wb,
		reports& reports_object,
		const std::string& theme_id,
		const config& level)
	: display(&board, wb, reports_object, theme_id, level)
	, overlay_map_()
	, attack_indicator_src_()
	, attack_indicator_dst_()
	, route_()
	, displayedUnitHex_()
	, first_turn_(true)
	, in_game_(false)
	, chat_man_(new display_chat_manager(*this))
	, mode_(RUNNING)
	, needs_rebuild_(false)
{
}

game_display::~game_display()
{
}

// ---------------------------------------------------------------------------
// Pure-virtual override required by display
// ---------------------------------------------------------------------------

display::overlay_map& game_display::get_overlays()
{
	return overlay_map_;
}

// ---------------------------------------------------------------------------
// game_display method stubs
// ---------------------------------------------------------------------------

void game_display::begin_game()
{
	in_game_ = true;
}

void game_display::display_unit_hex(map_location /*hex*/)
{
}

void game_display::float_label(const map_location& /*loc*/, const std::string& /*text*/,
	const color_t& /*color*/)
{
}

void game_display::highlight_reach(const pathfind::paths& /*paths_list*/)
{
}

void game_display::invalidate_unit_after_move(const map_location& /*src*/, const map_location& /*dst*/)
{
}

bool game_display::maybe_rebuild()
{
	if(needs_rebuild_) {
		needs_rebuild_ = false;
		return true;
	}
	return false;
}

void game_display::needs_rebuild(bool b)
{
	needs_rebuild_ = b;
}

void game_display::new_turn()
{
}

void game_display::scroll_to_leader(int /*side*/, display::SCROLL_TYPE /*scroll_type*/, bool /*force*/)
{
}

void game_display::set_game_mode(game_display::game_mode mode)
{
	mode_ = mode;
}

void game_display::set_route(const pathfind::marked_route* route)
{
	if(route) {
		route_ = *route;
	} else {
		route_ = pathfind::marked_route();
	}
}

bool game_display::unhighlight_reach()
{
	return false;
}

// Virtual overrides from display
void game_display::select_hex(map_location hex)
{
	display::select_hex(hex);
}

void game_display::highlight_hex(map_location hex)
{
	display::highlight_hex(hex);
}

void game_display::update()
{
	display::update();
}

void game_display::layout()
{
	display::layout();
}

void game_display::render()
{
	display::render();
}

void game_display::draw_invalidated()
{
}

void game_display::draw_hex(const map_location& /*loc*/)
{
}

const time_of_day& game_display::get_time_of_day(const map_location& loc) const
{
	return display::get_time_of_day(loc);
}

bool game_display::has_time_area() const
{
	return false;
}

void game_display::highlight_another_reach(const pathfind::paths& /*paths_list*/,
	const map_location& /*goal*/)
{
}

void game_display::set_attack_indicator(const map_location& src, const map_location& dst)
{
	attack_indicator_src_ = src;
	attack_indicator_dst_ = dst;
}

void game_display::clear_attack_indicator()
{
	attack_indicator_src_ = map_location();
	attack_indicator_dst_ = map_location();
}

void game_display::draw_movement_info(const map_location& /*loc*/)
{
}

// ---------------------------------------------------------------------------
// display_chat_manager stubs
// ---------------------------------------------------------------------------

void display_chat_manager::add_chat_message(const std::chrono::system_clock::time_point& /*time*/,
	const std::string& /*speaker*/, int /*side*/, const std::string& /*msg*/,
	events::chat_handler::MESSAGE_TYPE /*type*/, bool /*bell*/)
{
}

void display_chat_manager::prune_chat_messages(bool /*remove_all*/)
{
}
