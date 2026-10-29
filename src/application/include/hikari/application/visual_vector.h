#pragma once

#include "hikari/application/visual_tools.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::visual {

// Qt::Key values; the application layer does not link Qt.
namespace keys {
inline constexpr int A = 0x41; // Ctrl+A selects every point
inline constexpr int Delete = 0x01000007;
} // namespace keys

// `type` is the drawing command ('m', 'l', 'b' or 's'); `start` marks the point that begins a command.
struct VectorPoint {
    float x = 0, y = 0;
    char16_t type = u'm';
    bool start = true;
    bool selected = false;
    bool operator==(const VectorPoint &) const = default;
};

namespace legacy {
// The whole text as a C-locale number.
bool cDouble(std::u16string_view text, double &out);
// Leading blanks, a sign and digits; saturated.
int atoi(std::u16string_view text);
// printf's "%<format>"; unless the format ends ".0f", trailing zeros and the point dropped; leading
// blanks trimmed.
std::u16string getfloat(float value, std::string_view format = "5.3f");
// Float to int as MSVC x64 does (cvttss2si): truncated, INT_MIN when out of range.
int toInt(float value);
} // namespace legacy

// Tokens split at spaces; "p" reads as "s", "c" only starts a new command; a number without its pair,
// or a pair whose second number does not parse, is dropped.
std::vector<VectorPoint> parseVectorPoints(std::u16string_view text);

// Each coordinate plus `offset` in getfloat's `format`. A command letter is written when it changes,
// "m" always; an "s" run closes with "c"; a lone "m" gets an "l" to the same point.
std::u16string serializeVectorPoints(const std::vector<VectorPoint> &points, std::string_view format,
                                     PointF offset = {0, 0});

// A cubic Bézier, or a uniform B-spline segment: the samples, then the end point.
std::vector<PointF> flattenCurve(const PointF (&control)[4], bool bspline);

// `dashLen` pixels on and off; a zero-length segment ends the polyline.
void dashedLines(Overlay &out, const std::vector<PointF> &polyline, int dashLen, std::uint32_t argb, float width);
void pointSquare(Overlay &out, PointF centre, bool selected, float size);
void pointCircle(Overlay &out, PointF centre, bool selected, float size);

struct VectorFrame {
    float coeffW = 1, coeffH = 1;   // script units per video pixel, divided by the drawing's or clip's scale
    float offsetX = 0, offsetY = 0; // a drawing's position; 0 for clips
    PointF zoomMove{0, 0};
    PointF zoomScale{1, 1};
    IntRect videoRect;
    float unit = 1;
    float pointArea() const { return 4.f * unit / zoomScale.x; }
    PointF toView(const VectorPoint &p) const;
};

class VectorEditor {
public:
    // MUST KILL: Move adds a separate "m" point (Drag moves points); InvertSlot is a button index
    enum Mode { Drag = 0, Line, Bezier, Spline, Move, Delete, InvertSlot };
    static constexpr int kModeCount = 7;

    struct Callbacks {
        std::function<void(bool commit)> apply; // false: a gesture's preview, true: its commit
        std::function<void()> bell;
        std::function<void(std::u16string_view)> notice;
    };

    std::vector<VectorPoint> points;
    int mode = Line;
    bool drawing = false; // Shift nudges by a tenth
    // Buttons the wheel cycles over: seven for the clip, six for the drawing.
    int modeCount = kModeCount;

    // In the video window's device pixels. True when the event was used.
    void pointer(const Pointer &event, const VectorFrame &frame, const Callbacks &cb);
    bool key(const Key &event, const VectorFrame &frame, const Callbacks &cb);
    // Wraps over the modeCount buttons.
    void wheel(int steps);
    void draw(Overlay &out, const VectorFrame &frame) const;
    // A first "l" or "b" point becomes the missing "m".
    void normaliseFirst();
    void endDrag()
    {
        m_grabbed = -1;
        m_drawSelection = false;
    }

    int checkPos(PointF pos, const VectorFrame &frame, bool retlast = false) const; // pos: zoomed-out view
    void addCurve(PointF pos, int whereis, char16_t type, const VectorFrame &frame);
    bool addCurvePoint(PointF pos, int whereis, const VectorFrame &frame);
    void addLine(PointF pos, int whereis, const VectorFrame &frame);
    void addMove(PointF pos, int whereis, const VectorFrame &frame);
    void removePoints(int selectedPoint, const Callbacks &cb, bool fromKeyboard);
    void selectPoints(const VectorFrame &frame);
    void changeSelection(bool select);
    int findPoint(int pos, char16_t type, bool nextStart, bool fromEnd) const;
    VectorPoint findSnapPoint(const VectorPoint &pos, std::size_t pointToSkip) const;
    int checkCurve(int pos, bool checkSpline) const;
    void moveSelected(float x, float y, const Callbacks &cb);
    int grabbed() const { return m_grabbed; }
    int hovered() const { return m_lastpos; }
    bool selecting() const { return m_drawSelection; }
    bool showsCross() const { return m_drawCross; }
    bool showsToolLines() const { return m_drawToolLines; }
    // The corners in drag order, not normalised.
    IntRect selection() const { return m_selection; }

private:
    void drawPoints(Overlay &out, const VectorFrame &frame) const;
    void drawLine(Overlay &out, const VectorFrame &frame, int i) const;
    int drawCurve(Overlay &out, const VectorFrame &frame, int i, bool bspline) const;
    void drawRect(Overlay &out, const VectorFrame &frame, int i) const;
    void drawCircle(Overlay &out, const VectorFrame &frame, int i) const;

    VectorPoint m_acpoint, m_lastpoint;
    int m_grabbed = -1;
    int m_x = 0, m_y = 0;
    int m_lastpos = -1;
    bool m_drawSelection = false, m_drawToolLines = false, m_drawCross = false;
    IntRect m_selection;
    int m_diffX = 0, m_diffY = 0;
    PointF m_firstmove;
    unsigned char m_axis = 0;
};

} // namespace hikari::application::visual
