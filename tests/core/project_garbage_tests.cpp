#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace hikari::core;

namespace {

std::vector<std::byte> bytesOf(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::string text(const std::vector<std::byte> &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

const std::string kInfo = "[Script Info]\nScriptType: v4.00+\n\n";
const std::string kRest = "[V4+ Styles]\nFormat: Name, Fontname, Fontsize\nStyle: Default,Arial,20\n\n[Events]\n"
                          "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                          "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n";
const std::string kGarbage = "[Aegisub Project Garbage]\nAudio File: ep.mkv\nTimecodes File: old.txt\n\n";

std::string edited(const std::string &input, const std::function<void(Document &)> &edit)
{
    auto loaded = loadAss(bytesOf(input));
    edit(loaded.document);
    return text(encodeAss(loaded.document));
}

}

TEST(ProjectGarbage, ReadsAegisubsKeyValueLines)
{
    const auto loaded = loadAss(bytesOf(kInfo + kGarbage + kRest));
    EXPECT_EQ(loaded.document.projectGarbage(u8"Timecodes File"), std::optional<std::u8string>(u8"old.txt"));
    EXPECT_EQ(loaded.document.projectGarbage(u8"Audio File"), std::optional<std::u8string>(u8"ep.mkv"));
    EXPECT_FALSE(loaded.document.projectGarbage(u8"Video File"));
    EXPECT_EQ(text(encodeAss(loaded.document)), kInfo + kGarbage + kRest);
}

TEST(ProjectGarbage, ReplacesAValueInPlace)
{
    EXPECT_EQ(edited(kInfo + kGarbage + kRest,
                     [](Document &d) { EXPECT_TRUE(d.setProjectGarbage(u8"Timecodes File", u8"new.txt")); }),
              kInfo + "[Aegisub Project Garbage]\nAudio File: ep.mkv\nTimecodes File: new.txt\n\n" + kRest);
    EXPECT_EQ(edited(kInfo + kGarbage + kRest,
                     [](Document &d) { EXPECT_TRUE(d.setProjectGarbage(u8"Timecodes File", u8"old.txt")); }),
              kInfo + kGarbage + kRest);
}

TEST(ProjectGarbage, AddsTheSectionAfterScriptInfoAsAegisubWritesIt)
{
    const std::string added = edited(kInfo + kRest, [](Document &d) {
        EXPECT_TRUE(d.setProjectGarbage(u8"Timecodes File", u8"tc/ep.txt"));
    });
    EXPECT_EQ(added, kInfo + "[Aegisub Project Garbage]\nTimecodes File: tc/ep.txt\n\n" + kRest);
    EXPECT_EQ(loadAss(bytesOf(added)).document.projectGarbage(u8"Timecodes File"),
              std::optional<std::u8string>(u8"tc/ep.txt"));
    EXPECT_EQ(edited(kInfo + kRest,
                     [](Document &d) {
                         d.setProjectGarbage(u8"Timecodes File", u8"x.txt");
                         EXPECT_TRUE(d.removeProjectGarbage(u8"Timecodes File"));
                     }),
              kInfo + kRest);
}

TEST(ProjectGarbage, RemovesOnlyItsKey)
{
    EXPECT_EQ(edited(kInfo + kGarbage + kRest,
                     [](Document &d) { EXPECT_TRUE(d.removeProjectGarbage(u8"Timecodes File")); }),
              kInfo + "[Aegisub Project Garbage]\nAudio File: ep.mkv\n\n" + kRest);
    EXPECT_EQ(edited(kInfo + kRest, [](Document &d) { EXPECT_FALSE(d.removeProjectGarbage(u8"Timecodes File")); }),
              kInfo + kRest);
}

TEST(ProjectGarbage, ASectionInsideEmbeddedDataIsData)
{
    const std::string fonts = "[Fonts]\nfontname: a.ttf\n!!!!\n\n";
    const std::string input = kInfo + kRest + "\n" + fonts + kGarbage;
    const auto loaded = loadAss(bytesOf(input));
    EXPECT_FALSE(loaded.document.projectGarbage(u8"Timecodes File"));
    const std::string out = edited(input, [](Document &d) { d.setProjectGarbage(u8"Timecodes File", u8"new.txt"); });
    EXPECT_EQ(out, kInfo + "[Aegisub Project Garbage]\nTimecodes File: new.txt\n\n" + kRest + "\n" + fonts + kGarbage);
}

TEST(ProjectGarbage, RemovesTheLastLineOfTheFile)
{
    EXPECT_EQ(edited(kInfo + kRest + "\n" + kGarbage,
                     [](Document &d) { EXPECT_TRUE(d.removeProjectGarbage(u8"Timecodes File")); }),
              kInfo + kRest + "\n[Aegisub Project Garbage]\nAudio File: ep.mkv\n\n");
    EXPECT_EQ(edited("[Script Info]\nTitle: t\n\n[Aegisub Project Garbage]\nAudio File: a.wav\nTimecodes File: old.txt\n",
                     [](Document &d) { EXPECT_TRUE(d.removeProjectGarbage(u8"Timecodes File")); }),
              "[Script Info]\nTitle: t\n\n[Aegisub Project Garbage]\nAudio File: a.wav\n");
}
