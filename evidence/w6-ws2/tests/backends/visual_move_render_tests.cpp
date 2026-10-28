// T2: Move's overlay against libass's render. A square drawing at \an5 (its
// centre is the Line's anchor) is rendered by libass (the renderer the video
// uses) at times before, inside and after the \move's interval, and
// Move::DrawVisual's handles (VisualMove.cpp:36-93) must sit where libass
// paints the anchor: the cross (the position at the frame's time) always,
// the start's square until the \move begins and the end's circle once it is
// over; a \pos holds all three on the anchor. The controller hands the tool
// the shown frame's start (VideoBox::Tell, VisualToolsController::
// videoTimeMs) and the video session renders that frame at the same start
// (VideoSession::render, video_session.cpp:282-290), so one time serves both
// here. Each case runs at three video / script ratios, the view at the
// frame's size, at half of it and pillarboxed at about twice it. The
// renders, with the overlay drawn over them, are written to the artifact
// directory. A \move without times or with 0,0 moves over the whole Line in
// libass (ass_parse.c: t1 <= 0 && t2 <= 0), and Move's cross follows it
// there (approved departure T2-move-untimed: legacy's GetPosnScale gave
// Move the Line's start as both times and added it again on each later
// read, Visuals.cpp:648-675).

#include "hikari/application/visual_position.h"
#include "hikari/application/visual_tools.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <format>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::visual;

namespace {

// Handles and the painted anchor agree within this many frame pixels (the
// centroid of an antialiased square, libass's eighth-pixel placement and
// the tool's float view coordinates; 0.07 measured on Linux).
constexpr double kTolerance = 0.25;

// A video's frame, the script's PlayRes and the video window's client size
// (with the 40-pixel panel below it, as legacy's VideoBox has).
struct Ratio {
    const char *name;
    int frameWidth, frameHeight;
    int scriptWidth, scriptHeight;
    int clientWidth, clientHeight;
};

constexpr Ratio kRatios[] = {
    // The script twice the frame; the view the frame's size.
    {"half", 640, 360, 1280, 720, 640, 400},
    // A 4:3 PlayRes on 16:9 video (2 and 1.5 script units a pixel); the view
    // half the frame.
    {"wide", 1280, 720, 640, 480, 640, 400},
    // A 1080p PlayRes on 4:3 video (6 and 4.5); the view pillarboxed at
    // about twice the frame.
    {"pillarbox", 320, 240, 1920, 1080, 800, 540},
};

std::shared_ptr<const std::vector<std::byte>> testFont()
{
    QFile f(QStringLiteral(HIKARI_TEST_FONT));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    return bytes;
}

std::vector<std::byte> bytesOf(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// One Line from 1 s to 3 s.
std::string scriptOf(const Ratio &r, const std::string &text)
{
    return std::format("[Script Info]\nScriptType: v4.00+\nPlayResX: {}\nPlayResY: {}\nScaledBorderAndShadow: yes\n\n",
                       r.scriptWidth, r.scriptHeight) +
           "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
           "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
           "MarginL, MarginR, MarginV, Encoding\n"
           "Style: Default,Titillium Web,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,0,0,"
           "0,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
           "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,," +
           text + "\n";
}

// The square, a twelfth of the script's height, centred on the anchor.
std::string squareLine(const Ratio &r, const std::string &placement)
{
    const int side = r.scriptHeight / 12;
    return "{\\an5\\bord0\\shad0" + placement + std::format("\\p1}}m 0 0 l {0} 0 {0} {0} 0 {0}", side);
}

// A host over the ratio's view with the one Line active.
class Host : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    std::int64_t time = 1000;
    Host(const Ratio &r, const std::string &text)
    {
        v.setClient(r.clientWidth, r.clientHeight + 40, 40);
        v.setScript(r.scriptWidth, r.scriptHeight);
        v.open({r.frameWidth, r.frameHeight, 0, 1});
        s = std::make_unique<EditSession>(core::loadAss(bytesOf(scriptOf(r, text))).document);
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
    std::int64_t videoTimeMs() const override { return time; }
    LegacyTimebase timebase() const override { return LegacyTimebase({}, 25.0); }

    // A view point in the frame's pixels.
    QPointF toFrame(PointF p) const
    {
        const IntRect &rect = v.videoRect();
        return {(p.x - rect.left) * v.frameWidth() / rect.width(), (p.y - rect.top) * v.frameHeight() / rect.height()};
    }
};

// libass's render at `ms`: the image, and the painted square's centre (its
// alpha-weighted centroid) in frame pixels.
struct Ink {
    QImage image;
    std::optional<QPointF> centre;
};

Ink render(const Ratio &r, const std::string &text, std::int64_t ms)
{
    backends::LibassRenderer renderer;
    EXPECT_TRUE(renderer.prepare(RenderSnapshot{
        bytesOf(scriptOf(r, text)), {FontLease{"Titillium Web", testFont()}}, "Titillium Web", false}));
    const auto frame = renderer.render(core::DocumentTime(ms * 1000), r.frameWidth, r.frameHeight);
    EXPECT_TRUE(frame);
    Ink ink;
    ink.image = QImage(r.frameWidth, r.frameHeight, QImage::Format_ARGB32);
    ink.image.fill(Qt::black);
    if (!frame || frame->empty)
        return ink;
    double cx = 0, cy = 0, weight = 0;
    for (int y = 0; y < frame->height; ++y)
        for (int x = 0; x < frame->width; ++x) {
            const std::uint8_t *p = &frame->pixels[static_cast<std::size_t>(y) * frame->stride + x * 4];
            ink.image.setPixel(x, y, qRgba(p[2], p[1], p[0], 255));
            const double a = p[3] / 255.0;
            cx += (x + 0.5) * a;
            cy += (y + 0.5) * a;
            weight += a;
        }
    if (weight > 0)
        ink.centre = QPointF(cx / weight, cy / weight);
    return ink;
}

// Move's handles in the overlay, in frame pixels: the start's square
// (DrawRect), the end's circle (DrawCircle) and the cross (DrawCross: its
// two 30-pixel lines).
struct Handles {
    std::optional<QPointF> start, end, cross;
};

Handles handlesOf(const Overlay &o, const Host &host)
{
    Handles h;
    for (const auto &poly : o.polygons)
        if (poly.points.size() == 4)
            h.start = host.toFrame({(poly.points[0].x + poly.points[2].x) / 2, (poly.points[0].y + poly.points[2].y) / 2});
    for (const auto &c : o.circles)
        if (c.filled)
            h.end = host.toFrame(c.centre);
    // The horizontal line gives the cross's y, the vertical one its x.
    std::optional<float> crossX, crossY;
    for (const auto &l : o.lines) {
        if (l.from.y == l.to.y && std::abs(l.to.x - l.from.x - 30.f) < 0.01f)
            crossY = l.from.y;
        else if (l.from.x == l.to.x && std::abs(l.to.y - l.from.y - 30.f) < 0.01f)
            crossX = l.from.x;
    }
    if (crossX && crossY)
        h.cross = host.toFrame({*crossX, *crossY});
    return h;
}

void expectAt(const std::optional<QPointF> &handle, const QPointF &ink, const char *what)
{
    ASSERT_TRUE(handle) << what;
    EXPECT_LE(std::abs(handle->x() - ink.x()), kTolerance) << what << " x " << handle->x() << ", libass " << ink.x();
    EXPECT_LE(std::abs(handle->y() - ink.y()), kTolerance) << what << " y " << handle->y() << ", libass " << ink.y();
}

// The render with the overlay over it (view points in frame pixels), for review.
void save(QImage image, const Overlay &o, const Host &host, const QString &name)
{
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const double scale = static_cast<double>(host.v.frameWidth()) / host.v.videoRect().width();
    for (const auto &poly : o.polygons) {
        QPolygonF q;
        for (const auto &pt : poly.points)
            q << host.toFrame(pt);
        painter.setPen(poly.border ? QPen(QColor::fromRgba(poly.border), 1) : Qt::NoPen);
        painter.setBrush(poly.fill ? QBrush(QColor::fromRgba(poly.fill)) : Qt::NoBrush);
        painter.drawPolygon(q);
    }
    for (const auto &l : o.lines) {
        painter.setPen(QPen(QColor::fromRgba(l.argb), l.width * scale));
        painter.drawLine(host.toFrame(l.from), host.toFrame(l.to));
    }
    for (const auto &c : o.circles) {
        painter.setPen(c.filled ? Qt::NoPen : QPen(QColor::fromRgba(c.argb), 1));
        painter.setBrush(c.filled ? QBrush(QColor::fromRgba(c.argb)) : Qt::NoBrush);
        painter.drawEllipse(host.toFrame(c.centre), c.radius * scale, c.radius * scale);
    }
    painter.end();
    QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
    image.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR) + QStringLiteral("/") + name + QStringLiteral(".png"));
}

} // namespace

TEST(VisualMoveRender, HandlesSitWhereLibassDrawsTheMovingAnchor)
{
    // \move(x1,y1,x2,y2,300,1500) on the Line from 1 s: the move runs from
    // 1.3 s to 2.5 s. Times on the Line before the move, at its start,
    // inside it, at its end and after it (to the Line's last millisecond).
    const std::int64_t times[] = {1000, 1150, 1300, 1450, 1777, 1900, 2333, 2500, 2700, 2999};
    for (const Ratio &r : kRatios) {
        SCOPED_TRACE(r.name);
        const int x1 = r.scriptWidth / 4, y1 = r.scriptHeight * 3 / 10;
        const int x2 = r.scriptWidth * 7 / 10, y2 = r.scriptHeight * 13 / 20;
        const std::string text = squareLine(r, std::format("\\move({},{},{},{},300,1500)", x1, y1, x2, y2));
        Host host(r, text);
        MoveTool tool;
        tool.selected(host);
        tool.reset(host);
        ASSERT_EQ(tool.moveStart(), 1300);
        ASSERT_EQ(tool.moveEnd(), 2500);
        for (const std::int64_t t : times) {
            SCOPED_TRACE(t);
            host.time = t;
            const Ink ink = render(r, text, t);
            ASSERT_TRUE(ink.centre);
            const Overlay o = tool.overlay(host);
            const Handles h = handlesOf(o, host);
            expectAt(h.cross, *ink.centre, "cross");
            if (t <= tool.moveStart())
                expectAt(h.start, *ink.centre, "start");
            if (t >= tool.moveEnd())
                expectAt(h.end, *ink.centre, "end");
            // Inside the move the anchor has left the start and not reached
            // the end.
            if (t > tool.moveStart() + 100 && t < tool.moveEnd() - 100) {
                ASSERT_TRUE(h.start && h.end);
                EXPECT_GT(std::hypot(h.start->x() - ink.centre->x(), h.start->y() - ink.centre->y()), 2.0);
                EXPECT_GT(std::hypot(h.end->x() - ink.centre->x(), h.end->y() - ink.centre->y()), 2.0);
            }
            if (t == 1900)
                save(ink.image, o, host, QStringLiteral("move-%1").arg(QString::fromUtf8(r.name)));
        }
    }
}

TEST(VisualMoveRender, APosHoldsEveryHandleOnTheAnchor)
{
    // \pos: Move's start, end and cross on the one point libass draws at,
    // whatever the time (SetCurVisual: from and to the same point).
    for (const Ratio &r : kRatios) {
        SCOPED_TRACE(r.name);
        const std::string text =
            squareLine(r, std::format("\\pos({},{})", r.scriptWidth * 3 / 5, r.scriptHeight * 2 / 5));
        Host host(r, text);
        MoveTool tool;
        tool.selected(host);
        tool.reset(host);
        for (const std::int64_t t : {1000, 2000, 2999}) {
            SCOPED_TRACE(t);
            host.time = t;
            const Ink ink = render(r, text, t);
            ASSERT_TRUE(ink.centre);
            const Overlay o = tool.overlay(host);
            const Handles h = handlesOf(o, host);
            expectAt(h.start, *ink.centre, "start");
            expectAt(h.end, *ink.centre, "end");
            expectAt(h.cross, *ink.centre, "cross");
            if (t == 2000)
                save(ink.image, o, host, QStringLiteral("pos-%1").arg(QString::fromUtf8(r.name)));
        }
    }
}

TEST(VisualMoveRender, AMoveWithoutTimesOrWithZeroTimesFollowsLibassOverTheWholeLine)
{
    // T2-move-untimed: \move(x1,y1,x2,y2) and \move(x1,y1,x2,y2,0,0) on the
    // Line from 1 s to 3 s move from its first to its last millisecond in
    // libass. Move's cross on the painted square's centroid at its start,
    // inside it and at its last millisecond, read twice (legacy added the
    // Line's start again on the second read); the start's square on it at
    // the Line's start.
    const std::int64_t times[] = {1000, 1040, 1333, 1777, 2000, 2500, 2960, 2999};
    for (const Ratio &r : kRatios) {
        SCOPED_TRACE(r.name);
        const int x1 = r.scriptWidth / 4, y1 = r.scriptHeight * 3 / 10;
        const int x2 = r.scriptWidth * 7 / 10, y2 = r.scriptHeight * 13 / 20;
        for (const char *times6 : {"", ",0,0"}) {
            SCOPED_TRACE(times6);
            const std::string text = squareLine(r, std::format("\\move({},{},{},{}{})", x1, y1, x2, y2, times6));
            Host host(r, text);
            MoveTool tool;
            tool.selected(host);
            tool.reset(host);
            tool.reset(host);
            ASSERT_EQ(tool.moveStart(), 1000);
            ASSERT_EQ(tool.moveEnd(), 3000);
            for (const std::int64_t t : times) {
                SCOPED_TRACE(t);
                host.time = t;
                const Ink ink = render(r, text, t);
                ASSERT_TRUE(ink.centre);
                const Overlay o = tool.overlay(host);
                const Handles h = handlesOf(o, host);
                expectAt(h.cross, *ink.centre, "cross");
                if (t == 1000)
                    expectAt(h.start, *ink.centre, "start");
                if (t > 1100 && t < 2900) {
                    ASSERT_TRUE(h.start && h.end);
                    EXPECT_GT(std::hypot(h.start->x() - ink.centre->x(), h.start->y() - ink.centre->y()), 2.0);
                    EXPECT_GT(std::hypot(h.end->x() - ink.centre->x(), h.end->y() - ink.centre->y()), 2.0);
                }
                if (t == 2000)
                    save(ink.image, o, host,
                         QStringLiteral("move-%1-%2").arg(QString::fromUtf8(r.name),
                                                          *times6 ? QStringLiteral("zero") : QStringLiteral("untimed")));
            }
        }
    }
}
