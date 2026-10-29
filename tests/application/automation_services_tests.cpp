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
    EXPECT_EQ(decodeAutomationPath("?axxix/f", c), "/media/show/f");
    EXPECT_EQ(decodeAutomationPath("?other/f", c), "?other/f");
    EXPECT_EQ(decodeAutomationPath("?a", c), "?a");
    EXPECT_EQ(decodeAutomationPath("C:\\x\\y", c), "C:/x/y");
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

TEST(DecodePath, InstalledCopyWritesToTheUsersFolder)
{
    auto c = linuxContext();
    c.userAutomationDir = "/home/a/.local/share/HikariSub/HikariSub/Automation";
    EXPECT_EQ(decodeAutomationPath("?data/a.lua", c), "/opt/hikari/Automation/a.lua");
    EXPECT_EQ(decodeAutomationPath("?user/config", c), "/home/a/.local/share/HikariSub/HikariSub/Automation/config");
    EXPECT_EQ(decodeAutomationPath("?local/cfg", c), "/home/a/.local/share/HikariSub/HikariSub/Automation/cfg");
    EXPECT_EQ(decodeAutomationPath("?temp/t", c), "/home/a/.local/share/HikariSub/HikariSub/Automation/temp/t");
    EXPECT_EQ(decodeAutomationPath("?dictionary/en.dic", c), "/opt/hikari/Dictionary/en.dic");

    AutomationPathContext w{"", "", "", "C:\\Program Files\\HikariSub\\Automation", "C:\\Program Files\\HikariSub\\Dictionary", true};
    w.userAutomationDir = "C:\\Users\\a\\AppData\\Roaming\\HikariSub\\HikariSub\\Automation";
    EXPECT_EQ(decodeAutomationPath("?temp/x", w), "C:\\Users\\a\\AppData\\Roaming\\HikariSub\\HikariSub\\Automation\\temp\\x");
    EXPECT_EQ(decodeAutomationPath("?data/x", w), "C:\\Program Files\\HikariSub\\Automation\\x");
}

TEST(AutomationFolders, PortableCopyHasTheShippedFolderOnly)
{
    const std::vector<std::string> autoload{"/apps/hikari/Automation/automation/Autoload"};
    const std::vector<std::string> include{"/apps/hikari/Automation/automation/Include"};
    for (const std::string &user : {std::string(), std::string("/apps/hikari/Automation")}) {
        const auto f = automationFolders("/apps/hikari/Automation", user, false);
        EXPECT_EQ(f.autoload, autoload);
        EXPECT_EQ(f.include, include);
        EXPECT_TRUE(f.made.empty());
    }
    // Windows paths compare case-insensitively.
    const auto w = automationFolders("C:\\Hikari\\Automation", "c:\\hikari\\automation", true);
    EXPECT_EQ(w.autoload.size(), 1u);
    EXPECT_TRUE(w.made.empty());
}

TEST(AutomationFolders, InstalledCopyAddsTheUsersFolders)
{
    const auto f = automationFolders("/opt/hikarisub/Automation", "/home/a/.config/hikarisub", false);
    const std::string u = "/home/a/.config/hikarisub";
    EXPECT_EQ(f.autoload, (std::vector<std::string>{"/opt/hikarisub/Automation/automation/Autoload",
                                                     u + "/automation/Autoload", u + "/automation/autoload"}));
    EXPECT_EQ(f.include, (std::vector<std::string>{u + "/automation/Include", u + "/automation/include",
                                                    "/opt/hikarisub/Automation/automation/Include"}));
    EXPECT_EQ(f.made, (std::vector<std::string>{u + "/log", u + "/temp", u + "/autosave", u + "/automation/autoload",
                                                 u + "/automation/include"}));

    const auto w = automationFolders("C:\\Program Files\\HikariSub\\Automation", "C:\\Users\\a\\Automation\\", true);
    EXPECT_EQ(w.autoload, (std::vector<std::string>{"C:\\Program Files\\HikariSub\\Automation\\automation\\Autoload",
                                                     "C:\\Users\\a\\Automation\\automation\\Autoload"}));
    EXPECT_EQ(w.include, (std::vector<std::string>{"C:\\Users\\a\\Automation\\automation\\Include",
                                                    "C:\\Program Files\\HikariSub\\Automation\\automation\\Include"}));
}

TEST(AutomationFolders, AUserScriptReplacesTheShippedOne)
{
    const std::string s = "/opt/hikarisub/Automation/automation/Autoload";
    const std::string u = "/home/a/Automation/automation/Autoload";
    const std::string d = "/home/a/Automation/automation/autoload";
    const auto chosen = chooseAutoloadScripts({{s, {"a.moon", "b.lua", "gradient-factory.lua", "notes.txt"}},
                                               {u, {"gradient-factory.lua", "mine.LUA"}},
                                               {d, {"Gradient-Factory.lua", "l0.DependencyControl.Toolbox.moon"}}},
                                              false);
    EXPECT_EQ(chosen, (std::vector<std::string>{s + "/a.moon", s + "/b.lua", u + "/gradient-factory.lua",
                                                 u + "/mine.LUA", d + "/Gradient-Factory.lua",
                                                 d + "/l0.DependencyControl.Toolbox.moon"}));
    const auto windows = chooseAutoloadScripts({{"C:\\P\\Autoload", {"gradient-factory.lua", "b.lua"}},
                                                {"C:\\U\\Autoload", {"Gradient-Factory.lua"}}},
                                               true);
    EXPECT_EQ(windows, (std::vector<std::string>{"C:\\U\\Autoload\\Gradient-Factory.lua", "C:\\P\\Autoload\\b.lua"}));
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

TEST_F(RouterTest, GettextAnswersThroughTheTranslation)
{
    router.handle(request(HostService::Gettext, {}, {"Search bar"}), collect());
    router.setTranslation([](const std::string &s) { return s == "Search bar" ? std::string("Pasek szukania") : s; });
    router.handle(request(HostService::Gettext, {}, {"Search bar"}), collect());
    router.handle(request(HostService::Gettext, {}, {"no such key"}), collect());
    router.handle(request(HostService::Gettext), collect());
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
    ASSERT_EQ(replies.size(), 1u);
    EXPECT_EQ(replies[0].status, HostServiceReply::Status::Unavailable);
    router.withdraw();
    EXPECT_EQ(picker.withdrawn, 1);
    picker.reply(std::vector<std::string>{"/d/late.txt"});
    EXPECT_EQ(replies.size(), 1u);

    router.handle(request(HostService::SaveFile, {0}, {"Save", "", "", ""}), collect());
    EXPECT_EQ(picker.last.mode, FilePickerRequest::Mode::Save);
    EXPECT_FALSE(picker.last.promptOverwrite);
    picker.reply(std::nullopt);
    ASSERT_EQ(replies.size(), 2u);
    EXPECT_EQ(replies[1].status, HostServiceReply::Status::Unavailable);
}

struct FakeResolution : ScriptResolutionPort {
    ScriptResolutionRequest last;
    std::function<void(std::optional<Resolution>)> reply;
    int withdrawn = 0;
    void resolve(const ScriptResolutionRequest &r, std::function<void(std::optional<Resolution>)> f) override
    {
        last = r;
        reply = std::move(f);
    }
    void withdraw() override { ++withdrawn; }
};

TEST_F(RouterTest, TheScriptResolutionQuestionGoesToItsPort)
{
    router.handle(request(HostService::ScriptResolution, {0, 480}), collect());
    ASSERT_EQ(replies.size(), 1u);
    EXPECT_EQ(replies[0].status, HostServiceReply::Status::Unavailable);
    FakeResolution port;
    router.setScriptResolution(&port);
    router.handle(request(HostService::ScriptResolution, {0, 480}), collect());
    EXPECT_EQ(port.last.script, "/scripts/s.lua");
    EXPECT_EQ(port.last.written, (Resolution{0, 480}));
    EXPECT_EQ(replies.size(), 1u);
    port.reply(Resolution{1920, 1080});
    ASSERT_EQ(replies.size(), 2u);
    EXPECT_EQ(replies[1].integers, (std::vector<std::int64_t>{1920, 1080}));
    router.handle(request(HostService::ScriptResolution, {0, 0}), collect());
    port.reply(std::nullopt);
    ASSERT_EQ(replies.size(), 3u);
    EXPECT_EQ(replies[2].status, HostServiceReply::Status::Unavailable);
    router.handle(request(HostService::ScriptResolution, {0, 0}), collect());
    router.withdraw();
    EXPECT_EQ(port.withdrawn, 1);
    port.reply(Resolution{640, 360});
    EXPECT_EQ(replies.size(), 3u);
}

TEST_F(RouterTest, DecodePathUsesTheCurrentContext)
{
    router.setPathContext(linuxContext);
    router.handle(request(HostService::DecodePath, {}, {"?user/x"}), collect());
    ASSERT_EQ(replies.size(), 1u);
    EXPECT_EQ(replies[0].strings, std::vector<std::string>{"/opt/hikari/Automation/x"});
}

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
    EXPECT_LT(worst, largest * 1e-3);

    std::fill(samples.begin(), samples.end(), std::int16_t{50});
    const SpectrumLine flat = legacySpectrumLine(samples.data());
    EXPECT_EQ(flat[0], 2048.f * 50);
    for (std::size_t k = 1; k < kSpectrumLineLength; ++k)
        ASSERT_EQ(flat[k], 0.f) << k;
}

TEST(FrequencyPeaks, IntensitiesPerLineAndPeakRunsFollowCreateRange)
{
    const auto audio = levelAudio(48000, 50, 10 * 2048);
    auto peaks = legacyFrequencyPeaks(*audio, 0, 900, 0, 100, 0);
    EXPECT_EQ(peaks.times, (std::vector<int>{0, 42, 85, 128, 170, 213, 256, 298, 341, 384, 426, 469, 512, 554, 597,
                                             640, 682, 725, 768, 810, 853, 896}));
    std::vector<int> expected(22, 0);
    std::fill_n(expected.begin(), 10, 50);
    EXPECT_EQ(peaks.intensities, expected);
    peaks = legacyFrequencyPeaks(*audio, 400, 500, 0, 0, 0);
    EXPECT_EQ(peaks.times, (std::vector<int>{384, 426, 469}));
    EXPECT_EQ(peaks.intensities, (std::vector<int>{50, 0, 0}));
    peaks = legacyFrequencyPeaks(*audio, 0, 100, 100, 0, 0);
    EXPECT_EQ(peaks.intensities, (std::vector<int>{0, 0, 0}));

    peaks = legacyFrequencyPeaks(*audio, 0, 900, 0, 0, 40);
    EXPECT_EQ(peaks.times, std::vector<int>{0});
    EXPECT_TRUE(peaks.intensities.empty());
    EXPECT_TRUE(legacyFrequencyPeaks(*audio, 0, 900, 0, 0, 60).times.empty());
    EXPECT_TRUE(legacyFrequencyPeaks(*audio, 0, 300, 0, 0, 40).times.empty());
}

TEST_F(RouterTest, FrequencyPeaksReadTheAudioBox)
{
    FakeMedia media;
    router.setMedia(&media);
    router.handle(request(HostService::FrequencyPeaks, {0, 100, 0, 0, 0}), collect());
    media.audio = nullptr;
    router.handle(request(HostService::FrequencyPeaks, {0, 100, 0, 0, 0}), collect());
    const auto audio = levelAudio(48000, 50, 2048);
    media.audio = audio.get();
    router.handle(request(HostService::FrequencyPeaks, {0, 100, 0, 0, 0}), collect());
    router.handle(request(HostService::FrequencyPeaks, {0, 100, 0, 0, 40}), collect());
    router.handle(request(HostService::FrequencyPeaks, {-5, 100, 0, 0, 0}), collect());
    router.handle(request(HostService::FrequencyPeaks, {100, 100, 0, 0, 0}), collect());
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

}
