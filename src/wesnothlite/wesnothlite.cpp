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

#include <sstream>

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

#include "random.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <random>
#include <stdexcept>

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

WL_Engine* wl_init(const char* data_path, const char* userdata_path, const char* options)
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

        std::vector<std::string> argv{"wesnothlite"};
        if(options && *options) {
            std::istringstream iss(options);
            std::string tok;
            while(iss >> tok) argv.push_back(std::move(tok));
        }
        e.cmdline_opts = std::make_unique<commandline_options>(std::move(argv));

        e.config_manager = std::make_unique<game_config_manager>(
            *e.cmdline_opts);
        {
            auto _t0 = std::chrono::steady_clock::now();
            e.config_manager->init_game_config(game_config_manager::NO_FORCE_RELOAD);
            auto _t1 = std::chrono::steady_clock::now();
            LOG_WL << "[perf] init_game_config: "
                   << std::chrono::duration_cast<std::chrono::milliseconds>(_t1 - _t0).count()
                   << " ms";
        }

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

WL_Status wl_send(WL_Engine* engine, const WL_Command* cmd)
{
    if(!engine || !cmd) return WL_ERR_INVALID;
    WLEngineImpl& e = *engine->impl;
    if(!e.channel) return WL_ERR_NO_GAME;

    /* WL_CMD_CHOOSE can arrive while the game thread is blocked inside
     * request_choice() rather than wait_for_command(), so try the direct
     * choice channel first before checking game_waiting. */
    if(cmd->type == WL_CMD_CHOOSE) {
        if(e.channel->deliver_choice(cmd->choose.option))
            return WL_OK;
        /* Not in a choice; fall through and send via command channel. */
    }

    {
        std::lock_guard lock(e.channel->ev_mtx);
        if(!e.channel->game_waiting) return WL_ERR_NOT_TURN;
    }

    WLCommand internal;
    internal.type = cmd->type;
    switch(cmd->type) {
    case WL_CMD_MOVE:
        internal.loc1 = cmd->move.from;
        internal.loc2 = cmd->move.to;
        break;
    case WL_CMD_ATTACK:
        internal.loc1 = cmd->attack.att;
        internal.loc2 = cmd->attack.def;
        internal.int1 = cmd->attack.weapon;
        break;
    case WL_CMD_RECRUIT:
        if(!cmd->recruit.type_id) return WL_ERR_INVALID;
        internal.str1 = cmd->recruit.type_id;
        internal.loc1 = cmd->recruit.at;
        break;
    case WL_CMD_RECALL:
        if(!cmd->recall.unit_id) return WL_ERR_INVALID;
        internal.str1 = cmd->recall.unit_id;
        internal.loc1 = cmd->recall.at;
        break;
    case WL_CMD_DISMISS:
        if(!cmd->dismiss.unit_id) return WL_ERR_INVALID;
        internal.str1 = cmd->dismiss.unit_id;
        break;
    case WL_CMD_CHOOSE:
        internal.int1 = cmd->choose.option;
        break;
    case WL_CMD_END_TURN:
    case WL_CMD_UNDO:
        break;
    default:
        return WL_ERR_INVALID;
    }
    return e.channel->send_command(std::move(internal));
}

/* =========================================================================
 * Memory management
 * ========================================================================= */

void wl_free(void* snapshot)
{
    std::free(snapshot);
}

/* =========================================================================
 * Test helpers
 * ========================================================================= */
