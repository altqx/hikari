//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.

// The LuaJIT the application links (cmake/VendoredLuaJIT.cmake) must be the one
// the Windows build compiles in Thirdparty/Build/LuaJit/LuaJit.vcxproj: Lua 5.2
// extensions on, string.buffer off.  Automation scripts written against the
// Windows build depend on the former.

#include "check.h"

#include <lua.hpp>

#include <string>

namespace {

// Runs a chunk in a fresh state with the standard libraries, as the automation
// loader opens them, and returns its result as a string ("error: ..." when the
// chunk fails to compile or run).
std::string Run(const char *chunk)
{
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	std::string result;
	if (luaL_loadstring(L, chunk) || lua_pcall(L, 0, 1, 0))
		result = std::string("error: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
	else
		result = lua_tostring(L, -1) ? lua_tostring(L, -1) : "nil";
	lua_close(L);
	return result;
}

void ExpectRun(const char *chunk, const char *expected, int line)
{
	std::string actual = Run(chunk);
	if (actual == expected)
		return;
	++check::Failures();
	std::printf("%s:%d: %s\n  got      %s\n  expected %s\n", __FILE__, line, chunk, actual.c_str(), expected);
}

#define CHECK_RUN(chunk, expected) ExpectRun(chunk, expected, __LINE__)

} // namespace

TEST(luajit_has_the_lua52_library_functions)
{
	CHECK_RUN("local t = table.pack(1, nil, 3) return t.n .. ',' .. t[3]", "3,3");
	CHECK_RUN("return select('#', table.unpack({1, 2, 3}))", "3");
	CHECK_RUN("return type(package.searchers)", "table");
}

TEST(luajit_honours_the_lua52_metamethods)
{
	CHECK_RUN("local n = 0 "
		"for k in pairs(setmetatable({}, { __pairs = function() "
		"  return function(_, k) if not k then return 1 end end, nil, nil end })) do n = n + 1 end "
		"return n", "1");
	CHECK_RUN("local seen = false "
		"for _ in ipairs(setmetatable({}, { __ipairs = function() seen = true return function() end end })) do end "
		"return tostring(seen)", "true");
	CHECK_RUN("return #setmetatable({}, { __len = function() return 42 end })", "42");
}

TEST(luajit_parses_the_lua52_syntax)
{
	// MoonScript's `continue` compiles to a break that is not the last
	// statement of its block.
	CHECK_RUN("repeat break; local x = 1 until true return 'ok'", "ok");
	CHECK_RUN("for i = 1, 2 do if i == 1 then goto continue end do return 'reached ' .. i end ::continue:: end", "reached 2");
}

TEST(luajit_has_no_string_buffer_as_on_windows)
{
	CHECK_RUN("return tostring((pcall(require, 'string.buffer')))", "false");
}
