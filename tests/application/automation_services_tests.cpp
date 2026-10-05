// L3: host service routing through platform ports, and legacy decode_path
// (HikariSub/Automation.cpp decode_path at 20d647c4).

#include "hikari/application/audio_spectrum.h"
#include "hikari/application/automation_services.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using namespace hikari::application;

namespace {

AutomationPathContext linuxContext()
{
    return {"/media/show/ep1.wav", "/subs/show/ep1.ass", "/media/show/ep1.mkv", "/opt/hikari/Automation",
            "/opt/hikari/Dictionary", false};
}

TEST(DecodePath, PrefixesBecomeTheLegacyDirectories)
{
    const auto c = linuxContext();
    EXPECT_EQ(decodeAutomationPath("?audio/x.wav", c), "/media/show/x.wav");
    EXPECT_EQ(decodeAutomationPath("?data/a.lua", c), "/opt/hikari/Automation/a.lua");
    EXPECT_EQ(decodeAutomationPath("?dictionary/en.dic", c), "/opt/hikari/Dictionary/en.dic");
    EXPECT_EQ(decodeAutomationPath("?local/cfg", c), "/opt/hikari/Automation/cfg");
    EXPECT_EQ(decodeAutomationPath("?script/out.ass", c), "/subs/show/out.ass");
    EXPECT_EQ(decodeAutomationPath("?temp/t", c), "/opt/hikari/Automation/temp/t");
    EXPECT_EQ(decodeAutomationPath("?user/config", c), "/opt/hikari/Automation/config");
    EXPECT_EQ(decodeAutomationPath("?video", c), "/media/show");
}

TEST(DecodePath, LegacyQuirksArePinned)
{
    const auto c = linuxContext();
    // Only the 2nd and 5th characters are compared.
    EXPECT_EQ(decodeAutomationPath("?axxix/f", c), "/media/show/f");
    // Unknown prefixes, short paths and plain paths stay; backslashes become '/'.
    EXPECT_EQ(decodeAutomationPath("?other/f", c), "?other/f");
    EXPECT_EQ(decodeAutomationPath("?a", c), "?a");
    EXPECT_EQ(decodeAutomationPath("C:\\x\\y", c), "C:/x/y");
    // Without a Document file the directory is empty.
    AutomationPathContext none = c;
    none.subtitlePath.clear();
    EXPECT_EQ(decodeAutomationPath("?script/out.ass", none), "/out.ass");
}

TEST(DecodePath, WindowsSeparators)
{
    const AutomationPathContext c{"D:\\media\\ep1.wav", "D:\\subs\\ep1.ass", "D:\\media\\ep1.mkv",
                                  "C:\\Hikari\\Automation", "C:\\Hikari\\Dictionary", true};
    EXPECT_EQ(decodeAutomationPath("?script/out.ass", c), "D:\\subs\\out.ass");
    EXPECT_EQ(decodeAutomationPath("?temp/x", c), "C:\\Hikari\\Automation\\temp\\x");
}

struct FakeMedia : MacroMediaPort {
    bool video = true;
    std::optional<std::function<void(std::optional<Frame>)>> pendingFrame;
    std::optional<std::int64_t> frameFromMs(std::int64_t ms) const override
    {
        return video ? std::optional<std::int64_t>(ms / 40) : std::nullopt;
    }
    std::optional<std::int64_t> msFromFrame(std::int64_t f) const override
    {
        return video ? std::optional<std::int64_t>(f * 40) : std::nullopt;
    }
    std::optional<VideoSize> videoSize() const override
    {
        return video ? std::optional(VideoSize{640, 360, 16, 9}) : std::nullopt;
    }
    std::optional<std::vector<std::int64_t>> keyframes() const override
    {
        return video ? std::optional(std::vector<std::int64_t>{0, 24}) : std::nullopt;
    }
    void frame(std::int64_t, bool, std::function<void(std::optional<Frame>)> reply) override
    {
        pendingFrame = std::move(reply);
    }
    std::optional<std::pair<std::int64_t, std::int64_t>> audioSelection() const override { return std::nullopt; }
    std::optional<Project> project() const override { return Project{12, "a.wav", "v.mkv", ""}; }
    std::optional<std::string> fileName() const override { return "ep1.ass"; }
    std::optional<const DisplayAudio *> audio;
    std::optional<const DisplayAudio *> peakAudio() const override { return audio; }
};

struct FakePicker : FilePickerPort {
    FilePickerRequest last;
    std::function<void(std::optional<std::vector<std::string>>)> reply;
    int withdrawn = 0;
    void pick(const FilePickerRequest &r, std::function<void(std::optional<std::vector<std::string>>)> f) override
    {
        last = r;
        reply = std::move(f);
    }
    void withdraw() override { ++withdrawn; }
};

HostServiceRequest request(HostService s, std::vector<std::int64_t> i = {}, std::vector<std::string> str = {})
{
    HostServiceRequest r;
    r.service = s;
    r.integers = std::move(i);
    r.strings = std::move(str);
    r.script = "/scripts/s.lua";
    r.run = 7;
    return r;
}

struct RouterTest : ::testing::Test {
    AutomationServiceRouter router;
    std::vector<HostServiceReply> replies;
    AutomationServiceRouter::Reply collect()
    {
        return [this](HostServiceReply r) { replies.push_back(std::move(r)); };
    }
};

TEST_F(RouterTest, UnattachedPortsAnswerUnavailable)
{
    for (auto s : {HostService::FrameFromMs, HostService::VideoSize, HostService::Frame, HostService::ClipboardGet,
                   HostService::TextExtents, HostService::OpenFiles, HostService::EditorCursor,
                   HostService::StatusText})
        router.handle(request(s), collect());
    ASSERT_EQ(replies.size(), 8u);
    for (const auto &r : replies)
        EXPECT_EQ(r.status, HostServiceReply::Status::Unavailable);
}

// O5: aegisub.gettext goes to the application's lookup; without one it is
// Unavailable and the helper keeps the source.
TEST_F(RouterTest, GettextAnswersThroughTheTranslation)
{
    router.handle(request(HostService::Gettext, {}, {"Search bar"}), collect());
    router.setTranslation([](const std::string &s) { return s == "Search bar" ? std::string("Pasek szukania") : s; });
    router.handle(request(HostService::Gettext, {}, {"Search bar"}), collect());
    router.handle(request(HostService::Gettext, {}, {"no such key"}), collect());
    router.handle(request(HostService::Gettext), collect()); // no string: the empty source
    ASSERT_EQ(replies.size(), 4u);
    EXPECT_EQ(replies[0].status, HostServiceReply::Status::Unavailable);
    EXPECT_EQ(replies[1].strings, std::vector<std::string>{"Pasek szukania"});
    EXPECT_EQ(replies[2].strings, std::vector<std::string>{"no such key"});
    EXPECT_EQ(replies[3].strings, std::vector<std::string>{""});
}

TEST_F(RouterTest, MediaAnswersInTheirUnits)
{
    FakeMedia media;
    router.setMedia(&media);
    router.handle(request(HostService::FrameFromMs, {1000}), collect());
    router.handle(request(HostService::MsFromFrame, {25}), collect());
    router.handle(request(HostService::VideoSize), collect());
    router.handle(request(HostService::ProjectProperties), collect());
    media.video = false;
    router.handle(request(HostService::Keyframes), collect());
    ASSERT_EQ(replies.size(), 5u);
    EXPECT_EQ(replies[0].integers, std::vector<std::int64_t>{25});
    EXPECT_EQ(replies[1].integers, std::vector<std::int64_t>{1000});
    EXPECT_EQ(replies[2].integers, (std::vector<std::int64_t>{640, 360, 16, 9}));
    EXPECT_EQ(replies[3].integers, std::vector<std::int64_t>{12});
    EXPECT_EQ(replies[3].strings, (std::vector<std::string>{"a.wav", "v.mkv", ""}));
    EXPECT_EQ(replies[4].status, HostServiceReply::Status::Unavailable);
}

TEST_F(RouterTest, FramesAnswerWhenDecoded)
{
    FakeMedia media;
    router.setMedia(&media);
    router.handle(request(HostService::Frame, {3, 1}), collect());
    EXPECT_TRUE(replies.empty());
    ASSERT_TRUE(media.pendingFrame);
    (*media.pendingFrame)(MacroMediaPort::Frame{1, 1, {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}});
    ASSERT_EQ(replies.size(), 1u);
    EXPECT_EQ(replies[0].integers, (std::vector<std::int64_t>{1, 1}));
    EXPECT_EQ(replies[0].pixels.size(), 4u);
}

TEST_F(RouterTest, OnePickerAtATimeAndAWithdrawnAnswerIsDropped)
{
    FakePicker picker;
    router.setFilePicker(&picker);
    router.handle(request(HostService::OpenFiles, {1, 0}, {"Pick", "/d", "f.txt", "Text|*.txt"}), collect());
    EXPECT_EQ(picker.last.mode, FilePickerRequest::Mode::OpenMultiple);
    EXPECT_FALSE(picker.last.mustExist);
    EXPECT_EQ(picker.last.dir, "/d");
    EXPECT_EQ(picker.last.file, "f.txt");
    EXPECT_EQ(picker.last.script, "/scripts/s.lua");
    router.handle(request(HostService::SaveFile, {1}, {"Save", "", "", ""}), collect());
    ASSERT_EQ(replies.size(), 1u); // the second picker is refused while one is open
    EXPECT_EQ(replies[0].status, HostServiceReply::Status::Unavailable);
    router.withdraw();
    EXPECT_EQ(picker.withdrawn, 1);
    picker.reply(std::vector<std::string>{"/d/late.txt"});
    EXPECT_EQ(replies.size(), 1u);

    router.handle(request(HostService::SaveFile, {0}, {"Save", "", "", ""}), collect());
    EXPECT_EQ(picker.last.mode, FilePickerRequest::Mode::Save);
    EXPECT_FALSE(picker.last.promptOverwrite);
    picker.reply(std::nullopt); // cancelled: the legacy nil
    ASSERT_EQ(replies.size(), 2u);
    EXPECT_EQ(replies[1].status, HostServiceReply::Status::Unavailable);
}

TEST_F(RouterTest, DecodePathUsesTheCurrentContext)
{
    router.setPathContext(linuxContext);
    router.handle(request(HostService::DecodePath, {}, {"?user/x"}), collect());
    ASSERT_EQ(replies.size(), 1u);
    EXPECT_EQ(replies[0].strings, std::vector<std::string>{"/opt/hikari/Automation/x"});
}

// L6: aegisub.get_frequency_peaks over legacy AudioSpectrum::CreateRange
// (HikariSub/AudioSpectrum.cpp and GFFT at 20d647c4).

// Mono audio at 48 kHz: `level` for the first `levelSamples` samples, then silence.
std::unique_ptr<DisplayAudio> levelAudio(std::int64_t samples, std::int16_t level, std::int64_t levelSamples)
{
    auto audio = std::make_unique<DisplayAudio>(48000, samples);
    std::vector<std::int16_t> data(static_cast<std::size_t>(samples), 0);
    std::fill_n(data.begin(), levelSamples, level);
    EXPECT_TRUE(audio->appendFrames(data.data(), samples, 1));
    audio->finish();
    return audio;
}

TEST(FrequencyPeaks, ALineIsTheMagnitudeOfLegacysFft)
{
    // Against a plain DFT: bins 0..1023 of 2048 samples.
    std::vector<std::int16_t> samples(kSpectrumDoubleLength);
    std::uint32_t seed = 12345;
    for (auto &s : samples) {
        seed = seed * 1664525u + 1013904223u;
        s = static_cast<std::int16_t>(static_cast<int>(seed >> 16) - 32768);
    }
    const SpectrumLine line = legacySpectrumLine(samples.data());
    const double pi = std::acos(-1.0);
    double largest = 0, worst = 0;
    for (std::size_t k = 0; k < kSpectrumLineLength; k += 37) {
        double re = 0, im = 0;
        for (std::size_t n = 0; n < kSpectrumDoubleLength; ++n) {
            const double a = 2 * pi * static_cast<double>(k * n % kSpectrumDoubleLength) / kSpectrumDoubleLength;
            re += samples[n] * std::cos(a);
            im -= samples[n] * std::sin(a);
        }
        const double magnitude = std::hypot(re, im);
        largest = std::max(largest, magnitude);
        worst = std::max(worst, std::abs(magnitude - line[k]));
    }
    EXPECT_LT(worst, largest * 1e-3); // float, with legacy's twiddle recurrence

    // A constant is exact: everything in the first bin.
    std::fill(samples.begin(), samples.end(), std::int16_t{50});
    const SpectrumLine flat = legacySpectrumLine(samples.data());
    EXPECT_EQ(flat[0], 2048.f * 50);
    for (std::size_t k = 1; k < kSpectrumLineLength; ++k)
        ASSERT_EQ(flat[k], 0.f) << k;
}

TEST(FrequencyPeaks, IntensitiesPerLineAndPeakRunsFollowCreateRange)
{
    // Ten lines (2048 samples each) at level 50: the first bin's intensity
    // is 100 * (2048 * 50 * 16) / (32768 * 100) = 50.
    const auto audio = levelAudio(48000, 50, 10 * 2048);
    // 900 ms: lines 0 (0 ms) to 21 (43200 / 2048), each at line * 2048000 / 48000 ms;
    // bands 0 / (48000 / 2048) = 0 to 100 / 23 = 4, the strongest of them.
    auto peaks = legacyFrequencyPeaks(*audio, 0, 900, 0, 100, 0);
    EXPECT_EQ(peaks.times, (std::vector<int>{0, 42, 85, 128, 170, 213, 256, 298, 341, 384, 426, 469, 512, 554, 597,
                                             640, 682, 725, 768, 810, 853, 896}));
    std::vector<int> expected(22, 0);
    std::fill_n(expected.begin(), 10, 50);
    EXPECT_EQ(peaks.intensities, expected);
    // From the line holding the start time.
    peaks = legacyFrequencyPeaks(*audio, 400, 500, 0, 0, 0);
    EXPECT_EQ(peaks.times, (std::vector<int>{384, 426, 469}));
    EXPECT_EQ(peaks.intensities, (std::vector<int>{50, 0, 0}));
    // An end band below the start band is the start band (band 4: nothing).
    peaks = legacyFrequencyPeaks(*audio, 0, 100, 100, 0, 0);
    EXPECT_EQ(peaks.intensities, (std::vector<int>{0, 0, 0}));

    // With a peek: the time where each run of lines reaching it was
    // strongest (its first line at equal intensities), once the run ends.
    peaks = legacyFrequencyPeaks(*audio, 0, 900, 0, 0, 40);
    EXPECT_EQ(peaks.times, std::vector<int>{0});
    EXPECT_TRUE(peaks.intensities.empty());
    EXPECT_TRUE(legacyFrequencyPeaks(*audio, 0, 900, 0, 0, 60).times.empty());
    // A run still going at the last line is never reported (legacy).
    EXPECT_TRUE(legacyFrequencyPeaks(*audio, 0, 300, 0, 0, 40).times.empty());
}

TEST_F(RouterTest, FrequencyPeaksReadTheAudioBox)
{
    FakeMedia media;
    router.setMedia(&media);
    router.handle(request(HostService::FrequencyPeaks, {0, 100, 0, 0, 0}), collect()); // no audio
    media.audio = nullptr;                                                           // a box without audio yet
    router.handle(request(HostService::FrequencyPeaks, {0, 100, 0, 0, 0}), collect());
    const auto audio = levelAudio(48000, 50, 2048);
    media.audio = audio.get();
    router.handle(request(HostService::FrequencyPeaks, {0, 100, 0, 0, 0}), collect());
    router.handle(request(HostService::FrequencyPeaks, {0, 100, 0, 0, 40}), collect());
    router.handle(request(HostService::FrequencyPeaks, {-5, 100, 0, 0, 0}), collect()); // the helper raises
    router.handle(request(HostService::FrequencyPeaks, {100, 100, 0, 0, 0}), collect()); // the helper answers {}
    ASSERT_EQ(replies.size(), 6u);
    EXPECT_EQ(replies[0].status, HostServiceReply::Status::Unavailable);
    EXPECT_EQ(replies[1].integers, std::vector<std::int64_t>{1});
    EXPECT_EQ(replies[2].integers, (std::vector<std::int64_t>{0, 0, 42, 85}));
    EXPECT_EQ(replies[2].numbers, (std::vector<double>{50, 0, 0}));
    EXPECT_EQ(replies[3].integers, (std::vector<std::int64_t>{0, 0}));
    EXPECT_TRUE(replies[3].numbers.empty());
    EXPECT_EQ(replies[4].integers, std::vector<std::int64_t>{0});
    EXPECT_EQ(replies[5].integers, std::vector<std::int64_t>{0});
}

} // namespace
