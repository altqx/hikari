#include "hikari/application/position_commands.h"
#include "hikari/backends/libass_renderer.h"

#include <QFile>
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <format>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::position;

namespace {

constexpr int kWidth = 640, kHeight = 360, kSide = 40;
constexpr double kTolerance = 0.25;

std::shared_ptr<const std::vector<std::byte>> testFont()
{
    QFile f(QStringLiteral(HIKARI_TEST_FONT));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    return bytes;
}

std::vector<std::byte> scriptOf(const std::string &text)
{
    const std::string s =
        std::format("[Script Info]\nScriptType: v4.00+\nPlayResX: {}\nPlayResY: {}\nScaledBorderAndShadow: yes\n\n",
                    kWidth, kHeight) +
        "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
        "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
        "MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Titillium Web,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,0,0,"
        "0,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,," +
        text + "\n";
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::optional<std::pair<double, double>> inkCentre(const std::string &text, std::int64_t ms)
{
    backends::LibassRenderer renderer;
    EXPECT_TRUE(renderer.prepare(RenderSnapshot{scriptOf(text), {FontLease{"Titillium Web", testFont()}}, "Titillium Web", false}));
    const auto frame = renderer.render(core::DocumentTime(ms * 1000), kWidth, kHeight);
    if (!frame || frame->empty)
        return std::nullopt;
    double cx = 0, cy = 0, weight = 0;
    for (int y = 0; y < frame->height; ++y)
        for (int x = 0; x < frame->width; ++x) {
            const double a = frame->pixels[static_cast<std::size_t>(y) * frame->stride + x * 4 + 3] / 255.0;
            cx += (x + 0.5) * a;
            cy += (y + 0.5) * a;
            weight += a;
        }
    if (weight == 0)
        return std::nullopt;
    return std::pair{cx / weight, cy / weight};
}

Geometry squareGeometry()
{
    Geometry g;
    g.width = kWidth;
    g.height = kHeight;
    g.style = [](const core::LineRecord &) {
        core::StyleValues s;
        s.alignment = u8"2";
        s.angle = u8"0";
        s.marginLeft = s.marginRight = s.marginVertical = u8"0";
        return s;
    };
    g.size = [](const core::LineRecord &) { return std::optional<Size>(Size{kSide, kSide}); };
    return g;
}

std::string square(const std::string &tags)
{
    return "{\\bord0\\shad0" + tags + std::format("\\p1}}m 0 0 l {0} 0 {0} {0} 0 {0}", kSide);
}

}

TEST(PositionCommandsRender, ChangeAlignmentKeepsTheRenderInPlace)
{
    const std::string cases[] = {
        square("\\an7\\pos(200,120)"),
        square("\\an5\\pos(321.5,180.25)"),
        square("\\an3\\move(100,100,400,300,0,2000)"),
        square("\\an1\\pos(300,200)\\frz30"),
        square("\\an9\\pos(300,200)\\frx20\\fry-15\\org(250,150)"),
        square("\\an2\\pos(300,200)\\clip(290,150,330,190)"),
        square("\\an8\\pos(300,100)\\frz-40\\iclip(m 300 110 l 330 110 330 130 300 130)"),
    };
    const auto g = squareGeometry();
    const auto topLeft = inkCentre(cases[0], 1500);
    ASSERT_TRUE(topLeft);
    EXPECT_NEAR(topLeft->first, 200 + kSide / 2.0, kTolerance);
    EXPECT_NEAR(topLeft->second, 120 + kSide / 2.0, kTolerance);
    for (const auto &text : cases) {
        SCOPED_TRACE(text);
        core::LineRecord line;
        line.text = std::u8string(text.begin(), text.end());
        const auto before = inkCentre(text, 1500);
        ASSERT_TRUE(before);
        for (int an = 1; an <= 9; ++an) {
            SCOPED_TRACE(an);
            const auto changed = changeAlignment(line, an, g);
            const auto after = inkCentre(std::string(changed.begin(), changed.end()), 1500);
            ASSERT_TRUE(after);
            EXPECT_LE(std::abs(after->first - before->first), kTolerance)
                << std::string(changed.begin(), changed.end());
            EXPECT_LE(std::abs(after->second - before->second), kTolerance)
                << std::string(changed.begin(), changed.end());
        }
    }
}
