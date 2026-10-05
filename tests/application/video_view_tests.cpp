// V4 (#183): the video view's legacy arithmetic at 20d647c4 — the wheel's
// zoom and the zoom commands on the shared view (checked against the T1
// probe's captures of legacy RendererVideo), the AspectRatioDialog slider,
// the video volume steps, the snapshot image and file naming
// (RendererVideo::SaveFrame) and the Script properties YCbCr matrix
// (ProviderFFMS2::Init / SetColorSpace).
#include "hikari/application/video_controls.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/script_properties.h"
#include "hikari/application/video_matrix.h"
#include "hikari/application/video_snapshot.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>

using namespace hikari::application;
using visual::IntRect;
using visual::SourceGeometry;
using visual::VideoView;

namespace {

// The capture's client (640x400 with a 40-pixel panel) over a 1280x720 frame.
VideoView fit16x9()
{
    VideoView v;
    v.setClient(640, 400, 40);
    v.open(SourceGeometry{1280, 720, 0, 1});
    v.setScript(1920, 1080);
    return v;
}

} // namespace

// The wheel over the video (VideoBox.cpp:510-516): MID(1, zoom + step / 10, 10)
// where step is the event's rotation over its delta.
TEST(VideoZoom, WheelStepsAreTenthsBetweenOneAndTen)
{
    EXPECT_FLOAT_EQ(wheelZoomPercent(1.f, 1), 1.1f);
    EXPECT_EQ(wheelZoomPercent(1.f, -1), 1.f); // no zoom below 1
    EXPECT_EQ(wheelZoomPercent(9.95f, 3), 10.f);
    EXPECT_EQ(wheelZoomPercent(1.f, 15), 2.5f);
    // Legacy's float steps: 1 + 0.1 + 0.1 is not 1.2.
    EXPECT_EQ(wheelZoomPercent(wheelZoomPercent(1.f, 1), 1), 1.f + 0.1f + 0.1f);
}

// A wheel of 15 steps at 200,120 is the probe's `zoom 2.5 200 120`
// (local-t1-visual-20261005, case zoom-at-point): source 144,96-656,384,
// zoom rectangle 72,48-328,192.
TEST(VideoZoom, WheelAtAPointMatchesTheLegacyCapture)
{
    VideoView v = fit16x9();
    v.zoomAt(wheelZoomPercent(v.zoomPercent(), 15), 200, 120);
    EXPECT_EQ(v.sourceRect(), (IntRect{144, 96, 656, 384}));
    EXPECT_EQ(v.zoomRect(), (visual::EdgeRect{72, 48, 328, 192}));
    EXPECT_EQ(v.zoomPercent(), 2.5f);
    EXPECT_FALSE(v.zoomMode()); // the wheel zooms without the zoom mode
}

// GLOBAL_VIDEO_ZOOM with VIDEO_ZOOM_PERCENT unset (case zoom-toggle): the
// zoom mode at 2x around the centre; GLOBAL_RESET_VIDEO_ZOOM (case
// zoom-reset) gives the whole frame back and leaves the mode as it was.
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

    // VIDEO_ZOOM_PERCENT 300: 3x; outside 100..1100 legacy takes 2x.
    VideoView three = fit16x9();
    three.toggleZoom(300);
    // Legacy's float arithmetic: 640 / (426.67 - 213.33) is 2.9999998, not 3
    // (RendererVideo.cpp:760), so the status bar would read 299.
    EXPECT_EQ(three.zoomPercent(), std::nextafter(3.f, 0.f));
    EXPECT_EQ(static_cast<int>(three.zoomPercent() * 100), 299);
    VideoView odd = fit16x9();
    odd.toggleZoom(1200);
    EXPECT_EQ(odd.zoomPercent(), 2.f);
}

// The zoom mode's drag (case pan-drag: press 320,180, move to 200,120) and
// a pan pushed past the edge (case pan-clamped).
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

// The zoom mode's wheel (RendererVideo.cpp:947-952): five pixels a step on
// each side, the height by the video's ratio, kept at least 100x56.
TEST(VideoZoom, ZoomModeWheelGrowsAndShrinksTheRectangle)
{
    VideoView v = fit16x9();
    v.toggleZoom(0); // 160,90-480,270
    v.zoomWheel(2);
    const float ar = 640.f / 360.f;
    EXPECT_EQ(v.zoomRect().x, 150.f);
    EXPECT_EQ(v.zoomRect().width, 490.f);
    EXPECT_FLOAT_EQ(v.zoomRect().y, 90.f - 10 / ar);
    EXPECT_FLOAT_EQ(v.zoomRect().height, 270.f + 10 / ar);
    v.zoomWheel(-40); // 100 pixels in: under the minimum, nothing changes
    EXPECT_EQ(v.zoomRect().x, 150.f);
}

// AspectRatioDialog (VideoBox.cpp:106-125): value = ratio * 700000, the ratio
// value / 700000, the label 1 / ratio as "%5.3f". The probe's
// aspect-override (0.5) and no-sar-aspect-tall (1.25) are slider values
// 350000 and 875000.
TEST(VideoAspect, SliderValuesAndLabels)
{
    EXPECT_EQ(aspectSliderValue(0.5625f), 393750);
    EXPECT_EQ(aspectFromSlider(350000), 0.5f);
    EXPECT_EQ(aspectFromSlider(875000), 1.25f);
    EXPECT_EQ(aspectLabelNumber(0.5625f), "1.778");
    EXPECT_EQ(aspectLabelNumber(aspectFromSlider(100000)), "7.000");
    EXPECT_EQ(aspectLabelNumber(0.f), "  inf"); // no video opened yet

    VideoView v = fit16x9();
    v.setAspectRatio(aspectFromSlider(350000));
    EXPECT_EQ(v.videoRect(), (IntRect{0, 20, 640, 340}));
    EXPECT_EQ(v.zoomMove(), (visual::PointF{0, -20}));
    VideoView tall;
    tall.setClient(400, 700, 40);
    tall.open(SourceGeometry{1920, 1080, 0, 1});
    tall.setAspectRatio(aspectFromSlider(875000));
    EXPECT_EQ(tall.videoRect(), (IntRect{0, 80, 400, 580}));
    // Without a video the dialog changes nothing (VideoBox::SetAspectRatio).
    VideoView none;
    none.setAspectRatio(0.25f);
    EXPECT_EQ(none.aspectRatio(), 0.f);
}

// VIDEO_VOLUME_PLUS / _MINUS (VideoBox.cpp:1181-1233) step 2 while the value
// stays below 1 / above -91; the panel's wheel (VolSlider, VideoSlider.cpp:370-382)
// steps 3 and snaps to the ends.
TEST(VideoVolume, KeysAndWheelStepAsLegacy)
{
    EXPECT_EQ(videoVolumeKeyStep(0, true), std::nullopt);
    EXPECT_EQ(videoVolumeKeyStep(-1, true), std::nullopt); // 1 is not below 1
    EXPECT_EQ(videoVolumeKeyStep(-2, true), 0);
    EXPECT_EQ(videoVolumeKeyStep(0, false), -2);
    EXPECT_EQ(videoVolumeKeyStep(-88, false), -90); // the keys go below the slider's -86
    EXPECT_EQ(videoVolumeKeyStep(-90, false), std::nullopt);
    EXPECT_EQ(videoVolumeWheelStep(0, -1), -3);
    EXPECT_EQ(videoVolumeWheelStep(-2, 1), 0); // within three of the top
    EXPECT_EQ(videoVolumeWheelStep(-84, -1), -86);
    EXPECT_EQ(videoVolumeWheelStep(-90, 1), -86); // the wheel brings the keys' -90 back to -86
    EXPECT_EQ(videoVolumeWheelStep(-40, 2), -34);
    // -(pos * pos) hundredths of a dB.
    EXPECT_DOUBLE_EQ(videoVolumeGain(0), 1.0);
    EXPECT_NEAR(videoVolumeGain(-10), std::pow(10.0, -1.0 / 20.0), 1e-12);
    EXPECT_NEAR(videoVolumeGain(-86), std::pow(10.0, -73.96 / 20.0), 1e-15);
}

// SaveFrame's file name (RendererVideo.cpp:1228-1266).
TEST(VideoSnapshot, NamesNumberTheFilesAsLegacy)
{
    EXPECT_EQ(snapshotPattern("/videos/episode.01.mkv"), "episode.01_*_*.png");
    EXPECT_EQ(snapshotPath("/videos/episode.01.mkv", 1, 3'723'045), "/videos/episode.01_1_01;02;03,045.png");
    EXPECT_EQ(snapshotPath("C:\\v\\a.mkv", 12, 999), "C:\\v\\a_12_00;00;00,999.png");
    EXPECT_EQ(nextSnapshotNumber({}), 1);
    EXPECT_EQ(nextSnapshotNumber({"/v/a_1_00;00;01,000.png", "/v/a_3_00;00;01,000.png"}), 2);
    EXPECT_EQ(nextSnapshotNumber({"/v/a_2_x.png", "/v/a_1_x.png", "/v/a_1_y.png"}), 3);
    // The first "_<digits>_" of the whole path: a folder or video name
    // holding one numbers every file by it (a legacy defect, kept: both
    // files read 2, so the next is 1 and then 1 again — it overwrites).
    EXPECT_EQ(nextSnapshotNumber({"/d_2_x/a_1_t.png", "/d_2_x/a_3_t.png"}), 1);
    EXPECT_EQ(nextSnapshotNumber({"/v/show_01_a_1_t.png", "/v/show_01_a_2_t.png"}), 2);
}

// The snapshot is B, G, R of the frame (alpha dropped) as RGB; the subbed
// one first composites the premultiplied overlay source-over, the CPU
// reference the presenter is held to (tests/ui/presenter_tests.cpp).
TEST(VideoSnapshot, ImageIsTheFrameWithTheOverlayComposited)
{
    IndexedFrame f;
    f.width = 2;
    f.height = 1;
    f.stride = 12; // padded rows are skipped
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
    // Half-transparent white (premultiplied) on the first pixel, nothing on the second.
    o.pixels = {128, 128, 128, 128, 0, 0, 0, 0};
    const auto subbed = snapshotImage(f, &o);
    const auto mix = [](int over, int under) { return over + (under * (255 - 128) + 127) / 255; };
    EXPECT_EQ(subbed.rgb, (std::vector<std::uint8_t>{std::uint8_t(mix(128, 30)), std::uint8_t(mix(128, 20)),
                                                     std::uint8_t(mix(128, 10)), 50, 100, 200}));
    // An empty overlay or one of another size is not drawn.
    o.empty = true;
    EXPECT_EQ(snapshotImage(f, &o).rgb, plain.rgb);
}

// ColorMatrixDescription (ProviderFFMS2.cpp:929-948).
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

// The matrix legacy's video saw: a loaded ASS file without one, or with
// "None", had "TV.601" written in by SubsLoader::LoadASS (SubsLoader.cpp:167-168;
// C03-ycbcr-on-load keeps the file, the video keeps legacy's value); a "None"
// chosen later stays "None" (HikariSubFrame.cpp:1241-1245); other formats none.
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

// Script properties' matrix choice, as the dialog's OK writes it.
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

} // namespace

// The session's matrix: "None" as loaded is legacy's TV.601; "None" chosen in
// Script properties is "None" (the source's own matrix through
// LegacyColourMatrix::set), until Undo brings back the opened value, which
// legacy's Undo restored as the TV.601 its load wrote (SubsGridBase.cpp:1023-1025).
TEST(VideoMatrix, SessionMatrixKeepsAChosenNone)
{
    EditSession session{loadAss(kNoneAss)};
    EXPECT_EQ(sessionVideoMatrix(session), "TV.601");
    chooseMatrix(session, u8"TV.709");
    EXPECT_EQ(sessionVideoMatrix(session), "TV.709");
    chooseMatrix(session, u8"None");
    EXPECT_EQ(session.document().scriptInfo(u8"YCbCr Matrix"), std::optional<std::u8string>(u8"None"));
    EXPECT_EQ(sessionVideoMatrix(session), "None");
    // A later edit that leaves the matrix alone keeps the chosen "None".
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

    // A file loaded without the key is TV.601; a choice of TV.601 then "None"
    // is the source's own.
    EditSession bare{loadAss("[Script Info]\nScriptType: v4.00+\n\n[Events]\n")};
    EXPECT_EQ(sessionVideoMatrix(bare), "TV.601");
    chooseMatrix(bare, u8"TV.601");
    chooseMatrix(bare, u8"None");
    EXPECT_EQ(sessionVideoMatrix(bare), "None");

    // The chosen "None" reaches the converter as legacy SetColorSpace("None"):
    // the source's own matrix (ProviderFFMS2.cpp:950-962).
    using namespace ffms_colour;
    LegacyColourMatrix m;
    m.open(kBt709, kRangeMpeg, 1920, 1080, "TV.601");
    const auto change = m.set("None");
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->input, (LegacyColourMatrix::Input{kBt709, kRangeMpeg}));
}

// ProviderFFMS2::Init (ProviderFFMS2.cpp:393-414).
TEST(VideoMatrix, OpenAppliesTheDocumentMatrix)
{
    using namespace ffms_colour;
    using Input = LegacyColourMatrix::Input;
    LegacyColourMatrix m;
    // A BT.709 source with "TV.709": set to BT.709 (a no-op for a tagged one).
    EXPECT_EQ(m.open(kBt709, kRangeMpeg, 1920, 1080, "TV.709"), (Input{kBt709, kRangeMpeg}));
    EXPECT_EQ(m.applied(), "TV.709");
    // Any source with "TV.601": converted as BT.601, in its own range.
    EXPECT_EQ(m.open(kBt709, kRangeJpeg, 1920, 1080, "TV.601"), (Input{kBt470bg, kRangeJpeg}));
    EXPECT_EQ(m.applied(), "PC.601");
    EXPECT_EQ(m.source(), "PC.709");
    // A BT.601 source with "TV.709": left as tagged, though named TV.709.
    EXPECT_EQ(m.open(kSmpte170m, kRangeMpeg, 720, 480, "TV.709"), std::nullopt);
    EXPECT_EQ(m.applied(), "TV.709");
    EXPECT_EQ(m.source(), "TV.601");
    // Other names leave the source's tags.
    EXPECT_EQ(m.open(kBt709, kRangeMpeg, 1920, 1080, "PC.709"), std::nullopt);
    EXPECT_EQ(m.applied(), "TV.709");
    // Untagged: BT.709 for a frame wider than 1024 or at least 600 high
    // (set only with "TV.709"; otherwise the converter's default), else BT.601.
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1280, 720, "TV.709"), (Input{kBt709, kRangeUnspecified}));
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1024, 600, "TV.709"), (Input{kBt709, kRangeUnspecified}));
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1024, 599, "TV.709"), std::nullopt);
    EXPECT_EQ(m.source(), "TV.601");
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1280, 720, ""), std::nullopt);
    EXPECT_EQ(m.applied(), "TV.709");
}

// ProviderFFMS2::SetColorSpace (ProviderFFMS2.cpp:950-982): legacy's answer
// depends on what came before.
TEST(VideoMatrix, ChangesFollowLegacyState)
{
    using namespace ffms_colour;
    using Input = LegacyColourMatrix::Input;
    LegacyColourMatrix m;
    // An untagged HD source opened with no matrix: the converter's default
    // (BT.601), though named TV.709 (the guess).
    EXPECT_EQ(m.open(kUnspecified, kRangeUnspecified, 1920, 1080, ""), std::nullopt);
    // "TV.709" names what is applied: nothing; the video stays BT.601.
    EXPECT_EQ(m.set("TV.709"), std::nullopt);
    // "TV.601": BT.601 set.
    auto change = m.set("TV.601");
    ASSERT_TRUE(change);
    EXPECT_EQ(change->input, (Input{kBt470bg, kRangeUnspecified}));
    EXPECT_EQ(change->previous, "TV.709");
    EXPECT_EQ(m.applied(), "TV.601");
    // Back to "TV.709", the source's own: the guess is set, BT.709 now.
    change = m.set("TV.709");
    ASSERT_TRUE(change);
    EXPECT_EQ(change->input, (Input{kBt709, kRangeUnspecified}));
    // Any other name: the source's own again.
    change = m.set("None");
    ASSERT_TRUE(change);
    EXPECT_EQ(change->input, (Input{kBt709, kRangeUnspecified}));
    EXPECT_EQ(m.applied(), "None");
    // A refused change keeps the old name.
    change = m.set("TV.601");
    ASSERT_TRUE(change);
    m.revert(*change);
    EXPECT_EQ(m.applied(), "None");

    // A BT.601 source: "TV.709" is never set.
    LegacyColourMatrix sd;
    sd.open(kBt470bg, kRangeMpeg, 720, 576, "TV.601");
    EXPECT_EQ(sd.set("TV.709"), std::nullopt);
    EXPECT_EQ(sd.applied(), "TV.601");
}
