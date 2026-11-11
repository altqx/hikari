#pragma once

// AU21 position and alignment commands. Each works on the edited text of a Line (its translation
// when it has one) and writes numbers to 2 decimals unless the ported script says otherwise.

#include "hikari/application/edit_session.h"
#include "hikari/application/grid_commands.h"
#include "hikari/core/document.h"
#include "hikari/core/style.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application::position {

struct Size {
    double width = 0, height = 0;
};

struct Box {
    double left = 0, top = 0, right = 0, bottom = 0;
};

// `width` and `height` are the script resolution. `size` is a Line's text box (a drawing's bounds),
// border and shadow left out; none when there is nothing to measure.
struct Geometry {
    int width = 0, height = 0;
    std::function<core::StyleValues(const core::LineRecord &)> style;
    std::function<std::optional<Size>(const core::LineRecord &)> size;
};

enum class Edge { Left, Centre, Right, Top, Middle, Bottom };
enum class RelativeTo { ActiveLine, VideoFrame };
enum class Axis { Horizontal, Vertical };
enum class Keep { Start, End };
enum class CycleTag { Blur, Bord, Shad, Alpha, An, Fsp };

// The box `line` covers where it is now: its first \pos or \move start, else its alignment and margins.
std::optional<Box> box(const core::LineRecord &line, const Geometry &geometry);

// ILL.ChangeAlign: \an becomes `an` and the position moves so the Line stays where it is.
std::u8string changeAlignment(const core::LineRecord &line, int an, const Geometry &geometry);
// The start block's first `name` tag ("\blur2"), \t bodies left out; empty when it has none.
std::u8string startTag(std::u8string_view text, std::u8string_view name);
// "{…}" when the text starts with an override block, else empty.
std::u8string startBlock(std::u8string_view text);
// `tag` replaces the start block's first `name` tag, else goes at the block's end, else in a new block.
std::u8string withStartTag(std::u8string_view text, std::u8string_view name, std::u8string_view tag);
// `block` replaces the start block, or goes before the text when it has none.
std::u8string withStartBlock(std::u8string_view text, std::u8string_view block);

// To 2 decimals, trailing zeros dropped.
std::u8string formatted(double value);

struct Parts {
    bool position = true, moveStart = true, moveEnd = true, clip = true, drawing = false, origin = true;
};
// The `parts` of the Line move by (dx, dy). A Line with neither \pos nor \move gets a \pos from its
// alignment and margins when `parts.position` is on.
std::u8string shifted(const core::LineRecord &line, double dx, double dy, const Geometry &geometry, Parts parts = {});

// petzku.PosToMargin. Each returns false when the Line is left as it was.
bool positionToMargins(core::LineRecord &line, const Geometry &geometry);
bool marginsToPosition(core::LineRecord &line, const Geometry &geometry);

// petzku.Phantom on each \N row: the UTF-16 `selection` of the raw text, else the {}…{} markers.
std::u16string phantom(std::u16string_view text, Keep keep, std::optional<std::pair<std::size_t, std::size_t>> selection);

// ua.Cycles on the start block, \t bodies left out: the next value of `values` (the previous with
// `back`), a value off the list going to the nearest larger one on it (smaller with `back`), else
// round to the first (last); a missing tag gets the first value. \alpha values are hex digits.
std::u8string cycle(std::u8string_view text, CycleTag tag, std::span<const std::u8string> values, bool back);
std::u8string_view cycleTagName(CycleTag tag);
std::vector<std::u8string> defaultCycleValues(CycleTag tag);

std::expected<void, CommandRefusal> changeAlignment(EditSession &session, int an, const Geometry &geometry,
                                                    const LineVisible &visible = {});
std::expected<void, CommandRefusal> align(EditSession &session, Edge edge, RelativeTo to, const Geometry &geometry,
                                          const LineVisible &visible = {});
// Needs three Lines; the outermost two on the axis stay and the centres between are evenly spaced.
std::expected<void, CommandRefusal> distribute(EditSession &session, Axis axis, const Geometry &geometry,
                                               const LineVisible &visible = {});
std::expected<void, CommandRefusal> positionToMargins(EditSession &session, const Geometry &geometry,
                                                      const LineVisible &visible = {});
std::expected<void, CommandRefusal> marginsToPosition(EditSession &session, const Geometry &geometry,
                                                      const LineVisible &visible = {});
// The active Line only; `selection` is in its edited text.
std::expected<void, CommandRefusal> phantom(EditSession &session, Keep keep,
                                            std::optional<std::pair<std::size_t, std::size_t>> selection);
std::expected<void, CommandRefusal> cycle(EditSession &session, CycleTag tag, std::span<const std::u8string> values,
                                          bool back, const LineVisible &visible = {});

}
