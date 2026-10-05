#pragma once

// T2: what the legacy visual tools read from the script and draw, shared by
// the families (legacy Visuals, Visuals.cpp at 20d647c4): a Line's position
// and \move (GetPosition, GetPosnScale), the \move times (GetMoveTimes), a
// Line's default position (Dialogue::GetDefaultPosition) and its stacking
// below other unpositioned Lines (GetDialogueAdditionalPosition), the text
// size (GetTextSize, GetDrawingSize), the tag edits (TagFindReplace's
// ChangeText and Replace) and the handles (DrawRect, DrawCircle, DrawCross,
// DrawArrow, DrawDashedLine). Coordinates are floats as legacy's
// D3DXVECTOR2; texts are UTF-16 code units, as wxString on Windows.

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

// The script as Visuals saw it: the Document (tab->grid->file), its Styles,
// SubsSize (GetASSRes), the video's time (VideoBox::Tell) and Timebase, the
// active Line as the Line editor holds it (tab->edit->line: the pending
// draft applied), the Grid's ignoreFiltered and the text measure.
struct ScriptState {
    const core::Document *document = nullptr;
    std::vector<core::StyleValues> styles;
    int width = 0, height = 0;
    int timeMs = 0;
    LegacyTimebase timebase;
    TextMeasurePort *measure = nullptr;
    bool ignoreFiltered = false;
    std::optional<core::LineRecord> active;
    std::function<void(std::u16string_view)> log;
};
ScriptState scriptState(const VisualHost &host);

// A Line as Visuals reads it: the active Line with its pending draft, any
// other Line as committed.
const core::LineRecord *scriptLine(const ScriptState &state, core::LineId id);

// Dialogue::GetTextNoCopy / SetText: the translation when it is not empty,
// else the text (SubsDialogue.cpp:216-232).
bool editsTranslation(const core::LineRecord &line);
std::u16string lineText(const core::LineRecord &line);

// SubsFile::GetStyle(0, name): the first Style of that name, else the first
// Style, else a new Styles() (styles.cpp:249-275).
core::StyleValues lineStyle(const ScriptState &state, std::u8string_view name);
// Dialogue::GetDefaultPosition (SubsDialogue.cpp:609-634).
PointF defaultPosition(const core::LineRecord &line, const core::StyleValues &style, int an, int width, int height);

// GetPosition's \move table (PosData::moveTable): the end point, its times
// made absolute, and the number of values read after the start point.
struct MoveTable {
    std::array<double, 4> values{}; // x2, y2, t1, t2 (legacy moveTable[0..3])
    int count = 0;                  // moveTable[4]
};
// Visuals::GetPosition (Visuals.cpp:834-903): the first \pos or \move's
// start point and the tag's place (TextPos), or the default position with
// the stacking offset, the tag then going into the first block or a new one.
struct LinePosition {
    PointF pos;
    std::size_t textStart = 0, textLength = 0; // TextPos x, y
    bool putInBracket = false;
    std::optional<MoveTable> move; // with withMove, a \move with values after its start
};
LinePosition linePosition(const ScriptState &state, const core::LineRecord &line, bool withMove);

// Visuals::GetDialogueAdditionalPosition (Visuals.cpp:108-215).
PointF additionalPosition(const ScriptState &state, const core::LineRecord &line);

// Visuals::GetPosnScale (Visuals.cpp:624-722) for a family that does not ask
// for the scale: the editor text's first \pos or \move (or the default
// position), its alignment and the table `tbl` (7 values, kept by the caller
// between calls, as legacy's moveValues member).
PointF posnScale(const ScriptState &state, Family visual, const core::LineRecord &editLine,
                 std::u16string_view editorText, int *an, double *table);

// Visuals::GetMoveTimes (Visuals.cpp:607-622): the \move times relative to
// the Line's start, from the first and last frame the Line shows.
std::pair<int, int> moveTimes(const ScriptState &state, const core::LineRecord &editLine);

// CalcMovePosition (UtilsWindows.cpp:344-358): the point along the \move at
// `time` (table: x2, y2, t1, t2).
PointF calcMovePosition(PointF point, const double *table, int time);

// Visuals::GetTextSize (Visuals.cpp:953-1133) and GetDrawingSize
// (1135-1187). Without a text measure (or when it fails) the text adds
// nothing and "Cannot measure text: ..." is logged.
struct TextSize {
    PointF size;
    PointF border;                   // `border`
    PointF extraLead;                // `extralead`
    PointF drawingPosition;          // `drawingPosition`
    std::array<PointF, 2> bordShad;  // `bordshad`
};
TextSize textSize(const ScriptState &state, const core::LineRecord &line, const core::StyleValues *style,
                  bool keepExtraLead);
PointF drawingSize(std::u16string_view drawing, PointF *position);

// TagFindReplace::ChangeText (TagFindReplace.cpp:612-631).
int changeText(std::u16string &text, std::u16string_view what, bool inBracket, long x, long y);
// TagFindReplace::Replace (TagFindReplace.cpp:423-444) after FindTag found
// (x, y) inside a block or not.
int replaceTag(std::u16string &text, std::u16string_view replacement, bool inBracket, long x, long y);

// ZEROIT (SubsDialogue.h:20): a time in ms truncated to 10 ms.
inline int zeroit(int ms) { return (ms / 10) * 10; }
// A Line's start and end in whole ms (Dialogue Start.mstime).
int startMs(const core::LineRecord &line);
int endMs(const core::LineRecord &line);

// The handles, with legacy's colours (Visuals.cpp:305-423).
inline constexpr std::uint32_t kHandleFill = 0xAA121150;
inline constexpr std::uint32_t kHandleSelectedFill = 0xAAFCE6B1;
inline constexpr std::uint32_t kHandleBorder = 0xFFBB0000;
inline constexpr std::uint32_t kHelperColour = 0xFFFF00FF;
void drawRect(Overlay &out, PointF pos, bool selected = false, float size = 5.0f);
void drawCircle(Overlay &out, PointF pos, bool selected = false, float size = 6.0f);
void drawCross(Overlay &out, PointF pos, std::uint32_t colour = 0xFFFF0000);
// DrawArrow: draws the head at `to` and returns where the line should end.
PointF drawArrow(Overlay &out, PointF from, PointF to, int diff = 0);
void drawDashedLine(Overlay &out, const PointF *points, std::size_t count, int dashLength = 4,
                    std::uint32_t colour = kHandleBorder);
// The helper cross a middle click puts (Position and Move DrawVisual):
// dashed lines across the video rectangle and a small square.
void drawHelperLine(Overlay &out, int x, int y, const VideoView &view);

} // namespace hikari::application::visual
