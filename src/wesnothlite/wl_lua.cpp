/**
 * wl_lua.cpp  —  Lua event-posting glue and hook implementations
 *
 * Defines:
 *   - tl_channel / tl_skip_next_moveto   (thread-locals used by all post fns)
 *   - lua_post_*                          (C functions registered in Lua)
 *   - WL_EVENT_LUA                        (Lua hook snippet)
 *   - wl_lua_setup()                      (registers fns + injects snippet)
 *   - wl_hook_*()                         (callable from headless stubs)
 */

#include "wl_impl.hpp"
#include "wl_hooks.hpp"
#include "lua/lauxlib.h"
#include "resources.hpp"
#include "scripting/game_lua_kernel.hpp"
#include "serialization/string_utils.hpp"
#include "filesystem.hpp"

/* =========================================================================
 * Thread-local state (defined here; declared extern in wl_impl.hpp)
 * ========================================================================= */

thread_local WLChannel* tl_channel          = nullptr;
thread_local bool       tl_skip_next_moveto = false;

/* =========================================================================
 * Lua-callable event-posting functions
 * Registered in the game Lua kernel as wesnoth.wl_post_*
 * ========================================================================= */

namespace {

static WLEventInternal make_unit_loc_event(WL_EventType t, lua_State* L)
{
    /* args: unit_id(1) type_id(2) side(3) x(4) y(5) */
    WLEventInternal ev;
    ev.type = t;
    ev.s1   = luaL_optstring(L, 1, "");  /* unit_id  */
    ev.s2   = luaL_optstring(L, 2, "");  /* type_id  */
    ev.i1   = static_cast<int>(luaL_optinteger(L, 3, 0)); /* side */
    ev.loc1 = { static_cast<int>(luaL_optinteger(L, 4, 0)),
                static_cast<int>(luaL_optinteger(L, 5, 0)) };
    return ev;
}

/* wesnoth.wl_post_move(unit_id, type_id, side, from_x, from_y, to_x, to_y) */
static int lua_post_move(lua_State* L)
{
    if(!tl_channel) return 0;
    if(tl_skip_next_moveto) { tl_skip_next_moveto = false; return 0; }
    WLEventInternal ev;
    ev.type = WL_EVENT_UNIT_MOVE;
    ev.s1   = luaL_optstring(L, 1, "");   /* unit_id */
    ev.s2   = luaL_optstring(L, 2, "");   /* type_id (unused for move) */
    ev.i1   = static_cast<int>(luaL_optinteger(L, 3, 0)); /* side */
    ev.loc1 = { static_cast<int>(luaL_optinteger(L, 4, 0)),
                static_cast<int>(luaL_optinteger(L, 5, 0)) }; /* from */
    ev.loc2 = { static_cast<int>(luaL_optinteger(L, 6, 0)),
                static_cast<int>(luaL_optinteger(L, 7, 0)) }; /* to   */
    tl_channel->post_event(std::move(ev));
    return 0;
}

static int spawn_internal(lua_State* L)
{
    if(!tl_channel) return 0;
    WLEventInternal ev;
    ev.type = WL_EVENT_UNIT_SPAWN;
    ev.s1   = luaL_optstring(L, 1, "");   /* type_id */
    ev.s2   = luaL_optstring(L, 2, "");   /* unit_id */
    ev.i1   = static_cast<int>(luaL_optinteger(L, 3, 0));
    ev.loc1 = { static_cast<int>(luaL_optinteger(L, 4, 0)),
                static_cast<int>(luaL_optinteger(L, 5, 0)) };
    tl_channel->post_event(std::move(ev));
    return 0;
}

/* wesnoth.wl_post_recruit(type_id, unit_id, side, x, y) */
static int lua_post_recruit(lua_State* L) { return spawn_internal(L); }

/* wesnoth.wl_post_recall(unit_id, type_id, side, x, y) */
static int lua_post_recall(lua_State* L)  { return spawn_internal(L); }

/* wesnoth.wl_post_die(unit_id, type_id, side, x, y, killer_id) */
static int lua_post_die(lua_State* L)
{
    if(!tl_channel) return 0;
    WLEventInternal ev;
    ev.type = WL_EVENT_UNIT_DIE;
    ev.s1   = luaL_optstring(L, 1, "");
    ev.s2   = luaL_optstring(L, 2, "");
    ev.i1   = static_cast<int>(luaL_optinteger(L, 3, 0));
    ev.loc1 = { static_cast<int>(luaL_optinteger(L, 4, 0)),
                static_cast<int>(luaL_optinteger(L, 5, 0)) };
    ev.s3   = luaL_optstring(L, 6, "");   /* killer_id */
    tl_channel->post_event(std::move(ev));
    return 0;
}

/* wesnoth.wl_post_turn_end(side, turn) */
static int lua_post_turn_end(lua_State* L)
{
    if(!tl_channel) return 0;
    WLEventInternal ev;
    ev.type = WL_EVENT_SIDE_TURN_END;
    ev.i1   = static_cast<int>(luaL_optinteger(L, 1, 0));
    ev.i2   = static_cast<int>(luaL_optinteger(L, 2, 0));
    tl_channel->post_event(std::move(ev));
    return 0;
}

/* wesnoth.wl_post_message(speaker, portrait, text) */
static int lua_post_message(lua_State* L)
{
    if(!tl_channel) return 0;
    WLEventInternal ev;
    ev.type = WL_EVENT_MESSAGE;
    ev.s1   = luaL_optstring(L, 1, "");
    ev.s2   = luaL_optstring(L, 2, "");
    ev.s3   = luaL_optstring(L, 3, "");
    tl_channel->post_event(std::move(ev));
    return 0;
}

/**
 * wesnoth.wl_request_choice(kind, prompt, options...)
 *
 * Post WL_EVENT_CHOICE_NEEDED and block until wl_choose() delivers a result.
 * Returns the chosen option index (0-based).
 */
static int lua_request_choice(lua_State* L)
{
    if(!tl_channel) { lua_pushinteger(L, 0); return 1; }
    int kind = static_cast<int>(luaL_optinteger(L, 1, WL_CHOICE_MESSAGE));
    std::string prompt = luaL_optstring(L, 2, "");
    std::vector<std::string> options;
    int n = lua_gettop(L);
    for(int i = 3; i <= n; ++i)
        options.push_back(luaL_optstring(L, i, ""));
    int result = tl_channel->request_choice(
        static_cast<WL_ChoiceKind>(kind), prompt, options);
    lua_pushinteger(L, result);
    return 1;
}

} // anonymous namespace

/* =========================================================================
 * Lua hook snippet
 * Injected into the game Lua kernel after start to hook game events.
 * ========================================================================= */

static const char WL_EVENT_LUA[] = R"LUA(
local function uid(u)  return u and u.id   or "" end
local function uty(u)  return u and u.type or "" end
local function usd(u)  return u and u.side or 0  end

wesnoth.game_events.add_repeating("moveto", function()
    local ctx = wesnoth.current.event_context
    local u = wesnoth.units.get(ctx.unit_x, ctx.unit_y)
    wesnoth.wl_post_move(uid(u), uty(u), usd(u),
        ctx.x2 or 0, ctx.y2 or 0,
        ctx.unit_x or 0, ctx.unit_y or 0)
end)

wesnoth.game_events.add_repeating("recruit", function()
    local ctx = wesnoth.current.event_context
    local u = wesnoth.units.get(ctx.unit_x, ctx.unit_y)
    wesnoth.wl_post_recruit(uty(u), uid(u), usd(u),
        ctx.unit_x or 0, ctx.unit_y or 0)
end)

wesnoth.game_events.add_repeating("recall", function()
    local ctx = wesnoth.current.event_context
    local u = wesnoth.units.get(ctx.unit_x, ctx.unit_y)
    wesnoth.wl_post_recall(uid(u), uty(u), usd(u),
        ctx.unit_x or 0, ctx.unit_y or 0)
end)

wesnoth.game_events.add_repeating("die", function()
    local ctx = wesnoth.current.event_context
    local u = wesnoth.units.get(ctx.unit_x, ctx.unit_y)
    local killer = wesnoth.units.get(ctx.x2, ctx.y2)
    wesnoth.wl_post_die(uid(u), uty(u), usd(u),
        ctx.unit_x or 0, ctx.unit_y or 0, uid(killer))
end)

wesnoth.game_events.add_repeating("turn_end", function()
    wesnoth.wl_post_turn_end(wesnoth.current.side, wesnoth.current.turn)
end)
)LUA";

/* =========================================================================
 * wl_lua_setup — registers C functions + injects Lua hooks
 * Call once immediately after the game Lua kernel becomes live.
 * ========================================================================= */

void wl_lua_setup()
{
    if(!resources::lua_kernel) return;
    lua_State* L = resources::lua_kernel->get_state();

    static const luaL_Reg wl_fns[] = {
        { "wl_post_move",      lua_post_move      },
        { "wl_post_recruit",   lua_post_recruit   },
        { "wl_post_recall",    lua_post_recall    },
        { "wl_post_die",       lua_post_die       },
        { "wl_post_turn_end",  lua_post_turn_end  },
        { "wl_post_message",   lua_post_message   },
        { "wl_request_choice", lua_request_choice },
        { nullptr, nullptr }
    };

    lua_getglobal(L, "wesnoth");
    for(const luaL_Reg* r = wl_fns; r->name; ++r) {
        lua_pushcfunction(L, r->func);
        lua_setfield(L, -2, r->name);
    }
    lua_pop(L, 1);

    config lua_cfg;
    lua_cfg["code"] = WL_EVENT_LUA;
    resources::lua_kernel->run_lua_tag(lua_cfg);
}

/* =========================================================================
 * Hooks callable from headless stubs (wl_hooks.hpp)
 * These run in the game thread; tl_channel may be null outside a session.
 * ========================================================================= */

void wl_hook_story_part(const std::string& title,
                        const std::string& text,
                        const std::string& background)
{
    if(!tl_channel) return;
    WLEventInternal ev;
    ev.type = WL_EVENT_STORY;
    ev.s1   = title;
    ev.s2   = text;
    // Resolve the WML image path to an absolute filesystem path so the
    // browser can load it directly (e.g. "story/foo.webp" → "/game/data/…").
    if(!background.empty()) {
        auto resolved = filesystem::get_binary_file_location("images", background);
        ev.s3 = resolved ? *resolved : background;
    }
    tl_channel->post_event(std::move(ev));
}

void wl_hook_music_change(const std::string& path,
                          const std::string& title)
{
    if(!tl_channel) return;
    WLEventInternal ev;
    ev.type = WL_EVENT_MUSIC_CHANGE;
    ev.s1   = path;
    ev.s2   = title;
    tl_channel->post_event(std::move(ev));
}

int wl_hook_message(const std::string& speaker,
                    const std::string& portrait,
                    const std::string& text,
                    const std::vector<std::string>& options)
{
    if(!tl_channel) return 0;

    /* Post the narrative message event so the frontend can display it
     * with speaker name, portrait and text before asking for a choice. */
    {
        WLEventInternal ev;
        ev.type = WL_EVENT_MESSAGE;
        ev.s1   = speaker;
        /* Resolve portrait path to WASM VFS absolute path (same as backgrounds). */
        if(!portrait.empty()) {
            auto resolved = filesystem::get_binary_file_location("images", portrait);
            ev.s2 = resolved ? *resolved : portrait;
        }
        ev.s3 = text;
        tl_channel->post_event(std::move(ev));
    }

    /* Determine whether there are real player choices beyond a simple dismiss. */
    bool has_real_options = options.size() > 1 ||
                            (options.size() == 1 && !options[0].empty());

    if(has_real_options) {
        /* Real choices: also post WL_EVENT_CHOICE_NEEDED so the frontend
         * can present the options after the player dismisses the message.
         * The game thread blocks until the player picks an option. */
        return tl_channel->request_choice(WL_CHOICE_MESSAGE, text, options, speaker);
    } else {
        /* Simple message: block until the frontend ACKs with CHOOSE_OPTION=0. */
        //tl_channel->await_ack();  // TEST: non-blocking
        return 0;
    }
}
