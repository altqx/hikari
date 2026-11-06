#include "lua_nodes.h"

#include "hikari/backends/lua_protocol.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <array>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hikari::lua_nodes {

using application::LuaNodePort;
using application::NodeValue;
using application::PortSpec;
using application::PortType;
using backends::helper::Outcome;
using backends::helper::bytesOf;

namespace {

constexpr std::array kBase = {"assert",   "error",  "ipairs",       "next",         "pairs",  "pcall",
                              "rawequal", "rawget", "rawset",       "select",       "tonumber", "tostring",
                              "type",     "unpack", "getmetatable", "setmetatable", "xpcall", "_VERSION"};

struct Library {
    const char *name;
    std::array<std::string_view, 2> without;
};
constexpr std::array kLibraries = {Library{"string", {"dump", {}}}, Library{"table", {}}, Library{"bit", {}},
                                   Library{"math", {"random", "randomseed"}}};

struct Batch {
    const LuaNodePort::Call &call;
    backends::lua::NodeRows rows;
    std::string error;
};

void open(lua_State *L, lua_CFunction library, const char *name)
{
    lua_pushcfunction(L, library);
    lua_pushstring(L, name);
    lua_call(L, 1, 0);
}

// The environment as a table at the top of the stack, copied from the libraries the state opened.
void pushEnvironment(lua_State *L)
{
    lua_newtable(L);
    for (const char *name : kBase) {
        lua_getglobal(L, name);
        lua_setfield(L, -2, name);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_G");
    for (const Library &library : kLibraries) {
        lua_newtable(L);
        lua_getglobal(L, library.name);
        for (lua_pushnil(L); lua_next(L, -2);) {
            const bool left = lua_type(L, -2) == LUA_TSTRING &&
                              std::ranges::find(library.without, std::string_view(lua_tostring(L, -2))) !=
                                  library.without.end();
            if (left) {
                lua_pop(L, 1);
                continue;
            }
            lua_pushvalue(L, -2);
            lua_insert(L, -2);
            lua_settable(L, -5);
        }
        lua_pop(L, 1);
        lua_setfield(L, -2, library.name);
    }
}

std::string colourText(const application::NodeColour &c)
{
    char text[8];
    std::snprintf(text, sizeof text, "#%02X%02X%02X", c.r & 0xFF, c.g & 0xFF, c.b & 0xFF);
    return text;
}

void pushValue(lua_State *L, const NodeValue &v)
{
    if (const auto *x = std::get_if<double>(&v))
        lua_pushnumber(L, *x);
    else if (const auto *b = std::get_if<bool>(&v))
        lua_pushboolean(L, *b);
    else if (const auto *s = std::get_if<std::string>(&v))
        lua_pushlstring(L, s->data(), s->size());
    else if (const auto *t = std::get_if<application::NodeTags>(&v))
        lua_pushlstring(L, t->text.data(), t->text.size());
    else if (const auto *c = std::get_if<application::NodeColour>(&v))
        lua_pushstring(L, colourText(*c).c_str());
    else if (const auto *p = std::get_if<application::NodePoint>(&v)) {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, p->x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, p->y);
        lua_setfield(L, -2, "y");
    } else {
        lua_pushnil(L);
    }
}

std::optional<application::NodeColour> colourOf(std::string_view text)
{
    if (text.size() != 7 || text[0] != '#')
        return std::nullopt;
    int parts[3];
    for (int i = 0; i < 3; ++i) {
        int v = 0;
        for (const char c : text.substr(1 + 2 * i, 2)) {
            const int digit = c >= '0' && c <= '9' ? c - '0'
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                              : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                     : -1;
            if (digit < 0)
                return std::nullopt;
            v = v * 16 + digit;
        }
        parts[i] = v;
    }
    return application::NodeColour{parts[0], parts[1], parts[2]};
}

// The value at the top of the stack as the port's type, or why it isn't one.
std::expected<NodeValue, std::string> valueOf(lua_State *L, const PortSpec &port)
{
    const std::string what = "output \"" + port.name + "\"";
    const int type = lua_type(L, -1);
    if (type == LUA_TNIL)
        return std::unexpected("did not set its " + what);
    const std::string wrong = what + " is a " + lua_typename(L, type) + ", not a " +
                              std::string(application::portTypeName(port.type));
    switch (port.type) {
    case PortType::Text:
    case PortType::Tags: {
        if (type != LUA_TSTRING && type != LUA_TNUMBER)
            return std::unexpected(wrong);
        lua_pushvalue(L, -1);
        std::size_t size = 0;
        const char *s = lua_tolstring(L, -1, &size);
        std::string text(s, size);
        lua_pop(L, 1);
        if (port.type == PortType::Tags)
            return application::NodeTags{std::move(text)};
        return text;
    }
    case PortType::Bool:
        if (type != LUA_TBOOLEAN)
            return std::unexpected(wrong);
        return lua_toboolean(L, -1) != 0;
    case PortType::Colour: {
        const auto colour = type == LUA_TSTRING ? colourOf(lua_tostring(L, -1)) : std::nullopt;
        if (!colour)
            return std::unexpected(what + " is not a colour written #RRGGBB");
        return *colour;
    }
    case PortType::Point: {
        if (type != LUA_TTABLE)
            return std::unexpected(wrong);
        lua_getfield(L, -1, "x");
        lua_getfield(L, -2, "y");
        const bool numbers = lua_type(L, -2) == LUA_TNUMBER && lua_type(L, -1) == LUA_TNUMBER;
        const application::NodePoint p{lua_tonumber(L, -2), lua_tonumber(L, -1)};
        lua_pop(L, 2);
        if (!numbers)
            return std::unexpected(what + " is not a point {x = …, y = …}");
        return p;
    }
    default:
        if (type != LUA_TNUMBER)
            return std::unexpected(wrong);
        return lua_tonumber(L, -1);
    }
}

std::string errorText(lua_State *L)
{
    const char *s = lua_tostring(L, -1);
    return s ? s : std::string("(error object is a ") + luaL_typename(L, -1) + " value)";
}

// Stack: 1 the Batch, 2 the compiled code.
int runRows(lua_State *L)
{
    Batch &batch = *static_cast<Batch *>(lua_touserdata(L, 1));
    const LuaNodePort::Call &call = batch.call;
    for (const auto &row : call.rows) {
        pushEnvironment(L);
        for (std::size_t k = 0; k < call.inputs.size() && k < row.size(); ++k) {
            pushValue(L, row[k]);
            lua_setfield(L, -2, call.inputs[k].name.c_str());
        }
        lua_pushvalue(L, 2);
        lua_pushvalue(L, -2);
        lua_setfenv(L, -2);
        if (lua_pcall(L, 0, 0, 0) != 0) {
            batch.error = errorText(L);
            return 0;
        }
        std::vector<NodeValue> out;
        for (const PortSpec &port : call.outputs) {
            lua_getfield(L, -1, port.name.c_str());
            auto value = valueOf(L, port);
            lua_pop(L, 1);
            if (!value) {
                batch.error = value.error();
                return 0;
            }
            out.push_back(std::move(*value));
        }
        batch.rows.push_back(std::move(out));
        lua_pop(L, 1);
    }
    return 0;
}

}

void run(backends::helper::Reader &in, backends::helper::Responder &r, std::size_t payloadSize)
{
    const auto call = backends::lua::decodeNodeCall(in, payloadSize);
    if (!call)
        return r.terminal(Outcome::InvalidInput, bytesOf("malformed RunNodes"));
    const std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
    lua_State *L = state.get();
    if (!L)
        return r.terminal(Outcome::Failed, bytesOf("Could not initialize Lua state"));
    open(L, luaopen_base, "");
    open(L, luaopen_string, LUA_STRLIBNAME);
    open(L, luaopen_table, LUA_TABLIBNAME);
    open(L, luaopen_math, LUA_MATHLIBNAME);
    open(L, luaopen_bit, LUA_BITLIBNAME);
    // MUST KILL: getmetatable("") would reach the shared string library through __index
    lua_pushliteral(L, "");
    lua_getmetatable(L, -1);
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 2);

    Batch batch{*call, {}, {}};
    lua_pushcfunction(L, runRows);
    lua_pushlightuserdata(L, &batch);
    if (luaL_loadbufferx(L, call->code.data(), call->code.size(), "=code", "t") != 0)
        return r.terminal(Outcome::Failed, bytesOf(errorText(L)));
    if (lua_pcall(L, 2, 0, 0) != 0)
        return r.terminal(Outcome::Failed, bytesOf(errorText(L)));
    if (!batch.error.empty())
        return r.terminal(Outcome::Failed, bytesOf(batch.error));
    r.terminal(Outcome::Ok, backends::lua::encodeNodeRows(batch.rows, call->outputs));
}

}
