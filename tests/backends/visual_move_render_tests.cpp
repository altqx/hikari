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

constexpr double kTolerance = 0.25;

struct Ratio {
    const char *name;
    int frameWidth, frameHeight;
    int scriptWidth, scriptHeight;
    int clientWidth, clientHeight;
};

constexpr Ratio kRatios[] = {
    {"half", 640, 360, 1280, 720, 640, 400},
    {"wide", 1280, 720, 640, 480, 640, 400},
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

std::string squareLine(const Ratio &r, const std::string &placement)
{
    const int side = r.scriptHeight / 12;
    return "{\\an5\\bord0\\shad0" + placement + std::format("\\p1}}m 0 0 l {0} 0 {0} {0} 0 {0}", side);
}

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

    QPointF toFrame(PointF p) const
    {
        const IntRect &rect = v.videoRect();
        return {(p.x - rect.left) * v.frameWidth() / rect.width(), (p.y - rect.top) * v.frameHeight() / rect.height()};
    }
};

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

}

TEST(VisualMoveRender, HandlesSitWhereLibassDrawsTheMovingAnchor)
{
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
    // libass moves a \move without times, or with 0,0, over the whole Line (ass_parse.c: t1 <= 0 && t2 <= 0).
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
