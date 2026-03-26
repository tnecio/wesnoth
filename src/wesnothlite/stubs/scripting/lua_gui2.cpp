/* Headless stub: scripting/lua_gui2.cpp
 * WML messages and dialogs are printed to stdout.
 * All other GUI Lua functions are no-ops.
 */
#include "scripting/lua_gui2.hpp"
#include "scripting/lua_common.hpp"
#include "scripting/push_check.hpp"
#include "log.hpp"
#include "lua/lauxlib.h"
#include "wl_hooks.hpp"
#include <iostream>
#include <string>

static lg::log_domain log_lua_gui("scripting/lua/gui");
#define LOG_LUA LOG_STREAM(info, log_lua_gui)

namespace lua_gui2 {

// Print the message from a narration/message config table:
//   gui.show_narration({ title=..., message=..., portrait=... })
// arg 1 is the config table, arg 2 (optional) is an options array.
int show_message_dialog(lua_State* L) {
    std::string speaker, message;
    if(lua_istable(L, 1)) {
        lua_getfield(L, 1, "title");
        if(lua_isstring(L, -1)) speaker = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "message");
        if(lua_isstring(L, -1)) message = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    if(!speaker.empty() || !message.empty()) {
        if(!speaker.empty())
            std::cout << "[MSG] " << speaker << ": " << message << "\n" << std::flush;
        else
            std::cout << "[MSG] " << message << "\n" << std::flush;
    }
    lua_pushinteger(L, 0);
    return 1;
}

// show_story: emit WL_EVENT_STORY for each part via wl_hooks
int show_story(lua_State* L) {
    // arg 1: scenario name (string), arg 2: parts array (table)
    const char* scenario_name = luaL_optstring(L, 1, "");
    if(lua_istable(L, 2)) {
        lua_Integer n = luaL_len(L, 2);
        std::string last_music;
        for(lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, 2, i);
            if(lua_istable(L, -1)) {
                auto get = [&](const char* key) -> std::string {
                    lua_getfield(L, -1, key);
                    std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
                    lua_pop(L, 1);
                    return v;
                };
                std::string music = get("music");
                if(!music.empty() && music != last_music) {
                    wl_hook_music_change(music);
                    last_music = music;
                }
                std::string title = get("title");
                if(title.empty()) {
                    lua_getfield(L, -1, "show_title");
                    if(lua_toboolean(L, -1)) title = scenario_name;
                    lua_pop(L, 1);
                }
                wl_hook_story_part(title, get("text"), get("background"));
            }
            lua_pop(L, 1);
        }
    }
    return 0;
}

static int noop_dialog(lua_State* L) {
    lua_pushinteger(L, 0);
    return 1;
}

int show_popup_dialog(lua_State* L)          { return show_message_dialog(L); }
int show_menu(lua_State* L)                  { return noop_dialog(L); }
int show_message_box(lua_State* L)           { return noop_dialog(L); }
int intf_add_widget_definition(lua_State*)   { return 0; }
int show_lua_console(lua_State*, lua_kernel_base*) { return 0; }
int show_gamestate_inspector(const std::string& /*name*/, const game_data&, const game_state&) { return 0; }

int intf_show_recruit_dialog(lua_State* L) { return noop_dialog(L); }
int intf_show_recall_dialog(lua_State* L)  { return noop_dialog(L); }

int luaW_open(lua_State* L) {
    static luaL_Reg const gui_callbacks[] = {
        { "show_menu",             &show_menu },
        { "show_narration",        &show_message_dialog },
        { "show_popup",            &show_popup_dialog },
        { "show_story",            &show_story },
        { "show_prompt",           &show_message_box },
        { "show_help",             &noop_dialog },
        { "add_widget_definition", &intf_add_widget_definition },
        { "show_dialog",           &noop_dialog },
        { "show_lua_console",      &noop_dialog },
        { nullptr, nullptr },
    };
    lua_newtable(L);
    luaL_setfuncs(L, gui_callbacks, 0);
    return 1;
}

} // namespace lua_gui2
