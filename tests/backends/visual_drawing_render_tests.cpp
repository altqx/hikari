// T5: the drawings the drawing tool reads and writes, rendered by libass (the
// renderer the video uses), against the tool's own points. A drawing's
// points are drawn over the video where libass paints the drawing, for
// every alignment, the \p scale and \fscx / \fscy; a shape drawn with a
// preset (legacy's five defaults and one taken from a Line) is painted in
// the rectangle the tool showed while it was dragged.

#include "hikari/application/shape_presets.h"
#include "hikari/application/visual_drawing.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_vector.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <QFile>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::visual;

namespace {

constexpr int kW = 96, kH = 64;

std::vector<std::byte> bytesOf(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// The renderer has no system font provider (N3): a drawing needs a font
// leased in memory, the locked Qt install's OFL Titillium Web, as T4's
// clip renders do.
std::shared_ptr<const std::vector<std::byte>> testFont()
{
    QFile f(QStringLiteral(HIKARI_TEST_FONT));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    return bytes;
}

std::string script(const std::string &events)
{
    return "[Script Info]\nScriptType: v4.00+\nPlayResX: 96\nPlayResY: 64\nScaledBorderAndShadow: yes\n\n"
           "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
           "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, "
           "Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
           "MarginV, Encoding\n"
           "Style: Default,Titillium Web,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,"
           "100,100,0,0,1,0,0,7,0,0,0,1\n\n[Events]\nFormat: Layer, Start, End, Style, "
           "Name, MarginL, MarginR, MarginV, Effect, Text\n" +
           events;
}

std::string dialogue(const std::u8string &text)
{
    return "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,," +
           std::string(reinterpret_cast<const char *>(text.c_str())) + "\n";
}

struct Frame {
    OverlayFrame f;
    int alpha(int x, int y) const { return f.pixels[static_cast<std::size_t>(y) * f.stride + x * 4 + 3]; }
    bool painted(int x, int y) const { return alpha(x, y) > 127; }
    // The painted pixels' box: left, top, right, bottom (inclusive), or none.
    std::optional<IntRect> box() const
    {
        std::optional<IntRect> out;
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                if (painted(x, y)) {
                    if (!out)
                        out = IntRect{x, y, x, y};
                    out->left = std::min(out->left, x);
                    out->top = std::min(out->top, y);
                    out->right = std::max(out->right, x);
                    out->bottom = std::max(out->bottom, y);
                }
        return out;
    }
};

Frame render(const std::u8string &lineText)
{
    backends::LibassRenderer renderer;
    EXPECT_TRUE(renderer.prepare(
        RenderSnapshot{bytesOf(script(dialogue(lineText))), {FontLease{"Titillium Web", testFont()}}, "Titillium Web", false}));
    auto frame = renderer.render(core::DocumentTime(1'000'000), kW, kH);
    EXPECT_TRUE(frame);
    Frame out;
    if (!frame)
        return out;
    out.f = std::move(*frame);
    if (out.f.empty) {
        out.f.width = kW;
        out.f.height = kH;
        out.f.stride = kW * 4;
        out.f.pixels.assign(static_cast<std::size_t>(kW * kH * 4), 0);
    }
    return out;
}

// A host over a 96x64 video showing a 96x64 script (one script unit a
// pixel) with one Line, gestures committed to its session.
class Host : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    std::vector<ShapePreset> presets = defaultShapePresets();
    explicit Host(const std::u16string &text)
    {
        const std::string ass = "[Script Info]\nScriptType: v4.00+\nPlayResX: 96\nPlayResY: 64\n\n[Events]\n"
                                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,x\n";
        auto document = core::loadAss(bytesOf(ass)).document;
        const auto id = document.lines()[0]->id;
        document.editLine(id, [&](core::LineRecord &l) { l.text = core::toUtf8(text); });
        s = std::make_unique<EditSession>(std::move(document));
        s->setSelection({id, {id}, id, std::nullopt});
        v.setClient(kW, kH, 0);
        v.setScript(kW, kH);
        v.open({kW, kH, 0, 1});
    }
    std::u8string text() const { return s->document().lines()[0]->text; }
    const VideoView &view() const override { return v; }
    const EditSession *session() const override { return s.get(); }
    std::optional<core::LineId> activeLine() const override { return s->selection().active; }
    std::vector<core::LineId> batchTargets() const override { return {*s->selection().active}; }
    std::expected<Gesture *, CommandRefusal> beginGesture(std::vector<core::LineId> targets, std::string history) override
    {
        auto r = Gesture::begin(*s, std::move(targets), std::move(history));
        if (!r)
            return std::unexpected(r.error());
        g.emplace(std::move(*r));
        return &*g;
    }
    Gesture *gesture() override { return g ? &*g : nullptr; }
    std::expected<void, CommandRefusal> commitGesture() override
    {
        auto r = g->commit(*s);
        g.reset();
        return r;
    }
    void cancelGesture() override { g.reset(); }
    std::pair<int, int> measureLabel(std::u16string_view) const override { return {0, 0}; }
    void toolChanged() override {}
    std::int64_t videoTimeMs() const override { return 1000; }
    const std::vector<ShapePreset> *shapePresets() const override { return &presets; }
};

// The tool's path in view pixels: its points joined (lines, the flattened
// Béziers), split at each "m" (the closing line returns to it).
std::vector<std::vector<PointF>> pathOf(const DrawingTool &tool, const Host &host)
{
    const auto &pts = tool.editor().points;
    const VectorFrame frame = tool.frame(host);
    std::vector<std::vector<PointF>> out;
    std::size_t i = 0;
    while (i < pts.size()) {
        if (pts[i].type == u'm' || out.empty()) {
            out.push_back({frame.toView(pts[i])});
            ++i;
        } else if (pts[i].type == u'b' && i + 2 < pts.size()) {
            const PointF control[4] = {out.back().back(), frame.toView(pts[i]), frame.toView(pts[i + 1]),
                                       frame.toView(pts[i + 2])};
            const auto curve = flattenCurve(control, false);
            out.back().insert(out.back().end(), curve.begin() + 1, curve.end());
            i += 3;
        } else {
            out.back().push_back(frame.toView(pts[i]));
            ++i;
        }
    }
    return out;
}

bool inside(const std::vector<std::vector<PointF>> &path, double x, double y)
{
    int winding = 0;
    for (const auto &poly : path)
        for (std::size_t k = 0; k < poly.size(); ++k) {
            const PointF a = poly[k], b = poly[(k + 1) % poly.size()];
            if (a.y <= y) {
                if (b.y > y && (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y) > 0)
                    ++winding;
            } else if (b.y <= y && (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y) < 0) {
                --winding;
            }
        }
    return winding != 0;
}

double distanceToPath(const std::vector<std::vector<PointF>> &path, double x, double y)
{
    double best = 1e9;
    for (const auto &poly : path)
        for (std::size_t k = 0; k < poly.size(); ++k) {
            const PointF a = poly[k], b = poly[(k + 1) % poly.size()];
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double len = dx * dx + dy * dy;
            double t = len > 0 ? ((x - a.x) * dx + (y - a.y) * dy) / len : 0;
            t = std::clamp(t, 0.0, 1.0);
            best = std::min(best, std::hypot(x - (a.x + t * dx), y - (a.y + t * dy)));
        }
    return best;
}

// Every pixel more than a pixel from the tool's path is painted exactly when
// its centre is inside it.
void expectPaintFollowsPath(const Frame &frame, const std::vector<std::vector<PointF>> &path)
{
    int checked = 0, in = 0;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const double cx = x + 0.5, cy = y + 0.5;
            if (distanceToPath(path, cx, cy) < 1.0)
                continue;
            ++checked;
            const bool expected = inside(path, cx, cy);
            in += expected;
            EXPECT_EQ(frame.painted(x, y), expected) << "pixel " << x << "," << y;
        }
    EXPECT_GT(checked, kW * kH / 2);
    EXPECT_GT(in, 20); // the drawing is on screen
}

std::u8string drawn(const std::u16string &tags, const std::u16string &body)
{
    return core::toUtf8(u"{\\bord0\\shad0" + tags + u"}" + body);
}

Pointer at(Pointer::Kind kind, int x, int y, bool left = false, Pointer::Button b = Pointer::Button::None)
{
    Pointer p;
    p.kind = kind;
    p.x = x;
    p.y = y;
    p.leftDown = left;
    p.button = b;
    return p;
}

// A shape preset dragged from (x1, y1) to (x2, y2) over the Line, committed.
// Returns the rectangle the tool showed (view pixels).
std::pair<PointF, PointF> dragShape(Host &host, DrawingTool &tool, int preset, int x1, int y1, int x2, int y2)
{
    tool.reset(host);
    tool.setOption("shape", preset, host);
    tool.pointer(at(Pointer::Kind::Press, x1, y1, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, (x1 + x2) / 2, (y1 + y2) / 2, true), host);
    tool.pointer(at(Pointer::Kind::Move, x2, y2, true), host);
    const Overlay o = tool.overlay(host);
    tool.pointer(at(Pointer::Kind::Release, x2, y2, false, Pointer::Button::Left), host);
    EXPECT_EQ(o.lines.size(), 4u);
    if (o.lines.size() != 4u)
        return {};
    return {o.lines[0].from, o.lines[2].from};
}

} // namespace

TEST(DrawingRender, PointsAreDrawnWhereLibassPaintsTheDrawing)
{
    // SetCurVisual's reading of the position, the alignment's offset
    // (CalcDrawingSize), the \p scale and \fscx / \fscy, against libass.
    const std::u16string body = u"m 0 0 l 40 0 40 20 b 30 30 10 30 0 20";
    const std::u16string cases[] = {
        u"\\an7\\pos(10,8)\\p1",
        u"\\an5\\pos(48,32)\\p1",
        u"\\an2\\pos(48,50)\\p1",
        u"\\an3\\pos(90,60)\\p1",
        u"\\an9\\pos(90,4)\\p1",
        u"\\an4\\pos(4,32)\\p1",
        u"\\an7\\pos(10,8)\\p2",
        u"\\an7\\pos(10,8)\\fscx150\\fscy50\\p1",
        u"\\an5\\pos(48,32)\\fscx75\\fscy200\\p1",
    };
    for (const auto &tags : cases) {
        const std::u8string tags8 = core::toUtf8(tags);
        SCOPED_TRACE(std::string(tags8.begin(), tags8.end()));
        const std::u8string text = drawn(tags, tags.find(u"\\p2") != std::u16string::npos
                                                   ? u"m 0 0 l 80 0 80 40 b 60 60 20 60 0 40"
                                                   : body);
        Host host(core::toUtf16(text));
        DrawingTool tool;
        tool.reset(host);
        expectPaintFollowsPath(render(text), pathOf(tool, host));
    }
}

TEST(DrawingRender, AnEditedPointIsPaintedWhereTheToolMovedIt)
{
    // A drag of the second point commits a drawing libass paints where the
    // tool now shows it (the written offsets undo the alignment's).
    Host host(core::toUtf16(drawn(u"\\an5\\pos(48,32)\\p1", u"m 0 0 l 40 0 40 20 0 20")));
    DrawingTool tool;
    tool.editor().mode = VectorEditor::Drag;
    tool.reset(host);
    const PointF second = tool.frame(host).toView(tool.editor().points[1]);
    const int x = static_cast<int>(second.x), y = static_cast<int>(second.y);
    tool.pointer(at(Pointer::Kind::Press, x, y, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, x + 10, y - 6, true), host);
    tool.pointer(at(Pointer::Kind::Release, x + 10, y - 6, false, Pointer::Button::Left), host);
    ASSERT_NE(host.text(), drawn(u"\\an5\\pos(48,32)\\p1", u"m 0 0 l 40 0 40 20 0 20"));
    tool.reset(host); // the committed Line read again
    expectPaintFollowsPath(render(host.text()), pathOf(tool, host));
}

TEST(DrawingRender, ShapesFillTheRectangleTheToolShows)
{
    // Each of legacy's five presets dragged over a Line: libass paints the
    // shape inside the rectangle the tool drew while dragging, filling it
    // (legacy writes the shape one unit right and down of it:
    // Shapes::GetVisual's "- 1"). The rectangle spans the shape's points,
    // control points included (Shapes::SetShape's bounds): the circle's
    // control points reach 155 where its curves reach 141.25 (a cubic's
    // middle: (100 + 3 * 155) / 4), so libass paints it inset by 13.75 of
    // 310 on each side. The drag keeps the square presets (Preserve aspect
    // ratio) inside the 96x64 frame.
    for (int preset = 1; preset <= 5; ++preset) {
        SCOPED_TRACE("preset " + std::to_string(preset));
        Host host(u"{\\an7\\pos(0,0)\\bord0\\shad0}");
        DrawingTool tool;
        const auto [from, to] = dragShape(host, tool, preset, 12, 8, 52, 40);
        ASSERT_LE(to.y, kH - 4);
        const double insetX = preset == 2 ? (to.x - from.x) * 13.75 / 310 : 0;
        const double insetY = preset == 2 ? (to.y - from.y) * 13.75 / 310 : 0;
        const auto box = render(host.text()).box();
        ASSERT_TRUE(box);
        EXPECT_NEAR(box->left, from.x + 1 + insetX, 1.0);
        EXPECT_NEAR(box->top, from.y + 1 + insetY, 1.0);
        EXPECT_NEAR(box->right + 1, to.x + 1 - insetX, 1.0);
        EXPECT_NEAR(box->bottom + 1, to.y + 1 - insetY, 1.0);
        // And the tool, back on free drawing, shows the points libass paints.
        tool.setOption("shape", 0, host);
        expectPaintFollowsPath(render(host.text()), pathOf(tool, host));
    }
}

TEST(DrawingRender, AShapeTakenFromALineIsPaintedInTheRectangle)
{
    // "Get shape from active line": a Line's drawing becomes a preset, and a
    // drag draws it scaled into the rectangle.
    const std::u16string line = u"{\\an7\\pos(0,0)\\p1}m 0 0 l 30 0 30 10 10 10 10 30 0 30";
    ShapesEdition edition(defaultShapePresets(), 0);
    ASSERT_FALSE(edition.addShape(u"corner"));
    ASSERT_TRUE(edition.getShapeFromLine(line));
    ASSERT_TRUE(edition.save().saved());
    Host host(u"{\\an7\\pos(0,0)\\bord0\\shad0}");
    host.presets = edition.presets();
    DrawingTool tool;
    const auto [from, to] = dragShape(host, tool, 6, 20, 8, 80, 56);
    const Frame frame = render(host.text());
    const auto box = frame.box();
    ASSERT_TRUE(box);
    EXPECT_NEAR(box->left, from.x + 1, 1.0);
    EXPECT_NEAR(box->top, from.y + 1, 1.0);
    EXPECT_NEAR(box->right + 1, to.x + 1, 1.0);
    EXPECT_NEAR(box->bottom + 1, to.y + 1, 1.0);
    // The L's inner corner is empty, its arms painted.
    EXPECT_TRUE(frame.painted(static_cast<int>(from.x) + 4, static_cast<int>(from.y) + 4));
    EXPECT_FALSE(frame.painted(static_cast<int>(to.x) - 4, static_cast<int>(to.y) - 4));
}
