#pragma once

extern "C" {
#include <lua.h>
}

// aegisub.util's slowest functions in C++, for aegisub/util.moon to call with arguments of the types it expects.
namespace hikari::util_impl {

// aegisub.__util_impl on the aegisub table at the stack top: extract_color(s), trim(s), headtail(s) for strings
// and HSL_to_RGB(H, S, L) for numbers, each returning what aegisub.util's Lua returns.
void install(lua_State *L);

}
