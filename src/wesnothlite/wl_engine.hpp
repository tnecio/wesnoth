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
#include <vector>

class commandline_options;
class game_config_manager;
class saved_game;

/* =========================================================================
 * Internal event representation
 *
 * Mirrors WL_Event but uses std::string for all text so lifetime is trivial.
 * Converted to a heap-allocated WL_Event by materialize_event().
 * ========================================================================= */
struct WLEventInternal {
    WL_EventType type = WL_EVENT_SCENARIO_START;

    /* Generic string slots (semantics depend on event type). */
    std::string s1, s2, s3, s4, s5;   /* ids, type-ids, texts, paths … */

    /* Generic integer slots. */
    int i1 = 0, i2 = 0, i3 = 0, i4 = 0, i5 = 0, i6 = 0;

    /* Locations. */
    WL_Loc loc1{0, 0}, loc2{0, 0};

    /* Move path. */
    std::vector<WL_Loc> path;

    /* Combat. */
    WL_CombatResult cr_att{}, cr_def{};
    std::vector<WL_Blow> blows;

    /* Choice. */
    WL_ChoiceKind choice_kind = WL_CHOICE_MESSAGE;
    std::vector<std::string> options;

    /* Outcome. */
    WL_Outcome outcome = WL_OUTCOME_NONE;

    /* Status flags. */
    WL_UnitStatusFlags status_flags = WL_STATUS_NONE;
};

/* =========================================================================
 * Command: API thread → game thread
 * ========================================================================= */
enum class WLCmdType {
    MOVE, ATTACK, RECRUIT, RECALL, DISMISS, END_TURN, CHOOSE, UNDO
};

struct WLCommand {
    WLCmdType type;
    WL_Loc    loc1{0, 0}, loc2{0, 0};
    std::string str1;    /* unit_type_id or unit_id */
    int       int1 = -1; /* weapon_index or option_index */
};

/* =========================================================================
 * WLChannel: thread-safe bidirectional channel between game and API threads
 * ========================================================================= */
struct WLChannel {
    /* ── Events: game thread writes, API thread reads ─── */
    std::mutex              ev_mtx;
    std::condition_variable ev_cv;
    std::deque<WLEventInternal> events;
    bool game_waiting = false;  /* game thread blocked waiting for a command */
    bool game_done    = false;

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
                       const std::string& speaker = "",
                       const std::string& portrait = "")
    {
        WLEventInternal ev;
        ev.type        = WL_EVENT_CHOICE_NEEDED;
        ev.choice_kind = kind;
        ev.s1          = prompt;
        ev.s2          = speaker;
        ev.s3          = portrait;
        ev.options     = options;
        post_event(std::move(ev));

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
     * Called by the API thread (wl_choose()): deliver a choice result to
     * the game thread blocked in request_choice().
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
