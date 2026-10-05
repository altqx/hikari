// T4: the clips the tools write, rendered by libass (the renderer the video
// uses), against the tools' own outline. A rectangle clip shows exactly the
// pixels inside the outline the tool draws; a vector clip (lines, a Bézier,
// several subpaths, a scale) shows the pixels inside the path the tool draws
// (DrawingAndClip::Curve's flattening) and none outside it; \iclip the
// opposite; the tool's mask Line darkens what the clip hides.

#include "hikari/application/visual_clip.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_vector.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <QFile>
#include <gtest/gtest.h>

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

std::string script(const std::string &events)
{
    return "[Script Info]\nScriptType: v4.00+\nPlayResX: 96\nPlayResY: 64\n\n"
           "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
           "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, "
           "Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
           "MarginV, Encoding\n"
           "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,"
           "100,100,0,0,1,0,0,7,0,0,0,1\n\n[Events]\nFormat: Layer, Start, End, Style, "
           "Name, MarginL, MarginR, MarginV, Effect, Text\n" +
           events;
}

// A white box over the whole frame, with the clip's tags in front.
std::string boxLine(const std::u16string &tags)
{
    return "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,," + std::string(reinterpret_cast<const char *>(core::toUtf8(tags).c_str())) +
           "{\\pos(0,0)\\an7\\p1\\bord0\\shad0}m 0 0 l 96 0 96 64 0 64{\\p0}\n";
}

struct Frame {
    OverlayFrame f;
    // The box's alpha at a pixel (premultiplied BGRA).
    int alpha(int x, int y) const { return f.pixels[static_cast<std::size_t>(y) * f.stride + x * 4 + 3]; }
    bool painted(int x, int y) const { return alpha(x, y) > 127; }
};

Frame render(const std::string &events)
{
    backends::LibassRenderer renderer;
    EXPECT_TRUE(renderer.prepare(RenderSnapshot{bytesOf(script(events)), {}, "Arial", false}));
    auto frame = renderer.render(core::DocumentTime(1'000'000), kW, kH);
    EXPECT_TRUE(frame);
    Frame out;
    if (!frame)
        return out;
    out.f = std::move(*frame);
    if (out.f.empty) {
        // An empty overlay: nothing painted anywhere.
        out.f.width = kW;
        out.f.height = kH;
        out.f.stride = kW * 4;
        out.f.pixels.assign(static_cast<std::size_t>(kW * kH * 4), 0);
    }
    return out;
}

// A host over a 96x64 video showing a 96x64 script (one script unit a pixel).
class Host : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    Host(const std::u16string &text)
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
    const VideoView &view() const override { return v; }
    const EditSession *session() const override { return s.get(); }
    std::optional<core::LineId> activeLine() const override { return s->selection().active; }
    std::vector<core::LineId> batchTargets() const override { return {*s->selection().active}; }
    std::expected<Gesture *, CommandRefusal> beginGesture(std::vector<core::LineId>, std::string) override
    {
        return std::unexpected(CommandRefusal::Invalid);
    }
    Gesture *gesture() override { return nullptr; }
    std::expected<void, CommandRefusal> commitGesture() override { return std::unexpected(CommandRefusal::Invalid); }
    void cancelGesture() override {}
    std::pair<int, int> measureLabel(std::u16string_view) const override { return {0, 0}; }
    void toolChanged() override {}
};

// The tool's path in view pixels: its outline segments joined (lines and the
// flattened curves), split at each "m" (the closing line returns to it).
std::vector<std::vector<PointF>> pathOf(const VectorClipTool &tool, const Host &host)
{
    const auto &pts = tool.editor().points;
    const VectorFrame frame = tool.frame(host);
    std::vector<std::vector<PointF>> out;
    std::size_t i = 0;
    while (i < pts.size()) {
        if (pts[i].type == u'm') {
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

// Non-zero winding over the subpaths, as libass fills outlines.
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
// its centre is inside it (inverse: outside it).
void expectClipFollowsPath(const Frame &frame, const std::vector<std::vector<PointF>> &path, bool inverse)
{
    int checked = 0;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const double cx = x + 0.5, cy = y + 0.5;
            if (distanceToPath(path, cx, cy) < 1.0)
                continue;
            ++checked;
            EXPECT_EQ(frame.painted(x, y), inside(path, cx, cy) != inverse) << "pixel " << x << "," << y;
        }
    EXPECT_GT(checked, kW * kH / 2);
}

} // namespace

TEST(ClipRender, RectangleShowsWhatTheOutlineEncloses)
{
    // The tool writes the corners; its outline runs from x1, y1 to x2 - 1,
    // y2 - 1 (ClipRect::DrawVisual): libass paints exactly those pixels.
    Host host(u"{\\clip(10,8,70,40)}x");
    RectangleClipTool tool;
    tool.reset(host);
    const Overlay o = tool.overlay(host);
    ASSERT_EQ(o.lines.size(), 4u);
    const int left = static_cast<int>(o.lines[0].from.x), top = static_cast<int>(o.lines[0].from.y);
    const int right = static_cast<int>(o.lines[2].from.x), bottom = static_cast<int>(o.lines[2].from.y);
    EXPECT_EQ(left, 10);
    EXPECT_EQ(right, 69);
    const Frame clipped = render(boxLine(clip::putRectangle(u"", 10, 8, 70, 40, false)));
    const Frame inverse = render(boxLine(clip::putRectangle(u"", 10, 8, 70, 40, true)));
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const bool in = x >= left && x <= right && y >= top && y <= bottom;
            EXPECT_EQ(clipped.painted(x, y), in) << x << "," << y;
            EXPECT_EQ(inverse.painted(x, y), !in) << x << "," << y;
        }
}

TEST(ClipRender, VectorShowsWhatThePathEncloses)
{
    // Lines and a Bézier in one subpath, and a second subpath.
    const std::u16string text = u"{\\clip(m 8 8 l 50 6 b 70 10 80 40 60 56 l 10 50 m 70 4 l 92 4 92 20 70 20)}x";
    Host host(text);
    VectorClipTool tool;
    tool.reset(host);
    const auto path = pathOf(tool, host);
    ASSERT_EQ(path.size(), 2u);
    const std::u16string body = tool.body();
    expectClipFollowsPath(render(boxLine(u"{\\clip(" + body + u")}")), path, false);
    expectClipFollowsPath(render(boxLine(u"{\\iclip(" + body + u")}")), path, true);
}

TEST(ClipRender, ClipScaleDividesTheCoordinates)
{
    // \clip(3,...): the points are in quarter units; the tool draws them where
    // libass clips (its coefficients multiplied by 2^(3-1)).
    Host host(u"{\\clip(3,m 40 40 l 320 40 320 200 40 200)}x");
    VectorClipTool tool;
    tool.reset(host);
    EXPECT_EQ(tool.vectorScale(), 3);
    EXPECT_EQ(tool.frame(host).coeffW, 4.f);
    const auto path = pathOf(tool, host);
    EXPECT_EQ(path[0][1], (PointF{80, 10}));
    expectClipFollowsPath(render(boxLine(u"{\\clip(" + tool.body() + u")}")), path, false);
}

TEST(ClipRender, MaskDarkensWhatTheClipHides)
{
    // The mask Line (CreateClipMask) over an unclipped frame: translucent
    // black outside a \clip's path, nothing inside it.
    Host host(u"{\\clip(m 20 10 l 80 10 80 50 20 50)}x");
    VectorClipTool tool;
    tool.reset(host);
    const auto lines = tool.previewLines(host);
    ASSERT_EQ(lines.size(), 1u);
    const std::string text(reinterpret_cast<const char *>(lines[0].text.c_str()));
    const Frame mask = render("Dialogue: 2147483647,0:00:00.00,0:00:05.00,Default,,0,0,0,," + text + "\n");
    EXPECT_EQ(mask.alpha(50, 30), 0);           // inside the clip
    EXPECT_NEAR(mask.alpha(5, 5), 0x88, 2);      // outside: \1a&H77& is 0x88 opaque
    EXPECT_NEAR(mask.alpha(90, 60), 0x88, 2);
}
