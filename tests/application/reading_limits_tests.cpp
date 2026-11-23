#include "hikari/application/reading_limits.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

std::string saved(const core::Document &d)
{
    const auto b = core::encodeAss(d);
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

constexpr std::string_view kEvents =
    "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n";

std::string withInfo(std::string_view info)
{
    return "[Script Info]\nScriptType: v4.00+\n" + std::string(info) + std::string(kEvents);
}

}

TEST(ReadingLimits, SeverityFollowsAegisubsGridGradient)
{
    const ReadingLimits limits{.cpsWarning = 15, .cpsError = 30};
    EXPECT_EQ(cpsSeverity(limits, 10), 0);
    EXPECT_EQ(cpsSeverity(limits, 15), 0);
    EXPECT_DOUBLE_EQ(cpsSeverity(limits, 16), 2.0 / 16);
    EXPECT_DOUBLE_EQ(cpsSeverity(limits, 22), 8.0 / 16);
    EXPECT_EQ(cpsSeverity(limits, 30), 1);
    EXPECT_EQ(cpsSeverity(limits, 999), 1);
}

TEST(ReadingLimits, AnErrorLevelUnderTheWarningTintsFullyPastTheWarning)
{
    const ReadingLimits limits{.cpsWarning = 20, .cpsError = 10};
    EXPECT_EQ(cpsSeverity(limits, 20), 0);
    EXPECT_EQ(cpsSeverity(limits, 21), 1);
}

TEST(ReadingLimits, TheProfileHoldsTheDefaults)
{
    MemorySettingsStorage storage;
    Settings settings(storage);
    EXPECT_EQ(profileReadingLimits(settings), ReadingLimits{});
    EXPECT_EQ(profileReadingLimits(settings), (ReadingLimits{15, 30, 43, 2}));
    settings.set("grid.cpsWarning", std::int64_t{17});
    settings.set("grid.maxLines", std::int64_t{3});
    EXPECT_EQ(profileReadingLimits(settings), (ReadingLimits{17, 30, 43, 3}));
}

TEST(ReadingLimits, ScriptInfoKeysOverrideTheProfileOneByOne)
{
    const ReadingLimits profile{17, 25, 40, 3};
    const auto plain = load(withInfo(""));
    EXPECT_EQ(readingLimits(plain, profile), profile);
    EXPECT_TRUE(scriptReadingLimits(plain).empty());

    const auto some = load(withInfo("Hikari CPS Warning: 12\nHikari Line Length: 37\n"));
    EXPECT_EQ(readingLimits(some, profile), (ReadingLimits{12, 25, 37, 3}));
    EXPECT_EQ(scriptReadingLimits(some), (ScriptReadingLimits{.cpsWarning = 12, .lineLength = 37}));
}

TEST(ReadingLimits, AValueThatIsNotAPositiveIntegerIsIgnored)
{
    const ReadingLimits profile{17, 25, 40, 3};
    const auto bad = load(withInfo("Hikari CPS Warning: 0\nHikari CPS Error: -4\nHikari Line Length: 4x\n"
                                   "Hikari Max Lines: \n"));
    EXPECT_EQ(readingLimits(bad, profile), profile);
    EXPECT_TRUE(scriptReadingLimits(bad).empty());
}

TEST(ReadingLimits, OverridingWritesAllFourKeysAsOneStepAndUseMySettingsRemovesThem)
{
    EditSession session{load(withInfo("Title: Show\n"))};
    const auto steps = session.historySize();
    ASSERT_TRUE(applyReadingLimits(session, ReadingLimits{12, 20, 37, 3}));
    EXPECT_EQ(session.historySize(), steps + 1);
    const std::string text = saved(session.document());
    EXPECT_NE(text.find("Hikari CPS Warning: 12\n"), std::string::npos) << text;
    EXPECT_NE(text.find("Hikari CPS Error: 20\n"), std::string::npos) << text;
    EXPECT_NE(text.find("Hikari Line Length: 37\n"), std::string::npos) << text;
    EXPECT_NE(text.find("Hikari Max Lines: 3\n"), std::string::npos) << text;

    ASSERT_TRUE(applyReadingLimits(session, ReadingLimits{12, 20, 37, 3}));
    EXPECT_EQ(session.historySize(), steps + 1);

    ASSERT_TRUE(applyReadingLimits(session, std::nullopt));
    EXPECT_EQ(session.historySize(), steps + 2);
    EXPECT_EQ(saved(session.document()).find("Hikari "), std::string::npos);
    EXPECT_TRUE(scriptReadingLimits(session.document()).empty());

    ASSERT_TRUE(applyReadingLimits(session, std::nullopt));
    EXPECT_EQ(session.historySize(), steps + 2);

    ASSERT_TRUE(session.undo());
    EXPECT_EQ(readingLimits(session.document(), ReadingLimits{}), (ReadingLimits{12, 20, 37, 3}));
}

TEST(ReadingLimits, AegisubStyleKeysTheFileHoldsSurviveARoundTrip)
{
    const std::string info = "Title: Show\nScript Updated By: Aegisub 3.5\nAegisub Scroll Position: 4\n"
                             "Aegisub Active Line: 7\nAegisub Video Zoom Percent: 0.5\n";
    EditSession session{load(withInfo(info))};
    ASSERT_TRUE(applyReadingLimits(session, ReadingLimits{12, 20, 37, 3}));
    ASSERT_TRUE(applyReadingLimits(session, std::nullopt));
    EXPECT_EQ(saved(session.document()), saved(load(withInfo(info))));

    ASSERT_TRUE(applyReadingLimits(session, ReadingLimits{12, 20, 37, 3}));
    const std::string text = saved(session.document());
    for (std::string_view key : {"Aegisub Scroll Position: 4\n", "Aegisub Active Line: 7\n",
                                 "Aegisub Video Zoom Percent: 0.5\n", "Script Updated By: Aegisub 3.5\n"})
        EXPECT_NE(text.find(key), std::string::npos) << key;
    EXPECT_EQ(readingLimits(load(text), ReadingLimits{}), (ReadingLimits{12, 20, 37, 3}));
}
