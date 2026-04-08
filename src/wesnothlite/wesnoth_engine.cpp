/**
 * wesnoth_engine.cpp  —  WesnothEngine class implementation
 *
 * In the WASM build, this class is exposed to JavaScript via Embind.
 * In the native build, this file compiles but the methods with JsVal
 * parameters/return values are no-ops (the C API in wesnothlite.cpp is used
 * instead).
 */

#include "wesnoth_engine.hpp"
#include "wl_impl.hpp"

/* Lifecycle / session headers (mirror wesnothlite.cpp) */
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
#include "serialization/string_utils.hpp"
#include "video.hpp"
#include "random.hpp"

/* Query headers (mirror wl_queries.cpp) */
#include "actions/attack.hpp"
#include "game_board.hpp"
#include "game_data.hpp"
#include "game_state.hpp"
#include "play_controller.hpp"
#include "map/location.hpp"
#include "map/map.hpp"
#include "pathfind/pathfind.hpp"
#include "recall_list_manager.hpp"
#include "team.hpp"
#include "terrain/terrain.hpp"
#include "terrain/translation.hpp"
#include "tod_manager.hpp"
#include "units/map.hpp"
#include "units/types.hpp"
#include "units/unit.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#  include <emscripten/val.h>
   using emscripten::val;
#endif

/* =========================================================================
 * Constructor / Destructor
 * ========================================================================= */

WesnothEngine::WesnothEngine(const std::string& data_path,
                             const std::string& userdata_path,
                             const std::string& options)
    : impl_(new WLEngineImpl{})
{
    try {
        std::string root = filesystem::normalize_path(data_path, true, true);
        if(filesystem::file_exists(root + "/cores.cfg")) {
            auto sep = root.find_last_of('/');
            if(sep != std::string::npos) root = root.substr(0, sep);
        }
        game_config::path = root;
        g_data_root = root + "/data";

        std::string udata = userdata_path.empty()
                          ? "/tmp/wesnothlite_userdata" : userdata_path;
        filesystem::set_user_data_dir(udata);

        events::set_main_thread();
        video::init(video::fake::no_window);

        std::vector<std::string> argv{"wesnothlite"};
        if(!options.empty()) {
            std::istringstream iss(options);
            std::string tok;
            while(iss >> tok) argv.push_back(std::move(tok));
        }
        impl_->cmdline_opts = std::make_unique<commandline_options>(std::move(argv));
        impl_->config_manager = std::make_unique<game_config_manager>(
            *impl_->cmdline_opts);

        auto t0 = std::chrono::steady_clock::now();
        impl_->config_manager->init_game_config(game_config_manager::NO_FORCE_RELOAD);
        auto t1 = std::chrono::steady_clock::now();
        LOG_WL << "[perf] init_game_config: "
               << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
               << " ms";

        impl_->initialized = true;
    } catch(const std::exception& ex) {
        impl_->last_error = ex.what();
    }
}

WesnothEngine::~WesnothEngine()
{
    if(impl_) {
        if(impl_->game_thread.joinable()) {
            if(impl_->channel) impl_->channel->set_done();
            impl_->game_thread.join();
        }
        impl_->state.reset();
        impl_->config_manager.reset();
        impl_->cmdline_opts.reset();
        delete impl_;
        impl_ = nullptr;
    }
}

/* =========================================================================
 * Status
 * ========================================================================= */

bool WesnothEngine::initialized() const
{
    return impl_ && impl_->initialized;
}

std::string WesnothEngine::lastError() const
{
    if(!impl_) return "null impl";
    return impl_->last_error;
}

/* =========================================================================
 * Locale
 * ========================================================================= */

int WesnothEngine::setLocale(const std::string& locale,
                             const std::string& translations_path)
{
    if(!impl_ || !impl_->initialized) return WL_ERR_NO_GAME;

    std::string intl_dir = translations_path.empty()
        ? filesystem::normalize_path(game_config::path + "/../translations", true, false)
        : translations_path;

    translation::bind_textdomain("wesnoth",     intl_dir.c_str(), "UTF-8");
    translation::bind_textdomain("wesnoth-lib", intl_dir.c_str(), "UTF-8");

    if(!locale.empty())
        translation::set_language(locale, nullptr);

    return WL_OK;
}

/* =========================================================================
 * Session helpers (private)
 * ========================================================================= */

namespace {

WL_Status setup_scenario_impl(WLEngineImpl& e,
                               const std::string& campaign_id,
                               const std::string& scenario_id,
                               const std::string& difficulty)
{
    e.state = std::make_unique<saved_game>();
    saved_game& state = *e.state;

    state.classification().type       = campaign_type::type::scenario;
    state.classification().campaign   = campaign_id;
    state.classification().difficulty = difficulty.empty() ? "NORMAL" : difficulty;

    std::string first = scenario_id;

    if(!campaign_id.empty()) {
        for(const config& c :
                e.config_manager->game_config().child_range("campaign")) {
            if(c["id"].str() == campaign_id) {
                if(first.empty()) first = c["first_scenario"].str();
                state.classification().campaign_define = c["define"].str();
                break;
            }
        }
        if(state.classification().campaign_define.empty()) {
            e.last_error = "campaign not found: " + campaign_id;
            return WL_ERR_UNKNOWN;
        }
    }

    e.pending_scenario_id = first;
    e.needs_scenario_init = true;
    return WL_OK;
}

void launch_game_thread_impl(WLEngineImpl& e)
{
    if(e.game_thread.joinable()) {
        if(e.channel) {
            if(e.channel->game_waiting) {
                WLCommand qcmd;
                qcmd.type = WL_CMD_QUIT;
                e.channel->send_command(qcmd);
            }
            e.channel->set_done();
        }
        e.game_thread.join();
    }

    e.channel        = std::make_shared<WLChannel>();
    e.game_exception = nullptr;
    e.current_event  = nullptr;
    e.event_buf.clear();
    e.game_thread = std::thread(wl_run_game_thread, &e);
}

} // namespace

/* =========================================================================
 * Session API
 * ========================================================================= */

int WesnothEngine::startCampaign(const std::string& campaign_id,
                                 const std::string& difficulty)
{
    if(!impl_ || !impl_->initialized) return WL_ERR_NO_GAME;
    WL_Status s = setup_scenario_impl(*impl_, campaign_id, "", difficulty);
    if(s != WL_OK) return s;
    launch_game_thread_impl(*impl_);
    return WL_OK;
}

int WesnothEngine::startScenario(const std::string& campaign_id,
                                 const std::string& scenario_id,
                                 const std::string& difficulty)
{
    if(!impl_ || !impl_->initialized) return WL_ERR_NO_GAME;
    WL_Status s = setup_scenario_impl(*impl_, campaign_id, scenario_id, difficulty);
    if(s != WL_OK) return s;
    launch_game_thread_impl(*impl_);
    return WL_OK;
}

int WesnothEngine::loadFromBuffer(JsVal buf)
{
#ifndef __EMSCRIPTEN__
    (void)buf;
    return WL_ERR_INVALID;
#else
    if(!impl_ || !impl_->initialized) return WL_ERR_NO_GAME;

    try {
        /* Copy JS Uint8Array into a C++ vector. */
        size_t len = buf["length"].as<size_t>();
        std::vector<unsigned char> bytes(len);
        for(size_t i = 0; i < len; ++i)
            bytes[i] = buf[i].as<unsigned char>();

        /* Write to a temp file in the saves dir, then load. */
        std::string tmp = filesystem::get_saves_dir() + "/.wl_tmp_load.gz";
        {
            filesystem::scoped_ostream os = filesystem::ostream_file(tmp);
            os->write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(len));
        }

        impl_->state = std::make_unique<saved_game>();
        savegame::load_game_metadata load_data;
        load_data.manager = std::make_shared<savegame::save_index_class>(
            filesystem::directory_name(tmp));
        load_data.filename = filesystem::base_name(tmp);
        load_data.read_file();
        savegame::set_gamestate(*impl_->state, load_data);
        impl_->needs_scenario_init = false;
        filesystem::delete_file(tmp);
        launch_game_thread_impl(*impl_);
        return WL_OK;
    } catch(const std::exception& ex) {
        impl_->last_error = ex.what();
        return WL_ERR_GENERIC;
    }
#endif
}

JsVal WesnothEngine::saveToBuffer()
{
#ifndef __EMSCRIPTEN__
    return {};
#else
    if(!impl_ || !resources::controller) {
        impl_->last_error = "saveToBuffer: no game in progress";
        return val::null();
    }

    try {
        const std::string saves_dir = filesystem::get_saves_dir();
        const std::string tmp_path  = saves_dir + "/.wl_tmp_save.gz";

        WLCommand cmd;
        cmd.type = WL_CMD_SAVE;
        WL_Status status = impl_->channel->send_command(cmd);
        if(status != WL_OK) {
            impl_->last_error = "saveToBuffer: save command failed";
            return val::null();
        }

        std::ifstream f(tmp_path, std::ios::binary | std::ios::ate);
        if(!f.is_open()) {
            impl_->last_error = "saveToBuffer: could not read temp file";
            return val::null();
        }
        std::streamsize sz = f.tellg();
        f.seekg(0);
        std::vector<unsigned char> data(static_cast<size_t>(sz));
        f.read(reinterpret_cast<char*>(data.data()), sz);
        f.close();
        filesystem::delete_file(tmp_path);

        /* Return a Uint8Array copy. */
        auto view = emscripten::typed_memory_view(data.size(), data.data());
        return val::global("Uint8Array").new_(val(view));
    } catch(const std::exception& ex) {
        impl_->last_error = ex.what();
        return val::null();
    }
#endif
}

/* =========================================================================
 * listCampaigns
 * ========================================================================= */

JsVal WesnothEngine::listCampaigns()
{
#ifndef __EMSCRIPTEN__
    return {};
#else
    if(!impl_ || !impl_->initialized) return val::null();

    val arr = val::array();
    for(const config& c :
            impl_->config_manager->game_config().child_range("campaign")) {
        val obj = val::object();
        obj.set("id",            c["id"].str());
        obj.set("name",          c["name"].str());
        obj.set("description",   c["description"].str());
        obj.set("image",         c["image"].str());
        obj.set("icon",          c["icon"].str());
        obj.set("firstScenario", c["first_scenario"].str());

        val diffs = val::array();
        std::string dstr = c["difficulties"].str();
        if(dstr.empty()) {
            diffs.call<void>("push", std::string("NORMAL"));
        } else {
            for(const std::string& d : utils::split(dstr))
                diffs.call<void>("push", d);
        }
        obj.set("difficulties", diffs);

        arr.call<void>("push", obj);
    }
    return arr;
#endif
}

/* =========================================================================
 * step()  —  game pump
 * ========================================================================= */

#ifdef __EMSCRIPTEN__

namespace {

val make_coord(WL_Loc loc)
{
    val o = val::object();
    o.set("x", loc.x);
    o.set("y", loc.y);
    return o;
}

val make_path(const std::vector<WL_Loc>& path)
{
    val arr = val::array();
    for(const WL_Loc& l : path) arr.call<void>("push", make_coord(l));
    return arr;
}

} // anonymous namespace

JsVal WesnothEngine::buildEventVal(const WLEventInternal& ev)
{
    return std::visit([](const auto& e) -> val {
        using T = std::decay_t<decltype(e)>;
        val obj = val::object();

        if constexpr (std::is_same_v<T, WLEv::LoadingConfig>) {
            obj.set("type", std::string("loading_config"));

        } else if constexpr (std::is_same_v<T, WLEv::ScenarioStart>) {
            obj.set("type", std::string("scenario_start"));

        } else if constexpr (std::is_same_v<T, WLEv::ScenarioEnd>) {
            obj.set("type",         std::string("scenario_end"));
            obj.set("outcome",      static_cast<int>(e.outcome));
            obj.set("nextScenario", e.next_scenario);

        } else if constexpr (std::is_same_v<T, WLEv::TurnStart>) {
            obj.set("type", std::string("turn_start"));
            obj.set("turn", e.turn);

        } else if constexpr (std::is_same_v<T, WLEv::SideTurnStart>) {
            obj.set("type", std::string("side_turn_start"));
            obj.set("side", e.side);
            obj.set("turn", e.turn);

        } else if constexpr (std::is_same_v<T, WLEv::SideTurnEnd>) {
            obj.set("type", std::string("side_turn_end"));
            obj.set("side", e.side);
            obj.set("turn", e.turn);

        } else if constexpr (std::is_same_v<T, WLEv::WaitingForInput>) {
            obj.set("type", std::string("waiting_for_input"));
            obj.set("side", e.side);
            obj.set("turn", e.turn);

        } else if constexpr (std::is_same_v<T, WLEv::UnitMove>) {
            obj.set("type",     std::string("unit_move"));
            obj.set("unitId",   e.unit_id);
            obj.set("side",     e.side);
            obj.set("from",     make_coord(e.from));
            obj.set("to",       make_coord(e.to));
            obj.set("path",     make_path(e.path));

        } else if constexpr (std::is_same_v<T, WLEv::UnitAttack>) {
            val cr_att = val::object();
            cr_att.set("weaponIndex",  e.cr_att.weapon_index);
            cr_att.set("damagePerHit", e.cr_att.damage_per_hit);
            cr_att.set("numBlows",     e.cr_att.num_blows);
            cr_att.set("hits",         e.cr_att.hits);
            cr_att.set("chanceToHit",  e.cr_att.chance_to_hit);
            cr_att.set("hpStart",      e.cr_att.hp_start);
            cr_att.set("hpEnd",        e.cr_att.hp_end);
            val cr_def = val::object();
            cr_def.set("weaponIndex",  e.cr_def.weapon_index);
            cr_def.set("damagePerHit", e.cr_def.damage_per_hit);
            cr_def.set("numBlows",     e.cr_def.num_blows);
            cr_def.set("hits",         e.cr_def.hits);
            cr_def.set("chanceToHit",  e.cr_def.chance_to_hit);
            cr_def.set("hpStart",      e.cr_def.hp_start);
            cr_def.set("hpEnd",        e.cr_def.hp_end);
            val blows = val::array();
            for(const WL_Blow& b : e.blows) {
                val bv = val::object();
                bv.set("attackerStrikes", b.attacker_strikes);
                bv.set("hit",             b.hit);
                bv.set("damage",          b.damage);
                bv.set("attackerHpAfter", b.attacker_hp_after);
                bv.set("defenderHpAfter", b.defender_hp_after);
                blows.call<void>("push", bv);
            }
            obj.set("type",            std::string("unit_attack"));
            obj.set("attackerId",      e.attacker_id);
            obj.set("defenderId",      e.defender_id);
            obj.set("attackerSide",    e.attacker_side);
            obj.set("defenderSide",    e.defender_side);
            obj.set("attackerLoc",     make_coord(e.attacker_loc));
            obj.set("defenderLoc",     make_coord(e.defender_loc));
            obj.set("attackerResult",  cr_att);
            obj.set("defenderResult",  cr_def);
            obj.set("blows",           blows);

        } else if constexpr (std::is_same_v<T, WLEv::UnitSpawn>) {
            obj.set("type",       std::string("unit_spawn"));
            obj.set("unitTypeId", e.unit_type_id);
            obj.set("unitId",     e.unit_id);
            obj.set("side",       e.side);
            obj.set("at",         make_coord(e.at));

        } else if constexpr (std::is_same_v<T, WLEv::UnitDismiss>) {
            obj.set("type",       std::string("unit_dismiss"));
            obj.set("unitId",     e.unit_id);
            obj.set("unitTypeId", e.unit_type_id);
            obj.set("side",       e.side);

        } else if constexpr (std::is_same_v<T, WLEv::UnitDie>) {
            obj.set("type",       std::string("unit_die"));
            obj.set("unitId",     e.unit_id);
            obj.set("unitTypeId", e.unit_type_id);
            obj.set("killerId",   e.killer_id);
            obj.set("side",       e.side);
            obj.set("loc",        make_coord(e.loc));

        } else if constexpr (std::is_same_v<T, WLEv::UnitAdvance>) {
            obj.set("type",       std::string("unit_advance"));
            obj.set("unitId",     e.unit_id);
            obj.set("fromTypeId", e.from_type_id);
            obj.set("toTypeId",   e.to_type_id);
            obj.set("side",       e.side);
            obj.set("loc",        make_coord(e.loc));

        } else if constexpr (std::is_same_v<T, WLEv::UnitXP>) {
            obj.set("type",     std::string("unit_xp"));
            obj.set("unitId",   e.unit_id);
            obj.set("side",     e.side);
            obj.set("loc",      make_coord(e.loc));
            obj.set("xpGained", e.xp_gained);
            obj.set("xpTotal",  e.xp_total);
            obj.set("xpNeeded", e.xp_needed);

        } else if constexpr (std::is_same_v<T, WLEv::UnitHeal>) {
            obj.set("type",   std::string("unit_heal"));
            obj.set("unitId", e.unit_id);
            obj.set("side",   e.side);
            obj.set("loc",    make_coord(e.loc));
            obj.set("amount", e.amount);

        } else if constexpr (std::is_same_v<T, WLEv::UnitStatus>) {
            obj.set("type",   std::string("unit_status"));
            obj.set("unitId", e.unit_id);
            obj.set("side",   e.side);
            obj.set("loc",    make_coord(e.loc));
            obj.set("flags",  static_cast<int>(e.flags));

        } else if constexpr (std::is_same_v<T, WLEv::VillageCapture>) {
            obj.set("type",    std::string("village_capture"));
            obj.set("loc",     make_coord(e.loc));
            obj.set("oldSide", e.old_side);
            obj.set("newSide", e.new_side);

        } else if constexpr (std::is_same_v<T, WLEv::Message>) {
            obj.set("type",    std::string("message"));
            obj.set("speaker", e.speaker);
            obj.set("portrait",e.portrait);
            obj.set("text",    e.text);

        } else if constexpr (std::is_same_v<T, WLEv::Story>) {
            obj.set("type",       std::string("story"));
            obj.set("title",      e.title);
            obj.set("text",       e.text);
            obj.set("background", e.background);

        } else if constexpr (std::is_same_v<T, WLEv::ObjectivesUpdate>) {
            obj.set("type", std::string("objectives_update"));
            obj.set("side", e.side);
            obj.set("text", e.text);

        } else if constexpr (std::is_same_v<T, WLEv::ChoiceNeeded>) {
            val opts = val::array();
            for(const std::string& o : e.options) opts.call<void>("push", o);
            obj.set("type",    std::string("choice_needed"));
            obj.set("kind",    static_cast<int>(e.kind));
            obj.set("prompt",  e.prompt);
            obj.set("speaker", e.speaker);
            obj.set("options", opts);

        } else if constexpr (std::is_same_v<T, WLEv::Sound>) {
            obj.set("type", std::string("sound"));
            obj.set("path", e.path);

        } else if constexpr (std::is_same_v<T, WLEv::MusicChange>) {
            obj.set("type",  std::string("music_change"));
            obj.set("path",  e.path);
            obj.set("title", e.title);
        }

        return obj;
    }, ev);
}

#endif /* __EMSCRIPTEN__ */

JsVal WesnothEngine::step()
{
#ifndef __EMSCRIPTEN__
    return {};
#else
    if(!impl_ || !impl_->channel) return val::null();

    WLChannel& ch = *impl_->channel;

    std::unique_lock lock(ch.ev_mtx);
    ch.ev_cv.wait(lock, [&] {
        return !ch.events.empty() || ch.game_waiting || ch.awaiting_ack || ch.game_done;
    });

    if(ch.events.empty()) return val::null();

    WLEventInternal data = std::move(ch.events.front());
    ch.events.pop_front();
    lock.unlock();

    return buildEventVal(data);
#endif
}

/* =========================================================================
 * Commands
 * ========================================================================= */

int WesnothEngine::doSend(WLCommand cmd)
{
    if(!impl_ || !impl_->channel) return WL_ERR_NO_GAME;
    {
        std::lock_guard lock(impl_->channel->ev_mtx);
        if(!impl_->channel->game_waiting) return WL_ERR_NOT_TURN;
    }
    /* ev_mtx released before send_command() — the game thread calls
     * set_waiting(false) which also acquires ev_mtx; holding it across
     * a blocking send_command() would deadlock. */
    return static_cast<int>(impl_->channel->send_command(std::move(cmd)));
}

int WesnothEngine::sendMove(int fx, int fy, int tx, int ty)
{
    WLCommand cmd;
    cmd.type = WL_CMD_MOVE;
    cmd.loc1 = { fx, fy };
    cmd.loc2 = { tx, ty };
    return doSend(cmd);
}

int WesnothEngine::sendAttack(int ax, int ay, int dx, int dy, int weapon)
{
    WLCommand cmd;
    cmd.type = WL_CMD_ATTACK;
    cmd.loc1 = { ax, ay };
    cmd.loc2 = { dx, dy };
    cmd.int1 = weapon;
    return doSend(cmd);
}

int WesnothEngine::sendRecruit(const std::string& type_id, int x, int y)
{
    if(!impl_ || !impl_->channel) return WL_ERR_NO_GAME;
    WLCommand cmd;
    cmd.type = WL_CMD_RECRUIT;
    cmd.str1 = type_id.c_str();
    cmd.loc1 = { x, y };
    return static_cast<int>(impl_->channel->send_command(cmd));
}

int WesnothEngine::sendRecall(const std::string& unit_id, int x, int y)
{
    if(!impl_ || !impl_->channel) return WL_ERR_NO_GAME;
    WLCommand cmd;
    cmd.type = WL_CMD_RECALL;
    cmd.str1 = unit_id.c_str();
    cmd.loc1 = { x, y };
    return static_cast<int>(impl_->channel->send_command(cmd));
}

int WesnothEngine::sendDismiss(const std::string& unit_id)
{
    if(!impl_ || !impl_->channel) return WL_ERR_NO_GAME;
    WLCommand cmd;
    cmd.type = WL_CMD_DISMISS;
    cmd.str1 = unit_id.c_str();
    return static_cast<int>(impl_->channel->send_command(cmd));
}

int WesnothEngine::sendEndTurn()
{
    WLCommand cmd;
    cmd.type = WL_CMD_END_TURN;
    return doSend(cmd);
}

int WesnothEngine::sendUndo()
{
    WLCommand cmd;
    cmd.type = WL_CMD_UNDO;
    return doSend(cmd);
}

int WesnothEngine::sendChoose(int option)
{
    if(!impl_ || !impl_->channel) return WL_ERR_NO_GAME;
    if(impl_->channel->deliver_choice(option)) return WL_OK;
    /* Not in a choice; try as a regular command (covers await_ack). */
    WLCommand cmd;
    cmd.type = WL_CMD_CHOOSE;
    cmd.int1 = option;
    return doSend(cmd);
}

/* =========================================================================
 * Query methods
 * ========================================================================= */

#ifdef __EMSCRIPTEN__

namespace {

/* Build a val::object() for a single unit by calling fill_wl_unit and
 * then converting the WL_Unit struct to a val.  Reuses the existing helper
 * so attack specials, portrait paths, etc. are resolved consistently. */
val unit_to_val(const unit& u)
{
    WLArena arena(256);
    WL_Unit tmp{};
    fill_wl_unit(tmp, u, arena);

    auto s = [&](const char* p) -> std::string {
        return p ? std::string(p) : std::string{};
    };

    val obj = val::object();
    obj.set("id",       s(tmp.id));
    obj.set("typeId",   s(tmp.type_id));
    obj.set("name",     s(tmp.name));
    obj.set("portrait", s(tmp.portrait));
    obj.set("sprite",   s(tmp.sprite));
    obj.set("side",     tmp.side);
    obj.set("hp",       tmp.hp);
    obj.set("maxHp",    tmp.max_hp);
    obj.set("xp",       tmp.xp);
    obj.set("maxXp",    tmp.max_xp);
    obj.set("moves",    tmp.moves);
    obj.set("maxMoves", tmp.max_moves);
    obj.set("level",    tmp.level);
    obj.set("upkeep",   tmp.upkeep);
    obj.set("canRecruit",   tmp.canrecruit != 0);
    obj.set("canMove",      (tmp.capability & WL_UNIT_CAN_MOVE) != 0);
    obj.set("canAttack",    (tmp.capability & WL_UNIT_CAN_ATTACK) != 0);
    obj.set("status",       static_cast<int>(tmp.status));
    obj.set("alignment",    static_cast<int>(tmp.alignment));

    val loc = val::object();
    loc.set("x", tmp.loc.x);
    loc.set("y", tmp.loc.y);
    obj.set("loc", loc);

    val attacks = val::array();
    for(int i = 0; i < tmp.n_attacks; ++i) {
        const WL_Attack& a = tmp.attacks[i];
        val av = val::object();
        av.set("id",           s(a.id));
        av.set("name",         s(a.name));
        av.set("icon",         s(a.icon));
        av.set("damage",       a.damage);
        av.set("numAttacks",   a.num_attacks);
        av.set("range",        a.range);
        av.set("damageType",   static_cast<int>(a.damage_type));
        av.set("specials",     static_cast<int>(a.specials));
        av.set("specialsDesc", s(a.specials_desc));
        attacks.call<void>("push", av);
    }
    obj.set("attacks", attacks);

    val resistance = val::array();
    for(int i = 0; i < WL_DMG_COUNT; ++i)
        resistance.call<void>("push", tmp.resistance[i]);
    obj.set("resistance", resistance);

    obj.set("capability", static_cast<int>(tmp.capability));

    val traits = val::array();
    for(int i = 0; i < tmp.n_traits; ++i)
        traits.call<void>("push", s(tmp.traits[i]));
    obj.set("traits", traits);

    val abilities = val::array();
    for(int i = 0; i < tmp.n_abilities; ++i)
        abilities.call<void>("push", s(tmp.abilities[i]));
    obj.set("abilities", abilities);

    val advances_to = val::array();
    for(int i = 0; i < tmp.n_advances; ++i)
        advances_to.call<void>("push", s(tmp.advances_to[i]));
    obj.set("advancesTo", advances_to);

    return obj;
}

} // anonymous namespace

#endif /* __EMSCRIPTEN__ */

JsVal WesnothEngine::queryGame()
{
#ifndef __EMSCRIPTEN__
    return {};
#else
    if(!impl_) return val::null();
    WLEngineImpl& e = *impl_;

    val obj = val::object();
    obj.set("phase",       static_cast<int>(WL_PHASE_PLAYING));
    obj.set("turn",        resources::tod_manager ? resources::tod_manager->turn() : 0);
    obj.set("maxTurns",    resources::tod_manager ? resources::tod_manager->number_of_turns() : 0);
    obj.set("currentSide", resources::controller  ? resources::controller->current_side() : 0);
    obj.set("nSides",      resources::gameboard
                               ? static_cast<int>(resources::gameboard->teams().size()) : 0);
    obj.set("scenarioId",  e.state ? e.state->get_scenario_id() : std::string{});
    obj.set("scenarioName", resources::gamedata
                                ? resources::gamedata->get_variable("scenario_name").str()
                                : std::string{});
    obj.set("campaignId",  e.state ? e.state->classification().campaign : std::string{});
    obj.set("campaignName", std::string{});
    obj.set("difficulty",  e.state ? e.state->classification().difficulty : std::string{});
    obj.set("outcome",     static_cast<int>(WL_OUTCOME_NONE));

    val tv = val::object();
    if(resources::tod_manager) {
        const time_of_day& tod = resources::tod_manager->get_time_of_day();
        tv.set("id",          tod.id);
        tv.set("name",        tod.name.str());
        tv.set("lawfulBonus", tod.lawful_bonus);
        tv.set("image",       tod.image);
        tv.set("maskImage",   tod.image_mask);
    } else {
        tv.set("id",          std::string{});
        tv.set("name",        std::string{});
        tv.set("lawfulBonus", 0);
        tv.set("image",       std::string{});
        tv.set("maskImage",   std::string{});
    }
    obj.set("tod", tv);
    return obj;
#endif
}

JsVal WesnothEngine::queryMap()
{
#ifndef __EMSCRIPTEN__
    return {};
#else
    if(!impl_ || !resources::gameboard) return val::null();

    const gamemap& m = resources::gameboard->map();
    int W = m.w(), H = m.h();

    val hexes = val::array();
    for(int y = 0; y < H; ++y) {
        for(int x = 0; x < W; ++x) {
            map_location loc(x, y);
            auto tc = m.get_terrain(loc);
            const terrain_type& tt = m.get_terrain_info(tc);

            t_translation::terrain_code ov_tc(t_translation::NO_LAYER, tc.overlay);
            const terrain_type& ov_tt = m.get_terrain_info(ov_tc);
            const std::string ov_img = ov_tt.editor_image();

            val h = val::object();
            val hl = val::object();
            hl.set("x", x + 1);
            hl.set("y", y + 1);
            h.set("loc",         hl);
            h.set("category",    static_cast<int>(terrain_category(tt)));
            h.set("id",          tt.id());
            h.set("name",        tt.name().str());
            h.set("icon",        resolve_img(tt.editor_image()));
            h.set("overlayIcon", ov_img.empty() ? std::string{} : resolve_img(ov_img));
            h.set("villageSide", m.is_village(loc)
                                    ? resources::gameboard->village_owner(loc) + 1 : 0);
            int ss = 0;
            for(int s = 1; s <= static_cast<int>(resources::gameboard->teams().size()); ++s) {
                if(m.starting_position(s) == loc) { ss = s; break; }
            }
            h.set("startingSide", ss);
            h.set("isKeep",       tt.is_keep());
            hexes.call<void>("push", h);
        }
    }

    val obj = val::object();
    obj.set("width",  W);
    obj.set("height", H);
    obj.set("hexes",  hexes);
    return obj;
#endif
}

JsVal WesnothEngine::queryVisibility(int side)
{
#ifndef __EMSCRIPTEN__
    (void)side;
    return {};
#else
    if(!impl_ || !resources::gameboard) return val::null();
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > static_cast<int>(teams.size())) return val::null();

    const gamemap& m = resources::gameboard->map();
    int W = m.w(), H = m.h();
    const team& t = teams[static_cast<size_t>(side - 1)];

    val arr = val::array();
    for(int y = 0; y < H; ++y) {
        for(int x = 0; x < W; ++x) {
            map_location loc(x, y);
            int v = 0; /* visible */
            if(t.shrouded(loc))    v = 2;
            else if(t.fogged(loc)) v = 1;
            arr.call<void>("push", v);
        }
    }

    val obj = val::object();
    obj.set("side",   side);
    obj.set("width",  W);
    obj.set("height", H);
    obj.set("values", arr);
    return obj;
#endif
}

JsVal WesnothEngine::queryUnits()
{
#ifndef __EMSCRIPTEN__
    return {};
#else
    if(!impl_ || !resources::gameboard) return val::null();

    val arr = val::array();
    for(const unit& u : resources::gameboard->units())
        arr.call<void>("push", unit_to_val(u));
    return arr;
#endif
}

JsVal WesnothEngine::queryUnitAt(int x, int y)
{
#ifndef __EMSCRIPTEN__
    (void)x; (void)y;
    return {};
#else
    if(!impl_ || !resources::gameboard) return val::null();
    map_location ml(x - 1, y - 1);
    auto it = resources::gameboard->units().find(ml);
    if(it == resources::gameboard->units().end()) return val::null();
    return unit_to_val(*it);
#endif
}

JsVal WesnothEngine::queryUnitType(const std::string& type_id)
{
#ifndef __EMSCRIPTEN__
    (void)type_id;
    return {};
#else
    const unit_type* ut = unit_types.find(type_id);
    if(!ut) return val::null();

    WLArena arena(512);
    val obj = val::object();
    obj.set("typeId",      ut->id());
    obj.set("name",        ut->type_name().str());
    obj.set("description", ut->unit_description().str());
    obj.set("portrait",    ut->big_profile());
    obj.set("sprite",      ut->image());
    obj.set("race",        ut->race_id());
    obj.set("maxHp",       ut->hitpoints());
    obj.set("maxMoves",    ut->movement());
    obj.set("maxXp",       ut->experience_needed());
    obj.set("level",       ut->level());
    obj.set("alignment",   static_cast<int>(ut->alignment()));
    obj.set("cost",        ut->cost());
    obj.set("recallCost",  ut->recall_cost());

    val attacks = val::array();
    for(const attack_type& atk : ut->attacks()) {
        WL_Attack wa{};
        fill_wl_attack_pub(wa, atk, arena);
        val av = val::object();
        av.set("id",           wa.id ? std::string(wa.id) : std::string{});
        av.set("name",         wa.name ? std::string(wa.name) : std::string{});
        av.set("icon",         wa.icon ? std::string(wa.icon) : std::string{});
        av.set("damage",       wa.damage);
        av.set("numAttacks",   wa.num_attacks);
        av.set("range",        wa.range);
        av.set("damageType",   static_cast<int>(wa.damage_type));
        av.set("specials",     static_cast<int>(wa.specials));
        av.set("specialsDesc", wa.specials_desc ? std::string(wa.specials_desc) : std::string{});
        attacks.call<void>("push", av);
    }
    obj.set("attacks", attacks);

    val abilities = val::array();
    for(const auto& ab : ut->abilities_cfg().all_children_range())
        abilities.call<void>("push", ab.cfg["id"].str());
    obj.set("abilities", abilities);

    val advances = val::array();
    for(const std::string& adv : ut->advances_to())
        advances.call<void>("push", adv);
    obj.set("advancesTo", advances);

    return obj;
#endif
}

JsVal WesnothEngine::queryRecallList(int side)
{
#ifndef __EMSCRIPTEN__
    (void)side;
    return {};
#else
    if(!impl_ || !resources::gameboard) return val::null();
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > static_cast<int>(teams.size())) return val::null();

    val arr = val::array();
    for(const unit_ptr& u : teams[static_cast<size_t>(side - 1)].recall_list())
        arr.call<void>("push", unit_to_val(*u));
    return arr;
#endif
}

JsVal WesnothEngine::queryRecruitList(int side)
{
#ifndef __EMSCRIPTEN__
    (void)side;
    return {};
#else
    if(!impl_ || !resources::gameboard) return val::null();
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > static_cast<int>(teams.size())) return val::null();

    val arr = val::array();
    for(const std::string& r : teams[static_cast<size_t>(side - 1)].recruits())
        arr.call<void>("push", r);
    return arr;
#endif
}

JsVal WesnothEngine::queryTeam(int side)
{
#ifndef __EMSCRIPTEN__
    (void)side;
    return {};
#else
    if(!impl_ || !resources::gameboard) return val::null();
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > static_cast<int>(teams.size())) return val::null();

    const team& t = teams[static_cast<size_t>(side - 1)];
    val obj = val::object();
    obj.set("side",        side);
    obj.set("name",        t.user_team_name().str());
    obj.set("faction",     t.faction());
    obj.set("color",       t.color());
    obj.set("controller",  static_cast<int>(t.controller()));
    obj.set("gold",        t.gold());
    obj.set("income",      t.total_income());
    obj.set("baseIncome",  t.base_income());
    obj.set("villageGold", t.village_gold());
    obj.set("support",     t.support());
    obj.set("recallCost",  t.recall_cost());
    obj.set("objectives",  t.objectives().str());
    obj.set("objectivesChanged", t.objectives_changed());
    obj.set("lost",        t.lost());

    val villages = val::array();
    for(const map_location& v : t.villages()) {
        val vl = val::object();
        vl.set("x", v.wml_x());
        vl.set("y", v.wml_y());
        villages.call<void>("push", vl);
    }
    obj.set("villages", villages);

    val enemies = val::array();
    for(int s = 1; s <= static_cast<int>(teams.size()); ++s)
        if(t.is_enemy(s)) enemies.call<void>("push", s);
    obj.set("enemySides", enemies);

    /* recruits and recallList are populated by JS (engine-utils) for side 1 */
    obj.set("recruits",   val::array());
    obj.set("recallList", val::array());

    return obj;
#endif
}

JsVal WesnothEngine::queryReach(int x, int y)
{
#ifndef __EMSCRIPTEN__
    (void)x; (void)y;
    return {};
#else
    if(!impl_ || !resources::gameboard) return val::null();
    map_location loc(x - 1, y - 1);
    auto it = resources::gameboard->units().find(loc);
    if(it == resources::gameboard->units().end()) return val::null();

    const unit& u = *it;
    int side = u.side();
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > static_cast<int>(teams.size())) return val::null();

    const team& vt = teams[static_cast<size_t>(side - 1)];
    pathfind::paths paths_obj(u, false, true, vt);
    const gamemap& m = resources::gameboard->map();

    val reachable = val::array();
    val attackable = val::array();
    for(const pathfind::paths::step& s : paths_obj.destinations) {
        val hl = val::object();
        hl.set("x", s.curr.wml_x());
        hl.set("y", s.curr.wml_y());

        bool can_atk = false;
        for(const map_location& adj : get_adjacent_tiles(s.curr)) {
            auto aj = resources::gameboard->units().find(adj);
            if(aj != resources::gameboard->units().end() && vt.is_enemy(aj->side())) {
                can_atk = true; break;
            }
        }

        val h = val::object();
        h.set("loc",          hl);
        h.set("movesLeft",    s.move_left);
        h.set("defense",      u.defense_modifier(m.get_terrain(s.curr)));
        h.set("canAttackFrom", can_atk);
        reachable.call<void>("push", h);
        if(can_atk) attackable.call<void>("push", hl);
    }
    val result = val::object();
    result.set("reachable",  reachable);
    result.set("attackable", attackable);
    return result;
#endif
}

JsVal WesnothEngine::queryAttackOptions(int ax, int ay, int dx, int dy)
{
#ifndef __EMSCRIPTEN__
    (void)ax; (void)ay; (void)dx; (void)dy;
    return {};
#else
    if(!impl_ || !resources::gameboard) return val::null();

    map_location att(ax - 1, ay - 1);
    map_location def(dx - 1, dy - 1);
    const unit_map& units = resources::gameboard->units();
    auto ai = units.find(att);
    auto di = units.find(def);
    if(ai == units.end() || di == units.end()) return val::null();

    val arr = val::array();
    int best_idx = 0;
    double best_score = -1.0;

    int wi = 0;
    for([[maybe_unused]] const auto& atk : ai->attacks()) {
        try {
            battle_context bc(units, att, def, wi, -1, 0.0);
            const auto& as = bc.get_attacker_stats();
            const auto& ds = bc.get_defender_stats();

            val opt = val::object();
            val av = val::object();
            av.set("weaponIndex",    wi);
            av.set("damage",         as.damage);
            av.set("numBlows",       static_cast<int>(as.num_blows));
            av.set("chanceToHit",    static_cast<int>(as.chance_to_hit));
            av.set("expectedDamage", as.damage * static_cast<int>(as.num_blows)
                                     * static_cast<int>(as.chance_to_hit) / 100);
            val dv = val::object();
            dv.set("weaponIndex",    ds.attack_num);
            dv.set("damage",         ds.damage);
            dv.set("numBlows",       static_cast<int>(ds.num_blows));
            dv.set("chanceToHit",    static_cast<int>(ds.chance_to_hit));
            dv.set("expectedDamage", ds.damage * static_cast<int>(ds.num_blows)
                                     * static_cast<int>(ds.chance_to_hit) / 100);
            opt.set("attacker", av);
            opt.set("defender", dv);

            double score = as.damage * as.num_blows * as.chance_to_hit / 100.0
                         - ds.damage * ds.num_blows * ds.chance_to_hit / 200.0;
            if(score > best_score) {
                best_score = score;
                best_idx = static_cast<int>(arr["length"].as<int>());
            }
            arr.call<void>("push", opt);
        } catch(...) {}
        ++wi;
    }

    val result = val::object();
    result.set("options",       arr);
    result.set("defaultOption", best_idx);
    return result;
#endif
}
