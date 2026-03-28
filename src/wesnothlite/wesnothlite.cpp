/**
 * wesnothlite.cpp  —  WesnothLite C API: lifecycle, session, pump, actions
 *
 * This file is intentionally narrow.  All other implementation lives in:
 *   wl_snapshots.cpp  — image resolver, fill_wl_unit, terrain_category, attack helpers
 *   wl_events.cpp     — wl_materialize_event (WLEventInternal → WL_Event)
 *   wl_lua.cpp        — Lua C functions, hook snippet, wl_lua_setup(), wl_hook_*
 *   wl_controller.cpp — WLController class, wl_run_game_thread()
 *   wl_queries.cpp    — wl_list_campaigns, wl_query_* functions
 */

#include "wl_impl.hpp"

#include "commandline_options.hpp"
#include "events.hpp"
#include "filesystem.hpp"
#include "game_config.hpp"
#include "game_config_manager.hpp"
#include "game_end_exceptions.hpp"
#include "gettext.hpp"
#include "resources.hpp"
#include "save_index.hpp"
#include "saved_game.hpp"
#include "savegame.hpp"
#include "serialization/compression.hpp"
#include "video.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

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
        /* Images live under <root>/data/, not <root>/ */
        g_data_root = root + "/data";

        std::string udata = userdata_path ? userdata_path
                                          : "/tmp/wesnothlite_userdata";
        filesystem::set_user_data_dir(udata);

        /* Mark as Wesnoth "main" thread so events::call_in_main_thread()
         * runs tasks inline (no SDL event loop). */
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
        return handle;
    }
}

void wl_shutdown(WL_Engine* engine)
{
    if(!engine) return;
    WLEngineImpl& e = *engine->impl;

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

    std::string intl_dir;
    if(translations_path && *translations_path) {
        intl_dir = translations_path;
    } else {
        intl_dir = filesystem::normalize_path(
            game_config::path + "/../translations", true, false);
    }

    translation::bind_textdomain("wesnoth",     intl_dir.c_str(), "UTF-8");
    translation::bind_textdomain("wesnoth-lib", intl_dir.c_str(), "UTF-8");

    if(locale && *locale)
        translation::set_language(locale, nullptr);

    return WL_OK;
}

/* =========================================================================
 * Session helpers (private)
 * ========================================================================= */

static WL_Status setup_scenario(WLEngineImpl& e,
                                 const char* campaign_id,
                                 const char* scenario_id,
                                 const char* difficulty)
{
    e.state = std::make_unique<saved_game>();
    saved_game& state = *e.state;

    state.classification().type       = campaign_type::type::scenario;
    state.classification().campaign   = campaign_id ? campaign_id : "";
    state.classification().difficulty = difficulty  ? difficulty  : "NORMAL";

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

    /* Heavy work (load_game_config_for_game + scenario init) is deferred to
     * wl_run_game_thread() so it doesn't block the API/JS thread. */
    e.pending_scenario_id = first;
    e.needs_scenario_init = true;
    return WL_OK;
}

static void launch_game_thread(WLEngineImpl& e)
{
    if(e.game_thread.joinable()) {
        if(e.channel) e.channel->set_done();
        e.game_thread.join();
    }

    e.channel           = std::make_shared<WLChannel>();
    e.game_exception    = nullptr;
    e.current_event     = nullptr;
    e.event_buf.clear();

    e.game_thread = std::thread(wl_run_game_thread, &e);
}

/* =========================================================================
 * Session API
 * ========================================================================= */

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
        e.needs_scenario_init = false;
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
        std::string tmp = filesystem::get_saves_dir() + "/.wl_tmp_load.gz";
        {
            filesystem::scoped_ostream os = filesystem::ostream_file(tmp);
            os->write(reinterpret_cast<const char*>(buf),
                      static_cast<std::streamsize>(len));
        }

        e.state = std::make_unique<saved_game>();
        savegame::load_game_metadata load_data;
        load_data.manager = std::make_shared<savegame::save_index_class>(
            filesystem::directory_name(tmp));
        load_data.filename = filesystem::base_name(tmp);
        load_data.read_file();
        savegame::set_gamestate(*e.state, load_data);
        e.needs_scenario_init = false;
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
        std::string tmp = filesystem::get_saves_dir() + "/.wl_tmp_save.gz";
        {
            savegame::ingame_savegame sg(*engine->impl->state,
                                         compression::format::gzip);
            sg.save_game_automatic(false, tmp);
        }

        std::ifstream f(tmp, std::ios::binary | std::ios::ate);
        if(!f.is_open()) {
            engine->impl->last_error = "wl_save_to_buffer: could not read temp file";
            return nullptr;
        }
        std::streamsize sz = f.tellg();
        f.seekg(0);

        unsigned char* result = static_cast<unsigned char*>(
            std::malloc(static_cast<std::size_t>(sz)));
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

    std::unique_lock lock(ch.ev_mtx);
    ch.ev_cv.wait(lock, [&] {
        return !ch.events.empty() || ch.game_waiting || ch.game_done;
    });

    if(ch.events.empty())
        return nullptr;   /* Game is waiting for input (or done). */

    WLEventInternal data = std::move(ch.events.front());
    ch.events.pop_front();
    lock.unlock();

    e.current_event = wl_materialize_event(data, e.event_buf);
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
    return send_cmd(engine, cmd);
}

WL_Status wl_choose(WL_Engine* engine, int option_index)
{
    if(!engine) return WL_ERR_INVALID;
    WLEngineImpl& e = *engine->impl;
    if(!e.channel) return WL_ERR_NO_GAME;

    /* Try direct choice channel first (game thread blocked in request_choice). */
    if(e.channel->deliver_choice(option_index))
        return WL_OK;

    /* Fall back to command channel for mid-action choices during play_human_turn. */
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
 * Memory management
 * ========================================================================= */

void wl_free(void* snapshot)
{
    std::free(snapshot);
}
