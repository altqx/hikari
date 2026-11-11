#include "karaskel_tables.h"

#include "lua_helper_internal.h"
#include "lua_host_cache.h"

extern "C" {
#include <lauxlib.h>
}

#include <cstdio>
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
    lua_createtable(L, 0, 3);
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

Head pushKaraskelHead(lua_State *L, int subs, bool generateFurigana)
{
    subs = absolute(L, subs);
    const double scale = furiganaScale(L);
    lua_createtable(L, 0, 3);
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
    int size = static_cast<int>(lua_objlen(L, subs));
    if (luaL_callmeta(L, subs, "__len")) {
        size = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }
    for (int i = 1; i <= size; ++i) {
        if (cancelled(L))
            luaL_error(L, "User cancelled");
        lua_pushinteger(L, i);
        lua_gettable(L, subs);
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
    Head head;
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

void pushKaraskelLine(lua_State *L, int styles, int line, const kl::Line &layout)
{
    styles = absolute(L, styles);
    line = absolute(L, line);
    const kl::KaraLine &k = layout.kara;
    lua_helper::fillKaraokeData(L, line, layout.macro);

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
    const int furistyle = lua_gettop(L);

    lua_createtable(L, static_cast<int>(layout.karaSyllables.size()), 1);
    const int kara = lua_gettop(L);
    lua_createtable(L, static_cast<int>(layout.karaFuri.size()), 1);
    const int furi = lua_gettop(L);
    setNumber(L, kara, "n", static_cast<double>(layout.karaSyllables.size()) - 1);
    setNumber(L, furi, "n", static_cast<double>(layout.karaFuri.size()));

    for (std::size_t s = 0; s < layout.karaSyllables.size(); ++s) {
        const kl::KaraSyllable &syl = layout.karaSyllables[s];
        lua_newtable(L);
        const int t = lua_gettop(L);
        lua_createtable(L, static_cast<int>(syl.highlights.size()), 1);
        setNumber(L, -1, "n", static_cast<double>(syl.highlights.size()));
        for (std::size_t h = 0; h < syl.highlights.size(); ++h) {
            pushHighlight(L, syl.highlights[h]);
            lua_rawseti(L, -2, static_cast<int>(h) + 1);
        }
        lua_setfield(L, t, "highlights");
        lua_createtable(L, static_cast<int>(syl.furi.size()), 1);
        setNumber(L, -1, "n", static_cast<double>(syl.furi.size()));
        lua_setfield(L, t, "furi");
        setString(L, t, "text", syl.text);
        setNumber(L, t, "duration", syl.duration);
        setNumber(L, t, "kdur", syl.kdur);
        setNumber(L, t, "start_time", syl.start);
        setNumber(L, t, "end_time", syl.end);
        setString(L, t, "tag", syl.tag);
        setValue(L, t, "line", line);
        setNumber(L, t, "i", syl.i);
        setString(L, t, "text_stripped", syl.stripped);
        setString(L, t, "inline_fx", syl.inlineFx);
        setString(L, t, "text_spacestripped", syl.spaceStripped);
        setString(L, t, "prespace", syl.prespace);
        setString(L, t, "postspace", syl.postspace);
        setValue(L, t, "style", styleref);
        setNumber(L, t, "width", syl.width);
        setNumber(L, t, "height", syl.height);
        setNumber(L, t, "prespacewidth", syl.prespaceWidth);
        setNumber(L, t, "postspacewidth", syl.postspaceWidth);
        setNumber(L, t, "left", syl.left);
        setNumber(L, t, "center", syl.center);
        setNumber(L, t, "right", syl.right);
        lua_rawseti(L, kara, static_cast<int>(s));
    }
    for (std::size_t f = 0; f < layout.karaFuri.size(); ++f) {
        const kl::KaraFuri &part = layout.karaFuri[f];
        lua_rawgeti(L, kara, part.i);
        const int syl = lua_gettop(L);
        lua_newtable(L);
        const int t = lua_gettop(L);
        setValue(L, t, "syl", syl);
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
        lua_createtable(L, 1, 1);
        setNumber(L, -1, "n", 1);
        lua_getfield(L, syl, "highlights");
        lua_rawgeti(L, -1, static_cast<int>(part.highlight) + 1);
        lua_rawseti(L, -3, 1);
        lua_pop(L, 1);
        lua_setfield(L, t, "highlights");
        setBoolean(L, t, "isfuri", true);
        if (k.hasFuriStyle) {
            setValue(L, t, "style", furistyle);
            setNumber(L, t, "width", part.width);
            setNumber(L, t, "height", part.height);
            setNumber(L, t, "prespacewidth", 0);
            setNumber(L, t, "postspacewidth", 0);
            setNumber(L, t, "left", part.left);
            setNumber(L, t, "center", part.center);
            setNumber(L, t, "right", part.right);
        }
        lua_getfield(L, syl, "furi");
        const int own = static_cast<int>(lua_objlen(L, -1)) + 1;
        lua_pushvalue(L, t);
        lua_rawseti(L, -2, own);
        setNumber(L, -1, "n", own);
        lua_pop(L, 1);
        lua_rawseti(L, furi, static_cast<int>(f) + 1);
        lua_pop(L, 1);
    }

    setValue(L, line, "kara", kara);
    setValue(L, line, "furi", furi);
    setString(L, line, "text_stripped", k.textStripped);
    setNumber(L, line, "duration", k.duration);
    setValue(L, line, "styleref", styleref);
    setNumber(L, line, "width", k.width);
    setNumber(L, line, "height", k.height);
    setNumber(L, line, "descent", k.descent);
    setNumber(L, line, "extlead", k.extlead);
    setValue(L, line, "furistyle", furistyle);
    lua_getfield(L, line, "margin_t");
    lua_setfield(L, line, "margin_v");
    setNumber(L, line, "eff_margin_l", k.effMarginL);
    setNumber(L, line, "eff_margin_r", k.effMarginR);
    setNumber(L, line, "eff_margin_t", k.effMarginT);
    setNumber(L, line, "eff_margin_b", k.effMarginB);
    setNumber(L, line, "eff_margin_v", k.effMarginV);
    if (!k.halign.empty()) {
        setNumber(L, line, "left", k.left);
        setNumber(L, line, "center", k.center);
        setNumber(L, line, "right", k.right);
        setNumber(L, line, "x", k.x);
        setString(L, line, "halign", k.halign);
        setNumber(L, line, "hcenter", k.center);
        setNumber(L, line, "top", k.top);
        setNumber(L, line, "middle", k.middle);
        setNumber(L, line, "bottom", k.bottom);
        setNumber(L, line, "y", k.y);
        setString(L, line, "valign", k.valign);
        setNumber(L, line, "vcenter", k.middle);
    }
    lua_settop(L, styleref - 1);
    lua_pushvalue(L, line);
}

namespace {

int collectHead(lua_State *L)
{
    pushKaraskelHead(L, 1, lua_toboolean(L, 2) != 0);
    return 2;
}

int preprocLine(lua_State *L)
{
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_checktype(L, 4, LUA_TTABLE);
    lua_pushvalue(L, 4);
    const auto dialogue = lua_helper::dialogueOf(L);
    lua_pop(L, 1);
    if (!dialogue)
        return luaL_error(L, "preproc_line needs a dialogue line");
    std::optional<application::Resolution> video;
    lua_getglobal(L, "aegisub");
    lua_getfield(L, -1, "video_size");
    lua_remove(L, -2);
    lua_call(L, 0, 2);
    if (lua_toboolean(L, -1))
        video = application::Resolution{static_cast<int>(lua_tonumber(L, -2)), static_cast<int>(lua_tonumber(L, -1))};
    lua_pop(L, 2);
    kl::Layout layout = stagedLayout(video);
    std::vector<std::string> problems;
    pushKaraskelLine(L, 3, 4, layLine(L, layout, *dialogue, 0, problems));
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
