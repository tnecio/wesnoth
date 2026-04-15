/**
 * wl_bindings.cpp  —  Embind bindings for WesnothEngine (WASM only)
 *
 * Compiled only when building for Emscripten.  Registers WesnothEngine and
 * the WL_Status integer constants with the JS module.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten/bind.h>
#include "wesnoth_engine.hpp"
#include "wesnothlite.h"   /* WL_Status constants */

EMSCRIPTEN_BINDINGS(wesnothlite) {
    using namespace emscripten;

    /* ── WL_Status constants ──────────────────────────────────────────────── */
    constant("WL_OK",           static_cast<int>(WL_OK));
    constant("WL_ERR_GENERIC",  static_cast<int>(WL_ERR_GENERIC));
    constant("WL_ERR_INVALID",  static_cast<int>(WL_ERR_INVALID));
    constant("WL_ERR_NO_GAME",  static_cast<int>(WL_ERR_NO_GAME));
    constant("WL_ERR_NOT_TURN", static_cast<int>(WL_ERR_NOT_TURN));
    constant("WL_ERR_UNKNOWN",  static_cast<int>(WL_ERR_UNKNOWN));
    constant("WL_ERR_NO_PATH",  static_cast<int>(WL_ERR_NO_PATH));
    constant("WL_ERR_NO_GOLD",  static_cast<int>(WL_ERR_NO_GOLD));
    constant("WL_ERR_NO_SPACE", static_cast<int>(WL_ERR_NO_SPACE));

    /* ── WesnothEngine class ─────────────────────────────────────────────── */
    class_<WesnothEngine>("WesnothEngine")
        .constructor<std::string, std::string, std::string>()
        .property("initialized", &WesnothEngine::initialized)
        .function("lastError",          &WesnothEngine::lastError)
        .function("setLocale",          &WesnothEngine::setLocale)
        .function("listCampaigns",      &WesnothEngine::listCampaigns)
        .function("startCampaign",      &WesnothEngine::startCampaign)
        .function("startScenario",      &WesnothEngine::startScenario)
        .function("loadFromBuffer",     &WesnothEngine::loadFromBuffer)
        .function("saveToBuffer",       &WesnothEngine::saveToBuffer)
        .function("step",               &WesnothEngine::step)
        .function("sendMove",           &WesnothEngine::sendMove)
        .function("sendAttack",         &WesnothEngine::sendAttack)
        .function("sendRecruit",        &WesnothEngine::sendRecruit)
        .function("sendRecall",         &WesnothEngine::sendRecall)
        .function("sendDismiss",        &WesnothEngine::sendDismiss)
        .function("sendEndTurn",        &WesnothEngine::sendEndTurn)
        .function("sendUndo",           &WesnothEngine::sendUndo)
        .function("sendChoose",         &WesnothEngine::sendChoose)
        .function("queryGame",          &WesnothEngine::queryGame)
        .function("queryMap",           &WesnothEngine::queryMap)
        .function("queryVisibility",    &WesnothEngine::queryVisibility)
        .function("queryUnits",         &WesnothEngine::queryUnits)
        .function("queryUnitAt",        &WesnothEngine::queryUnitAt)
        .function("queryUnitType",      &WesnothEngine::queryUnitType)
        .function("queryRecallList",    &WesnothEngine::queryRecallList)
        .function("queryRecruitList",   &WesnothEngine::queryRecruitList)
        .function("queryTeam",          &WesnothEngine::queryTeam)
        .function("queryReach",               &WesnothEngine::queryReach)
        .function("queryAttackOptions",       &WesnothEngine::queryAttackOptions)
        .function("queryTerrainAt",           &WesnothEngine::queryTerrainAt)
        .function("queryUnitTypeAnimations",  &WesnothEngine::queryUnitTypeAnimations)
        ;
}

#endif /* __EMSCRIPTEN__ */
