// A1: the audio box's display, pinned against legacy AudioDisplay.cpp,
// Provider.cpp, ProviderFFMS2.cpp, ProviderDummy.cpp and WaveformPeaks.cpp at
// 20d647c4. Expected values are worked out from legacy's arithmetic.

#include "hikari/application/audio_box.h"
#include "hikari/application/audio_display.h"

#include <gtest/gtest.h>

#include <deque>

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
struct FakeAudioSource : IndexedSourcePort {
    std::uint64_t gen = 0;
    std::string openedPath;
    Progress progress;
    AudioOpened pendingOpen;
    std::deque<std::tuple<std::int64_t, std::int64_t, AudioReady>> reads;
    int cancelledOpens = 0, cancelledReads = 0;
    std::uint64_t open(const std::string &, Progress, Opened) override { return ++gen; }
    void cancelOpen() override { ++cancelledOpens; }
    void frame(int, FrameReady) override {}
    void openAudio(int, AudioOpened) override {}
    void audio(std::int64_t start, std::int64_t count, AudioReady done) override
    {
        reads.emplace_back(start, count, std::move(done));
    }
    std::uint64_t openDisplayAudio(const std::string &path, Progress p, AudioOpened done) override
    {
        openedPath = path;
        progress = std::move(p);
        pendingOpen = std::move(done);
        return ++gen;
    }
    void beginPcm(std::int64_t, std::int64_t, int, int, PcmBegun) override {}
    void nextPcm(std::int64_t, PcmReady) override {}
    void cancelReads() override { ++cancelledReads; }
    std::uint64_t generation() const override { return gen; }

    void opened(int channels, std::int64_t count)
    {
        AudioInfo info;
        info.generation = gen;
        info.format = SampleFormat::S16;
        info.bitsPerSample = 16;
        info.sampleRate = 48000;
        info.channels = channels;
        info.sampleCount = count;
        pendingOpen(info);
    }
    // Answers the oldest read with frames whose every channel is `value`.
    void answer(std::int16_t value, int channels)
    {
        auto [start, count, done] = std::move(reads.front());
        reads.pop_front();
        AudioBlock b;
        b.generation = gen;
        b.start = start;
        b.count = count;
        b.channels = channels;
        b.samples.resize(static_cast<std::size_t>(count * channels) * 2);
        auto *p = reinterpret_cast<std::int16_t *>(b.samples.data());
        for (std::int64_t i = 0; i < count * channels; ++i)
            p[i] = value;
        done(std::move(b));
    }
};

} // namespace

// SetFile through the source: indexing, then legacy disk-cache blocks with
// their progress, then the waveform's peak table.
TEST(AudioBox, OpensIndexesAndDecodesInBlocks)
{
    FakeAudioSource source;
    AudioBox box(source);
    int changes = 0;
    box.setObserver([&] { ++changes; });
    box.open("/media/episode.mkv");
    EXPECT_EQ(source.openedPath, "/media/episode.mkv");
    EXPECT_EQ(box.state(), AudioBox::State::Opening);
    EXPECT_TRUE(box.isOpen());
    source.progress(40, 100);
    EXPECT_EQ(box.indexing(), (std::pair<std::int64_t, std::int64_t>{40, 100}));
    source.opened(2, 400000);
    EXPECT_EQ(box.state(), AudioBox::State::Loading);
    ASSERT_EQ(source.reads.size(), 1u);
    EXPECT_EQ(std::get<0>(source.reads.front()), 0);
    EXPECT_EQ(std::get<1>(source.reads.front()), 332768);
    source.answer(-3, 2);
    EXPECT_FLOAT_EQ(box.progress(), 332768.f / 400000.f);
    ASSERT_EQ(source.reads.size(), 1u);
    EXPECT_EQ(std::get<0>(source.reads.front()), 332768);
    EXPECT_EQ(std::get<1>(source.reads.front()), 400000 - 332768);
    EXPECT_EQ(box.audio()->peaks(), nullptr);
    source.answer(100, 2);
    EXPECT_EQ(box.state(), AudioBox::State::Ready);
    EXPECT_FLOAT_EQ(box.progress(), 1.f);
    ASSERT_NE(box.audio()->peaks(), nullptr);
    std::int16_t s[2];
    box.audio()->read(332767, 2, s);
    EXPECT_EQ(s[0], -3);
    EXPECT_EQ(s[1], 100);
    EXPECT_GT(changes, 4);
}

TEST(AudioBox, ClosingDropsLateAnswersAndAFailedOpenLeavesNoAudio)
{
    FakeAudioSource source;
    AudioBox box(source);
    box.open("/media/a.wav");
    source.opened(1, 1'000'000);
    box.close();
    EXPECT_EQ(source.cancelledReads, 1);
    EXPECT_EQ(box.state(), AudioBox::State::Closed);
    source.answer(5, 1); // late: ignored
    EXPECT_EQ(box.audio(), nullptr);
    // opening again first unloads (legacy SetFile); its failure leaves nothing open
    box.open("/media/b.wav");
    box.open("/media/c.wav");
    EXPECT_EQ(source.cancelledOpens, 1);
    source.pendingOpen(std::unexpected(SourceError::InvalidInput));
    EXPECT_EQ(box.state(), AudioBox::State::Closed);
    EXPECT_EQ(box.error(), SourceError::InvalidInput);
    EXPECT_EQ(box.path(), "/media/c.wav");
}

// GLOBAL_OPEN_DUMMY_AUDIO and Provider::Get's dummy names.
TEST(AudioBox, BlankAudio)
{
    FakeAudioSource source;
    AudioBox box(source);
    box.open(AudioBox::kDummyName);
    EXPECT_EQ(box.state(), AudioBox::State::Ready);
    EXPECT_TRUE(source.openedPath.empty()); // nothing is indexed
    EXPECT_EQ(box.audio()->sampleRate(), 44100);
    EXPECT_EQ(box.audio()->sampleCount(), 396'900'000);
    EXPECT_EQ(box.audio()->sampleCount() * 1000 / 44100, 9'000'000); // 2 h 30 min
    box.open("?dummy:23.976000:40000:1280:720:47:163:254:");
    EXPECT_EQ(box.state(), AudioBox::State::Closed);
    EXPECT_EQ(box.error(), SourceError::Unsupported);
}
