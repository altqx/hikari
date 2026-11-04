#include "hikari/application/word_diff.h"

#include <gtest/gtest.h>

#include <string>

using namespace hikari::application;

namespace {

std::u16string render(std::u16string_view before, std::u16string_view after)
{
    std::u16string out;
    for (const DiffPart &part : wordDiff(before, after)) {
        switch (part.kind) {
        case DiffPart::Kind::Same:
            out += after.substr(part.start, part.length);
            break;
        case DiffPart::Kind::Removed:
            out += u"[-" + std::u16string(before.substr(part.start, part.length)) + u"]";
            break;
        case DiffPart::Kind::Added:
            out += u"[+" + std::u16string(after.substr(part.start, part.length)) + u"]";
            break;
        }
    }
    return out;
}

}

TEST(WordDiff, MarksTheWordsThatChanged)
{
    EXPECT_EQ(render(u"I can't go there", u"I cannot go there"), u"I [-can't][+cannot] go there");
}

TEST(WordDiff, KeepsOverrideTagsApartFromWords)
{
    EXPECT_EQ(render(u"{\\i1}hello", u"{\\i1}hello world"), u"{\\i1}hello[+ world]");
}

TEST(WordDiff, EqualTextsAreOneSamePart)
{
    const auto parts = wordDiff(u"same text", u"same text");
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0].kind, DiffPart::Kind::Same);
    EXPECT_EQ(parts[0].length, 9u);
}

TEST(WordDiff, EmptySidesAreWholeInsertionsOrRemovals)
{
    EXPECT_EQ(render(u"", u"new"), u"[+new]");
    EXPECT_EQ(render(u"gone", u""), u"[-gone]");
}

TEST(WordDiff, NonAsciiTextIsOneWordPerRun)
{
    EXPECT_EQ(render(u"こんにちは 世界", u"こんばんは 世界"), u"[-こんにちは][+こんばんは] 世界");
}
