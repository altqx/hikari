#pragma once

extern "C" {
#include <lua.h>
}

// aegisub.__shape_impl, the shape kernel for scripts. Each function answers true and its results, or false
// when the kernel does not reproduce the call (Yutils then runs its Lua):
//   bounding(shape) -> true, x0, y0, x1, y1 (nothing after true for a shape without points)
//   flatten(shape[, tolerance]), move(shape, x, y), transform(shape, matrix data: 16 numbers)
//   to_outline(shape, width_xy[, width_y][, mode]) -> true, shape
//   to_pixels(shape) -> true, {{alpha, x, y}, ...}
//   combine(a, b, "unite"|"subtract"|"intersect"|"exclude"), offset(shape, delta[, "miter"|"round"|"square"])
//     -> true, shape (filled nonzero; not part of Yutils)
//   text_outline(family, bold, italic, underline, strikeout, size, xscale, yscale, hspace, text)
//     -> true, shape, width, height, ascent, descent (the host's fonts through FreeType)
namespace hikari::shape_tables {

// Installs the table on the aegisub table at the stack top; `textOutline` asks the host.
void install(lua_State *L, lua_CFunction textOutline);

}
