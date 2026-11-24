#include "hikari/core/text_import.h"

#include <gtest/gtest.h>

#include <vector>

using namespace hikari::core;

namespace {

using Lines = std::vector<TextImportLine>;

Lines read(std::u8string_view text, const TextImportOptions &options = {})
{
    const auto lines = importText(text, options);
    EXPECT_TRUE(lines.has_value());
    return lines.value_or(Lines{});
}

}

TEST(TextImport, NamesCommentsAndBlankLinesReadAsAegisubReadsThem)
{
    EXPECT_EQ(read(u8"Ana: Hello there\r\n"
                   u8"\r\n"
                   u8"  Still Ana\r\n"
                   u8"#A note\n"
                   u8"Ben:  Hi\n"
                   u8"Plain line\n"
                   u8"Ben:\n"),
              (Lines{{u8"Ana", u8"Hello there", false},
                     {u8"Ana", u8"Still Ana", false},
                     {u8"", u8"A note", true},
                     {u8"Ben", u8"Hi", false},
                     {u8"Ben", u8"Plain line", false},
                     {u8"", u8"", true}}));
}

TEST(TextImport, OptionsChangeTheNamesCommentsAndBlankLines)
{
    constexpr std::u8string_view text = u8"Ana -- Hello: there\n\n// note\nPlain\n";
    TextImportOptions options;
    options.actorSeparator = u8" -- ";
    options.commentStarter = u8"//";
    options.keepBlankLines = true;
    options.namesCarryOver = false;
    EXPECT_EQ(read(text, options), (Lines{{u8"Ana", u8"Hello: there", false},
                                          {u8"", u8"", true},
                                          {u8"", u8"note", true},
                                          {u8"", u8"Plain", false}}));

    options.actorSeparator.clear();
    options.commentStarter.clear();
    options.keepBlankLines = false;
    EXPECT_EQ(read(text, options), (Lines{{u8"", u8"Ana -- Hello: there", false},
                                          {u8"", u8"// note", false},
                                          {u8"", u8"Plain", false}}));
}

TEST(TextImport, ATimecodesFileAndAFileWithoutLinesAreRefused)
{
    EXPECT_EQ(importText(u8"# timecode format v2\n0\n42\n", {}).error(), TextImportProblem::TimecodesFile);
    EXPECT_EQ(importText(u8"\n \r\n\t\n", {}).error(), TextImportProblem::NoLines);
    EXPECT_EQ(importText(u8"", {}).error(), TextImportProblem::NoLines);
}
