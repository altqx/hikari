// L3: host service routing through platform ports, and legacy decode_path
// (HikariSub/Automation.cpp decode_path at 20d647c4).

#include "hikari/application/automation_services.h"

#include <gtest/gtest.h>

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

} // namespace
