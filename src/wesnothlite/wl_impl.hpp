/**
 * wl_impl.hpp  —  WesnothLite internal shared declarations
 *
 * Include from wl_*.cpp translation units only.  Not part of the public API.
 */
#pragma once

#include "wl_engine.hpp"   // WLChannel, WLEngineImpl, WLArena, WLEventInternal, …
#include "log.hpp"
#include "lua/lua.h"

#include <string>
#include <vector>

/* =========================================================================
 * Shared mutable globals
 * ========================================================================= */

/** Data-root directory set by wl_init().  Used by resolve_img(). */
extern std::string g_data_root;

/** Game-thread → API-thread channel pointer (set by wl_run_game_thread). */
extern thread_local WLChannel* tl_channel;

/** Set by WLController::do_move() before calling move_unit_and_record so
 *  the Lua moveto handler skips posting a duplicate (path-less) event. */
extern thread_local bool tl_skip_next_moveto;

/* =========================================================================
 * Logging — shared domain across all translation units
 * ========================================================================= */

namespace wl_detail {
inline lg::log_domain& log_domain()
{
    static lg::log_domain d("wesnothlite");
    return d;
}
} // namespace wl_detail

#define LOG_WL  LOG_STREAM(info, wl_detail::log_domain())
#define ERR_WL  LOG_STREAM(err,  wl_detail::log_domain())

/* =========================================================================
 * Cross-file helper declarations
 * ========================================================================= */

class unit;
class terrain_type;

/** Resolve a bare Wesnoth image path to one relative to the data dir. */
std::string resolve_img(const std::string& rel);

/**
 * Register wl_post_* Lua functions and inject the WL_EVENT_LUA hook
 * snippet.  Call once, immediately after the game Lua kernel is live.
 */
void wl_lua_setup();

/** Run a game session.  Entry point for std::thread. */
void wl_run_game_thread(WLEngineImpl* e);

/** Serialise an internal event into a heap-allocated WL_Event. */
WL_Event* wl_materialize_event(const WLEventInternal& d, std::vector<char>& buf);

/** Fill a WL_Unit snapshot from a live unit instance. */
void fill_wl_unit(WL_Unit& out, const unit& u, WLArena& arena);

/** Classify a terrain_type into a WL_TerrainCategory. */
WL_TerrainCategory terrain_category(const terrain_type& tt);

/** Populate a WL_Attack from a unit_type attack (used by wl_queries.cpp). */
class attack_type;
void fill_wl_attack_pub(WL_Attack& out, const attack_type& atk, WLArena& arena);

/** Populate resistance array from a unit_type (used by wl_queries.cpp). */
class unit_type;
void fill_resistance_type(int (&res)[WL_DMG_COUNT], const unit_type& ut);
