#pragma once

// T4: the vector point editor the vector clip (T4) and the drawing tool (T5)
// share (docs/qt/visual-tools.md, "The vector point editor"): legacy
// DrawingAndClip (HikariSub/VisualClips.cpp, VisualClipPoint.h at 20d647c4)
// without its clip- or drawing-specific reading and writing, and the parts of
// legacy Visuals it uses (GetVectorPoints, Curve, DrawRect, DrawCircle,
// DrawDashedLine, getfloat). Every step keeps legacy's float arithmetic and
// int truncations, so a pointer adds, moves and nudges the point legacy did
// and the text written is legacy's.
//
// The owner (VectorClipTool, T5's drawing tool) reads its Line into points,
// sets the frame (the coefficients, the drawing's offset, the zoom), routes
// pointer and key events here and writes the points back when the editor
// asks: apply(false) while a gesture samples (legacy SetClip(true), the
// "dummy" preview) and apply(true) when it ends (SetClip(false), the commit).

#include "hikari/application/visual_tools.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application::visual {

// The keys the vector editor and the clips take (Qt::Key values; the
// application layer does not link Qt).
namespace keys {
inline constexpr int A = 0x41, D = 0x44, S = 0x53, W = 0x57;
inline constexpr int Delete = 0x01000007;
} // namespace keys

// Legacy ClipPoint: a drawing command's point. `type` is the command
// ('m', 'l', 'b' or 's'); `start` marks the point that begins a command.
struct VectorPoint {
    float x = 0, y = 0;
    char16_t type = u'm';
    bool start = true;
    bool selected = false;
    bool operator==(const VectorPoint &) const = default;
};

namespace legacy {
// wxString::ToCDouble: the whole text as a C-locale number.
bool cDouble(std::u16string_view text, double &out);
// wxAtoi (_wtoi): leading blanks, a sign and digits; saturated.
int atoi(std::u16string_view text);
// getfloat (config.cpp:1085-1101): printf's "%<format>", then, unless the
// format ends ".0f", trailing zeros and the point dropped; leading blanks
// trimmed.
std::u16string getfloat(float value, std::string_view format = "5.3f");
// A float converted to int as legacy's MSVC x64 build does (cvttss2si):
// truncated, INT_MIN when out of range.
int toInt(float value);
} // namespace legacy

// Visuals::GetVectorPoints (Visuals.cpp:566-605): the commands of a drawing
// or clip body. Tokens are split at spaces; "p" is read as "s", "c" only
// starts a new command; a number without its pair, or a pair whose second
// number does not parse, is dropped.
std::vector<VectorPoint> parseVectorPoints(std::u16string_view text);

// DrawingAndClip::GetVisual's loop (VisualClips.cpp:298-363): the points as
// drawing commands, each coordinate plus the offset in getfloat's `format`
// ("6.0f" for clips, "6.2f" for drawings). A command letter is written when
// it changes, "m" always; the continuation points of "b" and "s" go bare;
// an "s" run closes with "c"; a lone "m" (the last point, or one followed by
// another "m") gets an "l" to the same point. Trailing blanks are trimmed.
std::u16string serializeVectorPoints(const std::vector<VectorPoint> &points, std::string_view format,
                                     PointF offset = {0, 0});

// DrawingAndClip::Curve (VisualClips.cpp:820-868) on four control points:
// a cubic Bézier, or a uniform B-spline segment, flattened with legacy's
// step (the samples, then the end point).
std::vector<PointF> flattenCurve(const PointF (&control)[4], bool bspline);

// Legacy Visuals::DrawDashedLine (Visuals.cpp:403-425): the dashes of a
// polyline, `dashLen` pixels on and off; a zero-length segment ends it.
void dashedLines(Overlay &out, const std::vector<PointF> &polyline, int dashLen, std::uint32_t argb, float width);
// Visuals::DrawRect / DrawCircle (Visuals.cpp:358-401): a point's handle.
void pointSquare(Overlay &out, PointF centre, bool selected, float size);
void pointCircle(Overlay &out, PointF centre, bool selected, float size);

// Where the points are drawn and picked: DrawingAndClip's coeffW / coeffH
// (script units per video pixel, divided by the drawing's or clip's scale),
// its _x / _y (a drawing's position; 0 for clips) and the tools' zoom.
struct VectorFrame {
    float coeffW = 1, coeffH = 1;
    float offsetX = 0, offsetY = 0;
    PointF zoomMove{0, 0};
    PointF zoomScale{1, 1};
    IntRect videoRect; // legacy VideoSize: left, top, right, bottom
    float pointArea() const { return 4.f / zoomScale.x; } // SetCurVisual / SetZoom
    // ClipPoint::GetVector: a point in the view.
    PointF toView(const VectorPoint &p) const;
};

class VectorEditor {
public:
    // The legacy VectorItem modes (VideoToolbar.cpp:61-67): 0 move points,
    // 1 line, 2 Bézier, 3 B-spline, 4 separate point ("m"), 5 delete; 6 is
    // the Invert clip button's index, which the wheel can reach.
    enum Mode { Drag = 0, Line, Bezier, Spline, Move, Delete, InvertSlot };
    static constexpr int kModeCount = 7; // VectorItem::numIcons

    struct Callbacks {
        std::function<void(bool commit)> apply; // SetClip(!commit)
        std::function<void()> bell;             // wxBell
        std::function<void(std::u16string_view)> notice;
    };

    std::vector<VectorPoint> points;
    int mode = Line; // VectorItem::toggled = 1
    // VECTORDRAW: Shift nudges by a tenth.
    bool drawing = false;

    // Pointer and keys in the video window's device pixels (legacy
    // DrawingAndClip::OnMouseEvent / OnKeyPress). True when the event was used.
    void pointer(const Pointer &event, const VectorFrame &frame, const Callbacks &cb);
    bool key(const Key &event, const VectorFrame &frame, const Callbacks &cb);
    // The wheel's mode change (OnMouseEvent's first lines and
    // VectorItem::SetItemToggled: wraps over the seven buttons).
    void wheel(int steps);
    // DrawingAndClip::DrawVisual for the points and the tool's guides.
    void draw(Overlay &out, const VectorFrame &frame) const;
    // DrawVisual's own fix of the first point: "l" or "b" cannot stand in
    // for the missing "m" (legacy rewrites it while drawing).
    void normaliseFirst();
    // A reset during a drag ends it (no point grabbed, no selection box).
    void endDrag()
    {
        m_grabbed = -1;
        m_drawSelection = false;
    }

    // Legacy DrawingAndClip operations, for the owner and the tests.
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
    // Selection, hover and grab state, as legacy keeps it.
    int grabbed() const { return m_grabbed; }
    int hovered() const { return m_lastpos; }
    bool selecting() const { return m_drawSelection; }
    bool showsCross() const { return m_drawCross; }
    bool showsToolLines() const { return m_drawToolLines; }
    // The selection rectangle's corners (legacy wxRect x, y, width, height).
    IntRect selection() const { return m_selection; }

private:
    void drawPoints(Overlay &out, const VectorFrame &frame) const;
    void drawLine(Overlay &out, const VectorFrame &frame, int i) const;
    int drawCurve(Overlay &out, const VectorFrame &frame, int i, bool bspline) const;
    void drawRect(Overlay &out, const VectorFrame &frame, int i) const;
    void drawCircle(Overlay &out, const VectorFrame &frame, int i) const;

    VectorPoint m_acpoint, m_lastpoint;
    int m_grabbed = -1;
    int m_x = 0, m_y = 0; // the last pointer position
    int m_lastpos = -1;
    bool m_drawSelection = false, m_drawToolLines = false, m_drawCross = false;
    IntRect m_selection;
    int m_diffX = 0, m_diffY = 0; // wxPoint diffs
    PointF m_firstmove;
    unsigned char m_axis = 0;
};

} // namespace hikari::application::visual
