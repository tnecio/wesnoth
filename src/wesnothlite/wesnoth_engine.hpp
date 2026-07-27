/**
 * wesnoth_engine.hpp  —  C++ class interface for Embind (WASM) and native
 *
 * In the WASM build, Embind exposes this class directly to JavaScript.
 * In the native build, this class compiles but is not used (the C API in
 * wesnothlite.h / wesnothlite.cpp continues to serve wl-cli).
 */

#pragma once

#include "wl_engine.hpp"   /* WLEngineImpl, WLEv, WLCommand, WLChannel */

#ifdef __EMSCRIPTEN__
#  include <emscripten/val.h>
   using JsVal = emscripten::val;
#else
   struct JsVal {};  /* no-op stub so method signatures compile in native build */
#endif

#include <string>
#include <vector>

class WesnothEngine {
public:
    /**
     * Initialise the engine.  Mirrors wl_init().
     * data_path:     directory containing data/core and data/campaigns
     * userdata_path: writable directory for saves / user data
     * options:       space-separated extra options (may be empty)
     */
    WesnothEngine(const std::string& data_path,
                  const std::string& userdata_path,
                  const std::string& options);
    ~WesnothEngine();

    // Non-copyable/movable
    WesnothEngine(const WesnothEngine&) = delete;
    WesnothEngine& operator=(const WesnothEngine&) = delete;

    /* ── Properties / status ─────────────────────────────────────────────── */
    bool        initialized() const;
    std::string lastError()   const;

    /* ── Locale ──────────────────────────────────────────────────────────── */
    int setLocale(const std::string& locale,
                  const std::string& translations_path);

    /* ── Session setup ───────────────────────────────────────────────────── */
    /** Returns JS array of campaign info objects. */
    JsVal listCampaigns();

    int startCampaign(const std::string& campaign_id,
                      const std::string& difficulty);
    int startScenario(const std::string& campaign_id,
                      const std::string& scenario_id,
                      const std::string& difficulty);

    /** Load a saved game from a Uint8Array (WASM) / ignored (native). */
    int loadFromBuffer(JsVal buf);

    /** Return the current save file as a Uint8Array (WASM) / null (native). */
    JsVal saveToBuffer();

    /* ── Game pump ───────────────────────────────────────────────────────── */
    /**
     * Dequeue the next engine event and return it as a plain JS object, or
     * null when the game thread is waiting for player input / narrative ACK.
     * The object's "type" field is a snake_case string (e.g. "unit_move").
     */
    JsVal step();

    /* ── Commands ────────────────────────────────────────────────────────── */
    int sendMove(int fx, int fy, int tx, int ty);
    int sendAttack(int ax, int ay, int dx, int dy, int weapon);
    int sendRecruit(const std::string& type_id, int x, int y);
    int sendRecall(const std::string& unit_id, int x, int y);
    int sendDismiss(const std::string& unit_id);
    int sendEndTurn();
    int sendUndo();
    /** Deliver a choice index; also ACKs a simple message (index 0). */
    int sendChoose(int option);

    /* ── Queries ─────────────────────────────────────────────────────────── */
    JsVal queryGame();
    JsVal queryMap();
    JsVal queryVisibility(int side);
    JsVal queryUnits();
    JsVal queryUnitAt(int x, int y);
    JsVal queryUnitType(const std::string& type_id);
    JsVal queryRecallList(int side);
    JsVal queryRecruitList(int side);
    JsVal queryTeam(int side);
    JsVal queryReach(int x, int y);
    JsVal queryAttackOptions(int ax, int ay, int dx, int dy);

    /** All terrain image layers (background + foreground) for one hex.
     *  Returns { background: WlTerrainFrame[], foreground: WlTerrainFrame[] }.
     *  Call once per hex after scenario start; result is stable until map reload. */
    JsVal queryTerrainAt(int x, int y);

    /** All animation frame sequences for a unit type, grouped by event name.
     *  Returns Record<string, WlAnimFrame[]>.
     *  Stable for the life of the loaded game config; cache by type_id. */
    JsVal queryUnitTypeAnimations(const std::string& type_id);

    /** Named color palettes from game config (tc_info).
     *  Returns { [name]: [[r,g,b], ...] }.
     *  Available immediately after engine init; does not require a running scenario. */
    JsVal queryColorPalettes();

    /** Named color ranges from game config (color_info).
     *  Returns { [name]: { mid:[r,g,b], max:[r,g,b], min:[r,g,b], rep:[r,g,b] } }.
     *  Available immediately after engine init. */
    JsVal queryColorRanges();

    /** Color range for a specific team side (1-based).
     *  Returns { mid:[r,g,b], max:[r,g,b], min:[r,g,b], rep:[r,g,b] } or null. */
    JsVal querySideColorRange(int side);

private:
    WLEngineImpl* impl_;

#ifdef __EMSCRIPTEN__
    JsVal buildEventVal(const WLEventInternal& ev);
#endif

    int doSend(WLCommand cmd);
};
