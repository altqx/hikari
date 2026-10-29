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
    "Comment: 0,0:00:00.00,0:00:00.00,Romaji,,0,0,0,template syl,{\\pos($x,$y)}\n"
    "Comment: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,karaoke,{\\k50}ha{\\k50}ru\n"
    "Dialogue: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,fx,ha\n"
    "Dialogue: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,fx,ru\n"
    "Comment: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,fx,a commented one\n"
    "Dialogue: 0,0:00:04.00,0:00:06.00,Romaji,,0,0,0,,{\\k50}ka{\\k50}ze\n"
    "Dialogue: 0,0:00:04.00,0:00:06.00,Romaji,,0,0,0,Effector [fx],ka\n"
    "Dialogue: 0,0:00:07.00,0:00:09.00,Romaji,,0,0,0,FX,not the templater's\n"
    "Dialogue: 0,0:00:07.00,0:00:09.00,Romaji,,0,0,0,fx2,nor this\n"
    "Dialogue: 0,0:00:07.00,0:00:09.00,Romaji,,0,0,0,fx,last\n";

}

TEST(GeneratedRuns, OnlyTheTwoEffectsMarkAGeneratedLine)
{
    const core::Document doc = load(kSong);
    const auto lines = doc.lines();
    ASSERT_EQ(lines.size(), 10u);
    std::vector<bool> generated;
    for (const auto *line : lines)
        generated.push_back(isFoldableGeneratedLine(*line));
    EXPECT_EQ(generated, (std::vector<bool>{false, false, true, true, false, false, true, false, false, true}));
}

TEST(GeneratedRuns, ConsecutiveGeneratedLinesMakeOneRun)
{
    const core::Document doc = load(kSong);
    EXPECT_EQ(generatedRuns(doc.lines()), (std::vector<GeneratedRun>{{2, 2}, {6, 1}, {9, 1}}));
    EXPECT_TRUE(generatedRuns(core::Document{}.lines()).empty());
}
