// LuaJIT in its own process, as the Lua helper runs it: the JIT and FFI are
// available (B4).
#include <cstdio>
#include <cstring>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

int main()
{
    lua_State *L = luaL_newstate();
    if (!L)
        return 1;
    luaL_openlibs(L);
    const char *script = "local ffi = require('ffi')\n"
                         "assert(ffi.abi('64bit'))\n"
                         "return jit.version";
    if (luaL_dostring(L, script) != 0) {
        std::fprintf(stderr, "FAIL %s\n", lua_tostring(L, -1));
        return 1;
    }
    const char *version = lua_tostring(L, -1);
    std::printf("luajit=%s\n", version);
    const bool ok = version && std::strncmp(version, "LuaJIT 2.1", 10) == 0;
    lua_close(L);
    std::puts(ok ? "PASS luajit-load" : "FAIL luajit version");
    return ok ? 0 : 1;
}
