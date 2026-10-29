#pragma once

extern "C" {
#include <lua.h>
}

namespace hikari::lua_native {

// Process-wide locale for Boost.Locale case mapping: system default, else
// C.UTF-8, else classic.
void installGlobalLocale();

// The re, unicode and lfs FFI functions return malloc'd strings the scripts free.
void preload(lua_State *L);

}
