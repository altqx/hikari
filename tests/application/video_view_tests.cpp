#include "hikari/application/video_controls.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/script_properties.h"
#include "hikari/application/video_matrix.h"
#include "hikari/application/video_snapshot.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>

using namespace hikari::application;
using visual::IntRect;
using visual::SourceGeometry;
using visual::VideoView;

namespace {

VideoView fit16x9()
{
    VideoView v;
    v.setClient(640, 400, 40);
    v.open(SourceGeometry{1280, 720, 0, 1});
    v.setScript(1920, 1080);
    return v;
}

}

TEST(VideoZoom, WheelStepsAreTenthsBetweenOneAndTen)
{
    EXPECT_FLOAT_EQ(wheelZoomPercent(1.f, 1), 1.1f);
    EXPECT_EQ(wheelZoomPercent(1.f, -1), 1.f);
    EXPECT_EQ(wheelZoomPercent(9.95f, 3), 10.f);
    EXPECT_EQ(wheelZoomPercent(1.f, 15), 2.5f);
    EXPECT_EQ(wheelZoomPercent(wheelZoomPercent(1.f, 1), 1), 1.f + 0.1f + 0.1f);
}

TEST(VideoZoom, WheelAtAPointMatchesTheLegacyCapture)
{
    VideoView v = fit16x9();
    v.zoomAt(wheelZoomPercent(v.zoomPercent(), 15), 200, 120);
    EXPECT_EQ(v.sourceRect(), (IntRect{144, 96, 656, 384}));
    EXPECT_EQ(v.zoomRect(), (visual::EdgeRect{72, 48, 328, 192}));
    EXPECT_EQ(v.zoomPercent(), 2.5f);
    EXPECT_FALSE(v.zoomMode());
}

TEST(VideoZoom, ZoomModeAndResetMatchTheLegacyCaptures)
{
    VideoView v = fit16x9();
    v.toggleZoom(0);
    EXPECT_TRUE(v.zoomMode());
    EXPECT_EQ(v.sourceRect(), (IntRect{320, 180, 960, 540}));
    EXPECT_EQ(v.zoomRect(), (visual::EdgeRect{160, 90, 480, 270}));
    v.resetZoom();
    EXPECT_TRUE(v.zoomMode());
    EXPECT_EQ(v.sourceRect(), (IntRect{0, 0, 1280, 720}));
    EXPECT_EQ(v.zoomPercent(), 1.f);
    v.toggleZoom(0);
    EXPECT_FALSE(v.zoomMode());

    VideoView three = fit16x9();
    three.toggleZoom(300);
    EXPECT_EQ(three.zoomPercent(), std::nextafter(3.f, 0.f));
    EXPECT_EQ(static_cast<int>(three.zoomPercent() * 100), 299);
    VideoView odd = fit16x9();
    odd.toggleZoom(1200);
    EXPECT_EQ(odd.zoomPercent(), 2.f);
}

TEST(VideoZoom, ResetGivesTheWholeFrameAtEverySize)
{
    int legacyShort = 0;
    for (int width = 40; width <= 700; ++width) {
        VideoView v;
        v.setClient(width, 1000, 40);
        v.open(SourceGeometry{1280, 720, 0, 1});
        v.zoomAt(2.f, width / 2, 10);
        v.resetZoom();
        ASSERT_EQ(v.sourceRect(), (IntRect{0, 0, 1280, 720})) << "panel width " << width;
        const auto r = v.videoRect();
        const int w = r.right - r.left, h = r.bottom - r.top;
        const float xx = static_cast<float>(w) / 1280.f, yy = static_cast<float>(h) / 720.f;
        if (static_cast<int>(static_cast<float>(w) / xx) != 1280 || static_cast<int>(static_cast<float>(h) / yy) != 720)
            ++legacyShort;
    }
    EXPECT_GT(legacyShort, 0);
}

TEST(VideoZoom, ZoomModeDragsMatchTheLegacyCaptures)
{
    VideoView v = fit16x9();
    v.toggleZoom(0);
    v.zoomPress(320, 180);
    v.zoomDrag(200, 120);
    v.toggleZoom(0);
    EXPECT_EQ(v.sourceRect(), (IntRect{80, 60, 720, 420}));
    EXPECT_EQ(v.zoomRect(), (visual::EdgeRect{40, 30, 360, 210}));

    VideoView c;
    c.setClient(640, 400, 40);
    c.open(SourceGeometry{1280, 720, 0, 1});
    c.zoomAt(4, 600, 300);
    c.toggleZoom(0);
    c.zoomPress(400, 200);
    c.zoomDrag(900, 700);
    c.toggleZoom(0);
    EXPECT_EQ(c.sourceRect(), (IntRect{960, 540, 1280, 720}));
    EXPECT_EQ(c.zoomRect(), (visual::EdgeRect{480, 270, 640, 360}));
}

TEST(VideoZoom, ZoomModeWheelGrowsAndShrinksTheRectangle)
{
    VideoView v = fit16x9();
    v.toggleZoom(0);
    v.zoomWheel(2);
    const float ar = 640.f / 360.f;
    EXPECT_EQ(v.zoomRect().x, 150.f);
    EXPECT_EQ(v.zoomRect().width, 490.f);
    EXPECT_FLOAT_EQ(v.zoomRect().y, 90.f - 10 / ar);
    EXPECT_FLOAT_EQ(v.zoomRect().height, 270.f + 10 / ar);
    v.zoomWheel(-40);
    EXPECT_EQ(v.zoomRect().x, 150.f);
}

TEST(VideoAspect, SliderValuesAndLabels)
{
    EXPECT_EQ(aspectSliderValue(0.5625f), 393750);
    EXPECT_EQ(aspectFromSlider(350000), 0.5f);
    EXPECT_EQ(aspectFromSlider(875000), 1.25f);
    EXPECT_EQ(aspectLabelNumber(0.5625f), "1.778");
    EXPECT_EQ(aspectLabelNumber(aspectFromSlider(100000)), "7.000");
    EXPECT_EQ(aspectLabelNumber(0.f), "  inf");

    VideoView v = fit16x9();
    v.setAspectRatio(aspectFromSlider(350000));
    EXPECT_EQ(v.videoRect(), (IntRect{0, 20, 640, 340}));
    EXPECT_EQ(v.zoomMove(), (visual::PointF{0, -20}));
    VideoView tall;
    tall.setClient(400, 700, 40);
    tall.open(SourceGeometry{1920, 1080, 0, 1});
    tall.setAspectRatio(aspectFromSlider(875000));
    EXPECT_EQ(tall.videoRect(), (IntRect{0, 80, 400, 580}));
    VideoView none;
    none.setAspectRatio(0.25f);
    EXPECT_EQ(none.aspectRatio(), 0.f);
}

TEST(VideoVolume, KeysAndWheelStepAsLegacy)
{
    EXPECT_EQ(videoVolumeKeyStep(0, true), std::nullopt);
    EXPECT_EQ(videoVolumeKeyStep(-1, true), std::nullopt);
    EXPECT_EQ(videoVolumeKeyStep(-2, true), 0);
    EXPECT_EQ(videoVolumeKeyStep(0, false), -2);
    EXPECT_EQ(videoVolumeKeyStep(-88, false), -90);
    EXPECT_EQ(videoVolumeKeyStep(-90, false), std::nullopt);
    EXPECT_EQ(videoVolumeWheelStep(0, -1), -3);
    EXPECT_EQ(videoVolumeWheelStep(-2, 1), 0);
    EXPECT_EQ(videoVolumeWheelStep(-84, -1), -86);
    EXPECT_EQ(videoVolumeWheelStep(-90, 1), -86);
    EXPECT_EQ(videoVolumeWheelStep(-40, 2), -34);
    EXPECT_DOUBLE_EQ(videoVolumeGain(0), 1.0);
    EXPECT_NEAR(videoVolumeGain(-10), std::pow(10.0, -1.0 / 20.0), 1e-12);
    EXPECT_NEAR(videoVolumeGain(-86), std::pow(10.0, -73.96 / 20.0), 1e-15);
}

TEST(VideoSnapshot, NamesNumberTheFilesFromTheirNames)
{
    EXPECT_EQ(snapshotPattern("/videos/episode.01.mkv"), "episode.01_*_*.png");
    EXPECT_EQ(snapshotPath("/videos/episode.01.mkv", 1, 3'723'045), "/videos/episode.01_1_01;02;03,045.png");
    EXPECT_EQ(snapshotPath("C:\\v\\a.mkv", 12, 999), "C:\\v\\a_12_00;00;00,999.png");
    EXPECT_EQ(nextSnapshotNumber("/v/a.mkv", {}), 1);
    EXPECT_EQ(nextSnapshotNumber("/v/a.mkv", {"/v/a_1_00;00;01,000.png", "/v/a_3_00;00;01,000.png"}), 2);
    EXPECT_EQ(nextSnapshotNumber("/v/a.mkv", {"/v/a_2_x.png", "/v/a_1_x.png", "/v/a_1_y.png"}), 3);
    EXPECT_EQ(nextSnapshotNumber("C:\\v\\a.mkv", {"C:\\v\\a_1_x.png"}), 2);
    EXPECT_EQ(nextSnapshotNumber("/d_2_x/a.mkv", {"/d_2_x/a_1_t.png", "/d_2_x/a_3_t.png"}), 2);
    EXPECT_EQ(nextSnapshotNumber("/v/show_01.mkv", {"/v/show_01_1_t.png", "/v/show_01_2_t.png"}), 3);
    EXPECT_EQ(nextSnapshotNumber("/v/a.mkv", {"/v/a_x_1_t.png", "/v/a__t.png"}), 1);
}

TEST(VideoSnapshot, NeverOverwritesAFile)
{
    const std::vector<std::string> taken{"/v/a_2_00;00;00,000.png", "/v/a_3_00;00;00,000.png"};
    const auto exists = [&](const std::string &path) {
        return path == "/v/a_1_00;00;00,000.png" || std::find(taken.begin(), taken.end(), path) != taken.end();
    };
    EXPECT_EQ(nextSnapshotPath("/v/a.mkv", {"/v/a_2_x.png"}, 0, exists), "/v/a_4_00;00;00,000.png");
    EXPECT_EQ(nextSnapshotPath("/v/a.mkv", {}, 1001, exists), "/v/a_1_00;00;01,001.png");
}

TEST(VideoSnapshot, ImageIsTheFrameWithTheOverlayComposited)
{
    IndexedFrame f;
    f.width = 2;
    f.height = 1;
    f.stride = 12;
    f.bgra.assign(12, std::byte{0});
    const std::uint8_t px[8] = {10, 20, 30, 0, 200, 100, 50, 255};
    for (int i = 0; i < 8; ++i)
        f.bgra[static_cast<std::size_t>(i)] = std::byte{px[i]};
    const auto plain = snapshotImage(f, nullptr);
    ASSERT_EQ(plain.rgb.size(), 6u);
    EXPECT_EQ(plain.rgb, (std::vector<std::uint8_t>{30, 20, 10, 50, 100, 200}));

    OverlayFrame o;
    o.width = 2;
    o.height = 1;
    o.stride = 8;
    o.empty = false;
    o.pixels = {128, 128, 128, 128, 0, 0, 0, 0};
    const auto subbed = snapshotImage(f, &o);
    const auto mix = [](int over, int under) { return over + (under * (255 - 128) + 127) / 255; };
    EXPECT_EQ(subbed.rgb, (std::vector<std::uint8_t>{std::uint8_t(mix(128, 30)), std::uint8_t(mix(128, 20)),
                                                     std::uint8_t(mix(128, 10)), 50, 100, 200}));
    o.empty = true;
    EXPECT_EQ(snapshotImage(f, &o).rgb, plain.rgb);
}

TEST(VideoSnapshot, CompositeOverlayLaysItAsTheSnapshotDoes)
{
    constexpr int kW = 37, kH = 11, kStride = kW * 4 + 12;
    std::uint32_t seed = 12345;
    const auto next = [&] { return (seed = seed * 1664525u + 1013904223u) >> 24; };
    IndexedFrame f;
    f.width = kW;
    f.height = kH;
    f.stride = kStride;
    f.bgra.resize(static_cast<std::size_t>(kStride) * kH);
    for (auto &b : f.bgra)
        b = std::byte{static_cast<std::uint8_t>(next())};
    std::vector<std::uint8_t> over(static_cast<std::size_t>(kW) * kH * 4, 0);
    for (std::size_t i = 0; i < over.size(); i += 4) {
        if (next() % 3 == 0)
            continue;
        const std::uint8_t a = static_cast<std::uint8_t>(next());
        for (int c = 0; c < 3; ++c)
            over[i + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(a ? next() % (a + 1u) : 0);
        over[i + 3] = a;
    }
    OverlayFrame o;
    o.width = kW;
    o.height = kH;
    o.stride = kW * 4;
    o.empty = false;
    o.pixels = OverlayPixels(over);
    const SnapshotImage expected = snapshotImage(f, &o);
    std::vector<std::uint8_t> picture(f.bgra.size());
    std::memcpy(picture.data(), f.bgra.data(), picture.size());
    compositeOverlay(o, picture.data(), kW, kH, kStride);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const std::uint8_t *px = picture.data() + y * kStride + x * 4;
            const std::uint8_t *rgb = expected.rgb.data() + (static_cast<std::size_t>(y) * kW + x) * 3;
            ASSERT_EQ(px[2], rgb[0]) << x << "," << y;
            ASSERT_EQ(px[1], rgb[1]) << x << "," << y;
            ASSERT_EQ(px[0], rgb[2]) << x << "," << y;
            ASSERT_EQ(px[3], std::to_integer<std::uint8_t>(f.bgra[static_cast<std::size_t>(y * kStride + x * 4 + 3)]));
        }
    const std::vector<std::uint8_t> before = picture;
    o.empty = true;
    compositeOverlay(o, picture.data(), kW, kH, kStride);
    o.empty = false;
    compositeOverlay(o, picture.data(), kW - 1, kH, kStride);
    EXPECT_EQ(picture, before);
}

TEST(VideoMatrix, Names)
{
    using namespace ffms_colour;
    EXPECT_EQ(colourMatrixName(kBt709, kRangeMpeg), "TV.709");
    EXPECT_EQ(colourMatrixName(kBt709, kRangeJpeg), "PC.709");
    EXPECT_EQ(colourMatrixName(kBt470bg, kRangeUnspecified), "TV.601");
    EXPECT_EQ(colourMatrixName(kSmpte170m, kRangeMpeg), "TV.601");
    EXPECT_EQ(colourMatrixName(kFcc, kRangeMpeg), "TV.FCC");
    EXPECT_EQ(colourMatrixName(kSmpte240m, kRangeJpeg), "PC.240M");
    EXPECT_EQ(colourMatrixName(kRgb, kRangeMpeg), "None");
    EXPECT_EQ(colourMatrixName(kUnspecified, kRangeMpeg), "None");
}

TEST(VideoMatrix, DocumentMatrix)
{
    EXPECT_EQ(documentVideoMatrix(true, std::nullopt, true), "TV.601");
    EXPECT_EQ(documentVideoMatrix(true, "None", true), "TV.601");
    EXPECT_EQ(documentVideoMatrix(true, "", true), "TV.601");
    EXPECT_EQ(documentVideoMatrix(true, "None", false), "None");
    EXPECT_EQ(documentVideoMatrix(true, "TV.709", true), "TV.709");
    EXPECT_EQ(documentVideoMatrix(true, "PC.709", false), "PC.709");
    EXPECT_EQ(documentVideoMatrix(false, "TV.709", true), "");
}

namespace {

hikari::core::Document loadAss(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return hikari::core::loadAss(bytes).document;
}

void chooseMatrix(EditSession &session, std::u8string_view name)
{
    auto p = scriptProperties(session.document());
    for (std::size_t i = 0; i < matrixNames().size(); ++i)
        if (matrixNames()[i] == name)
            p.matrix = static_cast<int>(i);
    ASSERT_TRUE(applyScriptProperties(session, p, ScriptPropertiesEdits{}, false).has_value());
}

constexpr std::string_view kNoneAss = "[Script Info]\nScriptType: v4.00+\nYCbCr Matrix: None\n\n[Events]\n"
                                      "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                                      "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n";

}

TEST(VideoMatrix, SessionMatrixKeepsAChosenNone)
{
    EditSession session{loadAss(kNoneAss)};
    EXPECT_EQ(sessionVideoMatrix(session), "TV.601");
    chooseMatrix(session, u8"TV.709");
    EXPECT_EQ(sessionVideoMatrix(session), "TV.709");
    chooseMatrix(session, u8"None");
    EXPECT_EQ(session.document().scriptInfo(u8"YCbCr Matrix"), std::optional<std::u8string>(u8"None"));
    EXPECT_EQ(sessionVideoMatrix(session), "None");
    auto p = scriptProperties(session.document());
    p.title = u8"Edited";
    ScriptPropertiesEdits title;
    title.title = true;
    ASSERT_TRUE(applyScriptProperties(session, p, title, false).has_value());
    EXPECT_EQ(sessionVideoMatrix(session), "None");
    ASSERT_TRUE(session.undo());
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(sessionVideoMatrix(session), "TV.709");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(sessionVideoMatrix(session), "TV.601");
    ASSERT_TRUE(session.redo());
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(sessionVideoMatrix(session), "None");

    EditSession bare{loadAss("[Script Info]\nScriptType: v4.00+\n\n[Events]\n")};
    EXPECT_EQ(sessionVideoMatrix(bare), "TV.601");
    chooseMatrix(bare, u8"TV.601");
    chooseMatrix(bare, u8"None");
    EXPECT_EQ(sessionVideoMatrix(bare), "None");

    using namespace ffms_colour;
    LegacyColourMatrix m;
    m.open(kBt709, kRangeMpeg, 1920, 1080, "TV.601");
    const auto change = m.set("None");
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->input, (LegacyColourMatrix::Input{kBt709, kRangeMpeg}));
}

TEST(VideoMatrix, OpenAppliesTheDocumentMatrix)
{
    using namespace ffms_colour;
    using Input = LegacyColourMatrix::Input;
    LegacyColourMatrix m;
    EXPECT_EQ(m.open(kBt709, kRangeMpeg, 1920, 1080, "TV.709"), (Input{kBt709, kRangeMpeg}));
    EXPECT_EQ(m.applied(), "TV.709");
    EXPECT_EQ(m.open(kBt709, kRangeJpeg, 1920, 1080, "TV.601"), (Input{kBt470bg, kRangeJpeg}));
    EXPECT_EQ(m.applied(), "PC.601");
    EXPECT_EQ(m.source(), "PC.709");
    EXPECT_EQ(m.open(kSmpte170m, kRangeMpeg, 720, 480, "TV.709"), std::nullopt);
    EXPECT_EQ(m.applied(), "TV.709");
    EXPECT_EQ(m.source(), "TV.601");
    EXPECT_EQ(m.open(kBt709, kRangeMpeg, 1920, 1080, "PC.709"), std::nullopt);
    EXPECT_EQ(m.applied(), "TV.709");
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1280, 720, "TV.709"), (Input{kBt709, kRangeUnspecified}));
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1024, 600, "TV.709"), (Input{kBt709, kRangeUnspecified}));
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1024, 599, "TV.709"), (Input{kBt470bg, kRangeUnspecified}));
    EXPECT_EQ(m.source(), "TV.601");
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1280, 720, ""), (Input{kBt709, kRangeUnspecified}));
    EXPECT_EQ(m.applied(), "TV.709");
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1920, 1080, "PC.709"), (Input{kBt709, kRangeUnspecified}));
    EXPECT_EQ(m.applied(), "TV.709");
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1920, 1080, "TV.601"), (Input{kBt470bg, kRangeUnspecified}));
    EXPECT_EQ(m.applied(), "TV.601");
    EXPECT_EQ(m.open(kBt709, kRangeMpeg, 1920, 1080, ""), std::nullopt);
}

TEST(VideoMatrix, ChangesFollowLegacyState)
{
    using namespace ffms_colour;
    using Input = LegacyColourMatrix::Input;
    LegacyColourMatrix m;
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1920, 1080, ""), (Input{kBt709, kRangeUnspecified}));
    EXPECT_EQ(m.set("TV.709"), std::nullopt);
    auto change = m.set("TV.601");
    ASSERT_TRUE(change);
    EXPECT_EQ(change->input, (Input{kBt470bg, kRangeUnspecified}));
    EXPECT_EQ(change->previous, "TV.709");
    EXPECT_EQ(m.applied(), "TV.601");
    change = m.set("TV.709");
    ASSERT_TRUE(change);
    EXPECT_EQ(change->input, (Input{kBt709, kRangeUnspecified}));
    change = m.set("None");
    ASSERT_TRUE(change);
    EXPECT_EQ(change->input, (Input{kBt709, kRangeUnspecified}));
    EXPECT_EQ(m.applied(), "None");
    change = m.set("TV.601");
    ASSERT_TRUE(change);
    m.revert(*change);
    EXPECT_EQ(m.applied(), "None");

    LegacyColourMatrix sd;
    sd.open(kBt470bg, kRangeMpeg, 720, 576, "TV.601");
    EXPECT_EQ(sd.set("TV.709"), std::nullopt);
    EXPECT_EQ(sd.applied(), "TV.601");
}
