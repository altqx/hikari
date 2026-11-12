#include "shape_tables.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string g_written;

int write(lua_State *L)
{
    for (int i = 1; i <= lua_gettop(L); ++i) {
        std::size_t size = 0;
        const char *s = luaL_checklstring(L, i, &size);
        g_written.append(s, size);
    }
    return 0;
}

int noFont(lua_State *L)
{
    lua_pushboolean(L, 0);
    return 1;
}

std::string readFile(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

struct Lua {
    lua_State *L = luaL_newstate();
    ~Lua() { lua_close(L); }
};

void setUp(lua_State *L)
{
    luaL_openlibs(L);
    lua_newtable(L);
    hikari::shape_tables::install(L, noFont);
    lua_setglobal(L, "aegisub");
    lua_getglobal(L, "package");
    lua_pushstring(L, HIKARI_YUTILS_INCLUDE "/?.lua");
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
    lua_getglobal(L, "io");
    lua_pushcfunction(L, write);
    lua_setfield(L, -2, "write");
    lua_pop(L, 1);
}

// Counts the kernel's answers: true ones, and false ones (which run the Lua).
constexpr const char *kCountAnswers = R"(
answers = {native = 0, lua = 0}
for name, fn in pairs(aegisub.__shape_impl) do
    aegisub.__shape_impl[name] = function(...)
        local ok = fn(...)
        answers[ok and "native" or "lua"] = answers[ok and "native" or "lua"] + 1
        return fn(...)
    end
end
)";

int answers(lua_State *L, const char *kind)
{
    lua_getglobal(L, "answers");
    lua_getfield(L, -1, kind);
    const int n = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 2);
    return n;
}

}

TEST(YutilsRoute, ThePatchedYutilsStillGivesTheGoldens)
{
    Lua lua;
    setUp(lua.L);
    ASSERT_EQ(luaL_dostring(lua.L, kCountAnswers), 0) << lua_tostring(lua.L, -1);
    g_written.clear();
    ASSERT_EQ(luaL_dofile(lua.L, HIKARI_SHAPE_GOLDEN_SCRIPT), 0) << lua_tostring(lua.L, -1);
    EXPECT_TRUE(g_written == readFile(HIKARI_SHAPE_GOLDENS)) << "the routed Yutils differs from the goldens";
    EXPECT_GT(answers(lua.L, "native"), 500);
    EXPECT_LT(answers(lua.L, "lua"), 20);
}

TEST(YutilsRoute, TransformRoutesOnlyYutilsMatrices)
{
    Lua lua;
    setUp(lua.L);
    ASSERT_EQ(luaL_dostring(lua.L, kCountAnswers), 0);
    ASSERT_EQ(luaL_dostring(lua.L, R"(
        local Y = require("Yutils")
        local m = Y.math.create_matrix().translate(5, 0, 0)
        mine = Y.shape.transform("m 0 0 l 1 1", m)
        local other = {transform = function(x, y, z, w) return x * 2, y * 2, z, w or 1 end}
        theirs = Y.shape.transform("m 0 0 l 1 1", other)
    )"),
              0)
        << lua_tostring(lua.L, -1);
    lua_getglobal(lua.L, "mine");
    EXPECT_STREQ(lua_tostring(lua.L, -1), "m 5 0 l 6 1");
    lua_getglobal(lua.L, "theirs");
    EXPECT_STREQ(lua_tostring(lua.L, -1), "m 0 0 l 2 2");
    EXPECT_EQ(answers(lua.L, "native"), 1);
}

TEST(YutilsRoute, BadArgumentsStillRaiseYutilsErrors)
{
    Lua lua;
    setUp(lua.L);
    ASSERT_NE(luaL_dostring(lua.L, R"(require("Yutils").shape.to_outline("m 0 0 l 5 0 5 5", 0))"), 0);
    EXPECT_NE(std::string(lua_tostring(lua.L, -1)).find("one width must be >0"), std::string::npos);
    ASSERT_NE(luaL_dostring(lua.L, R"(require("Yutils").shape.flatten(5))"), 0);
    EXPECT_NE(std::string(lua_tostring(lua.L, -1)).find("shape expected"), std::string::npos);
}
