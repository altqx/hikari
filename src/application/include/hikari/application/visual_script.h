#pragma once

// Coordinates are floats; texts are UTF-16 code units.

#include "hikari/application/legacy_timebase.h"
#include "hikari/application/visual_tools.h"
#include "hikari/core/style.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application::visual {

struct ScriptState {
    const core::Document *document = nullptr;
    std::vector<core::StyleValues> styles;
    int width = 0, height = 0;
    int timeMs = 0;
    LegacyTimebase timebase;
    TextMeasurePort *measure = nullptr;
    bool ignoreFiltered = false;
    std::optional<core::LineRecord> active; // with its pending draft applied
    std::function<void(std::u16string_view)> log;
};
ScriptState scriptState(const VisualHost &host);

// The active Line with its pending draft, any other Line as committed.
const core::LineRecord *scriptLine(const ScriptState &state, core::LineId id);

// The translation when it is not empty, else the text.
bool editsTranslation(const core::LineRecord &line);
std::u16string lineText(const core::LineRecord &line);

// The first Style of that name, else the first Style, else a default Style.
core::StyleValues lineStyle(const ScriptState &state, std::u8string_view name);
PointF defaultPosition(const core::LineRecord &line, const core::StyleValues &style, int an, int width, int height);

// The \move end point, its times made absolute, and how many values followed the start point.
struct MoveTable {
    std::array<double, 4> values{}; // x2, y2, t1, t2
    int count = 0;
};
// The first \pos or \move start point and the tag's place, or the default position with the
// stacking offset (the tag then going into the first block or a new one).
struct LinePosition {
    PointF pos;
    std::size_t textStart = 0, textLength = 0;
    bool putInBracket = false;
    std::optional<MoveTable> move; // with withMove, a \move with values after its start
};
LinePosition linePosition(const ScriptState &state, const core::LineRecord &line, bool withMove);

// The offset stacking the Line below other unpositioned Lines.
PointF additionalPosition(const ScriptState &state, const core::LineRecord &line);

// The editor text's first \pos or \move (or the default position), its alignment, and `table`
// (7 values the caller keeps between calls). \move times are made absolute, 0 when it has none.
PointF posnScale(const ScriptState &state, Family visual, const core::LineRecord &editLine,
                 std::u16string_view editorText, int *an, double *table);

// In absolute ms, as libass moves: a \move without times, or with both times 0 or less, moves
// over the whole Line; otherwise over its own times, the earlier first.
std::pair<int, int> moveInterval(const double *table, const core::LineRecord &editLine);

// The \move times relative to the Line's start, from the first and last frame the Line shows.
std::pair<int, int> moveTimes(const ScriptState &state, const core::LineRecord &editLine);

// The point along the \move at `time` (table: x2, y2, t1, t2).
PointF calcMovePosition(PointF point, const double *table, int time);

// Without a text measure (or when it fails) the text adds nothing and an error is logged.
struct TextSize {
    PointF size;
    PointF border;
    PointF extraLead;
    PointF drawingPosition;
    std::array<PointF, 2> bordShad;
};
TextSize textSize(const ScriptState &state, const core::LineRecord &line, const core::StyleValues *style,
                  bool keepExtraLead);
PointF drawingSize(std::u16string_view drawing, PointF *position);

int changeText(std::u16string &text, std::u16string_view what, bool inBracket, long x, long y);
int replaceTag(std::u16string &text, std::u16string_view replacement, bool inBracket, long x, long y);

inline int zeroit(int ms) { return (ms / 10) * 10; }
int startMs(const core::LineRecord &line);
int endMs(const core::LineRecord &line);

inline constexpr std::uint32_t kHandleFill = 0xAA121150;
inline constexpr std::uint32_t kHandleSelectedFill = 0xAAFCE6B1;
inline constexpr std::uint32_t kHandleBorder = 0xFFBB0000;
inline constexpr std::uint32_t kHelperColour = 0xFFFF00FF;
void drawRect(Overlay &out, PointF pos, bool selected = false, float size = 5.0f);
void drawCircle(Overlay &out, PointF pos, bool selected = false, float size = 6.0f);
void drawCross(Overlay &out, PointF pos, std::uint32_t colour = 0xFFFF0000);
// Draws the head at `to`; returns where the line should end.
PointF drawArrow(Overlay &out, PointF from, PointF to, int diff = 0);
void drawDashedLine(Overlay &out, const PointF *points, std::size_t count, int dashLength = 4,
                    std::uint32_t colour = kHandleBorder);
// Dashed lines across the video rectangle and a small square.
void drawHelperLine(Overlay &out, int x, int y, const VideoView &view);

} // namespace hikari::application::visual
