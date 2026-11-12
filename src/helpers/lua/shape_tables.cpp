#include "shape_tables.h"

#include "hikari/application/shape_kernel.h"

extern "C" {
#include <lauxlib.h>
}

#include <string_view>

namespace hikari::shape_tables {

namespace {

namespace shape = application::shape;
namespace yutils = application::shape::yutils;

int answerNo(lua_State *L)
{
    lua_pushboolean(L, 0);
    return 1;
}

std::optional<std::string_view> stringArg(lua_State *L, int i)
{
    if (lua_type(L, i) != LUA_TSTRING)
        return std::nullopt;
    std::size_t size = 0;
    const char *s = lua_tolstring(L, i, &size);
    return std::string_view(s, size);
}

std::optional<double> numberArg(lua_State *L, int i)
{
    if (lua_type(L, i) != LUA_TNUMBER)
        return std::nullopt;
    return lua_tonumber(L, i);
}

int answerText(lua_State *L, const std::optional<std::string> &text)
{
    if (!text)
        return answerNo(L);
    lua_pushboolean(L, 1);
    lua_pushlstring(L, text->data(), text->size());
    return 2;
}

int bounding(lua_State *L)
{
    const auto s = stringArg(L, 1);
    if (!s)
        return answerNo(L);
    const auto b = yutils::bounding(*s);
    if (!b)
        return answerNo(L);
    lua_pushboolean(L, 1);
    if (!b->bounds)
        return 1;
    for (const double v : {b->bounds->x0, b->bounds->y0, b->bounds->x1, b->bounds->y1})
        lua_pushnumber(L, v);
    return 5;
}

int flatten(lua_State *L)
{
    const auto s = stringArg(L, 1);
    const auto tolerance = lua_isnoneornil(L, 2) ? std::optional(1.0) : numberArg(L, 2);
    if (!s || !tolerance)
        return answerNo(L);
    return answerText(L, yutils::flatten(*s, *tolerance));
}

int move(lua_State *L)
{
    const auto s = stringArg(L, 1);
    const auto x = numberArg(L, 2), y = numberArg(L, 3);
    if (!s || !x || !y)
        return answerNo(L);
    return answerText(L, yutils::move(*s, *x, *y));
}

int transform(lua_State *L)
{
    const auto s = stringArg(L, 1);
    if (!s || !lua_istable(L, 2) || lua_objlen(L, 2) != 16)
        return answerNo(L);
    yutils::Matrix m{};
    for (int i = 0; i < 16; ++i) {
        lua_rawgeti(L, 2, i + 1);
        const auto v = numberArg(L, -1);
        lua_pop(L, 1);
        if (!v)
            return answerNo(L);
        m[static_cast<std::size_t>(i)] = *v;
    }
    return answerText(L, yutils::transform(*s, m));
}

int toOutline(lua_State *L)
{
    const auto s = stringArg(L, 1);
    const auto width = numberArg(L, 2);
    if (!s || !width)
        return answerNo(L);
    std::optional<double> widthY;
    if (!lua_isnoneornil(L, 3)) {
        widthY = numberArg(L, 3);
        if (!widthY)
            return answerNo(L);
    }
    auto mode = yutils::OutlineMode::Round;
    if (!lua_isnoneornil(L, 4)) {
        const auto name = stringArg(L, 4);
        if (name == "bevel")
            mode = yutils::OutlineMode::Bevel;
        else if (name == "miter")
            mode = yutils::OutlineMode::Miter;
        else if (name != "round")
            return answerNo(L);
    }
    return answerText(L, yutils::toOutline(*s, *width, widthY, mode));
}

int toPixels(lua_State *L)
{
    const auto s = stringArg(L, 1);
    if (!s)
        return answerNo(L);
    const auto pixels = yutils::toPixels(*s);
    if (!pixels)
        return answerNo(L);
    lua_pushboolean(L, 1);
    lua_createtable(L, static_cast<int>(pixels->size()), 0);
    int i = 0;
    for (const auto &p : *pixels) {
        lua_createtable(L, 0, 3);
        lua_pushnumber(L, p.alpha);
        lua_setfield(L, -2, "alpha");
        lua_pushnumber(L, p.x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, p.y);
        lua_setfield(L, -2, "y");
        lua_rawseti(L, -2, ++i);
    }
    return 2;
}

int combine(lua_State *L)
{
    const auto a = stringArg(L, 1), b = stringArg(L, 2), name = stringArg(L, 3);
    if (!a || !b || !name)
        return answerNo(L);
    shape::Combine op;
    if (name == "unite")
        op = shape::Combine::Unite;
    else if (name == "subtract")
        op = shape::Combine::Subtract;
    else if (name == "intersect")
        op = shape::Combine::Intersect;
    else if (name == "exclude")
        op = shape::Combine::Exclude;
    else
        return answerNo(L);
    return answerText(L, shape::writeDrawing(shape::combine(shape::readDrawing(*a), shape::readDrawing(*b), op), 3));
}

int offset(lua_State *L)
{
    const auto s = stringArg(L, 1);
    const auto delta = numberArg(L, 2);
    if (!s || !delta)
        return answerNo(L);
    auto join = shape::Join::Round;
    if (!lua_isnoneornil(L, 3)) {
        const auto name = stringArg(L, 3);
        if (name == "miter")
            join = shape::Join::Miter;
        else if (name == "square")
            join = shape::Join::Square;
        else if (name != "round")
            return answerNo(L);
    }
    return answerText(L, shape::writeDrawing(shape::offset(shape::readDrawing(*s), *delta, join), 3));
}

}

void install(lua_State *L, lua_CFunction textOutline)
{
    lua_createtable(L, 0, 10);
    const std::pair<const char *, lua_CFunction> functions[] = {
        {"bounding", bounding},    {"flatten", flatten},   {"move", move},       {"transform", transform},
        {"to_outline", toOutline}, {"to_pixels", toPixels}, {"combine", combine}, {"offset", offset},
        {"text_outline", textOutline}};
    for (const auto &[name, fn] : functions) {
        lua_pushcfunction(L, fn);
        lua_setfield(L, -2, name);
    }
    lua_setfield(L, -2, "__shape_impl");
}

}
