/**
 * wl_engine.hpp  —  WesnothLite internal C++ types
 *
 * Not part of the public API.  Included only by wesnothlite.cpp.
 */

#pragma once

#include "wesnothlite.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

class commandline_options;
class game_config_manager;
class saved_game;

/* =========================================================================
 * Typed per-event structs (WLEv namespace)
 *
 * Each struct carries exactly the fields that event type needs.
 * WLEventInternal is a std::variant over all of them.
 * ========================================================================= */
namespace WLEv {

struct LoadingConfig  {};
struct ScenarioStart  {};
struct ScenarioEnd    { WL_Outcome outcome = WL_OUTCOME_NONE; std::string next_scenario; };
struct TurnStart      { int turn = 0; };
struct SideTurnStart  { int side = 0; int turn = 0; };
struct SideTurnEnd    { int side = 0; int turn = 0; };
struct WaitingForInput{ int side = 0; int turn = 0; };

struct UnitMove {
    std::string unit_id;
    int         side = 0;
    WL_Loc      from{0,0}, to{0,0};
    std::vector<WL_Loc> path;
};
struct UnitAttack {
    std::string     attacker_id, defender_id;
    int             attacker_side = 0, defender_side = 0;
    WL_Loc          attacker_loc{0,0}, defender_loc{0,0};
    WL_CombatResult cr_att{}, cr_def{};
    std::vector<WL_Blow> blows;
};
struct UnitSpawn   { std::string unit_type_id, unit_id; int side = 0; WL_Loc at{0,0}; };
struct UnitDismiss { std::string unit_id, unit_type_id; int side = 0; };
struct UnitDie     { std::string unit_id, unit_type_id, killer_id; int side = 0; WL_Loc loc{0,0}; };
struct UnitAdvance { std::string unit_id, from_type_id, to_type_id; int side = 0; WL_Loc loc{0,0}; };
struct UnitXP      { std::string unit_id; int side = 0; WL_Loc loc{0,0}; int xp_gained=0, xp_total=0, xp_needed=0; };
struct UnitHeal    { std::string unit_id; int side = 0; WL_Loc loc{0,0}; int amount = 0; };
struct UnitStatus  { std::string unit_id; int side = 0; WL_Loc loc{0,0}; WL_UnitStatusFlags flags = WL_STATUS_NONE; };

struct VillageCapture { WL_Loc loc{0,0}; int old_side = 0, new_side = 0; };

struct Message     { std::string speaker, portrait, text; };
struct Story       { std::string title, text, background; };
struct ObjectivesUpdate { int side = 0; std::string text; };
struct ChoiceNeeded { WL_ChoiceKind kind = WL_CHOICE_MESSAGE; std::string prompt, speaker; std::vector<std::string> options; };

struct Sound       { std::string path; };
struct MusicChange { std::string path, title; };

} // namespace WLEv

/* WLEventInternal: the one type posted through WLChannel::events. */
using WLEventInternal = std::variant<
    WLEv::LoadingConfig,
    WLEv::ScenarioStart,
    WLEv::ScenarioEnd,
    WLEv::TurnStart,
    WLEv::SideTurnStart,
    WLEv::SideTurnEnd,
    WLEv::WaitingForInput,
    WLEv::UnitMove,
    WLEv::UnitAttack,
    WLEv::UnitSpawn,
    WLEv::UnitDismiss,
    WLEv::UnitDie,
    WLEv::UnitAdvance,
    WLEv::UnitXP,
    WLEv::UnitHeal,
    WLEv::UnitStatus,
    WLEv::VillageCapture,
    WLEv::Message,
    WLEv::Story,
    WLEv::ObjectivesUpdate,
    WLEv::ChoiceNeeded,
    WLEv::Sound,
    WLEv::MusicChange
>;

/* =========================================================================
 * Command: API thread → game thread
 *
 * Uses WL_CmdType from the public header as discriminator.
 * str1 is a non-owning pointer: valid for the duration of send_command(),
 * which blocks synchronously until the game thread posts its result.
 * ========================================================================= */
struct WLCommand {
    WL_CmdType  type  = WL_CMD_END_TURN;
    WL_Loc      loc1{0, 0}, loc2{0, 0};
    const char* str1 = nullptr;   /* unit_type_id or unit_id */
    int         int1 = -1;        /* weapon_index or option_index */
};

/* =========================================================================
 * WLChannel: thread-safe bidirectional channel between game and API threads
 * ========================================================================= */
struct WLChannel {
    /* ── Events: game thread writes, API thread reads ─── */
    std::mutex              ev_mtx;
    std::condition_variable ev_cv;
    std::deque<WLEventInternal> events;
    bool game_waiting   = false;  /* game thread blocked waiting for a command */
    bool awaiting_ack   = false;  /* game thread blocked in await_ack() for narrative ACK */
    bool game_done      = false;

    /* ── Commands: API thread writes, game thread reads ── */
    std::mutex              cmd_mtx;
    std::condition_variable cmd_cv;
    std::condition_variable result_cv;
    std::optional<WLCommand> pending_cmd;
    std::optional<WL_Status> cmd_result;

    /* ── Choice sub-channel ────────────────────────────────────────────────
     * The game thread calls request_choice() when a human-controlled unit
     * needs a choice (advancement, WML message options).  It posts a
     * WL_EVENT_CHOICE_NEEDED event and then blocks until the API thread
     * delivers the result via deliver_choice() (called from wl_choose()).
     * This bypasses the normal command pipeline so it works whether or not
     * play_human_turn() is active.
     * -------------------------------------------------------------------- */
    std::mutex              choice_mtx;
    std::condition_variable choice_result_cv;
    std::optional<int>      pending_choice_result;
    bool                    choice_pending = false;

    /* Called by the game thread to post an event to the API thread. */
    void post_event(WLEventInternal ev)
    {
        {
            std::lock_guard lock(ev_mtx);
            events.push_back(std::move(ev));
        }
        ev_cv.notify_one();
    }

    /* Called by the game thread when entering the human-turn wait. */
    void set_waiting(bool w)
    {
        {
            std::lock_guard lock(ev_mtx);
            game_waiting = w;
        }
        ev_cv.notify_one();
    }

    /* Called by the game thread when the scenario finishes. */
    void set_done()
    {
        {
            std::lock_guard lock(ev_mtx);
            game_done = true;
        }
        ev_cv.notify_one();
    }

    /* Called by the game thread: block until a command arrives. */
    WLCommand wait_for_command()
    {
        std::unique_lock lock(cmd_mtx);
        cmd_cv.wait(lock, [this] { return pending_cmd.has_value(); });
        WLCommand cmd = std::move(*pending_cmd);
        pending_cmd.reset();
        return cmd;
    }

    /* Called by the game thread: post result of a command back to API thread. */
    void post_result(WL_Status s)
    {
        {
            std::lock_guard lock(cmd_mtx);
            cmd_result = s;
        }
        result_cv.notify_one();
    }

    /**
     * Called by the game thread: post a WL_EVENT_CHOICE_NEEDED event and
     * block until the API thread delivers a choice via deliver_choice().
     * Returns the chosen option index.
     */
    int request_choice(WL_ChoiceKind kind,
                       const std::string& prompt,
                       const std::vector<std::string>& options,
                       const std::string& speaker = "")
    {
        post_event(WLEv::ChoiceNeeded{kind, prompt, speaker, options});

        std::unique_lock lock(choice_mtx);
        choice_pending = true;
        pending_choice_result.reset();
        choice_result_cv.wait(lock, [this] { return pending_choice_result.has_value(); });
        int result = *pending_choice_result;
        pending_choice_result.reset();
        choice_pending = false;
        return result;
    }

    /**
     * Called by the game thread: block until the API thread sends any
     * CHOOSE_OPTION (i.e. the player dismisses a narrative or dialog).
     * Used for WL_EVENT_MESSAGE so the game thread blocks waiting for
     * the player to acknowledge the message, without posting a second
     * WL_EVENT_CHOICE_NEEDED event.
     */
    void await_ack()
    {
        /* Set choice_pending BEFORE signalling ev_cv so that any caller
         * that wakes from wl_step() seeing awaiting_ack==true can safely
         * call deliver_choice() without a race on choice_pending. */
        {
            std::lock_guard choice_lock(choice_mtx);
            choice_pending = true;
            pending_choice_result.reset();
        }

        /* Signal wl_step() to return null while we wait for the ACK. */
        {
            std::lock_guard ev_lock(ev_mtx);
            awaiting_ack = true;
        }
        ev_cv.notify_one();

        std::unique_lock lock(choice_mtx);
        choice_result_cv.wait(lock, [this] { return pending_choice_result.has_value(); });
        pending_choice_result.reset();
        choice_pending = false;

        {
            std::lock_guard ev_lock(ev_mtx);
            awaiting_ack = false;
        }
    }

    /**
     * Called by the API thread (wl_choose()): deliver a choice result to
     * the game thread blocked in request_choice() or await_ack().
     * Returns true if a choice was pending and was delivered.
     */
    bool deliver_choice(int option_index)
    {
        std::lock_guard lock(choice_mtx);
        if(!choice_pending) return false;
        pending_choice_result = option_index;
        choice_result_cv.notify_one();
        return true;
    }

    /* Called by the API thread: send a command and wait for its result. */
    WL_Status send_command(WLCommand cmd)
    {
        {
            std::lock_guard lock(cmd_mtx);
            pending_cmd = std::move(cmd);
            cmd_result.reset();
        }
        cmd_cv.notify_one();

        std::unique_lock lock(cmd_mtx);
        result_cv.wait(lock, [this] { return cmd_result.has_value(); });
        return *cmd_result;
    }
};

/* =========================================================================
 * Arena allocator for snapshot strings
 * ========================================================================= */
struct WLArena {
    std::vector<char> buf;

    explicit WLArena(size_t reserve = 1024) { buf.reserve(reserve); }

    /* Copy a std::string into the arena; return pointer into arena. */
    const char* store(const std::string& s)
    {
        if (s.empty()) {
            /* Store an empty C string so callers don't get dangling ptrs. */
            buf.push_back('\0');
            return &buf[buf.size() - 1];
        }
        size_t off = buf.size();
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back('\0');
        return &buf[off];
    }

    const char* store(const char* s)
    {
        return store(s ? std::string(s) : std::string{});
    }
};

/* =========================================================================
 * WLEngineImpl: the concrete C++ object behind WL_Engine*
 * ========================================================================= */
struct WLEngineImpl {
    /* Game initialisation state (mirrors headless_engine::impl). */
    std::unique_ptr<commandline_options> cmdline_opts;
    std::unique_ptr<game_config_manager> config_manager;
    std::unique_ptr<saved_game>          state;
    bool initialized = false;

    /* Thread + channel. */
    std::shared_ptr<WLChannel> channel;
    std::thread                game_thread;
    std::exception_ptr         game_exception;

    /* Current materialized event (valid until next wl_step()). */
    /* We allocate one big buffer: [WL_Event][string data…]          */
    std::vector<char>          event_buf;
    WL_Event*                  current_event = nullptr;

    /* Last error string. */
    std::string last_error;

    /* Set by wl_start_campaign / wl_start_scenario: game thread must finish
     * config load + scenario init before running.  Clear after use. */
    bool        needs_scenario_init = false;
    std::string pending_scenario_id;
};

/* Expose the opaque C type as an alias so we can use the pointer directly. */
struct WL_Engine {
    WLEngineImpl* impl;
};
