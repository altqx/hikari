#include "hikari/application/audio_box.h"
#include "hikari/application/audio_display.h"

#include <gtest/gtest.h>

#include <cstring>
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

int width30(AudioShape::Font, std::string_view) { return 30; }

AudioView minuteView()
{
    AudioView view;
    view.setSamplesPercent(50, false);
    view.setSource(48000, 48000 * 60);
    view.resize(1000, 150, 22, 16);
    return view;
}

}

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

TEST(DisplayAudio, MixesDownLikeLegacy)
{
    DisplayAudio audio(48000, 4);
    const std::int16_t stereo[] = {-3, 0, 32767, 32767, -32768, -32767, 5, -2};
    audio.appendFrames(stereo, 4, 2);
    std::int16_t out[6];
    audio.read(0, 6, out);
    EXPECT_EQ(out[0], -1);
    EXPECT_EQ(out[1], 32767);
    EXPECT_EQ(out[2], -32767);
    EXPECT_EQ(out[3], 1);
    EXPECT_EQ(out[4], 0);
    EXPECT_EQ(out[5], 0);
    DisplayAudio mono(48000, 2);
    const std::int16_t one[] = {-7, 9};
    mono.appendFrames(one, 2, 1);
    mono.read(0, 2, out);
    EXPECT_EQ(out[0], -7);
    EXPECT_EQ(out[1], 9);
    EXPECT_EQ(mono.peaks(), nullptr);
    mono.finish();
    ASSERT_NE(mono.peaks(), nullptr);
    EXPECT_EQ(mono.peaks()->blockSamples(), 256);
}

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
    const auto columns = legacyWaveform(audio, 0, 4, 100, 1323, 1.f);
    EXPECT_EQ(columns.min, (std::vector<int>{50, 50, 50, 50}));
    EXPECT_EQ(columns.peak, (std::vector<int>{50, 50, 50, 50}));
}

TEST(Waveform, ColumnsFromSamplesAtCloseZoom)
{
    DisplayAudio audio(48000, 33);
    std::vector<std::int16_t> mono(33, 0);
    mono[3] = 32767;
    mono[7] = -32768;
    mono[22] = 16384;
    mono[25] = -1;
    audio.appendFrames(mono.data(), 33, 1);
    audio.finish();
    const auto columns = legacyWaveform(audio, 0, 4, 100, 11, 1.f);
    EXPECT_EQ(columns.min, (std::vector<int>{1, 50, 25, 50}));
    EXPECT_EQ(columns.peak, (std::vector<int>{100, 50, 50, 50}));
    const auto loud = legacyWaveform(audio, 0, 3, 100, 11, audioScaleFromSlider(75));
    EXPECT_EQ(loud.min, (std::vector<int>{0, 50, 0}));
    EXPECT_EQ(loud.peak, (std::vector<int>{100, 50, 50}));
}

TEST(Waveform, ZoomedOutColumnsCoverWholePeakBlocks)
{
    DisplayAudio audio(48000, 2880);
    std::vector<std::int16_t> mono(2880, 0);
    mono[1500] = 32767;
    audio.appendFrames(mono.data(), 2880, 1);
    const auto beforePeaks = legacyWaveform(audio, 0, 2, 128, 1440, 1.f);
    EXPECT_EQ(beforePeaks.min, (std::vector<int>{64, 1}));
    audio.finish();
    const auto withPeaks = legacyWaveform(audio, 0, 2, 128, 1440, 1.f);
    EXPECT_EQ(withPeaks.min, (std::vector<int>{1, 1}));
    EXPECT_EQ(withPeaks.peak, (std::vector<int>{64, 64}));
    const auto close = legacyWaveform(audio, 0, 2, 128, 1023, 1.f);
    EXPECT_EQ(close.min, (std::vector<int>{64, 1}));
}

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
    EXPECT_EQ(at(48000, 1), 1);
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

TEST(AudioView, FollowsTheActiveLine)
{
    AudioView view = minuteView();
    view.makeVisible(30000, 32000, false, false, 16);
    EXPECT_EQ(view.position(), 950);
    EXPECT_EQ(view.positionSample(), 950 * 1440);
    EXPECT_FLOAT_EQ(view.xAtMs(30000), 50.f);
    view.makeVisible(31000, 32000, false, false, 16);
    EXPECT_EQ(view.position(), 950);
    view.makeVisible(31000, 32000, true, false, 16);
    EXPECT_EQ(view.position(), (1488000 + 1536000 - 1000 * 1440) / 2 / 1440);
    view.makeVisible(50000, 52000, false, true, 16);
    EXPECT_EQ(view.position(), (2496000 - 950 * 1440) / 1440);
    const auto bar = view.updateScrollbar(16);
    EXPECT_EQ(bar.page, 83);
    EXPECT_EQ(bar.range, 2000 / 12 + 16);
    EXPECT_EQ(bar.position, static_cast<int>(view.position() / 12));
}

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
    EXPECT_EQ(dashes[21], (std::pair<float, float>{126, 128}));
}

TEST(AudioScene, LineMarksAtLegacyPositions)
{
    const AudioView view = minuteView();
    AudioMarks marks;
    marks.startMs = 1000;
    marks.endMs = 2000;
    WaveformColumns columns;
    columns.min.assign(1000, 60);
    columns.peak.assign(1000, 70);
    const AudioDisplayOptions o;
    const auto scene = audioScene(view, columns, marks, o, width30);
    ASSERT_FALSE(scene.empty());
    EXPECT_EQ(scene[0].kind, AudioShape::Kind::Fill);
    EXPECT_EQ(scene[0].colour, o.background);
    EXPECT_EQ(scene[0].y2, 150);
    const auto selection = shapes(scene, AudioShape::Kind::Fill, o.selectionBackground);
    ASSERT_EQ(selection.size(), 1u);
    EXPECT_EQ(selection[0].x1, 33);
    EXPECT_EQ(selection[0].x2, 67);
    EXPECT_EQ(selection[0].y2, 128);
    const auto selected = shapes(scene, AudioShape::Kind::Line, o.waveformSelected);
    ASSERT_EQ(selected.size(), 33u);
    EXPECT_EQ(selected.front().x1, 33.5f);
    EXPECT_EQ(selected.back().x1, 65.5f);
    EXPECT_EQ(selected.front().y1, 70.5f);
    EXPECT_EQ(selected.front().y2, 59.5f);
    EXPECT_EQ(shapes(scene, AudioShape::Kind::Line, o.waveform).size(), 1000u - 33u);
    const auto starts = shapes(scene, AudioShape::Kind::Line, o.lineStart);
    ASSERT_GE(starts.size(), 2u);
    EXPECT_EQ(starts[0].x1, 34);
    EXPECT_EQ(starts[0].y2, 128);
    EXPECT_EQ(starts[0].width, 2);
    const auto flags = shapes(scene, AudioShape::Kind::Triangle, o.lineStart);
    ASSERT_EQ(flags.size(), 4u);
    EXPECT_EQ(flags[0].x2, 44);
    EXPECT_EQ(flags[1].y1, 118);
    EXPECT_EQ(flags[2].x1, 67);
    EXPECT_EQ(flags[2].x2, 57);
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
    EXPECT_EQ(inactive[0].y1, 70);
    EXPECT_EQ(inactive[0].y2, 59);
    EXPECT_EQ(inactive[30].x1, 83);
    const auto boundaries = shapes(scene, AudioShape::Kind::Line, o.inactiveBoundary);
    ASSERT_EQ(boundaries.size(), 4u);
    EXPECT_EQ(boundaries[0].x1, 1);
    EXPECT_EQ(boundaries[1].x1, 31);
    EXPECT_EQ(boundaries[2].x1, 84);
    EXPECT_EQ(boundaries[3].x1, 101);
    AudioDisplayOptions none;
    none.inactiveLines = 0;
    EXPECT_TRUE(shapes(audioScene(view, columns, marks, none, width30), AudioShape::Kind::Fill, o.inactiveBackground).empty());
}

TEST(AudioScene, KeyframeVideoAndRulerMarks)
{
    const AudioView view = minuteView();
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
    EXPECT_EQ(keyframes[0].x1, 0);
    EXPECT_EQ(keyframes[1].x1, 32);
    EXPECT_EQ(keyframes[2].x1, 66);
    const auto video = shapes(scene, AudioShape::Kind::Line, o.cursor);
    ASSERT_EQ(video.size(), 22u);
    EXPECT_FLOAT_EQ(video[0].x1, 1001 * 48.f / 1440.f);
    EXPECT_EQ(video[0].width, 2);
    EXPECT_EQ(video[21].y2, 128);
    const auto seconds = shapes(scene, AudioShape::Kind::Line, o.secondBoundaries);
    EXPECT_EQ(seconds.size(), 30u * 22u);
    EXPECT_EQ(seconds[22].x1, 34);
    EXPECT_EQ(seconds[44].x1, 67);
    const auto ruler = shapes(scene, AudioShape::Kind::Line, o.timescaleText);
    ASSERT_GE(ruler.size(), 3u);
    EXPECT_EQ(ruler[0].y1, 128);
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

TEST(AudioScene, RulerLabelsSkipWhenCrowdedAndFillWhenSparse)
{
    AudioView far;
    far.setSamplesPercent(100, false);
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
    const auto sparse = labels(far);
    ASSERT_FALSE(sparse.empty());
    EXPECT_EQ(sparse[0], "0");
    EXPECT_EQ(sparse[1], "10");
    EXPECT_EQ(sparse[6], "1:00");
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
    const auto progress = audioProgressScene(view, 0.5f, o);
    ASSERT_EQ(progress.size(), 1u + 8u + 2u);
    EXPECT_EQ(progress[1].colour, 0xFF00FFFFu);
    EXPECT_EQ(progress[1].y1, 54);
    EXPECT_EQ(progress[9].width, 37);
    EXPECT_EQ(progress[9].x2, 500);
    EXPECT_EQ(progress[10].text, "50%");
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

TEST(AudioLines, InactiveLinesAndSelections)
{
    const std::vector<AudioLineSpan> lines = {
        {0, 1000, true}, {1500, 1800, false}, {2000, 3000, true}, {0, 0, true}, {5000, 6000, true}};
    EXPECT_EQ(legacyKeyFromPosition(lines, 2, -1), 0);
    EXPECT_EQ(legacyKeyFromPosition(lines, 2, 1), 3);
    EXPECT_EQ(legacyKeyFromPosition(lines, 4, 1), 4);
    EXPECT_EQ(legacyKeyFromPosition(lines, 0, -1), 0);
    EXPECT_EQ(legacyInactiveLines(1, lines, 2), (std::vector<std::pair<int, int>>{{0, 1000}, {0, 0}}));
    EXPECT_EQ(legacyInactiveLines(2, lines, 2), (std::vector<std::pair<int, int>>{{0, 1000}, {0, 0}, {5000, 6000}}));
    EXPECT_TRUE(legacyInactiveLines(0, lines, 2).empty());
    EXPECT_EQ(legacyLineSelection(lines, 2, 0), (std::pair{2000, 3000}));
    EXPECT_EQ(legacyLineSelection(lines, 3, 2), (std::pair{3000, 8000}));
    EXPECT_EQ(legacyLineSelection(lines, 3, 0), (std::pair{0, 5000}));
}

namespace {

struct FakeAudioSource : DisplayAudioPort {
    std::string probedPath, openedPath, openedIndexFile;
    int openedTrack = -1;
    int opens = 0;
    Probed pendingProbe;
    Progress progress;
    Opened pendingOpen;
    std::deque<std::tuple<std::int64_t, std::int64_t, Read, BlockRead>> reads;
    int cancels = 0;
    std::vector<std::int16_t> block;
    void probe(const std::string &path, Probed done) override
    {
        probedPath = path;
        pendingProbe = std::move(done);
    }
    void openDisplayAudio(const std::string &path, int track, const std::string &indexFile, Progress p,
                          Opened done) override
    {
        ++opens;
        openedPath = path;
        openedTrack = track;
        openedIndexFile = indexFile;
        block.clear();
        progress = std::move(p);
        pendingOpen = std::move(done);
    }
    void displayAudio(const BlockRead &read, Read done) override
    {
        reads.emplace_back(read.start, read.frames, std::move(done), read);
    }
    void cancelDisplay() override
    {
        ++cancels;
        auto pending = std::move(reads);
        reads.clear();
        for (auto &[start, count, done, read] : pending)
            done(std::unexpected(AudioFailure{SourceError::Cancelled, AudioStage::Host, {}, {}}));
    }
    std::vector<std::byte> read(const BlockRead &r, std::int64_t decoded, std::int16_t value, int channels, bool ramp)
    {
        std::vector<std::int16_t> fresh;
        auto &buffer = r.fresh ? fresh : block;
        const auto samples = static_cast<std::size_t>(r.frames * channels);
        if (buffer.size() < samples)
            buffer.resize(samples, 0);
        for (std::int64_t i = r.decode; i < r.frames; ++i)
            buffer[static_cast<std::size_t>(i)] = 0;
        for (std::int64_t i = 0; i < decoded; ++i)
            for (int c = 0; c < channels; ++c)
                buffer[static_cast<std::size_t>(i * channels + c)] =
                    ramp ? static_cast<std::int16_t>((r.start + i) % 30000) : value;
        std::vector<std::byte> out(samples * 2);
        std::memcpy(out.data(), buffer.data(), out.size());
        return out;
    }

    void probed(std::vector<AudioTrack> tracks, bool hasVideo = false)
    {
        MediaProbe p;
        p.hasVideo = hasVideo;
        p.audio = std::move(tracks);
        std::exchange(pendingProbe, nullptr)(std::move(p));
    }
    void probedOneTrack() { probed({AudioTrack{1, false, false, {}, {}, "aac"}}); }
    void opened(int channels, std::int64_t count, bool newIndex = true)
    {
        AudioInfo info;
        info.format = SampleFormat::S16;
        info.bitsPerSample = 16;
        info.sampleRate = 48000;
        info.channels = channels;
        info.sampleCount = count;
        std::exchange(pendingOpen, nullptr)(DisplayAudioOpened{info, newIndex});
    }
    void failOpen(SourceError error, AudioStage stage, std::string text)
    {
        std::exchange(pendingOpen, nullptr)(std::unexpected(AudioFailure{error, stage, std::move(text), {}}));
    }
    void answer(std::int16_t value, int channels, bool ramp = false)
    {
        auto [start, count, done, r] = std::move(reads.front());
        reads.pop_front();
        AudioBlock b;
        b.start = start;
        b.count = count;
        b.channels = channels;
        b.samples = read(r, r.decode, value, channels, ramp);
        done(std::move(b));
    }
    // FFMS2 leaves the frames it decoded before a failure in the buffer.
    void failRead(AudioFailure failure, int channels = 1, std::int64_t decoded = 0, std::int16_t value = 0)
    {
        auto [start, count, done, r] = std::move(reads.front());
        reads.pop_front();
        if (failure.stage == AudioStage::Read)
            failure.samples = read(r, decoded, value, channels, false);
        done(std::unexpected(std::move(failure)));
    }
};

struct BoxFixture {
    FakeAudioSource source;
    AudioBox box{source};
    AudioCacheSettings settings;
    std::vector<std::pair<std::string, AudioBox::LogLevel>> logged;
    explicit BoxFixture(const char *name)
    {
        settings.cacheDir = std::filesystem::temp_directory_path() / ("hikari-a1-" + std::string(name));
        settings.indexDir = std::filesystem::temp_directory_path() / ("hikari-a1-" + std::string(name) + "-indices");
        std::filesystem::remove_all(settings.cacheDir);
        std::filesystem::remove_all(settings.indexDir);
        box.setSettings([this] { return settings; });
        box.setLog([this](const std::string &m, AudioBox::LogLevel l) { logged.emplace_back(m, l); });
    }
    ~BoxFixture()
    {
        // Windows cannot remove a file that is still open.
        box.close();
        std::error_code ec;
        std::filesystem::remove_all(settings.cacheDir, ec);
        std::filesystem::remove_all(settings.indexDir, ec);
    }
};

std::int16_t at(const DisplayAudio &audio, std::int64_t i)
{
    std::int16_t s = -1;
    audio.read(i, 1, &s);
    return s;
}

}

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
    EXPECT_EQ(std::filesystem::path(f.source.openedIndexFile), f.settings.indexDir / "episode_1.ffindex");
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
    EXPECT_EQ(std::get<3>(f.source.reads.front()).decode, 332768);
    EXPECT_FALSE(std::get<3>(f.source.reads.front()).fresh);
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
    EXPECT_EQ(at(*f.box.audio(), 332767), -3);
    EXPECT_EQ(at(*f.box.audio(), 332768), 100);
    EXPECT_EQ(at(*f.box.audio(), 400000), 0);
    EXPECT_GT(changes, 4);
    f.box.close();
    EXPECT_FALSE(std::filesystem::exists(part));
    EXPECT_EQ(std::filesystem::file_size(file), 400000u * 4);
    EXPECT_TRUE(f.logged.empty());
}

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
    EXPECT_TRUE(std::get<3>(f.source.reads.front()).fresh);
    f.source.answer(0, 2, true);
    EXPECT_FLOAT_EQ(f.box.progress(), 0.f);
    EXPECT_EQ(std::get<0>(f.source.reads.front()), 1'048'576);
    EXPECT_EQ(std::get<1>(f.source.reads.front()), 1'500'000 - 1'048'576);
    f.source.answer(0, 2, true);
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    EXPECT_FLOAT_EQ(f.box.progress(), 1.f);
    EXPECT_EQ(at(*f.box.audio(), 1'048'577), 1'048'577 % 30000);
    EXPECT_FALSE(std::filesystem::exists(f.settings.cacheDir));
}

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
        EXPECT_EQ(at(*f.box.audio(), 4800), 0);
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
        EXPECT_EQ(std::get<3>(f.source.reads.front()).decode, 400);
        f.source.answer(0, 1, true);
        EXPECT_FLOAT_EQ(f.box.progress(), 1.f);
        EXPECT_EQ(at(*f.box.audio(), 0), 4800);
        EXPECT_EQ(at(*f.box.audio(), 399), 5199);
        EXPECT_EQ(at(*f.box.audio(), 400), 0);
        EXPECT_EQ(at(*f.box.audio(), 5199), 0);
    }
    {
        BoxFixture f("delay-minus-stereo");
        f.settings.delayMs = -3000;
        f.box.open("/media/a.mkv");
        f.source.probedOneTrack();
        f.source.opened(2, 700'000);
        EXPECT_EQ(f.box.audio()->sampleCount(), 556'000);
        EXPECT_EQ(std::get<3>(f.source.reads.front()).decode, 332768);
        f.source.answer(0, 2, true);
        const auto second = std::get<3>(f.source.reads.front());
        EXPECT_EQ(second.start, 476'768);
        EXPECT_EQ(second.frames, 700'000 - 476'768);
        EXPECT_EQ(second.decode, 556'000 - 476'768);
        f.source.answer(0, 2, true);
        ASSERT_EQ(f.box.state(), AudioBox::State::Ready);
        EXPECT_EQ(at(*f.box.audio(), 332'768 + 50'000), (476'768 + 50'000) % 30000);
        EXPECT_EQ(at(*f.box.audio(), 332'768 + 79'231), (476'768 + 79'231) % 30000);
        EXPECT_EQ(at(*f.box.audio(), 332'768 + 79'232), 0);
        EXPECT_EQ(at(*f.box.audio(), 332'768 + 111'615), 0);
        EXPECT_EQ(at(*f.box.audio(), 332'768 + 111'616), (144'000 + 111'616) % 30000);
        EXPECT_EQ(at(*f.box.audio(), 556'000 - 1), (144'000 + 223'231) % 30000);
    }
    {
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
        EXPECT_EQ(f.logged[0].second, AudioBox::LogLevel::Warning);
    }
}

TEST(AudioBox, AFailedDiskBlockKeepsWhatTheBlockBufferHeldAsLegacyCachesIt)
{
    BoxFixture f("failed-block");
    f.box.open("/media/a.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 800'000);
    f.source.answer(0, 1, true);
    f.source.failRead({SourceError::BackendFailure, AudioStage::Read, "decode broke", {}}, 1, 1000, 77);
    f.source.answer(9, 1);
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    EXPECT_EQ(at(*f.box.audio(), 332767), 332767 % 30000);
    EXPECT_EQ(at(*f.box.audio(), 332768), 77);
    EXPECT_EQ(at(*f.box.audio(), 332768 + 999), 77);
    EXPECT_EQ(at(*f.box.audio(), 332768 + 1000), 1000);
    EXPECT_EQ(at(*f.box.audio(), 332768 + 1234), 1234);
    EXPECT_EQ(at(*f.box.audio(), 2 * 332768), 9);
    ASSERT_EQ(f.logged.size(), 1u);
    EXPECT_EQ(f.logged[0], (std::pair<std::string, AudioBox::LogLevel>{"error audiodecode broke", AudioBox::LogLevel::Debug}));

    f.box.open("/media/first.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 1000);
    f.source.failRead({SourceError::BackendFailure, AudioStage::Read, "x", {}});
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    EXPECT_EQ(at(*f.box.audio(), 10), 0);

    f.box.open("/media/b.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 400'000);
    f.source.failRead({SourceError::HelperLost, AudioStage::Host, {}, {}});
    EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
    EXPECT_EQ(f.box.error(), SourceError::HelperLost);
    EXPECT_EQ(f.logged.back().first, "Cannot open audio /media/b.mkv");
}

TEST(AudioBox, AFailedRamBlockKeepsTheDecodedFramesAndIsSilenceAfter)
{
    BoxFixture f("failed-ram-block");
    f.settings.ram = true;
    f.box.open("/media/a.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 3'000'000);
    f.source.answer(7, 1);
    f.source.failRead({SourceError::BackendFailure, AudioStage::Read, "decode broke", {}}, 1, 500, 5);
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    EXPECT_EQ(at(*f.box.audio(), 2'097'151), 7);
    EXPECT_EQ(at(*f.box.audio(), 2'097'152), 5);
    EXPECT_EQ(at(*f.box.audio(), 2'097'651), 5);
    EXPECT_EQ(at(*f.box.audio(), 2'097'652), 0);
    EXPECT_EQ(at(*f.box.audio(), 2'999'999), 0);
}

TEST(AudioBox, ACacheIsReusedOnlyWithAnIndexReadFromItsFile)
{
    BoxFixture f("reuse");
    f.box.open("/media/ep.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 400'000);
    f.source.answer(0, 1, true);
    f.source.answer(0, 1, true);
    ASSERT_EQ(f.box.state(), AudioBox::State::Ready);
    const auto file = f.box.cacheFile();
    f.box.close();
    ASSERT_TRUE(std::filesystem::exists(file));

    std::vector<std::function<void()>> deferred;
    f.box.setDefer([&](std::function<void()> step) { deferred.push_back(std::move(step)); });
    f.box.open("/media/ep.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 400'000, false);
    EXPECT_TRUE(f.box.cacheReused());
    EXPECT_EQ(f.box.cacheFile(), file);
    EXPECT_TRUE(f.source.reads.empty());
    EXPECT_EQ(f.box.state(), AudioBox::State::Loading);
    ASSERT_EQ(deferred.size(), 1u);
    std::exchange(deferred, {}).front()();
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    ASSERT_NE(f.box.audio()->peaks(), nullptr);
    EXPECT_EQ(at(*f.box.audio(), 350'000), 350'000 % 30000);
    std::int16_t lo = 0, hi = 0;
    ASSERT_TRUE(f.box.audio()->peaks()->range(0, 256, &lo, &hi));
    EXPECT_EQ(hi, 255);
    f.box.close();
    EXPECT_TRUE(std::filesystem::exists(file));

    f.box.setDefer(nullptr);
    f.box.open("/media/ep.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 400'000, true);
    EXPECT_FALSE(f.box.cacheReused());
    EXPECT_FALSE(std::filesystem::exists(file));
    EXPECT_EQ(f.source.reads.size(), 1u);
    f.box.close();

    f.settings.ram = true;
    f.box.open("/media/ep.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 400'000, false);
    EXPECT_FALSE(f.box.cacheReused());
    EXPECT_EQ(f.source.reads.size(), 1u);
}

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
    BoxFixture f("probe");
    f.box.open("/media/missing.wav");
    std::exchange(f.source.pendingProbe, nullptr)(std::unexpected(AudioFailure{SourceError::InvalidInput, AudioStage::Indexer, "Can't open"}));
    EXPECT_EQ(f.logged.at(0), (std::pair<std::string, AudioBox::LogLevel>{"Indexing error occurred: Can't open", AudioBox::LogLevel::Debug}));
    f.box.open("/media/video-only.mkv");
    f.source.probed({}, true);
    EXPECT_EQ(f.box.error(), SourceError::Unsupported);
    EXPECT_EQ(f.logged.size(), 1u);
}

TEST(AudioBox, AnUnwritableCacheEndsTheOpen)
{
    BoxFixture f("unwritable");
    std::filesystem::create_directories(f.settings.cacheDir);
    const auto blocker = f.settings.cacheDir / "file";
    { std::ofstream(blocker) << "x"; }
    f.settings.cacheDir = blocker;
    f.box.open("/media/a.mkv");
    f.source.probedOneTrack();
    f.source.opened(1, 1000);
    EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
    ASSERT_EQ(f.logged.size(), 1u);
    EXPECT_TRUE(f.logged[0].first.starts_with("Cannot create the audio cache "));
    f.settings.cacheDir = blocker.parent_path();
}

TEST(DisplayAudio, TheDiskCacheReadsBackFramesBeforeTheyAreWritten)
{
    const auto dir = std::filesystem::temp_directory_path() / "hikari-v1-disk-store";
    std::filesystem::remove_all(dir);
    const auto path = dir / "a_track1_2ch_0.w64";
    std::string error;
    auto store = diskAudioStore(path, 2, &error);
    ASSERT_TRUE(store) << error;
    constexpr int kPieces = 40, kFrames = 5000;
    const auto sample = [](std::int64_t frame, int channel) {
        return static_cast<std::int16_t>((frame * 7 + channel * 3) % 30011 - 15000);
    };
    std::vector<std::int16_t> piece(static_cast<std::size_t>(kFrames) * 2);
    for (int p = 0; p < kPieces; ++p) {
        for (int i = 0; i < kFrames; ++i)
            for (int c = 0; c < 2; ++c)
                piece[static_cast<std::size_t>(i) * 2 + c] = sample(std::int64_t(p) * kFrames + i, c);
        ASSERT_TRUE(store->append(piece.data(), kFrames));
        ASSERT_EQ(store->frames(), std::int64_t(p + 1) * kFrames);
        const std::int64_t start = std::max<std::int64_t>(0, std::int64_t(p) * kFrames - 1234);
        const std::int64_t count = std::int64_t(p + 1) * kFrames - start;
        std::vector<std::int16_t> out(static_cast<std::size_t>(count) * 2, 1);
        store->read(start, count, out.data());
        for (std::int64_t i = 0; i < count; ++i)
            for (int c = 0; c < 2; ++c)
                ASSERT_EQ(out[static_cast<std::size_t>(i) * 2 + c], sample(start + i, c)) << "frame " << start + i;
    }
    store->complete();
    store.reset();
    auto part = path;
    part += ".part";
    EXPECT_FALSE(std::filesystem::exists(part));
    ASSERT_EQ(std::filesystem::file_size(path), std::uintmax_t(kPieces) * kFrames * 4);
    std::ifstream file(path, std::ios::binary);
    std::vector<std::int16_t> all(static_cast<std::size_t>(kPieces) * kFrames * 2);
    file.read(reinterpret_cast<char *>(all.data()), static_cast<std::streamsize>(all.size() * 2));
    for (std::int64_t i = 0; i < std::int64_t(kPieces) * kFrames; i += 997)
        EXPECT_EQ(all[static_cast<std::size_t>(i) * 2 + 1], sample(i, 1));
    file.close(); // Windows refuses to remove an open file.
    std::filesystem::remove_all(dir);
}

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
    EXPECT_EQ(legacyAudioTrackChoice(tracks, {"POL", "eng"}).track, 4);
    EXPECT_EQ(legacyAudioTrackChoice(tracks, {"eng", "pol"}).track, 2);
    EXPECT_TRUE(legacyAudioTrackChoice(tracks, {"eng"}).rows.empty());
    EXPECT_FALSE(legacyAudioTrackChoice(tracks, {"ger"}).track);
    const std::vector<AudioTrack> named = {{0, false, true, {}, "Commentary [eng]", "ac3"},
                                           {5, true, true, "", "xy[ger]", "aac"},
                                           {6, true, true, "", "x[ger]", "aac"}};
    EXPECT_EQ(legacyAudioTrackChoice(named, {}).rows,
              (std::vector<std::string>{"0: eng (ac3)", "5: xy [ger] (aac)", "6:  [ger] (aac)"}));
    EXPECT_EQ(legacyAudioTrackChoice({tracks[2]}, {"eng"}).track, 3);
    EXPECT_FALSE(legacyAudioTrackChoice({}, {}).track);
    EXPECT_EQ(legacyAcceptedStreams("eng;;pol;"), (std::vector<std::string>{"eng", "pol"}));
    EXPECT_EQ(legacyAudioCacheName("C:\\media\\ep.01.mkv", 2, 2, -4800), "ep.01_track2_2ch_-4800.w64");
    EXPECT_EQ(legacyIndexName("C:\\media\\ep.01.mkv", 2), "ep.01_2.ffindex");
    EXPECT_EQ(legacyIndexName("/media/clip.mp4", -1), "clip_-1.ffindex");
}

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
    f.settings.acceptedStreams = {"jpn"};
    f.box.open("/media/c.mkv");
    f.source.probed({{1, true, true, "A", "eng", "aac"}, {3, true, true, "B", "jpn", "aac"}});
    EXPECT_FALSE(answer);
    EXPECT_EQ(f.source.openedTrack, 3);
}

TEST(AudioBox, TheVideosAudioOpensTheVideosTrackFromItsIndexFile)
{
    BoxFixture f("from-video");
    bool asked = false;
    f.box.setChooser([&](const std::vector<std::string> &, std::function<void(std::optional<int>)>) { asked = true; });
    f.box.openFromVideo("/media/v.mkv", 3, true);
    EXPECT_TRUE(f.box.fromVideo());
    EXPECT_TRUE(f.source.probedPath.empty());
    EXPECT_FALSE(asked);
    EXPECT_EQ(f.source.openedTrack, 3);
    EXPECT_EQ(std::filesystem::path(f.source.openedIndexFile), f.settings.indexDir / "v_3.ffindex");
    f.source.opened(2, 1000, false);
    ASSERT_EQ(f.source.reads.size(), 1u);
    f.source.answer(4, 2);
    ASSERT_EQ(f.box.state(), AudioBox::State::Ready);
    const auto file = f.box.cacheFile();
    EXPECT_EQ(file.filename(), "v_track3_2ch_0.w64");
    f.box.close();
    EXPECT_EQ(f.source.cancels, 0);

    f.box.openFromVideo("/media/v.mkv", 3, false);
    f.source.opened(2, 1000, false);
    EXPECT_TRUE(f.box.cacheReused());
    EXPECT_TRUE(f.source.reads.empty());
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    f.box.openFromVideo("/media/v.mkv", 3, true);
    f.source.opened(2, 1000, false);
    EXPECT_FALSE(f.box.cacheReused());
    EXPECT_EQ(f.source.reads.size(), 1u);
    f.box.close();
    EXPECT_EQ(f.source.cancels, 1);
}

TEST(AudioBox, TheVideosHandedOverIndexStandsInForItsIndexFile)
{
    BoxFixture f("from-video-handoff");
    f.box.openFromVideo("/media/v.mkv", 3, true, "/tmp/hikari-index-abc123.ffindex");
    EXPECT_EQ(f.source.openedIndexFile, "/tmp/hikari-index-abc123.ffindex");
    f.source.opened(2, 1000, false);
    EXPECT_FALSE(f.box.cacheReused());
    ASSERT_EQ(f.source.reads.size(), 1u);
    f.box.open("/media/v.mkv");
    f.source.probedOneTrack();
    EXPECT_EQ(std::filesystem::path(f.source.openedIndexFile), f.settings.indexDir / "v_1.ffindex");
    f.box.openFromVideo("/media/v.mkv", 3, false);
    EXPECT_EQ(std::filesystem::path(f.source.openedIndexFile), f.settings.indexDir / "v_3.ffindex");
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
    late(std::unexpected(AudioFailure{SourceError::HelperLost, AudioStage::Host, {}}));
    EXPECT_EQ(f.box.audio(), nullptr);
    EXPECT_TRUE(f.logged.empty());
    f.box.open("/media/b.wav");
    f.box.open("/media/c.wav");
    f.source.probedOneTrack();
    f.source.failOpen(SourceError::InvalidInput, AudioStage::Indexer, "x");
    EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
    EXPECT_EQ(f.box.error(), SourceError::InvalidInput);
    EXPECT_EQ(f.box.path(), "/media/c.wav");
}

TEST(AudioBox, BlankAudio)
{
    BoxFixture f("blank");
    f.box.open(AudioBox::kDummyName);
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    EXPECT_TRUE(f.source.probedPath.empty());
    EXPECT_EQ(f.box.audio()->sampleRate(), 44100);
    EXPECT_EQ(f.box.audio()->sampleCount(), 396'900'000);
    EXPECT_EQ(f.box.audio()->sampleCount() * 1000 / 44100, 9'000'000);
    f.box.open("?dummy:23.976000:40000:1280:720:47:163:254:");
    EXPECT_EQ(f.box.state(), AudioBox::State::Closed);
    EXPECT_EQ(f.box.error(), SourceError::Unsupported);
}

TEST(AudioBox, BlankAudioOfAGivenLength)
{
    BoxFixture f("blank");
    const std::string name = AudioBox::dummyName(88200);
    EXPECT_EQ(name, "dummy-audio:silence?sr=44100&bd=16&ch=1&ln=88200");
    EXPECT_TRUE(AudioBox::isDummyName(name));
    EXPECT_FALSE(AudioBox::isDummyName("?dummy:25.000000:50:320:240:47:163:254:"));
    f.box.open(name);
    EXPECT_EQ(f.box.state(), AudioBox::State::Ready);
    EXPECT_TRUE(f.source.probedPath.empty());
    EXPECT_EQ(f.box.audio()->sampleCount(), 88200);
    EXPECT_EQ(f.box.path(), name);
    EXPECT_EQ(AudioBox::dummySamples(AudioBox::kDummyName), AudioBox::kDummySamples);
    EXPECT_EQ(AudioBox::dummySamples("dummy"), AudioBox::kDummySamples);
    EXPECT_EQ(AudioBox::dummySamples("dummy-audio:silence?ln=0"), AudioBox::kDummySamples);
    EXPECT_EQ(AudioBox::dummySamples("dummy-audio:silence?ln=12x"), AudioBox::kDummySamples);
    EXPECT_EQ(AudioBox::dummySamples("dummy-audio:silence?ln=12&sr=44100"), 12);
    EXPECT_EQ(AudioBox::dummyName(0), "dummy-audio:silence?sr=44100&bd=16&ch=1&ln=1");
}
