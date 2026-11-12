#include "util_impl.h"

#include <cmath>
#include <cstddef>
#include <string_view>

namespace hikari::util_impl {

namespace {

// Lua pattern %s and %x as LuaJIT's lj_char tables give them: the C locale.
bool isSpace(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::string_view stringArg(lua_State *L)
{
    std::size_t len = 0;
    const char *s = lua_tolstring(L, 1, &len);
    return {s, len};
}

// Two hex digits at `at`, or -1.
int hexPair(std::string_view s, std::size_t at)
{
    if (at + 2 > s.size())
        return -1;
    const int hi = hexValue(s[at]), lo = hexValue(s[at + 1]);
    return hi < 0 || lo < 0 ? -1 : hi * 16 + lo;
}

// The first "&H" followed by `pairs` hex pairs and, when `closed`, "&"; their values into `out`.
bool findAmpersandForm(std::string_view s, int pairs, bool closed, int *out)
{
    for (std::size_t at = s.find("&H"); at != std::string_view::npos; at = s.find("&H", at + 1)) {
        bool ok = true;
        for (int k = 0; k < pairs && ok; ++k) {
            out[k] = hexPair(s, at + 2 + 2 * static_cast<std::size_t>(k));
            ok = out[k] >= 0;
        }
        const std::size_t end = at + 2 + 2 * static_cast<std::size_t>(pairs);
        if (ok && (!closed || (end < s.size() && s[end] == '&')))
            return true;
    }
    return false;
}

// '#(%x%x)(%x?%x?)(%x?%x?)(%x?%x?)': each optional group as tonumber(group, 16) or 0.
bool findHashForm(std::string_view s, double *out)
{
    for (std::size_t at = s.find('#'); at != std::string_view::npos; at = s.find('#', at + 1)) {
        const int first = hexPair(s, at + 1);
        if (first < 0)
            continue;
        out[0] = first;
        std::size_t next = at + 3;
        for (int k = 1; k < 4; ++k) {
            int value = 0, digits = 0;
            while (digits < 2 && next < s.size() && hexValue(s[next]) >= 0) {
                value = value * 16 + hexValue(s[next]);
                ++digits;
                ++next;
            }
            out[k] = value;
        }
        return true;
    }
    return false;
}

void pushNumbers(lua_State *L, double r, double g, double b, double a)
{
    lua_pushnumber(L, r);
    lua_pushnumber(L, g);
    lua_pushnumber(L, b);
    lua_pushnumber(L, a);
}

int extractColor(lua_State *L)
{
    const std::string_view s = stringArg(L);
    int v[4];
    if (findAmpersandForm(s, 4, false, v)) {
        pushNumbers(L, v[3], v[2], v[1], v[0]);
        return 4;
    }
    if (findAmpersandForm(s, 3, true, v)) {
        pushNumbers(L, v[2], v[1], v[0], 0);
        return 4;
    }
    if (findAmpersandForm(s, 1, true, v)) {
        pushNumbers(L, 0, 0, 0, v[0]);
        return 4;
    }
    double hash[4];
    if (findHashForm(s, hash)) {
        pushNumbers(L, hash[0], hash[1], hash[2], hash[3]);
        return 4;
    }
    return 0;
}

int trim(lua_State *L)
{
    const std::string_view s = stringArg(L);
    std::size_t begin = 0, end = s.size();
    while (begin < end && isSpace(s[begin]))
        ++begin;
    while (end > begin && isSpace(s[end - 1]))
        --end;
    lua_pushlstring(L, s.data() + begin, end - begin);
    lua_pushinteger(L, 1);
    return 2;
}

int headtail(lua_State *L)
{
    const std::string_view s = stringArg(L);
    std::size_t space = 0;
    while (space < s.size() && !isSpace(s[space]))
        ++space;
    if (space == s.size()) {
        lua_pushvalue(L, 1);
        lua_pushliteral(L, "");
        return 2;
    }
    std::size_t tail = space;
    while (tail < s.size() && isSpace(s[tail]))
        ++tail;
    lua_pushlstring(L, s.data(), space);
    lua_pushlstring(L, s.data() + tail, s.size() - tail);
    return 2;
}

double clamp(double val, double min, double max)
{
    if (val < min)
        return min;
    if (val > max)
        return max;
    return val;
}

// LuaJIT's a % b.
double luaMod(double a, double b)
{
    return a - std::floor(a / b) * b;
}

int hslToRgb(lua_State *L)
{
    const double H = luaMod(std::fabs(lua_tonumber(L, 1)), 360);
    const double S = clamp(lua_tonumber(L, 2), 0, 1);
    const double Lum = clamp(lua_tonumber(L, 3), 0, 1);
    double r = Lum, g = Lum, b = Lum;
    if (S != 0) {
        const double Q = Lum < 0.5 ? Lum * (1.0 + S) : Lum + S - (Lum * S);
        const double P = 2.0 * Lum - Q;
        const double Hk = H / 360;
        double Tr, Tg, Tb;
        if (Hk < 1.0 / 3) {
            Tr = Hk + 1.0 / 3;
            Tg = Hk;
            Tb = Hk + 2.0 / 3;
        } else if (Hk > 2.0 / 3) {
            Tr = Hk - 2.0 / 3;
            Tg = Hk;
            Tb = Hk - 1.0 / 3;
        } else {
            Tr = Hk + 1.0 / 3;
            Tg = Hk;
            Tb = Hk - 1.0 / 3;
        }
        const auto component = [&](double T) {
            if (T < 1.0 / 6)
                return P + ((Q - P) * 6.0 * T);
            if (1.0 / 6 <= T && T < 1.0 / 2)
                return Q;
            if (1.0 / 2 <= T && T < 2.0 / 3)
                return P + ((Q - P) * (2.0 / 3 - T) * 6.0);
            return P;
        };
        r = component(Tr);
        g = component(Tg);
        b = component(Tb);
    }
    lua_pushnumber(L, std::floor(r * 255 + 0.5));
    lua_pushnumber(L, std::floor(g * 255 + 0.5));
    lua_pushnumber(L, std::floor(b * 255 + 0.5));
    return 3;
}

}

void install(lua_State *L)
{
    lua_createtable(L, 0, 4);
    lua_pushcfunction(L, extractColor);
    lua_setfield(L, -2, "extract_color");
    lua_pushcfunction(L, trim);
    lua_setfield(L, -2, "trim");
    lua_pushcfunction(L, headtail);
    lua_setfield(L, -2, "headtail");
    lua_pushcfunction(L, hslToRgb);
    lua_setfield(L, -2, "HSL_to_RGB");
    lua_setfield(L, -2, "__util_impl");
}

}
