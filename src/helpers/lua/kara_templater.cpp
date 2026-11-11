#include "kara_templater.h"

#include "hikari/application/karaoke_layout.h"
#include "hikari/application/karaoke_templates.h"
#include "karaskel_tables.h"
#include "lua_helper_internal.h"

#include <absl/container/flat_hash_map.h>

extern "C" {
#include <lauxlib.h>
}

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hikari::kara_templater {

namespace {

namespace kt = application::karaoke_templates;
using application::MacroDialogueLine;
using application::MacroInfoLine;

double clockNow()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string gettext(lua_State *L, const char *text)
{
    lua_getglobal(L, "aegisub");
    lua_getfield(L, -1, "gettext");
    lua_remove(L, -2);
    lua_pushstring(L, text);
    lua_call(L, 1, 1);
    std::string out = lua_isstring(L, -1) ? lua_tostring(L, -1) : text;
    lua_pop(L, 1);
    return out;
}

// aegisub.<table>.<name> on the stack.
void pushAegisub(lua_State *L, const char *table, const char *name)
{
    lua_getglobal(L, "aegisub");
    if (table) {
        lua_getfield(L, -1, table);
        lua_remove(L, -2);
    }
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
}

void debugOut(lua_State *L, int level, const std::string &text)
{
    pushAegisub(L, "debug", "out");
    lua_pushinteger(L, level);
    lua_pushlstring(L, text.data(), text.size());
    lua_call(L, 2, 0);
}

void progressSet(lua_State *L, double percent)
{
    pushAegisub(L, "progress", "set");
    lua_pushnumber(L, percent);
    lua_call(L, 1, 0);
}

void progressTask(lua_State *L, const char *task)
{
    pushAegisub(L, "progress", "task");
    lua_pushstring(L, task);
    lua_call(L, 1, 0);
}

bool cancelled(lua_State *L)
{
    pushAegisub(L, "progress", "is_cancelled");
    lua_call(L, 0, 1);
    const bool out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return out;
}

int absolute(lua_State *L, int index)
{
    return index > 0 || index <= LUA_REGISTRYINDEX ? index : lua_gettop(L) + index + 1;
}

std::string typeName(lua_State *L, int index)
{
    return lua_typename(L, lua_type(L, index));
}

// The field `name` of the table at `table` as Lua arithmetic reads it.
double number(lua_State *L, int table, const char *name)
{
    lua_getfield(L, table, name);
    if (!lua_isnumber(L, -1))
        luaL_error(L, "attempt to perform arithmetic on field '%s' (a %s value)", name, typeName(L, -1).c_str());
    const double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

// The field as a template variable's value; std::nullopt for nil or a value no variable can hold.
std::optional<kt::Value> value(lua_State *L, int table, const char *name)
{
    lua_getfield(L, table, name);
    std::optional<kt::Value> out;
    if (lua_type(L, -1) == LUA_TNUMBER)
        out = lua_tonumber(L, -1);
    else if (lua_type(L, -1) == LUA_TSTRING)
        out = std::string(lua_tostring(L, -1), lua_objlen(L, -1));
    lua_pop(L, 1);
    return out;
}

// The field as `..` reads it.
std::string concatenated(lua_State *L, int table, const char *name)
{
    lua_getfield(L, table, name);
    if (!lua_isstring(L, -1))
        luaL_error(L, "attempt to concatenate field '%s' (a %s value)", name, typeName(L, -1).c_str());
    std::size_t size = 0;
    const char *text = lua_tolstring(L, -1, &size);
    std::string out(text, size);
    lua_pop(L, 1);
    return out;
}

std::string stringField(lua_State *L, int table, const char *name)
{
    lua_getfield(L, table, name);
    std::string out = lua_isstring(L, -1) ? std::string(lua_tostring(L, -1), lua_objlen(L, -1)) : std::string();
    lua_pop(L, 1);
    return out;
}

void setString(lua_State *L, int table, const char *name, const std::string &text)
{
    table = absolute(L, table);
    lua_pushlstring(L, text.data(), text.size());
    lua_setfield(L, table, name);
}

void setNumber(lua_State *L, int table, const char *name, double v)
{
    table = absolute(L, table);
    lua_pushnumber(L, v);
    lua_setfield(L, table, name);
}

// table.copy: a new table with the same keys and values, on the stack.
void copyTable(lua_State *L, int source)
{
    source = absolute(L, source);
    lua_newtable(L);
    lua_pushnil(L);
    while (lua_next(L, source)) {
        lua_pushvalue(L, -2);
        lua_insert(L, -2);
        lua_settable(L, -4);
    }
}

// tostring(v) through the global, as 2.1.7 calls it.
std::string luaToString(lua_State *L, int index)
{
    index = absolute(L, index);
    lua_getglobal(L, "tostring");
    lua_pushvalue(L, index);
    lua_call(L, 1, 1);
    std::string out = lua_isstring(L, -1) ? lua_tostring(L, -1) : std::string();
    lua_pop(L, 1);
    return out;
}

constexpr int kTenv = 1; // upvalue index of tenv in its functions

int tenvRetime(lua_State *L)
{
    const int tenv = lua_upvalueindex(kTenv);
    lua_getfield(L, tenv, "line");
    const int line = lua_gettop(L);
    lua_getfield(L, tenv, "syl");
    const int syl = lua_gettop(L);
    const std::string mode = lua_isstring(L, 1) ? lua_tostring(L, 1) : std::string();
    const auto arg = [&](int i) {
        if (lua_isnoneornil(L, i) || (lua_isboolean(L, i) && !lua_toboolean(L, i)))
            return 0.0;
        if (!lua_isnumber(L, i))
            luaL_error(L, "attempt to perform arithmetic on local 'add%s' (a %s value)", i == 2 ? "start" : "end",
                       typeName(L, i).c_str());
        return lua_tonumber(L, i);
    };
    const double addstart = arg(2), addend = arg(3);
    const auto lineField = [&](const char *f) { return number(L, line, f); };
    const auto sylField = [&](const char *f) { return number(L, syl, f); };
    double newstart = lineField("start_time"), newend = lineField("end_time");
    if (mode == "syl") {
        newstart = lineField("start_time") + sylField("start_time") + addstart;
        newend = lineField("start_time") + sylField("end_time") + addend;
    } else if (mode == "presyl") {
        newstart = lineField("start_time") + sylField("start_time") + addstart;
        newend = lineField("start_time") + sylField("start_time") + addend;
    } else if (mode == "postsyl") {
        newstart = lineField("start_time") + sylField("end_time") + addstart;
        newend = lineField("start_time") + sylField("end_time") + addend;
    } else if (mode == "line") {
        newstart = lineField("start_time") + addstart;
        newend = lineField("end_time") + addend;
    } else if (mode == "preline") {
        newstart = lineField("start_time") + addstart;
        newend = lineField("start_time") + addend;
    } else if (mode == "postline") {
        newstart = lineField("end_time") + addstart;
        newend = lineField("end_time") + addend;
    } else if (mode == "start2syl") {
        newstart = lineField("start_time") + addstart;
        newend = lineField("start_time") + sylField("start_time") + addend;
    } else if (mode == "syl2end") {
        newstart = lineField("start_time") + sylField("end_time") + addstart;
        newend = lineField("end_time") + addend;
    } else if (mode == "set" || mode == "abs") {
        newstart = addstart;
        newend = addend;
    } else if (mode == "sylpct") {
        newstart = lineField("start_time") + sylField("start_time") + addstart * sylField("duration") / 100;
        newend = lineField("start_time") + sylField("start_time") + addend * sylField("duration") / 100;
    }
    setNumber(L, line, "start_time", newstart);
    setNumber(L, line, "end_time", newend);
    setNumber(L, line, "duration", newend - newstart);
    lua_pushliteral(L, "");
    return 1;
}

constexpr const char *kKRetimeUndo = "hikari.kara_templater.k_retime";

// karaOK's k_retime: retime, then shift the syllable by the Line's change in start. The shift is undone when the
// template's loop iteration ends, so later templates and iterations see the syllable's own times.
int tenvKRetime(lua_State *L)
{
    const int tenv = lua_upvalueindex(kTenv);
    lua_settop(L, 3);
    lua_getfield(L, tenv, "line");
    const int line = lua_gettop(L);
    lua_getfield(L, tenv, "syl");
    const int syl = lua_gettop(L);
    const double oldstart = number(L, line, "start_time");
    lua_getfield(L, tenv, "retime");
    for (int i = 1; i <= 3; ++i)
        lua_pushvalue(L, i);
    lua_call(L, 3, 0);
    const double shift = oldstart - number(L, line, "start_time");
    lua_getfield(L, LUA_REGISTRYINDEX, kKRetimeUndo);
    const int undo = lua_gettop(L);
    lua_pushvalue(L, syl);
    lua_rawget(L, undo);
    if (lua_isnil(L, -1)) {
        lua_createtable(L, 2, 0);
        lua_getfield(L, syl, "start_time");
        lua_rawseti(L, -2, 1);
        lua_getfield(L, syl, "end_time");
        lua_rawseti(L, -2, 2);
        lua_pushvalue(L, syl);
        lua_insert(L, -2);
        lua_rawset(L, undo);
    }
    setNumber(L, syl, "start_time", number(L, syl, "start_time") + shift);
    setNumber(L, syl, "end_time", number(L, syl, "end_time") + shift);
    lua_pushliteral(L, "");
    return 1;
}

int tenvRelayer(lua_State *L)
{
    lua_getfield(L, lua_upvalueindex(kTenv), "line");
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "layer");
    lua_pushliteral(L, "");
    return 1;
}

constexpr int kStyles = 2;

int tenvRestyle(lua_State *L)
{
    lua_getfield(L, lua_upvalueindex(kTenv), "line");
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "style");
    lua_pushvalue(L, 1);
    lua_gettable(L, lua_upvalueindex(kStyles));
    lua_setfield(L, -2, "styleref");
    lua_pushliteral(L, "");
    return 1;
}

int tenvMaxloop(lua_State *L)
{
    lua_pushvalue(L, 1);
    lua_setfield(L, lua_upvalueindex(kTenv), "maxj");
    lua_pushliteral(L, "");
    return 1;
}

int tenvLoopctl(lua_State *L)
{
    lua_pushvalue(L, 1);
    lua_setfield(L, lua_upvalueindex(kTenv), "j");
    lua_pushvalue(L, 2);
    lua_setfield(L, lua_upvalueindex(kTenv), "maxj");
    lua_pushliteral(L, "");
    return 1;
}

// "_<tostring(tenv.<field>)>_<name>"
int decorate(lua_State *L, const char *field)
{
    lua_getfield(L, lua_upvalueindex(kTenv), field);
    std::string out = "_" + luaToString(L, -1) + "_";
    lua_pop(L, 1);
    out += lua_isstring(L, 1) ? lua_tostring(L, 1) : luaToString(L, 1);
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int decorateLine(lua_State *L)
{
    return decorate(L, "orgline");
}
int decorateSyl(lua_State *L)
{
    return decorate(L, "syl");
}
int decorateBasesyl(lua_State *L)
{
    return decorate(L, "basesyl");
}

// name decorated by the function at `decorator` (none when nil), on the stack.
void decoratedName(lua_State *L, int name, int decorator)
{
    if (lua_isnil(L, decorator) || (lua_isboolean(L, decorator) && !lua_toboolean(L, decorator))) {
        lua_pushvalue(L, name);
        return;
    }
    lua_pushvalue(L, decorator);
    const std::string text = luaToString(L, name);
    lua_pushlstring(L, text.data(), text.size());
    lua_call(L, 1, 1);
}

void pushRecallMeta(lua_State *L)
{
    lua_getfield(L, lua_upvalueindex(kTenv), "recall");
    lua_getmetatable(L, -1);
    lua_remove(L, -2);
}

int recallCall(lua_State *L)
{
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, "decorators");
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    const int decorator = lua_gettop(L);
    decoratedName(L, 2, decorator);
    lua_gettable(L, 1);
    if (lua_isnil(L, -1) || (lua_isboolean(L, -1) && !lua_toboolean(L, -1))) {
        lua_pop(L, 1);
        lua_pushvalue(L, 3);
    }
    return 1;
}

int remember(lua_State *L, int name, int value, int decorator)
{
    pushRecallMeta(L);
    lua_getfield(L, -1, "decorators");
    lua_pushvalue(L, name);
    lua_pushvalue(L, decorator);
    lua_settable(L, -3);
    lua_pop(L, 2);
    decoratedName(L, name, decorator);
    lua_getfield(L, lua_upvalueindex(kTenv), "recall");
    lua_insert(L, -2);
    lua_pushvalue(L, value);
    lua_settable(L, -3);
    lua_pop(L, 1);
    lua_pushvalue(L, value);
    return 1;
}

int tenvRemember(lua_State *L)
{
    lua_settop(L, 3);
    return remember(L, 1, 2, 3);
}

int rememberWith(lua_State *L, const char *decorator)
{
    lua_settop(L, 2);
    pushRecallMeta(L);
    lua_getfield(L, -1, decorator);
    lua_remove(L, -2);
    return remember(L, 1, 2, 3);
}

int tenvRememberLine(lua_State *L)
{
    return rememberWith(L, "decorator_line");
}
int tenvRememberSyl(lua_State *L)
{
    return rememberWith(L, "decorator_syl");
}
int tenvRememberBasesyl(lua_State *L)
{
    return rememberWith(L, "decorator_basesyl");
}

int tenvRememberIf(lua_State *L)
{
    lua_settop(L, 4);
    if (lua_toboolean(L, 3))
        return remember(L, 1, 2, 4);
    lua_pushvalue(L, 2);
    return 1;
}

bool isKaraoke(const MacroDialogueLine &l)
{
    return (l.effect.empty() && !l.comment) || l.effect.find("Karaoke") != std::string::npos ||
           l.effect.find("karaoke") != std::string::npos;
}

bool isGenerated(const MacroDialogueLine &l)
{
    return l.effect == "fx" && !l.comment;
}

void setUndoPoint(lua_State *L)
{
    pushAegisub(L, nullptr, "set_undo_point");
    lua_pushliteral(L, "apply karaoke template");
    lua_call(L, 1, 0);
}

struct Issue {
    kt::Where where;
    std::optional<kt::At> at;
    std::string message;
};

struct Deferred {
    int level;
    std::optional<kt::Where> where;
    std::string message;
};

struct Stats {
    double started = 0, prepare = 0;
    int lines = 0, problems = 0;
    std::vector<Issue> issues;
    std::set<std::string> issueKeys;
};

struct Spent {
    double seconds = 0;
    int generated = 0;
};

struct Chunk {
    int ref = LUA_NOREF;
    std::string error;
};

class Run {
public:
    Run(lua_State *L, int subs) : L(L), m_subs(absolute(L, subs)) { m_stats.emplace(); m_stats->started = clockNow(); }
    Run(const Run &) = delete;
    Run &operator=(const Run &) = delete;
    ~Run()
    {
        for (const auto &[_, c] : m_chunks)
            luaL_unref(L, LUA_REGISTRYINDEX, c.ref);
        for (const int ref : {m_tenv, m_meta, m_styles})
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
        for (auto &[_, list] : m_collected)
            for (const int ref : list)
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }

    int wholeRun();
    int selectedRun(int selection);

private:
    // problem_at
    void problem(int level, const std::optional<kt::Where> &where, const std::optional<kt::At> &at,
                 const std::string &message)
    {
        if (m_quiet)
            return;
        if (where)
            m_problemLines.insert(where->index);
        if (m_stats) {
            ++m_stats->problems;
            if (where) {
                const std::string key = std::to_string(where->index) + ":" + std::to_string(at ? at->start : 0) + ":" +
                                        std::to_string(at ? at->length : 0) + ":" + (at ? "" : message);
                if (!m_stats->issueKeys.contains(key) && m_stats->issues.size() < 100) {
                    m_stats->issueKeys.insert(key);
                    m_stats->issues.push_back({*where, at, message});
                }
            }
        }
        if (m_deferred)
            m_deferred->push_back({level, where, message});
        else
            report(level, where, message, where ? where->number : 0);
    }

    void report(int level, const std::optional<kt::Where> &where, std::string message, int number)
    {
        if (where)
            message += std::string(where->kind == "code" ? "Code" : "Template") + " Line: " + std::to_string(number) +
                       "\n";
        debugOut(L, level, message + "\n");
    }

    kt::Report reporter()
    {
        return [this](int level, const std::optional<kt::Where> &w, const std::optional<kt::At> &at,
                      const std::string &m) { problem(level, w, at, m); };
    }

    void spend(const kt::Template &t, double since, int generated = 0)
    {
        if (m_stats && !m_quiet) {
            Spent &s = m_spent[&t];
            s.seconds += clockNow() - since;
            s.generated += generated;
        }
    }

    int size() const
    {
        const auto &s = lua_helper::staged();
        return static_cast<int>(s.info.size() + s.styles.size() + s.dialogues.size());
    }

    const MacroDialogueLine *dialogueAt(int index) const
    {
        const auto &s = lua_helper::staged();
        const int first = static_cast<int>(s.info.size() + s.styles.size()) + 1;
        if (index < first || index >= first + static_cast<int>(s.dialogues.size()))
            return nullptr;
        return &s.dialogues[static_cast<std::size_t>(index - first)];
    }

    bool isKaraokeAt(int index) const
    {
        const MacroDialogueLine *l = dialogueAt(index);
        return l && isKaraoke(*l);
    }

    bool isGeneratedAt(int index) const
    {
        const MacroDialogueLine *l = dialogueAt(index);
        return l && isGenerated(*l);
    }

    void pushSubsMethod(const char *name)
    {
        lua_getfield(L, m_subs, name);
    }

    void pushLine(int index)
    {
        lua_pushinteger(L, index);
        lua_gettable(L, m_subs);
    }

    void pushRef(int ref) const
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    }

    void collectHead();
    void parse(bool keepFx);
    void makeTenv();
    void applyTemplates(const std::vector<int> *sources, const std::set<int> *chosen);
    bool applyLine(int line);
    bool applySyllableTemplates(int syl, int line, const std::vector<kt::TemplateRef> &list, kt::Variables &vars);
    int applyOne(int syl, int line, const kt::Template &t, kt::Variables &vars, bool skipPerchar, bool skipMulti);
    bool matches(const kt::Template &t, int line);
    template <typename Body> void loop(double initMaxj, Body body);
    void undoKRetime();
    bool withinMaxloop();
    void name(int tenv, const char *field, int value);
    void pushUnits(int line, const application::karaoke_layout::KaraOKUnits &units);
    bool needsUnits() const;
    bool applyUnits(int line, const char *list, const std::vector<kt::TemplateRef> &templates, kt::Variables &v,
                    int from, const std::function<void(int tenv, int unit)> &names);
    void runCode(const kt::Template &t);
    std::string runText(const std::string &text, const kt::Variables *vars, const std::optional<kt::Where> &where,
                        const std::vector<kt::Part> *parts);
    kt::Evaluated evaluate(const std::string &expression);
    const Chunk &chunk(const std::string &source, const std::string &name);
    void emit(int newline);
    int problemSelection();
    void reportRun(const char *mode, int generated, const std::function<int(const kt::Where &)> &numberOf);

    lua_State *L;
    int m_subs;
    int m_tenv = LUA_NOREF, m_meta = LUA_NOREF, m_styles = LUA_NOREF;
    kt::Templates m_templates;
    kt::Mode m_mode = kt::Mode::Stock;
    std::set<std::string> m_owned; // the unit names stock mode set in tenv itself
    std::set<int> m_problemLines;
    std::optional<std::vector<Deferred>> m_deferred;
    bool m_quiet = false;
    std::optional<Stats> m_stats;
    std::map<const kt::Template *, Spent> m_spent;
    int m_currentSource = 0;
    bool m_collecting = false;
    karaskel_tables::Head m_head;
    std::optional<application::karaoke_layout::Layout> m_layout;
    std::map<int, std::vector<int>> m_collected; // newline table refs per karaoke Line
    absl::flat_hash_map<std::string, Chunk> m_chunks;
};

void Run::collectHead()
{
    progressTask(L, "Collecting header data...");
    m_head = karaskel_tables::pushKaraskelHead(L, m_subs, true);
    m_styles = luaL_ref(L, LUA_REGISTRYINDEX);
    m_meta = luaL_ref(L, LUA_REGISTRYINDEX);
}

void Run::parse(bool keepFx)
{
    progressTask(L, "Parsing templates...");
    const auto &s = lua_helper::staged();
    std::vector<kt::SubsEntry> entries(s.info.size() + s.styles.size());
    for (const MacroDialogueLine &d : s.dialogues)
        entries.push_back({.dialogue = true, .comment = d.comment, .layer = d.layer, .effect = d.effect, .text = d.text,
                           .style = d.style});
    const auto toNumber = [this](std::string_view text) -> std::optional<double> {
        lua_getglobal(L, "tonumber");
        lua_pushlstring(L, text.data(), text.size());
        lua_call(L, 1, 1);
        std::optional<double> out;
        if (lua_type(L, -1) == LUA_TNUMBER)
            out = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return out;
    };
    std::optional<std::string_view> modeLine;
    for (const MacroInfoLine &info : s.info)
        if (info.key == kt::kModeKey)
            modeLine = info.value;
    m_mode = kt::modeOf(modeLine, entries);
    kt::Parsed parsed = kt::parseTemplates(entries, keepFx, toNumber, m_mode);
    for (const kt::Problem &p : parsed.problems)
        problem(p.level, p.where, p.at, p.message);
    if (!parsed.removed.empty()) {
        pushSubsMethod("delete");
        for (const int i : parsed.removed)
            lua_pushinteger(L, i);
        lua_call(L, static_cast<int>(parsed.removed.size()), 0);
    }
    progressSet(L, 100);
    m_templates = std::move(parsed.templates);
}

void Run::makeTenv()
{
    lua_newtable(L);
    const int tenv = lua_gettop(L);
    pushRef(m_meta);
    lua_setfield(L, tenv, "meta");
    lua_getglobal(L, "string");
    lua_setfield(L, tenv, "string");
    lua_getglobal(L, "math");
    lua_setfield(L, tenv, "math");
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setfield(L, tenv, "_G");
    lua_pushvalue(L, tenv);
    lua_setfield(L, tenv, "tenv");
    const auto closure = [&](const char *name, lua_CFunction f, bool styles = false) {
        lua_pushvalue(L, tenv);
        if (styles)
            pushRef(m_styles);
        lua_pushcclosure(L, f, styles ? 2 : 1);
        lua_setfield(L, tenv, name);
    };
    closure("retime", tenvRetime);
    lua_newtable(L);
    lua_setfield(L, tenv, "fxgroup");
    closure("relayer", tenvRelayer);
    closure("restyle", tenvRestyle, true);
    closure("maxloop", tenvMaxloop);
    lua_getfield(L, tenv, "maxloop");
    lua_setfield(L, tenv, "maxloops");
    closure("loopctl", tenvLoopctl);
    closure("k_retime", tenvKRetime);
    for (const char *lib : {"table", "ipairs", "pairs", "tonumber", "tostring", "type"}) {
        lua_getglobal(L, lib);
        lua_setfield(L, tenv, lib);
    }
    if (m_mode == kt::Mode::KaraOK) {
        lua_pushvalue(L, m_subs);
        lua_setfield(L, tenv, "subs");
    }
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kKRetimeUndo);

    lua_newtable(L);
    lua_newtable(L);
    lua_newtable(L);
    lua_setfield(L, -2, "decorators");
    lua_pushcfunction(L, recallCall);
    lua_setfield(L, -2, "__call");
    using Decorator = std::pair<const char *, lua_CFunction>;
    for (const auto &[name, f] : {Decorator{"decorator_line", decorateLine}, Decorator{"decorator_syl", decorateSyl},
                                  Decorator{"decorator_basesyl", decorateBasesyl}}) {
        lua_pushvalue(L, tenv);
        lua_pushcclosure(L, f, 1);
        lua_setfield(L, -2, name);
    }
    lua_setmetatable(L, -2);
    lua_setfield(L, tenv, "recall");
    closure("remember", tenvRemember);
    closure("remember_line", tenvRememberLine);
    closure("remember_syl", tenvRememberSyl);
    closure("remember_basesyl", tenvRememberBasesyl);
    closure("remember_if", tenvRememberIf);
    m_tenv = luaL_ref(L, LUA_REGISTRYINDEX);
}

const Chunk &Run::chunk(const std::string &source, const std::string &name)
{
    const std::string key = name + '\0' + source;
    if (const auto it = m_chunks.find(key); it != m_chunks.end())
        return it->second;
    Chunk c;
    if (luaL_loadbuffer(L, source.data(), source.size(), name.c_str())) {
        c.error = lua_isstring(L, -1) ? lua_tostring(L, -1) : std::string();
        lua_pop(L, 1);
    } else {
        pushRef(m_tenv);
        lua_setfenv(L, -2);
        c.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    return m_chunks.emplace(key, std::move(c)).first->second;
}

template <typename Body> void Run::loop(double initMaxj, Body body)
{
    pushRef(m_tenv);
    const int tenv = lua_gettop(L);
    setNumber(L, tenv, "maxj", initMaxj);
    setNumber(L, tenv, "j", 0);
    for (;;) {
        lua_getfield(L, tenv, "j");
        lua_getfield(L, tenv, "maxj");
        if (lua_type(L, -2) != LUA_TNUMBER || lua_type(L, -1) != LUA_TNUMBER)
            luaL_error(L, "attempt to compare %s with %s", typeName(L, -1).c_str(), typeName(L, -2).c_str());
        const double j = lua_tonumber(L, -2), maxj = lua_tonumber(L, -1);
        lua_pop(L, 2);
        if (j >= maxj || cancelled(L))
            break;
        setNumber(L, tenv, "j", j + 1);
        body();
        undoKRetime();
    }
    lua_pop(L, 1);
}

void Run::undoKRetime()
{
    lua_getfield(L, LUA_REGISTRYINDEX, kKRetimeUndo);
    const int undo = lua_gettop(L);
    bool any = false;
    lua_pushnil(L);
    while (lua_next(L, undo)) {
        any = true;
        lua_rawgeti(L, -1, 1);
        lua_setfield(L, -3, "start_time");
        lua_rawgeti(L, -1, 2);
        lua_setfield(L, -3, "end_time");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    if (any) {
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, kKRetimeUndo);
    }
}

// karaOK writes a loop's Line only while j <= maxj, so a maxloop below j drops it.
bool Run::withinMaxloop()
{
    if (m_mode != kt::Mode::KaraOK)
        return true;
    pushRef(m_tenv);
    const double j = number(L, lua_gettop(L), "j"), maxj = number(L, lua_gettop(L), "maxj");
    lua_pop(L, 1);
    return j <= maxj;
}

// karaOK's word, char and syll names; stock mode sets them only where the script has not.
void setTenvField(lua_State *L, int tenv, const char *name, int value);

void Run::name(int tenv, const char *field, int value)
{
    if (m_mode == kt::Mode::Stock && !m_owned.contains(field)) {
        if (value == 0)
            return;
        lua_getfield(L, tenv, field);
        const bool taken = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (taken)
            return;
        m_owned.insert(field);
    }
    setTenvField(L, tenv, field, value);
}

void Run::runCode(const kt::Template &t)
{
    const Chunk &c = chunk(t.text, "template code");
    if (c.ref == LUA_NOREF) {
        problem(2, t.where, std::nullopt,
                "Failed to parse Lua code: " + c.error + "\nCode that failed to parse: " + t.text + "\n");
        return;
    }
    const int ref = c.ref;
    loop(t.loops, [&] {
        pushRef(ref);
        if (lua_pcall(L, 0, 0, 0)) {
            const std::string err = luaToString(L, -1);
            lua_pop(L, 1);
            problem(2, t.where, std::nullopt,
                    "Runtime error in template code: " + err + "\nCode producing error: " + t.text + "\n");
        }
    });
}

kt::Evaluated Run::evaluate(const std::string &expression)
{
    const std::string source = "return (" + expression + ")";
    const Chunk &c = chunk(source, source);
    if (c.ref == LUA_NOREF)
        return {kt::Evaluated::Kind::ParseError, c.error};
    pushRef(c.ref);
    if (lua_pcall(L, 0, 1, 0)) {
        kt::Evaluated e{kt::Evaluated::Kind::RuntimeError, luaToString(L, -1)};
        lua_pop(L, 1);
        return e;
    }
    kt::Evaluated e;
    switch (lua_type(L, -1)) {
    case LUA_TSTRING:
    case LUA_TNUMBER:
        e = {kt::Evaluated::Kind::Text, std::string(lua_tostring(L, -1), lua_objlen(L, -1))};
        break;
    case LUA_TNIL:
        break;
    case LUA_TBOOLEAN:
        if (!lua_toboolean(L, -1))
            break;
        [[fallthrough]];
    default:
        luaL_error(L, "invalid replacement value (a %s)", typeName(L, -1).c_str());
    }
    lua_pop(L, 1);
    return e;
}

std::string Run::runText(const std::string &text, const kt::Variables *vars, const std::optional<kt::Where> &where,
                         const std::vector<kt::Part> *parts)
{
    return kt::runTextTemplate(
        text, vars, where, parts, [this](const std::string &e) { return evaluate(e); }, reporter());
}

bool Run::matches(const kt::Template &t, int line)
{
    if (t.style) {
        lua_getfield(L, line, "style");
        const bool same = lua_isstring(L, -1) && lua_type(L, -1) == LUA_TSTRING && *t.style == lua_tostring(L, -1);
        lua_pop(L, 1);
        if (!same)
            return false;
    }
    if (t.stylegroup && stringField(L, line, "style").find(*t.stylegroup) == std::string::npos)
        return false;
    if (!t.fxgroup)
        return true;
    pushRef(m_tenv);
    lua_getfield(L, -1, "fxgroup");
    lua_pushlstring(L, t.fxgroup->data(), t.fxgroup->size());
    lua_gettable(L, -2);
    const bool off = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
    lua_pop(L, 3);
    return !off;
}

void Run::emit(int newline)
{
    newline = absolute(L, newline);
    if (m_collecting) {
        lua_pushvalue(L, newline);
        m_collected[m_currentSource].push_back(luaL_ref(L, LUA_REGISTRYINDEX));
        return;
    }
    pushSubsMethod("append");
    lua_pushvalue(L, newline);
    lua_call(L, 1, 0);
    pushAegisub(L, "hikari", "set_source");
    lua_pushinteger(L, size());
    lua_pushinteger(L, m_currentSource);
    lua_pcall(L, 2, 0, 0) && (lua_pop(L, 1), true);
}

// set_ctx_syl; karaOK rounds places to 0.1 px where 2.1.7 rounds to whole ones.
void sylVariables(lua_State *L, kt::Variables &v, int line, int syl, kt::Mode mode)
{
    const auto round = [mode](double x) {
        return mode == kt::Mode::KaraOK ? std::floor(x * 10 + 0.5) / 10 : std::floor(x + 0.5);
    };
    const auto put = [&](const char *name, std::optional<kt::Value> value) {
        if (value)
            v[name] = *value;
        else
            v.erase(name);
    };
    const auto get = [&](const char *name) -> std::optional<kt::Value> {
        const auto it = v.find(name);
        return it == v.end() ? std::nullopt : std::optional<kt::Value>(it->second);
    };
    const auto numberOf = [&](const char *name) {
        const auto it = v.find(name);
        if (it == v.end() || !std::holds_alternative<double>(it->second))
            luaL_error(L, "attempt to perform arithmetic on field '%s'", name);
        return std::get<double>(it->second);
    };
    put("sstart", value(L, syl, "start_time"));
    put("send", value(L, syl, "end_time"));
    put("sdur", value(L, syl, "duration"));
    put("skdur", number(L, syl, "duration") / 10);
    put("smid", number(L, syl, "start_time") + number(L, syl, "duration") / 2);
    put("start", get("sstart"));
    put("end", get("send"));
    put("dur", get("sdur"));
    put("kdur", get("skdur"));
    put("mid", get("smid"));
    put("si", value(L, syl, "i"));
    put("i", get("si"));
    const double left = number(L, line, "left");
    put("sleft", round(left + number(L, syl, "left")));
    put("scenter", round(left + number(L, syl, "center")));
    put("sright", round(left + number(L, syl, "right")));
    put("swidth", round(number(L, syl, "width")));
    lua_getfield(L, syl, "isfuri");
    const bool furi = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (furi) {
        put("sbottom", get("ltop"));
        put("stop", round(numberOf("ltop") - number(L, syl, "height")));
        put("smiddle", round(numberOf("ltop") - number(L, syl, "height") / 2));
    } else {
        put("stop", get("ltop"));
        put("smiddle", get("lmiddle"));
        put("sbottom", get("lbottom"));
    }
    put("sheight", value(L, syl, "height"));
    const std::string halign = stringField(L, line, "halign"), valign = stringField(L, line, "valign");
    if (halign == "left")
        put("sx", round(left + number(L, syl, "left")));
    else if (halign == "center")
        put("sx", round(left + number(L, syl, "center")));
    else if (halign == "right")
        put("sx", round(left + number(L, syl, "right")));
    if (valign == "top")
        put("sy", get("stop"));
    else if (valign == "middle")
        put("sy", get("smiddle"));
    else if (valign == "bottom")
        put("sy", get("sbottom"));
    put("ci", value(L, syl, "ci"));
    put("slen", value(L, syl, "length"));
    put("left", get("sleft"));
    put("center", get("scenter"));
    put("right", get("sright"));
    put("width", get("swidth"));
    put("top", get("stop"));
    put("middle", get("smiddle"));
    put("bottom", get("sbottom"));
    put("height", get("sheight"));
    put("x", get("sx"));
    put("y", get("sy"));
    put("len", get("slen"));
}

void setTenvField(lua_State *L, int tenv, const char *name, int value)
{
    if (value == 0)
        lua_pushnil(L);
    else
        lua_pushvalue(L, value);
    lua_setfield(L, tenv, name);
}

bool Run::applyLine(int line)
{
    line = absolute(L, line);
    const bool karaOK = m_mode == kt::Mode::KaraOK;
    bool applied = false;
    kt::Variables v;
    const auto put = [&](const char *name, std::optional<kt::Value> value) {
        if (value)
            v[name] = *value;
    };
    lua_getfield(L, line, "styleref");
    const int styleref = lua_gettop(L);
    const auto margin = [&](const char *own, const char *styled) -> std::optional<kt::Value> {
        if (number(L, line, own) > 0)
            return value(L, line, own);
        if (!lua_istable(L, styleref))
            luaL_error(L, "attempt to index field 'styleref' (a %s value)", typeName(L, styleref).c_str());
        return value(L, styleref, styled);
    };
    put("layer", value(L, line, "layer"));
    put("lstart", value(L, line, "start_time"));
    put("lend", value(L, line, "end_time"));
    put("ldur", value(L, line, "duration"));
    put("lmid", number(L, line, "start_time") + number(L, line, "duration") / 2);
    put("style", value(L, line, "style"));
    put("actor", value(L, line, "actor"));
    put("margin_l", margin("margin_l", "margin_l"));
    put("margin_r", margin("margin_r", "margin_r"));
    put("margin_t", margin("margin_t", "margin_t"));
    put("margin_b", margin("margin_b", "margin_b"));
    put("margin_v", margin("margin_t", "margin_t"));
    lua_getfield(L, line, "kara");
    const int kara = lua_gettop(L);
    const int syln = static_cast<int>(number(L, kara, "n"));
    put("syln", value(L, kara, "n"));
    put("li", value(L, line, "i"));
    const double left = number(L, line, "left"), width = number(L, line, "width");
    const auto tenths = [](double x) { return std::floor(x * 10 + 0.5) / 10; };
    if (karaOK) {
        put("lleft", tenths(left));
        put("lcenter", std::floor(left * 10 + width * 5 + 0.5) / 10);
        put("lright", std::floor(left * 10 + width * 10 + 0.5) / 10);
        put("lwidth", tenths(width));
        put("ltop", tenths(number(L, line, "top")));
        put("lmiddle", tenths(number(L, line, "middle")));
        put("lbottom", tenths(number(L, line, "bottom")));
        put("lheight", tenths(number(L, line, "height")));
        put("lx", tenths(number(L, line, "x")));
        put("ly", tenths(number(L, line, "y")));
    } else {
        put("lleft", std::floor(left + 0.5));
        put("lcenter", std::floor(left + width / 2 + 0.5));
        put("lright", std::floor(left + width + 0.5));
        put("lwidth", std::floor(width + 0.5));
        put("ltop", std::floor(number(L, line, "top") + 0.5));
        put("lmiddle", std::floor(number(L, line, "middle") + 0.5));
        put("lbottom", std::floor(number(L, line, "bottom") + 0.5));
        put("lheight", std::floor(number(L, line, "height") + 0.5));
        put("lx", std::floor(number(L, line, "x") + 0.5));
        put("ly", std::floor(number(L, line, "y") + 0.5));
    }
    put("llen", value(L, line, "length"));

    pushRef(m_tenv);
    const int tenv = lua_gettop(L);
    setTenvField(L, tenv, "orgline", line);
    for (const char *f : {"line", "syl", "basesyl"})
        setTenvField(L, tenv, f, 0);
    for (const char *f : {"word", "baseword", "syll", "basesyll", "char", "basechar"})
        name(tenv, f, 0);

    const auto copy = [&](const char *from, const char *to) {
        const auto it = v.find(from);
        if (it == v.end())
            v.erase(to);
        else
            v[to] = it->second;
    };
    for (const kt::TemplateRef &ref : m_templates.line) {
        const kt::Template &t = *ref;
        if (!matches(t, line))
            continue;
        if (cancelled(L))
            break;
        copy("lstart", "start");
        copy("lend", "end");
        copy("ldur", "dur");
        if (const auto it = v.find("dur"); it != v.end() && std::holds_alternative<double>(it->second))
            v["kdur"] = std::floor(std::get<double>(it->second) / 10);
        else
            luaL_error(L, "attempt to perform arithmetic on field 'dur'");
        copy("lmid", "mid");
        copy("li", "i");
        copy("lleft", "left");
        copy("lcenter", "center");
        copy("lright", "right");
        copy("lwidth", "width");
        copy("ltop", "top");
        copy("lmiddle", "middle");
        copy("lbottom", "bottom");
        copy("lheight", "height");
        copy("lx", "x");
        copy("ly", "y");
        copy("llen", "len");

        const double since = clockNow();
        int generated = 0;
        loop(t.loops, [&] {
            if (t.code) {
                setTenvField(L, tenv, "line", line);
                runCode(t);
                return;
            }
            applied = true;
            copyTable(L, line);
            const int newline = lua_gettop(L);
            setTenvField(L, tenv, "line", newline);
            setNumber(L, newline, "layer", t.layer);
            std::string buffer;
            if (!karaOK)
                setString(L, newline, "text", "");
            const auto append = [&](const std::function<std::string()> &more) {
                if (karaOK) {
                    buffer += more();
                    return;
                }
                const std::string before = concatenated(L, newline, "text");
                const std::string added = more();
                setString(L, newline, "text", before + added);
            };
            if (!t.pre.empty())
                append([&] { return runText(t.pre, &v, t.preWhere, &t.preParts); });
            if (!t.text.empty()) {
                const char *list = t.unit == kt::Unit::Word ? "words" : t.unit == kt::Unit::Char ? "chars" : nullptr;
                if (list)
                    lua_getfield(L, line, list);
                else
                    lua_pushvalue(L, kara);
                const int units = lua_gettop(L);
                const int n = lua_istable(L, units) ? static_cast<int>(number(L, units, "n")) : 0;
                for (int i = 1; i <= n; ++i) {
                    lua_rawgeti(L, units, i);
                    const int unit = lua_gettop(L);
                    setTenvField(L, tenv, "syl", unit);
                    setTenvField(L, tenv, "basesyl", unit);
                    if (t.unit == kt::Unit::Word) {
                        name(tenv, "word", unit);
                        name(tenv, "baseword", unit);
                    } else if (t.unit == kt::Unit::Char || karaOK) {
                        lua_getfield(L, unit, "word");
                        name(tenv, "word", lua_gettop(L));
                        name(tenv, "baseword", lua_gettop(L));
                        if (t.unit == kt::Unit::Char)
                            lua_getfield(L, unit, "syll");
                        else
                            lua_pushvalue(L, unit);
                        name(tenv, "syll", lua_gettop(L));
                        name(tenv, "basesyll", lua_gettop(L));
                        if (t.unit == kt::Unit::Char) {
                            name(tenv, "char", unit);
                            name(tenv, "basechar", unit);
                        }
                        lua_settop(L, unit);
                    }
                    sylVariables(L, v, line, unit, m_mode);
                    append([&] { return runText(t.text, &v, t.textWhere, &t.textParts); });
                    if (t.addtext)
                        append([&] { return concatenated(L, unit, t.keeptags ? "text" : "text_stripped"); });
                    lua_settop(L, units);
                }
                lua_pop(L, 1);
            } else if (!karaOK || t.addtext) {
                append([&] { return concatenated(L, line, t.keeptags ? "text" : "text_stripped"); });
            }
            if (karaOK)
                setString(L, newline, "text", buffer);
            setString(L, newline, "effect", "fx");
            if (withinMaxloop()) {
                emit(newline);
                ++generated;
            }
            lua_pop(L, 1);
        });
        spend(t, since, generated);
    }

    const auto wordOf = [&](int tenv, int unit) {
        lua_getfield(L, unit, "word");
        name(tenv, "word", lua_gettop(L));
        name(tenv, "baseword", lua_gettop(L));
        lua_pop(L, 1);
    };
    if (karaOK) {
        if (applyUnits(line, "furi", m_templates.furi, v, 1, nullptr))
            applied = true;
        if (applyUnits(line, "fchars", m_templates.furichar, v, 1, nullptr))
            applied = true;
        if (applyUnits(line, "words", m_templates.word, v, 1, [&](int tenv, int unit) {
                name(tenv, "word", unit);
                name(tenv, "baseword", unit);
            }))
            applied = true;
        if (applyUnits(line, "kara", m_templates.syl, v, 1, [&](int tenv, int unit) {
                wordOf(tenv, unit);
                name(tenv, "syll", unit);
                name(tenv, "basesyll", unit);
            }))
            applied = true;
        if (applyUnits(line, "chars", m_templates.chr, v, 1, [&](int tenv, int unit) {
                wordOf(tenv, unit);
                lua_getfield(L, unit, "syll");
                name(tenv, "syll", lua_gettop(L));
                name(tenv, "basesyll", lua_gettop(L));
                lua_pop(L, 1);
                name(tenv, "char", unit);
                name(tenv, "basechar", unit);
            }))
            applied = true;
    } else {
        if (applyUnits(line, "kara", m_templates.syl, v, 0, nullptr))
            applied = true;
        if (applyUnits(line, "furi", m_templates.furi, v, 1, nullptr))
            applied = true;
        if (applyUnits(line, "fchars", m_templates.furichar, v, 1, nullptr))
            applied = true;
        if (applyUnits(line, "words", m_templates.word, v, 1, [&](int tenv, int unit) {
                name(tenv, "word", unit);
                name(tenv, "baseword", unit);
            }))
            applied = true;
    }
    lua_settop(L, line);
    return applied;
}

bool Run::applyUnits(int line, const char *list, const std::vector<kt::TemplateRef> &templates, kt::Variables &v,
                     int from, const std::function<void(int tenv, int unit)> &names)
{
    if (templates.empty())
        return false;
    bool applied = false;
    pushRef(m_tenv);
    const int tenv = lua_gettop(L);
    lua_getfield(L, line, list);
    const int units = lua_gettop(L);
    const int n = lua_istable(L, units) ? static_cast<int>(number(L, units, "n")) : 0;
    for (int i = from; i <= n; ++i) {
        if (cancelled(L))
            break;
        lua_rawgeti(L, units, i);
        const int unit = lua_gettop(L);
        if (names)
            names(tenv, unit);
        if (applySyllableTemplates(unit, line, templates, v))
            applied = true;
        lua_settop(L, units);
    }
    lua_settop(L, tenv - 1);
    return applied;
}

bool Run::applySyllableTemplates(int syl, int line, const std::vector<kt::TemplateRef> &list, kt::Variables &vars)
{
    int applied = 0;
    pushRef(m_tenv);
    const int tenv = lua_gettop(L);
    for (const kt::TemplateRef &ref : list) {
        if (!matches(*ref, line))
            continue;
        if (cancelled(L))
            break;
        setTenvField(L, tenv, "syl", syl);
        setTenvField(L, tenv, "basesyl", syl);
        sylVariables(L, vars, line, syl, m_mode);
        const double since = clockNow();
        const int generated = applyOne(syl, line, *ref, vars, false, false);
        spend(*ref, since, generated);
        applied += generated;
    }
    lua_pop(L, 1);
    return applied > 0;
}

// is_syl_blank; karaOK counts only spaces as blank, where 2.1.7 skips a syllable with no duration too.
bool sylBlank(lua_State *L, int syl, kt::Mode mode)
{
    if (mode == kt::Mode::Stock && number(L, syl, "duration") <= 0)
        return true;
    std::string t = concatenated(L, syl, "text_stripped");
    std::string out;
    for (std::size_t at = 0; at < t.size();) {
        if (t.compare(at, 3, "\xE3\x80\x80") == 0) {
            at += 3;
            continue;
        }
        if (t[at] != ' ' && t[at] != '\t' && t[at] != '\n' && t[at] != '\r')
            out += t[at];
        ++at;
    }
    return out.empty();
}

int Run::applyOne(int syl, int line, const kt::Template &t, kt::Variables &vars, bool skipPerchar, bool skipMulti)
{
    if (cancelled(L))
        return 0;
    int applied = 0;
    if (t.fx) {
        lua_getfield(L, syl, "inline_fx");
        const bool same = lua_type(L, -1) == LUA_TSTRING && *t.fx == lua_tostring(L, -1);
        lua_pop(L, 1);
        if (!same)
            return 0;
    }
    if (t.noblank && sylBlank(L, syl, m_mode))
        return 0;
    pushRef(m_tenv);
    const int tenv = lua_gettop(L);
    if (!skipPerchar && t.perchar) {
        copyTable(L, syl);
        const int charsyl = lua_gettop(L);
        setTenvField(L, tenv, "syl", charsyl);
        lua_getfield(L, tenv, "graphemes");
        const bool graphemes = !(lua_isboolean(L, -1) && !lua_toboolean(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, syl, "style");
        const int style = lua_gettop(L);
        double left = number(L, syl, "left");
        const std::string text = concatenated(L, syl, "text_stripped");
        const auto split = graphemes ? application::karaoke_layout::CharSplit::Graphemes
                                     : application::karaoke_layout::CharSplit::CodePoints;
        for (const std::string &c : application::karaoke_layout::charsOf(text, split)) {
            for (const char *f : {"text", "text_stripped", "text_spacestripped"})
                setString(L, charsyl, f, c);
            setString(L, charsyl, "prespace", "");
            setString(L, charsyl, "postspace", "");
            lua_getglobal(L, "aegisub");
            lua_getfield(L, -1, "text_extents");
            lua_remove(L, -2);
            lua_pushvalue(L, style);
            lua_pushlstring(L, c.data(), c.size());
            lua_call(L, 2, 1);
            if (!lua_isnumber(L, -1))
                luaL_error(L, "attempt to perform arithmetic on local 'width' (a %s value)", typeName(L, -1).c_str());
            const double width = lua_tonumber(L, -1);
            lua_pop(L, 1);
            setNumber(L, charsyl, "left", left);
            setNumber(L, charsyl, "center", left + width / 2);
            setNumber(L, charsyl, "right", left + width);
            setNumber(L, charsyl, "prespacewidth", 0);
            setNumber(L, charsyl, "postspacewidth", 0);
            left += width;
            sylVariables(L, vars, line, charsyl, m_mode);
            applied += applyOne(charsyl, line, t, vars, true, false);
        }
        lua_settop(L, tenv - 1);
        return applied;
    }
    if (!skipMulti && t.multi) {
        copyTable(L, syl);
        const int hlsyl = lua_gettop(L);
        setTenvField(L, tenv, "syl", hlsyl);
        lua_getfield(L, syl, "highlights");
        const int highlights = lua_gettop(L);
        const int n = static_cast<int>(number(L, highlights, "n"));
        for (int hl = 1; hl <= n; ++hl) {
            lua_rawgeti(L, highlights, hl);
            for (const char *f : {"start_time", "end_time", "duration"}) {
                lua_getfield(L, -1, f);
                lua_setfield(L, hlsyl, f);
            }
            lua_pop(L, 1);
            sylVariables(L, vars, line, hlsyl, m_mode);
            applied += applyOne(hlsyl, line, t, vars, true, true);
        }
        lua_settop(L, tenv - 1);
        return applied;
    }
    if (t.code) {
        setTenvField(L, tenv, "line", line);
        runCode(t);
    } else {
        loop(t.loops, [&] {
            copyTable(L, line);
            const int newline = lua_gettop(L);
            lua_getfield(L, syl, "style");
            const int style = lua_gettop(L);
            lua_pushvalue(L, style);
            lua_setfield(L, newline, "styleref");
            if (!lua_istable(L, style))
                luaL_error(L, "attempt to index field 'style' (a %s value)", typeName(L, style).c_str());
            lua_getfield(L, style, "name");
            lua_setfield(L, newline, "style");
            lua_pop(L, 1);
            setNumber(L, newline, "layer", t.layer);
            setTenvField(L, tenv, "line", newline);
            std::string text = runText(t.text, &vars, t.where, nullptr);
            if (t.keeptags)
                text += concatenated(L, syl, "text");
            else if (t.addtext)
                text += concatenated(L, syl, "text_stripped");
            setString(L, newline, "text", text);
            setString(L, newline, "effect", "fx");
            if (withinMaxloop()) {
                emit(newline);
                ++applied;
            }
            lua_pop(L, 1);
        });
    }
    lua_settop(L, tenv - 1);
    return applied;
}

bool Run::needsUnits() const
{
    if (m_mode == kt::Mode::KaraOK || !m_templates.word.empty() || !m_templates.furichar.empty() ||
        !m_templates.chr.empty())
        return true;
    return std::ranges::any_of(m_templates.line, [](const kt::TemplateRef &t) {
        return t->unit == kt::Unit::Word || t->unit == kt::Unit::Char;
    });
}

// additional_proc_line's tables: line.words, line.chars and line.fchars, and each syllable's ci, chars and word.
void Run::pushUnits(int line, const application::karaoke_layout::KaraOKUnits &u)
{
    namespace kl = application::karaoke_layout;
    line = absolute(L, line);
    lua_getfield(L, line, "kara");
    const int kara = lua_gettop(L);
    lua_getfield(L, line, "furi");
    const int furi = lua_gettop(L);
    lua_getfield(L, line, "styleref");
    const int styleref = lua_gettop(L);
    lua_getfield(L, line, "furistyle");
    const int furistyle = lua_gettop(L);
    setNumber(L, line, "length", u.length);
    for (std::size_t i = 0; i < u.sylCi.size(); ++i) {
        lua_rawgeti(L, kara, static_cast<int>(i));
        if (lua_istable(L, -1)) {
            if (i > 0)
                setNumber(L, lua_gettop(L), "ci", u.sylCi[i]);
            lua_createtable(L, static_cast<int>(u.sylChars[i].size()), 1);
            setNumber(L, lua_gettop(L), "n", 0);
            lua_setfield(L, -2, "chars");
        }
        lua_pop(L, 1);
    }
    const auto list = [&](std::size_t n) {
        lua_createtable(L, static_cast<int>(n), 1);
        setNumber(L, lua_gettop(L), "n", static_cast<double>(n));
    };
    const auto append = [&](int owner, const char *field, int item) {
        lua_getfield(L, owner, field);
        const int t = lua_gettop(L);
        const int n = static_cast<int>(number(L, t, "n")) + 1;
        lua_pushvalue(L, item);
        lua_rawseti(L, t, n);
        setNumber(L, t, "n", n);
        lua_pop(L, 1);
    };
    const auto fields = [&](int t, const kl::KaraUnit &k) {
        setString(L, t, "tags", k.tags);
        setString(L, t, "text", k.text);
        setString(L, t, "text_stripped", k.stripped);
        setString(L, t, "text_spacestripped", k.spaceStripped);
        setString(L, t, "prespace", k.prespace);
        setString(L, t, "postspace", k.postspace);
        setNumber(L, t, "width", k.width);
        setNumber(L, t, "left", k.left);
        setNumber(L, t, "center", k.center);
        setNumber(L, t, "right", k.right);
        setNumber(L, t, "prespacewidth", k.prespaceWidth);
        setNumber(L, t, "postspacewidth", k.postspaceWidth);
        setNumber(L, t, "length", k.length);
        if (k.timed) {
            setNumber(L, t, "start_time", k.start);
            setNumber(L, t, "end_time", k.end);
            setNumber(L, t, "duration", k.duration);
        } else {
            for (const char *f : {"start_time", "end_time", "duration"}) {
                lua_getfield(L, line, f);
                lua_setfield(L, t, f);
            }
        }
    };

    list(u.words.size());
    const int words = lua_gettop(L);
    for (std::size_t w = 0; w < u.words.size(); ++w) {
        const kl::KaraUnit &k = u.words[w];
        lua_newtable(L);
        const int t = lua_gettop(L);
        setNumber(L, t, "i", k.i);
        setNumber(L, t, "ci", k.ci);
        fields(t, k);
        list(0);
        lua_setfield(L, t, "chars");
        list(k.kara.size());
        const int wkara = lua_gettop(L);
        for (std::size_t j = 0; j < k.kara.size(); ++j) {
            lua_rawgeti(L, kara, k.kara[j]);
            lua_rawseti(L, wkara, static_cast<int>(j) + 1);
        }
        lua_setfield(L, t, "kara");
        lua_pushvalue(L, styleref);
        lua_setfield(L, t, "style");
        if (k.timed) {
            list(k.highlights.size());
            for (std::size_t h = 0; h < k.highlights.size(); ++h) {
                lua_createtable(L, 0, 3);
                setNumber(L, lua_gettop(L), "duration", k.highlights[h].duration);
                setNumber(L, lua_gettop(L), "start_time", k.highlights[h].start);
                setNumber(L, lua_gettop(L), "end_time", k.highlights[h].end);
                lua_rawseti(L, -2, static_cast<int>(h) + 1);
            }
            lua_setfield(L, t, "highlights");
            lua_rawgeti(L, kara, k.kara.front());
            lua_getfield(L, -1, "i");
            lua_setfield(L, t, "si");
            lua_getfield(L, -1, "inline_fx");
            lua_setfield(L, t, "inline_fx");
            lua_pop(L, 1);
        }
        lua_rawseti(L, words, static_cast<int>(w) + 1);
    }
    for (std::size_t i = 1; i < u.sylWord.size(); ++i) {
        if (u.sylWord[i] == 0)
            continue;
        lua_rawgeti(L, kara, static_cast<int>(i));
        lua_rawgeti(L, words, u.sylWord[i]);
        lua_setfield(L, -2, "word");
        setNumber(L, lua_gettop(L), "wi", u.sylWord[i]);
        lua_pop(L, 1);
    }

    list(u.chars.size());
    const int chars = lua_gettop(L);
    for (std::size_t c = 0; c < u.chars.size(); ++c) {
        const kl::KaraUnit &k = u.chars[c];
        lua_newtable(L);
        const int t = lua_gettop(L);
        setNumber(L, t, "i", k.i);
        setNumber(L, t, "ci", k.ci);
        fields(t, k);
        lua_pushvalue(L, styleref);
        lua_setfield(L, t, "style");
        if (k.word > 0) {
            lua_rawgeti(L, words, k.word);
            const int word = lua_gettop(L);
            lua_pushvalue(L, word);
            lua_setfield(L, t, "word");
            setNumber(L, t, "wi", k.word);
            append(word, "chars", t);
            lua_pop(L, 1);
        }
        if (k.timed) {
            lua_rawgeti(L, kara, k.syl);
            const int syll = lua_gettop(L);
            lua_pushvalue(L, syll);
            lua_setfield(L, t, "syll");
            setNumber(L, t, "si", k.syl);
            lua_getfield(L, syll, "highlights");
            lua_setfield(L, t, "highlights");
            lua_getfield(L, syll, "inline_fx");
            lua_setfield(L, t, "inline_fx");
            append(syll, "chars", t);
            lua_pop(L, 1);
        }
        lua_rawseti(L, chars, static_cast<int>(c) + 1);
    }

    list(u.fchars.size());
    const int fchars = lua_gettop(L);
    for (std::size_t c = 0; c < u.fchars.size(); ++c) {
        const kl::KaraUnit &k = u.fchars[c];
        lua_newtable(L);
        const int t = lua_gettop(L);
        fields(t, k);
        lua_pushvalue(L, furistyle);
        lua_setfield(L, t, "style");
        lua_rawgeti(L, furi, k.syl + 1);
        const int part = lua_gettop(L);
        lua_pushvalue(L, part);
        lua_setfield(L, t, "syll");
        setNumber(L, t, "si", k.syl + 1);
        lua_getfield(L, part, "highlights");
        lua_setfield(L, t, "highlights");
        lua_getfield(L, part, "inline_fx");
        lua_setfield(L, t, "inline_fx");
        lua_pop(L, 1);
        lua_rawseti(L, fchars, static_cast<int>(c) + 1);
    }
    lua_pushvalue(L, words);
    lua_setfield(L, line, "words");
    lua_pushvalue(L, chars);
    lua_setfield(L, line, "chars");
    lua_pushvalue(L, fchars);
    lua_setfield(L, line, "fchars");
    lua_settop(L, kara - 1);
}

void Run::applyTemplates(const std::vector<int> *sources, const std::set<int> *chosen)
{
    makeTenv();
    m_layout.emplace(karaskel_tables::stagedLayout(m_head.video));
    for (const kt::TemplateRef &t : m_templates.once) {
        const double since = clockNow();
        runCode(*t);
        spend(*t, since);
    }
    const int n = sources ? static_cast<int>(sources->size()) : size();
    for (int k = 1; k <= n; ++k) {
        progressSet(L, double(k - 1) / n * 100);
        const int i = sources ? (*sources)[static_cast<std::size_t>(k - 1)] : k;
        if (!isKaraokeAt(i))
            continue;
        m_currentSource = i;
        m_quiet = chosen && !chosen->contains(i);
        pushLine(i);
        const int l = lua_gettop(L);
        setNumber(L, l, "i", i);
        lua_pushboolean(L, false);
        lua_setfield(L, l, "comment");
        const double since = clockNow();
        std::vector<std::string> unmeasured;
        const auto layout = karaskel_tables::layLine(L, *m_layout, *dialogueAt(i), i, unmeasured);
        pushRef(m_styles);
        karaskel_tables::pushKaraskelLine(L, -1, l, layout);
        lua_settop(L, l);
        if (needsUnits()) {
            pushRef(m_tenv);
            lua_getfield(L, -1, "graphemes");
            const bool graphemes = !(lua_isboolean(L, -1) && !lua_toboolean(L, -1));
            lua_settop(L, l);
            application::karaoke_layout::KaraOKUnits units;
            lua_helper::raiseOnHostFailure(L, "text_extents", [&] {
                units = m_layout->karaOKUnits(layout,
                                              graphemes ? application::karaoke_layout::CharSplit::Graphemes
                                                        : application::karaoke_layout::CharSplit::CodePoints,
                                              unmeasured);
            });
            pushUnits(l, units);
        }
        for (const std::string &failure : unmeasured)
            problem(2, std::nullopt, std::nullopt, failure + "; it counts as 0 wide.\n");
        if (m_stats && !m_quiet) {
            m_stats->prepare += clockNow() - since;
            ++m_stats->lines;
        }
        if (applyLine(l)) {
            lua_pushboolean(L, true);
            lua_setfield(L, l, "comment");
            setString(L, l, "effect", "karaoke");
            if (!chosen || chosen->contains(i)) {
                lua_pushinteger(L, i);
                lua_pushvalue(L, l);
                lua_settable(L, m_subs);
            }
        }
        lua_settop(L, l - 1);
    }
}

std::string jsonString(const std::string &s)
{
    std::string out = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c < 0x20 || c == 0x7F) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else
            out += ch;
    }
    return out + "\"";
}

std::string jsonNumber(double x)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6f", x);
    std::string out = buf;
    while (!out.empty() && out.back() == '0')
        out.pop_back();
    if (!out.empty() && out.back() == '.')
        out.pop_back();
    return out;
}

void Run::reportRun(const char *mode, int generated, const std::function<int(const kt::Where &)> &numberOf)
{
    if (!m_stats)
        return;
    std::vector<const kt::Template *> list;
    std::set<const kt::Template *> seen;
    for (const auto *klass : {&m_templates.once, &m_templates.line, &m_templates.syl, &m_templates.furi,
                              &m_templates.word, &m_templates.chr, &m_templates.furichar})
        for (const kt::TemplateRef &t : *klass)
            if (t->where && seen.insert(t.get()).second)
                list.push_back(t.get());
    std::ranges::stable_sort(list, [](const kt::Template *a, const kt::Template *b) {
        return a->where->index < b->where->index;
    });
    std::string entries, issues;
    for (const kt::Template *t : list) {
        const Spent s = m_spent.contains(t) ? m_spent.at(t) : Spent{};
        entries += std::string(entries.empty() ? "" : ",") + "{\"line\":" + std::to_string(numberOf(*t->where)) +
                   ",\"effect\":" + jsonString(t->where->effect) + ",\"style\":" + jsonString(t->where->style) +
                   ",\"seconds\":" + jsonNumber(s.seconds) + ",\"generated\":" + std::to_string(s.generated) + "}";
    }
    for (const Issue &p : m_stats->issues) {
        std::string message = p.message;
        while (!message.empty() && message.back() == '\n')
            message.pop_back();
        issues += std::string(issues.empty() ? "" : ",") + "{\"line\":" + std::to_string(numberOf(p.where)) +
                  ",\"kind\":" + jsonString(p.where.kind == "code" ? "code" : "template") +
                  ",\"message\":" + jsonString(message) + ",\"start\":" + std::to_string(p.at ? p.at->start : 0) +
                  ",\"length\":" + std::to_string(p.at ? p.at->length : 0) + "}";
    }
    const std::string text = "{\"report\":\"karaoke-templater\",\"version\":1,\"mode\":" + jsonString(mode) +
                             ",\"seconds\":" + jsonNumber(clockNow() - m_stats->started) +
                             ",\"prepare_seconds\":" + jsonNumber(m_stats->prepare) +
                             ",\"karaoke_lines\":" + std::to_string(m_stats->lines) +
                             ",\"generated\":" + std::to_string(generated) +
                             ",\"problems\":" + std::to_string(m_stats->problems) + ",\"templates\":[" + entries +
                             "],\"issues\":[" + issues + "]}";
    pushAegisub(L, "hikari", "report");
    lua_pushlstring(L, text.data(), text.size());
    lua_call(L, 1, 0);
}

int Run::problemSelection()
{
    if (m_problemLines.empty())
        return 0;
    lua_createtable(L, static_cast<int>(m_problemLines.size()), 0);
    int k = 0;
    for (const int i : m_problemLines) {
        lua_pushinteger(L, i);
        lua_rawseti(L, -2, ++k);
    }
    lua_pushinteger(L, *m_problemLines.begin());
    return 2;
}

int Run::wholeRun()
{
    collectHead();
    parse(false);
    progressTask(L, "Applying templates...");
    applyTemplates(nullptr, nullptr);
    int generated = 0;
    for (int i = 1; i <= size(); ++i)
        if (isGeneratedAt(i))
            ++generated;
    reportRun("all", generated, [](const kt::Where &w) { return w.number; });
    setUndoPoint(L);
    return problemSelection();
}

std::string trimmed(const std::string &s)
{
    const auto first = std::find_if_not(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return first < last ? std::string(first, last) : std::string();
}

// line_key
std::string lineKey(const MacroDialogueLine &l)
{
    const std::string nul(1, '\0');
    return std::string(l.comment ? "1" : "0") + nul + std::to_string(l.layer) + nul + std::to_string(l.startMs) +
           nul + std::to_string(l.endMs) + nul + l.style + nul + trimmed(l.actor) + nul + std::to_string(l.marginL) +
           nul + std::to_string(l.marginR) + nul + std::to_string(l.marginV) + nul + trimmed(l.effect) + nul +
           trimmed(l.text) + nul + trimmed(l.translation);
}

int countBelow(const std::vector<int> &list, int x)
{
    return static_cast<int>(std::ranges::lower_bound(list, x) - list.begin());
}

int Run::selectedRun(int selection)
{
    selection = absolute(L, selection);
    const int count = size();
    collectHead();
    const int shift = size() - count;
    m_deferred.emplace();
    parse(true);
    std::map<int, int> generatedFrom;
    pushAegisub(L, "hikari", "sources");
    lua_call(L, 0, 1);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        generatedFrom[static_cast<int>(lua_tointeger(L, -2))] = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    std::set<int> chosen;
    const int selected = static_cast<int>(lua_objlen(L, selection));
    for (int k = 1; k <= selected; ++k) {
        lua_rawgeti(L, selection, k);
        const int i = static_cast<int>(lua_tointeger(L, -1)) + shift;
        lua_pop(L, 1);
        if (isKaraokeAt(i))
            chosen.insert(i);
        else if (isGeneratedAt(i) && generatedFrom.contains(i) && isKaraokeAt(generatedFrom[i]))
            chosen.insert(generatedFrom[i]);
    }
    const std::vector<int> sources(chosen.begin(), chosen.end());
    const auto refuse = [&](const char *message) {
        debugOut(L, 2, gettext(L, message) + "\n");
        lua_pushlightuserdata(L, nullptr);
        pushAegisub(L, nullptr, "cancel");
        lua_call(L, 0, 0);
    };
    if (sources.empty())
        refuse("Select karaoke lines, or lines generated from them, to apply the template to.");
    std::vector<int> unknown;
    for (int i = 1; i <= size(); ++i)
        if (isGeneratedAt(i) && !generatedFrom.contains(i))
            unknown.push_back(i);
    progressTask(L, "Applying templates...");

    const int last = !unknown.empty() ? size() : sources.back();
    std::vector<int> order;
    for (int i = 1; i <= last; ++i)
        if (isKaraokeAt(i))
            order.push_back(i);
    m_collecting = true;
    applyTemplates(&order, &chosen);
    m_collecting = false;
    m_quiet = false;

    const auto keyOfRef = [&](int ref) {
        pushRef(ref);
        const auto d = lua_helper::dialogueOf(L);
        lua_pop(L, 1);
        return d ? lineKey(*d) : std::string();
    };
    // find_generated
    std::vector<int> old;
    std::set<int> known;
    for (int i = 1; i <= size(); ++i)
        if (generatedFrom.contains(i) && isGeneratedAt(i)) {
            known.insert(generatedFrom[i]);
            if (chosen.contains(generatedFrom[i]))
                old.push_back(i);
        }
    std::optional<int> firstUnknown;
    bool found = true;
    if (!unknown.empty()) {
        struct Pool {
            std::vector<int> at;
            std::size_t next = 0;
        };
        std::map<std::string, Pool> pool;
        std::set<int> taken;
        for (const int i : unknown)
            pool[lineKey(*dialogueAt(i))].at.push_back(i);
        std::vector<int> others;
        for (const auto &[source, _] : m_collected)
            if (!chosen.contains(source) && !known.contains(source))
                others.push_back(source);
        struct Span {
            int from = 0, to = 0;
        };
        std::map<int, Span> spans;
        for (const int source : others) {
            for (const int ref : m_collected[source]) {
                const auto it = pool.find(keyOfRef(ref));
                if (it == pool.end() || it->second.next >= it->second.at.size()) {
                    found = false;
                    break;
                }
                const int at = it->second.at[it->second.next++];
                taken.insert(at);
                auto [span, fresh] = spans.try_emplace(source, Span{at, at});
                span->second.from = std::min(span->second.from, at);
                span->second.to = std::max(span->second.to, at);
            }
            if (!found)
                break;
        }
        if (found) {
            // owners_of
            bool ordered = true;
            int lastTo = 0;
            for (const int source : others)
                if (const auto it = spans.find(source); it != spans.end()) {
                    ordered = ordered && it->second.from > lastTo;
                    lastTo = it->second.to;
                }
            struct Owner {
                std::set<std::string> styles;
                double after = 0, before = HUGE_VAL;
            };
            std::vector<Owner> owners;
            for (const int source : chosen) {
                if (known.contains(source))
                    continue;
                Owner owner;
                owner.styles.insert(dialogueAt(source)->style);
                if (const auto it = m_collected.find(source); it != m_collected.end())
                    for (const int ref : it->second) {
                        pushRef(ref);
                        owner.styles.insert(stringField(L, -1, "style"));
                        lua_pop(L, 1);
                    }
                if (ordered)
                    for (const int other : others)
                        if (const auto it = spans.find(other); it != spans.end()) {
                            if (other < source)
                                owner.after = std::max(owner.after, double(it->second.to));
                            else if (other > source)
                                owner.before = std::min(owner.before, double(it->second.from));
                        }
                owners.push_back(std::move(owner));
            }
            for (const int i : unknown) {
                if (taken.contains(i))
                    continue;
                const bool owned = std::ranges::any_of(owners, [&](const Owner &o) {
                    return o.styles.contains(dialogueAt(i)->style) && i > o.after && i < o.before;
                });
                if (!owned) {
                    found = false;
                    break;
                }
                old.push_back(i);
                firstUnknown = std::min(firstUnknown.value_or(i), i);
            }
            std::ranges::sort(old);
        }
    }
    if (!found)
        refuse("Cannot tell which generated lines came from the selected lines: the other karaoke lines' generated "
               "lines are not what the template gives them now (the template or those lines changed, or the template "
               "uses random values). Apply the template to the whole file once; after that, selected lines can be "
               "applied to on their own.");

    // replace_generated
    std::map<int, int> firstOf;
    for (const int i : old)
        if (generatedFrom.contains(i))
            firstOf.try_emplace(generatedFrom[i], i);
    struct Entry {
        int ref, source;
    };
    std::map<int, std::vector<Entry>> at;
    std::vector<int> anchors;
    std::vector<Entry> appended;
    for (const int source : sources) {
        std::optional<int> anchor = firstOf.contains(source) ? std::optional(firstOf[source]) : firstUnknown;
        std::vector<Entry> *list = &appended;
        if (anchor) {
            if (!at.contains(*anchor))
                anchors.push_back(*anchor);
            list = &at[*anchor];
        }
        if (const auto it = m_collected.find(source); it != m_collected.end())
            for (const int ref : it->second)
                list->push_back({ref, source});
    }
    std::ranges::sort(anchors);
    std::map<int, int> insertedBelow;
    int total = 0;
    for (const int anchor : anchors) {
        insertedBelow[anchor] = total;
        total += static_cast<int>(at[anchor].size());
    }
    const auto now = [&](int i) {
        const int k = countBelow(anchors, i);
        const int before =
            k > 0 ? insertedBelow[anchors[static_cast<std::size_t>(k - 1)]] +
                        static_cast<int>(at[anchors[static_cast<std::size_t>(k - 1)]].size())
                  : 0;
        return i - countBelow(old, i) + before;
    };
    std::optional<int> hi;
    for (int k = static_cast<int>(old.size()) - 1; k >= 0; --k) {
        const int i = old[static_cast<std::size_t>(k)];
        if (!hi)
            hi = i;
        if (at.contains(i) || k == 0 || old[static_cast<std::size_t>(k - 1)] != i - 1) {
            pushSubsMethod("deleterange");
            lua_pushinteger(L, i);
            lua_pushinteger(L, *hi);
            lua_call(L, 2, 0);
            if (const auto it = at.find(i); it != at.end()) {
                const auto &list = it->second;
                for (std::size_t from = 0; from < list.size(); from += 200) {
                    pushSubsMethod("insert");
                    lua_pushinteger(L, i + static_cast<int>(from));
                    const std::size_t end = std::min(from + 200, list.size());
                    for (std::size_t j = from; j < end; ++j)
                        pushRef(list[j].ref);
                    lua_call(L, static_cast<int>(end - from) + 1, 0);
                }
            }
            hi.reset();
        }
    }
    for (const Entry &e : appended) {
        pushSubsMethod("append");
        pushRef(e.ref);
        lua_call(L, 1, 0);
    }
    for (const int anchor : anchors) {
        const int first = anchor - countBelow(old, anchor) + insertedBelow[anchor];
        const auto &list = at[anchor];
        for (std::size_t j = 0; j < list.size(); ++j) {
            pushAegisub(L, "hikari", "set_source");
            lua_pushinteger(L, first + static_cast<int>(j));
            lua_pushinteger(L, now(list[j].source));
            lua_call(L, 2, 0);
        }
    }
    for (std::size_t j = 0; j < appended.size(); ++j) {
        pushAegisub(L, "hikari", "set_source");
        lua_pushinteger(L, size() - static_cast<int>(appended.size()) + static_cast<int>(j) + 1);
        lua_pushinteger(L, now(appended[j].source));
        lua_call(L, 2, 0);
    }

    std::vector<Deferred> problems = std::move(*m_deferred);
    m_deferred.reset();
    for (const Deferred &p : problems)
        report(p.level, p.where, p.message, p.where ? now(p.where->index) - (p.where->index - p.where->number) : 0);
    std::set<int> moved;
    for (const int index : m_problemLines)
        moved.insert(now(index));
    m_problemLines = std::move(moved);
    int generated = 0;
    for (const int source : sources)
        if (const auto it = m_collected.find(source); it != m_collected.end())
            generated += static_cast<int>(it->second.size());
    reportRun("selected", generated,
              [&](const kt::Where &w) { return now(w.index) - (w.index - w.number); });
    setUndoPoint(L);
    return problemSelection();
}

bool canTemplate()
{
    for (const auto &d : lua_helper::staged().dialogues)
        if (kt::lower(kt::headtail(d.effect).first) == "template")
            return true;
    return false;
}

int applyAll(lua_State *L)
{
    Run run(L, 1);
    return run.wholeRun();
}

int applySelected(lua_State *L)
{
    Run run(L, 1);
    return run.selectedRun(2);
}

int validateAll(lua_State *L)
{
    lua_pushboolean(L, canTemplate());
    return 1;
}

int validateSelected(lua_State *L)
{
    if (!canTemplate()) {
        lua_pushboolean(L, false);
        return 1;
    }
    const auto &s = lua_helper::staged();
    const int first = static_cast<int>(s.info.size() + s.styles.size()) + 1;
    const int n = static_cast<int>(lua_objlen(L, 2));
    for (int k = 1; k <= n; ++k) {
        lua_rawgeti(L, 2, k);
        const int i = static_cast<int>(lua_tointeger(L, -1)) - first;
        lua_pop(L, 1);
        if (i < 0 || i >= static_cast<int>(s.dialogues.size()))
            continue;
        const MacroDialogueLine &l = s.dialogues[static_cast<std::size_t>(i)];
        if (isKaraoke(l) || isGenerated(l)) {
            lua_pushboolean(L, true);
            return 1;
        }
    }
    lua_pushboolean(L, false);
    return 1;
}

}

int load(lua_State *L)
{
    lua_getglobal(L, "include");
    lua_pushliteral(L, "karaskel.lua");
    lua_call(L, 1, 0);
    lua_pushstring(L, gettext(L, "Karaoke Templater").c_str());
    lua_setglobal(L, "script_name");
    lua_pushstring(L, gettext(L, "Macro to apply karaoke effects using the template language").c_str());
    lua_setglobal(L, "script_description");
    lua_pushliteral(L, "Niels Martin Hansen");
    lua_setglobal(L, "script_author");
    lua_pushliteral(L, "2.1.7");
    lua_setglobal(L, "script_version");
    lua_helper::registerMacro(L, gettext(L, "Apply karaoke template"), gettext(L, "Applies karaoke effects from templates"),
                              applyAll, validateAll);
    lua_helper::registerMacro(L, gettext(L, "Apply karaoke template to selected lines"),
                              gettext(L, "Applies karaoke effects from templates to the selected lines only, replacing "
                                    "their earlier generated lines"),
                              applySelected, validateSelected);
    return 0;
}

}
