#pragma once

#include "hikari/application/visual_tools.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::visual {

// Qt::Key values; the application layer does not link Qt.
namespace keys {
inline constexpr int A = 0x41; // Ctrl+A selects every point
inline constexpr int Delete = 0x01000007;
}

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
}

// Tokens split at spaces; "p" reads as "s", "c" only starts a new command; a number without its pair,
// or a pair whose second number does not parse, is dropped.
std::vector<VectorPoint> parseVectorPoints(std::u16string_view text);

// Each coordinate plus `offset` in getfloat's `format`. A command letter is written when it changes,
// "m" always; an "s" run closes with "c"; a lone "m" gets an "l" to the same point.
// Vector clips keep two decimal places, as Aegisub 3.5 writes them.
inline constexpr std::string_view kClipPointFormat = "6.2f";

std::u16string serializeVectorPoints(const std::vector<VectorPoint> &points, std::string_view format,
                                     PointF offset = {0, 0});

// A cubic Bézier, or a uniform B-spline segment: the samples, then the end point.
std::vector<PointF> flattenCurve(const PointF (&control)[4], bool bspline);

// `dashLen` pixels on and off; a zero-length segment ends the polyline.
void dashedLines(Overlay &out, const std::vector<PointF> &polyline, int dashLen, std::uint32_t argb, float width);
void pointSquare(Overlay &out, PointF centre, bool selected, float size);
void pointCircle(Overlay &out, PointF centre, bool selected, float size);

// Smoothing (0–100) as the farthest a fitted curve strays from the stroke, in view pixels.
inline constexpr int kDefaultSmoothing = 45;
double freehandTolerance(int smoothing);
// Cubic Béziers through a stroke, each within `tolerance` of the samples it covers (Schneider, Graphics Gems I).
std::vector<std::array<PointF, 4>> fitStroke(const std::vector<PointF> &stroke, double tolerance);

class VectorEditor;
ToolOption freehandOption(const VectorEditor &editor, bool enabled);
// In Freehand mode: Smoothing, then the last stroke's points and curves.
std::vector<ToolValue> freehandValues(const VectorEditor &editor);
// Smoothing from a value's text, rounded and kept within 0–100; nullopt for another value or a bad number.
std::optional<int> smoothingOf(const std::string &name, const std::u16string &text);

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
    enum Mode { Drag = 0, Line, Bezier, Spline, Move, Delete, InvertSlot, Freehand };
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
    int smoothing = kDefaultSmoothing;

    // In the video window's device pixels. True when the event was used.
    void pointer(const Pointer &event, const VectorFrame &frame, const Callbacks &cb);
    bool key(const Key &event, const VectorFrame &frame, const Callbacks &cb);
    // Wraps over the modeCount buttons.
    void wheel(int steps);
    void draw(Overlay &out, const VectorFrame &frame) const;
    // The last freehand stroke, fitted again at `smoothing` in place of its first fit; false without one.
    bool refitStroke();
    void forgetStroke() { m_lastStroke.reset(); }
    // The last stroke's curves; nullopt without one.
    std::optional<int> strokeCurves() const;
    bool stroking() const { return m_stroking; }
    // `pos` in the video window's device pixels, as a Pointer's; a click on a point is not on a segment.
    std::optional<Segment> segmentAt(PointF pos, const VectorFrame &frame) const;
    // Make curve puts the controls at a third and two thirds; Insert point keeps a curve's shape; Delete opens
    // the contour there. False when `segment` no longer matches the points.
    bool editSegment(Segment segment, SegmentEdit edit, PointF pos, const VectorFrame &frame);
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
    void placeStroke();
    std::vector<Segment> segments() const;
    PointF toPoints(PointF device, const VectorFrame &frame) const;

    struct Stroke {
        std::vector<VectorPoint> before;
        std::vector<PointF> samples;
        float viewPixel = 1;
        int curves = 0;
    };
    std::vector<PointF> m_stroke;
    bool m_stroking = false;
    std::optional<Stroke> m_lastStroke;

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

}
