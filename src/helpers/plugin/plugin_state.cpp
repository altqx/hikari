#include "plugin_state.h"

#include "preludes.h"

#include "hikari/api/api.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace hikari::plugin_helper {

using api::Json;

namespace {

constexpr std::size_t kMaxSourceBytes = 16u << 20;
constexpr std::size_t kMaxCatalogBytes = 1u << 20;
constexpr int kMaxDepth = 64;
constexpr double kMaxExactInteger = 9007199254740992.0;

const char kCommandsKey[] = "hikari.commands";
const char kEventsKey[] = "hikari.events";
const char kLoadedKey[] = "hikari.loaded";
const char kArrayKey[] = "hikari.array";
const char kNull = 0;
const char kLoading = 0;

void *allocate(void *budget, void *block, std::size_t oldSize, std::size_t newSize)
{
    auto &b = *static_cast<PluginState::Budget *>(budget);
    const std::size_t held = block ? oldSize : 0;
    if (newSize == 0) {
        std::free(block);
        b.used -= held;
        return nullptr;
    }
    if (newSize > held && b.used - held + newSize > b.limit)
        return nullptr;
    void *grown = std::realloc(block, newSize);
    if (grown)
        b.used = b.used - held + newSize;
    return grown;
}

std::filesystem::path fromUtf8(const std::string &text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::expected<std::string, std::string> readFile(const std::filesystem::path &path, std::size_t limit,
                                                 const std::string &shown)
{
    std::error_code error;
    if (std::filesystem::is_symlink(path, error) || !std::filesystem::is_regular_file(path, error))
        return std::unexpected(shown + " isn't a file in the plugin");
    if (std::filesystem::file_size(path, error) > limit)
        return std::unexpected(shown + " is too large");
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof())
        return std::unexpected("can't read " + shown);
    return text;
}

constexpr char kBytecodeMark = '\x1b';

int loadSource(lua_State *L, const std::string &source, const std::string &shown)
{
    if (!source.empty() && source.front() == kBytecodeMark) {
        const std::string message = shown + " is compiled bytecode; plugins load Lua source only";
        lua_pushlstring(L, message.data(), message.size());
        return LUA_ERRSYNTAX;
    }
    const std::string chunk = "@" + shown;
    return luaL_loadbufferx(L, source.data(), source.size(), chunk.c_str(), "t");
}

bool isModuleName(std::string_view name)
{
    if (name.empty() || name.size() > 200 || name.front() == '.' || name.back() == '.')
        return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                          c == '-';
        if (!word && !(c == '.' && name[i + 1] != '.'))
            return false;
    }
    return true;
}

std::string text(lua_State *L, int index)
{
    std::size_t size = 0;
    const char *data = lua_tolstring(L, index, &size);
    return data ? std::string(data, size) : std::string();
}

[[noreturn]] void raise(lua_State *L, const std::string &message)
{
    lua_pushlstring(L, message.data(), message.size());
    lua_error(L);
    std::abort();
}

std::string inQuotes(std::string_view name)
{
    return "\xE2\x80\x9C" + std::string(name) + "\xE2\x80\x9D";
}

std::expected<Json, std::string> toJson(lua_State *L, int index, int depth);

std::expected<Json, std::string> tableToJson(lua_State *L, int index, int depth)
{
    bool marked = false;
    if (lua_getmetatable(L, index)) {
        luaL_getmetatable(L, kArrayKey);
        marked = lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
    }
    std::size_t count = 0;
    bool sequence = true;
    std::vector<std::string> keys;
    lua_pushnil(L);
    while (lua_next(L, index)) {
        lua_pop(L, 1);
        ++count;
        if (lua_type(L, -1) == LUA_TSTRING) {
            sequence = false;
            keys.push_back(text(L, -1));
        } else if (lua_type(L, -1) == LUA_TNUMBER) {
            const double n = lua_tonumber(L, -1);
            if (n < 1 || n != std::floor(n)) {
                lua_pop(L, 1);
                return std::unexpected("a table sent to HikariSub has a key that is neither text nor 1, 2, 3…");
            }
            keys.emplace_back();
        } else {
            lua_pop(L, 1);
            return std::unexpected("a table sent to HikariSub has a key that is neither text nor 1, 2, 3…");
        }
    }
    const std::size_t length = lua_objlen(L, index);
    if (marked && !sequence)
        return std::unexpected("an array sent to HikariSub has text keys");
    if (count > 0 && sequence) {
        if (count != length)
            return std::unexpected("an array sent to HikariSub has holes");
        Json out = Json::array();
        for (std::size_t i = 1; i <= length; ++i) {
            lua_rawgeti(L, index, static_cast<int>(i));
            auto item = toJson(L, lua_gettop(L), depth + 1);
            lua_pop(L, 1);
            if (!item)
                return item;
            out.push(std::move(*item));
        }
        return out;
    }
    if (marked)
        return Json::array();
    if (std::ranges::any_of(keys, [](const std::string &k) { return k.empty(); }))
        return std::unexpected("a table sent to HikariSub mixes text keys with 1, 2, 3…");
    std::ranges::sort(keys);
    Json out = Json::object();
    for (const auto &key : keys) {
        if (!api::isUtf8(key))
            return std::unexpected("text sent to HikariSub must be UTF-8");
        lua_pushlstring(L, key.data(), key.size());
        lua_rawget(L, index);
        auto value = toJson(L, lua_gettop(L), depth + 1);
        lua_pop(L, 1);
        if (!value)
            return value;
        out[key] = std::move(*value);
    }
    return out;
}

std::expected<Json, std::string> toJson(lua_State *L, int index, int depth)
{
    if (depth > kMaxDepth)
        return std::unexpected("a table sent to HikariSub is nested too deeply, or holds itself");
    switch (lua_type(L, index)) {
    case LUA_TNONE:
    case LUA_TNIL:
        return Json();
    case LUA_TBOOLEAN:
        return Json(lua_toboolean(L, index) != 0);
    case LUA_TNUMBER: {
        const double n = lua_tonumber(L, index);
        if (!std::isfinite(n))
            return std::unexpected("numbers sent to HikariSub must be finite");
        if (n == std::floor(n) && std::fabs(n) < kMaxExactInteger)
            return Json(static_cast<std::int64_t>(n));
        return Json(n);
    }
    case LUA_TSTRING: {
        std::string value = text(L, index);
        if (!api::isUtf8(value))
            return std::unexpected("text sent to HikariSub must be UTF-8");
        return Json(std::move(value));
    }
    case LUA_TLIGHTUSERDATA:
        if (lua_touserdata(L, index) == &kNull)
            return Json();
        break;
    case LUA_TTABLE:
        return tableToJson(L, index, depth);
    default:
        break;
    }
    return std::unexpected(std::string("HikariSub can't take a ") + luaL_typename(L, index));
}

void pushJson(lua_State *L, const Json &value)
{
    luaL_checkstack(L, 4, "nested too deeply");
    switch (value.type()) {
    case Json::Type::Null:
        lua_pushlightuserdata(L, const_cast<char *>(&kNull));
        return;
    case Json::Type::Bool:
        lua_pushboolean(L, value.asBool());
        return;
    case Json::Type::Int:
    case Json::Type::Double:
        lua_pushnumber(L, value.asNumber());
        return;
    case Json::Type::String:
        lua_pushlstring(L, value.asString().data(), value.asString().size());
        return;
    case Json::Type::Array: {
        const auto &items = value.asArray();
        lua_createtable(L, static_cast<int>(items.size()), 0);
        for (std::size_t i = 0; i < items.size(); ++i) {
            pushJson(L, items[i]);
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }
        luaL_getmetatable(L, kArrayKey);
        lua_setmetatable(L, -2);
        return;
    }
    case Json::Type::Object:
        lua_createtable(L, 0, static_cast<int>(value.asObject().size()));
        for (const auto &member : value.asObject()) {
            lua_pushlstring(L, member.key.data(), member.key.size());
            pushJson(L, member.value);
            lua_rawset(L, -3);
        }
        return;
    }
}

int traceback(lua_State *L)
{
    const char *message = lua_tostring(L, 1);
    if (!message)
        message = lua_pushfstring(L, "(the error is a %s value)", luaL_typename(L, 1));
    luaL_traceback(L, L, message, 1);
    return 1;
}

}

struct PluginState::Access {
    static PluginState &of(lua_State *L)
    {
        return *static_cast<PluginState *>(lua_touserdata(L, lua_upvalueindex(1)));
    }
    static Host &host(lua_State *L)
    {
        PluginState &state = of(L);
        if (!state.m_host)
            raise(L, "HikariSub isn't listening to this plugin right now");
        return *state.m_host;
    }
    static Options &options(lua_State *L) { return of(L).m_options; }
    static const Json &catalog(lua_State *L) { return of(L).m_catalog; }
    static const std::filesystem::path &root(lua_State *L) { return of(L).m_options.root; }
};

namespace {

using Access = PluginState::Access;

int loadText(lua_State *L)
{
    lua_settop(L, 4);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_pushliteral(L, "t");
    lua_pushvalue(L, 4);
    lua_call(L, 4, LUA_MULTRET);
    return lua_gettop(L) - 4;
}

int require(lua_State *L)
{
    const std::string name = luaL_checkstring(L, 1);
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, kLoadedKey);
    lua_getfield(L, 2, name.c_str());
    if (lua_touserdata(L, -1) == &kLoading)
        raise(L, "module " + inQuotes(name) + " requires itself while it loads");
    if (lua_toboolean(L, -1))
        return 1;
    lua_pop(L, 1);
    if (!isModuleName(name))
        raise(L, "module " + inQuotes(name) + " isn't a module name: use words separated by dots");
    std::string relative = "lua/" + name;
    std::ranges::replace(relative, '.', '/');
    std::filesystem::path base = Access::root(L) / fromUtf8(relative);
    std::string shown = relative + ".lua";
    std::filesystem::path path = base;
    path += ".lua";
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        path = base / "init.lua";
        shown = relative + "/init.lua";
        if (!std::filesystem::exists(path, error))
            raise(L, "module " + inQuotes(name) + " isn't in the plugin's lua folder");
    }
    auto source = readFile(path, kMaxSourceBytes, shown);
    if (!source)
        raise(L, source.error());
    if (loadSource(L, *source, shown) != 0)
        return lua_error(L);
    lua_pushlightuserdata(L, const_cast<char *>(&kLoading));
    lua_setfield(L, 2, name.c_str());
    lua_pushstring(L, name.c_str());
    if (lua_pcall(L, 1, 1, 0) != 0) {
        lua_pushnil(L);
        lua_setfield(L, 2, name.c_str());
        return lua_error(L);
    }
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, 2, name.c_str());
    return 1;
}

int print(lua_State *L)
{
    std::string line;
    const int count = lua_gettop(L);
    lua_getglobal(L, "tostring");
    for (int i = 1; i <= count; ++i) {
        lua_pushvalue(L, -1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        if (i > 1)
            line += '\t';
        line += text(L, -1);
        lua_pop(L, 1);
    }
    Access::host(L).notify(Level::Info, false, line);
    return 0;
}

int notify(lua_State *L)
{
    const std::string message = luaL_checkstring(L, 1);
    static const char *const levels[] = {"info", "warning", "error", nullptr};
    const int level = luaL_checkoption(L, 2, "info", levels);
    Access::host(L).notify(static_cast<Level>(level), true, message);
    return 0;
}

int pushCallResult(lua_State *L, const std::expected<Json, Json> &result)
{
    if (result) {
        pushJson(L, *result);
        return 1;
    }
    lua_pushnil(L);
    pushJson(L, result.error());
    return 2;
}

int call(lua_State *L)
{
    const std::string operation = luaL_checkstring(L, 1);
    auto params = toJson(L, 2, 0);
    if (!params)
        raise(L, params.error());
    if (params->isNull())
        params = Json::object();
    return pushCallResult(L, Access::host(L).call(operation, *params));
}

int request(lua_State *L, Area area)
{
    const std::string operation = luaL_checkstring(L, 1);
    auto params = toJson(L, 2, 0);
    if (!params)
        raise(L, params.error());
    return pushCallResult(L, Access::host(L).request(area, operation, *params));
}

int uiCall(lua_State *L)
{
    return request(L, Area::Ui);
}

int filesCall(lua_State *L)
{
    return request(L, Area::Files);
}

int netCall(lua_State *L)
{
    return request(L, Area::Net);
}

void addHandler(lua_State *L, const std::string &name, int function)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kEventsKey);
    lua_getfield(L, -1, name.c_str());
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, name.c_str());
    }
    lua_pushvalue(L, function);
    lua_rawseti(L, -2, static_cast<int>(lua_objlen(L, -2) + 1));
    lua_pop(L, 2);
}

int listen(lua_State *L)
{
    const std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    addHandler(L, name, 2);
    return 0;
}

int lock(lua_State *L)
{
    const auto held = Access::host(L).lock();
    if (held)
        return 0;
    lua_pushlstring(L, held.error().data(), held.error().size());
    return 1;
}

int unlock(lua_State *L)
{
    Access::host(L).unlock();
    return 0;
}

int runCommand(lua_State *L)
{
    const std::string command = luaL_checkstring(L, 1);
    return pushCallResult(L, Access::host(L).call("commands.run", Json::object({{"command", command}})));
}

int registerCommand(lua_State *L)
{
    const std::string id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    const auto &declared = Access::options(L).commands;
    if (std::ranges::find(declared, id) == declared.end())
        raise(L, inQuotes(id) + " isn't a command in hikari-plugin.json");
    lua_getfield(L, LUA_REGISTRYINDEX, kCommandsKey);
    lua_getfield(L, -1, id.c_str());
    if (!lua_isnil(L, -1))
        raise(L, "command " + inQuotes(id) + " already has a handler");
    lua_pop(L, 1);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, id.c_str());
    return 0;
}

int onEvent(lua_State *L)
{
    const std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_getfield(L, LUA_REGISTRYINDEX, kEventsKey);
    lua_getfield(L, -1, name.c_str());
    const bool first = lua_isnil(L, -1);
    lua_pop(L, 2);
    if (first)
        if (auto subscribed = Access::host(L).subscribe(name); !subscribed)
            raise(L, subscribed.error());
    addHandler(L, name, 2);
    return 0;
}

const Json &declaredSetting(lua_State *L, const std::string &id)
{
    const Json *value = Access::options(L).settings.find(id);
    if (!value)
        raise(L, inQuotes(id) + " isn't a setting in hikari-plugin.json");
    return *value;
}

int getSetting(lua_State *L)
{
    pushJson(L, declaredSetting(L, luaL_checkstring(L, 1)));
    return 1;
}

int setSetting(lua_State *L)
{
    const std::string id = luaL_checkstring(L, 1);
    declaredSetting(L, id);
    auto value = toJson(L, 2, 0);
    if (!value)
        raise(L, value.error());
    if (auto saved = Access::host(L).setSetting(id, *value); !saved)
        raise(L, saved.error());
    Access::options(L).settings[id] = std::move(*value);
    return 0;
}

int catalogText(lua_State *L)
{
    const std::string key = luaL_checkstring(L, 1);
    const Json *found = Access::catalog(L).find(key);
    const std::string pattern = found ? found->asString() : key;
    std::string out;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char next = i + 1 < pattern.size() ? pattern[i + 1] : '\0';
        if (pattern[i] == '%' && next >= '1' && next <= '9' && next - '0' + 1 <= lua_gettop(L)) {
            out += luaL_checkstring(L, next - '0' + 1);
            ++i;
        } else {
            out += pattern[i];
        }
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int markArray(lua_State *L)
{
    if (lua_isnoneornil(L, 1))
        lua_newtable(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
    luaL_getmetatable(L, kArrayKey);
    lua_setmetatable(L, 1);
    return 1;
}

void setFunction(lua_State *L, PluginState *state, const char *name, lua_CFunction function)
{
    lua_pushlightuserdata(L, state);
    lua_pushcclosure(L, function, 1);
    lua_setfield(L, -2, name);
}

void openRestricted(lua_State *L)
{
    const luaL_Reg libraries[] = {{"", luaopen_base},
                                  {LUA_STRLIBNAME, luaopen_string},
                                  {LUA_TABLIBNAME, luaopen_table},
                                  {LUA_MATHLIBNAME, luaopen_math},
                                  {LUA_BITLIBNAME, luaopen_bit}};
    for (const auto &library : libraries) {
        lua_pushcfunction(L, library.func);
        lua_pushstring(L, library.name);
        lua_call(L, 1, 0);
    }
    for (const char *name : {"dofile", "loadfile", "require", "module"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);
    lua_getglobal(L, "load");
    lua_pushvalue(L, -1);
    lua_pushcclosure(L, loadText, 1);
    lua_setglobal(L, "load");
    lua_pushcclosure(L, loadText, 1);
    lua_setglobal(L, "loadstring");
}

void openUnrestricted(lua_State *L, const std::filesystem::path &root)
{
    luaL_openlibs(L);
    const std::string base = root.generic_string();
    lua_getglobal(L, LUA_LOADLIBNAME);
    const std::string path = base + "/lua/?.lua;" + base + "/lua/?/init.lua";
    lua_pushstring(L, path.c_str());
    lua_setfield(L, -2, "path");
#ifdef _WIN32
    const std::string native = base + "/lib/?.dll";
#else
    const std::string native = base + "/lib/?.so";
#endif
    lua_pushstring(L, native.c_str());
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);
}

}

PluginState::PluginState(Options options) : m_options(std::move(options))
{
    m_budget.limit = m_options.memoryLimit;
}

PluginState::~PluginState()
{
    if (m_lua)
        lua_close(m_lua);
}

std::expected<void, std::string> PluginState::readCatalog()
{
    std::vector<std::string> names{"en"};
    const std::string &locale = m_options.locale;
    if (const auto cut = locale.find('_'); cut != std::string::npos && cut > 0)
        names.push_back(locale.substr(0, cut));
    if (!locale.empty() && locale != "en")
        names.push_back(locale);
    for (const auto &name : names) {
        const std::string shown = "i18n/" + name + ".json";
        const auto path = m_options.root / fromUtf8(shown);
        std::error_code error;
        if (!std::filesystem::exists(path, error))
            continue;
        auto text = readFile(path, kMaxCatalogBytes, shown);
        if (!text)
            return std::unexpected(text.error());
        auto catalog = Json::parse(*text);
        if (!catalog || !catalog->isObject() ||
            !std::ranges::all_of(catalog->asObject(), [](const Json::Member &m) { return m.value.isString(); }))
            return std::unexpected(shown + " must be a JSON object of strings");
        for (auto &member : catalog->asObject())
            m_catalog[member.key] = std::move(member.value);
    }
    return {};
}

std::expected<void, std::string> PluginState::open()
{
    if (auto catalog = readCatalog(); !catalog)
        return catalog;
    m_lua = lua_newstate(allocate, &m_budget);
    if (!m_lua)
        return std::unexpected("LuaJIT can't start a plugin state with a memory limit");
    lua_State *L = m_lua;
    if (m_options.tier == Tier::Restricted)
        openRestricted(L);
    else
        openUnrestricted(L, m_options.root);

    luaL_newmetatable(L, kArrayKey);
    lua_pop(L, 1);
    for (const char *key : {kCommandsKey, kEventsKey, kLoadedKey}) {
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, key);
    }

    lua_newtable(L);
    lua_pushinteger(L, api::kApiVersion);
    lua_setfield(L, -2, "api_version");
    lua_pushlightuserdata(L, const_cast<char *>(&kNull));
    lua_setfield(L, -2, "null");
    setFunction(L, this, "array", markArray);
    setFunction(L, this, "call", call);
    setFunction(L, this, "notify", notify);

    lua_newtable(L);
    lua_pushstring(L, m_options.id.c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, m_options.version.c_str());
    lua_setfield(L, -2, "version");
    lua_setfield(L, -2, "plugin");

    lua_newtable(L);
    setFunction(L, this, "register", registerCommand);
    setFunction(L, this, "run", runCommand);
    lua_setfield(L, -2, "commands");

    lua_newtable(L);
    setFunction(L, this, "on", onEvent);
    lua_setfield(L, -2, "events");

    lua_newtable(L);
    setFunction(L, this, "get", getSetting);
    setFunction(L, this, "set", setSetting);
    lua_setfield(L, -2, "settings");

    lua_newtable(L);
    lua_pushstring(L, m_options.locale.empty() ? "en" : m_options.locale.c_str());
    lua_setfield(L, -2, "locale");
    setFunction(L, this, "t", catalogText);
    lua_setfield(L, -2, "i18n");

    lua_setglobal(L, "hikari");
    if (auto prelude = runPrelude(); !prelude)
        return prelude;

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, print, 1);
    lua_setglobal(L, "print");
    if (m_options.tier == Tier::Restricted) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, require, 1);
        lua_setglobal(L, "require");
    }
    return {};
}

std::expected<void, std::string> PluginState::runPrelude()
{
    lua_State *L = m_lua;
    const std::pair<std::string_view, std::array<lua_CFunction, 2>> preludes[] = {
        {documentPrelude(), {lock, unlock}},
        {uiPrelude(), {uiCall, listen}},
        {filesPrelude(), {filesCall, netCall}},
    };
    for (const auto &[source, functions] : preludes) {
        if (luaL_loadbufferx(L, source.data(), source.size(), "=hikari", "t") != 0) {
            std::string message = text(L, -1);
            lua_pop(L, 1);
            return std::unexpected(std::move(message));
        }
        lua_getglobal(L, "hikari");
        for (const lua_CFunction function : functions) {
            lua_pushlightuserdata(L, this);
            lua_pushcclosure(L, function, 1);
        }
        if (lua_pcall(L, 3, 0, 0) != 0) {
            std::string message = text(L, -1);
            lua_pop(L, 1);
            return std::unexpected(std::move(message));
        }
    }
    return {};
}

std::expected<void, std::string> PluginState::protectedCall(int arguments, Host &host)
{
    lua_State *L = m_lua;
    const int base = lua_gettop(L) - arguments;
    lua_pushcfunction(L, traceback);
    lua_insert(L, base);
    m_host = &host;
    const int status = lua_pcall(L, arguments, 0, base);
    m_host = nullptr;
    std::string message;
    if (status == LUA_ERRMEM)
        message = "the plugin ran out of memory: it may hold " + std::to_string(m_budget.limit >> 20) + " MB";
    else if (status != 0)
        message = text(L, -1);
    lua_settop(L, base - 1);
    if (status != 0) {
        lua_gc(L, LUA_GCCOLLECT, 0);
        return std::unexpected(message);
    }
    return {};
}

std::expected<std::unique_ptr<PluginState>, std::string> PluginState::load(Options options, Host &host)
{
    std::unique_ptr<PluginState> state(new PluginState(std::move(options)));
    if (auto opened = state->open(); !opened)
        return std::unexpected(opened.error());
    const auto &main = state->m_options.main;
    auto source = readFile(state->m_options.root / fromUtf8(main), kMaxSourceBytes, main);
    if (!source)
        return std::unexpected(source.error());
    lua_State *L = state->m_lua;
    if (const int status = loadSource(L, *source, main); status != 0) {
        std::string message = status == LUA_ERRMEM ? "the plugin ran out of memory" : text(L, -1);
        return std::unexpected(std::move(message));
    }
    if (auto ran = state->protectedCall(0, host); !ran)
        return std::unexpected(ran.error());
    lua_gc(L, LUA_GCCOLLECT, 0);
    return state;
}

std::vector<std::string> PluginState::handledCommands() const
{
    std::vector<std::string> out;
    lua_getfield(m_lua, LUA_REGISTRYINDEX, kCommandsKey);
    for (const auto &id : m_options.commands) {
        lua_getfield(m_lua, -1, id.c_str());
        if (!lua_isnil(m_lua, -1))
            out.push_back(id);
        lua_pop(m_lua, 1);
    }
    lua_pop(m_lua, 1);
    return out;
}

std::expected<void, std::string> PluginState::run(const std::string &command, Host &host)
{
    lua_State *L = m_lua;
    lua_getfield(L, LUA_REGISTRYINDEX, kCommandsKey);
    lua_getfield(L, -1, command.c_str());
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return std::unexpected("command " + inQuotes(command) + " has no handler: the plugin must call "
                               "hikari.commands.register for it");
    }
    return protectedCall(0, host);
}

std::expected<void, std::string> PluginState::dispatch(const std::string &event, const Json &data, Host &host)
{
    lua_State *L = m_lua;
    if (event == "settings:changed") {
        const Json *id = data.find("id");
        const Json *value = data.find("value");
        Json &settings = m_options.settings;
        if (id && id->isString() && value && settings.find(id->asString()))
            settings[id->asString()] = *value;
        return {};
    }
    lua_getfield(L, LUA_REGISTRYINDEX, kEventsKey);
    lua_getfield(L, -1, event.c_str());
    lua_remove(L, -2);
    std::expected<void, std::string> first;
    if (lua_istable(L, -1)) {
        const int handlers = lua_gettop(L);
        const auto count = static_cast<int>(lua_objlen(L, handlers));
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, handlers, i);
            pushJson(L, data);
            if (auto ran = protectedCall(1, host); !ran && first)
                first = std::unexpected(ran.error());
        }
    }
    lua_pop(L, 1);
    return first;
}

}
