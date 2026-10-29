#pragma once

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

// printf with `format`, trailing zeros and the point removed unless the
// format ends in ".0f", leading spaces trimmed.
std::u16string getfloat(float num, std::string_view format = "5.3f");
// The whole text as a C-locale number; `out` is left alone when it is not one.
bool toDouble(std::u16string_view text, double &out);
// The leading number, 0 without one.
double atof(std::u16string_view text);
int atoi(std::u16string_view text);

struct FindData {
    std::u16string finding;
    long x = 0, y = 0;
    bool inBracket = false;
};

// findTag's last result feeds replace(), getDouble() and the others.
class TagFind {
public:
    // With one Line selected, mode 0 searches from this selection; with
    // several it is 0.
    void setSelection(long from, long to)
    {
        m_selFrom = from;
        m_selTo = to;
    }
    void setSeveralLines(bool several) { m_several = several; }

    // Mode 0 from the selection, 1 from position 0.
    bool findTag(std::u16string_view pattern, std::u16string_view text, int mode = 0);
    // `func` gets each match of "\\"+pattern's first group and gives its new
    // value; with returnPosWhenNoTags and no match in the first block,
    // "\"+tag+value goes into the first block or a new one.
    int replaceAll(std::u16string_view pattern, std::u16string_view tag, std::u16string &text,
                   const std::function<void(const FindData &, std::u16string &)> &func, bool returnPosWhenNoTags = false);
    // The last findTag's place takes `replacement`.
    int replace(std::u16string_view replacement, std::u16string &text) const;
    const FindData &result() const { return m_result; }
    void setResult(const FindData &result) { m_result = result; }
    std::pair<long, long> selection() const { return {m_selFrom, m_selTo}; }
    std::pair<long, long> positionInText() const { return {m_result.x, m_result.y}; }
    bool getDouble(double &out) const;
    bool getTwoValueDouble(double &a, double &b) const;
    bool getTextResult(std::u16string &out) const;

private:
    FindData m_result;
    long m_selFrom = 0, m_selTo = 0;
    bool m_several = false;
};

// 1 when a new block was made.
int changeText(std::u16string &text, std::u16string_view what, bool inBracket, long x, long y);

// `active` has the pending draft applied; `editorText` is the text editor's,
// or the original's in translation mode when the translation is empty.
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
// `withStyles` false leaves `styles` empty, for a caller keeping them decoded.
Context context(const VisualHost &host, bool withStyles = true);

// The translation when there is one, else the text.
std::u16string lineText(const core::LineRecord &line);
bool editsTranslation(const core::LineRecord &line);
const core::LineRecord *findLine(const core::Document &document, core::LineId id);

// The first Style of that name, else the first, else a default Style.
core::StyleValues lineStyle(const Context &context, std::u8string_view name);
// toDouble(), else atoi().
double styleNumber(const std::u8string &value, double initial);
PointF defaultPosition(const core::LineRecord &line, const core::StyleValues &style, int an, int width, int height);

// The editor text's first \pos or \move (else the default position), the
// scale (fscx/fscy at the selection's block, or from position 0 with
// `fromStart`, else the Style's) and the alignment. `tbl` holds 7 \move
// values the caller keeps between calls.
PointF posnScale(const Context &context, TagFind &find, bool fromStart, PointF *scale, int *an, double *tbl);
std::pair<int, int> moveTimes(const Context &context);
// Changes `tbl`.
PointF calcMovePos(const Context &context, double *tbl, int start, int end);

// The first \pos or \move start of `line` and the tag's place, else the
// default position plus additionalPosition(), the tag then going into the
// first block or a new one (`putInBracket`).
struct LinePosition {
    PointF pos;
    long textX = 0, textY = 0;
    bool putInBracket = false;
};
LinePosition linePosition(const Context &context, const core::LineRecord &line);
PointF additionalPosition(const Context &context, const core::LineRecord &line);

// `style` null: the active Line's Style.
struct TextSize {
    PointF size;
    PointF border;
};
TextSize textSize(const Context &context, const core::LineRecord &line, const core::StyleValues *style,
                  bool keepExtraLead);
// The rectangle `line`'s text covers, its border included, in script pixels
// (top-left, bottom-right); none for a Line with no text to measure.
std::optional<std::pair<PointF, PointF>> lineBox(const Context &context, const core::LineRecord &line);
PointF drawingSize(std::u16string_view drawing, PointF *position);

struct ClipPoint {
    float x = 0, y = 0;
    std::u16string type;
    bool start = false;
};
std::vector<ClipPoint> vectorPoints(std::u16string_view vector);
PointF rotateZ(PointF point, float sinOfAngle, float cosOfAngle, PointF pivot);

// For a several-Line target: `coordx`/`coordy` added to its \org, or to its
// position (a \pos then put in) without one.
void changeOrg(const Context &context, TagFind &find, std::u16string &text, const core::LineRecord &line,
               float coordx, float coordy);

// The first \clip or \iclip moved about `pivot`, a rectangle written as a
// vector. A vector clip keeps its scale and a rectangle its size.
void changeClipScale(TagFind &find, std::u16string &text, PointF pivot, float scalex, float scaley);
void changeClipRotationZ(TagFind &find, std::u16string &text, PointF pivot, float sinus, float cosinus);

// For a several-Line target whose active Line has a \move with 3 or more values.
std::u16string moveText(PointF pos, const double *tbl, int lineStartMs);

// In device pixels; drawArrow returns where the line should end.
inline constexpr std::uint32_t kHandleFill = 0xAA121150;
inline constexpr std::uint32_t kHandleSelectedFill = 0xAAFCE6B1;
inline constexpr std::uint32_t kHandleBorder = 0xFFBB0000;
PointF drawArrow(Overlay &out, PointF from, PointF to, int diff = 0);
void drawRect(Overlay &out, PointF pos, bool selected, float size);

// A press holds its own button; `moving` is a move with no button held.
struct Mouse {
    bool click = false, holding = false, leftc = false, rightc = false, middlec = false;
    bool buttonUp = false, moving = false, leftIsDown = false, shift = false;
    int x = 0, y = 0;
};
Mouse mouse(const Pointer &event);

// Any target set other than the active Line alone.
bool severalLines(const std::vector<core::LineId> &targets, std::optional<core::LineId> active);

int startMs(const core::LineRecord &line);
int endMs(const core::LineRecord &line);
// Truncated to 10 ms.
inline int zeroit(int ms) { return (ms / 10) * 10; }

}
