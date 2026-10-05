// The isolated Lua automation helper (N8; docs/qt/automation.md, ADR 0006).
// One process per loaded script; the script's LuaJIT state lives for the
// process. The Lua-facing API stays synchronous: a dialog blocks the script
// in a host service call while the application keeps running.
//
// Legacy behaviour reproduced here (HikariSub/Automation*.cpp at 20d647c4):
// chunk named by the file path with a UTF-8 BOM skipped; dofile/loadfile
// removed and `include` searching the script directory then the shared
// include directory; package.path built from both; script_* globals read
// with string coercion; register_macro's description read from the top of
// the stack (so it is empty whenever a function is the last argument);
// register_filter a stub; aegisub.progress/debug/log/dialog installed per
// run, with progress and debug removed afterwards; dialog arguments decoded
// with the legacy field coercions, button IDs truncated, and results typed
// per control.
//
// Host services (L3) keep the legacy argument handling and return shapes and
// are answered by the application's platform ports, also while the script's
// top level runs.
//
// Not yet here, owned by the A33 automation cards: the native preloads
// (lpeg, luabins, re/unicode/lfs) and MoonScript.

#include "hikari/backends/helper_endpoint.h"
#include "hikari/backends/lua_protocol.h"
#include "hikari/core/text_projection.h"
#include "lua_native.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <filesystem>
#include <regex>
#include <type_traits>
#include <variant>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace hikari::backends::helper;
namespace lua = hikari::backends::lua;
using hikari::application::DialogControl;
using hikari::application::DialogRequest;
using hikari::application::DialogResult;
using hikari::application::ScriptInfo;
using hikari::application::ScriptMacro;

namespace {

struct Script {
    lua_State *L = nullptr;
    std::filesystem::path path;
    std::vector<std::string> includePath; // the script's directory, then the shared one
    int traceLevel = 3;
    ScriptInfo info;
    std::vector<int> features; // registry refs of {run, validate, isactive}
};

Script g_script;
Responder *g_responder = nullptr; // the running request
Responder *g_services = nullptr;  // host services: the running request, or the Load while the top level runs
int g_lastProgress = 0;
char g_cancelTag; // its address identifies aegisub.cancel()

std::string toUtf8(const std::filesystem::path &p)
{
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::filesystem::path fromUtf8(const std::string &s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string prettyName(const std::filesystem::path &p)
{
    return toUtf8(p.filename());
}

bool readFile(const std::filesystem::path &path, std::string &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return !in.bad();
}

// Legacy LoadFile: the whole file, a UTF-8 BOM skipped, the path as chunk name.
bool loadFile(lua_State *L, const std::filesystem::path &path)
{
    std::string text;
    if (!readFile(path, text)) {
        lua_pushfstring(L, "cannot read %s", toUtf8(path).c_str());
        return false;
    }
    const char *data = text.data();
    std::size_t size = text.size();
    if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xEF && static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        data += 3;
        size -= 3;
    }
    const std::string name = toUtf8(path);
    if (!name.ends_with("moon"))
        return luaL_loadbuffer(L, data, size, name.c_str()) == 0;
    // MoonScript: compiled by moonscript.loadstring (installed at load). The
    // raw text is kept for mapping error lines back to the .moon source.
    lua_getfield(L, LUA_REGISTRYINDEX, "moonscript");
    lua_pushlstring(L, data, size);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, ("raw moonscript: " + name).c_str());
    lua_pushstring(L, name.c_str());
    if (lua_pcall(L, 2, 2, 0))
        return false; // leaves the error message
    // loadstring returns nil, error on error, or the function.
    if (lua_isnil(L, -2)) {
        lua_remove(L, -2);
        return false;
    }
    lua_pop(L, 1);
    return true;
}

std::string stringOrEmpty(lua_State *L, int index)
{
    return lua_isstring(L, index) ? lua_tostring(L, index) : std::string();
}

std::string globalString(lua_State *L, const char *name)
{
    lua_getglobal(L, name);
    std::string value = stringOrEmpty(L, -1);
    lua_pop(L, 1);
    return value;
}

// Legacy check_string: a string or number, else a type error.
std::string checkString(lua_State *L, int index)
{
    std::size_t len = 0;
    const char *s = lua_tolstring(L, index, &len);
    if (!s)
        luaL_typerror(L, index, "string");
    return std::string(s, len);
}

// Legacy moon_line: a .moon file's Lua line mapped through MoonScript's line
// tables to a character offset, then to a line of the raw source.
int moonLine(lua_State *L, int luaLine, const std::string &file)
{
    if (luaL_dostring(L, "return require 'moonscript.line_tables'")) {
        lua_pop(L, 1);
        return luaLine;
    }
    lua_pushstring(L, file.c_str());
    lua_rawget(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return luaLine;
    }
    lua_rawgeti(L, -1, luaLine);
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 3);
        return luaLine;
    }
    const auto charPos = static_cast<std::size_t>(lua_tonumber(L, -1));
    lua_pop(L, 3);
    lua_getfield(L, LUA_REGISTRYINDEX, ("raw moonscript: " + file).c_str());
    if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        return luaLine;
    }
    std::size_t len = 0;
    const char *moon = lua_tolstring(L, -1, &len);
    const int line = static_cast<int>(std::count(moon, moon + std::min(len, charPos), '\n')) + 1;
    lua_pop(L, 1);
    return line;
}

// Legacy add_stack_trace: one "File ..., line ..." block per frame, each
// followed by the message with its "[string ...]:N: " location removed.
int addStackTrace(lua_State *L)
{
    if (lua_touserdata(L, 1) == &g_cancelTag)
        return 1; // cancellation is not an error to annotate
    int level = 1;
    if (lua_isnumber(L, 2)) {
        level = static_cast<int>(lua_tointeger(L, 2));
        lua_pop(L, 1);
    }
    const char *err = lua_tostring(L, 1);
    if (!err)
        return 1;
    std::string message = err;
    if (lua_gettop(L))
        lua_pop(L, 1);
    static const std::regex location("^\\[string (.*)\\]:[0-9]+: ");
    message = std::regex_replace(message, location, "", std::regex_constants::format_first_only);

    std::string frames;
    lua_Debug ar;
    while (lua_getstack(L, level++, &ar)) {
        lua_getinfo(L, "Snl", &ar);
        if (ar.what[0] == 't') {
            frames += "(tail call)";
            continue;
        }
        std::string file = ar.source ? ar.source : "";
        const bool moon = file != "=[C]" && file.ends_with(".moon");
        if (file == "=[C]")
            file = "<C function>";
        const auto realLine = [&](int line) { return moon ? moonLine(L, line, file) : line; };
        std::string function = ar.name ? ar.name : "";
        if (*ar.what == 'm')
            function += " <main>";
        else if (*ar.what == 'C')
            function += '?';
        else if (!*ar.namewhat)
            function += " <anonymous function at lines " + std::to_string(realLine(ar.linedefined)) + "-" +
                        std::to_string(realLine(ar.lastlinedefined - 1)) + ">";
        frames += "File \"" + file + "\", line " + std::to_string(realLine(ar.currentline)) + " " + function + "\n" +
                  message + "\n\n";
    }
    lua_pushstring(L, frames.c_str());
    return 1;
}

void send(lua::ProgressEvent event, const std::string &text)
{
    if (!g_responder)
        return;
    Writer w;
    w.i32(static_cast<std::int32_t>(event)).str(text);
    g_responder->progress(w.take());
}

// ---- aegisub table -------------------------------------------------------

int registerMacro(lua_State *L)
{
    ScriptMacro macro;
    macro.name = checkString(L, 1);
    macro.description = stringOrEmpty(L, -1); // legacy get_string(L, 2) reads index -1
    if (!lua_isfunction(L, 3))
        return luaL_error(L, "The macro processing function must be a function");
    macro.hasValidate = lua_isfunction(L, 4);
    macro.hasIsActive = lua_isfunction(L, 5);
    for (const auto &m : g_script.info.macros)
        if (m.name == macro.name)
            return luaL_error(L, "Macro named '%s' is already defined in the script '%s'", macro.name.c_str(),
                              prettyName(g_script.path).c_str());
    lua_createtable(L, 0, 3);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, "run");
    lua_pushvalue(L, 4);
    lua_setfield(L, -2, "validate");
    lua_pushvalue(L, 5);
    lua_setfield(L, -2, "isactive");
    g_script.features.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    g_script.info.macros.push_back(std::move(macro));
    return 0;
}

int registerFilter(lua_State *)
{
    return 0; // legacy stub: HikariSub has no export filters
}

int cancelScript(lua_State *L)
{
    lua_pushlightuserdata(L, &g_cancelTag);
    return lua_error(L);
}

int gettext(lua_State *L); // O5: through the host's catalog (below, with the host services)

int include(lua_State *L)
{
    const std::string filename = checkString(L, 1);
    std::filesystem::path filepath;
    const bool fullpath = filename.find_first_of("\\/") != std::string::npos;
    if (fullpath)
        filepath = fromUtf8(filename);
    if (!std::filesystem::exists(filepath)) {
        for (const auto &dir : g_script.includePath) {
            filepath = fromUtf8(dir) / fromUtf8(filename);
            if (std::filesystem::exists(filepath))
                break;
        }
    }
    if (!std::filesystem::exists(filepath)) {
        lua_pushfstring(L, "Lua include not found: %s", toUtf8(filepath).c_str());
        return lua_error(L);
    }
    if (!loadFile(L, filepath)) {
        // Legacy appends check_string(L, 1), the include's own name, not the error.
        lua_pushfstring(L, "Error loading Lua include \"%s\":\n%s", toUtf8(filepath).c_str(), filename.c_str());
        return lua_error(L);
    }
    const int before = lua_gettop(L) - 1;
    lua_call(L, 0, LUA_MULTRET);
    return lua_gettop(L) - before;
}

// ---- per-run sink (legacy LuaProgressSink) --------------------------------

int progressSet(lua_State *L)
{
    const int progress = static_cast<int>(lua_tonumber(L, 1));
    if (g_lastProgress < progress && g_responder) {
        Writer w;
        w.i32(static_cast<std::int32_t>(lua::ProgressEvent::Set)).f64(progress);
        g_responder->progress(w.take());
    }
    g_lastProgress = progress;
    return 0;
}

int progressTask(lua_State *L)
{
    send(lua::ProgressEvent::Task, stringOrEmpty(L, 1));
    return 0;
}

int progressTitle(lua_State *L)
{
    send(lua::ProgressEvent::Title, stringOrEmpty(L, 1));
    return 0;
}

int progressCancelled(lua_State *L)
{
    lua_pushboolean(L, !g_responder || g_responder->cancelled());
    return 1;
}

int debugOut(lua_State *L)
{
    if (lua_isnumber(L, 1)) {
        if (lua_tointeger(L, 1) > g_script.traceLevel)
            return 0;
        lua_remove(L, 1);
    }
    if (lua_gettop(L) > 1) {
        lua_getglobal(L, "string");
        lua_getfield(L, -1, "format");
        lua_remove(L, -2);
        lua_insert(L, 1);
        lua_call(L, lua_gettop(L) - 1, 1);
    }
    send(lua::ProgressEvent::Log, stringOrEmpty(L, 1));
    return 0;
}

// Legacy get_field coercions: a field keeps its default unless it has the
// right Lua type (numbers accept numeric strings; strings accept numbers).
std::string fieldString(lua_State *L, const char *name, std::string def = {})
{
    lua_getfield(L, -1, name);
    if (lua_isstring(L, -1))
        def = lua_tostring(L, -1);
    lua_pop(L, 1);
    return def;
}
int fieldInt(lua_State *L, const char *name, int def)
{
    lua_getfield(L, -1, name);
    if (lua_isnumber(L, -1))
        def = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return def;
}
double fieldNumber(lua_State *L, const char *name, double def)
{
    lua_getfield(L, -1, name);
    if (lua_isnumber(L, -1))
        def = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return def;
}
bool fieldBool(lua_State *L, const char *name, bool def)
{
    lua_getfield(L, -1, name);
    if (lua_isboolean(L, -1))
        def = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return def;
}

// Legacy AssColor (HikariSub/styles.cpp at 20d647c4): SetAss parses a
// decimal SSA number, an ASS &HAABBGGRR& string or an HTML #AARRGGBB/#RRGGBB
// string; GetHex prints #RRGGBB, or #AARRGGBB (AA = ASS transparency) when
// alpha is wanted and non-zero. Its quirks are kept: Upper()'s result is
// discarded, so a lowercase "&h" is not stripped, and a malformed component
// stays 0.
struct AssColor {
    long r = 0, g = 0, b = 0, a = 0;
};

bool parseLong(const std::string &text, int base, long &out)
{
    if (text.empty())
        return false;
    char *end = nullptr;
    const long v = std::strtol(text.c_str(), &end, base);
    if (*end != '\0')
        return false;
    out = v;
    return true;
}

// wxString::SubString(from, to): inclusive, clamped to the string.
std::string subString(const std::string &s, std::size_t from, std::size_t to)
{
    if (from >= s.size())
        return {};
    return s.substr(from, std::min(to, s.size() - 1) - from + 1);
}

AssColor assColor(std::string color)
{
    AssColor c;
    const bool number = !color.empty() &&
                        std::all_of(color.begin(), color.end(), [](unsigned char ch) { return std::isdigit(ch); });
    if (number) {
        long v = 0;
        parseLong(color, 10, v);
        c.r = v & 0xff;
        c.g = (v >> 8) & 0xff;
        c.b = (v >> 16) & 0xff;
        c.a = (v >> 24) & 0xff;
        return c;
    }
    const bool html = color.rfind('#', 0) == 0;
    std::erase(color, '&');
    std::erase(color, 'H');
    std::erase(color, '#');
    if (color.size() > 7) {
        parseLong(subString(color, 0, 1), 16, c.a);
        color = color.substr(2);
    }
    std::string r = subString(color, 4, 5), g = subString(color, 2, 3), b = subString(color, 0, 1);
    if (html)
        std::swap(r, b);
    parseLong(r, 16, c.r);
    parseLong(g, 16, c.g);
    parseLong(b, 16, c.b);
    return c;
}

std::string hexOf(const AssColor &c, bool alpha)
{
    char text[16];
    if (alpha && c.a)
        std::snprintf(text, sizeof text, "#%02lX%02lX%02lX%02lX", c.a, c.r, c.g, c.b);
    else
        std::snprintf(text, sizeof text, "#%02lX%02lX%02lX", c.r, c.g, c.b);
    return text;
}

DialogControl decodeControl(lua_State *L)
{
    if (!lua_istable(L, -1))
        luaL_error(L, "bad control table entry");
    DialogControl c;
    c.kind = fieldString(L, "class");
    std::transform(c.kind.begin(), c.kind.end(), c.kind.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    static const char *const kKinds[] = {"label",    "edit",     "intedit", "floatedit",  "textbox",
                                         "dropdown", "checkbox", "color",   "coloralpha", "alpha"};
    if (std::find_if(std::begin(kKinds), std::end(kKinds), [&](const char *k) { return c.kind == k; }) ==
        std::end(kKinds))
        luaL_error(L, "bad control table entry");
    c.name = fieldString(L, "name");
    c.hint = fieldString(L, "hint");
    c.x = fieldInt(L, "x", 0);
    c.y = fieldInt(L, "y", 0);
    c.width = fieldInt(L, "width", 1);
    c.height = fieldInt(L, "height", 1);
    if (c.kind == "label" || c.kind == "checkbox")
        c.label = fieldString(L, "label");
    if (c.kind == "edit" || c.kind == "textbox" || c.kind == "alpha") {
        c.text = fieldString(L, "value");
        c.text = fieldString(L, "text", c.text); // undocumented legacy alias
    } else if (c.kind == "dropdown") {
        c.text = fieldString(L, "value");
    } else if (c.kind == "color" || c.kind == "coloralpha") {
        // The picker starts from the parsed colour; an unchanged dialog
        // returns it normalized, as legacy did.
        c.text = hexOf(assColor(fieldString(L, "value")), c.kind == "coloralpha");
    }
    if (c.kind == "intedit") {
        c.intValue = fieldInt(L, "value", 0);
        c.intMin = fieldInt(L, "min", INT_MIN);
        c.intMax = fieldInt(L, "max", INT_MAX);
        if (c.intMin >= c.intMax) {
            c.intMin = INT_MIN;
            c.intMax = INT_MAX;
        }
        c.intValue = std::clamp(c.intValue, c.intMin, c.intMax); // NumCtrl clamps what it shows
    } else if (c.kind == "floatedit") {
        c.number = fieldNumber(L, "value", 0.0);
        c.numberMin = fieldNumber(L, "min", -DBL_MAX);
        c.numberMax = fieldNumber(L, "max", DBL_MAX);
        c.step = fieldNumber(L, "step", 0.0);
        if (c.numberMin >= c.numberMax) {
            c.numberMin = -DBL_MAX;
            c.numberMax = DBL_MAX;
        }
        c.number = std::clamp(c.number, c.numberMin, c.numberMax);
    } else if (c.kind == "checkbox") {
        c.checked = fieldBool(L, "value", false);
    } else if (c.kind == "dropdown") {
        lua_getfield(L, -1, "items");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_isstring(L, -1))
                    c.items.emplace_back(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    return c;
}

void pushValue(lua_State *L, const hikari::application::DialogValue &v)
{
    if (const auto *s = std::get_if<std::string>(&v))
        lua_pushlstring(L, s->data(), s->size());
    else if (const auto *i = std::get_if<int>(&v))
        lua_pushinteger(L, *i);
    else if (const auto *d = std::get_if<double>(&v))
        lua_pushnumber(L, *d);
    else if (const auto *b = std::get_if<bool>(&v))
        lua_pushboolean(L, *b);
    else
        lua_pushnil(L);
}

int dialogDisplay(lua_State *L)
{
    if (!g_responder)
        return luaL_error(L, "no macro is running");
    // Legacy: one argument gets an empty button table; extra arguments,
    // including the button-ID table, are dropped before decoding.
    if (lua_gettop(L) == 1)
        lua_newtable(L);
    if (lua_gettop(L) > 2)
        lua_settop(L, 2);
    if (!lua_istable(L, 1))
        return luaL_error(L, "Cannot create config dialog from something non-table");

    DialogRequest request;
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        request.controls.push_back(decodeControl(L));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    if (lua_istable(L, 2)) {
        lua_pushvalue(L, 2);
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            request.buttons.push_back(checkString(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    Writer w;
    w.i32(static_cast<std::int32_t>(lua::Service::Dialog));
    auto payload = w.take();
    const auto body = lua::encodeDialogRequest(request);
    payload.insert(payload.end(), body.begin(), body.end());
    const auto answer = g_responder->call(std::move(payload));
    if (!answer) {
        if (answer.error() == Outcome::Cancelled)
            return cancelScript(L); // the run was cancelled while the dialog waited
        return luaL_error(L, "the dialog could not be shown");
    }
    auto result = lua::decodeDialogResult(*answer, request);
    if (!result)
        return luaL_error(L, "the host returned a malformed dialog result");

    // Button: false when closed or for the default Cancel; otherwise its label.
    // With the ID table dropped, a custom button never maps to Cancel.
    if (result->pressed < 0 || (request.buttons.empty() && result->pressed == 1))
        lua_pushboolean(L, false);
    else if (request.buttons.empty())
        lua_pushstring(L, "");
    else
        lua_pushstring(L, request.buttons[static_cast<std::size_t>(result->pressed)].c_str());

    lua_createtable(L, 0, static_cast<int>(request.controls.size()));
    for (std::size_t i = 0; i < request.controls.size(); ++i) {
        const auto &control = request.controls[i];
        if ((control.kind == "color" || control.kind == "coloralpha") &&
            std::holds_alternative<std::string>(result->values[i]))
            result->values[i] = hexOf(assColor(std::get<std::string>(result->values[i])), control.kind == "coloralpha");
        pushValue(L, result->values[i]); // a label sets its name to nil
        lua_setfield(L, -2, request.controls[i].name.c_str());
    }
    return 2;
}

int openDialog(lua_State *L);
int saveDialog(lua_State *L);

void installSink(lua_State *L)
{
    lua_getglobal(L, "aegisub");
    lua_createtable(L, 0, 4);
    lua_pushcfunction(L, progressSet);
    lua_setfield(L, -2, "set");
    lua_pushcfunction(L, progressTask);
    lua_setfield(L, -2, "task");
    lua_pushcfunction(L, progressTitle);
    lua_setfield(L, -2, "title");
    lua_pushcfunction(L, progressCancelled);
    lua_setfield(L, -2, "is_cancelled");
    lua_setfield(L, -2, "progress");
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, debugOut);
    lua_setfield(L, -2, "out");
    lua_setfield(L, -2, "debug");
    lua_pushcfunction(L, debugOut);
    lua_setfield(L, -2, "log");
    lua_createtable(L, 0, 3);
    lua_pushcfunction(L, dialogDisplay);
    lua_setfield(L, -2, "display");
    lua_pushcfunction(L, openDialog);
    lua_setfield(L, -2, "open");
    lua_pushcfunction(L, saveDialog);
    lua_setfield(L, -2, "save");
    lua_setfield(L, -2, "dialog");
    lua_pop(L, 1);
}

void removeSink(lua_State *L)
{
    lua_getglobal(L, "aegisub");
    lua_pushnil(L);
    lua_setfield(L, -2, "progress");
    lua_pushnil(L);
    lua_setfield(L, -2, "debug");
    lua_pop(L, 1);
}

void installHostServices(lua_State *L);

void installAegisub(lua_State *L)
{
    lua_createtable(L, 0, 24);
    lua_pushcfunction(L, registerMacro);
    lua_setfield(L, -2, "register_macro");
    lua_pushcfunction(L, registerFilter);
    lua_setfield(L, -2, "register_filter");
    lua_pushcfunction(L, cancelScript);
    lua_setfield(L, -2, "cancel");
    lua_pushcfunction(L, gettext);
    lua_setfield(L, -2, "gettext");
    lua_pushinteger(L, 4);
    lua_setfield(L, -2, "lua_automation_version");
    installHostServices(L);
    lua_setglobal(L, "aegisub");
}

// ---- requests -------------------------------------------------------------

// Legacy module_loader (package.loaders[2]): each package.path entry with
// "/?" replaced by the module path, a .moon file preferred over its .lua.
int moduleLoader(lua_State *L)
{
    const int pretop = lua_gettop(L);
    std::string module = checkString(L, -1);
    std::replace(module.begin(), module.end(), '.', LUA_DIRSEP[0]);
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    const std::string paths = checkString(L, -1);
    lua_pop(L, 2);
    std::size_t from = 0;
    while (from <= paths.size()) {
        const std::size_t to = std::min(paths.find(';', from), paths.size());
        std::string filename = paths.substr(from, to - from);
        from = to + 1;
        if (filename.empty())
            continue;
        for (std::size_t at = 0; (at = filename.find("/?", at)) != std::string::npos; at += module.size())
            filename.replace(at, 2, module);
        if (filename.ends_with("lua")) {
            const std::string moon = filename.substr(0, filename.rfind('.')) + ".moon";
            if (std::filesystem::exists(fromUtf8(moon)))
                filename = moon;
        }
        if (!std::filesystem::exists(fromUtf8(filename)))
            continue;
        if (!loadFile(L, fromUtf8(filename)))
            return luaL_error(L, "Error loading Lua module \"%s\":\n%s", filename.c_str(), checkString(L, 1).c_str());
        break;
    }
    return lua_gettop(L) - pretop;
}

// Legacy Install: package.path from the include directories, the module
// loader above, and moonscript.loadstring kept in the registry. False leaves
// the error message on the stack.
bool install(lua_State *L)
{
    lua_getglobal(L, "package");
    std::string packagePath;
    for (const auto &dir : g_script.includePath)
        packagePath += dir + "/?.lua;" + dir + "/?/init.lua;";
    lua_pushstring(L, packagePath.c_str());
    lua_setfield(L, -2, "path");
    lua_getfield(L, -1, "loaders");
    lua_pushcfunction(L, moduleLoader);
    lua_rawseti(L, -2, 2);
    lua_pop(L, 2);
    luaL_loadstring(L, "return require('moonscript').loadstring");
    if (lua_pcall(L, 0, 1, 0))
        return false;
    lua_setfield(L, LUA_REGISTRYINDEX, "moonscript");
    return true;
}

void load(Reader &in, Responder &r)
{
    const std::string path = in.str();
    const std::string sharedInclude = in.str();
    const std::int32_t traceLevel = in.i32();
    if (!in.ok() || path.empty())
        return r.terminal(Outcome::InvalidInput, bytesOf("malformed Load"));
    if (g_script.L)
        return r.terminal(Outcome::Failed, bytesOf("a script is already loaded; reload starts a new helper"));

    g_script.path = fromUtf8(path);
    g_script.traceLevel = traceLevel;
    // Legacy include directories end with a separator (wxPATH_GET_SEPARATOR).
    g_script.includePath = {toUtf8(g_script.path.parent_path()) + LUA_DIRSEP, sharedInclude + LUA_DIRSEP};
    lua_State *L = luaL_newstate();
    if (!L)
        return r.terminal(Outcome::Failed, bytesOf("Could not initialize Lua state"));
    luaL_openlibs(L);
    hikari::lua_native::preload(L);
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    lua_pushcfunction(L, include);
    lua_setglobal(L, "include");
    g_script.L = L;
    auto fail = [&](const std::string &message) {
        g_script.info = {};
        g_script.features.clear();
        lua_close(L);
        g_script.L = nullptr;
        r.terminal(Outcome::Failed, bytesOf(message));
    };
    if (!install(L))
        return fail(stringOrEmpty(L, -1));
    lua_pushstring(L, path.c_str());
    lua_setfield(L, LUA_REGISTRYINDEX, "filename");
    installAegisub(L);
    g_services = &r;
    struct EndServices {
        ~EndServices() { g_services = nullptr; }
    } endServices;

    if (!loadFile(L, g_script.path))
        return fail(stringOrEmpty(L, -1));
    lua_pushcfunction(L, addStackTrace);
    lua_insert(L, -2);
    if (lua_pcall(L, 0, 0, -2) != 0)
        return fail("Error initializing Lua script \"" + prettyName(g_script.path) + "\":\n\n" +
                    stringOrEmpty(L, -1) + ".");
    lua_pop(L, 1);
    lua_getglobal(L, "version");
    const bool automation3 = lua_isnumber(L, -1) && lua_tointeger(L, -1) == 3;
    lua_pop(L, 1);
    if (automation3)
        return fail("You are trying to load an Automation 3 script as an Automation 4 script. Automation 3 is no "
                    "longer supported.");
    g_script.info.name = globalString(L, "script_name");
    g_script.info.description = globalString(L, "script_description");
    g_script.info.author = globalString(L, "script_author");
    g_script.info.version = globalString(L, "script_version");
    if (g_script.info.name.empty())
        g_script.info.name = prettyName(g_script.path);
    lua_gc(L, LUA_GCCOLLECT, 0);
    r.terminal(Outcome::Ok, lua::encodeInfo(g_script.info));
}

// ---- the subtitles object (legacy AutoToFile, HikariSub/AutomationToFile.cpp) --
//
// A staged copy of the Document's info, style and dialogue lists. Script
// indices run 1-based across the three lists in that order. Reads, writes,
// deletes, appends and inserts follow the legacy object, including its error
// messages; nothing reaches the Document until the host applies the result.

using hikari::application::MacroDialogueLine;
using hikari::application::MacroInfoLine;
using hikari::application::MacroResult;
using hikari::application::MacroSnapshot;
using hikari::application::MacroStyleLine;

struct Staged {
    MacroSnapshot lists;
    bool canModify = true;
    int size() const
    {
        return static_cast<int>(lists.info.size() + lists.styles.size() + lists.dialogues.size());
    }
};
Staged *g_subs = nullptr; // the running macro's object
char g_subsTag;

void checkLive(lua_State *L)
{
    if (!g_subs)
        luaL_error(L, "the subtitles object is no longer valid");
    if (g_responder && g_responder->cancelled()) {
        lua_pushlightuserdata(L, &g_cancelTag); // legacy raised "cancelled"
        lua_error(L);
    }
}

void checkAllowModify(lua_State *L)
{
    if (!g_subs->canModify)
        luaL_error(L, "You cannot modify read-only subtitles");
}

std::string field(const MacroStyleLine &s, std::size_t i)
{
    return i < s.fields.size() ? s.fields[i] : std::string();
}

double toDouble(const std::string &s)
{
    return std::strtod(s.c_str(), nullptr);
}

void setString(lua_State *L, const char *name, const std::string &value)
{
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, name);
}
void setNumber(lua_State *L, const char *name, double value)
{
    lua_pushnumber(L, value);
    lua_setfield(L, -2, name);
}
void setBool(lua_State *L, const char *name, bool value)
{
    lua_pushboolean(L, value);
    lua_setfield(L, -2, name);
}

// AssColor::GetAss(alpha = true): "&HAABBGGRR&".
std::string assOf(const std::string &value)
{
    const AssColor c = assColor(value);
    char text[16];
    std::snprintf(text, sizeof text, "&H%02lX%02lX%02lX%02lX&", c.a, c.b, c.g, c.r);
    return text;
}

bool lineToLua(lua_State *L, int i)
{
    const auto &l = g_subs->lists;
    const int sinfo = static_cast<int>(l.info.size());
    const int styles = sinfo + static_cast<int>(l.styles.size());
    if (i < 0 || i >= g_subs->size())
        return false;
    lua_newtable(L);
    if (i < sinfo) {
        const auto &info = l.info[static_cast<std::size_t>(i)];
        setString(L, "section", "[Script Info]");
        setString(L, "raw", info.key + ": " + info.value);
        setString(L, "key", info.key);
        setString(L, "value", info.value);
        lua_pushstring(L, "info");
    } else if (i < styles) {
        const auto &s = l.styles[static_cast<std::size_t>(i - sinfo)];
        std::string raw = "Style: ";
        for (std::size_t k = 0; k < s.fields.size(); ++k)
            raw += (k ? "," : "") + s.fields[k];
        setString(L, "section", "[V4+ Styles]");
        setString(L, "raw", raw);
        setString(L, "name", field(s, 0));
        setString(L, "fontname", field(s, 1));
        setNumber(L, "fontsize", toDouble(field(s, 2)));
        setString(L, "color1", assOf(field(s, 3)));
        setString(L, "color2", assOf(field(s, 4)));
        setString(L, "color3", assOf(field(s, 5)));
        setString(L, "color4", assOf(field(s, 6)));
        setBool(L, "bold", std::atoi(field(s, 7).c_str()) != 0);
        setBool(L, "italic", std::atoi(field(s, 8).c_str()) != 0);
        setBool(L, "underline", std::atoi(field(s, 9).c_str()) != 0);
        setBool(L, "strikeout", std::atoi(field(s, 10).c_str()) != 0);
        setNumber(L, "scale_x", std::atoi(field(s, 11).c_str()));
        setNumber(L, "scale_y", std::atoi(field(s, 12).c_str()));
        setNumber(L, "spacing", toDouble(field(s, 13)));
        setNumber(L, "angle", toDouble(field(s, 14)));
        // Legacy keeps BorderStyle as a bool (opaque box) and pushes it as a number.
        setNumber(L, "borderstyle", std::atoi(field(s, 15).c_str()) == 3 ? 1 : 0);
        setNumber(L, "outline", toDouble(field(s, 16)));
        setNumber(L, "shadow", toDouble(field(s, 17)));
        setNumber(L, "align", std::atoi(field(s, 18).c_str()));
        setNumber(L, "margin_l", std::atoi(field(s, 19).c_str()));
        setNumber(L, "margin_r", std::atoi(field(s, 20).c_str()));
        setNumber(L, "margin_t", std::atoi(field(s, 21).c_str()));
        setNumber(L, "margin_b", std::atoi(field(s, 21).c_str()));
        setNumber(L, "encoding", std::atoi(field(s, 22).c_str()));
        setNumber(L, "relative_to", 2);
        lua_pushstring(L, "style");
    } else {
        const auto &d = l.dialogues[static_cast<std::size_t>(i - styles)];
        setString(L, "section", "[Events]");
        setString(L, "raw", d.raw);
        setBool(L, "comment", d.comment);
        setNumber(L, "layer", d.layer);
        setNumber(L, "start_time", static_cast<double>(d.startMs));
        setNumber(L, "end_time", static_cast<double>(d.endMs));
        setString(L, "style", d.style);
        setString(L, "actor", d.actor);
        setNumber(L, "margin_l", d.marginL);
        setNumber(L, "margin_r", d.marginR);
        setNumber(L, "margin_t", d.marginV);
        setNumber(L, "margin_b", d.marginV);
        setString(L, "effect", d.effect);
        setString(L, "text", d.text);
        if (!d.translation.empty())
            setString(L, "text_translation", d.translation);
        lua_newtable(L);
        lua_setfield(L, -2, "extra");
        lua_pushstring(L, "dialogue");
    }
    lua_setfield(L, -2, "class");
    return true;
}

using Entry = std::variant<MacroInfoLine, MacroStyleLine, MacroDialogueLine>;

std::string getString(lua_State *L, const char *name, const char *cls)
{
    lua_getfield(L, -1, name);
    if (!lua_isstring(L, -1))
        luaL_error(L, "Invalid string '%s' field in '%s' class subtitle line", name, cls);
    std::string v = lua_tostring(L, -1);
    lua_pop(L, 1);
    return v;
}
double getNumber(lua_State *L, const char *name, const char *cls)
{
    lua_getfield(L, -1, name);
    if (!lua_isnumber(L, -1))
        luaL_error(L, "Invalid number '%s' field in '%s' class subtitle line", name, cls);
    const double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}
bool getBool(lua_State *L, const char *name, const char *cls)
{
    lua_getfield(L, -1, name);
    if (!lua_isboolean(L, -1))
        luaL_error(L, "Invalid boolean '%s' field in '%s' class subtitle line", name, cls);
    const bool v = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return v;
}

std::string numberText(double v)
{
    char text[64];
    std::snprintf(text, sizeof text, "%g", v);
    return text;
}

// Legacy LuaToLine: the table on the stack top; std::nullopt for an unknown class.
std::optional<Entry> luaToLine(lua_State *L)
{
    if (!lua_istable(L, -1))
        luaL_error(L, "Cannot convert non table value");
    lua_getfield(L, -1, "class");
    if (!lua_isstring(L, -1))
        luaL_error(L, "Table do not have class field");
    std::string cls = lua_tostring(L, -1);
    lua_pop(L, 1);
    std::transform(cls.begin(), cls.end(), cls.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (cls == "dialogue") {
        MacroDialogueLine d;
        d.comment = getBool(L, "comment", "dialogue");
        d.layer = static_cast<int>(getNumber(L, "layer", "dialogue"));
        d.startMs = static_cast<std::int64_t>(getNumber(L, "start_time", "dialogue"));
        d.endMs = static_cast<std::int64_t>(getNumber(L, "end_time", "dialogue"));
        d.style = getString(L, "style", "dialogue");
        d.actor = getString(L, "actor", "dialogue");
        d.marginL = static_cast<int>(getNumber(L, "margin_l", "dialogue"));
        d.marginR = static_cast<int>(getNumber(L, "margin_r", "dialogue"));
        d.marginV = static_cast<int>(getNumber(L, "margin_t", "dialogue"));
        d.effect = getString(L, "effect", "dialogue");
        d.text = getString(L, "text", "dialogue");
        lua_getfield(L, -1, "text_translation");
        if (lua_isstring(L, -1))
            d.translation = lua_tostring(L, -1);
        lua_pop(L, 1);
        return d;
    }
    if (cls == "style") {
        MacroStyleLine s;
        const auto str = [&](const char *n) { return getString(L, n, "style"); };
        const auto num = [&](const char *n) { return getNumber(L, n, "style"); };
        const auto flag = [&](const char *n) { return getBool(L, n, "style") ? std::string("-1") : std::string("0"); };
        const std::string name = str("name"), font = str("fontname");
        const double size = num("fontsize");
        const std::string c1 = str("color1"), c2 = str("color2"), c3 = str("color3"), c4 = str("color4");
        const std::string bold = flag("bold"), italic = flag("italic"), underline = flag("underline"),
                          strike = flag("strikeout");
        const double sx = num("scale_x"), sy = num("scale_y"), spacing = num("spacing"), angle = num("angle");
        // Legacy reads BorderStyle back as (borderstyle == -3).
        const int border = static_cast<int>(num("borderstyle"));
        const double outline = num("outline"), shadow = num("shadow");
        const int align = static_cast<int>(num("align")), ml = static_cast<int>(num("margin_l")),
                  mr = static_cast<int>(num("margin_r")), mt = static_cast<int>(num("margin_t")),
                  mb = static_cast<int>(num("margin_b")), enc = static_cast<int>(num("encoding"));
        auto hex = [](const std::string &v) {
            const AssColor c = assColor(v);
            char text[16];
            std::snprintf(text, sizeof text, "&H%02lX%02lX%02lX%02lX", c.a, c.b, c.g, c.r);
            return std::string(text);
        };
        s.fields = {name, font, numberText(size), hex(c1), hex(c2), hex(c3), hex(c4), bold, italic, underline,
                    strike, numberText(sx), numberText(sy), numberText(spacing), numberText(angle),
                    border == -3 ? "3" : "1", numberText(outline), numberText(shadow), std::to_string(align),
                    std::to_string(ml), std::to_string(mr), std::to_string(std::max(mt, mb)), std::to_string(enc)};
        return s;
    }
    if (cls == "info") {
        MacroInfoLine i;
        i.key = getString(L, "key", "info");
        i.value = getString(L, "value", "info");
        return i;
    }
    return std::nullopt;
}

template <typename T> void insertAt(std::vector<T> &list, int at, T value)
{
    at = std::clamp(at, 0, static_cast<int>(list.size()));
    list.insert(list.begin() + at, std::move(value));
}

void removeIndex(int i)
{
    auto &l = g_subs->lists;
    const int sinfo = static_cast<int>(l.info.size());
    const int styles = sinfo + static_cast<int>(l.styles.size());
    if (i < 0)
        return;
    if (i < sinfo)
        l.info.erase(l.info.begin() + i);
    else if (i < styles)
        l.styles.erase(l.styles.begin() + (i - sinfo));
    else if (i < g_subs->size())
        l.dialogues.erase(l.dialogues.begin() + (i - styles));
}

int subsDelete(lua_State *L)
{
    checkLive(L);
    checkAllowModify(L);
    std::vector<int> ids;
    const int total = g_subs->size();
    int count = lua_gettop(L);
    if (count == 1 && lua_istable(L, 1)) {
        lua_pushvalue(L, 1);
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            const auto n = static_cast<int>(lua_tointeger(L, -1));
            luaL_argcheck(L, n > 0 && n <= total, 1, "Line index is out of range");
            ids.push_back(n - 1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    } else {
        for (; count > 0; --count) {
            if (!lua_isnumber(L, count))
                return luaL_error(L, "You trying to delete non number line");
            const auto n = static_cast<int>(lua_tointeger(L, count));
            luaL_argcheck(L, n > 0 && n <= total, count, "Out of range line index");
            ids.push_back(n - 1);
        }
    }
    std::sort(ids.begin(), ids.end());
    for (auto it = ids.rbegin(); it != ids.rend(); ++it)
        removeIndex(*it);
    return 0;
}

int subsDeleteRange(lua_State *L)
{
    checkLive(L);
    checkAllowModify(L);
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2))
        return luaL_error(L, "Non number argument of function DeleteRange");
    int a = static_cast<int>(lua_tointeger(L, 1)), b = static_cast<int>(lua_tointeger(L, 2));
    const int all = g_subs->size() + 1;
    if (a < 1)
        a = 1;
    if (b > all)
        b = all;
    for (int i = b - 1; i >= a - 1; --i)
        removeIndex(i);
    return 0;
}

int subsAppend(lua_State *L)
{
    checkLive(L);
    checkAllowModify(L);
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        lua_pushvalue(L, i);
        auto entry = luaToLine(L);
        lua_pop(L, 1);
        if (!entry)
            continue; // legacy silently drops an unknown class here
        auto &l = g_subs->lists;
        std::visit([&](auto &&e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, MacroInfoLine>)
                l.info.push_back(e);
            else if constexpr (std::is_same_v<T, MacroStyleLine>)
                l.styles.push_back(e);
            else
                l.dialogues.push_back(e);
        }, *entry);
    }
    return 0;
}

int subsInsert(lua_State *L)
{
    checkLive(L);
    checkAllowModify(L);
    if (!lua_isnumber(L, 1))
        return luaL_error(L, "Cannot put non numeric index");
    const int n = lua_gettop(L);
    int start = static_cast<int>(lua_tonumber(L, 1) - 1);
    if (start < 0 || start > g_subs->size())
        return luaL_error(L, "Out of range line index");
    for (int i = 2; i <= n; ++i) {
        lua_pushvalue(L, i);
        auto entry = luaToLine(L);
        lua_pop(L, 1);
        if (!entry)
            return luaL_error(L, "You trying to put line of unknown class");
        auto &l = g_subs->lists;
        const int sinfo = static_cast<int>(l.info.size());
        const int styles = sinfo + static_cast<int>(l.styles.size());
        std::visit([&](auto &&e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, MacroInfoLine>)
                insertAt(l.info, start, e);
            else if constexpr (std::is_same_v<T, MacroStyleLine>)
                insertAt(l.styles, start - sinfo, e);
            else
                insertAt(l.dialogues, start - styles, e);
        }, *entry);
        ++start;
    }
    return 0;
}

int subsLengths(lua_State *L)
{
    checkLive(L);
    lua_pushinteger(L, static_cast<lua_Integer>(g_subs->lists.info.size()));
    lua_pushinteger(L, static_cast<lua_Integer>(g_subs->lists.styles.size()));
    lua_pushinteger(L, static_cast<lua_Integer>(g_subs->lists.dialogues.size()));
    return 3;
}

int subsScriptResolution(lua_State *L)
{
    checkLive(L);
    int w = 0, h = 0;
    for (const auto &i : g_subs->lists.info) {
        if (i.key == "PlayResX")
            w = std::atoi(i.value.c_str());
        else if (i.key == "PlayResY")
            h = std::atoi(i.value.c_str());
    }
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

int subsIndex(lua_State *L)
{
    checkLive(L);
    switch (lua_type(L, 2)) {
    case LUA_TNUMBER:
        return lineToLua(L, static_cast<int>(lua_tointeger(L, 2)) - 1) ? 1 : 0;
    case LUA_TSTRING: {
        const std::string key = lua_tostring(L, 2);
        if (key == "n") {
            lua_pushnumber(L, g_subs->size());
            return 1;
        }
        static const std::pair<const char *, lua_CFunction> kMethods[] = {
            {"delete", subsDelete}, {"deleterange", subsDeleteRange}, {"insert", subsInsert},
            {"append", subsAppend}, {"lengths", subsLengths}, {"script_resolution", subsScriptResolution}};
        for (const auto &[name, fn] : kMethods)
            if (key == name) {
                lua_pushcfunction(L, fn);
                return 1;
            }
        return luaL_error(L, "Subtitles object do not have index: '%s'", key.c_str());
    }
    default:
        return luaL_error(L, "Subtitles object do not have index type: '%s'.", lua_typename(L, lua_type(L, 2)));
    }
}

int subsNewIndex(lua_State *L)
{
    checkLive(L);
    if (!lua_isnumber(L, 2))
        return luaL_error(L, "You cannot write usnig non number index");
    checkAllowModify(L);
    const int n = static_cast<int>(lua_tointeger(L, 2));
    if (n < 0) {
        lua_pushcfunction(L, subsInsert);
        lua_pushinteger(L, -n);
        lua_pushvalue(L, 3);
        lua_call(L, 2, 0);
        return 0;
    }
    if (n == 0) {
        lua_pushcfunction(L, subsAppend);
        lua_pushvalue(L, 3);
        lua_call(L, 1, 0);
        return 0;
    }
    if (lua_isnil(L, 3)) {
        lua_pushcfunction(L, subsDelete);
        lua_pushvalue(L, 2);
        lua_call(L, 1, 0);
        return 0;
    }
    lua_pushvalue(L, 3);
    auto entry = luaToLine(L);
    lua_pop(L, 1);
    auto &l = g_subs->lists;
    const int i = n - 1;
    const int sinfo = static_cast<int>(l.info.size());
    const int styles = sinfo + static_cast<int>(l.styles.size());
    if (i >= g_subs->size())
        return luaL_error(L, "Line index is out of range");
    const char *slot = i < sinfo ? "info" : i < styles ? "styles" : "dialogs";
    if (entry && i < sinfo && std::holds_alternative<MacroInfoLine>(*entry)) {
        l.info[static_cast<std::size_t>(i)] = std::get<MacroInfoLine>(*entry);
    } else if (entry && i >= sinfo && i < styles && std::holds_alternative<MacroStyleLine>(*entry)) {
        l.styles[static_cast<std::size_t>(i - sinfo)] = std::get<MacroStyleLine>(*entry);
    } else if (entry && i >= styles && std::holds_alternative<MacroDialogueLine>(*entry)) {
        // The slot keeps the identity of the Line it replaces.
        auto replacement = std::get<MacroDialogueLine>(*entry);
        auto &slotLine = l.dialogues[static_cast<std::size_t>(i - styles)];
        replacement.id = slotLine.id;
        replacement.raw = slotLine.raw;
        slotLine = std::move(replacement);
    } else {
        const char *given = !entry ? "dialogs"
                            : std::holds_alternative<MacroInfoLine>(*entry)  ? "info"
                            : std::holds_alternative<MacroStyleLine>(*entry) ? "styles"
                                                                             : "dialogs";
        return luaL_error(L, "Cannot add a line of class %s to a field of class %s", given, slot);
    }
    return 0;
}

int subsLen(lua_State *L)
{
    checkLive(L);
    lua_pushnumber(L, g_subs->size());
    return 1;
}

int subsIterNext(lua_State *L)
{
    checkLive(L);
    const auto i = static_cast<int>(luaL_checkinteger(L, 2));
    if (i >= g_subs->size()) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, i + 1);
    lineToLua(L, i);
    return 2;
}

int subsIPairs(lua_State *L)
{
    lua_pushcfunction(L, subsIterNext);
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 0);
    return 3;
}

void pushSubtitles(lua_State *L)
{
    lua_newuserdata(L, 1);
    lua_createtable(L, 0, 4);
    lua_pushcfunction(L, subsIndex);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, subsNewIndex);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, subsLen);
    lua_setfield(L, -2, "__len");
    lua_pushcfunction(L, subsIPairs);
    lua_setfield(L, -2, "__ipairs");
    lua_setmetatable(L, -2);
}

int setUndoPoint(lua_State *)
{
    return 0; // a legacy stub (C06): it changes nothing
}

// Legacy `laf->was_cancelled`: a cancelled run raises the cancellation.
void checkCancelled(lua_State *L)
{
    if (g_responder && g_responder->cancelled())
        cancelScript(L);
}

// ---- aegisub.parse_karaoke_data (legacy AutoToFile::LuaParseKaraokeData) ----
// wxString arithmetic over UTF-16 units, as wxString holds them on Windows.

using u16 = std::u16string;

// wxString::Mid: out of range gives "", the count is clamped.
u16 wxMid(const u16 &s, std::size_t first, std::size_t count)
{
    const std::size_t len = s.size();
    if (first > len)
        return {};
    if (count > len - first)
        count = len - first;
    return s.substr(first, count);
}

// wxString::Find(ch, true): the last position, or wxNOT_FOUND (-1).
int wxFindLast(const u16 &s, char16_t ch)
{
    const auto at = s.rfind(ch);
    return at == u16::npos ? -1 : static_cast<int>(at);
}

// wxString::Replace(old, new) for every occurrence, left to right.
void wxReplaceAll(u16 &s, const u16 &from, const u16 &to)
{
    u16 out;
    std::size_t at = 0;
    for (std::size_t hit; (hit = s.find(from, at)) != u16::npos; at = hit + from.size())
        out += s.substr(at, hit - at) + to;
    s = out + s.substr(at);
}

// wxRegEx("\\{[^\\}]*\\}").ReplaceAll(&text, ""): every override block removed.
u16 withoutBlocks(const u16 &s)
{
    u16 out;
    std::size_t at = 0;
    for (;;) {
        const auto open = s.find(u'{', at);
        const auto close = open == u16::npos ? u16::npos : s.find(u'}', open);
        if (close == u16::npos)
            break;
        out += s.substr(at, open - at);
        at = close + 1;
    }
    return out + s.substr(at);
}

// wxString::ToCDouble: the whole text as a C-locale number.
bool wxToCDouble(const u16 &text)
{
    std::string ascii;
    for (const char16_t c : text) {
        if (c > 0x7F)
            return false;
        ascii += static_cast<char>(c);
    }
    if (ascii.empty())
        return false;
    char *end = nullptr;
    std::strtod(ascii.c_str(), &end);
    return end != ascii.c_str() && *end == '\0';
}

struct KaraokeTag {
    u16 name, value;
    unsigned int startTextPos = 0; // where the value starts
};

// Dialogue::ParseTags({"kf", "ko", "k", "K"}, 4) (plainText false, no \p
// among the names): the karaoke tags inside override blocks.
std::vector<KaraokeTag> karaokeTags(const u16 &txt)
{
    static const u16 names[] = {u"kf", u"ko", u"k", u"K"};
    std::vector<KaraokeTag> out;
    const std::size_t len = txt.size();
    std::size_t pos = 0;
    bool tagsBlock = false;
    if (len < 1)
        return out;
    while (pos < len) {
        const char16_t ch = txt[pos];
        if (ch == u'}') {
            tagsBlock = false;
        } else if (ch == u'{' || pos >= len - 1) {
            tagsBlock = true;
            if (pos >= len - 1)
                ++pos;
        } else if (tagsBlock && ch == u'\\') {
            ++pos;
            const std::size_t slash = txt.find(u'\\', pos), bracket = txt.find(u'}', pos);
            const std::size_t tagEnd = slash == u16::npos && bracket == u16::npos ? len
                                       : slash == u16::npos                     ? bracket
                                       : bracket == u16::npos                   ? slash
                                                                                : std::min(slash, bracket);
            u16 tag = txt.substr(pos, tagEnd - pos);
            if (!tag.empty() && tag.back() == u')')
                tag.pop_back();
            for (const u16 &name : names) {
                if (tag.size() <= name.size() || tag.compare(0, name.size(), name) != 0)
                    continue;
                const char16_t first = tag[name.size()];
                if (!(first == u'(' || (first >= u'0' && first <= u'9') || first == u'.' || first == u'-' ||
                      first == u'+'))
                    continue;
                KaraokeTag data{name, {}, static_cast<unsigned int>(pos + name.size())};
                u16 value = tag.substr(name.size());
                if (first == u'(') {
                    ++data.startTextPos;
                    // tagValue.After('(').BeforeFirst(')')
                    const u16 after = value.substr(value.find(u'(') + 1);
                    data.value = after.substr(0, after.find(u')'));
                } else {
                    if (!wxToCDouble(value)) {
                        u16 digits;
                        for (const char16_t c : value) {
                            if (!(c >= u'0' && c <= u'9') && c != u'.' && c != u'-' && c != u'+')
                                break;
                            digits += c;
                        }
                        value = digits;
                    }
                    data.value = value;
                }
                out.push_back(std::move(data));
                pos = tagEnd - 1;
                break;
            }
        }
        ++pos;
    }
    return out;
}

std::string utf8Of(const u16 &s)
{
    const auto u8 = hikari::core::toUtf8(s);
    return std::string(u8.begin(), u8.end());
}

void pushSyllable(lua_State *L, int duration, int start, int end, const std::string &tag, const u16 &text,
                  const u16 &stripped)
{
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, duration);
    lua_setfield(L, -2, "duration");
    lua_pushinteger(L, start);
    lua_setfield(L, -2, "start_time");
    lua_pushinteger(L, end);
    lua_setfield(L, -2, "end_time");
    setString(L, "tag", tag);
    setString(L, "text", utf8Of(text));
    setString(L, "text_stripped", utf8Of(stripped));
}

// Legacy stores the syllables in the line table it was given (from index 0,
// an empty syllable first) and returns that table: it never made one of its
// own. With no karaoke tag, index 1 is the whole line as one "k" syllable.
int parseKaraokeData(lua_State *L)
{
    checkCancelled(L);
    const auto entry = luaToLine(L);
    if (!entry || !std::holds_alternative<MacroDialogueLine>(*entry)) {
        lua_pushstring(L, "You try to parse karaoke from non dialogue line");
        return lua_error(L);
    }
    const auto &line = std::get<MacroDialogueLine>(*entry);
    // LuaToLine keeps "text" as TextTl when "text_translation" is set; the
    // parsed text is TextTl when it is not empty, else Text.
    const std::string &raw = line.text.empty() ? line.translation : line.text;
    const u16 text =
        hikari::core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(raw.data()), raw.size()));

    int kcount = 0;
    int ktime = 0;
    pushSyllable(L, 0, 0, 0, "", {}, {});
    lua_rawseti(L, -2, kcount++);
    const auto tags = karaokeTags(text);
    std::size_t lastPosition = 0;
    const std::size_t tagssize = tags.size();
    for (std::size_t i = 0; i < tagssize; i++) {
        const KaraokeTag &tdata = tags[i];
        long long nextKstart = static_cast<long long>(lastPosition);
        if (i < tagssize - 1) {
            nextKstart = tags[i + 1].startTextPos;
            const u16 newtxt = wxMid(text, lastPosition, static_cast<std::size_t>(nextKstart) - lastPosition);
            const std::size_t bracketstartpos = static_cast<std::size_t>(wxFindLast(newtxt, u'{'));
            // A part without '{' (legacy: "should not happen") ends before the
            // first '\' after its last '}', taken as a position in the line.
            if (bracketstartpos == static_cast<std::size_t>(-1)) {
                const std::size_t bracketendpos = static_cast<std::size_t>(wxFindLast(newtxt, u'}'));
                const std::size_t firstSlash = newtxt.find(u'\\', bracketendpos + 1);
                if (firstSlash != u16::npos)
                    nextKstart = static_cast<long long>(firstSlash - 1);
            } else {
                nextKstart = static_cast<long long>(lastPosition + bracketstartpos - 1);
            }
        } else {
            nextKstart = static_cast<long long>(text.length());
        }
        int kdur = std::atoi(utf8Of(tdata.value).c_str()); // wxAtoi
        kdur *= 10;
        u16 ktext;
        if (nextKstart >= 0) {
            ktext = wxMid(text, lastPosition, tdata.startTextPos - lastPosition - tdata.name.length() - 1);
            const std::size_t newStart = tdata.startTextPos + tdata.value.length();
            ktext += wxMid(text, newStart, static_cast<std::size_t>(nextKstart) - newStart + 1);
            wxReplaceAll(ktext, u"{}", u"");
        }
        const int start = ktime;
        ktime += kdur;
        pushSyllable(L, kdur, start, ktime, utf8Of(tdata.name), ktext, withoutBlocks(ktext));
        lua_rawseti(L, -2, kcount++);
        lastPosition = static_cast<std::size_t>(nextKstart + 1);
    }
    if (kcount < 2) {
        const int startMs = static_cast<int>(line.startMs), endMs = static_cast<int>(line.endMs);
        pushSyllable(L, endMs - startMs, startMs, endMs, "k", text, withoutBlocks(text));
        lua_rawseti(L, -2, kcount++);
    }
    return 1;
}

// ---- host services (L3; legacy HikariSub/Automation.cpp) ------------------
// Each function keeps the legacy argument handling and return shape; the
// application answers through its platform ports. Unavailable is the legacy
// nil (no video, no Document, a cancelled picker).

using hikari::application::HostService;
using hikari::application::HostServiceReply;
using hikari::application::HostServiceRequest;

// A synchronous host call. std::nullopt with `failure` set when no request
// can carry it, the request was cancelled or the host could not answer.
std::optional<HostServiceReply> askHost(HostServiceRequest request, Outcome *failure = nullptr)
{
    Outcome ignored;
    Outcome &why = failure ? *failure : ignored;
    if (!g_services) {
        why = Outcome::Failed;
        return std::nullopt;
    }
    Writer w;
    w.i32(static_cast<std::int32_t>(lua::Service::Host));
    auto payload = w.take();
    const auto body = lua::encodeHostRequest(request);
    payload.insert(payload.end(), body.begin(), body.end());
    const auto answer = g_services->call(std::move(payload));
    if (!answer) {
        why = answer.error();
        return std::nullopt;
    }
    auto reply = lua::decodeHostReply(*answer);
    if (!reply)
        why = Outcome::InvalidInput;
    return reply;
}

// From Lua: a cancelled run raises the cancellation, a failed call a script
// error. std::nullopt means Unavailable (push nil).
std::optional<HostServiceReply> callHost(lua_State *L, const char *name, HostService service,
                                         std::vector<std::int64_t> integers = {},
                                         std::vector<std::string> strings = {},
                                         std::vector<std::string> style = {})
{
    if (!g_services)
        return std::nullopt;
    HostServiceRequest request;
    request.service = service;
    request.integers = std::move(integers);
    request.strings = std::move(strings);
    request.style = std::move(style);
    Outcome failure = Outcome::Ok;
    auto reply = askHost(std::move(request), &failure);
    if (!reply) {
        if (failure == Outcome::Cancelled)
            cancelScript(L);
        luaL_error(L, "the host could not answer aegisub.%s", name);
    }
    if (reply->status == HostServiceReply::Status::Unavailable)
        return std::nullopt;
    return reply;
}

std::int64_t integerAt(const HostServiceReply &r, std::size_t i)
{
    return i < r.integers.size() ? r.integers[i] : 0;
}

std::string stringAt(const HostServiceReply &r, std::size_t i)
{
    return i < r.strings.size() ? r.strings[i] : std::string();
}

int pushNil(lua_State *L)
{
    lua_pushnil(L);
    return 1;
}

int frameFromMs(lua_State *L)
{
    const int ms = static_cast<int>(lua_tonumber(L, -1));
    const auto reply = callHost(L, "frame_from_ms", HostService::FrameFromMs, {ms});
    if (!reply)
        return pushNil(L);
    lua_pushnumber(L, static_cast<double>(integerAt(*reply, 0)));
    return 1;
}

int msFromFrame(lua_State *L)
{
    const int frame = static_cast<int>(lua_tonumber(L, -1));
    const auto reply = callHost(L, "ms_from_frame", HostService::MsFromFrame, {frame});
    if (!reply)
        return pushNil(L);
    lua_pushnumber(L, static_cast<double>(integerAt(*reply, 0)));
    return 1;
}

int videoSize(lua_State *L)
{
    const auto reply = callHost(L, "video_size", HostService::VideoSize);
    if (!reply)
        return pushNil(L);
    const float ar = static_cast<float>(integerAt(*reply, 2)) / static_cast<float>(integerAt(*reply, 3));
    lua_pushnumber(L, static_cast<double>(integerAt(*reply, 0)));
    lua_pushnumber(L, static_cast<double>(integerAt(*reply, 1)));
    lua_pushnumber(L, ar);
    // Legacy's 2.35 test can never hold, so wide frames report 4.
    lua_pushnumber(L, (ar == 1.0f)                 ? 0
                      : (ar < 1.34f && ar > 1.33f) ? 1
                      : (ar < 1.78f && ar > 1.77f) ? 2
                                                   : 4);
    return 4;
}

int keyframes(lua_State *L)
{
    const auto reply = callHost(L, "keyframes", HostService::Keyframes);
    if (!reply)
        return pushNil(L);
    lua_createtable(L, static_cast<int>(reply->integers.size()), 0);
    for (std::size_t i = 0; i < reply->integers.size(); ++i) {
        lua_pushnumber(L, static_cast<double>(reply->integers[i]));
        lua_rawseti(L, -2, static_cast<int>(i) + 1);
    }
    return 1;
}

// get_frame's VideoFrame: BGRA rows, never flipped here.
struct VideoFrame {
    std::size_t width = 0, height = 0;
    std::vector<std::byte> data;
};

VideoFrame *checkFrame(lua_State *L)
{
    return static_cast<VideoFrame *>(luaL_checkudata(L, 1, "VideoFrame"));
}

int frameWidth(lua_State *L)
{
    lua_pushnumber(L, static_cast<double>(checkFrame(L)->width));
    return 1;
}

int frameHeight(lua_State *L)
{
    lua_pushnumber(L, static_cast<double>(checkFrame(L)->height));
    return 1;
}

// The pixel at (x, y), the last two arguments; nil outside the frame.
const unsigned char *framePixel(lua_State *L, VideoFrame *frame)
{
    const auto x = static_cast<std::size_t>(lua_tointeger(L, -2));
    const auto y = static_cast<std::size_t>(lua_tointeger(L, -1));
    lua_pop(L, 2);
    if (x >= frame->width || y >= frame->height)
        return nullptr;
    return reinterpret_cast<const unsigned char *>(frame->data.data()) + y * frame->width * 4 + x * 4;
}

int frameGetPixel(lua_State *L)
{
    const unsigned char *p = framePixel(L, checkFrame(L));
    if (!p)
        return pushNil(L);
    lua_pushnumber(L, p[2] * 65536 + p[1] * 256 + p[0]); // RGB
    return 1;
}

int frameGetPixelFormatted(lua_State *L)
{
    const unsigned char *p = framePixel(L, checkFrame(L));
    if (!p)
        return pushNil(L);
    char text[16]; // AssColor::GetAss(false): "&HBBGGRR&"
    std::snprintf(text, sizeof text, "&H%02X%02X%02X&", p[0], p[1], p[2]);
    lua_pushstring(L, text);
    return 1;
}

int frameCollect(lua_State *L)
{
    checkFrame(L)->~VideoFrame();
    return 0;
}

int getFrame(lua_State *L)
{
    const std::int64_t number = lua_tointeger(L, 1);
    const bool withSubtitles = lua_gettop(L) >= 2 && lua_toboolean(L, 2);
    if (luaL_newmetatable(L, "VideoFrame")) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        static const luaL_Reg methods[] = {{"width", frameWidth},
                                           {"height", frameHeight},
                                           {"getPixel", frameGetPixel},
                                           {"getPixelFormatted", frameGetPixelFormatted},
                                           {"__gc", frameCollect},
                                           {nullptr, nullptr}};
        luaL_register(L, nullptr, methods);
    }
    lua_pop(L, 1);
    auto reply = callHost(L, "get_frame", HostService::Frame, {number, withSubtitles ? 1 : 0});
    const auto width = reply ? static_cast<std::size_t>(std::max<std::int64_t>(0, integerAt(*reply, 0))) : 0;
    const auto height = reply ? static_cast<std::size_t>(std::max<std::int64_t>(0, integerAt(*reply, 1))) : 0;
    if (!reply || reply->pixels.size() != width * height * 4)
        return pushNil(L);
    auto *frame = new (lua_newuserdata(L, sizeof(VideoFrame))) VideoFrame{width, height, std::move(reply->pixels)};
    (void)frame;
    luaL_getmetatable(L, "VideoFrame");
    lua_setmetatable(L, -2);
    return 1;
}

// aegisub.get_frequency_peaks (legacy AutoToFile::LuaGetFreqencyReach): the
// host reads the open audio's spectrum (legacy AudioSpectrum::CreateRange);
// the argument checks and their errors keep legacy's order.
int frequencyPeaks(lua_State *L)
{
    checkCancelled(L);
    for (int i = 1; i <= 5; ++i) {
        if (!lua_isnumber(L, i)) {
            lua_pushstring(L, "Non number argument of function get_frequency_peaks");
            return lua_error(L);
        }
    }
    const int start = static_cast<int>(lua_tointeger(L, 1)), end = static_cast<int>(lua_tointeger(L, 2)),
              freqStart = static_cast<int>(lua_tointeger(L, 3)), freqEnd = static_cast<int>(lua_tointeger(L, 4));
    const lua_Integer asked = lua_tointeger(L, 5);
    const int peek = static_cast<int>(asked < 0 ? 0 : asked > 1000 ? 1000 : asked); // MID(0, peek, 1000)
    const auto reply =
        callHost(L, "get_frequency_peaks", HostService::FrequencyPeaks, {start, end, freqStart, freqEnd, peek});
    if (!reply) {
        lua_pushstring(L, "get_frequency_peaks needs loaded audio by FFMS2");
        return lua_error(L);
    }
    if (integerAt(*reply, 0) != 0) {
        lua_pushstring(L, "get_frequency_peaks cannot get audio provider");
        return lua_error(L);
    }
    if (start < 0 || end < 0) {
        lua_pushstring(L, "get_frequency_peaks start or end time less than zero");
        return lua_error(L);
    }
    // push_value(std::vector<int>): a 1-based list.
    const auto pushList = [L](const auto &values, std::size_t from) {
        const std::size_t count = values.size() > from ? values.size() - from : 0;
        lua_createtable(L, static_cast<int>(count), 0);
        for (std::size_t i = 0; i < count; ++i) {
            lua_pushinteger(L, static_cast<lua_Integer>(values[from + i]));
            lua_rawseti(L, -2, static_cast<int>(i) + 1);
        }
    };
    if (start >= end) {
        pushList(std::vector<int>{}, 0);
        return 1;
    }
    pushList(reply->integers, 1);
    if (peek > 0)
        return 1;
    pushList(reply->numbers, 0);
    return 2;
}

int audioSelection(lua_State *L)
{
    const auto reply = callHost(L, "get_audio_selection", HostService::AudioSelection);
    if (!reply)
        return pushNil(L);
    lua_pushnumber(L, static_cast<double>(integerAt(*reply, 0)));
    lua_pushnumber(L, static_cast<double>(integerAt(*reply, 1)));
    return 2;
}

// Legacy looks Script Info up by the Lua field names themselves
// (GetSInfo("automation_scripts"), ...), so these are empty unless a script
// carries such keys.
std::string scriptInfoValue(const char *key)
{
    if (!g_subs)
        return {};
    for (const auto &info : g_subs->lists.info)
        if (info.key == key)
            return info.value;
    return {};
}

int projectProperties(lua_State *L)
{
    const auto reply = callHost(L, "project_properties", HostService::ProjectProperties);
    if (!reply)
        return pushNil(L);
    lua_createtable(L, 0, 14);
    for (const char *key : {"automation_scripts", "export_filters", "export_encoding", "style_storage"})
        setString(L, key, scriptInfoValue(key));
    setNumber(L, "video_zoom", 1);
    for (const char *key : {"ar_value", "scroll_position", "active_row", "ar_mode"})
        setString(L, key, scriptInfoValue(key));
    setNumber(L, "video_position", static_cast<double>(integerAt(*reply, 0)));
    setString(L, "audio_file", stringAt(*reply, 0));
    setString(L, "video_file", stringAt(*reply, 1));
    setString(L, "timecodes_file", "");
    setString(L, "keyframes_file", stringAt(*reply, 2));
    return 1;
}

int textExtents(lua_State *L)
{
    if (!lua_istable(L, 1)) {
        lua_pushstring(L, "First argument of text_extents must be a table");
        return lua_error(L);
    }
    if (!lua_isstring(L, 2)) {
        lua_pushfstring(L, "Second argument of text_extents must be a string but is of type %s",
                        lua_typename(L, lua_type(L, 2)));
        return lua_error(L);
    }
    lua_pushvalue(L, 1);
    const auto entry = luaToLine(L);
    lua_pop(L, 1);
    if (!entry || !std::holds_alternative<MacroStyleLine>(*entry))
        return 0;
    const std::string text = lua_tostring(L, 2);
    if (text.empty()) {
        for (int i = 0; i < 4; ++i)
            lua_pushnumber(L, 0);
        return 4;
    }
    const auto reply =
        callHost(L, "text_extents", HostService::TextExtents, {}, {text}, std::get<MacroStyleLine>(*entry).fields);
    if (!reply || reply->numbers.size() != 4)
        return 0;
    for (const double v : reply->numbers)
        lua_pushnumber(L, v);
    return 4;
}

// Clipboard through LuaJIT FFI function pointers (aegisub.__init_clipboard,
// used by aegisub/clipboard.lua). They cannot raise Lua errors: a failed or
// cancelled call reads as an empty clipboard or a failed set.
char *hikariClipboardGet()
{
    HostServiceRequest request;
    request.service = HostService::ClipboardGet;
    const auto reply = askHost(std::move(request));
    if (!reply || reply->status != HostServiceReply::Status::Ok || reply->strings.empty() ||
        reply->strings[0].empty())
        return nullptr;
    const std::string &text = reply->strings[0];
    auto *copy = static_cast<char *>(std::malloc(text.size() + 1)); // freed by the script (ffi.C.free)
    if (copy) {
        std::memcpy(copy, text.data(), text.size());
        copy[text.size()] = 0;
    }
    return copy;
}

bool hikariClipboardSet(const char *text)
{
    HostServiceRequest request;
    request.service = HostService::ClipboardSet;
    request.strings = {text ? text : ""};
    const auto reply = askHost(std::move(request));
    return reply && reply->status == HostServiceReply::Status::Ok && integerAt(*reply, 0) != 0;
}

int initClipboard(lua_State *L)
{
    lua_getglobal(L, "require");
    lua_pushstring(L, "ffi");
    lua_call(L, 1, 1);
    lua_getfield(L, -1, "cast");
    lua_remove(L, -2);
    lua_createtable(L, 0, 2);
    const auto add = [&](const char *name, const char *type, void *fn) {
        lua_pushvalue(L, -2);
        lua_pushstring(L, type);
        lua_pushlightuserdata(L, fn);
        lua_call(L, 2, 1);
        lua_setfield(L, -2, name);
    };
    add("get", "char *(*)()", reinterpret_cast<void *>(&hikariClipboardGet));
    add("set", "bool (*)(const char *)", reinterpret_cast<void *>(&hikariClipboardSet));
    lua_remove(L, -2);
    return 1;
}

int fileName(lua_State *L)
{
    const auto reply = callHost(L, "file_name", HostService::FileName);
    if (!reply)
        return pushNil(L);
    lua_pushstring(L, stringAt(*reply, 0).c_str());
    return 1;
}

int decodePath(lua_State *L)
{
    const std::string path = checkString(L, 1);
    const auto reply = callHost(L, "decode_path", HostService::DecodePath, {}, {path});
    lua_pushstring(L, reply ? stringAt(*reply, 0).c_str() : path.c_str());
    return 1;
}

// Legacy get_translation (Automation.cpp:83-88): check_string, then
// wxGetTranslation(str) pushed with lua_pushstring, so the result ends at its
// first NUL. O5: the application answers through the
// HikariSub.Automation.Gettext QM of the current language (docs/qt/localisation.md,
// "Lua compatibility"); it decides the lookup (the legacy UTF-8 conversion
// and the untranslated source). Without an answer the source comes back, as
// an untranslated key does.
int gettext(lua_State *L)
{
    const std::string source = checkString(L, 1);
    const auto reply = callHost(L, "gettext", HostService::Gettext, {}, {source});
    lua_pushstring(L, reply && !reply->strings.empty() ? reply->strings.front().c_str() : source.c_str());
    return 1;
}

int statusText(lua_State *L)
{
    const std::string text = checkString(L, 1);
    lua_pop(L, 1);
    if (!callHost(L, "set_status_text", HostService::StatusText, {}, {text}))
        return pushNil(L);
    return 0;
}

int openDialog(lua_State *L)
{
    // Legacy reads (title, dir, file, wildcard), so the 2nd argument is the
    // directory and the 3rd the file name.
    const std::string title = checkString(L, 1), dir = checkString(L, 2), file = checkString(L, 3),
                      wildcard = checkString(L, 4);
    const bool multiple = lua_toboolean(L, 5);
    const bool mustExist = lua_toboolean(L, 6) || lua_isnil(L, 6);
    const auto reply = callHost(L, "dialog.open", HostService::OpenFiles, {multiple ? 1 : 0, mustExist ? 1 : 0},
                                {title, dir, file, wildcard});
    if (!reply || reply->strings.empty())
        return pushNil(L);
    if (!multiple) {
        lua_pushstring(L, reply->strings[0].c_str());
        return 1;
    }
    lua_createtable(L, static_cast<int>(reply->strings.size()), 0);
    for (std::size_t i = 0; i < reply->strings.size(); ++i) {
        lua_pushstring(L, reply->strings[i].c_str());
        lua_rawseti(L, -2, static_cast<int>(i) + 1);
    }
    return 1;
}

int saveDialog(lua_State *L)
{
    const std::string title = checkString(L, 1), dir = checkString(L, 2), file = checkString(L, 3),
                      wildcard = checkString(L, 4);
    const bool promptOverwrite = !lua_toboolean(L, 5);
    const auto reply =
        callHost(L, "dialog.save", HostService::SaveFile, {promptOverwrite ? 1 : 0}, {title, dir, file, wildcard});
    if (!reply || reply->strings.empty())
        return pushNil(L);
    lua_pushstring(L, reply->strings[0].c_str());
    return 1;
}

// aegisub.gui: Line editor positions are 1-based in Lua, 0-based on the wire.
int getCursor(lua_State *L)
{
    const auto reply = callHost(L, "gui.get_cursor", HostService::EditorCursor);
    if (!reply)
        return pushNil(L);
    lua_pushnumber(L, static_cast<double>(integerAt(*reply, 0) + 1));
    return 1;
}

int setCursor(lua_State *L)
{
    const std::int64_t point = lua_tointeger(L, -1) - 1;
    lua_pop(L, 1);
    callHost(L, "gui.set_cursor", HostService::SetEditorCursor, {point});
    return 0;
}

int getSelection(lua_State *L)
{
    const auto reply = callHost(L, "gui.get_selection", HostService::EditorSelection);
    if (!reply)
        return pushNil(L);
    const std::int64_t start = integerAt(*reply, 0) + 1, end = integerAt(*reply, 1) + 1;
    lua_pushnumber(L, static_cast<double>(std::min(start, end)));
    lua_pushnumber(L, static_cast<double>(std::max(start, end)));
    return 2;
}

int setSelection(lua_State *L)
{
    const std::int64_t start = lua_tointeger(L, -2) - 1, end = lua_tointeger(L, -1) - 1;
    lua_pop(L, 2);
    callHost(L, "gui.set_selection", HostService::SetEditorSelection, {start, end});
    return 0;
}

int isModified(lua_State *L)
{
    const auto reply = callHost(L, "gui.is_modified", HostService::EditorModified);
    if (!reply)
        return pushNil(L);
    lua_pushboolean(L, integerAt(*reply, 0) != 0);
    return 1;
}

void installHostServices(lua_State *L)
{
    static const std::pair<const char *, lua_CFunction> kServices[] = {
        {"text_extents", textExtents},   {"frame_from_ms", frameFromMs},
        {"ms_from_frame", msFromFrame},  {"video_size", videoSize},
        {"keyframes", keyframes},        {"decode_path", decodePath},
        {"__init_clipboard", initClipboard}, {"file_name", fileName},
        {"project_properties", projectProperties}, {"get_audio_selection", audioSelection},
        {"set_status_text", statusText}, {"get_frame", getFrame}};
    for (const auto &[name, fn] : kServices) {
        lua_pushcfunction(L, fn);
        lua_setfield(L, -2, name);
    }
    lua_createtable(L, 0, 5);
    static const std::pair<const char *, lua_CFunction> kGui[] = {{"get_cursor", getCursor},
                                                                  {"set_cursor", setCursor},
                                                                  {"get_selection", getSelection},
                                                                  {"set_selection", setSelection},
                                                                  {"is_modified", isModified}};
    for (const auto &[name, fn] : kGui) {
        lua_pushcfunction(L, fn);
        lua_setfield(L, -2, name);
    }
    lua_setfield(L, -2, "gui");
}

void run(Reader &in, Responder &r, std::size_t payloadSize)
{
    const std::int32_t index = in.i32();
    auto snapshot = lua::decodeSnapshot(in, payloadSize);
    if (!in.ok() || !snapshot)
        return r.terminal(Outcome::InvalidInput, bytesOf("malformed Run"));
    lua_State *L = g_script.L;
    if (!L)
        return r.terminal(Outcome::Failed, bytesOf("no script is loaded"));
    if (index < 0 || static_cast<std::size_t>(index) >= g_script.features.size())
        return r.terminal(Outcome::InvalidInput, bytesOf("no such macro"));

    Staged staged;
    staged.canModify = snapshot->canModify;
    staged.lists = std::move(*snapshot);
    g_subs = &staged;
    g_responder = &r;
    g_services = &r;
    g_lastProgress = 0;
    installSink(L);
    lua_getglobal(L, "aegisub");
    // Legacy AutoToFile's constructor, made for each run: these three stay
    // in the aegisub table after the run.
    lua_pushcfunction(L, parseKaraokeData);
    lua_setfield(L, -2, "parse_karaoke_data");
    lua_pushcfunction(L, frequencyPeaks);
    lua_setfield(L, -2, "get_frequency_peaks");
    lua_pushcfunction(L, setUndoPoint);
    lua_setfield(L, -2, "set_undo_point");
    lua_pop(L, 1);
    lua_pushcfunction(L, addStackTrace);
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_script.features[static_cast<std::size_t>(index)]);
    lua_getfield(L, -1, "run");
    lua_remove(L, -2);
    pushSubtitles(L);
    lua_createtable(L, static_cast<int>(staged.lists.selected.size()), 0);
    for (std::size_t i = 0; i < staged.lists.selected.size(); ++i) {
        lua_pushinteger(L, staged.lists.selected[i]);
        lua_rawseti(L, -2, static_cast<int>(i) + 1);
    }
    lua_pushinteger(L, staged.lists.active);
    const int status = lua_pcall(L, 3, 2, -5);
    if (status == 0) {
        // Legacy reads (selected rows, active row) from the macro's returns.
        MacroResult result;
        result.info = std::move(staged.lists.info);
        result.styles = std::move(staged.lists.styles);
        result.dialogues = std::move(staged.lists.dialogues);
        if (lua_isnumber(L, -1))
            result.active = static_cast<int>(lua_tointeger(L, -1));
        if (lua_istable(L, -2)) {
            std::vector<int> selected;
            lua_pushnil(L);
            while (lua_next(L, -3)) {
                if (lua_isnumber(L, -1))
                    selected.push_back(static_cast<int>(lua_tointeger(L, -1)));
                lua_pop(L, 1);
            }
            result.selected = std::move(selected);
        }
        lua_pop(L, 3);
        removeSink(L);
        g_subs = nullptr;
        g_responder = nullptr;
        g_services = nullptr;
        return r.terminal(Outcome::Ok, lua::encodeMacroResult(result));
    }
    const bool cancelled = lua_touserdata(L, -1) == &g_cancelTag;
    const std::string message = cancelled ? std::string() : stringOrEmpty(L, -1);
    lua_pop(L, 2);
    removeSink(L);
    g_subs = nullptr;
    g_responder = nullptr;
    g_services = nullptr;
    r.terminal(cancelled ? Outcome::Cancelled : Outcome::Failed, bytesOf(message));
}

} // namespace

int main()
{
    hikari::lua_native::installGlobalLocale();
    return runHelper(lua::kHelperName, lua::kProtocolVersion, [](const Frame &request, Responder &r) {
        Reader in(request.payload);
        switch (static_cast<lua::Command>(in.i32())) {
        case lua::Command::Load:
            return load(in, r);
        case lua::Command::Run:
            return run(in, r, request.payload.size());
        }
        r.terminal(Outcome::Unsupported);
    });
}
