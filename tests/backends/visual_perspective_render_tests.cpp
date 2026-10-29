#include "hikari/application/visual_perspective.h"
#include "hikari/backends/libass_renderer.h"

#include <QFile>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::visual;
using namespace hikari::application::visual::perspective;

namespace {

constexpr int kScriptW = 320, kScriptH = 180;

std::vector<std::byte> bytesOf(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// libass needs a font even for a drawing, and the renderer has no system font provider.
std::shared_ptr<const std::vector<std::byte>> testFont()
{
    QFile f(QStringLiteral(HIKARI_TEST_FONT));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    return bytes;
}

std::string script(const std::string &text)
{
    return "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\nScaledBorderAndShadow: yes\n\n"
           "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
           "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, "
           "Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
           "MarginV, Encoding\n"
           "Style: Default,Titillium Web,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,"
           "100,100,0,0,1,0,0,7,0,0,0,1\n\n[Events]\nFormat: Layer, Start, End, Style, "
           "Name, MarginL, MarginR, MarginV, Effect, Text\n"
           "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,," +
           text + "\n";
}

core::StyleValues style()
{
    core::StyleValues s;
    s.scaleX = s.scaleY = u8"100";
    s.angle = s.outlineWidth = s.shadow = u8"0";
    return s;
}

struct Frame {
    OverlayFrame f;
    int alpha(int x, int y) const { return f.pixels[static_cast<std::size_t>(y) * f.stride + x * 4 + 3]; }
};

Frame render(const std::u16string &text, int w, int h)
{
    backends::LibassRenderer renderer;
    std::string utf8;
    for (const char16_t c : text)
        utf8.push_back(static_cast<char>(c)); // the texts are ASCII
    EXPECT_TRUE(renderer.prepare(
        RenderSnapshot{bytesOf(script(utf8)), {FontLease{"Titillium Web", testFont()}}, "Titillium Web", false}));
    auto frame = renderer.render(core::DocumentTime(1'000'000), w, h);
    EXPECT_TRUE(frame);
    Frame out;
    if (frame)
        out.f = std::move(*frame);
    if (out.f.empty || out.f.pixels.empty()) {
        out.f.width = w;
        out.f.height = h;
        out.f.stride = w * 4;
        out.f.pixels.assign(static_cast<std::size_t>(w * h * 4), 0);
    }
    return out;
}

double cross(PointF o, PointF a, PointF b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

double segmentDistance(PointF p, PointF a, PointF b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double l = dx * dx + dy * dy;
    const double t = l > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l, 0., 1.) : 0.;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

double outside(const Quad &q, PointF p)
{
    const double turn = cross(q[0], q[1], q[2]) > 0 ? 1 : -1;
    bool in = true;
    for (int i = 0; i < 4; ++i)
        in = in && turn * cross(q[static_cast<std::size_t>(i)], q[static_cast<std::size_t>((i + 1) % 4)], p) >= 0;
    if (in)
        return 0;
    double d = 1e9;
    for (int i = 0; i < 4; ++i)
        d = std::min(d, segmentDistance(p, q[static_cast<std::size_t>(i)], q[static_cast<std::size_t>((i + 1) % 4)]));
    return d;
}
double inside(const Quad &q, PointF p)
{
    if (outside(q, p) > 0)
        return 0;
    double d = 1e9;
    for (int i = 0; i < 4; ++i)
        d = std::min(d, segmentDistance(p, q[static_cast<std::size_t>(i)], q[static_cast<std::size_t>((i + 1) % 4)]));
    return d;
}

void expectInkOnQuad(const std::u16string &text, const Quad &scriptQuad, int w, int h)
{
    std::string where = std::string(text.begin(), text.end()) + " at " + std::to_string(w) + "x" + std::to_string(h) +
                        ", quad";
    for (const PointF p : scriptQuad)
        where += " " + std::to_string(p.x) + "," + std::to_string(p.y);
    SCOPED_TRACE(where);
    const double sx = double(w) / kScriptW, sy = double(h) / kScriptH;
    Quad q{};
    for (int i = 0; i < 4; ++i) {
        const PointF p = scriptQuad[static_cast<std::size_t>(i)];
        q[static_cast<std::size_t>(i)] = PointF{static_cast<float>(p.x * sx), static_cast<float>(p.y * sy)};
    }
    const Frame f = render(text, w, h);
    int stray = 0, faint = 0, missing = 0, inked = 0;
    std::array<double, 4> nearest{1e9, 1e9, 1e9, 1e9};
    double worst = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const PointF c{x + 0.5f, y + 0.5f};
            const int a = f.alpha(x, y);
            if (a > 0) {
                ++inked;
                const double d = outside(q, c);
                worst = std::max(worst, d);
                stray += a > 127 && d > 1.0;
                faint += d > 1.5;
                for (int i = 0; i < 4; ++i) {
                    const PointF k = q[static_cast<std::size_t>(i)];
                    nearest[static_cast<std::size_t>(i)] =
                        std::min(nearest[static_cast<std::size_t>(i)], double(std::hypot(c.x - k.x, c.y - k.y)));
                }
            } else if (inside(q, c) > 1.0) {
                ++missing;
            }
        }
    EXPECT_GT(inked, 50);
    EXPECT_EQ(stray, 0) << "ink up to " << worst << " px outside the quad";
    EXPECT_EQ(faint, 0) << "ink up to " << worst << " px outside the quad";
    EXPECT_EQ(missing, 0);
    for (int i = 0; i < 4; ++i)
        EXPECT_LE(nearest[static_cast<std::size_t>(i)], 1.0) << "corner " << i;
}

std::u16string lineWith(const Transform &t, std::u16string_view drawing)
{
    return writeTransform(u"{\\an" + std::u16string(1, char16_t(u'0' + t.an)) + u"\\p1}" + std::u16string(drawing), t,
                          style());
}

TEST(PerspectiveRender, TheTagsPaintTheQuad)
{
    const std::u16string drawing = u"m 0 0 l 120 0 120 50 0 50";
    const Box box{{0, 0}, {120, 50}};
    struct Case {
        double frx, fry, frz, fax, fscx, fscy;
        int an;
        PointF pos, org;
    };
    const Case cases[] = {
        {0, 0, 0, 0, 100, 100, 7, {40, 30}, {40, 30}},
        {20, 30, 0, 0, 100, 100, 7, {100, 60}, {160, 85}},
        {-25, 15, 10, 0.2, 110, 90, 5, {160, 90}, {150, 80}},
        {35, -20, -15, -0.15, 90, 120, 2, {170, 150}, {170, 110}},
        {-10, 40, 170, 0, 100, 100, 9, {260, 40}, {200, 70}},
        {15, 15, 30, 0.1, 80, 80, 1, {60, 140}, {100, 100}},
    };
    for (const Case &c : cases) {
        Transform t;
        t.frx = c.frx;
        t.fry = c.fry;
        t.frz = c.frz;
        t.fax = c.fax;
        t.fscx = c.fscx;
        t.fscy = c.fscy;
        t.an = c.an;
        t.pos = c.pos;
        t.org = c.org;
        const std::u16string text = lineWith(t, drawing);
        // libass lays out at the rendered size, so at twice the script's size its camera distance in script units halves.
        for (const auto [w, h] : {std::pair{kScriptW, kScriptH}, std::pair{2 * kScriptW, 2 * kScriptH}})
            expectInkOnQuad(text, textToQuad(t, box, screenZ(kScriptH, h)), w, h);
    }
}

// libass aligns a drawing by its bounding box, not its origin.
TEST(PerspectiveRender, ADrawingAwayFromItsOrigin)
{
    const std::u16string drawing = u"m 30 20 l 130 20 130 70 30 70";
    const Box box{{30, 20}, {130, 70}};
    for (const int an : {7, 5, 3}) {
        Transform t;
        t.frx = 20;
        t.fry = -25;
        t.fax = 0.1;
        t.an = an;
        t.pos = {160, 90};
        t.org = {150, 100};
        expectInkOnQuad(lineWith(t, drawing), textToQuad(t, box, screenZ(kScriptH, kScriptH)), kScriptW, kScriptH);
    }
}

// libass: a drawing's ascent is its height less \pbo and its descent \pbo, neither below 0.
TEST(PerspectiveRender, ABaselineOffsetMovesTheBox)
{
    const std::u16string drawing = u"m 0 0 l 100 0 100 40 0 40";
    const std::pair<std::u16string, Box> offsets[] = {
        {u"20", Box{{0, 0}, {100, 40}}},
        {u"50", Box{{0, 10}, {100, 50}, 50}},
        {u"-10", Box{{0, 0}, {100, 40}, 50}},
    };
    for (const auto &[pbo, box] : offsets)
        for (const int an : {7, 5, 2}) {
            Transform t;
            t.frx = 15;
            t.fry = -20;
            t.an = an;
            t.pos = {160, 90};
            t.org = {160, 90};
            const std::u16string text = writeTransform(
                u"{\\an" + std::u16string(1, char16_t(u'0' + an)) + u"\\p1\\pbo" + pbo + u"}" + drawing, t, style());
            expectInkOnQuad(text, textToQuad(t, box, screenZ(kScriptH, kScriptH)), kScriptW, kScriptH);
            Transform from;
            from.an = an;
            const Quad quad = textToQuad(t, box, screenZ(kScriptH, kScriptH));
            const auto found = quadToTransform(quad, box, OrgMode::Center, from, screenZ(kScriptH, kScriptH));
            ASSERT_TRUE(found);
            const std::u16string again = writeTransform(
                u"{\\an" + std::u16string(1, char16_t(u'0' + an)) + u"\\p1\\pbo" + pbo + u"}" + drawing, *found,
                style());
            expectInkOnQuad(again, quad, kScriptW, kScriptH);
        }
}

TEST(PerspectiveRender, TheTagsFoundForAQuadPaintIt)
{
    const std::u16string drawing = u"m 0 0 l 100 0 100 40 0 40";
    const Box box{{0, 0}, {100, 40}};
    const Quad quads[] = {
        {PointF{60, 40}, PointF{220, 55}, PointF{230, 130}, PointF{50, 120}},
        {PointF{100, 30}, PointF{200, 40}, PointF{260, 150}, PointF{40, 140}},
        {PointF{80, 60}, PointF{240, 30}, PointF{250, 110}, PointF{90, 100}},
    };
    for (const Quad &quad : quads)
        for (const OrgMode mode : {OrgMode::Center, OrgMode::NoFax})
            for (const auto [w, h] : {std::pair{kScriptW, kScriptH}, std::pair{2 * kScriptW, 2 * kScriptH}}) {
                Transform from;
                from.an = 7;
                const auto t = quadToTransform(quad, box, mode, from, screenZ(kScriptH, h));
                ASSERT_TRUE(t);
                expectInkOnQuad(lineWith(*t, drawing), quad, w, h);
            }
}

}
