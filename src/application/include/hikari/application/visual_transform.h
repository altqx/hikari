#pragma once

// T3: what the legacy Scale, RotationZ and RotationXY tools (VisualScale.cpp,
// VisualRotationZ.cpp, VisualRotationXY.cpp at 20d647c4) read from and write
// to the script through their Visuals and TagFindReplace bases: the active
// Line's position, \move table, scale and alignment (Visuals::GetPosnScale,
// GetMoveTimes, CalcMovePos), another Line's position (GetPosition with
// GetDialogueAdditionalPosition), the text's size (GetTextSize,
// GetDrawingSize), the \org edit (ChangeOrg), the tag search and rewrite
// (TagFindReplace::FindTag, Replace, ReplaceAll, ChangeText), the number
// texts (getfloat, wxString::ToCDouble, wxAtof) and the clip rewrite of the
// "preserve proportions" modes (ChangeClipScale, ChangeClipRotationZ).
//
// Texts are UTF-16 code units (wxString on Windows) and coordinates floats
// (D3DXVECTOR2), keeping legacy's float and double steps. The names live in
// visual::transform so they stay apart from T2's helpers of the same legacy
// functions until the families share one module.

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

namespace hikari::application::visual::transform {

// getfloat (config.cpp:1085-1101): printf with `format` ("5.3f", "6.0f",
// "5.0f"), trailing zeros and the point removed unless the format ends in
// ".0f", leading spaces trimmed. The value is a float, as legacy's parameter.
std::u16string getfloat(float num, std::string_view format = "5.3f");
// wxString::ToCDouble / ToDouble (the English interface): the whole text as
// a C-locale number; `out` is left alone when it is not one.
bool toDouble(std::u16string_view text, double &out);
// wxAtof / wxAtoi: the leading number, 0 without one.
double atof(std::u16string_view text);
int atoi(std::u16string_view text);

// TagFindReplace's FindData (TagFindReplace.h:34-48).
struct FindData {
    std::u16string finding;
    long x = 0, y = 0; // positionInText
    bool inBracket = false;
};

// TagFindReplace as a Visuals object holds it (one per tool): FindTag's last
// result feeds Replace, GetDouble and the others.
class TagFind {
public:
    // FindTag's selection: with one Line selected, mode 0 reads the editor's
    // selection (TagFindReplace.cpp:40-41); with several it is 0 (52-53).
    void setSelection(long from, long to)
    {
        m_selFrom = from;
        m_selTo = to;
    }
    void setSeveralLines(bool several) { m_several = several; }

    // TagFindReplace::FindTag (TagFindReplace.cpp:24-255) on `text`; mode 0
    // from the selection, 1 from position 0.
    bool findTag(std::u16string_view pattern, std::u16string_view text, int mode = 0);
    // ReplaceAll (257-304): `func` gets each match of "\\"+pattern's first
    // group and gives its new value; with returnPosWhenNoTags and no match in
    // the first block, "\"+tag+value goes into the first block or a new one.
    int replaceAll(std::u16string_view pattern, std::u16string_view tag, std::u16string &text,
                   const std::function<void(const FindData &, std::u16string &)> &func, bool returnPosWhenNoTags = false);
    // Replace (423-443): the last FindTag's place takes `replacement`.
    int replace(std::u16string_view replacement, std::u16string &text) const;
    const FindData &result() const { return m_result; }
    std::pair<long, long> selection() const { return {m_selFrom, m_selTo}; }
    std::pair<long, long> positionInText() const { return {m_result.x, m_result.y}; }
    bool getDouble(double &out) const;                     // 555-564
    bool getTwoValueDouble(double &a, double &b) const;    // 585-599
    bool getTextResult(std::u16string &out) const;        // 634-641

private:
    FindData m_result;
    long m_selFrom = 0, m_selTo = 0;
    bool m_several = false;
};

// TagFindReplace::ChangeText (612-632): 1 when a new block was made.
int changeText(std::u16string &text, std::u16string_view what, bool inBracket, long x, long y);

// What Visuals read: the Document and its Styles (tab->grid->file), SubsSize
// (GetASSRes), the video's time (Tell) and Timebase, the text measure, the
// Grid's ignoreFiltered, and the active Line as the Line editor holds it
// (tab->edit->line, the pending draft applied) with the editor's text
// (TextEdit, or TextEditOrig in TLMode when TextEdit is empty).
struct Context {
    const core::Document *document = nullptr;
    std::vector<core::StyleValues> styles;
    int width = 0, height = 0;
    int timeMs = 0;
    LegacyTimebase timebase;
    TextMeasurePort *measure = nullptr;
    bool ignoreFiltered = false;
    std::optional<core::LineRecord> active;
    std::u16string editorText;
    bool editorIsTranslation = false;
    std::function<void(std::u16string_view)> log;
};
Context context(const VisualHost &host);

// Dialogue::GetTextNoCopy / SetText (SubsDialogue.cpp:216-232).
std::u16string lineText(const core::LineRecord &line);
bool editsTranslation(const core::LineRecord &line);
// A target as Visuals' several-Line path reads it, in Document order.
const core::LineRecord *findLine(const core::Document &document, core::LineId id);

// SubsFile::GetStyle(0, name) (SubsFile.cpp:818-830): the first Style of
// that name, else the first, else a new Styles() (styles.cpp:249-275).
core::StyleValues lineStyle(const Context &context, std::u8string_view name);
// Styles::GetScaleXDouble and its siblings (styles.cpp:134-190): ToDouble,
// else wxAtoi.
double styleNumber(const std::u8string &value, double initial);
// Dialogue::GetDefaultPosition (SubsDialogue.cpp:609-634).
PointF defaultPosition(const core::LineRecord &line, const core::StyleValues &style, int an, int width, int height);

// Visuals::GetPosnScale (Visuals.cpp:624-722) for Scale and the rotations
// (Visual < VECTORCLIP, no drawing scale): the editor text's first \pos or
// \move (or the default position), the scale (fscx/fscy found with FindTag,
// at the selection's block or, with `fromStart`, from position 0, else the
// Style's), the alignment and the table `tbl` (legacy's moveValues member, 7
// values the caller keeps between calls).
PointF posnScale(const Context &context, TagFind &find, bool fromStart, PointF *scale, int *an, double *tbl);
// Visuals::GetMoveTimes (607-622).
std::pair<int, int> moveTimes(const Context &context);
// Visuals::CalcMovePos (547-564): `tbl` is changed as legacy's moveValues.
PointF calcMovePos(const Context &context, double *tbl, int start, int end);

// Visuals::GetPosition (834-903) without the \move table: the first \pos or
// \move's start point of `line` (GetText) and the tag's place, or the default
// position with GetDialogueAdditionalPosition's offset, the tag then going
// into the first block (1, 0) or a new one at 0 (`putInBracket`).
struct LinePosition {
    PointF pos;
    long textX = 0, textY = 0; // TextPos
    bool putInBracket = false;
};
LinePosition linePosition(const Context &context, const core::LineRecord &line);
// Visuals::GetDialogueAdditionalPosition (108-215).
PointF additionalPosition(const Context &context, const core::LineRecord &line);

// Visuals::GetTextSize (953-1133) with `style` (null: the active Line's
// Style) and keepExtraLead; the border is `border`.
struct TextSize {
    PointF size;
    PointF border;
};
TextSize textSize(const Context &context, const core::LineRecord &line, const core::StyleValues *style,
                  bool keepExtraLead);
// Visuals::GetDrawingSize (1135-1187).
PointF drawingSize(std::u16string_view drawing, PointF *position);

// ClipPoint (VisualClipPoint.h) as GetVectorPoints (566-605) makes them.
struct ClipPoint {
    float x = 0, y = 0;
    std::u16string type;
    bool start = false;
};
std::vector<ClipPoint> vectorPoints(std::u16string_view vector);
// Visuals::RotateZ (1189-1195).
PointF rotateZ(PointF point, float sinOfAngle, float cosOfAngle, PointF pivot);

// Visuals::ChangeOrg (905-934) on a several-Line target: `coordx`/`coordy`
// added to its \org, or to its position (a \pos then put in) without one.
void changeOrg(const Context &context, TagFind &find, std::u16string &text, const core::LineRecord &line,
               float coordx, float coordy);

// Scale::ChangeClipScale (VisualScale.cpp:741-827) and
// RotationZ::ChangeClipRotationZ (VisualRotationZ.cpp:455-547): the first
// \clip or \iclip moved about `pivot`, a rectangle written as a vector. Both
// keep a vector clip's scale (Scale moves its points about the pivot in the
// vector's units) and a rectangle's size (T3-scale-vector-clip,
// T3-rotz-rect-clip).
void changeClipScale(TagFind &find, std::u16string &text, PointF pivot, float scalex, float scaley);
void changeClipRotationZ(TagFind &find, std::u16string &text, PointF pivot, float sinus, float cosinus);

// The \move text Scale and RotationZ write for a several-Line target whose
// active Line has a \move with 3 or more values (VisualScale.cpp:448-455,
// VisualRotationZ.cpp:338-345).
std::u16string moveText(PointF pos, const double *tbl, int lineStartMs);

// The few legacy draw helpers these tools use, in device pixels
// (Visuals.cpp:305-374): DrawArrow returns where the line should end.
inline constexpr std::uint32_t kHandleFill = 0xAA121150;
inline constexpr std::uint32_t kHandleSelectedFill = 0xAAFCE6B1;
inline constexpr std::uint32_t kHandleBorder = 0xFFBB0000;
PointF drawArrow(Overlay &out, PointF from, PointF to, int diff = 0);
void drawRect(Overlay &out, PointF pos, bool selected, float size);

// A pointer event as wxMouseEvent answered the tools: LeftDown/RightDown/
// MiddleDown, the buttons held (a press holds its own button), ButtonUp,
// Moving (a move with no button held) and ShiftDown.
struct Mouse {
    bool click = false, holding = false, leftc = false, rightc = false, middlec = false;
    bool buttonUp = false, moving = false, leftIsDown = false, shift = false;
    int x = 0, y = 0;
};
Mouse mouse(const Pointer &event);

// Whether a gesture's targets take legacy's several-Line path
// (Visuals::SetVisual, Visuals.cpp:735: EditBox::IsCursorOnStart, more than
// one Line selected): any target set other than the active Line alone. The
// active Line alone is the Line editor's path.
bool severalLines(const std::vector<core::LineId> &targets, std::optional<core::LineId> active);

// A Line's start in whole ms (Dialogue Start.mstime) and ZEROIT
// (SubsDialogue.h:20).
int startMs(const core::LineRecord &line);
int endMs(const core::LineRecord &line);
inline int zeroit(int ms) { return (ms / 10) * 10; }

} // namespace hikari::application::visual::transform
