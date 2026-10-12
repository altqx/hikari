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

// DependencyControl's native modules (cmake/DependencyControlModules.cmake),
// loaded through the bundled Lua wrappers and requireffi the way scripts load
// them: from <package.path dir>/BM/BadMutex/libBadMutex.so and so on.

#include "check.h"

#include <curl/curl.h>
#include <lua.hpp>

#include <string>

namespace {

// HIKARISUB_TEST_AUTOMATION_INCLUDE: the bundled Automation/automation/Include;
// HIKARISUB_TEST_DEPCTRL_DIR: where the build writes the modules.  requireffi
// tries every package.path directory, as the application's include path.
std::string Run(const char *chunk)
{
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	lua_getglobal(L, "package");
	std::string path = std::string(HIKARISUB_TEST_AUTOMATION_INCLUDE "/?.lua;") + HIKARISUB_TEST_DEPCTRL_DIR "/?.lua";
	lua_pushstring(L, path.c_str());
	lua_setfield(L, -2, "path");
	lua_pop(L, 1);
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

TEST(depctrl_badmutex_loads_from_its_module_directory)
{
	CHECK_RUN("local BM = require 'BM.BadMutex' "
		"BM.lock() BM.unlock() local got = BM.tryLock() BM.unlock() "
		"return tostring(got) .. ' ' .. BM.loadedLibraryPath:match('BM/BadMutex/[^/]*$')",
		"true BM/BadMutex/libBadMutex.so");
}

TEST(depctrl_precisetimer_loads_and_measures)
{
	CHECK_RUN("local PT = require 'PT.PreciseTimer' "
		"local t = PT() PT.sleep(20) local elapsed = t:timeElapsed() "
		"return tostring(elapsed >= 0.015 and elapsed < 5) .. ' ' .. PT.loadedLibraryPath:match('PT/PreciseTimer/[^/]*$')",
		"true PT/PreciseTimer/libPreciseTimer.so");
}

TEST(depctrl_downloadmanager_loads_and_hashes_with_its_own_sha1)
{
	// The test, like the application, has libcurl and so OpenSSL's libcrypto
	// loaded; its SHA1_Init/Update/Final must not replace the module's own.
	CHECK(curl_version() != nullptr);
	CHECK_RUN("local DM = require 'DM.DownloadManager' "
		"local m = DM() "
		"local ok, err = m:checkStringSHA1('abc', 'a9993e364706816aba3e25717850c26c9cd0d89d') "
		"return tostring(ok) .. ' ' .. tostring(err)",
		"true nil");
	// The updater checks downloaded files this way.
	CHECK_RUN("local DM = require 'DM.DownloadManager' "
		"local name = os.tmpname() local f = assert(io.open(name, 'wb')) f:write('abc') f:close() "
		"local ok, err = DM():checkFileSHA1(name, 'A9993E364706816ABA3E25717850C26C9CD0D89D') os.remove(name) "
		"return tostring(ok) .. ' ' .. tostring(err)",
		"true nil");
	CHECK_RUN("local DM = require 'DM.DownloadManager' "
		"return tostring(DM():isInternetConnected())",
		"true");
}
