// A1: the audio box's display, pinned against legacy AudioDisplay.cpp,
// Provider.cpp, ProviderFFMS2.cpp, ProviderDummy.cpp and WaveformPeaks.cpp at
// 20d647c4. Expected values are worked out from legacy's arithmetic.

#include "hikari/application/audio_box.h"
#include "hikari/application/audio_display.h"

#include <gtest/gtest.h>

#include <deque>
#include <filesystem>
#include <fstream>

using namespace hikari::application;

namespace {

std::vector<std::int16_t> ramp(int count)
{
    std::vector<std::int16_t> samples(static_cast<std::size_t>(count));
    for (int i = 0; i < count; i++)
        samples[static_cast<std::size_t>(i)] = static_cast<std::int16_t>((i % 2) ? i : -i);
    return samples;
}

// The scene's shapes of one kind and colour.
std::vector<AudioShape> shapes(const std::vector<AudioShape> &all, AudioShape::Kind kind, std::uint32_t colour)
{
    std::vector<AudioShape> out;
    for (const auto &s : all)
        if (s.kind == kind && s.colour == colour)
            out.push_back(s);
    return out;
}

std::vector<std::string> texts(const std::vector<AudioShape> &all)
{
    std::vector<std::string> out;
    for (const auto &s : all)
        if (s.kind == AudioShape::Kind::Text)
            out.push_back(s.text);
    return out;
}

// Every text is 30 pixels wide (legacy GetTextExtent in tahoma8).
int width30(AudioShape::Font, std::string_view) { return 30; }

// 60 s at 48 kHz in a 1000 x 150 display with a 22-row ruler: zoom 50 gives
// 1440 samples per column (33.3 columns per second).
AudioView minuteView()
{
    AudioView view;
    view.setSamplesPercent(50, false);
    view.setSource(48000, 48000 * 60);
    view.resize(1000, 150, 22, 16);
    return view;
}

} // namespace

// Legacy tests/WaveformPeaksTests.cpp, unchanged.
TEST(WaveformPeaks, CoverWholeBlocks)
{
    const auto samples = ramp(1000);
    WaveformPeaks peaks(100);
    peaks.append(samples.data(), 1000);
    std::int16_t lo, hi;
    ASSERT_TRUE(peaks.range(0, 100, &lo, &hi));
    EXPECT_EQ(lo, -98);
    EXPECT_EQ(hi, 99);
    ASSERT_TRUE(peaks.range(200, 300, &lo, &hi));
    EXPECT_EQ(lo, -498);
    EXPECT_EQ(hi, 499);
    // a range takes every block it touches
    ASSERT_TRUE(peaks.range(150, 100, &lo, &hi));
    EXPECT_EQ(lo, -298);
    EXPECT_EQ(hi, 299);
}

TEST(WaveformPeaks, PiecesPartialBlocksAndTheEnd)
{
    const auto samples = ramp(1000);
    WaveformPeaks whole(100), pieces(100);
    whole.append(samples.data(), 1000);
    pieces.append(samples.data(), 37);
    pieces.append(samples.data() + 37, 500);
    pieces.append(samples.data() + 537, 463);
    for (int start = 0; start < 1000; start += 100) {
        std::int16_t a, b, c, d;
        ASSERT_TRUE(whole.range(start, 100, &a, &b));
        ASSERT_TRUE(pieces.range(start, 100, &c, &d));
        EXPECT_EQ(a, c);
        EXPECT_EQ(b, d);
    }
    const auto short250 = ramp(250);
    WaveformPeaks last(100);
    last.append(short250.data(), 250);
    std::int16_t lo, hi;
    ASSERT_TRUE(last.range(200, 50, &lo, &hi));
    EXPECT_EQ(hi, 249);
    EXPECT_EQ(last.samples(), 250);
    EXPECT_FALSE(last.range(300, 100, &lo, &hi));
    EXPECT_TRUE(last.range(200, 1000, &lo, &hi));
}

// ProviderFFMS2::GetBuffer: the channels' sum over their count, truncated.
TEST(DisplayAudio, MixesDownLikeLegacy)
{
    DisplayAudio audio(48000, 4);
    const std::int16_t stereo[] = {-3, 0, 32767, 32767, -32768, -32767, 5, -2};
    audio.appendFrames(stereo, 4, 2);
    std::int16_t out[6];
    audio.read(0, 6, out);
    EXPECT_EQ(out[0], -1); // -3 / 2
    EXPECT_EQ(out[1], 32767);
    EXPECT_EQ(out[2], -32767); // -65535 / 2
    EXPECT_EQ(out[3], 1);
    EXPECT_EQ(out[4], 0); // past the end
    EXPECT_EQ(out[5], 0);
    DisplayAudio mono(48000, 2);
    const std::int16_t one[] = {-7, 9};
    mono.appendFrames(one, 2, 1);
    mono.read(0, 2, out);
    EXPECT_EQ(out[0], -7);
    EXPECT_EQ(out[1], 9);
    // the peak table is used only once every sample is in (legacy BuildPeaks)
    EXPECT_EQ(mono.peaks(), nullptr);
    mono.finish();
    ASSERT_NE(mono.peaks(), nullptr);
    EXPECT_EQ(mono.peaks()->blockSamples(), 256);
}

// GLOBAL_OPEN_DUMMY_AUDIO: ProviderDummy's silence, which never builds peaks.
TEST(DisplayAudio, BlankAudioIsSilence)
{
    const auto audio = DisplayAudio::silence(44100, 396'900'000);
    EXPECT_EQ(audio.sampleCount(), 396'900'000);
    EXPECT_EQ(audio.decoded(), 396'900'000);
    EXPECT_TRUE(audio.finished());
    EXPECT_EQ(audio.peaks(), nullptr);
    std::int16_t out[3] = {1, 1, 1};
    audio.read(396'899'999, 3, out);
    EXPECT_EQ(out[0], 0);
    // every column is the middle row, drawn from sample data
    const auto columns = legacyWaveform(audio, 0, 4, 100, 1323, 1.f);
    EXPECT_EQ(columns.min, (std::vector<int>{50, 50, 50, 50}));
    EXPECT_EQ(columns.peak, (std::vector<int>{50, 50, 50, 50}));
}

// Provider::GetWaveForm reading samples: y = h/2 - sample * (h/2 * scale) / 0x8000.
TEST(Waveform, ColumnsFromSamplesAtCloseZoom)
{
    DisplayAudio audio(48000, 33);
    std::vector<std::int16_t> mono(33, 0);
    mono[3] = 32767;   // column 0's top: 50 - 49
    mono[7] = -32768;  // column 0's bottom: 50 + 50
    mono[22] = 16384;  // column 2: 50 - 25
    mono[25] = -1;     // rounds to the middle
    audio.appendFrames(mono.data(), 33, 1);
    audio.finish();
    // zoom 10%: 11 samples per column, below four peak blocks, so samples are read
    const auto columns = legacyWaveform(audio, 0, 4, 100, 11, 1.f);
    EXPECT_EQ(columns.min, (std::vector<int>{1, 50, 25, 50}));
    EXPECT_EQ(columns.peak, (std::vector<int>{100, 50, 50, 50})); // column 3 is past the end: silence
    // vertical zoom 75 (scale 3.375): half amplitude 168, clipped to the area
    const auto loud = legacyWaveform(audio, 0, 3, 100, 11, audioScaleFromSlider(75));
    EXPECT_EQ(loud.min, (std::vector<int>{0, 50, 0}));
    EXPECT_EQ(loud.peak, (std::vector<int>{100, 50, 50}));
}

// Zoomed out (1440 samples, zoom 50 at 48 kHz) a column reads whole 256-sample
// blocks: column 0 (samples 0..1439) also covers block 5, up to sample 1535.
TEST(Waveform, ZoomedOutColumnsCoverWholePeakBlocks)
{
    DisplayAudio audio(48000, 2880);
    std::vector<std::int16_t> mono(2880, 0);
    mono[1500] = 32767; // in column 1, and in column 0's last block
    audio.appendFrames(mono.data(), 2880, 1);
    const auto beforePeaks = legacyWaveform(audio, 0, 2, 128, 1440, 1.f);
    EXPECT_EQ(beforePeaks.min, (std::vector<int>{64, 1}));
    audio.finish();
    const auto withPeaks = legacyWaveform(audio, 0, 2, 128, 1440, 1.f);
    EXPECT_EQ(withPeaks.min, (std::vector<int>{1, 1}));
    EXPECT_EQ(withPeaks.peak, (std::vector<int>{64, 64}));
    // 1023 samples per column stay below four blocks: samples again, so
    // column 0 (0..1022) misses the spike and column 1 (1023..2045) has it
    const auto close = legacyWaveform(audio, 0, 2, 128, 1023, 1.f);
    EXPECT_EQ(close.min, (std::vector<int>{64, 1}));
}

// UpdateSamples: 2 minutes in 500 columns at 100%, cubic below.
TEST(AudioView, SamplesPerColumnAtKnownZooms)
{
    const auto at = [](int rate, int percent) {
        AudioView view;
        view.setSamplesPercent(percent, false);
        view.setSource(rate, 1'000'000'000);
        view.resize(800, 150, 22, 16);
        return view.samples();
    };
    EXPECT_EQ(at(48000, 100), 11520);
    EXPECT_EQ(at(48000, 50), 1440);
    EXPECT_EQ(at(44100, 50), 1323);
    EXPECT_EQ(at(48000, 10), 11);
    EXPECT_EQ(at(48000, 1), 1); // 0.0115 samples
    EXPECT_FLOAT_EQ(audioScaleFromSlider(50), 1.f);
}

TEST(AudioView, TimeAndColumnConversions)
{
    const AudioView view = minuteView();
    EXPECT_EQ(view.width(), 1000);
    EXPECT_EQ(view.height(), 128);
    EXPECT_EQ(view.samples(), 1440);
    EXPECT_FLOAT_EQ(view.xAtMs(1000), 1000 * 48.f / 1440.f);
    EXPECT_EQ(view.msAtX(100), 3000);
    EXPECT_EQ(view.sampleAtMs(1001), 48048);
    EXPECT_EQ(view.sampleAtX(10), 14400);
}

// MakeDialogueVisible: a Line near the right edge moves to 50 columns from the left.
TEST(AudioView, FollowsTheActiveLine)
{
    AudioView view = minuteView();
    view.makeVisible(30000, 32000, false, false, 16); // columns 1000..1066
    EXPECT_EQ(view.position(), 950);
    EXPECT_EQ(view.positionSample(), 950 * 1440);
    EXPECT_FLOAT_EQ(view.xAtMs(30000), 50.f);
    // already in view: nothing moves
    view.makeVisible(31000, 32000, false, false, 16);
    EXPECT_EQ(view.position(), 950);
    // forced: the Line centred ((startPos + endPos - w * samples) / 2)
    view.makeVisible(31000, 32000, true, false, 16);
    EXPECT_EQ(view.position(), (1488000 + 1536000 - 1000 * 1440) / 2 / 1440);
    // to the end: the end 50 columns from the right edge
    view.makeVisible(50000, 52000, false, true, 16);
    EXPECT_EQ(view.position(), (2496000 - 950 * 1440) / 1440);
    // the scrollbar: legacy units of 12 columns, plus the bar's thickness
    const auto bar = view.updateScrollbar(16);
    EXPECT_EQ(bar.page, 83);
    EXPECT_EQ(bar.range, 2000 / 12 + 16);
    EXPECT_EQ(bar.position, static_cast<int>(view.position() / 12));
}

// Audio shorter than 500 columns of the zoom starts at the beginning, and its
// scrollbar page is the whole range.
TEST(AudioView, ShortAudioStaysAtTheStart)
{
    AudioView view;
    view.setSamplesPercent(50, false);
    view.setSource(48000, 240000);
    view.resize(1000, 150, 22, 16);
    view.makeVisible(4000, 4500, false, false, 16);
    EXPECT_EQ(view.positionSample(), 0);
    const auto bar = view.updateScrollbar(16);
    EXPECT_EQ(bar.page, 240000 / 1440 / 12 + 16);
    EXPECT_EQ(bar.range, bar.page);
}

TEST(AudioScene, LegacyDashes)
{
    const auto dashes = legacyDashes(0, 128, 3);
    ASSERT_EQ(dashes.size(), 22u);
    EXPECT_EQ(dashes[0], (std::pair<float, float>{0, 3}));
    EXPECT_EQ(dashes[20], (std::pair<float, float>{120, 123}));
    EXPECT_EQ(dashes[21], (std::pair<float, float>{126, 128})); // the last dash reaches the end
}

TEST(AudioScene, LineMarksAtLegacyPositions)
{
    const AudioView view = minuteView();
    AudioMarks marks;
    marks.startMs = 1000; // column 33.3
    marks.endMs = 2000;   // column 66.7
    WaveformColumns columns;
    columns.min.assign(1000, 60);
    columns.peak.assign(1000, 70);
    const AudioDisplayOptions o;
    const auto scene = audioScene(view, columns, marks, o, width30);
    ASSERT_FALSE(scene.empty());
    EXPECT_EQ(scene[0].kind, AudioShape::Kind::Fill);
    EXPECT_EQ(scene[0].colour, o.background);
    EXPECT_EQ(scene[0].y2, 150);
    // the selection background, one column past the end
    const auto selection = shapes(scene, AudioShape::Kind::Fill, o.selectionBackground);
    ASSERT_EQ(selection.size(), 1u);
    EXPECT_EQ(selection[0].x1, 33);
    EXPECT_EQ(selection[0].x2, 67);
    EXPECT_EQ(selection[0].y2, 128);
    // waveform columns: selected for 33 <= x < 66, from below the lowest to above the highest row
    const auto selected = shapes(scene, AudioShape::Kind::Line, o.waveformSelected);
    ASSERT_EQ(selected.size(), 33u);
    EXPECT_EQ(selected.front().x1, 33.5f);
    EXPECT_EQ(selected.back().x1, 65.5f);
    EXPECT_EQ(selected.front().y1, 70.5f);
    EXPECT_EQ(selected.front().y2, 59.5f);
    EXPECT_EQ(shapes(scene, AudioShape::Kind::Line, o.waveform).size(), 1000u - 33u);
    // the boundaries: lineStart + width / 2, `width` wide, flags 10 pixels inwards
    const auto starts = shapes(scene, AudioShape::Kind::Line, o.lineStart);
    ASSERT_GE(starts.size(), 2u);
    EXPECT_EQ(starts[0].x1, 34);
    EXPECT_EQ(starts[0].y2, 128);
    EXPECT_EQ(starts[0].width, 2);
    const auto flags = shapes(scene, AudioShape::Kind::Triangle, o.lineStart);
    ASSERT_EQ(flags.size(), 4u);
    EXPECT_EQ(flags[0].x2, 44);
    EXPECT_EQ(flags[1].y1, 118);
    // the end boundary at 66 + 1, its flags pointing left
    EXPECT_EQ(flags[2].x1, 67);
    EXPECT_EQ(flags[2].x2, 57);
    // without a modified or negative Line there is no warning text
    for (const auto &t : texts(scene))
        EXPECT_TRUE(t != "Modified" && t != "Negative time");
}

TEST(AudioScene, NegativeAndModifiedLines)
{
    const AudioView view = minuteView();
    WaveformColumns columns;
    columns.min.assign(1000, 60);
    columns.peak.assign(1000, 70);
    const AudioDisplayOptions o;
    AudioMarks negative;
    negative.startMs = 2000;
    negative.endMs = 1000;
    auto scene = audioScene(view, columns, negative, o, width30);
    EXPECT_TRUE(shapes(scene, AudioShape::Kind::Fill, o.selectionBackground).empty());
    // nothing is selected, but the waveform takes the selected colour's list
    EXPECT_TRUE(shapes(scene, AudioShape::Kind::Line, o.waveformSelected).empty());
    auto names = texts(scene);
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names.front(), "Negative time");
    AudioMarks modified;
    modified.startMs = 1000;
    modified.endMs = 2000;
    modified.modified = true;
    scene = audioScene(view, columns, modified, o, width30);
    EXPECT_EQ(texts(scene).front(), "Modified");
    EXPECT_EQ(shapes(scene, AudioShape::Kind::Line, o.waveformModified).size(), 33u);
    for (const auto &s : scene)
        if (s.kind == AudioShape::Kind::Text && s.text == "Modified") {
            EXPECT_EQ(s.x1, 4);
            EXPECT_EQ(s.y1, 4);
            EXPECT_EQ(s.colour, 0xFFFF0000u);
            EXPECT_TRUE(s.outlined);
            EXPECT_EQ(s.font, AudioShape::Font::Label);
        }
}

// DrawInactiveLines: the previous Line (0..900 ms, columns 0..30) and the next
// (2500..3000 ms, 83..100) around the active one (columns 33..66). A range
// wholly on one side of the selection leaves a one-column shade at its edge.
TEST(AudioScene, InactiveLinesAndTheirOneColumnShades)
{
    const AudioView view = minuteView();
    AudioMarks marks;
    marks.startMs = 1000;
    marks.endMs = 2000;
    marks.inactive = {{0, 900}, {2500, 3000}};
    WaveformColumns columns;
    columns.min.assign(1000, 60);
    columns.peak.assign(1000, 70);
    const AudioDisplayOptions o;
    const auto scene = audioScene(view, columns, marks, o, width30);
    const auto shades = shapes(scene, AudioShape::Kind::Fill, o.inactiveBackground);
    ASSERT_EQ(shades.size(), 4u);
    EXPECT_EQ(std::pair(shades[0].x1, shades[0].x2), (std::pair<float, float>{0, 31}));
    EXPECT_EQ(std::pair(shades[1].x1, shades[1].x2), (std::pair<float, float>{66, 67}));
    EXPECT_EQ(std::pair(shades[2].x1, shades[2].x2), (std::pair<float, float>{33, 34}));
    EXPECT_EQ(std::pair(shades[3].x1, shades[3].x2), (std::pair<float, float>{83, 101}));
    const auto inactive = shapes(scene, AudioShape::Kind::Line, o.waveformInactive);
    ASSERT_EQ(inactive.size(), 30u + 17u);
    EXPECT_EQ(inactive[0].x1, 0);
    EXPECT_EQ(inactive[0].y1, 70); // peak to min - 1
    EXPECT_EQ(inactive[0].y2, 59);
    EXPECT_EQ(inactive[30].x1, 83);
    const auto boundaries = shapes(scene, AudioShape::Kind::Line, o.inactiveBoundary);
    ASSERT_EQ(boundaries.size(), 4u);
    EXPECT_EQ(boundaries[0].x1, 1);
    EXPECT_EQ(boundaries[1].x1, 31);
    EXPECT_EQ(boundaries[2].x1, 84);
    EXPECT_EQ(boundaries[3].x1, 101);
    // mode 0 shades nothing
    AudioDisplayOptions none;
    none.inactiveLines = 0;
    EXPECT_TRUE(shapes(audioScene(view, columns, marks, none, width30), AudioShape::Kind::Fill, o.inactiveBackground).empty());
}

// DrawKeyframes draws a keyframe in view at ((ms - 20) / 10) * 10 and stops
// after the first one past the view; DrawTimescale and the video mark.
TEST(AudioScene, KeyframeVideoAndRulerMarks)
{
    const AudioView view = minuteView(); // 0..30000 ms in view
    AudioMarks marks;
    marks.startMs = 1000;
    marks.endMs = 2000;
    marks.keyframesMs = {0, 1001, 2002, 70000, 1500};
    marks.videoMs = 1001;
    WaveformColumns columns;
    columns.min.assign(1000, 60);
    columns.peak.assign(1000, 70);
    const AudioDisplayOptions o;
    const auto scene = audioScene(view, columns, marks, o, width30);
    const auto keyframes = shapes(scene, AudioShape::Kind::Line, o.keyframe);
    ASSERT_EQ(keyframes.size(), 3u);
    EXPECT_EQ(keyframes[0].x1, 0);  // -20 ms: column -0.67, truncated to 0
    EXPECT_EQ(keyframes[1].x1, 32); // 980 ms
    EXPECT_EQ(keyframes[2].x1, 66); // 1980 ms; 70000 ends the walk before 1500
    // the paused video's time, dashed and two pixels wide
    const auto video = shapes(scene, AudioShape::Kind::Line, o.cursor);
    ASSERT_EQ(video.size(), 22u);
    EXPECT_FLOAT_EQ(video[0].x1, 1001 * 48.f / 1440.f);
    EXPECT_EQ(video[0].width, 2);
    EXPECT_EQ(video[21].y2, 128);
    // seconds: dashed boundaries where (x * samples + start) % rate < samples
    const auto seconds = shapes(scene, AudioShape::Kind::Line, o.secondBoundaries);
    EXPECT_EQ(seconds.size(), 30u * 22u); // seconds 0..29, 22 dashes each
    EXPECT_EQ(seconds[22].x1, 34);
    EXPECT_EQ(seconds[44].x1, 67);
    // the ruler: second ticks h+2..h+8, half seconds h+2..h+5 (pixBounds 2), labels each second
    const auto ruler = shapes(scene, AudioShape::Kind::Line, o.timescaleText);
    ASSERT_GE(ruler.size(), 3u);
    EXPECT_EQ(ruler[0].y1, 128); // the ruler's top line
    EXPECT_EQ(ruler[1].x1, 0);
    EXPECT_EQ(ruler[1].y2, 136);
    EXPECT_EQ(ruler[2].x1, 17);
    EXPECT_EQ(ruler[2].y2, 133);
    std::vector<std::string> labels;
    for (const auto &s : scene)
        if (s.kind == AudioShape::Kind::Text && s.colour == o.timescaleText)
            labels.push_back(s.text);
    ASSERT_EQ(labels.size(), 30u);
    EXPECT_EQ(labels[0], "0");
    EXPECT_EQ(labels[1], "1");
    for (const auto &s : scene)
        if (s.kind == AudioShape::Kind::Text && s.text == "1") {
            EXPECT_EQ(s.x1, 34 - 50);
            EXPECT_EQ(s.y1, 136);
            EXPECT_EQ(s.align, AudioShape::Align::TopCenter);
        }
}

// The ruler's label spacing: the distance between the second and third
// second boundaries over the widest label's width.
TEST(AudioScene, RulerLabelsSkipWhenCrowdedAndFillWhenSparse)
{
    AudioView far;
    far.setSamplesPercent(100, false); // 11520 samples: 4.17 columns a second
    far.setSource(48000, 48000 * 600);
    far.resize(1000, 150, 22, 16);
    WaveformColumns columns;
    columns.min.assign(1000, 60);
    columns.peak.assign(1000, 70);
    const AudioDisplayOptions o;
    AudioMarks marks;
    auto labels = [&](const AudioView &view) {
        std::vector<std::string> out;
        for (const auto &s : audioScene(view, columns, marks, o, width30))
            if (s.kind == AudioShape::Kind::Text && s.colour == o.timescaleText)
                out.push_back(s.text);
        return out;
    };
    // 4 columns between seconds, 30 per label: every int(40 / 4) = 10th second
    const auto sparse = labels(far);
    ASSERT_FALSE(sparse.empty());
    EXPECT_EQ(sparse[0], "0");
    EXPECT_EQ(sparse[1], "10");
    EXPECT_EQ(sparse[6], "1:00");
    // close in (zoom 30: 311 samples, 154 columns a second): tenth ticks,
    // every second one labelled (154 / 30 is over 4.5)
    AudioView close;
    close.setSamplesPercent(30, false);
    close.setSource(48000, 48000 * 600);
    close.resize(1000, 150, 22, 16);
    const auto dense = labels(close);
    ASSERT_GE(dense.size(), 3u);
    EXPECT_EQ(dense[0], "0");
    EXPECT_EQ(dense[1], "0.2");
    EXPECT_EQ(dense[2], "0.4");
}

TEST(AudioScene, FocusBorderProgressAndCursor)
{
    const AudioView view = minuteView();
    WaveformColumns columns;
    columns.min.assign(1000, 60);
    columns.peak.assign(1000, 70);
    const AudioDisplayOptions o;
    AudioMarks marks;
    marks.focused = true;
    const auto scene = audioScene(view, columns, marks, o, width30);
    const auto &last = scene.back();
    EXPECT_EQ(last.kind, AudioShape::Kind::Line);
    EXPECT_EQ(last.colour, o.waveform);
    EXPECT_EQ(last.y1, 127);
    const auto &right = scene[scene.size() - 3];
    EXPECT_EQ(right.x1, 999);
    // DrawProgress: frames at halfY = (h + 20) / 2, a 37-pixel bar and the percentage
    const auto progress = audioProgressScene(view, 0.5f, o);
    ASSERT_EQ(progress.size(), 1u + 8u + 2u);
    EXPECT_EQ(progress[1].colour, 0xFF00FFFFu);
    EXPECT_EQ(progress[1].y1, 54);
    EXPECT_EQ(progress[9].width, 37);
    EXPECT_EQ(progress[9].x2, 500);
    EXPECT_EQ(progress[10].text, "50%");
    // DrawCursor: two pixels, antialiased, with its time unless playing
    const auto cursor = audioCursor(view, 100, false, o);
    ASSERT_EQ(cursor.size(), 2u);
    EXPECT_TRUE(cursor[0].smooth);
    EXPECT_EQ(cursor[0].width, 2);
    EXPECT_EQ(cursor[1].text, "0:00:03.00");
    EXPECT_EQ(cursor[1].x1, -50);
    EXPECT_EQ(cursor[1].x2, 250);
    EXPECT_EQ(cursor[1].y1, 5);
    EXPECT_EQ(audioCursor(view, 100, true, o).size(), 1u);
}

TEST(AudioScene, LegacyAssTime)
{
    EXPECT_EQ(legacyAssTime(3723456), "1:02:03.45");
    EXPECT_EQ(legacyAssTime(-5), "0:00:00.00");
}

// SubsGrid::GetKeyFromPosition, DrawInactiveLines' range and SetDialogue's times.
TEST(AudioLines, InactiveLinesAndSelections)
{
    const std::vector<AudioLineSpan> lines = {
        {0, 1000, true}, {1500, 1800, false}, {2000, 3000, true}, {0, 0, true}, {5000, 6000, true}};
    EXPECT_EQ(legacyKeyFromPosition(lines, 2, -1), 0); // the hidden Line is skipped
    EXPECT_EQ(legacyKeyFromPosition(lines, 2, 1), 3);
    EXPECT_EQ(legacyKeyFromPosition(lines, 4, 1), 4);  // none after: the last shown Line
    EXPECT_EQ(legacyKeyFromPosition(lines, 0, -1), 0);
    EXPECT_EQ(legacyInactiveLines(1, lines, 2), (std::vector<std::pair<int, int>>{{0, 1000}, {0, 0}}));
    EXPECT_EQ(legacyInactiveLines(2, lines, 2), (std::vector<std::pair<int, int>>{{0, 1000}, {0, 0}, {5000, 6000}}));
    EXPECT_TRUE(legacyInactiveLines(0, lines, 2).empty());
    EXPECT_EQ(legacyLineSelection(lines, 2, 0), (std::pair{2000, 3000}));
    // a 0:00:00.00 Line right after the previous active one starts at its end
    EXPECT_EQ(legacyLineSelection(lines, 3, 2), (std::pair{3000, 8000}));
    EXPECT_EQ(legacyLineSelection(lines, 3, 0), (std::pair{0, 5000}));
}

namespace {

// The audio box's source: answers are delivered by the test.
struct FakeAudioSource : DisplayAudioPort {
    std::string probedPath, openedPath;
    int openedTrack = -1, sourceTrack = -1;
    Probed pendingProbe;
    Progress progress;
    Opened pendingOpen;
    std::deque<std::tuple<std::int64_t, std::int64_t, Read>> reads;
    int cancels = 0;
    void probe(const std::string &path, Probed done) override
    {
        probedPath = path;
        pendingProbe = std::move(done);
    }
    void openDisplayAudio(const std::string &path, int track, Progress p, Opened done) override
    {
        openedPath = path;
        openedTrack = track;
        progress = std::move(p);
        pendingOpen = std::move(done);
    }
    void openSourceDisplayAudio(int track, Opened done) override
    {
        sourceTrack = track;
        pendingOpen = std::move(done);
    }
    void displayAudio(std::int64_t start, std::int64_t count, Read done) override
    {
        reads.emplace_back(start, count, std::move(done));
    }
    void cancelDisplay() override
    {
        ++cancels;
        auto pending = std::move(reads);
        reads.clear();
        for (auto &[start, count, done] : pending)
            done(std::unexpected(AudioFailure{SourceError::Cancelled, AudioStage::Host, {}}));
    }

    void probed(std::vector<AudioTrack> tracks, bool hasVideo = false)
    {
        MediaProbe p;
        p.hasVideo = hasVideo;
        p.audio = std::move(tracks);
        std::exchange(pendingProbe, nullptr)(std::move(p));
    }
    void probedOneTrack() { probed({AudioTrack{1, false, false, {}, {}, "aac"}}); }
    void opened(int channels, std::int64_t count)
    {
        AudioInfo info;
        info.format = SampleFormat::S16;
        info.bitsPerSample = 16;
        info.sampleRate = 48000;
        info.channels = channels;
        info.sampleCount = count;
        std::exchange(pendingOpen, nullptr)(info);
    }
    void failOpen(SourceError error, AudioStage stage, std::string text)
    {
        std::exchange(pendingOpen, nullptr)(std::unexpected(AudioFailure{error, stage, std::move(text)}));
    }
    // Answers the oldest read with frames whose every channel is `value`
    // (or, with `ramp`, frame i of the source is start + i).
    void answer(std::int16_t value, int channels, bool ramp = false)
    {
        auto [start, count, done] = std::move(reads.front());
        reads.pop_front();
        AudioBlock b;
        b.start = start;
        b.count = count;
        b.channels = channels;
        b.samples.resize(static_cast<std::size_t>(count * channels) * 2);
        auto *p = reinterpret_cast<std::int16_t *>(b.samples.data());
        for (std::int64_t i = 0; i < count; ++i)
            for (int c = 0; c < channels; ++c)
                p[i * channels + c] = ramp ? static_cast<std::int16_t>((start + i) % 30000) : value;
        done(std::move(b));
    }
    void failRead(AudioFailure failure)
    {
        auto [start, count, done] = std::move(reads.front());
        reads.pop_front();
        done(std::unexpected(std::move(failure)));
    }
};

// A box with a disk cache in a folder of its own, logging into `logged`.
struct BoxFixture {
    FakeAudioSource source;
    AudioBox box{source};
    AudioCacheSettings settings;
    std::vector<std::pair<std::string, AudioBox::LogLevel>> logged;
    std::vector<std::filesystem::path> cached;
    explicit BoxFixture(const char *name)
    {
        settings.cacheDir = std::filesystem::temp_directory_path() / ("hikari-a1-" + std::string(name));
        std::filesystem::remove_all(settings.cacheDir);
        box.setSettings([this] { return settings; });
        box.setLog([this](const std::string &m, AudioBox::LogLevel l) { logged.emplace_back(m, l); });
        box.setCached([this](const std::filesystem::path &f) { cached.push_back(f); });
    }
    ~BoxFixture() { std::filesystem::remove_all(settings.cacheDir); }
};

std::int16_t at(const DisplayAudio &audio, std::int64_t i)
{
    std::int16_t s = -1;
    audio.read(i, 1, &s);
    return s;
}

} // namespace

// SetFile through the source: the file's tracks, indexing, then legacy
// DiskCache blocks of 332768 frames with their progress, the cache file
// written as .part and named once complete, then the peak table.
TEST(AudioBox, OpensIndexesAndCachesOnDiskInBlocks)
{
    BoxFixture f("disk");
    int changes = 0;
    f.box.setObserver([&] { ++changes; });
    f.box.open("/media/episode.mkv");
    EXPECT_EQ(f.source.probedPath, "/media/episode.mkv");
    EXPECT_EQ(f.box.state(), AudioBox::State::Opening);
    f.source.probedOneTrack();
    EXPECT_EQ(f.source.openedPath, "/media/episode.mkv");
    EXPECT_EQ(f.source.openedTrack, 1);
    f.source.progress(40, 100);
    EXPECT_EQ(f.box.indexing(), (std::pair<std::int64_t, std::int64_t>{40, 100}));
    f.source.opened(2, 400000);
    EXPECT_EQ(f.box.state(), AudioBox::State::Loading);
    const auto file = f.settings.cacheDir / "episode_track1_2ch_0.w64";
    EXPECT_EQ(f.box.cacheFile(), file);
    auto part = file;
    part += ".part";
    EXPECT_TRUE(std::filesystem::exists(part));
    ASSERT_EQ(f.source.reads.size(), 1u);
    EXPECT_EQ(std::get<0>(f.source.reads.front()), 0);
    EXPECT_EQ(std::get<1>(f.source.reads.front()), 332768);
    f.source.answer(-3, 2);
    EXPECT_FLOAT_EQ(f.box.progress(), 332768.f / 400000.f);
    ASSERT_EQ(f.source.reads.size(), 1u);
    EXPECT_EQ(std::get<0>(f.source.reads.front()), 332768);
    EXPECT_EQ(std::get<1>(f.source.reads.front()), 400000 - 332768);
    EXPECT_EQ(f.box.audio()->peaks(), nullptr);
    f.source.answer(100, 2);
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    EXPECT_FLOAT_EQ(f.box.progress(), 1.f);
    ASSERT_NE(f.box.audio()->peaks(), nullptr);
    EXPECT_EQ(at(*f.box.audio(), 332767), -3); // read back from the file
    EXPECT_EQ(at(*f.box.audio(), 332768), 100);
    EXPECT_EQ(at(*f.box.audio(), 400000), 0); // past the end
    EXPECT_EQ(f.cached, (std::vector<std::filesystem::path>{file})); // legacy then trims old caches
    EXPECT_GT(changes, 4);
    // closing renames the complete cache (legacy ~ProviderFFMS2)
    f.box.close();
    EXPECT_FALSE(std::filesystem::exists(part));
    EXPECT_EQ(std::filesystem::file_size(file), 400000u * 4);
    EXPECT_TRUE(f.logged.empty());
}

// An incomplete cache is removed; opening again writes it anew (a new index).
TEST(AudioBox, AnIncompleteDiskCacheIsRemoved)
{
    BoxFixture f("incomplete");
    f.box.open("/media/a.wav");
    f.source.probedOneTrack();
    f.source.opened(1, 1'000'000);
    f.source.answer(5, 1);
    auto part = f.box.cacheFile();
    part += ".part";
    const auto file = f.box.cacheFile();
    ASSERT_TRUE(std::filesystem::exists(part));
    f.box.close();
    EXPECT_EQ(f.source.cancels, 1);
    EXPECT_FALSE(std::filesystem::exists(part));
    EXPECT_FALSE(std::filesystem::exists(file));
}

// AUDIO_RAM_CACHE: legacy RAMCache's 4 MiB blocks (1048576 stereo frames),
// progress i / (blocks - 1) after block i, no file.
TEST(AudioBox, RamCacheReadsFourMebibyteBlocks)
{
    BoxFixture f("ram");
    f.settings.ram = true;
    f.box.open("/media/a.mkv");
    f.source.probedOneTrack();
    f.source.opened(2, 1'500'000);
    EXPECT_TRUE(f.box.cacheFile().empty());
    ASSERT_EQ(f.source.reads.size(), 1u);
    EXPECT_EQ(std::get<0>(f.source.reads.front()), 0);
    EXPECT_EQ(std::get<1>(f.source.reads.front()), 1'048'576);
    f.source.answer(0, 2, true);
    EXPECT_FLOAT_EQ(f.box.progress(), 0.f); // block 0 of 2
    EXPECT_EQ(std::get<0>(f.source.reads.front()), 1'048'576);
    EXPECT_EQ(std::get<1>(f.source.reads.front()), 1'500'000 - 1'048'576);
    f.source.answer(0, 2, true);
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    EXPECT_FLOAT_EQ(f.box.progress(), 1.f);
    EXPECT_EQ(at(*f.box.audio(), 1'048'577), 1'048'577 % 30000);
    EXPECT_TRUE(f.cached.empty());
    EXPECT_FALSE(std::filesystem::exists(f.settings.cacheDir));
}

// AUDIO_DELAY: a positive delay starts with silence and keeps the length (the
// last frames fall off); a negative one skips the start. The disk cache's
// name carries the delay in frames.
TEST(AudioBox, DelayShiftsTheAudioAsLegacyCachesIt)
{
    {
        BoxFixture f("delay-plus");
        f.settings.delayMs = 100;
        f.box.open("/media/a.mkv");
        f.source.probedOneTrack();
        f.source.opened(1, 10'000);
        EXPECT_EQ(f.box.delayFrames(), 4800);
        EXPECT_EQ(f.box.cacheFile().filename(), "a_track1_1ch_4800.w64");
        EXPECT_EQ(f.box.audio()->sampleCount(), 10'000);
        EXPECT_EQ(std::get<0>(f.source.reads.front()), 0);
        EXPECT_EQ(std::get<1>(f.source.reads.front()), 10'000);
        f.source.answer(0, 1, true);
        EXPECT_EQ(at(*f.box.audio(), 4799), 0);
        EXPECT_EQ(at(*f.box.audio(), 4800), 0); // source frame 0
        EXPECT_EQ(at(*f.box.audio(), 4801), 1);
        EXPECT_EQ(at(*f.box.audio(), 9999), 5199);
        EXPECT_EQ(at(*f.box.audio(), 10'000), 0);
    }
    {
        BoxFixture f("delay-minus");
        f.settings.delayMs = -100;
        f.box.open("/media/a.mkv");
        f.source.probedOneTrack();
        f.source.opened(1, 10'000);
        EXPECT_EQ(f.box.cacheFile().filename(), "a_track1_1ch_-4800.w64");
        EXPECT_EQ(f.box.audio()->sampleCount(), 5200);
        EXPECT_EQ(std::get<0>(f.source.reads.front()), 4800);
        EXPECT_EQ(std::get<1>(f.source.reads.front()), 5200);
        f.source.answer(0, 1, true);
        EXPECT_FLOAT_EQ(f.box.progress(), 1.f);
        EXPECT_EQ(at(*f.box.audio(), 0), 4800);
    }
    {
        // RAM: the silence is counted within the first block
        BoxFixture f("delay-ram");
        f.settings.ram = true;
        f.settings.delayMs = 100;
        f.box.open("/media/a.mkv");
        f.source.probedOneTrack();
        f.source.opened(1, 10'000);
        EXPECT_EQ(std::get<0>(f.source.reads.front()), 0);
        EXPECT_EQ(std::get<1>(f.source.reads.front()), 5200);
        f.source.answer(0, 1, true);
        EXPECT_EQ(at(*f.box.audio(), 4801), 1);
        EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    }
    {
        BoxFixture f("delay-long");
        f.settings.delayMs = 1000;
        f.box.open("/media/a.mkv");
        f.source.probedOneTrack();
        f.source.opened(1, 48'000);
        EXPECT_EQ(f.box.delayFrames(), 0);
        ASSERT_EQ(f.logged.size(), 1u);
        EXPECT_EQ(f.logged[0].first, "Delay failed, it's longer than audio duration time");
    }
}

// R3-hang-crash-loss: a block FFMS2 cannot decode is silence and caching goes
// on (legacy logged it for debugging and kept the buffer's old contents); a
// lost helper ends the open with a message (legacy crashed with it).
TEST(AudioBox, AFailedBlockIsSilenceAndCachingGoesOn)
{
    BoxFixture f("failed-block");
    f.box.open("/media/a.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 400'000);
    f.source.answer(7, 1);
    f.source.failRead({SourceError::BackendFailure, AudioStage::Read, "decode broke"});
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    EXPECT_EQ(at(*f.box.audio(), 332767), 7);
    EXPECT_EQ(at(*f.box.audio(), 332768), 0);
    EXPECT_EQ(at(*f.box.audio(), 399'999), 0);
    ASSERT_EQ(f.logged.size(), 1u);
    EXPECT_EQ(f.logged[0], (std::pair<std::string, AudioBox::LogLevel>{"error audiodecode broke", AudioBox::LogLevel::Debug}));

    f.box.open("/media/b.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 400'000);
    f.source.failRead({SourceError::HelperLost, AudioStage::Host, {}});
    EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
    EXPECT_EQ(f.box.error(), SourceError::HelperLost);
    EXPECT_EQ(f.logged.back().first, "Cannot open audio /media/b.mkv");
}

// Legacy ProviderFFMS2::Init's messages, with FFMS2's own text.
TEST(AudioBox, FailuresLogLegacyMessages)
{
    struct Case {
        SourceError error;
        AudioStage stage;
        std::optional<std::pair<std::string, AudioBox::LogLevel>> expected;
    };
    const std::vector<Case> cases = {
        {SourceError::BackendFailure, AudioStage::Indexing, std::pair{std::string("Indexing error occurred: boom"), AudioBox::LogLevel::Shown}},
        {SourceError::Cancelled, AudioStage::Indexing, std::nullopt},
        {SourceError::Unsupported, AudioStage::Source, std::pair{std::string("An error occurred when creating audio source: boom"), AudioBox::LogLevel::Shown}},
        {SourceError::Unsupported, AudioStage::Convert, std::pair{std::string("An error occurred when converting audio: boom"), AudioBox::LogLevel::Shown}},
        {SourceError::InvalidInput, AudioStage::Indexer, std::pair{std::string("Indexing error occurred: boom"), AudioBox::LogLevel::Debug}},
    };
    for (const auto &c : cases) {
        BoxFixture f("messages");
        f.box.open("/media/a.mkv");
        f.source.probedOneTrack();
        f.source.failOpen(c.error, c.stage, "boom");
        EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
        EXPECT_EQ(f.box.error(), c.error);
        if (c.expected) {
            ASSERT_EQ(f.logged.size(), 1u);
            EXPECT_EQ(f.logged[0], *c.expected);
        } else {
            EXPECT_TRUE(f.logged.empty());
        }
    }
    // a file FFMS2 cannot open is a debug message; one without audio says nothing
    BoxFixture f("probe");
    f.box.open("/media/missing.wav");
    std::exchange(f.source.pendingProbe, nullptr)(std::unexpected(AudioFailure{SourceError::InvalidInput, AudioStage::Indexer, "Can't open"}));
    EXPECT_EQ(f.logged.at(0), (std::pair<std::string, AudioBox::LogLevel>{"Indexing error occurred: Can't open", AudioBox::LogLevel::Debug}));
    f.box.open("/media/video-only.mkv");
    f.source.probed({}, true);
    EXPECT_EQ(f.box.error(), SourceError::Unsupported);
    EXPECT_EQ(f.logged.size(), 1u);
}

// A cache file that cannot be made ends the open with a message (legacy
// waited forever on audio that never came).
TEST(AudioBox, AnUnwritableCacheEndsTheOpen)
{
    BoxFixture f("unwritable");
    std::filesystem::create_directories(f.settings.cacheDir);
    const auto blocker = f.settings.cacheDir / "file";
    { std::ofstream(blocker) << "x"; }
    f.settings.cacheDir = blocker; // a file, not a folder
    f.box.open("/media/a.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 1000);
    EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
    ASSERT_EQ(f.logged.size(), 1u);
    EXPECT_TRUE(f.logged[0].first.starts_with("Cannot create the audio cache "));
    f.settings.cacheDir = blocker.parent_path();
}

// A store that cannot keep frames (RAM exhausted) stops the append.
TEST(DisplayAudio, AStoreThatCannotKeepFramesFailsTheAppend)
{
    struct Full : AudioStore {
        bool append(const std::int16_t *, std::int64_t) override { return false; }
        void read(std::int64_t, std::int64_t, std::int16_t *) const override {}
        std::int64_t frames() const override { return 0; }
        int channels() const override { return 1; }
    };
    DisplayAudio audio(48000, 10, std::make_unique<Full>());
    const std::int16_t one[] = {1, 2};
    EXPECT_FALSE(audio.appendFrames(one, 2, 1));
    EXPECT_FALSE(audio.appendSilence(5, 1));
    std::int16_t out[2] = {9, 9};
    audio.read(0, 2, out);
    EXPECT_EQ(out[0], 0);
}

// Legacy ProviderFFMS2::Init with several audio tracks.
TEST(AudioTracks, LegacyChoiceAndChooserRows)
{
    const std::vector<AudioTrack> tracks = {
        {1, true, true, "Main", "jpn", "aac"},
        {2, false, true, {}, "Commentary [eng]", "ac3"},
        {3, false, false, {}, {}, "flac"},
        {4, true, true, "Dub", "pol", "opus"},
    };
    const auto ask = legacyAudioTrackChoice(tracks, {});
    EXPECT_FALSE(ask.track);
    EXPECT_EQ(ask.rows, (std::vector<std::string>{"1: Main [jpn] (aac)", "2: eng (ac3)", "3: Untitled (flac)",
                                                   "4: Dub [pol] (opus)"}));
    EXPECT_EQ(ask.rowTracks, (std::vector<int>{1, 2, 3, 4}));
    // the language that comes first in ACCEPTED_AUDIO_STREAM wins, case aside
    EXPECT_EQ(legacyAudioTrackChoice(tracks, {"POL", "eng"}).track, 4);
    EXPECT_EQ(legacyAudioTrackChoice(tracks, {"eng", "pol"}).track, 2);
    EXPECT_TRUE(legacyAudioTrackChoice(tracks, {"eng"}).rows.empty());
    EXPECT_FALSE(legacyAudioTrackChoice(tracks, {"ger"}).track);
    // a bracketed language names a track without a name (text before the bracket)
    // (only past the second character), and an untitled one shows no name
    const std::vector<AudioTrack> named = {{0, false, true, {}, "Commentary [eng]", "ac3"},
                                           {5, true, true, "", "xy[ger]", "aac"},
                                           {6, true, true, "", "x[ger]", "aac"}};
    EXPECT_EQ(legacyAudioTrackChoice(named, {}).rows,
              (std::vector<std::string>{"0: eng (ac3)", "5: xy [ger] (aac)", "6:  [ger] (aac)"}));
    // one track is taken as it is; none gives nothing
    EXPECT_EQ(legacyAudioTrackChoice({tracks[2]}, {"eng"}).track, 3);
    EXPECT_FALSE(legacyAudioTrackChoice({}, {}).track);
    EXPECT_EQ(legacyAcceptedStreams("eng;;pol;"), (std::vector<std::string>{"eng", "pol"}));
    EXPECT_EQ(legacyAudioCacheName("C:\\media\\ep.01.mkv", 2, 2, -4800), "ep.01_track2_2ch_-4800.w64");
}

// "Choose the track": a row opens its track; Cancel opens nothing.
TEST(AudioBox, TheChooserPicksTheTrack)
{
    BoxFixture f("chooser");
    std::vector<std::string> shown;
    std::function<void(std::optional<int>)> answer;
    f.box.setChooser([&](const std::vector<std::string> &rows, std::function<void(std::optional<int>)> a) {
        shown = rows;
        answer = std::move(a);
    });
    const std::vector<AudioTrack> tracks = {{1, true, false, "A", {}, "aac"}, {2, true, false, "B", {}, "aac"}};
    f.box.open("/media/a.mkv");
    f.source.probed(tracks);
    EXPECT_TRUE(f.box.choosing());
    EXPECT_EQ(shown, (std::vector<std::string>{"1: A (aac)", "2: B (aac)"}));
    std::exchange(answer, nullptr)(1);
    EXPECT_EQ(f.source.openedTrack, 2);
    EXPECT_FALSE(f.box.choosing());
    f.box.open("/media/b.mkv");
    f.source.probed(tracks);
    std::exchange(answer, nullptr)(std::nullopt);
    EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
    EXPECT_EQ(f.box.error(), SourceError::Cancelled);
    EXPECT_TRUE(f.box.declined());
    EXPECT_TRUE(f.logged.empty());
    // ACCEPTED_AUDIO_STREAM answers without asking
    f.settings.acceptedStreams = {"jpn"};
    f.box.open("/media/c.mkv");
    f.source.probed({{1, true, true, "A", "eng", "aac"}, {3, true, true, "B", "jpn", "aac"}});
    EXPECT_FALSE(answer);
    EXPECT_EQ(f.source.openedTrack, 3);
}

// The open video's audio is read through the video's source (one index);
// closing cancels only the box's requests there.
TEST(AudioBox, TheVideosAudioIsReadThroughTheVideosSource)
{
    BoxFixture f("from-video");
    FakeAudioSource video;
    f.box.openFromVideo(video, "/media/v.mkv");
    EXPECT_TRUE(f.box.fromVideo());
    EXPECT_EQ(video.probedPath, "/media/v.mkv");
    EXPECT_TRUE(f.source.probedPath.empty());
    video.probedOneTrack();
    EXPECT_EQ(video.sourceTrack, 1);
    EXPECT_TRUE(f.source.openedPath.empty()); // not indexed again
    video.opened(2, 500'000);
    ASSERT_EQ(video.reads.size(), 1u);
    EXPECT_TRUE(f.source.reads.empty());
    f.box.close();
    EXPECT_EQ(video.cancels, 1);
    EXPECT_EQ(f.source.cancels, 0);
}

TEST(AudioBox, ClosingDropsLateAnswersAndAFailedOpenLeavesNoAudio)
{
    BoxFixture f("late");
    f.box.open("/media/a.wav");
    f.source.probedOneTrack();
    f.source.opened(1, 1'000'000);
    auto late = std::move(std::get<2>(f.source.reads.front()));
    f.source.reads.clear();
    f.box.close();
    EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
    late(std::unexpected(AudioFailure{SourceError::HelperLost, AudioStage::Host, {}})); // late: ignored
    EXPECT_EQ(f.box.audio(), nullptr);
    EXPECT_TRUE(f.logged.empty());
    // opening again first unloads (legacy SetFile); its failure leaves nothing open
    f.box.open("/media/b.wav");
    f.box.open("/media/c.wav");
    f.source.probedOneTrack();
    f.source.failOpen(SourceError::InvalidInput, AudioStage::Indexer, "x");
    EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
    EXPECT_EQ(f.box.error(), SourceError::InvalidInput);
    EXPECT_EQ(f.box.path(), "/media/c.wav");
}

// GLOBAL_OPEN_DUMMY_AUDIO and Provider::Get's dummy names.
TEST(AudioBox, BlankAudio)
{
    BoxFixture f("blank");
    f.box.open(AudioBox::kDummyName);
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    EXPECT_TRUE(f.source.probedPath.empty()); // nothing is indexed
    EXPECT_EQ(f.box.audio()->sampleRate(), 44100);
    EXPECT_EQ(f.box.audio()->sampleCount(), 396'900'000);
    EXPECT_EQ(f.box.audio()->sampleCount() * 1000 / 44100, 9'000'000); // 2 h 30 min
    f.box.open("?dummy:23.976000:40000:1280:720:47:163:254:");
    EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
    EXPECT_EQ(f.box.error(), SourceError::Unsupported);
}
