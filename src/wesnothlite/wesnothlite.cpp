/**
 * wesnothlite.cpp  —  WesnothLite C API implementation
 */

#include "wl_engine.hpp"

/* ── Wesnoth game-engine headers ── */
#include "actions/attack.hpp"
#include "actions/create.hpp"
#include "actions/move.hpp"
#include "actions/undo.hpp"
#include "commandline_options.hpp"
#include "events.hpp"
#include "filesystem.hpp"
#include "gettext.hpp"
#include "game_board.hpp"
#include "game_classification.hpp"
#include "game_config.hpp"
#include "game_config_manager.hpp"
#include "game_data.hpp"
#include "game_end_exceptions.hpp"
#include "game_initialization/playcampaign.hpp"
#include "game_state.hpp"
#include "log.hpp"
#include "lua/lauxlib.h"
#include "lua/lua.h"
#include "map/map.hpp"
#include "pathfind/pathfind.hpp"
#include "playsingle_controller.hpp"
#include "recall_list_manager.hpp"
#include "resources.hpp"
#include "save_index.hpp"
#include "saved_game.hpp"
#include "savegame.hpp"
#include "scripting/game_lua_kernel.hpp"
#include "serialization/compression.hpp"
#include "serialization/string_utils.hpp"
#include "team.hpp"
#include "terrain/terrain.hpp"
#include "terrain/translation.hpp"
#include "replay_helper.hpp"
#include "synced_context.hpp"
#include "tod_manager.hpp"
#include "units/map.hpp"
#include "units/orb_status.hpp"
#include "units/types.hpp"
#include "units/unit.hpp"
#include "video.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

static lg::log_domain log_wl("wesnothlite");
#define LOG_WL  LOG_STREAM(info,  log_wl)
#define ERR_WL  LOG_STREAM(err,   log_wl)

/* =========================================================================
 * Thread-local channel pointer
 * Used by the Lua-callable C functions to post events to the channel.
 * ========================================================================= */
static thread_local WLChannel* tl_channel = nullptr;
/* Set by do_move() before calling move_unit_and_record so the Lua moveto
 * handler skips posting a duplicate (path-less) event. */
static thread_local bool tl_skip_next_moveto = false;

/* =========================================================================
 * Lua-callable event-posting functions
 * Registered in the game Lua kernel as wesnoth.wl_post_*
 * ========================================================================= */

static WLEventInternal make_unit_loc_event(WL_EventType t, lua_State* L)
{
    /* args: unit_id(1) side(2) x(3) y(4) */
    WLEventInternal ev;
    ev.type  = t;
    ev.s1    = luaL_optstring(L, 1, "");  /* unit_id    */
    ev.s2    = luaL_optstring(L, 2, "");  /* type_id    */
    ev.i1    = (int)luaL_optinteger(L, 3, 0); /* side  */
    ev.loc1  = { (int)luaL_optinteger(L, 4, 0),
                 (int)luaL_optinteger(L, 5, 0) };
    return ev;
}

/* wesnoth.wl_post_move(unit_id, type_id, side, from_x, from_y, to_x, to_y) */
static int lua_post_move(lua_State* L)
{
    if(!tl_channel) return 0;
    if(tl_skip_next_moveto) { tl_skip_next_moveto = false; return 0; }
    WLEventInternal ev;
    ev.type  = WL_EVENT_UNIT_MOVE;
    ev.s1    = luaL_optstring(L, 1, "");   /* unit_id */
    ev.s2    = luaL_optstring(L, 2, "");   /* type_id (unused for move) */
    ev.i1    = (int)luaL_optinteger(L, 3, 0); /* side */
    ev.loc1  = { (int)luaL_optinteger(L, 4, 0),
                 (int)luaL_optinteger(L, 5, 0) }; /* from */
    ev.loc2  = { (int)luaL_optinteger(L, 6, 0),
                 (int)luaL_optinteger(L, 7, 0) }; /* to   */
    tl_channel->post_event(std::move(ev));
    return 0;
}

/* wesnoth.wl_post_recruit(type_id, unit_id, side, x, y) */
static int lua_post_recruit(lua_State* L)
{
    if(!tl_channel) return 0;
    WLEventInternal ev;
    ev.type = WL_EVENT_UNIT_RECRUIT;
    ev.s1   = luaL_optstring(L, 1, "");   /* type_id */
    ev.s2   = luaL_optstring(L, 2, "");   /* unit_id */
    ev.i1   = (int)luaL_optinteger(L, 3, 0);
    ev.loc1 = { (int)luaL_optinteger(L, 4, 0),
                (int)luaL_optinteger(L, 5, 0) };
    tl_channel->post_event(std::move(ev));
    return 0;
}

/* wesnoth.wl_post_recall(unit_id, type_id, side, x, y) */
static int lua_post_recall(lua_State* L)
{
    if(!tl_channel) return 0;
    WLEventInternal ev;
    ev.type = WL_EVENT_UNIT_RECALL;
    ev.s1   = luaL_optstring(L, 1, "");
    ev.s2   = luaL_optstring(L, 2, "");
    ev.i1   = (int)luaL_optinteger(L, 3, 0);
    ev.loc1 = { (int)luaL_optinteger(L, 4, 0),
                (int)luaL_optinteger(L, 5, 0) };
    tl_channel->post_event(std::move(ev));
    return 0;
}

/* wesnoth.wl_post_die(unit_id, type_id, side, x, y, killer_id) */
static int lua_post_die(lua_State* L)
{
    if(!tl_channel) return 0;
    WLEventInternal ev;
    ev.type = WL_EVENT_UNIT_DIE;
    ev.s1   = luaL_optstring(L, 1, "");
    ev.s2   = luaL_optstring(L, 2, "");
    ev.i1   = (int)luaL_optinteger(L, 3, 0);
    ev.loc1 = { (int)luaL_optinteger(L, 4, 0),
                (int)luaL_optinteger(L, 5, 0) };
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
    ev.i1   = (int)luaL_optinteger(L, 1, 0);
    ev.i2   = (int)luaL_optinteger(L, 2, 0);
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

/* =========================================================================
 * Hooks callable from headless stubs (wl_hooks.hpp)
 * These run in the game thread; tl_channel may be null outside a session.
 * ========================================================================= */

#include "wl_hooks.hpp"

void wl_hook_story_part(const std::string& title,
                        const std::string& text,
                        const std::string& background)
{
    if(!tl_channel) return;
    WLEventInternal ev;
    ev.type = WL_EVENT_STORY;
    ev.s1   = title;
    ev.s2   = text;
    ev.s3   = background;
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
    /* Post CHOICE_NEEDED with kind=MESSAGE and block until the player
       dismisses / picks an option.  An empty options vector means "dismiss". */
    std::vector<std::string> opts = options.empty()
                                        ? std::vector<std::string>{""}
                                        : options;
    return tl_channel->request_choice(
        WL_CHOICE_MESSAGE, text, opts, speaker, portrait);
}

/* ── Image path resolution ────────────────────────────────────────────────
 * The data root for the current session (e.g. "/game/data").  Set once in
 * wl_init so that resolve_img() can strip the prefix.
 */
static std::string g_data_root;

/** Resolve a WML-relative image path to a URL-friendly path under the game
 *  data root (e.g. "core/images/units/knight.png" or
 *  "campaigns/Two_Brothers/images/units/arvith.png").
 *  Falls back to "core/images/<base>" if the file cannot be located. */
static std::string resolve_img(const std::string& rel)
{
    if(rel.empty()) return rel;
    /* Strip Wesnoth image modifiers (~BLIT, ~CROP, etc.) */
    std::string base = rel.substr(0, rel.find('~'));
    if(base.empty()) return rel;
    auto opt = filesystem::get_binary_file_location("images", base);
    if(!opt) return "core/images/" + base;
    std::string full = *opt;
    /* Strip data-root prefix to get a URL-friendly relative path */
    if(!g_data_root.empty()) {
        const std::string prefix = g_data_root + "/";
        if(full.size() > prefix.size() &&
           full.substr(0, prefix.size()) == prefix) {
            return full.substr(prefix.size());
        }
    }
    return full;
}

/**
 * wesnoth.wl_request_choice(kind, prompt, options...)
 *
 * Post WL_EVENT_CHOICE_NEEDED and block the game thread until wl_choose()
 * delivers a result.  Returns the chosen option index (0-based).
 *
 * kind   : integer WL_ChoiceKind value (WL_CHOICE_MESSAGE=0, etc.)
 * prompt : string shown to the player
 * options: one or more option strings
 *
 * Intended to be called from WML Lua filters that need a player choice,
 * e.g. a custom [message] implementation.
 */
static int lua_request_choice(lua_State* L)
{
    if(!tl_channel) {
        lua_pushinteger(L, 0);
        return 1;
    }
    int kind = static_cast<int>(luaL_optinteger(L, 1, WL_CHOICE_MESSAGE));
    std::string prompt = luaL_optstring(L, 2, "");
    std::vector<std::string> options;
    int n = lua_gettop(L);
    for(int i = 3; i <= n; ++i) {
        options.push_back(luaL_optstring(L, i, ""));
    }
    int result = tl_channel->request_choice(
        static_cast<WL_ChoiceKind>(kind), prompt, options);
    lua_pushinteger(L, result);
    return 1;
}

/* =========================================================================
 * Lua snippet injected after the game kernel is live.
 * Hooks game events and calls the wl_post_* C functions.
 * ========================================================================= */
static const char WL_EVENT_LUA[] = R"LUA(
local function uid(u)  return u and u.id   or "" end
local function uty(u)  return u and u.type or "" end
local function usd(u)  return u and u.side or 0  end
local function ux(u)   return u and u.x    or 0  end
local function uy(u)   return u and u.y    or 0  end

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
 * WLController: playsingle_controller adapted for channel-based I/O
 * ========================================================================= */
class WLController : public playsingle_controller
{
public:
    WLController(const config& level, saved_game& state,
                 std::shared_ptr<WLChannel> ch)
        : playsingle_controller(level, state)
        , ch_(std::move(ch))
    {}

protected:
    void play_human_turn() override
    {
        /* Tell the API thread we are ready for commands. */
        {
            WLEventInternal ev;
            ev.type = WL_EVENT_WAITING_FOR_INPUT;
            ev.i1   = current_side();
            ev.i2   = static_cast<int>(turn());
            ch_->post_event(std::move(ev));
        }
        ch_->set_waiting(true);

        while(!should_return_to_play_side()) {
            WLCommand cmd = ch_->wait_for_command();
            WL_Status result = process_command(cmd);
            ch_->post_result(result);

            if(result == WL_OK && cmd.type == WLCmdType::END_TURN)
                break;
            if(result == WL_OK && cmd.type == WLCmdType::UNDO)
                continue; /* undo stays in loop */
        }

        ch_->set_waiting(false);
    }

private:
    std::shared_ptr<WLChannel> ch_;

    WL_Status process_command(const WLCommand& cmd)
    {
        try {
            switch(cmd.type) {
            case WLCmdType::END_TURN:
                force_end_turn();
                return WL_OK;

            case WLCmdType::UNDO:
                if(undo_stack().can_undo()) {
                    undo_stack().undo();
                    return WL_OK;
                }
                return WL_ERR_INVALID;

            case WLCmdType::MOVE:
                return do_move(cmd.loc1, cmd.loc2);

            case WLCmdType::ATTACK:
                return do_attack(cmd.loc1, cmd.loc2, cmd.int1);

            case WLCmdType::RECRUIT:
                return do_recruit(cmd.str1,
                    map_location(cmd.loc1.x - 1, cmd.loc1.y - 1));

            case WLCmdType::RECALL:
                return do_recall(cmd.str1,
                    map_location(cmd.loc1.x - 1, cmd.loc1.y - 1));

            case WLCmdType::DISMISS:
                return do_dismiss(cmd.str1);

            case WLCmdType::CHOOSE:
                /* Deliver the choice to a pending request_choice() call
                   that originated from inside do_move/do_attack/etc. */
                if(ch_->deliver_choice(cmd.int1))
                    return WL_OK;
                return WL_ERR_INVALID;
            }
        } catch(const std::exception& e) {
            ERR_WL << "command error: " << e.what();
            return WL_ERR_GENERIC;
        }
        return WL_ERR_INVALID;
    }

    /* ── Action helpers (mirrors headless_playsingle_controller) ── */

    WL_Status do_move(WL_Loc wfrom, WL_Loc wto)
    {
        map_location from(wfrom.x - 1, wfrom.y - 1);
        map_location to  (wto.x   - 1, wto.y   - 1);

        const gamemap& m = get_map();
        if(!from.valid(m.w(), m.h()) || !to.valid(m.w(), m.h()))
            return WL_ERR_INVALID;

        auto it = get_units().find(from);
        if(it == get_units().end() || it->side() != current_side())
            return WL_ERR_INVALID;

        pathfind::shortest_path_calculator calc(
            *it, current_team(), get_teams(), m);
        pathfind::plain_route route = pathfind::a_star_search(
            from, to, 10000.0, calc, m.w(), m.h());

        if(route.steps.empty())
            return WL_ERR_NO_PATH;

        /* Post the move event with full path before the move executes.
         * The Lua moveto handler will skip its duplicate via tl_skip_next_moveto. */
        {
            WLEventInternal ev;
            ev.type = WL_EVENT_UNIT_MOVE;
            ev.s1   = it->id();
            ev.i1   = it->side();
            ev.loc1 = { from.wml_x(), from.wml_y() };
            ev.loc2 = { to.wml_x(),   to.wml_y() };
            int n = std::min((int)route.steps.size(), WL_MAX_PATH);
            ev.path.resize(n);
            for(int i = 0; i < n; ++i)
                ev.path[i] = { route.steps[i].wml_x(), route.steps[i].wml_y() };
            tl_channel->post_event(std::move(ev));
        }
        tl_skip_next_moveto = true;
        actions::move_unit_and_record(route.steps, &undo_stack());
        return WL_OK;
    }

    WL_Status do_attack(WL_Loc watt, WL_Loc wdef, int weapon)
    {
        map_location att(watt.x - 1, watt.y - 1);
        map_location def(wdef.x - 1, wdef.y - 1);

        const gamemap& m = get_map();
        if(!att.valid(m.w(), m.h()) || !def.valid(m.w(), m.h()))
            return WL_ERR_INVALID;

        auto a = get_units().find(att);
        if(a == get_units().end() || a->side() != current_side())
            return WL_ERR_INVALID;
        auto d = get_units().find(def);
        if(d == get_units().end())
            return WL_ERR_INVALID;
        if(weapon < 0) weapon = 0;

        synced_context::run_and_throw("attack",
            replay_helper::get_attack(
                att, def,
                weapon, -1,
                a->type_id(), d->type_id(),
                a->level(),  d->level(),
                resources::tod_manager->turn(),
                resources::tod_manager->get_time_of_day()));
        return WL_OK;
    }

    WL_Status do_recruit(const std::string& type_id, map_location hex)
    {
        /* These checks mirror menu_handler::can_recruit (menu_events.cpp).
         * TODO: decouple menu_handler from GUI so we can call it directly. */
        const unit_type* ut = unit_types.find(type_id);
        if(!ut) return WL_ERR_UNKNOWN;
        if(current_team().gold() < ut->cost()) return WL_ERR_NO_GOLD;

        map_location from;
        std::string err = actions::find_recruit_location(
            current_side(), hex, from, type_id);
        if(!err.empty()) return WL_ERR_NO_SPACE;

        synced_context::run_and_throw("recruit",
            replay_helper::get_recruit(type_id, hex, from));
        return WL_OK;
    }

    WL_Status do_recall(const std::string& unit_id, map_location hex)
    {
        /* These checks mirror menu_handler::recall (menu_events.cpp).
         * TODO: decouple menu_handler from GUI so we can call it directly. */
        const team& t = current_team();
        const recall_list_manager& rl = t.recall_list();
        unit_ptr u_ptr;
        for(const auto& u : rl) {
            if(u->id() == unit_id) { u_ptr = u; break; }
        }
        if(!u_ptr) return WL_ERR_UNKNOWN;
        int cost = u_ptr->recall_cost() >= 0 ? u_ptr->recall_cost() : t.recall_cost();
        if(t.gold() < cost) return WL_ERR_NO_GOLD;

        map_location from;
        std::string err = actions::find_recall_location(
            current_side(), hex, from, *u_ptr);
        if(!err.empty()) return WL_ERR_NO_SPACE;

        synced_context::run_in_synced_context_if_not_already("recall",
            replay_helper::get_recall(unit_id, hex, from));
        return WL_OK;
    }

    WL_Status do_dismiss(const std::string& unit_id)
    {
        const recall_list_manager& rl = current_team().recall_list();
        bool found = std::any_of(rl.begin(), rl.end(),
            [&](const unit_ptr& u){ return u->id() == unit_id; });
        if(!found) return WL_ERR_UNKNOWN;

        synced_context::run_and_throw("disband",
            replay_helper::get_disband(unit_id));
        return WL_OK;
    }
};

/* =========================================================================
 * Game thread entry point
 * ========================================================================= */
static void run_game_thread(WLEngineImpl* e)
{
    tl_channel = e->channel.get();
    WLChannel& ch = *e->channel;

    try {
        saved_game& state = *e->state;

        /* ── Deferred heavy setup (moved off the API/JS thread) ─────────────
         * Emit LOADING_CONFIG immediately so the JS side can show a status
         * message while we do the slow WML parse. */
        {
            WLEventInternal ev;
            ev.type = WL_EVENT_LOADING_CONFIG;
            ch.post_event(std::move(ev));
        }

        e->config_manager->load_game_config_for_game(
            state.classification(),
            e->needs_scenario_init ? e->pending_scenario_id
                                   : state.get_scenario_id());

        if(e->needs_scenario_init) {
            const std::string& sid = e->pending_scenario_id;
            config scenario_cfg;
            for(const config& sc :
                    e->config_manager->game_config().child_range("scenario")) {
                if(sc["id"].str() == sid) { scenario_cfg = sc; break; }
            }
            if(scenario_cfg.empty()) {
                e->last_error = std::string("scenario not found: ") + sid;
                WLEventInternal ev;
                ev.type    = WL_EVENT_SCENARIO_END;
                ev.outcome = WL_OUTCOME_QUIT;
                ch.post_event(std::move(ev));
                tl_channel = nullptr;
                ch.set_done();
                return;
            }
            state.set_scenario(scenario_cfg);
            state.set_defaults();
            e->needs_scenario_init = false;
        }
        /* ── End deferred setup ──────────────────────────────────────────── */

        state.expand_scenario();
        state.expand_random_scenario();
        state.expand_mp_events();
        state.expand_carryover();
        state.expand_mp_options();

        /* Emit scenario-start event. */
        {
            WLEventInternal ev;
            ev.type = WL_EVENT_SCENARIO_START;
            ch.post_event(std::move(ev));
        }

        const config& level = state.get_starting_point();
        WLController controller(level, state, e->channel);

        /* Register wl_post_* functions in the Lua kernel. */
        if(resources::lua_kernel) {
            lua_State* L = resources::lua_kernel->get_state();
            lua_getglobal(L, "wesnoth");

            static const luaL_Reg wl_fns[] = {
                { "wl_post_move",       lua_post_move       },
                { "wl_post_recruit",    lua_post_recruit    },
                { "wl_post_recall",     lua_post_recall     },
                { "wl_post_die",        lua_post_die        },
                { "wl_post_turn_end",   lua_post_turn_end   },
                { "wl_post_message",    lua_post_message    },
                { "wl_request_choice",  lua_request_choice  },
                { nullptr, nullptr }
            };
            for(const luaL_Reg* r = wl_fns; r->name; ++r) {
                lua_pushcfunction(L, r->func);
                lua_setfield(L, -2, r->name);
            }
            lua_pop(L, 1);

            config lua_cfg;
            lua_cfg["code"] = WL_EVENT_LUA;
            resources::lua_kernel->run_lua_tag(lua_cfg);
        }

        level_result::type result = controller.play_scenario(level);

        /* Extract next_scenario by converting state to start-save form,
         * mirroring what campaign_controller::play_game() does.
         * Without this, get_scenario_id() returns the *current* scenario ID
         * (from starting_point_["id"]), not the next one. */
        std::string next_scenario_id;
        if(result == level_result::type::victory
                && controller.is_regular_game_end()
                && controller.get_end_level_data().proceed_to_next_level) {
            state.set_snapshot(controller.to_config());
            state.convert_to_start_save();
            next_scenario_id = state.get_scenario_id();
        }

        /* Emit scenario-end event. */
        WLEventInternal ev;
        ev.type   = WL_EVENT_SCENARIO_END;
        ev.outcome = (result == level_result::type::victory)
                         ? WL_OUTCOME_VICTORY
                         : WL_OUTCOME_DEFEAT;
        ev.s1 = next_scenario_id; /* next_scenario (empty if no next level) */
        ch.post_event(std::move(ev));

    } catch(const savegame::load_game_exception&) {
        /* Load-game during play: treat as quit. */
        WLEventInternal ev;
        ev.type    = WL_EVENT_SCENARIO_END;
        ev.outcome = WL_OUTCOME_QUIT;
        ch.post_event(std::move(ev));
    } catch(...) {
        e->game_exception = std::current_exception();
        WLEventInternal ev;
        ev.type    = WL_EVENT_SCENARIO_END;
        ev.outcome = WL_OUTCOME_QUIT;
        ch.post_event(std::move(ev));
    }

    tl_channel = nullptr;
    ch.set_done();
}

/* =========================================================================
 * Event materialisation: WLEventInternal → heap-allocated WL_Event
 * ========================================================================= */
static WL_Event* materialize_event(const WLEventInternal& d,
                                    std::vector<char>& buf)
{
    /* First pass: measure string bytes needed. */
    WLArena arena(256);

    /* We build a temporary WL_Event on the stack, then memcpy into buf. */
    WL_Event ev{};
    ev.type = d.type;

    /* Helper lambda: store string in arena, return ptr (relative offset later). */
    /* We store everything in arena first, then compute final layout. */

    auto store = [&](const std::string& s) -> const char* {
        return arena.store(s);
    };
    auto store_c = [&](const char* s) -> const char* {
        return arena.store(s ? s : "");
    };

    switch(d.type) {
    case WL_EVENT_SCENARIO_START:
        break;

    case WL_EVENT_SCENARIO_END:
        ev.scenario_end.outcome       = d.outcome;
        ev.scenario_end.next_scenario = store(d.s1);
        break;

    case WL_EVENT_TURN_START:
        ev.turn_start.turn = d.i1;
        break;

    case WL_EVENT_SIDE_TURN_START:
        ev.side_turn_start.side = d.i1;
        ev.side_turn_start.turn = d.i2;
        break;

    case WL_EVENT_SIDE_TURN_END:
        ev.side_turn_end.side = d.i1;
        ev.side_turn_end.turn = d.i2;
        break;

    case WL_EVENT_WAITING_FOR_INPUT:
        ev.waiting_for_input.side = d.i1;
        ev.waiting_for_input.turn = d.i2;
        break;

    case WL_EVENT_UNIT_MOVE: {
        ev.unit_move.unit_id  = store(d.s1);
        ev.unit_move.side     = d.i1;
        ev.unit_move.from     = d.loc1;
        ev.unit_move.to       = d.loc2;
        int n = std::min((int)d.path.size(), WL_MAX_PATH);
        for(int i = 0; i < n; ++i) ev.unit_move.path[i] = d.path[i];
        ev.unit_move.path_len = n;
        break;
    }

    case WL_EVENT_UNIT_ATTACK:
        ev.unit_attack.attacker_id   = store(d.s1);
        ev.unit_attack.defender_id   = store(d.s2);
        ev.unit_attack.attacker_side = d.i1;
        ev.unit_attack.defender_side = d.i2;
        ev.unit_attack.attacker_loc  = d.loc1;
        ev.unit_attack.defender_loc  = d.loc2;
        ev.unit_attack.attacker_result = d.cr_att;
        ev.unit_attack.defender_result = d.cr_def;
        {
            int n = std::min((int)d.blows.size(), WL_MAX_BLOWS);
            for(int i = 0; i < n; ++i) ev.unit_attack.blows[i] = d.blows[i];
            ev.unit_attack.n_blows = n;
        }
        break;

    case WL_EVENT_UNIT_RECRUIT:
        ev.unit_recruit.unit_type_id = store(d.s1);
        ev.unit_recruit.unit_id      = store(d.s2);
        ev.unit_recruit.side         = d.i1;
        ev.unit_recruit.at           = d.loc1;
        break;

    case WL_EVENT_UNIT_RECALL:
        ev.unit_recall.unit_id      = store(d.s1);
        ev.unit_recall.unit_type_id = store(d.s2);
        ev.unit_recall.side         = d.i1;
        ev.unit_recall.at           = d.loc1;
        break;

    case WL_EVENT_UNIT_DISMISS:
        ev.unit_dismiss.unit_id      = store(d.s1);
        ev.unit_dismiss.unit_type_id = store(d.s2);
        ev.unit_dismiss.side         = d.i1;
        break;

    case WL_EVENT_UNIT_DIE:
        ev.unit_die.unit_id      = store(d.s1);
        ev.unit_die.unit_type_id = store(d.s2);
        ev.unit_die.side         = d.i1;
        ev.unit_die.loc          = d.loc1;
        ev.unit_die.killer_id    = store(d.s3);
        break;

    case WL_EVENT_UNIT_ADVANCE:
        ev.unit_advance.unit_id      = store(d.s1);
        ev.unit_advance.side         = d.i1;
        ev.unit_advance.loc          = d.loc1;
        ev.unit_advance.from_type_id = store(d.s2);
        ev.unit_advance.to_type_id   = store(d.s3);
        break;

    case WL_EVENT_UNIT_XP:
        ev.unit_xp.unit_id   = store(d.s1);
        ev.unit_xp.side      = d.i1;
        ev.unit_xp.loc       = d.loc1;
        ev.unit_xp.xp_gained = d.i2;
        ev.unit_xp.xp_total  = d.i3;
        ev.unit_xp.xp_needed = d.i4;
        break;

    case WL_EVENT_UNIT_HEAL:
        ev.unit_heal.unit_id = store(d.s1);
        ev.unit_heal.side    = d.i1;
        ev.unit_heal.loc     = d.loc1;
        ev.unit_heal.amount  = d.i2;
        break;

    case WL_EVENT_UNIT_STATUS:
        ev.unit_status.unit_id = store(d.s1);
        ev.unit_status.side    = d.i1;
        ev.unit_status.loc     = d.loc1;
        ev.unit_status.flags   = d.status_flags;
        break;

    case WL_EVENT_VILLAGE_CAPTURE:
        ev.village_capture.loc      = d.loc1;
        ev.village_capture.old_side = d.i1;
        ev.village_capture.new_side = d.i2;
        break;

    case WL_EVENT_MESSAGE:
        ev.message.speaker  = store(d.s1);
        ev.message.portrait = store(d.s2);
        ev.message.text     = store(d.s3);
        break;

    case WL_EVENT_STORY:
        ev.story.title      = store(d.s1);
        ev.story.text       = store(d.s2);
        ev.story.background = store(d.s3);
        break;

    case WL_EVENT_OBJECTIVES_UPDATE:
        ev.objectives_update.side = d.i1;
        ev.objectives_update.text = store(d.s1);
        break;

    case WL_EVENT_CHOICE_NEEDED: {
        ev.choice_needed.kind    = d.choice_kind;
        ev.choice_needed.prompt  = store(d.s1);
        ev.choice_needed.speaker  = store(d.s2);
        ev.choice_needed.portrait = store(d.s3);
        int n = std::min((int)d.options.size(), WL_MAX_OPTIONS);
        for(int i = 0; i < n; ++i)
            ev.choice_needed.options[i] = store(d.options[i]);
        ev.choice_needed.n_options = n;
        break;
    }

    case WL_EVENT_SOUND:
        ev.sound.path = store(d.s1);
        break;

    case WL_EVENT_MUSIC_CHANGE:
        ev.music_change.path  = store(d.s1);
        ev.music_change.title = store(d.s2);
        break;
    }

    /* Now build the final contiguous buffer:
     * [ WL_Event struct ] [ string arena bytes ]
     * Relocate const char* fields from arena into the final block. */

    size_t ev_sz  = sizeof(WL_Event);
    size_t str_sz = arena.buf.size();
    buf.resize(ev_sz + str_sz);

    /* Copy string data into the tail of buf. */
    if(str_sz) std::memcpy(buf.data() + ev_sz, arena.buf.data(), str_sz);

    /* Patch every const char* that points into the arena to point into buf. */
    auto relocate = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        if(off < 0 || (size_t)off >= arena.buf.size()) return nullptr;
        return reinterpret_cast<const char*>(buf.data() + ev_sz + off);
    };

    /* Relocate pointers in ev before copying into buf. */
    switch(d.type) {
    case WL_EVENT_SCENARIO_END:
        ev.scenario_end.next_scenario = relocate(ev.scenario_end.next_scenario);
        break;
    case WL_EVENT_UNIT_MOVE:
        ev.unit_move.unit_id = relocate(ev.unit_move.unit_id);
        break;
    case WL_EVENT_UNIT_ATTACK:
        ev.unit_attack.attacker_id = relocate(ev.unit_attack.attacker_id);
        ev.unit_attack.defender_id = relocate(ev.unit_attack.defender_id);
        break;
    case WL_EVENT_UNIT_RECRUIT:
        ev.unit_recruit.unit_type_id = relocate(ev.unit_recruit.unit_type_id);
        ev.unit_recruit.unit_id      = relocate(ev.unit_recruit.unit_id);
        break;
    case WL_EVENT_UNIT_RECALL:
        ev.unit_recall.unit_id      = relocate(ev.unit_recall.unit_id);
        ev.unit_recall.unit_type_id = relocate(ev.unit_recall.unit_type_id);
        break;
    case WL_EVENT_UNIT_DISMISS:
        ev.unit_dismiss.unit_id      = relocate(ev.unit_dismiss.unit_id);
        ev.unit_dismiss.unit_type_id = relocate(ev.unit_dismiss.unit_type_id);
        break;
    case WL_EVENT_UNIT_DIE:
        ev.unit_die.unit_id      = relocate(ev.unit_die.unit_id);
        ev.unit_die.unit_type_id = relocate(ev.unit_die.unit_type_id);
        ev.unit_die.killer_id    = relocate(ev.unit_die.killer_id);
        break;
    case WL_EVENT_UNIT_ADVANCE:
        ev.unit_advance.unit_id      = relocate(ev.unit_advance.unit_id);
        ev.unit_advance.from_type_id = relocate(ev.unit_advance.from_type_id);
        ev.unit_advance.to_type_id   = relocate(ev.unit_advance.to_type_id);
        break;
    case WL_EVENT_UNIT_XP:
        ev.unit_xp.unit_id = relocate(ev.unit_xp.unit_id);
        break;
    case WL_EVENT_UNIT_HEAL:
        ev.unit_heal.unit_id = relocate(ev.unit_heal.unit_id);
        break;
    case WL_EVENT_UNIT_STATUS:
        ev.unit_status.unit_id = relocate(ev.unit_status.unit_id);
        break;
    case WL_EVENT_MESSAGE:
        ev.message.speaker  = relocate(ev.message.speaker);
        ev.message.portrait = relocate(ev.message.portrait);
        ev.message.text     = relocate(ev.message.text);
        break;
    case WL_EVENT_STORY:
        ev.story.title      = relocate(ev.story.title);
        ev.story.text       = relocate(ev.story.text);
        ev.story.background = relocate(ev.story.background);
        break;
    case WL_EVENT_OBJECTIVES_UPDATE:
        ev.objectives_update.text = relocate(ev.objectives_update.text);
        break;
    case WL_EVENT_CHOICE_NEEDED: {
        ev.choice_needed.prompt   = relocate(ev.choice_needed.prompt);
        ev.choice_needed.speaker  = relocate(ev.choice_needed.speaker);
        ev.choice_needed.portrait = relocate(ev.choice_needed.portrait);
        for(int i = 0; i < ev.choice_needed.n_options; ++i)
            ev.choice_needed.options[i] = relocate(ev.choice_needed.options[i]);
        break;
    }
    case WL_EVENT_SOUND:
        ev.sound.path = relocate(ev.sound.path);
        break;
    case WL_EVENT_MUSIC_CHANGE:
        ev.music_change.path  = relocate(ev.music_change.path);
        ev.music_change.title = relocate(ev.music_change.title);
        break;
    default:
        break;
    }

    std::memcpy(buf.data(), &ev, ev_sz);
    return reinterpret_cast<WL_Event*>(buf.data());
}

/* =========================================================================
 * Snapshot helpers
 * ========================================================================= */

static WL_DamageType damage_type_from_string(const std::string& s)
{
    if(s == "blade")  return WL_DMG_BLADE;
    if(s == "pierce") return WL_DMG_PIERCE;
    if(s == "impact") return WL_DMG_IMPACT;
    if(s == "fire")   return WL_DMG_FIRE;
    if(s == "cold")   return WL_DMG_COLD;
    if(s == "arcane") return WL_DMG_ARCANE;
    return WL_DMG_BLADE;
}

static WL_AttackSpecials specials_from_attack(const attack_type& atk)
{
    int sp = 0;
    if(atk.has_special_or_ability("magical"))     sp |= WL_ATKSPC_MAGICAL;
    if(atk.has_special_or_ability("marksman"))    sp |= WL_ATKSPC_MARKSMAN;
    if(atk.has_special_or_ability("poison"))      sp |= WL_ATKSPC_POISON;
    if(atk.has_special_or_ability("slow"))        sp |= WL_ATKSPC_SLOW;
    if(atk.has_special_or_ability("drain"))       sp |= WL_ATKSPC_DRAIN;
    if(atk.has_special_or_ability("petrifies"))   sp |= WL_ATKSPC_PETRIFY;
    if(atk.has_special_or_ability("plague"))      sp |= WL_ATKSPC_PLAGUE;
    if(atk.has_special_or_ability("backstab"))    sp |= WL_ATKSPC_BACKSTAB;
    if(atk.has_special_or_ability("charge"))      sp |= WL_ATKSPC_CHARGE;
    if(atk.has_special_or_ability("firststrike")) sp |= WL_ATKSPC_FIRSTSTRIKE;
    if(atk.has_special_or_ability("swarm"))       sp |= WL_ATKSPC_SWARM;
    if(atk.has_special_or_ability("berserk"))     sp |= WL_ATKSPC_BERSERK;
    return static_cast<WL_AttackSpecials>(sp);
}

static void fill_wl_attack(WL_Attack& out, const attack_type& atk,
                            WLArena& arena)
{
    out.id            = arena.store(atk.id());
    out.name          = arena.store(atk.name());
    out.damage_type   = damage_type_from_string(atk.type());
    out.icon          = arena.store(atk.icon());
    out.damage        = atk.damage();
    out.num_attacks   = atk.num_attacks();
    out.range         = (atk.range() == "ranged") ? 1 : 0;
    out.specials      = specials_from_attack(atk);
    out.specials_desc = arena.store(""); /* TODO: iterate specials for desc */
}

/* Fill resistance array from a unit instance. */
static void fill_resistance(int (&res)[WL_DMG_COUNT], const unit& u)
{
    /* Resistance is 100 - resist% where resist% = movetype value */
    /* Positive = resists more, use the unit's actual computed values. */
    res[WL_DMG_BLADE]  = u.resistance_against("blade",  false, map_location());
    res[WL_DMG_PIERCE] = u.resistance_against("pierce", false, map_location());
    res[WL_DMG_IMPACT] = u.resistance_against("impact", false, map_location());
    res[WL_DMG_FIRE]   = u.resistance_against("fire",   false, map_location());
    res[WL_DMG_COLD]   = u.resistance_against("cold",   false, map_location());
    res[WL_DMG_ARCANE] = u.resistance_against("arcane", false, map_location());
}

/* Fill resistance array from a unit_type. */
static void fill_resistance_type(int (&res)[WL_DMG_COUNT],
                                  const unit_type& ut)
{
    res[WL_DMG_BLADE]  = 100 - ut.movement_type().resistance_against("blade");
    res[WL_DMG_PIERCE] = 100 - ut.movement_type().resistance_against("pierce");
    res[WL_DMG_IMPACT] = 100 - ut.movement_type().resistance_against("impact");
    res[WL_DMG_FIRE]   = 100 - ut.movement_type().resistance_against("fire");
    res[WL_DMG_COLD]   = 100 - ut.movement_type().resistance_against("cold");
    res[WL_DMG_ARCANE] = 100 - ut.movement_type().resistance_against("arcane");
}

static WL_UnitStatusFlags unit_status_flags(const unit& u)
{
    int f = WL_STATUS_NONE;
    if(u.get_state("poisoned"))  f |= WL_STATUS_POISONED;
    if(u.get_state("slowed"))    f |= WL_STATUS_SLOWED;
    if(u.get_state("petrified")) f |= WL_STATUS_PETRIFIED;
    if(u.invisible(u.get_location(), false)) f |= WL_STATUS_INVISIBLE;
    return static_cast<WL_UnitStatusFlags>(f);
}

static WL_UnitCapability unit_capability(const unit& u)
{
    int c = WL_UNIT_DONE;
    if(u.movement_left() > 0)  c |= WL_UNIT_CAN_MOVE;
    if(!u.attacks_left() == 0) c |= WL_UNIT_CAN_ATTACK;
    return static_cast<WL_UnitCapability>(c);
}

/* Build a WL_Unit from a unit instance + arena. */
static void fill_wl_unit(WL_Unit& out, const unit& u, WLArena& arena)
{
    out.id        = arena.store(u.id());
    out.type_id   = arena.store(u.type_id());
    out.name      = arena.store(u.name().str());
    out.portrait  = arena.store(resolve_img(u.big_profile()));
    out.sprite    = arena.store(resolve_img(u.absolute_image()));
    out.side      = u.side();
    out.loc       = { u.get_location().wml_x(), u.get_location().wml_y() };

    out.hp        = u.hitpoints();
    out.max_hp    = u.max_hitpoints();
    out.xp        = u.experience();
    out.max_xp    = u.max_experience();
    out.level     = u.level();
    out.moves     = u.movement_left();
    out.max_moves = u.total_movement();

    out.alignment = static_cast<WL_Alignment>(u.alignment());
    out.status    = unit_status_flags(u);
    out.capability = unit_capability(u);

    fill_resistance(out.resistance, u);

    /* Attacks */
    int na = 0;
    for(const auto& atk : u.attacks()) {
        if(na >= 8) break;
        fill_wl_attack(out.attacks[na++], atk, arena);
    }
    out.n_attacks = na;

    /* Traits */
    int nt = 0;
    for(const auto& tr : u.get_traits_list()) {
        if(nt >= 8) break;
        out.traits[nt++] = arena.store(tr);
    }
    out.n_traits = nt;

    /* Abilities */
    int nab = 0;
    for(const auto& ab : u.get_ability_id_list()) {
        if(nab >= 8) break;
        out.abilities[nab++] = arena.store(ab);
    }
    out.n_abilities = nab;

    /* Advances-to */
    int nav = 0;
    for(const auto& adv : u.advances_to()) {
        if(nav >= 8) break;
        out.advances_to[nav++] = arena.store(adv);
    }
    out.n_advances = nav;

    /* Upkeep */
    int upk = u.upkeep();
    out.upkeep    = std::max(0, upk);
    out.canrecruit = u.can_recruit() ? 1 : 0;
}

static WL_TerrainCategory terrain_category(const terrain_type& tt)
{
    const std::string& id = tt.id();
    if(id.empty()) return WL_TERRAIN_OTHER;
    char c = id[0];
    switch(c) {
    case 'G': case 'R': case 'D': case 'S':
        return WL_TERRAIN_FLAT;
    case 'F':
        return WL_TERRAIN_FOREST;
    case 'H':
        return WL_TERRAIN_HILLS;
    case 'M':
        return WL_TERRAIN_MOUNTAINS;
    case 'W':
        return WL_TERRAIN_WATER_SHALLOW;
    case 'V':
        return WL_TERRAIN_VILLAGE;
    case 'C': case 'K':
        return WL_TERRAIN_CASTLE;
    case 'U':
        return WL_TERRAIN_UNWALKABLE;
    }
    return WL_TERRAIN_OTHER;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

WL_Engine* wl_init(const char* data_path, const char* userdata_path)
{
    if(!data_path) return nullptr;

    auto* handle = new WL_Engine{};
    handle->impl = new WLEngineImpl{};
    WLEngineImpl& e = *handle->impl;

    try {
        std::string root = filesystem::normalize_path(data_path, true, true);
        if(filesystem::file_exists(root + "/cores.cfg")) {
            auto sep = root.find_last_of('/');
            if(sep != std::string::npos) root = root.substr(0, sep);
        }
        game_config::path = root;
        /* Store the game data root for image URL resolution (e.g. "/game/data") */
        g_data_root = root + "/data";

        std::string udata = userdata_path ? userdata_path
                                          : "/tmp/wesnothlite_userdata";
        filesystem::set_user_data_dir(udata);

        // Mark this thread as the Wesnoth "main" thread so that
        // events::call_in_main_thread() runs tasks inline (no SDL event loop).
        events::set_main_thread();

        video::init(video::fake::no_window);

        e.cmdline_opts = std::make_unique<commandline_options>(
            std::vector<std::string>{"wesnothlite"});

        e.config_manager = std::make_unique<game_config_manager>(
            *e.cmdline_opts);
        e.config_manager->init_game_config(game_config_manager::NO_FORCE_RELOAD);

        e.initialized = true;
        return handle;
    } catch(const std::exception& ex) {
        e.last_error = ex.what();
        /* Return handle with last_error set so wl_last_error() works. */
        return handle;
    }
}

void wl_shutdown(WL_Engine* engine)
{
    if(!engine) return;
    WLEngineImpl& e = *engine->impl;

    /* Stop the game thread if running. */
    if(e.game_thread.joinable()) {
        if(e.channel) e.channel->set_done();
        e.game_thread.join();
    }

    e.state.reset();
    e.config_manager.reset();
    e.cmdline_opts.reset();
    e.initialized = false;

    delete engine->impl;
    delete engine;
}

const char* wl_last_error(WL_Engine* engine)
{
    if(!engine) return "null engine";
    return engine->impl->last_error.c_str();
}

WL_Status wl_set_locale(WL_Engine* engine,
                         const char* locale,
                         const char* translations_path)
{
    if(!engine || !engine->impl->initialized) return WL_ERR_NO_GAME;

    /* Resolve translations directory: use the supplied path or fall back to
     * <data_path>/../translations (the standard Wesnoth source tree layout). */
    std::string intl_dir;
    if(translations_path && *translations_path) {
        intl_dir = translations_path;
    } else {
        intl_dir = filesystem::normalize_path(
            game_config::path + "/../translations", true, false);
    }

    /* Register the two base Wesnoth textdomains.  Campaign-specific domains
     * (e.g. "wesnoth-tutorial") are registered automatically when the WML
     * [textdomain] tags are processed during game-config loading. */
    translation::bind_textdomain("wesnoth",     intl_dir.c_str(), "UTF-8");
    translation::bind_textdomain("wesnoth-lib", intl_dir.c_str(), "UTF-8");

    /* Apply the requested locale (empty string → keep system default). */
    if(locale && *locale)
        translation::set_language(locale, nullptr);

    return WL_OK;
}

/* =========================================================================
 * Campaign list
 * ========================================================================= */

WL_CampaignList* wl_list_campaigns(WL_Engine* engine)
{
    if(!engine || !engine->impl->initialized) return nullptr;
    WLEngineImpl& e = *engine->impl;

    /* Collect campaign configs. */
    std::vector<const config*> campaigns;
    for(const config& c :
            e.config_manager->game_config().child_range("campaign"))
        campaigns.push_back(&c);

    /* Compute arena size. */
    WLArena arena(campaigns.size() * 512);

    /* Pre-build data so we can measure before allocating final block. */
    struct CInfo {
        const char *id, *name, *desc, *image, *icon, *first;
        const char* diffs[8];
        int n_diffs;
    };
    std::vector<CInfo> infos;
    infos.reserve(campaigns.size());

    for(const config* c : campaigns) {
        CInfo ci{};
        ci.id    = arena.store((*c)["id"].str());
        ci.name  = arena.store((*c)["name"].str());
        ci.desc  = arena.store((*c)["description"].str());
        ci.image = arena.store((*c)["image"].str());
        ci.icon  = arena.store((*c)["icon"].str());
        ci.first = arena.store((*c)["first_scenario"].str());

        /* Difficulties: stored as comma-separated or individual keys. */
        std::string dstr = (*c)["difficulties"].str();
        if(dstr.empty()) {
            /* Try individual difficulty defines. */
            ci.diffs[0]  = arena.store("NORMAL");
            ci.n_diffs   = 1;
        } else {
            for(const std::string& d : utils::split(dstr)) {
                if(ci.n_diffs >= 8) break;
                ci.diffs[ci.n_diffs++] = arena.store(d);
            }
        }
        infos.push_back(ci);
    }

    /* Allocate final block:
     * [ WL_CampaignList ][ WL_CampaignInfo × n ][ string arena ] */
    int n = (int)infos.size();
    size_t list_sz = sizeof(WL_CampaignList);
    size_t items_sz = (size_t)n * sizeof(WL_CampaignInfo);
    size_t str_sz  = arena.buf.size();
    char* buf = (char*)std::malloc(list_sz + items_sz + str_sz);
    if(!buf) return nullptr;

    char* str_base = buf + list_sz + items_sz;
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto relocate = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        if(off < 0 || (size_t)off >= arena.buf.size()) return nullptr;
        return str_base + off;
    };

    WL_CampaignList* list = reinterpret_cast<WL_CampaignList*>(buf);
    WL_CampaignInfo* items = reinterpret_cast<WL_CampaignInfo*>(buf + list_sz);
    list->campaigns = items;
    list->count     = n;

    for(int i = 0; i < n; ++i) {
        const CInfo& ci = infos[i];
        WL_CampaignInfo& item = items[i];
        item.id             = relocate(ci.id);
        item.name           = relocate(ci.name);
        item.description    = relocate(ci.desc);
        item.image          = relocate(ci.image);
        item.icon           = relocate(ci.icon);
        item.first_scenario = relocate(ci.first);
        item.n_difficulties = ci.n_diffs;
        for(int j = 0; j < ci.n_diffs; ++j)
            item.difficulties[j] = relocate(ci.diffs[j]);
    }

    return list;
}

/* =========================================================================
 * Scenario setup helpers
 * ========================================================================= */

static WL_Status setup_scenario(WLEngineImpl& e,
                                 const char* campaign_id,
                                 const char* scenario_id,
                                 const char* difficulty)
{
    e.state = std::make_unique<saved_game>();
    saved_game& state = *e.state;

    state.classification().type = campaign_type::type::scenario;
    state.classification().campaign = campaign_id ? campaign_id : "";
    state.classification().difficulty = difficulty ? difficulty : "NORMAL";

    std::string first = scenario_id ? scenario_id : "";

    if(campaign_id && *campaign_id) {
        for(const config& c :
                e.config_manager->game_config().child_range("campaign")) {
            if(c["id"].str() == campaign_id) {
                if(first.empty()) first = c["first_scenario"].str();
                state.classification().campaign_define = c["define"].str();
                break;
            }
        }
        if(state.classification().campaign_define.empty()) {
            e.last_error = std::string("campaign not found: ") + campaign_id;
            return WL_ERR_UNKNOWN;
        }
    }

    /* The heavy work (load_game_config_for_game + scenario init) is deferred
     * to run_game_thread() so it doesn't block the API/JS thread. */
    e.pending_scenario_id = first;
    e.needs_scenario_init = true;
    return WL_OK;
}

static void launch_game_thread(WLEngineImpl& e)
{
    /* Stop any previous game thread. */
    if(e.game_thread.joinable()) {
        if(e.channel) e.channel->set_done();
        e.game_thread.join();
    }

    e.channel = std::make_shared<WLChannel>();
    e.game_exception = nullptr;
    e.current_event  = nullptr;
    e.event_buf.clear();

    e.game_thread = std::thread(run_game_thread, &e);
}

WL_Status wl_start_campaign(WL_Engine* engine,
                              const char* campaign_id,
                              const char* difficulty)
{
    if(!engine || !engine->impl->initialized) return WL_ERR_NO_GAME;
    WLEngineImpl& e = *engine->impl;

    WL_Status s = setup_scenario(e, campaign_id, nullptr, difficulty);
    if(s != WL_OK) return s;

    launch_game_thread(e);
    return WL_OK;
}

WL_Status wl_start_scenario(WL_Engine* engine,
                              const char* campaign_id,
                              const char* scenario_id,
                              const char* difficulty)
{
    if(!engine || !engine->impl->initialized) return WL_ERR_NO_GAME;
    WLEngineImpl& e = *engine->impl;

    WL_Status s = setup_scenario(e, campaign_id, scenario_id, difficulty);
    if(s != WL_OK) return s;

    launch_game_thread(e);
    return WL_OK;
}

WL_Status wl_load_save(WL_Engine* engine, const char* save_path)
{
    if(!engine || !engine->impl->initialized || !save_path)
        return WL_ERR_INVALID;
    WLEngineImpl& e = *engine->impl;

    try {
        e.state = std::make_unique<saved_game>();
        savegame::load_game_metadata load_data;
        load_data.manager = std::make_shared<savegame::save_index_class>(
            filesystem::directory_name(save_path));
        load_data.filename = filesystem::base_name(save_path);
        load_data.read_file();
        savegame::set_gamestate(*e.state, load_data);
        e.needs_scenario_init = false;  /* state fully set; game thread only needs config load */
        launch_game_thread(e);
        return WL_OK;
    } catch(const std::exception& ex) {
        e.last_error = ex.what();
        return WL_ERR_GENERIC;
    }
}

WL_Status wl_load_from_buffer(WL_Engine* engine,
                               const unsigned char* buf, size_t len)
{
    if(!engine || !buf || !len) return WL_ERR_INVALID;
    WLEngineImpl& e = *engine->impl;
    if(!e.initialized) return WL_ERR_NO_GAME;

    try {
        /* Write the buffer to a temp file, load it, then clean up. */
        std::string tmp = filesystem::get_saves_dir() + "/.wl_tmp_load.gz";
        {
            filesystem::scoped_ostream os = filesystem::ostream_file(tmp);
            os->write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(len));
        }

        e.state = std::make_unique<saved_game>();
        savegame::load_game_metadata load_data;
        load_data.manager = std::make_shared<savegame::save_index_class>(
            filesystem::directory_name(tmp));
        load_data.filename = filesystem::base_name(tmp);
        load_data.read_file();
        savegame::set_gamestate(*e.state, load_data);
        e.needs_scenario_init = false;  /* state fully set; game thread only needs config load */
        filesystem::delete_file(tmp);
        launch_game_thread(e);
        return WL_OK;
    } catch(const std::exception& ex) {
        e.last_error = ex.what();
        return WL_ERR_GENERIC;
    }
}

WL_Status wl_save(WL_Engine* engine, const char* save_path)
{
    if(!engine || !save_path) return WL_ERR_INVALID;
    if(!resources::controller) return WL_ERR_NO_GAME;

    try {
        savegame::ingame_savegame sg(*engine->impl->state,
                                     compression::format::gzip);
        sg.save_game_automatic(false, save_path);
        return WL_OK;
    } catch(const std::exception& ex) {
        engine->impl->last_error = ex.what();
        return WL_ERR_GENERIC;
    }
}

unsigned char* wl_save_to_buffer(WL_Engine* engine, size_t* out_size)
{
    if(!engine || !out_size) return nullptr;
    if(!resources::controller) {
        engine->impl->last_error = "wl_save_to_buffer: no game in progress";
        return nullptr;
    }

    try {
        /* Save to a temp file, read back the bytes, delete the file. */
        std::string tmp = filesystem::get_saves_dir() + "/.wl_tmp_save.gz";
        {
            savegame::ingame_savegame sg(*engine->impl->state,
                                         compression::format::gzip);
            sg.save_game_automatic(false, tmp);
        }

        /* Read the file into a malloc'd buffer (caller frees with wl_free). */
        std::ifstream f(tmp, std::ios::binary | std::ios::ate);
        if(!f.is_open()) {
            engine->impl->last_error = "wl_save_to_buffer: could not read temp file";
            return nullptr;
        }
        std::streamsize sz = f.tellg();
        f.seekg(0);

        unsigned char* result = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(sz)));
        if(!result) {
            engine->impl->last_error = "wl_save_to_buffer: out of memory";
            return nullptr;
        }
        f.read(reinterpret_cast<char*>(result), sz);
        *out_size = static_cast<std::size_t>(sz);

        f.close();
        filesystem::delete_file(tmp);
        return result;
    } catch(const std::exception& ex) {
        engine->impl->last_error = ex.what();
        return nullptr;
    }
}

/* =========================================================================
 * Game pump
 * ========================================================================= */

const WL_Event* wl_step(WL_Engine* engine)
{
    if(!engine) return nullptr;
    WLEngineImpl& e = *engine->impl;
    if(!e.channel) return nullptr;

    WLChannel& ch = *e.channel;

    /* Wait until an event is available or the game thread reaches wait/done. */
    std::unique_lock lock(ch.ev_mtx);
    ch.ev_cv.wait(lock, [&] {
        return !ch.events.empty() || ch.game_waiting || ch.game_done;
    });

    if(ch.events.empty()) {
        /* Game is waiting for input (or done). */
        return nullptr;
    }

    WLEventInternal data = std::move(ch.events.front());
    ch.events.pop_front();
    lock.unlock();

    /* If this was the WAITING event, mark the channel as waiting. */
    if(data.type == WL_EVENT_WAITING_FOR_INPUT) {
        /* The game thread already set game_waiting via set_waiting(). */
    }

    e.current_event = materialize_event(data, e.event_buf);
    return e.current_event;
}

/* =========================================================================
 * Player actions
 * ========================================================================= */

static WL_Status send_cmd(WL_Engine* engine, WLCommand cmd)
{
    if(!engine) return WL_ERR_INVALID;
    WLEngineImpl& e = *engine->impl;
    if(!e.channel) return WL_ERR_NO_GAME;

    /* Only valid when game thread is blocked waiting for input. */
    {
        std::lock_guard lock(e.channel->ev_mtx);
        if(!e.channel->game_waiting) return WL_ERR_NOT_TURN;
    }

    return e.channel->send_command(std::move(cmd));
}

WL_Status wl_move(WL_Engine* engine, WL_Loc from, WL_Loc to)
{
    WLCommand cmd;
    cmd.type = WLCmdType::MOVE;
    cmd.loc1 = from;
    cmd.loc2 = to;
    return send_cmd(engine, cmd);
}

WL_Status wl_attack(WL_Engine* engine, WL_Loc attacker, WL_Loc defender,
                     int weapon_index)
{
    WLCommand cmd;
    cmd.type = WLCmdType::ATTACK;
    cmd.loc1 = attacker;
    cmd.loc2 = defender;
    cmd.int1 = weapon_index;
    return send_cmd(engine, cmd);
}

WL_Status wl_recruit(WL_Engine* engine, const char* unit_type_id, WL_Loc at)
{
    if(!unit_type_id) return WL_ERR_INVALID;
    WLCommand cmd;
    cmd.type = WLCmdType::RECRUIT;
    cmd.str1 = unit_type_id;
    cmd.loc1 = at;
    return send_cmd(engine, cmd);
}

WL_Status wl_recall(WL_Engine* engine, const char* unit_id, WL_Loc at)
{
    if(!unit_id) return WL_ERR_INVALID;
    WLCommand cmd;
    cmd.type = WLCmdType::RECALL;
    cmd.str1 = unit_id;
    cmd.loc1 = at;
    return send_cmd(engine, cmd);
}

WL_Status wl_dismiss(WL_Engine* engine, const char* unit_id)
{
    if(!unit_id) return WL_ERR_INVALID;
    WLCommand cmd;
    cmd.type = WLCmdType::DISMISS;
    cmd.str1 = unit_id;
    return send_cmd(engine, cmd);
}

WL_Status wl_end_turn(WL_Engine* engine)
{
    WLCommand cmd;
    cmd.type = WLCmdType::END_TURN;
    /* end_turn unblocks the game thread; set_waiting(false) happens in
       play_human_turn() after the loop exits. */
    return send_cmd(engine, cmd);
}

WL_Status wl_choose(WL_Engine* engine, int option_index)
{
    if(!engine) return WL_ERR_INVALID;
    WLEngineImpl& e = *engine->impl;
    if(!e.channel) return WL_ERR_NO_GAME;

    /* First try the direct choice channel — works when the game thread is
       blocked in request_choice() anywhere (inside or outside play_human_turn). */
    if(e.channel->deliver_choice(option_index))
        return WL_OK;

    /* Fall back to the regular command channel — handles CHOOSE commands
       that arrive while play_human_turn() is waiting for input and a
       mid-action choice was triggered (e.g., unit advancement during MOVE). */
    WLCommand cmd;
    cmd.type = WLCmdType::CHOOSE;
    cmd.int1 = option_index;
    return send_cmd(engine, cmd);
}

WL_Status wl_undo(WL_Engine* engine)
{
    WLCommand cmd;
    cmd.type = WLCmdType::UNDO;
    return send_cmd(engine, cmd);
}

/* =========================================================================
 * State queries
 * ========================================================================= */

WL_GameInfo* wl_query_game(WL_Engine* engine)
{
    if(!engine || !resources::gameboard) return nullptr;
    WLEngineImpl& e = *engine->impl;

    WLArena arena(512);
    WL_GameInfo gi{};

    gi.phase        = WL_PHASE_PLAYING;
    gi.turn         = resources::tod_manager
                          ? resources::tod_manager->turn() : 0;
    gi.max_turns    = resources::tod_manager
                          ? resources::tod_manager->number_of_turns() : 0;
    gi.current_side = resources::controller
                          ? resources::controller->current_side() : 0;
    gi.n_sides      = (int)resources::gameboard->teams().size();
    gi.outcome      = WL_OUTCOME_NONE;

    if(resources::tod_manager) {
        const time_of_day& tod =
            resources::tod_manager->get_time_of_day();
        gi.tod.id           = arena.store(tod.id);
        gi.tod.name         = arena.store(tod.name.str());
        gi.tod.lawful_bonus = tod.lawful_bonus;
        gi.tod.image        = arena.store(tod.image);
        gi.tod.mask_image   = arena.store(tod.image_mask);
    }

    gi.scenario_id   = arena.store(e.state ? e.state->get_scenario_id() : "");
    gi.scenario_name = arena.store(
        resources::gamedata ? resources::gamedata->get_variable("scenario_name").str() : "");
    gi.campaign_id   = arena.store(
        e.state ? e.state->classification().campaign : "");
    gi.campaign_name = arena.store("");
    gi.difficulty    = arena.store(
        e.state ? e.state->classification().difficulty : "");

    /* Allocate final block. */
    size_t str_sz = arena.buf.size();
    char* buf = (char*)std::malloc(sizeof(WL_GameInfo) + str_sz);
    if(!buf) return nullptr;
    char* str_base = buf + sizeof(WL_GameInfo);
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        return (off >= 0 && (size_t)off < arena.buf.size()) ? str_base + off : nullptr;
    };

    gi.tod.id         = rel(gi.tod.id);
    gi.tod.name       = rel(gi.tod.name);
    gi.tod.image      = rel(gi.tod.image);
    gi.tod.mask_image = rel(gi.tod.mask_image);
    gi.scenario_id    = rel(gi.scenario_id);
    gi.scenario_name  = rel(gi.scenario_name);
    gi.campaign_id    = rel(gi.campaign_id);
    gi.campaign_name  = rel(gi.campaign_name);
    gi.difficulty     = rel(gi.difficulty);

    std::memcpy(buf, &gi, sizeof(WL_GameInfo));
    return reinterpret_cast<WL_GameInfo*>(buf);
}

WL_MapData* wl_query_map(WL_Engine* engine)
{
    if(!engine || !resources::gameboard) return nullptr;

    const gamemap& m = resources::gameboard->map();
    int W = m.w(), H = m.h();

    WLArena arena((size_t)(W * H) * 64);

    /* Pre-build terrain info. */
    std::vector<WL_Terrain> terrains((size_t)(W * H));
    for(int y = 0; y < H; ++y) {
        for(int x = 0; x < W; ++x) {
            map_location loc(x, y);
            WL_Terrain& t = terrains[(size_t)(y * W + x)];
            t.loc = { x + 1, y + 1 };

            auto tc = m.get_terrain(loc);
            const terrain_type& tt =
                resources::gameboard->map().get_terrain_info(tc);

            t.category     = terrain_category(tt);
            t.id           = arena.store(tt.id());
            t.name         = arena.store(tt.name().str());
            t.icon         = arena.store(resolve_img(tt.editor_image()));
            /* Expose overlay terrain icon (e.g. village building on grass) */
            {
                std::string overlay_img;
                const std::string& tid = tt.id();
                auto caret = tid.find('^');
                if(caret != std::string::npos) {
                    try {
                        auto ov_str = "^" + tid.substr(caret + 1);
                        auto ov_tc  = t_translation::read_terrain_code(ov_str);
                        if(ov_tc != t_translation::NONE_TERRAIN) {
                            const terrain_type& ot =
                                resources::gameboard->map().get_terrain_info(ov_tc);
                            overlay_img = resolve_img(ot.editor_image());
                        }
                    } catch(...) {}
                }
                t.overlay_icon = arena.store(overlay_img);
            }
            t.village_side = m.is_village(loc)
                                 ? resources::gameboard->village_owner(loc) + 1
                                 : 0;
            t.starting_side = 0;
            for(int s = 1; s <= (int)resources::gameboard->teams().size(); ++s) {
                if(m.starting_position(s) == loc) { t.starting_side = s; break; }
            }
        }
    }

    /* Allocate: flexible array struct + terrain array + strings. */
    size_t hdr_sz  = sizeof(WL_MapData);
    size_t arr_sz  = (size_t)(W * H) * sizeof(WL_Terrain);
    size_t str_sz  = arena.buf.size();
    char* buf = (char*)std::malloc(hdr_sz + arr_sz + str_sz);
    if(!buf) return nullptr;

    char* str_base = buf + hdr_sz + arr_sz;
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        return (off >= 0 && (size_t)off < str_sz) ? str_base + off : nullptr;
    };

    WL_MapData* out = reinterpret_cast<WL_MapData*>(buf);
    out->width  = W;
    out->height = H;

    WL_Terrain* hex_arr = reinterpret_cast<WL_Terrain*>(buf + hdr_sz);
    for(int i = 0; i < W * H; ++i) {
        hex_arr[i] = terrains[(size_t)i];
        hex_arr[i].id   = rel(hex_arr[i].id);
        hex_arr[i].name = rel(hex_arr[i].name);
        hex_arr[i].icon = rel(hex_arr[i].icon);
    }

    return out;
}

WL_VisibilityMap* wl_query_visibility(WL_Engine* engine, int side)
{
    if(!engine || !resources::gameboard) return nullptr;
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > (int)teams.size()) return nullptr;

    const gamemap& m = resources::gameboard->map();
    int W = m.w(), H = m.h();
    int n = W * H;

    size_t hdr_sz = sizeof(WL_VisibilityMap);
    size_t val_sz = (size_t)n * sizeof(WL_Visibility);
    char* buf = (char*)std::malloc(hdr_sz + val_sz);
    if(!buf) return nullptr;

    WL_VisibilityMap* out = reinterpret_cast<WL_VisibilityMap*>(buf);
    out->side   = side;
    out->width  = W;
    out->height = H;

    const team& t = teams[(size_t)(side - 1)];
    WL_Visibility* vals = reinterpret_cast<WL_Visibility*>(buf + hdr_sz);
    for(int y = 0; y < H; ++y) {
        for(int x = 0; x < W; ++x) {
            map_location loc(x, y);
            WL_Visibility v = WL_VIS_VISIBLE;
            if(t.shrouded(loc))     v = WL_VIS_SHROUDED;
            else if(t.fogged(loc))  v = WL_VIS_FOGGED;
            vals[y * W + x] = v;
        }
    }

    return out;
}

WL_UnitList* wl_query_units(WL_Engine* engine)
{
    if(!engine || !resources::gameboard) return nullptr;
    const unit_map& units = resources::gameboard->units();

    WLArena arena(units.size() * 256);
    std::vector<WL_Unit> tmp(units.size());
    int n = 0;
    for(const unit& u : units)
        fill_wl_unit(tmp[(size_t)n++], u, arena);

    size_t list_sz  = sizeof(WL_UnitList);
    size_t items_sz = (size_t)n * sizeof(WL_Unit);
    size_t str_sz   = arena.buf.size();
    char* buf = (char*)std::malloc(list_sz + items_sz + str_sz);
    if(!buf) return nullptr;

    char* str_base = buf + list_sz + items_sz;
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        return (off >= 0 && (size_t)off < str_sz) ? str_base + off : nullptr;
    };

    WL_UnitList* list  = reinterpret_cast<WL_UnitList*>(buf);
    WL_Unit*     items = reinterpret_cast<WL_Unit*>(buf + list_sz);
    list->units = items;
    list->count = n;

    for(int i = 0; i < n; ++i) {
        items[i] = tmp[(size_t)i];
        /* Relocate all const char* fields. */
        items[i].id       = rel(items[i].id);
        items[i].type_id  = rel(items[i].type_id);
        items[i].name     = rel(items[i].name);
        items[i].portrait = rel(items[i].portrait);
        items[i].sprite   = rel(items[i].sprite);
        for(int a = 0; a < items[i].n_attacks; ++a) {
            items[i].attacks[a].id           = rel(items[i].attacks[a].id);
            items[i].attacks[a].name         = rel(items[i].attacks[a].name);
            items[i].attacks[a].icon         = rel(items[i].attacks[a].icon);
            items[i].attacks[a].specials_desc = rel(items[i].attacks[a].specials_desc);
        }
        for(int t = 0; t < items[i].n_traits;    ++t) items[i].traits[t]    = rel(items[i].traits[t]);
        for(int a = 0; a < items[i].n_abilities;  ++a) items[i].abilities[a] = rel(items[i].abilities[a]);
        for(int v = 0; v < items[i].n_advances;   ++v) items[i].advances_to[v] = rel(items[i].advances_to[v]);
    }

    return list;
}

WL_Unit* wl_query_unit_at(WL_Engine* engine, WL_Loc loc)
{
    if(!engine || !resources::gameboard) return nullptr;
    map_location ml(loc.x - 1, loc.y - 1);
    auto it = resources::gameboard->units().find(ml);
    if(it == resources::gameboard->units().end()) return nullptr;

    WLArena arena(256);
    size_t sz = sizeof(WL_Unit) + arena.buf.capacity();

    /* Build first to measure arena. */
    WL_Unit tmp{};
    fill_wl_unit(tmp, *it, arena);

    size_t str_sz = arena.buf.size();
    char* buf = (char*)std::malloc(sizeof(WL_Unit) + str_sz);
    if(!buf) return nullptr;

    char* str_base = buf + sizeof(WL_Unit);
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        return (off >= 0 && (size_t)off < str_sz) ? str_base + off : nullptr;
    };

    tmp.id       = rel(tmp.id);
    tmp.type_id  = rel(tmp.type_id);
    tmp.name     = rel(tmp.name);
    tmp.portrait = rel(tmp.portrait);
    tmp.sprite   = rel(tmp.sprite);
    for(int a = 0; a < tmp.n_attacks; ++a) {
        tmp.attacks[a].id            = rel(tmp.attacks[a].id);
        tmp.attacks[a].name          = rel(tmp.attacks[a].name);
        tmp.attacks[a].icon          = rel(tmp.attacks[a].icon);
        tmp.attacks[a].specials_desc = rel(tmp.attacks[a].specials_desc);
    }
    for(int i = 0; i < tmp.n_traits;    ++i) tmp.traits[i]     = rel(tmp.traits[i]);
    for(int i = 0; i < tmp.n_abilities;  ++i) tmp.abilities[i]  = rel(tmp.abilities[i]);
    for(int i = 0; i < tmp.n_advances;   ++i) tmp.advances_to[i] = rel(tmp.advances_to[i]);

    std::memcpy(buf, &tmp, sizeof(WL_Unit));
    return reinterpret_cast<WL_Unit*>(buf);
}

WL_UnitType* wl_query_unit_type(WL_Engine* engine, const char* type_id)
{
    if(!engine || !type_id) return nullptr;

    const unit_type* ut = unit_types.find(type_id);
    if(!ut) return nullptr;

    WLArena arena(512);
    WL_UnitType tmp{};

    tmp.type_id    = arena.store(ut->id());
    tmp.name       = arena.store(ut->type_name().str());
    tmp.description= arena.store(ut->unit_description().str());
    tmp.portrait   = arena.store(ut->big_profile());
    tmp.sprite     = arena.store(ut->image());
    tmp.race       = arena.store(ut->race_id());
    tmp.max_hp     = ut->hitpoints();
    tmp.max_moves  = ut->movement();
    tmp.max_xp     = ut->experience_needed();
    tmp.level      = ut->level();
    tmp.alignment  = static_cast<WL_Alignment>(ut->alignment());
    tmp.cost       = ut->cost();
    tmp.recall_cost= ut->recall_cost();

    fill_resistance_type(tmp.resistance, *ut);

    int na = 0;
    for(const attack_type& atk : ut->attacks()) {
        if(na >= 8) break;
        fill_wl_attack(tmp.attacks[na++], atk, arena);
    }
    tmp.n_attacks = na;

    int nab = 0;
    for(const auto& ab : ut->abilities_cfg().all_children_range()) {
        if(nab >= 8) break;
        tmp.abilities[nab++] = arena.store(ab.cfg["id"].str());
    }
    tmp.n_abilities = nab;

    int nav = 0;
    for(const std::string& adv : ut->advances_to()) {
        if(nav >= 8) break;
        tmp.advances_to[nav++] = arena.store(adv);
    }
    tmp.n_advances = nav;

    /* Build final block. */
    size_t str_sz = arena.buf.size();
    char* buf = (char*)std::malloc(sizeof(WL_UnitType) + str_sz);
    if(!buf) return nullptr;

    char* str_base = buf + sizeof(WL_UnitType);
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        return (off >= 0 && (size_t)off < str_sz) ? str_base + off : nullptr;
    };

    tmp.type_id     = rel(tmp.type_id);
    tmp.name        = rel(tmp.name);
    tmp.description = rel(tmp.description);
    tmp.portrait    = rel(tmp.portrait);
    tmp.sprite      = rel(tmp.sprite);
    tmp.race        = rel(tmp.race);
    for(int i = 0; i < tmp.n_attacks; ++i) {
        tmp.attacks[i].id            = rel(tmp.attacks[i].id);
        tmp.attacks[i].name          = rel(tmp.attacks[i].name);
        tmp.attacks[i].icon          = rel(tmp.attacks[i].icon);
        tmp.attacks[i].specials_desc = rel(tmp.attacks[i].specials_desc);
    }
    for(int i = 0; i < tmp.n_abilities; ++i) tmp.abilities[i]  = rel(tmp.abilities[i]);
    for(int i = 0; i < tmp.n_advances;  ++i) tmp.advances_to[i] = rel(tmp.advances_to[i]);

    std::memcpy(buf, &tmp, sizeof(WL_UnitType));
    return reinterpret_cast<WL_UnitType*>(buf);
}

WL_UnitList* wl_query_recall_list(WL_Engine* engine, int side)
{
    if(!engine || !resources::gameboard) return nullptr;
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > (int)teams.size()) return nullptr;

    const recall_list_manager& rl = teams[(size_t)(side - 1)].recall_list();

    WLArena arena(rl.size() * 128);
    std::vector<WL_Unit> tmp(rl.size());
    int n = 0;
    for(const unit_ptr& u : rl)
        fill_wl_unit(tmp[(size_t)n++], *u, arena);

    size_t list_sz  = sizeof(WL_UnitList);
    size_t items_sz = (size_t)n * sizeof(WL_Unit);
    size_t str_sz   = arena.buf.size();
    char* buf = (char*)std::malloc(list_sz + items_sz + str_sz);
    if(!buf) return nullptr;

    char* str_base = buf + list_sz + items_sz;
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        return (off >= 0 && (size_t)off < str_sz) ? str_base + off : nullptr;
    };

    WL_UnitList* list  = reinterpret_cast<WL_UnitList*>(buf);
    WL_Unit*     items = reinterpret_cast<WL_Unit*>(buf + list_sz);
    list->units = items;
    list->count = n;

    for(int i = 0; i < n; ++i) {
        items[i] = tmp[(size_t)i];
        items[i].id       = rel(items[i].id);
        items[i].type_id  = rel(items[i].type_id);
        items[i].name     = rel(items[i].name);
        items[i].portrait = rel(items[i].portrait);
        items[i].sprite   = rel(items[i].sprite);
        for(int a = 0; a < items[i].n_attacks; ++a) {
            items[i].attacks[a].id            = rel(items[i].attacks[a].id);
            items[i].attacks[a].name          = rel(items[i].attacks[a].name);
            items[i].attacks[a].icon          = rel(items[i].attacks[a].icon);
            items[i].attacks[a].specials_desc = rel(items[i].attacks[a].specials_desc);
        }
        for(int j = 0; j < items[i].n_traits;    ++j) items[i].traits[j]      = rel(items[i].traits[j]);
        for(int j = 0; j < items[i].n_abilities;  ++j) items[i].abilities[j]   = rel(items[i].abilities[j]);
        for(int j = 0; j < items[i].n_advances;   ++j) items[i].advances_to[j] = rel(items[i].advances_to[j]);
    }
    return list;
}

WL_RecruitList* wl_query_recruit_list(WL_Engine* engine, int side)
{
    if(!engine || !resources::gameboard) return nullptr;
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > (int)teams.size()) return nullptr;

    const team& t = teams[(size_t)(side - 1)];
    const std::set<std::string>& recruits = t.recruits();

    WLArena arena(recruits.size() * 32);
    size_t str_sz = 0;
    /* Pre-measure. */
    for(const auto& r : recruits) str_sz += r.size() + 1;
    arena.buf.reserve(str_sz);

    WL_RecruitList tmp{};
    int n = 0;
    for(const auto& r : recruits) {
        if(n >= 64) break;
        tmp.types[n++] = arena.store(r);
    }
    tmp.count = n;

    str_sz = arena.buf.size();
    char* buf = (char*)std::malloc(sizeof(WL_RecruitList) + str_sz);
    if(!buf) return nullptr;
    char* str_base = buf + sizeof(WL_RecruitList);
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        return (off >= 0 && (size_t)off < str_sz) ? str_base + off : nullptr;
    };

    for(int i = 0; i < n; ++i) tmp.types[i] = rel(tmp.types[i]);
    std::memcpy(buf, &tmp, sizeof(WL_RecruitList));
    return reinterpret_cast<WL_RecruitList*>(buf);
}

WL_Team* wl_query_team(WL_Engine* engine, int side)
{
    if(!engine || !resources::gameboard) return nullptr;
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > (int)teams.size()) return nullptr;

    const team& t = teams[(size_t)(side - 1)];
    WLArena arena(512);
    WL_Team tmp{};

    tmp.side         = side;
    tmp.name         = arena.store(t.user_team_name().str());
    tmp.faction      = arena.store(t.faction());
    tmp.color        = arena.store(t.color());
    tmp.controller   = static_cast<WL_SideController>(t.controller());
    tmp.gold         = t.gold();
    tmp.income       = t.total_income();
    tmp.base_income  = t.base_income();
    tmp.village_gold = t.village_gold();
    tmp.support      = t.support();
    tmp.recall_cost  = t.recall_cost();

    const auto& vils = t.villages();
    int nv = 0;
    for(const map_location& v : vils) {
        if(nv >= 256) break;
        tmp.villages[nv++] = { v.wml_x(), v.wml_y() };
    }
    tmp.n_villages = nv;

    tmp.objectives         = arena.store(t.objectives().str());
    tmp.objectives_changed = t.objectives_changed() ? 1 : 0;

    int ne = 0;
    for(int s = 1; s <= (int)teams.size(); ++s) {
        if(t.is_enemy(s)) tmp.enemy_sides[ne++] = s;
    }
    tmp.n_enemy_sides = ne;
    tmp.lost = t.lost() ? 1 : 0;

    size_t str_sz = arena.buf.size();
    char* buf = (char*)std::malloc(sizeof(WL_Team) + str_sz);
    if(!buf) return nullptr;
    char* str_base = buf + sizeof(WL_Team);
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        return (off >= 0 && (size_t)off < str_sz) ? str_base + off : nullptr;
    };

    tmp.name       = rel(tmp.name);
    tmp.faction    = rel(tmp.faction);
    tmp.color      = rel(tmp.color);
    tmp.objectives = rel(tmp.objectives);

    std::memcpy(buf, &tmp, sizeof(WL_Team));
    return reinterpret_cast<WL_Team*>(buf);
}

WL_ReachList* wl_query_reach(WL_Engine* engine, WL_Loc wloc)
{
    if(!engine || !resources::gameboard) return nullptr;
    map_location loc(wloc.x - 1, wloc.y - 1);

    auto it = resources::gameboard->units().find(loc);
    if(it == resources::gameboard->units().end()) return nullptr;

    const unit& u = *it;
    int side = u.side();
    if(side < 1 || side > (int)resources::gameboard->teams().size())
        return nullptr;

    const team& viewing_team = resources::gameboard->teams()[(size_t)(side - 1)];
    pathfind::paths paths_obj(u, false, true, viewing_team);

    int n = (int)paths_obj.destinations.size();

    size_t list_sz  = sizeof(WL_ReachList);
    size_t items_sz = (size_t)n * sizeof(WL_ReachHex);
    char* buf = (char*)std::malloc(list_sz + items_sz);
    if(!buf) return nullptr;

    WL_ReachList* list  = reinterpret_cast<WL_ReachList*>(buf);
    WL_ReachHex* items  = reinterpret_cast<WL_ReachHex*>(buf + list_sz);
    list->hexes = items;
    list->count = n;

    const gamemap& m = resources::gameboard->map();
    int i = 0;
    for(const pathfind::paths::step& s : paths_obj.destinations) {
        items[i].loc        = { s.curr.wml_x(), s.curr.wml_y() };
        items[i].moves_left = s.move_left;
        items[i].defense    = u.defense_modifier(m.get_terrain(s.curr));
        /* Check if any enemy is adjacent. */
        items[i].can_attack = 0;
        for(const map_location& adj : get_adjacent_tiles(s.curr)) {
            auto aj = resources::gameboard->units().find(adj);
            if(aj != resources::gameboard->units().end()
               && viewing_team.is_enemy(aj->side())) {
                items[i].can_attack = 1;
                break;
            }
        }
        ++i;
    }

    return list;
}

WL_AttackOptionList* wl_query_attack_options(WL_Engine* engine,
                                               WL_Loc watt, WL_Loc wdef)
{
    if(!engine || !resources::gameboard) return nullptr;

    map_location att(watt.x - 1, watt.y - 1);
    map_location def(wdef.x - 1, wdef.y - 1);

    const unit_map& units = resources::gameboard->units();
    auto ai = units.find(att);
    auto di = units.find(def);
    if(ai == units.end() || di == units.end()) return nullptr;

    const unit& attacker = *ai;
    const unit& defender = *di;

    /* Build one battle_context per attacker weapon. */
    std::vector<WL_AttackOption> opts;
    int default_opt = 0;
    double best_score = -1.0;

    int n_att_weapons = (int)attacker.attacks().size();
    for(int wi = 0; wi < n_att_weapons; ++wi) {
        try {
            battle_context bc(units, att, def, wi, -1, 0.0);

            const battle_context_unit_stats& as = bc.get_attacker_stats();
            const battle_context_unit_stats& ds = bc.get_defender_stats();

            WL_AttackOption opt{};
            opt.attacker_weapon_index = wi;
            opt.defender_weapon_index = ds.attack_num;

            opt.attacker.weapon_index  = wi;
            opt.attacker.damage        = as.damage;
            opt.attacker.num_blows     = (int)as.num_blows;
            opt.attacker.chance_to_hit = (int)as.chance_to_hit;
            opt.attacker.expected_damage =
                as.damage * (int)as.num_blows * (int)as.chance_to_hit / 100;
            opt.attacker.specials      = WL_ATKSPC_NONE; /* TODO */

            opt.defender.weapon_index  = ds.attack_num;
            opt.defender.damage        = ds.damage;
            opt.defender.num_blows     = (int)ds.num_blows;
            opt.defender.chance_to_hit = (int)ds.chance_to_hit;
            opt.defender.expected_damage =
                ds.damage * (int)ds.num_blows * (int)ds.chance_to_hit / 100;
            opt.defender.specials      = WL_ATKSPC_NONE;

            double score = opt.attacker.expected_damage
                           - opt.defender.expected_damage * 0.5;
            if(score > best_score) {
                best_score  = score;
                default_opt = (int)opts.size();
            }
            opts.push_back(opt);
        } catch(...) {
            /* Weapon not usable in this context; skip. */
        }
    }

    if(opts.empty()) return nullptr;

    int n = (int)opts.size();
    size_t list_sz  = sizeof(WL_AttackOptionList);
    size_t items_sz = (size_t)n * sizeof(WL_AttackOption);
    char* buf = (char*)std::malloc(list_sz + items_sz);
    if(!buf) return nullptr;

    WL_AttackOptionList* list =
        reinterpret_cast<WL_AttackOptionList*>(buf);
    WL_AttackOption* items =
        reinterpret_cast<WL_AttackOption*>(buf + list_sz);

    list->options        = items;
    list->count          = n;
    list->default_option = default_opt;

    std::memcpy(items, opts.data(), (size_t)n * sizeof(WL_AttackOption));
    return list;
}

/* =========================================================================
 * Memory management
 * ========================================================================= */

void wl_free(void* snapshot)
{
    std::free(snapshot);
}
