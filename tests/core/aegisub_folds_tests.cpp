#include "hikari/core/aegisub_folds.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace hikari::core;

namespace {

std::vector<std::byte> bytesOf(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

Document fixture(const std::string &name)
{
    std::ifstream in(std::string(HIKARI_EXTRADATA_FIXTURES) + "/" + name, std::ios::binary);
    return loadAss(bytesOf(std::string(std::istreambuf_iterator<char>(in), {}))).document;
}

// One Line per value; an empty value is a Line without a fold entry.
Document withFolds(const std::vector<std::string> &values)
{
    std::string events;
    std::string data;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const std::string id = std::to_string(i);
        events += "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,," +
                  (values[i].empty() ? std::string() : "{=" + id + "}") + "line " + id + "\n";
        if (!values[i].empty())
            data += "Data: " + id + ",_aegi_folddata,e" + values[i] + "\n";
    }
    return loadAss(bytesOf("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" +
                           events + "\n[Aegisub Extradata]\n" + data))
        .document;
}

std::vector<LineId> ids(const Document &document)
{
    std::vector<LineId> out;
    for (const auto *line : document.lines())
        out.push_back(line->id);
    return out;
}

}

TEST(AegisubFolds, SingleNestedAndAdjacentFoldsFromAegisub)
{
    const auto document = fixture("folds.aegisub.ass");
    const auto l = ids(document);
    const auto found = aegisubFolds(document);
    EXPECT_EQ(found.folds, (std::vector<AegisubFold>{{l[0], l[4], false},
                                                     {l[1], l[3], true},
                                                     {l[5], l[6], false},
                                                     {l[7], l[8], false}}));
    EXPECT_TRUE(found.dropped.empty());
}

TEST(AegisubFolds, ASingleFoldTakesItsOpenersCollapsedState)
{
    const auto document = withFolds({"", "0;1;7", "", "1;0;7", ""});
    const auto l = ids(document);
    EXPECT_EQ(aegisubFolds(document).folds, (std::vector<AegisubFold>{{l[1], l[3], true}}));
}

TEST(AegisubFolds, UnpairedDelimitersAreDropped)
{
    const auto document = withFolds({"1;0;1", "0;0;2", "", "0;0;3"});
    const auto l = ids(document);
    const auto found = aegisubFolds(document);
    EXPECT_TRUE(found.folds.empty());
    EXPECT_EQ(found.dropped, (std::vector<LineId>{l[0], l[1], l[3]}));
}

TEST(AegisubFolds, AnEnderPastAnInnerOpenerDropsTheInnerFold)
{
    const auto document = withFolds({"0;0;1", "0;0;2", "1;0;1", "1;0;2"});
    const auto l = ids(document);
    const auto found = aegisubFolds(document);
    EXPECT_EQ(found.folds, (std::vector<AegisubFold>{{l[0], l[2], false}}));
    EXPECT_EQ(found.dropped, (std::vector<LineId>{l[1], l[3]}));
}

TEST(AegisubFolds, AnIdUsedAgainAfterItsFoldStartsANewFold)
{
    const auto document = withFolds({"0;0;1", "1;0;1", "0;1;1", "1;1;1"});
    const auto l = ids(document);
    EXPECT_EQ(aegisubFolds(document).folds,
              (std::vector<AegisubFold>{{l[0], l[1], false}, {l[2], l[3], true}}));
}

TEST(AegisubFolds, MalformedValuesAreNotDelimiters)
{
    const auto document = withFolds({"0;0;1", "0;x;2", "0;0", "1;0;1;", "1;0;1"});
    const auto l = ids(document);
    const auto found = aegisubFolds(document);
    EXPECT_EQ(found.folds, (std::vector<AegisubFold>{{l[0], l[4], false}}));
    EXPECT_TRUE(found.dropped.empty());
}
