// N3: the libass SubtitleRenderer adapter. Fonts come from the locked Qt
// install (OFL Titillium Web), added in memory with no system provider, so
// the results are deterministic.

#include "hikari/backends/libass_renderer.h"

#include "image_compare.h"

#include <QFile>
#include <QImage>
#include <gtest/gtest.h>

#include <cstring>

using namespace hikari;
using namespace hikari::application;

namespace {

std::shared_ptr<const std::vector<std::byte>> testFont()
{
    QFile f(QStringLiteral(HIKARI_TEST_FONT));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(d.size()));
    std::memcpy(bytes->data(), d.constData(), bytes->size());
    return bytes;
}

std::vector<std::byte> scriptOf(std::string_view events)
{
    const std::string s = std::string("[Script Info]\nScriptType: v4.00+\nPlayResX: 96\nPlayResY: 64\n\n"
                                      "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                                      "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, "
                                      "Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
                                      "MarginV, Encoding\n"
                                      "Style: D,Titillium Web,20,&H0030A0E0,&H000000FF,&H00000000,&H00000000,0,0,0,0,"
                                      "100,100,0,0,1,0,0,7,0,0,0,1\n\n[Events]\nFormat: Layer, Start, End, Style, "
                                      "Name, MarginL, MarginR, MarginV, Effect, Text\n") +
                          std::string(events);
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// The same drawing as the image runner's libass reference.
constexpr std::string_view kDrawing = "Dialogue: 0,0:00:00.00,0:00:05.00,D,,0,0,0,,{\\pos(10,10)\\p1}m 0 0 l 50 0 "
                                      "40 30 0 40{\\p0}\n";

RenderSnapshot snapshot(std::string_view events)
{
    return RenderSnapshot{scriptOf(events), {FontLease{"Titillium Web", testFont()}}, "Titillium Web", false};
}

// Premultiplied BGRA over opaque black, as the runner composites it.
QImage overBlack(const OverlayFrame &f)
{
    QImage image(f.width, f.height, QImage::Format_ARGB32);
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x) {
            const std::uint8_t *p = &f.pixels[static_cast<std::size_t>(y) * f.stride + x * 4];
            image.setPixel(x, y, qRgba(p[2], p[1], p[0], 255));
        }
    return image;
}

} // namespace

TEST(LibassRenderer, DrawingMatchesTheCpuReference)
{
    backends::LibassRenderer renderer;
    ASSERT_TRUE(renderer.prepare(snapshot(kDrawing)));
    const auto frame = renderer.render(core::DocumentTime(1'000'000), 96, 64);
    ASSERT_TRUE(frame);
    EXPECT_FALSE(frame->empty);
    const QImage image = overBlack(*frame);
    ASSERT_NE(image.pixel(30, 20), qRgba(0, 0, 0, 255)); // the drawing painted pixels
    QString message;
    EXPECT_TRUE(hikari::testing::matchesReference(image, QStringLiteral("runner-libass-drawing"), {},
                                          QStringLiteral(HIKARI_REFERENCE_DIR),
                                          QStringLiteral(HIKARI_TEST_ARTIFACT_DIR), &message))
        << message.toStdString();
}

TEST(LibassRenderer, OverlaysAppearAndDisappearWithTheirEvents)
{
    backends::LibassRenderer renderer;
    ASSERT_TRUE(renderer.prepare(snapshot("Dialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,{\\pos(10,10)}Hi\n")));
    const auto before = renderer.render(core::DocumentTime(500'000), 96, 64);
    ASSERT_TRUE(before);
    EXPECT_TRUE(before->empty);
    const auto shown = renderer.render(core::DocumentTime(1'500'000), 96, 64);
    ASSERT_TRUE(shown);
    EXPECT_FALSE(shown->empty);
    ASSERT_EQ(shown->changed.size(), 1u);
    const PixelRect area = shown->changed[0];
    EXPECT_GT(area.width, 0);
    // Same time again: nothing changed, same pixels.
    const auto again = renderer.render(core::DocumentTime(1'500'000), 96, 64);
    ASSERT_TRUE(again);
    EXPECT_TRUE(again->changed.empty());
    EXPECT_EQ(again->pixels, shown->pixels);
    // After the event: empty, and its old area is reported so it gets cleared.
    const auto after = renderer.render(core::DocumentTime(2'500'000), 96, 64);
    ASSERT_TRUE(after);
    EXPECT_TRUE(after->empty);
    ASSERT_EQ(after->changed.size(), 1u);
    EXPECT_EQ(after->changed[0], area);
    EXPECT_TRUE(std::ranges::all_of(after->pixels, [](std::uint8_t b) { return b == 0; }));
}

TEST(LibassRenderer, ResizingRepaintsEverything)
{
    backends::LibassRenderer renderer;
    ASSERT_TRUE(renderer.prepare(snapshot(kDrawing)));
    ASSERT_TRUE(renderer.render(core::DocumentTime(1'000'000), 96, 64));
    const auto bigger = renderer.render(core::DocumentTime(1'000'000), 192, 128);
    ASSERT_TRUE(bigger);
    EXPECT_EQ(bigger->width, 192);
    EXPECT_EQ(bigger->stride, 192 * 4);
    ASSERT_EQ(bigger->changed.size(), 1u);
    EXPECT_EQ(bigger->changed[0], (PixelRect{0, 0, 192, 128}));
}

TEST(LibassRenderer, FontLeasesLiveUntilTheContextIsReplaced)
{
    backends::LibassRenderer renderer;
    auto font = testFont();
    std::weak_ptr<const std::vector<std::byte>> watch = font;
    ASSERT_TRUE(renderer.prepare(RenderSnapshot{scriptOf(kDrawing), {FontLease{"Titillium Web", std::move(font)}},
                                                "Titillium Web", false}));
    EXPECT_FALSE(watch.expired()); // held by the context
    const auto second = renderer.prepare(snapshot(kDrawing));
    ASSERT_TRUE(second);
    EXPECT_EQ(*second, 2u);
    EXPECT_TRUE(watch.expired()); // released with the replaced context
    const auto frame = renderer.render(core::DocumentTime(1'000'000), 96, 64);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->generation, 2u);
}

TEST(LibassRenderer, ExplicitErrors)
{
    backends::LibassRenderer renderer;
    EXPECT_EQ(renderer.render(core::DocumentTime(0), 96, 64).error(), RenderError::NoSnapshot);
    ASSERT_TRUE(renderer.prepare(snapshot(kDrawing)));
    EXPECT_EQ(renderer.render(core::DocumentTime(0), 0, 64).error(), RenderError::InvalidSize);
    EXPECT_EQ(renderer.render(core::DocumentTime(0), 96, 100000).error(), RenderError::InvalidSize);
}
