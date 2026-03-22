/* Headless stub: scripting/lua_audio.cpp
 * Creates wesnoth.audio.music_list with no-op stubs so Lua WML scripts
 * that call music_list.add / .clear / .play / etc. don't crash.
 */
#include "scripting/lua_audio.hpp"
#include "scripting/lua_common.hpp"
#include "lua/lauxlib.h"
#include "lua/lua.h"

namespace lua_audio {

static int noop(lua_State*) { return 0; }
static int noop_len(lua_State* L) { lua_pushinteger(L, 0); return 1; }

// __index: look up the key in the methods table (upvalue 1), else return nil
static int music_index(lua_State* L) {
    // L: [userdata, key]
    lua_pushvalue(L, 2);           // push key
    lua_rawget(L, lua_upvalueindex(1));  // look up in methods table
    return 1;
}
static int music_newindex(lua_State*) { return 0; }

static void push_music_list(lua_State* L) {
    // Build the methods table (upvalue for __index)
    lua_createtable(L, 0, 8);
    static luaL_Reg const methods[] = {
        { "play",          noop     },
        { "add",           noop     },
        { "clear",         noop     },
        { "remove",        noop     },
        { "next",          noop     },
        { "force_refresh", noop     },
        { nullptr, nullptr          },
    };
    luaL_setfuncs(L, methods, 0);
    // methods table is at top; use it as upvalue for __index closure

    lua_newuserdatauv(L, 0, 0);    // the music_list userdata
    lua_createtable(L, 0, 4);      // metatable
    // __index closure captures the methods table
    lua_pushvalue(L, -3);          // methods table as upvalue
    lua_pushcclosure(L, music_index, 1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, music_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, noop_len);
    lua_setfield(L, -2, "__len");
    lua_setmetatable(L, -2);       // set metatable on userdata
    // stack: [methods_table, userdata] — remove methods_table
    lua_remove(L, -2);
}

std::string register_table(lua_State* L) {
    luaW_getglobal(L, "wesnoth", "audio");  // push wesnoth.audio

    push_music_list(L);
    lua_setfield(L, -2, "music_list");

    // sources: same pattern, simpler
    lua_newuserdatauv(L, 0, 0);
    lua_createtable(L, 0, 2);
    lua_createtable(L, 0, 0);      // empty methods for upvalue
    lua_pushcclosure(L, music_index, 1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, music_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "sources");

    lua_pop(L, 1);  // pop wesnoth.audio
    return "";
}

} // namespace lua_audio
