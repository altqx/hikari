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
// Not yet here, owned by the A33 automation cards: the native preloads
// (lpeg, luabins, re/unicode/lfs) and MoonScript, document and media
// services, and the gettext catalog (identity for now).

#include "hikari/backends/helper_endpoint.h"
#include "hikari/backends/lua_protocol.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
    return luaL_loadbuffer(L, data, size, toUtf8(path).c_str()) == 0;
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

int addStackTrace(lua_State *L)
{
    if (lua_touserdata(L, 1) == &g_cancelTag)
        return 1; // cancellation is not an error to annotate
    const char *message = lua_tostring(L, 1);
    luaL_traceback(L, L, message ? message : "(error object is not a string)", 1);
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

int gettext(lua_State *L)
{
    lua_pushstring(L, checkString(L, 1).c_str()); // catalog bridge: A33-compat
    return 1;
}

int notYetAvailable(lua_State *L)
{
    return luaL_error(L, "%s is not yet available in the isolated automation host",
                      lua_tostring(L, lua_upvalueindex(1)));
}

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
        lua_pushfstring(L, "Error loading Lua include \"%s\":\n%s", toUtf8(filepath).c_str(), lua_tostring(L, -1));
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

int fileDialog(lua_State *L)
{
    return luaL_error(L, "file dialogs are not yet available in the isolated automation host");
}

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
    lua_pushcfunction(L, fileDialog);
    lua_setfield(L, -2, "open");
    lua_pushcfunction(L, fileDialog);
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
    // Host services that arrive with the A33 automation cards.
    for (const char *name : {"text_extents", "frame_from_ms", "ms_from_frame", "video_size", "keyframes",
                             "decode_path", "__init_clipboard", "file_name", "project_properties",
                             "get_audio_selection", "set_status_text", "get_frame"}) {
        lua_pushstring(L, (std::string("aegisub.") + name).c_str());
        lua_pushcclosure(L, notYetAvailable, 1);
        lua_setfield(L, -2, name);
    }
    lua_createtable(L, 0, 5);
    for (const char *name : {"get_cursor", "set_cursor", "get_selection", "set_selection", "is_modified"}) {
        lua_pushstring(L, (std::string("aegisub.gui.") + name).c_str());
        lua_pushcclosure(L, notYetAvailable, 1);
        lua_setfield(L, -2, name);
    }
    lua_setfield(L, -2, "gui");
    lua_setglobal(L, "aegisub");
}

// ---- requests -------------------------------------------------------------

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
    g_script.includePath = {toUtf8(g_script.path.parent_path()), sharedInclude};
    lua_State *L = luaL_newstate();
    if (!L)
        return r.terminal(Outcome::Failed, bytesOf("Could not initialize Lua state"));
    luaL_openlibs(L);
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    lua_pushcfunction(L, include);
    lua_setglobal(L, "include");
    lua_getglobal(L, "package");
    std::string packagePath;
    for (const auto &dir : g_script.includePath)
        packagePath += dir + "/?.lua;" + dir + "/?/init.lua;";
    lua_pushstring(L, packagePath.c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
    installAegisub(L);
    g_script.L = L;

    auto fail = [&](const std::string &message) {
        g_script.info = {};
        g_script.features.clear();
        lua_close(L);
        g_script.L = nullptr;
        r.terminal(Outcome::Failed, bytesOf(message));
    };
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

void run(Reader &in, Responder &r)
{
    const std::int32_t index = in.i32();
    if (!in.ok())
        return r.terminal(Outcome::InvalidInput, bytesOf("malformed Run"));
    lua_State *L = g_script.L;
    if (!L)
        return r.terminal(Outcome::Failed, bytesOf("no script is loaded"));
    if (index < 0 || static_cast<std::size_t>(index) >= g_script.features.size())
        return r.terminal(Outcome::InvalidInput, bytesOf("no such macro"));

    g_responder = &r;
    g_lastProgress = 0;
    installSink(L);
    lua_pushcfunction(L, addStackTrace);
    lua_rawgeti(L, LUA_REGISTRYINDEX, g_script.features[static_cast<std::size_t>(index)]);
    lua_getfield(L, -1, "run");
    lua_remove(L, -2);
    // The document view (subtitles, selection, active line) arrives with the
    // staged-edit card; an empty view stands in until then.
    lua_newtable(L);
    lua_newtable(L);
    lua_pushinteger(L, 0);
    const int status = lua_pcall(L, 3, 0, -5);
    if (status == 0) {
        lua_pop(L, 1);
        removeSink(L);
        g_responder = nullptr;
        return r.terminal(Outcome::Ok);
    }
    const bool cancelled = lua_touserdata(L, -1) == &g_cancelTag;
    const std::string message = cancelled ? std::string() : stringOrEmpty(L, -1);
    lua_pop(L, 2);
    removeSink(L);
    g_responder = nullptr;
    r.terminal(cancelled ? Outcome::Cancelled : Outcome::Failed, bytesOf(message));
}

} // namespace

int main()
{
    return runHelper(lua::kHelperName, lua::kProtocolVersion, [](const Frame &request, Responder &r) {
        Reader in(request.payload);
        switch (static_cast<lua::Command>(in.i32())) {
        case lua::Command::Load:
            return load(in, r);
        case lua::Command::Run:
            return run(in, r);
        }
        r.terminal(Outcome::Unsupported);
    });
}
