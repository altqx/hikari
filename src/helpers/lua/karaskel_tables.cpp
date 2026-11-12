#include "karaskel_tables.h"

#include "lua_helper_internal.h"
#include "lua_host_cache.h"

extern "C" {
#include <lauxlib.h>
}

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace hikari::karaskel_tables {

namespace {

namespace kl = application::karaoke_layout;

int absolute(lua_State *L, int index)
{
    return index > 0 || index <= LUA_REGISTRYINDEX ? index : lua_gettop(L) + index + 1;
}

void setNumber(lua_State *L, int table, const char *name, double v)
{
    table = absolute(L, table);
    lua_pushnumber(L, v);
    lua_setfield(L, table, name);
}

void setString(lua_State *L, int table, const char *name, const std::string &text)
{
    table = absolute(L, table);
    lua_pushlstring(L, text.data(), text.size());
    lua_setfield(L, table, name);
}

void setBoolean(lua_State *L, int table, const char *name, bool v)
{
    table = absolute(L, table);
    lua_pushboolean(L, v);
    lua_setfield(L, table, name);
}

void setValue(lua_State *L, int table, const char *name, int value)
{
    table = absolute(L, table);
    lua_pushvalue(L, value);
    lua_setfield(L, table, name);
}

// LuaJIT's parser makes a constructor's table for its first key and grows it key by key; a table that
// starts the same size and takes the same keys in the same order iterates them in the same pairs() order.
void pushAsConstructor(lua_State *L)
{
    lua_createtable(L, 0, 1);
}

// { n = 0 }
void pushList(lua_State *L)
{
    lua_newtable(L);
    setNumber(L, -1, "n", 0);
}

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

std::string stringField(lua_State *L, int table, const char *name)
{
    lua_getfield(L, table, name);
    std::string out = lua_isstring(L, -1) ? std::string(lua_tostring(L, -1), lua_objlen(L, -1)) : std::string();
    lua_pop(L, 1);
    return out;
}

double numberField(lua_State *L, int table, const char *name)
{
    lua_getfield(L, table, name);
    if (!lua_isnumber(L, -1))
        luaL_error(L, "attempt to perform arithmetic on field '%s' (a %s value)", name,
                   lua_typename(L, lua_type(L, -1)));
    const double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

void debugOut(lua_State *L, int level, const std::string &text)
{
    lua_getglobal(L, "aegisub");
    lua_getfield(L, -1, "debug");
    lua_getfield(L, -1, "out");
    lua_pushinteger(L, level);
    lua_pushlstring(L, text.data(), text.size());
    lua_call(L, 2, 0);
    lua_pop(L, 2);
}

bool cancelled(lua_State *L)
{
    lua_getglobal(L, "aegisub");
    lua_getfield(L, -1, "progress");
    lua_getfield(L, -1, "is_cancelled");
    lua_call(L, 0, 1);
    const bool out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 3);
    return out;
}

// karaskel.furigana_scale, set to 0.5 when unset as collect_head does.
double furiganaScale(lua_State *L)
{
    lua_getglobal(L, "karaskel");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0.5;
    }
    lua_getfield(L, -1, "furigana_scale");
    if (!lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        setNumber(L, -1, "furigana_scale", 0.5);
        lua_getfield(L, -1, "furigana_scale");
    }
    const double scale = lua_tonumber(L, -1);
    lua_pop(L, 2);
    return scale;
}

std::string asciiLower(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string formatted(const char *format, double v)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, v);
    return buffer;
}

// A highlight table, shared by a syllable and its furigana.
void pushHighlight(lua_State *L, const kl::Highlight &h)
{
    pushAsConstructor(L);
    setNumber(L, -1, "start_time", h.start);
    setNumber(L, -1, "end_time", h.end);
    setNumber(L, -1, "duration", h.duration);
}

}

kl::Layout stagedLayout(std::optional<application::Resolution> video)
{
    const auto &staged = lua_helper::staged();
    std::vector<application::MacroStyleLine> styles;
    styles.reserve(staged.styles.size());
    for (const application::MacroStyleLine &s : staged.styles)
        styles.push_back({lua_helper::canonicalStyle(s.fields)});
    return kl::Layout(staged.info, styles, lua_host::runExtents(), video);
}

kl::Line layLine(lua_State *L, kl::Layout &layout, const application::MacroDialogueLine &dialogue, double li,
                 std::vector<std::string> &problems)
{
    kl::Line out;
    lua_helper::raiseOnHostFailure(L, "text_extents", [&] {
        out = layout.line(lua_helper::karaokeSource(dialogue), li, {}, problems);
    });
    return out;
}

Head pushKaraskelHead(lua_State *L, int subs, bool generateFurigana, int rows, int cancelCheck)
{
    subs = absolute(L, subs);
    rows = rows ? absolute(L, rows) : 0;
    cancelCheck = cancelCheck ? absolute(L, cancelCheck) : 0;
    Head head;
    const double scale = furiganaScale(L);
    pushAsConstructor(L);
    const int meta = lua_gettop(L);
    setNumber(L, meta, "res_x", 0);
    setNumber(L, meta, "res_y", 0);
    setNumber(L, meta, "video_x_correct_factor", 1.0);
    pushList(L);
    const int styles = lua_gettop(L);
    lua_newtable(L);
    const int toinsert = lua_gettop(L);
    int queued = 0, count = 0;
    std::optional<int> firstStyle;
    int size = static_cast<int>(lua_objlen(L, rows ? rows : subs));
    if (!rows && luaL_callmeta(L, subs, "__len")) {
        size = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }
    for (int i = 1; i <= size; ++i) {
        if (cancelCheck) {
            lua_pushvalue(L, cancelCheck);
            lua_call(L, 0, 0);
        } else if (cancelled(L)) {
            luaL_error(L, "User cancelled");
        }
        if (rows) {
            lua_rawgeti(L, rows, i);
        } else {
            lua_pushinteger(L, i);
            lua_gettable(L, subs);
        }
        const int l = lua_gettop(L);
        const std::string kind = stringField(L, l, "class");
        if (kind == "style") {
            if (!firstStyle)
                firstStyle = i;
            lua_pushvalue(L, l);
            lua_rawseti(L, styles, ++count);
            setNumber(L, styles, "n", count);
            const std::string name = stringField(L, l, "name");
            lua_pushvalue(L, l);
            lua_setfield(L, styles, name.c_str());
            lua_getfield(L, l, "margin_t");
            lua_setfield(L, l, "margin_v");
            if (generateFurigana && name.find("furigana") == std::string::npos) {
                debugOut(L, 5, "Creating furigana style for style: " + name + "\n");
                copyTable(L, l);
                const int fs = lua_gettop(L);
                setNumber(L, fs, "fontsize", numberField(L, l, "fontsize") * scale);
                setNumber(L, fs, "outline", numberField(L, l, "outline") * scale);
                setNumber(L, fs, "shadow", numberField(L, l, "shadow") * scale);
                setString(L, fs, "name", name + "-furigana");
                lua_rawseti(L, toinsert, ++queued);
            }
        } else if (kind == "info") {
            const std::string key = asciiLower(stringField(L, l, "key"));
            lua_getfield(L, l, "value");
            lua_setfield(L, meta, key.c_str());
        } else {
            lua_pop(L, 1);
            break;
        }
        lua_pop(L, 1);
    }
    if (!firstStyle)
        luaL_error(L, "Karaskel error: No styles were found in the file, bug?!");
    for (int k = 1; k <= queued; ++k) {
        lua_rawgeti(L, toinsert, k);
        const int fs = lua_gettop(L);
        lua_getfield(L, fs, "name");
        lua_gettable(L, styles);
        const bool known = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (!known) {
            head.furiganaAdded.push_back(stringField(L, fs, "name"));
            lua_pushvalue(L, fs);
            lua_rawseti(L, styles, ++count);
            setNumber(L, styles, "n", count);
            lua_getfield(L, fs, "name");
            lua_pushvalue(L, fs);
            lua_settable(L, styles);
            lua_pushinteger(L, -*firstStyle);
            lua_pushvalue(L, fs);
            lua_settable(L, subs);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, subs, "script_resolution");
    lua_call(L, 0, 2);
    const double resX = lua_tonumber(L, -2), resY = lua_tonumber(L, -1);
    lua_setfield(L, meta, "res_y");
    lua_setfield(L, meta, "res_x");
    head.firstStyle = *firstStyle;
    std::ranges::reverse(head.furiganaAdded);
    lua_getglobal(L, "aegisub");
    lua_getfield(L, -1, "video_size");
    lua_remove(L, -2);
    lua_call(L, 0, 2);
    if (lua_toboolean(L, -1)) {
        const double videoX = lua_tonumber(L, -2), videoY = lua_tonumber(L, -1);
        setNumber(L, meta, "video_x_correct_factor", (videoY / videoX) / (resY / resX));
        head.video = application::Resolution{static_cast<int>(videoX), static_cast<int>(videoY)};
    }
    lua_pop(L, 2);
    lua_getfield(L, meta, "video_x_correct_factor");
    debugOut(L, 4, formatted("Karaskel: Video X correction factor = %f\n\n", lua_tonumber(L, -1)));
    lua_pop(L, 1);
    return head;
}

void dropAddedFurigana(lua_State *L, int styles, const Head &head)
{
    styles = absolute(L, styles);
    int n = static_cast<int>(numberField(L, styles, "n"));
    for (const std::string &name : head.furiganaAdded) {
        lua_pushnil(L);
        lua_setfield(L, styles, name.c_str());
        lua_pushnil(L);
        lua_rawseti(L, styles, n--);
    }
    setNumber(L, styles, "n", n);
}

namespace {

void setIndex(lua_State *L, int table, int index, int value)
{
    table = absolute(L, table);
    lua_pushvalue(L, value);
    lua_rawseti(L, table, index);
}

int listPush(lua_State *L, int list, int value)
{
    list = absolute(L, list);
    const int n = static_cast<int>(numberField(L, list, "n")) + 1;
    setNumber(L, list, "n", n);
    setIndex(L, list, n, value);
    return n;
}

// The furigana part preproc_line_text makes for `part`, on the stack top.
void pushFuri(lua_State *L, int line, int worksyl, int highlight, const kl::KaraFuri &part)
{
    lua_newtable(L);
    const int t = lua_gettop(L);
    setValue(L, t, "syl", worksyl);
    setBoolean(L, t, "isbreak", part.isBreak);
    setBoolean(L, t, "spillback", part.spillback);
    setNumber(L, t, "start_time", part.start);
    setNumber(L, t, "end_time", part.end);
    setNumber(L, t, "duration", part.duration);
    setNumber(L, t, "kdur", part.kdur);
    setString(L, t, "text", part.text);
    setString(L, t, "text_stripped", part.text);
    setString(L, t, "text_spacestripped", part.text);
    setValue(L, t, "line", line);
    setString(L, t, "tag", part.tag);
    setString(L, t, "inline_fx", part.inlineFx);
    setNumber(L, t, "i", part.i);
    setString(L, t, "prespace", "");
    setString(L, t, "postspace", "");
    pushAsConstructor(L);
    setNumber(L, -1, "n", 1);
    setIndex(L, -1, 1, highlight);
    lua_setfield(L, t, "highlights");
    setBoolean(L, t, "isfuri", true);
}

// What karaskel's preproc_line_text, preproc_line_size and preproc_line_pos leave on `line`, set in their
// order so that pairs() walks the tables as it walks karaskel's.
void fillAsKaraskel(lua_State *L, int line, int styleref, int furistyle, const kl::Line &layout)
{
    const kl::KaraLine &k = layout.kara;
    lua_helper::fillKaraokeData(L, line, layout.macro);
    pushList(L);
    lua_setfield(L, line, "kara");
    pushList(L);
    lua_setfield(L, line, "furi");
    setString(L, line, "text_stripped", "");
    setNumber(L, line, "duration", k.duration);
    lua_getfield(L, line, "kara");
    const int kara = lua_gettop(L);
    lua_getfield(L, line, "furi");
    const int furi = lua_gettop(L);

    const int last = static_cast<int>(layout.karaSyllables.size()) - 1;
    for (int s = 0; s <= last; ++s) {
        const kl::KaraSyllable &syl = layout.karaSyllables[static_cast<std::size_t>(s)];
        pushAsConstructor(L);
        const int worksyl = lua_gettop(L);
        pushList(L);
        lua_setfield(L, worksyl, "highlights");
        pushList(L);
        lua_setfield(L, worksyl, "furi");
        for (std::size_t h = 0; h < syl.highlights.size(); ++h) {
            pushHighlight(L, syl.highlights[h]);
            const int hl = lua_gettop(L);
            lua_getfield(L, worksyl, "highlights");
            listPush(L, -1, hl);
            lua_pop(L, 1);
            for (const kl::KaraFuri &part : layout.karaFuri) {
                if (part.i != s || part.highlight != h)
                    continue;
                pushFuri(L, line, worksyl, hl, part);
                const int f = lua_gettop(L);
                listPush(L, furi, f);
                lua_getfield(L, worksyl, "furi");
                listPush(L, -1, f);
                lua_pop(L, 2);
            }
            if (h == 0) {
                setString(L, worksyl, "text", syl.text);
                setNumber(L, worksyl, "duration", syl.duration);
                setNumber(L, worksyl, "kdur", syl.kdur);
                setNumber(L, worksyl, "start_time", syl.start);
                setNumber(L, worksyl, "end_time", syl.end);
                setString(L, worksyl, "tag", syl.tag);
                setValue(L, worksyl, "line", line);
                setNumber(L, worksyl, "i", syl.i);
                setString(L, worksyl, "text_stripped", syl.stripped);
                setString(L, worksyl, "inline_fx", syl.inlineFx);
                setString(L, worksyl, "text_spacestripped", syl.spaceStripped);
                setString(L, worksyl, "prespace", syl.prespace);
                setString(L, worksyl, "postspace", syl.postspace);
            }
            lua_pop(L, 1);
        }
        setIndex(L, kara, s, worksyl);
        if (s < last)
            setNumber(L, kara, "n", s + 1);
        lua_pop(L, 1);
    }
    setString(L, line, "text_stripped", k.textStripped);

    setValue(L, line, "styleref", styleref);
    setNumber(L, line, "extlead", k.extlead);
    setNumber(L, line, "descent", k.descent);
    setNumber(L, line, "height", k.height);
    setNumber(L, line, "width", k.width);
    for (int s = 0; s <= last; ++s) {
        const kl::KaraSyllable &syl = layout.karaSyllables[static_cast<std::size_t>(s)];
        lua_rawgeti(L, kara, s);
        setValue(L, -1, "style", styleref);
        setNumber(L, -1, "height", syl.height);
        setNumber(L, -1, "width", syl.width);
        setNumber(L, -1, "prespacewidth", syl.prespaceWidth);
        setNumber(L, -1, "postspacewidth", syl.postspaceWidth);
        lua_pop(L, 1);
    }
    setValue(L, line, "furistyle", furistyle);
    if (k.hasFuriStyle)
        for (std::size_t f = 0; f < layout.karaFuri.size(); ++f) {
            lua_rawgeti(L, furi, static_cast<int>(f) + 1);
            setValue(L, -1, "style", furistyle);
            setNumber(L, -1, "height", layout.karaFuri[f].height);
            setNumber(L, -1, "width", layout.karaFuri[f].width);
            setNumber(L, -1, "prespacewidth", 0);
            setNumber(L, -1, "postspacewidth", 0);
            lua_pop(L, 1);
        }

    for (int s = 0; s <= last; ++s) {
        const kl::KaraSyllable &syl = layout.karaSyllables[static_cast<std::size_t>(s)];
        lua_rawgeti(L, kara, s);
        setNumber(L, -1, "left", syl.left);
        setNumber(L, -1, "center", syl.center);
        setNumber(L, -1, "right", syl.right);
        lua_pop(L, 1);
    }
    if (k.hasFuriStyle)
        for (std::size_t f = 0; f < layout.karaFuri.size(); ++f) {
            lua_rawgeti(L, furi, static_cast<int>(f) + 1);
            setNumber(L, -1, "left", layout.karaFuri[f].left);
            setNumber(L, -1, "center", layout.karaFuri[f].center);
            setNumber(L, -1, "right", layout.karaFuri[f].right);
            lua_pop(L, 1);
        }
    lua_pop(L, 2);

    lua_getfield(L, line, "margin_t");
    lua_setfield(L, line, "margin_v");
    setNumber(L, line, "eff_margin_l", k.effMarginL);
    setNumber(L, line, "eff_margin_r", k.effMarginR);
    setNumber(L, line, "eff_margin_t", k.effMarginT);
    setNumber(L, line, "eff_margin_b", k.effMarginB);
    setNumber(L, line, "eff_margin_v", k.effMarginV);
    if (k.halign.empty())
        return;
    setNumber(L, line, "left", k.left);
    setNumber(L, line, "center", k.center);
    setNumber(L, line, "right", k.right);
    setNumber(L, line, "x", k.x);
    setString(L, line, "halign", k.halign);
    setNumber(L, line, "hcenter", k.center);
    if (k.valign == "bottom") {
        setNumber(L, line, "bottom", k.bottom);
        setNumber(L, line, "middle", k.middle);
        setNumber(L, line, "top", k.top);
    } else {
        setNumber(L, line, "top", k.top);
        setNumber(L, line, "middle", k.middle);
        setNumber(L, line, "bottom", k.bottom);
    }
    setNumber(L, line, "y", k.y);
    setString(L, line, "valign", k.valign);
    setNumber(L, line, "vcenter", k.middle);
}

}

void pushKaraskelLine(lua_State *L, int styles, int line, const kl::Line &layout)
{
    styles = absolute(L, styles);
    line = absolute(L, line);
    lua_getfield(L, line, "style");
    lua_gettable(L, styles);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_rawgeti(L, styles, 1);
    }
    const int styleref = lua_gettop(L);
    const std::string styleName = stringField(L, line, "style");
    lua_pushstring(L, (styleName + "-furigana").c_str());
    lua_gettable(L, styles);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, false);
    }
    fillAsKaraskel(L, line, styleref, lua_gettop(L), layout);
    lua_settop(L, styleref - 1);
    lua_pushvalue(L, line);
}

namespace {

bool hasType(lua_State *L, int table, const char *name, int type)
{
    lua_getfield(L, table, name);
    const bool out = lua_type(L, -1) == type;
    lua_pop(L, 1);
    return out;
}

// subs' rows as collect_head's first pass reads them, through the first that is neither a Style nor an info
// line, pushed as a list; or nothing pushed where karaskel's Lua would raise on them.
bool pushHeadRows(lua_State *L, int subs, bool generateFurigana)
{
    lua_newtable(L);
    const int rows = lua_gettop(L);
    int size = 0;
    if (luaL_callmeta(L, subs, "__len")) {
        size = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }
    bool styles = false;
    for (int i = 1; i <= size; ++i) {
        lua_pushinteger(L, i);
        lua_gettable(L, subs);
        const int row = lua_gettop(L);
        if (!lua_istable(L, row)) {
            lua_settop(L, rows - 1);
            return false;
        }
        lua_pushvalue(L, row);
        lua_rawseti(L, rows, i);
        const std::string kind = stringField(L, row, "class");
        bool fine = true;
        if (kind == "style") {
            styles = true;
            fine = hasType(L, row, "name", LUA_TSTRING);
            if (fine && generateFurigana && stringField(L, row, "name").find("furigana") == std::string::npos)
                fine = hasType(L, row, "fontsize", LUA_TNUMBER) && hasType(L, row, "outline", LUA_TNUMBER) &&
                       hasType(L, row, "shadow", LUA_TNUMBER);
        } else if (kind == "info") {
            fine = hasType(L, row, "key", LUA_TSTRING);
        } else {
            lua_pop(L, 1);
            break;
        }
        lua_pop(L, 1);
        if (!fine) {
            lua_settop(L, rows - 1);
            return false;
        }
    }
    if (!styles)
        lua_settop(L, rows - 1);
    return styles;
}

bool furiganaScaleIsNumber(lua_State *L)
{
    lua_getglobal(L, "karaskel");
    bool out = true;
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "furigana_scale");
        out = !lua_toboolean(L, -1) || lua_type(L, -1) == LUA_TNUMBER;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return out;
}

bool plainTable(lua_State *L, int index)
{
    if (!lua_istable(L, index))
        return false;
    if (!lua_getmetatable(L, index))
        return true;
    lua_pop(L, 1);
    return false;
}

std::optional<double> rawNumber(lua_State *L, int table, const char *name)
{
    lua_pushstring(L, name);
    lua_rawget(L, table);
    std::optional<double> out;
    if (lua_type(L, -1) == LUA_TNUMBER)
        out = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return out;
}

bool rawIsNil(lua_State *L, int table, const char *name)
{
    lua_pushstring(L, name);
    lua_rawget(L, table);
    const bool nil = lua_isnil(L, -1);
    lua_pop(L, 1);
    return nil;
}

bool hostFunction(lua_State *L, const char *table, const char *name, lua_helper::HostFunction function)
{
    lua_getglobal(L, "aegisub");
    if (table && lua_istable(L, -1)) {
        lua_pushstring(L, table);
        lua_rawget(L, -2);
        lua_remove(L, -2);
    }
    bool own = false;
    if (lua_istable(L, -1)) {
        lua_pushstring(L, name);
        lua_rawget(L, -2);
        own = lua_helper::isHost(L, -1, function);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return own;
}

// (subs, generate_furigana, cancel_check): meta and styles, or nothing where the run could differ from
// karaskel's Lua; cancel_check raises karaskel's own "User cancelled".
int collectHead(lua_State *L)
{
    const bool generate = lua_toboolean(L, 2) != 0;
    if (!lua_helper::isSubtitles(L, 1) || !lua_isfunction(L, 3) ||
        !hostFunction(L, "debug", "out", lua_helper::HostFunction::DebugOut) || !furiganaScaleIsNumber(L))
        return 0;
    if (!pushHeadRows(L, 1, generate))
        return 0;
    pushKaraskelHead(L, 1, generate, lua_gettop(L), 3);
    return 2;
}

struct Prepared {
    kl::Line layout;
};
std::optional<Prepared> g_prepared;

// (line, styleref, furistyle or false, res_x, res_y, factor, style name): the layout preproc_line gives, into
// g_prepared; raises where karaskel's Lua would raise or the host could not answer.
int prepareLine(lua_State *L)
{
    lua_pushvalue(L, 1);
    const auto dialogue = lua_helper::dialogueOf(L);
    lua_pop(L, 1);
    if (!dialogue)
        return luaL_error(L, "not a dialogue line");
    const std::string name = lua_tostring(L, 7);
    std::vector<std::pair<std::u8string, std::vector<std::string>>> styles;
    const auto addStyle = [&](int index, const std::string &as) {
        lua_pushvalue(L, index);
        auto fields = lua_helper::styleFields(L);
        lua_pop(L, 1);
        if (!fields)
            luaL_error(L, "not a style");
        styles.emplace_back(std::u8string(as.begin(), as.end()), std::move(*fields));
    };
    addStyle(2, name);
    if (lua_istable(L, 3))
        addStyle(3, name + "-furigana");
    kl::Layout layout(styles, lua_tonumber(L, 4), lua_tonumber(L, 5), lua_tonumber(L, 6), lua_host::runExtents());
    std::vector<std::string> problems;
    Prepared prepared;
    lua_helper::raiseOnHostFailure(L, "text_extents", [&] {
        prepared.layout = layout.line(lua_helper::karaokeSource(*dialogue), 0, {}, problems);
    });
    if (!problems.empty())
        return luaL_error(L, "%s", problems.front().c_str());
    g_prepared = std::move(prepared);
    return 0;
}

// The layout reads one MarginV and the Line's integer times; karaskel reads each table field. They agree here.
bool sameAsTables(lua_State *L, int line, int styleref, const kl::Line &layout)
{
    const kl::StyleInfo &style = *layout.style;
    const application::MacroDialogueLine &d = layout.macro;
    const auto is = [&](int table, const char *name, double v) { return rawNumber(L, table, name) == v; };
    return style.align >= 1 && style.align <= 9 && is(styleref, "align", style.align) &&
           is(styleref, "margin_l", style.marginL) && is(styleref, "margin_r", style.marginR) &&
           is(styleref, "margin_t", style.marginV) && is(styleref, "margin_b", style.marginV) &&
           is(styleref, "margin_v", style.marginV) && is(line, "margin_l", d.marginL) &&
           is(line, "margin_r", d.marginR) && is(line, "margin_t", d.marginV) && is(line, "margin_b", d.marginV) &&
           is(line, "start_time", static_cast<double>(d.startMs)) && is(line, "end_time", static_cast<double>(d.endMs));
}

// karaskel.preproc_line(subs, meta, styles, line): true once the line holds what karaskel's Lua would leave
// there. Nothing, and nothing changed, when only the Lua can match it: a script's own text_extents,
// parse_karaoke_data or debug.out, a trace level that shows karaskel's own messages, tables with metatables,
// a line already part-processed, fields the layout reads otherwise, or anything the Lua would raise on.
int preprocLine(lua_State *L)
{
    const int meta = 2, styles = 3, line = 4;
    if (!plainTable(L, meta) || !plainTable(L, styles) || !plainTable(L, line) || !rawIsNil(L, line, "kara") ||
        !rawIsNil(L, line, "styleref") || lua_helper::traceLevel() >= 4 ||
        !hostFunction(L, nullptr, "text_extents", lua_helper::HostFunction::TextExtents) ||
        !hostFunction(L, nullptr, "parse_karaoke_data", lua_helper::HostFunction::ParseKaraokeData) ||
        !hostFunction(L, "debug", "out", lua_helper::HostFunction::DebugOut))
        return 0;
    const auto resX = rawNumber(L, meta, "res_x"), resY = rawNumber(L, meta, "res_y"),
               factor = rawNumber(L, meta, "video_x_correct_factor");
    lua_pushstring(L, "style");
    lua_rawget(L, line);
    if (!resX || !resY || !factor || lua_type(L, -1) != LUA_TSTRING)
        return 0;
    const std::string name(lua_tostring(L, -1), lua_objlen(L, -1));
    lua_rawget(L, styles);
    const bool found = lua_toboolean(L, -1) != 0;
    if (!found) {
        lua_pop(L, 1);
        lua_rawgeti(L, styles, 1);
    }
    const int styleref = lua_gettop(L);
    lua_pushlstring(L, (name + "-furigana").data(), name.size() + 9);
    lua_rawget(L, styles);
    if (!lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, false);
    }
    const int furistyle = lua_gettop(L);
    if (!plainTable(L, styleref) || (lua_toboolean(L, furistyle) && !plainTable(L, furistyle)))
        return 0;

    g_prepared.reset();
    lua_pushcfunction(L, prepareLine);
    lua_pushvalue(L, line);
    lua_pushvalue(L, styleref);
    lua_pushvalue(L, furistyle);
    lua_pushnumber(L, *resX);
    lua_pushnumber(L, *resY);
    lua_pushnumber(L, *factor);
    lua_pushlstring(L, name.data(), name.size());
    if (lua_pcall(L, 7, 0, 0) != 0 || !g_prepared || !sameAsTables(L, line, styleref, g_prepared->layout)) {
        g_prepared.reset();
        return 0;
    }
    const kl::Line layout = std::move(g_prepared->layout);
    g_prepared.reset();
    if (!found)
        debugOut(L, 2, "WARNING: Style not found: " + name + "\n");
    fillAsKaraskel(L, line, styleref, furistyle, layout);
    lua_pushboolean(L, true);
    return 1;
}

}

void install(lua_State *L)
{
    lua_getfield(L, -1, "__karaskel_impl");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "__karaskel_impl");
    }
    lua_pushcfunction(L, collectHead);
    lua_setfield(L, -2, "collect_head");
    lua_pushcfunction(L, preprocLine);
    lua_setfield(L, -2, "preproc_line");
    lua_pop(L, 1);
}

}
