// T3: the Scale and rotation tools' origin and anchor against libass's
// renders. The tools write the tags (through their pointer gestures over a
// 1:1 view: the 640x360 script fills a 640x360 video rectangle), libass
// renders the Line before and after, and the ink's measured geometry must
// turn about the \org the tool draws its cross at, and stay anchored at the
// position its arrows start from. The images are compared too: the render
// before, turned or scaled about the point the overlay draws (its cross, its
// arrows' start), must cover the render after, and better than about that
// point moved by a few pixels. The renders, with the tool's overlay drawn
// over them, are written to the artifact directory for review.

#include "hikari/application/visual_rotation.h"
#include "hikari/application/visual_scale.h"
#include "hikari/application/visual_tools.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <functional>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::visual;

namespace {

constexpr int kWidth = 640, kHeight = 360;

std::shared_ptr<const std::vector<std::byte>> testFont()
{
    QFile f(QStringLiteral(HIKARI_TEST_FONT));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    return bytes;
}

std::string scriptOf(const std::u8string &text)
{
    return "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\nScaledBorderAndShadow: yes\n\n"
           "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
           "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
           "MarginL, MarginR, MarginV, Encoding\n"
           "Style: Default,Titillium Web,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,"
           "0,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,," +
           std::string(text.begin(), text.end()) + "\n";
}

std::vector<std::byte> bytesOf(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// A host over a 1:1 view and the one Line.
class Host : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    explicit Host(const std::u8string &text)
    {
        v.setClient(kWidth, kHeight + 40, 40);
        v.setScript(kWidth, kHeight);
        v.open({1280, 720, 0, 1});
        const std::string script = scriptOf(text);
        s = std::make_unique<EditSession>(core::loadAss(bytesOf(script)).document);
        const core::LineId id = s->document().lines().front()->id;
        s->setSelection({id, {id}, id, std::nullopt});
    }
    const VideoView &view() const override { return v; }
    const EditSession *session() const override { return s.get(); }
    std::optional<core::LineId> activeLine() const override { return s->selection().active; }
    std::vector<core::LineId> batchTargets() const override { return {*s->selection().active}; }
    std::expected<Gesture *, CommandRefusal> beginGesture(std::vector<core::LineId> t, std::string history) override
    {
        auto r = Gesture::begin(*s, std::move(t), std::move(history));
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
    std::int64_t videoTimeMs() const override { return 1500; }
    LegacyTimebase timebase() const override { return LegacyTimebase({}, 25.0); }
    std::u8string text() const { return s->document().lines().front()->text; }
};

void drag(VisualTool &tool, Host &host, Pointer::Button button, int x0, int y0, int x1, int y1)
{
    Pointer p{Pointer::Kind::Press, x0, y0, button};
    p.leftDown = button == Pointer::Button::Left;
    p.rightDown = button == Pointer::Button::Right;
    p.middleDown = button == Pointer::Button::Middle;
    tool.pointer(p, host);
    p.kind = Pointer::Kind::Move;
    p.button = Pointer::Button::None;
    p.x = (x0 + x1) / 2;
    p.y = (y0 + y1) / 2;
    tool.pointer(p, host);
    p.x = x1;
    p.y = y1;
    tool.pointer(p, host);
    Pointer r{Pointer::Kind::Release, x1, y1, button};
    tool.pointer(r, host);
    tool.reset(host); // the shell refreshes after the edit (RendererVideo::SetVisual)
}

struct Ink {
    double minX = 1e9, minY = 1e9, maxX = -1, maxY = -1, cx = 0, cy = 0, weight = 0;
    QImage image;
    QImage coverage; // libass's alpha, premultiplied white
};

Ink render(const std::u8string &text)
{
    backends::LibassRenderer renderer;
    EXPECT_TRUE(renderer.prepare(
        RenderSnapshot{bytesOf(scriptOf(text)), {FontLease{"Titillium Web", testFont()}}, "Titillium Web", false}));
    const auto frame = renderer.render(core::DocumentTime(1'500'000), kWidth, kHeight);
    EXPECT_TRUE(frame);
    Ink ink;
    ink.image = QImage(kWidth, kHeight, QImage::Format_ARGB32);
    ink.coverage = QImage(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < frame->height; ++y)
        for (int x = 0; x < frame->width; ++x) {
            const std::uint8_t *p = &frame->pixels[static_cast<std::size_t>(y) * frame->stride + x * 4];
            ink.image.setPixel(x, y, qRgba(p[2], p[1], p[0], 255));
            ink.coverage.setPixel(x, y, qRgba(p[3], p[3], p[3], p[3]));
            const double a = p[3] / 255.0;
            if (a <= 0)
                continue;
            ink.minX = std::min<double>(ink.minX, x);
            ink.minY = std::min<double>(ink.minY, y);
            ink.maxX = std::max<double>(ink.maxX, x);
            ink.maxY = std::max<double>(ink.maxY, y);
            ink.cx += (x + 0.5) * a;
            ink.cy += (y + 0.5) * a;
            ink.weight += a;
        }
    if (ink.weight > 0) {
        ink.cx /= ink.weight;
        ink.cy /= ink.weight;
    }
    return ink;
}

// The render with the tool's overlay over it (device pixels are script
// pixels here), for review.
void save(const Ink &ink, const VisualTool &tool, const Host &host, const QString &name)
{
    QImage image = ink.image;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const Overlay o = tool.overlay(host);
    for (const auto &poly : o.polygons) {
        QPolygonF q;
        for (const auto &pt : poly.points)
            q << QPointF(pt.x, pt.y);
        painter.setPen(poly.border ? QPen(QColor::fromRgba(poly.border), 1) : Qt::NoPen);
        painter.setBrush(poly.fill ? QBrush(QColor::fromRgba(poly.fill)) : Qt::NoBrush);
        painter.drawPolygon(q);
    }
    for (const auto &l : o.lines) {
        painter.setPen(QPen(QColor::fromRgba(l.argb), l.width));
        painter.drawLine(QPointF(l.from.x, l.from.y), QPointF(l.to.x, l.to.y));
    }
    painter.end();
    QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
    image.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR) + QStringLiteral("/") + name + QStringLiteral(".png"));
}

// The point the overlay's cross is drawn at: its 20-pixel horizontal and
// vertical strokes (RotationZ's and RotationXY's \org).
PointF crossOf(const Overlay &o)
{
    for (const auto &h : o.lines)
        if (h.from.y == h.to.y && h.to.x - h.from.x == 20)
            for (const auto &v : o.lines)
                if (v.from.x == v.to.x && v.to.y - v.from.y == 20 && v.from.x == h.from.x + 10 &&
                    v.from.y == h.from.y - 10)
                    return {v.from.x, h.from.y};
    ADD_FAILURE() << "the overlay draws no cross";
    return {};
}

// The point Scale's three arrows start from (its last three lines).
PointF arrowsStartOf(const Overlay &o)
{
    EXPECT_GE(o.lines.size(), 3u);
    const PointF start = o.lines.back().from;
    EXPECT_EQ(o.lines[o.lines.size() - 2].from, start);
    EXPECT_EQ(o.lines[o.lines.size() - 3].from, start);
    return start;
}

// How well `before`'s ink, mapped by `t`, covers `after`'s: pixels at least
// half covered, intersection over union.
double overlap(const Ink &before, const QTransform &t, const Ink &after)
{
    QImage mapped(before.coverage.size(), QImage::Format_ARGB32_Premultiplied);
    mapped.fill(Qt::transparent);
    QPainter painter(&mapped);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setTransform(t);
    painter.drawImage(0, 0, before.coverage);
    painter.end();
    int both = 0, either = 0;
    for (int y = 0; y < mapped.height(); ++y)
        for (int x = 0; x < mapped.width(); ++x) {
            const bool a = qAlpha(mapped.pixel(x, y)) >= 128;
            const bool b = qAlpha(after.coverage.pixel(x, y)) >= 128;
            both += a && b;
            either += a || b;
        }
    return either ? static_cast<double>(both) / either : 0;
}

// `before` mapped about `pivot` covers `after`, and covers it better than
// when mapped about the pivot moved 4 pixels along any axis the mapping
// depends on: the image turns or scales about the point the overlay draws.
void expectMappedAbout(const Ink &before, const Ink &after, PointF pivot,
                       const std::function<QTransform(double, double)> &about, double least)
{
    const QTransform t = about(pivot.x, pivot.y);
    const double at = overlap(before, t, after);
    EXPECT_GE(at, least) << "about (" << pivot.x << ", " << pivot.y << ")";
    for (const auto &[dx, dy] : {std::pair{4, 0}, {-4, 0}, {0, 4}, {0, -4}}) {
        const QTransform moved = about(pivot.x + dx, pivot.y + dy);
        if (moved == t)
            continue; // a scale of 1 or a mirror along that axis
        EXPECT_LT(overlap(before, moved, after), at) << "about the pivot moved by (" << dx << ", " << dy << ")";
    }
}

std::u8string without(std::u8string text, std::u8string_view tag)
{
    const auto at = text.find(tag);
    if (at == std::u8string::npos)
        return text;
    auto end = at + 1;
    while (end < text.size() && text[end] != u8'\\' && text[end] != u8'}')
        ++end;
    text.erase(at, end - at);
    return text;
}

} // namespace

TEST(VisualGeometry, RotationZTurnsTheInkAboutTheToolsOrigin)
{
    Host host(u8"{\\an5\\pos(320,180)}Turning text");
    RotationZTool tool;
    tool.reset(host);
    EXPECT_EQ(tool.org(), (PointF{320, 180})); // no \org: the position
    // Move the origin cross (RotationZ::OnMouseEvent's isOrg), then turn.
    drag(tool, host, Pointer::Button::Left, 320, 180, 400, 220);
    EXPECT_EQ(host.text(), u8"{\\org(400,220)\\an5\\pos(320,180)}Turning text");
    EXPECT_EQ(tool.org(), (PointF{400, 220}));
    drag(tool, host, Pointer::Button::Left, 500, 220, 400, 320);
    EXPECT_EQ(host.text(), u8"{\\frz270\\org(400,220)\\an5\\pos(320,180)}Turning text");
    const Ink before = render(without(host.text(), u8"\\frz"));
    const Ink after = render(host.text());
    save(after, tool, host, QStringLiteral("rotation-z-org"));
    ASSERT_GT(before.weight, 100);
    ASSERT_GT(after.weight, 100);
    // The image turned by 270 degrees counter-clockwise about the cross the
    // tool draws.
    const PointF cross = crossOf(tool.overlay(host));
    EXPECT_EQ(cross, (PointF{400, 220}));
    expectMappedAbout(before, after, cross, [](double x, double y) {
        return QTransform().translate(x, y).rotate(-270).translate(-x, -y);
    }, 0.9);
    // libass turns the text counter-clockwise by \frz about \org: the ink's
    // centre goes where the same turn about the tool's cross puts it.
    const double t = 270 * M_PI / 180;
    const double dx = before.cx - 400, dy = before.cy - 220;
    const double ex = 400 + dx * std::cos(t) + dy * std::sin(t);
    const double ey = 220 - dx * std::sin(t) + dy * std::cos(t);
    EXPECT_NEAR(after.cx, ex, 1.0);
    EXPECT_NEAR(after.cy, ey, 1.0);
    // The bounds turn with it: width and height swap.
    EXPECT_NEAR(after.maxX - after.minX, before.maxY - before.minY, 2.0);
    EXPECT_NEAR(after.maxY - after.minY, before.maxX - before.minX, 2.0);
}

TEST(VisualGeometry, ScaleKeepsTheAnchorTheArrowsStartFrom)
{
    // Top-left anchored: the arrows start at \pos; a width drag of 50
    // pixels from the width arrow's end makes \fscx150 and the ink grows
    // to the right only.
    Host host(u8"{\\an7\\pos(100,100)}Scaled text");
    ScaleTool tool;
    tool.reset(host);
    EXPECT_EQ(tool.from(), (PointF{100, 100}));
    drag(tool, host, Pointer::Button::Left, 200, 100, 250, 100);
    EXPECT_EQ(host.text(), u8"{\\fscx150\\an7\\pos(100,100)}Scaled text");
    const Ink before = render(u8"{\\an7\\pos(100,100)}Scaled text");
    const Ink after = render(host.text());
    save(after, tool, host, QStringLiteral("scale-an7"));
    const PointF anchor = arrowsStartOf(tool.overlay(host));
    EXPECT_EQ(anchor, (PointF{100, 100}));
    expectMappedAbout(before, after, anchor, [](double x, double y) {
        return QTransform().translate(x, y).scale(1.5, 1).translate(-x, -y);
    }, 0.8);
    // Everything scales away from the anchor (100, 100), the line box's
    // top-left: the ink's distances from it grow by half across, not down.
    EXPECT_NEAR(after.minX - 100, 1.5 * (before.minX - 100), 1.0);
    EXPECT_NEAR(after.minY, before.minY, 1.0);
    EXPECT_NEAR(after.maxY, before.maxY, 1.0);
    EXPECT_NEAR(after.maxX - 100, 1.5 * (before.maxX - 100), 1.5);
    // Height by the right button: halved downwards from the anchor's top.
    drag(tool, host, Pointer::Button::Right, 100, 200, 100, 150);
    EXPECT_EQ(host.text(), u8"{\\fscy50\\fscx150\\an7\\pos(100,100)}Scaled text");
    const Ink flat = render(host.text());
    expectMappedAbout(after, flat, arrowsStartOf(tool.overlay(host)), [](double x, double y) {
        return QTransform().translate(x, y).scale(1, 0.5).translate(-x, -y);
    }, 0.8);
    EXPECT_NEAR(flat.minY - 100, 0.5 * (after.minY - 100), 1.0);
    EXPECT_NEAR(flat.maxY - 100, 0.5 * (after.maxY - 100), 1.0);
    EXPECT_NEAR(flat.minX, after.minX, 1.0);
    EXPECT_NEAR(flat.maxX, after.maxX, 1.0);
}

TEST(VisualGeometry, ScaleAboutTheCentreForAn5)
{
    Host host(u8"{\\an5\\pos(320,180)}Centred");
    ScaleTool tool;
    tool.reset(host);
    EXPECT_EQ(tool.from(), (PointF{320, 180}));
    // Middle drag: both axes (type 2).
    drag(tool, host, Pointer::Button::Middle, 420, 280, 470, 330);
    EXPECT_EQ(host.text(), u8"{\\fscy150\\fscx150\\an5\\pos(320,180)}Centred");
    const Ink before = render(u8"{\\an5\\pos(320,180)}Centred");
    const Ink after = render(host.text());
    save(after, tool, host, QStringLiteral("scale-an5"));
    const PointF anchor = arrowsStartOf(tool.overlay(host));
    EXPECT_EQ(anchor, (PointF{320, 180}));
    expectMappedAbout(before, after, anchor, [](double x, double y) {
        return QTransform().translate(x, y).scale(1.5, 1.5).translate(-x, -y);
    }, 0.8);
    EXPECT_NEAR((after.minX + after.maxX) / 2, (before.minX + before.maxX) / 2, 1.0);
    EXPECT_NEAR(after.cx, before.cx, 1.0);
    EXPECT_NEAR((after.maxX - after.minX) / (before.maxX - before.minX), 1.5, 0.03);
}

TEST(VisualGeometry, RotationXYMirrorsAboutTheToolsOrigin)
{
    // A left drag of 180 pixels is \fry180: the text turned half way about
    // the vertical through \org, its ink mirrored about org.x.
    Host host(u8"{\\an5\\pos(320,180)\\org(360,180)}Mirror");
    RotationXYTool tool;
    tool.reset(host);
    EXPECT_EQ(tool.org(), (PointF{360, 180}));
    drag(tool, host, Pointer::Button::Left, 100, 300, 280, 300);
    EXPECT_EQ(host.text(), u8"{\\fry180\\an5\\pos(320,180)\\org(360,180)}Mirror");
    const Ink before = render(u8"{\\an5\\pos(320,180)\\org(360,180)}Mirror");
    const Ink after = render(host.text());
    save(after, tool, host, QStringLiteral("rotation-xy-org"));
    // Mirrored about the vertical through the cross the tool draws.
    const PointF cross = crossOf(tool.overlay(host));
    EXPECT_EQ(cross, (PointF{360, 180}));
    expectMappedAbout(before, after, cross, [](double x, double y) {
        return QTransform().translate(x, y).scale(-1, 1).translate(-x, -y);
    }, 0.9);
    EXPECT_NEAR(after.cx, 2 * 360 - before.cx, 1.0);
    EXPECT_NEAR(after.cy, before.cy, 1.0);
    EXPECT_NEAR(after.maxX - after.minX, before.maxX - before.minX, 2.0);
    // The overlay's grid is centred on the cross: the projected origin of the
    // tool's model is the \org.
    XYProjection p;
    p.width = kWidth;
    p.height = kHeight;
    p.org = tool.org();
    p.from = tool.org();
    const auto centre = projectXY(p, 0, 0, 0);
    ASSERT_TRUE(centre);
    EXPECT_NEAR(centre->x, 360, 1e-3);
    EXPECT_NEAR(centre->y, 180, 1e-3);
}
