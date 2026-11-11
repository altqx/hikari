#pragma once

extern "C" {
#include <lua.h>
}

// The native Karaoke Templater: 2.1.7's template language and output with HikariSub's KE1 additions,
// in this helper's Lua state. Lua runs only the templates' code Lines and !expressions!.
namespace hikari::kara_templater {

// The top level of application::kBuiltinKaraokeTemplater: karaskel for code Lines, then the macros.
int load(lua_State *L);

}
