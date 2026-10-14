// N3: the libass SubtitleRenderer adapter. Fonts come from the locked Qt
// install (OFL Titillium Web), added in memory with no system provider, so
// the results are deterministic. Its frames are checked byte for byte
// against legacy's composite: libass's images blended by legacy
// BlendAssBitmap (HikariSub/AssBlend.cpp, copied unchanged into the build)
// onto a cleared frame, as SubtitlesLibass::DrawOverlay does.

#include "hikari/backends/libass_renderer.h"
#include "hikari/core/ass_attachments.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include "AssBlend.h" // legacy
#include "image_compare.h"

extern "C" {
#include <ass/ass.h>
}

#include <QFile>
#include <QImage>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

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

std::vector<std::byte> bytesOf(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
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
    // The runner's reference truncates its divisions by 255; the adapter
    // rounds them as legacy's blend does, one level apart at most on this
    // drawing (FramesMatchLegacysCompositeByteForByte holds it to legacy exactly).
    QString message;
    EXPECT_TRUE(hikari::testing::matchesReference(image, QStringLiteral("runner-libass-drawing"), {1, 0},
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
    EXPECT_TRUE(again->pixels.sameBuffer(shown->pixels)); // handed out again, not copied
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

// E5: TL_MODE_HIDE_ORIGINAL_ON_VIDEO. A TLMode pair (the original in the
// top-aligned TLMode Style, the translation at the bottom) renders both
// lines; with the option the renderer's script (SubsGrid::GetVisible) leaves
// the original out, so nothing is drawn in the top half.
TEST(LibassRenderer, HidingTheOriginalLeavesOnlyTheTranslation)
{
    const std::string script = "[Script Info]\nScriptType: v4.00+\nPlayResX: 96\nPlayResY: 64\nTLMode: Yes\n"
                               "TLMode Style: O\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, "
                               "SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
                               "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
                               "MarginV, Encoding\n"
                               "Style: D,Titillium Web,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,"
                               "0,0,1,0,0,2,0,0,2,1\n"
                               "Style: O,Titillium Web,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,"
                               "0,0,1,0,0,8,0,0,2,1\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, "
                               "MarginR, MarginV, Effect, Text\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,Gate\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,Brama\n";
    std::vector<std::byte> bytes(script.size());
    std::memcpy(bytes.data(), script.data(), script.size());
    const auto document = core::loadAss(bytes).document;
    ASSERT_EQ(document.lines().size(), 1u);
    const auto inkIn = [](const OverlayFrame &f, int top, int bottom) {
        for (int y = top; y < bottom; ++y)
            for (int x = 0; x < f.width; ++x)
                if (f.pixels[static_cast<std::size_t>(y) * f.stride + x * 4 + 3] != 0)
                    return true;
        return false;
    };
    for (const bool hide : {false, true}) {
        const auto rendered = core::encodeAss(document, core::AssSaveOptions{.hideOriginalOnVideo = hide, .renderer = true});
        backends::LibassRenderer renderer;
        ASSERT_TRUE(renderer.prepare(RenderSnapshot{rendered, {FontLease{"Titillium Web", testFont()}}, "Titillium Web", false}));
        const auto frame = renderer.render(core::DocumentTime(1'500'000), 96, 64);
        ASSERT_TRUE(frame);
        ASSERT_FALSE(frame->empty);
        EXPECT_TRUE(inkIn(*frame, 32, 64)) << "the translation, hide " << hide;
        EXPECT_EQ(inkIn(*frame, 0, 32), !hide) << "the original, hide " << hide;
    }
}

namespace {

// Heavy typesetting at 640x360: blur, borders and shadows, alpha, moves,
// transforms, fades, rotation, clips, drawings, layers over one another, and
// ink past the frame's edges.
constexpr std::string_view kHeavyEvents =
    "Dialogue: 0,0:00:00.00,0:00:10.00,T,,0,0,0,,{\\move(20,300,420,40)\\blur3\\bord4\\shad3}Moving blurred text\n"
    "Dialogue: 1,0:00:00.50,0:00:09.00,T,,0,0,0,,{\\pos(320,180)\\an5\\frz30\\t(0,4000,\\frz-30\\1c&H00FF40&)"
    "\\fad(500,500)}Rotating karaoke line\n"
    "Dialogue: 2,0:00:01.00,0:00:08.00,T,,0,0,0,,{\\pos(0,0)\\p1\\1a&H80&\\bord0\\shad0\\clip(40,40,600,320)}"
    "m 0 0 l 640 0 640 360 0 360{\\p0}\n"
    "Dialogue: 3,0:00:00.00,0:00:10.00,T,,0,0,0,,{\\pos(-30,350)\\an1\\fs60\\be2}Off the edge both ways and more\n"
    "Dialogue: 4,0:00:02.00,0:00:06.00,T,,0,0,0,,{\\pos(100,100)\\p1\\3c&H0000FF&\\bord6\\blur8}"
    "m 0 0 b 80 -40 160 40 240 0 l 200 120 40 120{\\p0}\n"
    "Dialogue: 5,0:00:03.00,0:00:07.00,T,,0,0,0,,{\\an7\\pos(10,10)\\alpha&H40&\\t(\\fscx150\\fscy150)}"
    "Top line one\\NTop line two\n"
    "Dialogue: 6,0:00:04.00,0:00:04.50,T,,0,0,0,,{\\pos(500,200)\\fs80}Flash\n";

std::string heavyScript(int playResX = 640, int playResY = 360, std::string_view primary = "&H00F0E0D0")
{
    return "[Script Info]\nScriptType: v4.00+\nPlayResX: " + std::to_string(playResX) +
           "\nPlayResY: " + std::to_string(playResY) +
           "\nScaledBorderAndShadow: yes\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, "
           "SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, "
           "Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
           "Style: T,Titillium Web,36," + std::string(primary) +
           ",&H000000FF,&H00202020,&H80000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n\n"
           "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" +
           std::string(kHeavyEvents);
}

// Legacy SubtitlesLibass::DrawOverlay's composite of the same script: every
// image blended by legacy BlendAssBitmap onto a cleared frame.
class LegacyComposite {
public:
    LegacyComposite(const std::string &script, const std::shared_ptr<const std::vector<std::byte>> &font)
    {
        m_library = ass_library_init();
        ass_add_font(m_library, "Titillium Web", const_cast<char *>(reinterpret_cast<const char *>(font->data())),
                     static_cast<int>(font->size()));
        m_renderer = ass_renderer_init(m_library);
        ass_set_fonts(m_renderer, nullptr, "Titillium Web", ASS_FONTPROVIDER_NONE, nullptr, 1);
        m_track = ass_read_memory(m_library, const_cast<char *>(script.data()), script.size(), nullptr);
    }
    ~LegacyComposite()
    {
        ass_free_track(m_track);
        ass_renderer_done(m_renderer);
        ass_library_done(m_library);
    }
    std::vector<std::uint8_t> render(long long ms, int width, int height)
    {
        ass_set_frame_size(m_renderer, width, height);
        ass_set_storage_size(m_renderer, width, height);
        int change = 0;
        std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4, 0);
        const int pitch = width * 4;
        for (ASS_Image *img = ass_render_frame(m_renderer, m_track, ms, &change); img; img = img->next) {
            if (img->h == 0 || img->w == 0)
                continue;
            BlendAssBitmap(out.data() + img->dst_y * pitch + img->dst_x * 4, pitch, img->bitmap, img->stride, img->w,
                           img->h, img->color);
        }
        return out;
    }

private:
    ASS_Library *m_library = nullptr;
    ASS_Renderer *m_renderer = nullptr;
    ASS_Track *m_track = nullptr;
};

bool inside(const std::vector<PixelRect> &rects, int x, int y)
{
    return std::ranges::any_of(rects, [&](const PixelRect &r) {
        return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
    });
}

int maxDifference(const OverlayPixels &a, const std::vector<std::uint8_t> &b)
{
    int most = 0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        most = std::max(most, std::abs(int(a[i]) - int(b[i])));
    return most;
}

} // namespace

// Every frame of the heavy script, at the script's size and at an odd one
// (every vector width and tail), byte for byte against legacy's composite.
// Every third frame is held on to, as the presenter holds what it shows: it
// must stay as it was while later frames are drawn.
TEST(LibassRenderer, FramesMatchLegacysCompositeByteForByte)
{
    const auto font = testFont();
    const std::string script = heavyScript();
    for (const auto [w, h] : {std::pair{640, 360}, std::pair{333, 187}}) {
        backends::LibassRenderer renderer;
        ASSERT_TRUE(renderer.prepare(RenderSnapshot{bytesOf(script), {FontLease{"Titillium Web", font}},
                                                    "Titillium Web", false}));
        LegacyComposite legacy(script, font);
        std::vector<std::pair<OverlayFrame, std::vector<std::uint8_t>>> held;
        int frames = 0, drawn = 0;
        for (long long ms = 0; ms <= 10'000; ms += 125, ++frames) {
            const auto frame = renderer.render(core::DocumentTime(ms * 1000), w, h);
            ASSERT_TRUE(frame);
            const auto reference = legacy.render(ms, w, h);
            ASSERT_TRUE(frame->pixels == OverlayPixels(reference))
                << w << "x" << h << " at " << ms << " ms, largest difference " << maxDifference(frame->pixels, reference);
            drawn += !frame->empty;
            if (frames % 3 == 0)
                held.emplace_back(*frame, reference);
        }
        EXPECT_GT(drawn, frames / 2);
        for (const auto &[frame, reference] : held)
            EXPECT_TRUE(frame.pixels == OverlayPixels(reference)) << "a held frame was drawn over";
    }
}

// The changed areas: inside the frame, the bounds of what was drawn before
// and now, covering every byte that differs from the previous frame; nothing
// is drawn outside the new bounds.
TEST(LibassRenderer, ChangedAreasCoverEveryDifferenceAndNoMore)
{
    const auto font = testFont();
    backends::LibassRenderer renderer;
    ASSERT_TRUE(renderer.prepare(RenderSnapshot{bytesOf(heavyScript()), {FontLease{"Titillium Web", font}},
                                                "Titillium Web", false}));
    constexpr int kW = 640, kH = 360;
    std::optional<OverlayFrame> previous;
    for (long long ms = 0; ms <= 10'000; ms += 250) {
        const auto frame = renderer.render(core::DocumentTime(ms * 1000), kW, kH);
        ASSERT_TRUE(frame);
        for (const PixelRect &r : frame->changed) {
            EXPECT_GE(r.x, 0);
            EXPECT_GE(r.y, 0);
            EXPECT_GT(r.width, 0);
            EXPECT_LE(r.x + r.width, kW);
            EXPECT_LE(r.y + r.height, kH);
        }
        if (!previous) {
            EXPECT_EQ(frame->changed, (std::vector<PixelRect>{{0, 0, kW, kH}})); // the first: all of it
        } else {
            ASSERT_LE(frame->changed.size(), 2u);
            bool differs = false;
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x) {
                    const std::size_t at = static_cast<std::size_t>(y) * frame->stride + static_cast<std::size_t>(x) * 4;
                    if (std::equal(frame->pixels.data() + at, frame->pixels.data() + at + 4, previous->pixels.data() + at))
                        continue;
                    differs = true;
                    ASSERT_TRUE(inside(frame->changed, x, y)) << "at " << ms << " ms, " << x << "," << y;
                }
            if (frame->changed.empty())
                EXPECT_FALSE(differs);
        }
        // Nothing drawn outside the new bounds (the last of the changed areas when drawn).
        if (!frame->empty) {
            const PixelRect bounds = frame->changed.back();
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x)
                    if (!(x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height))
                        ASSERT_EQ(frame->pixels[static_cast<std::size_t>(y) * frame->stride + x * 4 + 3], 0)
                            << "at " << ms << " ms, " << x << "," << y;
        } else {
            EXPECT_TRUE(std::ranges::all_of(frame->pixels, [](std::uint8_t b) { return b == 0; }));
        }
        previous = *frame;
    }
}

TEST(LibassRenderer, AnUnchangedFrameSharesTheShownBuffer)
{
    const auto font = testFont();
    backends::LibassRenderer renderer;
    ASSERT_TRUE(renderer.prepare(RenderSnapshot{bytesOf(heavyScript()), {FontLease{"Titillium Web", font}},
                                                "Titillium Web", false}));
    const auto first = renderer.render(core::DocumentTime(4'200'000), 640, 360);
    const auto still = renderer.render(core::DocumentTime(4'200'000), 640, 360);
    ASSERT_TRUE(first && still);
    EXPECT_FALSE(still->empty);
    EXPECT_TRUE(still->changed.empty());
    EXPECT_TRUE(still->pixels.sameBuffer(first->pixels));
    // The next change goes to another buffer while these frames hold the shown one.
    const std::vector<std::uint8_t> before(first->pixels.begin(), first->pixels.end());
    const auto moved = renderer.render(core::DocumentTime(4'300'000), 640, 360);
    ASSERT_TRUE(moved);
    EXPECT_FALSE(moved->changed.empty());
    EXPECT_FALSE(moved->pixels.sameBuffer(first->pixels));
    EXPECT_TRUE(first->pixels == OverlayPixels(before));
}

// prepare() keeps the libass renderer (and its caches) while the snapshot's
// fonts are the same leases, and switches only the track; other fonts get a
// new one. Each prepare still starts a new generation, and its first frame
// is drawn whatever libass reports.
TEST(LibassRenderer, PrepareKeepsTheRendererWhileTheFontsStay)
{
    const auto font = testFont();
    backends::LibassRenderer renderer;
    const auto snapshotWith = [&](std::string script, std::shared_ptr<const std::vector<std::byte>> lease) {
        return RenderSnapshot{bytesOf(script), {FontLease{"Titillium Web", std::move(lease)}}, "Titillium Web", false};
    };
    ASSERT_TRUE(renderer.prepare(snapshotWith(heavyScript(), font)));
    const void *engine = renderer.engineIdentity();
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(renderer.render(core::DocumentTime(1'000'000), 640, 360));
    const auto again = renderer.prepare(snapshotWith(heavyScript(), font));
    ASSERT_TRUE(again);
    EXPECT_EQ(*again, 2u);
    EXPECT_EQ(renderer.engineIdentity(), engine);
    EXPECT_EQ(renderer.parsedTracks(), 1u); // the same script: no second parse
    const auto redrawn = renderer.render(core::DocumentTime(1'000'000), 640, 360);
    ASSERT_TRUE(redrawn);
    EXPECT_EQ(redrawn->generation, 2u);
    EXPECT_FALSE(redrawn->changed.empty());
    // Another default family or provider, or other leases: a new renderer.
    auto other = snapshotWith(heavyScript(), font);
    other.defaultFamily = "Other";
    ASSERT_TRUE(renderer.prepare(std::move(other)));
    EXPECT_NE(renderer.engineIdentity(), engine);
    engine = renderer.engineIdentity();
    ASSERT_TRUE(renderer.prepare(snapshotWith(heavyScript(), testFont())));
    EXPECT_NE(renderer.engineIdentity(), engine);
}

// A reload of the system's fonts (legacy RefreshVideo(true) when the fonts
// on the system change) reaches a kept renderer: the next prepare() of the
// same script with the same leases makes a new library and renderer, so
// fontconfig and libass's face and glyph caches start over, and draws as a
// new renderer does. Until that prepare() the kept one goes on drawing.
TEST(LibassRenderer, AReloadOfTheSystemFontsMakesTheRendererAnew)
{
    const auto font = testFont();
    const auto snapshotOf = [&] {
        return RenderSnapshot{bytesOf(heavyScript()), {FontLease{"Titillium Web", font}}, "Titillium Web", false};
    };
    backends::LibassRenderer renderer;
    ASSERT_TRUE(renderer.prepare(snapshotOf()));
    const void *engine = renderer.engineIdentity();
    ASSERT_TRUE(renderer.render(core::DocumentTime(2'000'000), 640, 360));
    application::reloadSubtitleFonts();
    EXPECT_EQ(renderer.engineIdentity(), engine);
    ASSERT_TRUE(renderer.render(core::DocumentTime(2'500'000), 640, 360));
    ASSERT_TRUE(renderer.prepare(snapshotOf()));
    EXPECT_NE(renderer.engineIdentity(), engine);
    EXPECT_EQ(renderer.parsedTracks(), 1u); // the old tracks went with the old library
    const auto frame = renderer.render(core::DocumentTime(2'000'000), 640, 360);
    ASSERT_TRUE(frame);
    EXPECT_FALSE(frame->changed.empty());
    backends::LibassRenderer fresh;
    ASSERT_TRUE(fresh.prepare(snapshotOf()));
    const auto expected = fresh.render(core::DocumentTime(2'000'000), 640, 360);
    ASSERT_TRUE(expected);
    EXPECT_TRUE(frame->pixels == expected->pixels);
    // No further reload: the renderer is kept again.
    engine = renderer.engineIdentity();
    ASSERT_TRUE(renderer.prepare(snapshotOf()));
    EXPECT_EQ(renderer.engineIdentity(), engine);
}

// Edits, style changes and script resolution changes on a kept renderer
// draw exactly what a new renderer draws; going back to a script parsed
// before finds its track, and draws as it did.
TEST(LibassRenderer, KeptRendererDrawsEditsStylesAndResolutionsAsANewOne)
{
    const auto font = testFont();
    const auto snapshotOf = [&](const std::string &script) {
        return RenderSnapshot{bytesOf(script), {FontLease{"Titillium Web", font}}, "Titillium Web", false};
    };
    const std::string base = heavyScript();
    std::string edited = base;
    edited.replace(edited.find("Rotating karaoke line"), 21, "Edited karaoke text!!");
    const std::vector<std::pair<const char *, std::string>> scripts = {
        {"base", base},
        {"edited text", edited},
        {"base again", base},
        {"style colour", heavyScript(640, 360, "&H000000FF")},
        {"script resolution", heavyScript(1280, 720)},
    };
    backends::LibassRenderer kept;
    std::vector<std::vector<std::uint8_t>> drawn;
    for (const auto &[what, script] : scripts) {
        ASSERT_TRUE(kept.prepare(snapshotOf(script))) << what;
        if (drawn.size() == 2)
            EXPECT_EQ(kept.parsedTracks(), 2u) << "base came back from the cache";
        backends::LibassRenderer fresh;
        ASSERT_TRUE(fresh.prepare(snapshotOf(script))) << what;
        std::vector<std::uint8_t> all;
        for (long long ms : {1'500LL, 4'200LL, 6'000LL}) {
            const auto a = kept.render(core::DocumentTime(ms * 1000), 640, 360);
            const auto b = fresh.render(core::DocumentTime(ms * 1000), 640, 360);
            ASSERT_TRUE(a && b);
            ASSERT_TRUE(a->pixels == b->pixels) << what << " at " << ms << " ms";
            all.insert(all.end(), a->pixels.begin(), a->pixels.end());
        }
        drawn.push_back(std::move(all));
    }
    EXPECT_EQ(drawn[2], drawn[0]);
    for (std::size_t i : {1u, 3u, 4u})
        EXPECT_NE(drawn[i], drawn[0]) << scripts[i].first << " changed nothing";
    EXPECT_EQ(kept.parsedTracks(), 3u);
}

// Legacy ParsedScripts: three tracks, the least recently used going first.
TEST(LibassRenderer, TheTrackCacheKeepsTheThreeLatestScripts)
{
    const auto font = testFont();
    backends::LibassRenderer renderer;
    const auto prepare = [&](int n) {
        return renderer.prepare(RenderSnapshot{scriptOf("Dialogue: 0,0:00:00.00,0:00:05.00,D,,0,0,0,,{\\pos(10,10)}" +
                                                        std::to_string(n) + "\n"),
                                               {FontLease{"Titillium Web", font}}, "Titillium Web", false});
    };
    for (int n = 0; n < 5; ++n) {
        ASSERT_TRUE(prepare(n));
        EXPECT_EQ(renderer.parsedTracks(), static_cast<std::size_t>(std::min(n + 1, 3)));
    }
    // 2, 3 and 4 are kept; 3 again is found, then 0 is parsed and 2 goes.
    ASSERT_TRUE(prepare(3));
    EXPECT_EQ(renderer.parsedTracks(), 3u);
    ASSERT_TRUE(prepare(0));
    EXPECT_EQ(renderer.parsedTracks(), 3u);
    const auto frame = renderer.render(core::DocumentTime(1'000'000), 96, 64);
    ASSERT_TRUE(frame);
    EXPECT_FALSE(frame->empty);
}

// A new frame size: the whole frame is reported, and drawn as a new
// renderer draws it.
TEST(LibassRenderer, ANewSizeDrawsAsANewRenderer)
{
    const auto font = testFont();
    const auto snapshot = [&] {
        return RenderSnapshot{bytesOf(heavyScript()), {FontLease{"Titillium Web", font}}, "Titillium Web", false};
    };
    backends::LibassRenderer renderer;
    ASSERT_TRUE(renderer.prepare(snapshot()));
    ASSERT_TRUE(renderer.render(core::DocumentTime(3'000'000), 640, 360));
    const auto smaller = renderer.render(core::DocumentTime(3'000'000), 320, 180);
    backends::LibassRenderer fresh;
    ASSERT_TRUE(fresh.prepare(snapshot()));
    const auto reference = fresh.render(core::DocumentTime(3'000'000), 320, 180);
    ASSERT_TRUE(smaller && reference);
    EXPECT_EQ(smaller->changed, (std::vector<PixelRect>{{0, 0, 320, 180}}));
    EXPECT_TRUE(smaller->pixels == reference->pixels);
    EXPECT_EQ(smaller->pixels.size(), 320u * 180u * 4u);
}

// 38c (user decision, 2026-10-14): the fonts a script attaches reach libass
// as the application hands them over: the renderer script without [Fonts],
// and the decoded fonts as leases. tests/fixtures/attachments/attached.ass
// draws "HH" in HikariAttachedTest, a family only its [Fonts] has, whose
// letters are filled boxes; with the system's fonts another face draws "HH"
// unless the attached font is leased (libass reads no font from a script).
TEST(LibassRenderer, DrawsTheScriptsAttachedFonts)
{
    QFile f(QStringLiteral(HIKARI_ATTACHMENT_FIXTURES "/attached.ass"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    std::vector<std::byte> source(static_cast<std::size_t>(data.size()));
    std::memcpy(source.data(), data.constData(), source.size());
    const core::Document document = core::loadAss(source).document;
    std::vector<FontLease> leases;
    for (auto &font : core::attachedFonts(document))
        leases.push_back({font.name, std::make_shared<const std::vector<std::byte>>(std::move(font.bytes))});
    ASSERT_EQ(leases.size(), 1u);
    const auto script = core::encodeAss(document, core::AssSaveOptions{.renderer = true});

    struct Drawn {
        int left = 640, top = 360, right = -1, bottom = -1;
        long covered = 0;
        double fill() const { return right < left ? 0 : double(covered) / ((right - left + 1.0) * (bottom - top + 1.0)); }
    };
    const auto draw = [&](std::vector<FontLease> fonts) {
        backends::LibassRenderer renderer;
        EXPECT_TRUE(renderer.prepare(RenderSnapshot{script, std::move(fonts), {}, true}));
        const auto frame = renderer.render(core::DocumentTime(1'000'000), 640, 360);
        Drawn d;
        EXPECT_TRUE(frame);
        if (!frame)
            return d;
        for (int y = 0; y < frame->height; ++y)
            for (int x = 0; x < 360; ++x) // the "HH" Line's part of the frame
                if (frame->pixels[std::size_t(y) * frame->stride + std::size_t(x) * 4 + 3] >= 128) {
                    ++d.covered;
                    d.left = std::min(d.left, x);
                    d.right = std::max(d.right, x);
                    d.top = std::min(d.top, y);
                    d.bottom = std::max(d.bottom, y);
                }
        return d;
    };
    const Drawn without = draw({});
    std::fprintf(stderr, "38c libass: without the lease %ld pixels, %.2f of their box; ", without.covered, without.fill());
    EXPECT_GT(without.covered, 0);
    EXPECT_LT(without.fill(), 0.75);
    const Drawn with = draw(leases);
    std::fprintf(stderr, "with it %ld pixels, %.2f of [%d,%d]-[%d,%d]\n", with.covered, with.fill(), with.left, with.top,
                 with.right, with.bottom);
    // two boxes of 90 by 70 pixels (100 px text, a 1000-unit em), 10 apart
    EXPECT_GT(with.fill(), 0.9);
    EXPECT_GT(with.right - with.left, 170);
    EXPECT_LT(with.right - with.left, 200);
    EXPECT_GT(with.bottom - with.top, 60);
    EXPECT_LT(with.bottom - with.top, 80);
}
