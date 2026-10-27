// KP1 (#286): the generated Lines the Grid folds, and their runs.

#include "hikari/application/generated_runs.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

constexpr std::string_view kSong =
    "[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Comment: 0,0:00:00.00,0:00:00.00,Romaji,,0,0,0,template syl,{\\pos($x,$y)}\n" // 0
    "Comment: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,karaoke,{\\k50}ha{\\k50}ru\n"  // 1
    "Dialogue: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,fx,ha\n"                      // 2
    "Dialogue: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,fx,ru\n"                      // 3
    "Comment: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,fx,a commented one\n"          // 4
    "Dialogue: 0,0:00:04.00,0:00:06.00,Romaji,,0,0,0,,{\\k50}ka{\\k50}ze\n"        // 5
    "Dialogue: 0,0:00:04.00,0:00:06.00,Romaji,,0,0,0,Effector [fx],ka\n"           // 6
    "Dialogue: 0,0:00:07.00,0:00:09.00,Romaji,,0,0,0,FX,not the templater's\n"     // 7
    "Dialogue: 0,0:00:07.00,0:00:09.00,Romaji,,0,0,0,fx2,nor this\n"               // 8
    "Dialogue: 0,0:00:07.00,0:00:09.00,Romaji,,0,0,0,fx,last\n";                   // 9

} // namespace

TEST(GeneratedRuns, OnlyTheTwoEffectsMarkAGeneratedLine)
{
    const core::Document doc = load(kSong);
    const auto lines = doc.lines();
    ASSERT_EQ(lines.size(), 10u);
    std::vector<bool> generated;
    for (const auto *line : lines)
        generated.push_back(isFoldableGeneratedLine(*line));
    EXPECT_EQ(generated, (std::vector<bool>{false, false, true, true, true, false, true, false, false, true}));
}

TEST(GeneratedRuns, ConsecutiveGeneratedLinesMakeOneRun)
{
    const core::Document doc = load(kSong);
    EXPECT_EQ(generatedRuns(doc.lines()), (std::vector<GeneratedRun>{{2, 3}, {6, 1}, {9, 1}}));
    EXPECT_TRUE(generatedRuns(core::Document{}.lines()).empty());
}
