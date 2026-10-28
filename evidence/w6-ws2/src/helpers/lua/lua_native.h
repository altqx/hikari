#pragma once

// The legacy native preloads of the Lua helper (L6; HikariSub/AutomationUtils.cpp
// preload_modules and HikariSub/AutomationFileSystem.cpp at 20d647c4):
// aegisub.__re_impl (Boost.Regex over ICU), aegisub.__unicode_impl
// (Boost.Locale case mapping), aegisub.__lfs_impl, lpeg 0.10 and luabins, all
// registered in package.preload. The re, unicode and lfs tables hold LuaJIT
// FFI function pointers, as legacy did; strings they return are malloc'd and
// freed by the scripts (aegisub/ffi.moon).

extern "C" {
#include <lua.h>
}

namespace hikari::lua_native {

// The process-wide locale Boost.Locale case mapping uses: the system default
// (legacy HikariSubFrame), falling back to C.UTF-8 and then the classic locale.
void installGlobalLocale();

void preload(lua_State *L);

} // namespace hikari::lua_native
