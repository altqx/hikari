#include "hikari/core/spelling.h"

#include <gtest/gtest.h>

#include <set>

using namespace hikari::core;
using namespace hikari::core::legacy;

namespace {

bool asciiLetter(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}
bool asciiDigit(char16_t c)
{
    return c >= u'0' && c <= u'9';
}

std::vector<WordSegment> asciiWords(std::u16string_view text)
{
    std::vector<WordSegment> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (asciiLetter(text[i]) || asciiDigit(text[i])) {
            WordSegment s{i, 0, false, false};
            bool letters = false;
            while (i < text.size() && (asciiLetter(text[i]) || asciiDigit(text[i]))) {
                letters = letters || asciiLetter(text[i]);
                ++i;
            }
            s.length = i - s.start;
            s.letters = letters;
            s.number = !letters;
            out.push_back(s);
        } else {
            out.push_back({i, 1, false, false});
            ++i;
        }
    }
    return out;
}

const WordSegmenter segment = asciiWords;

WordCheck dictionary(std::set<std::u16string> words)
{
    return [words = std::move(words)](std::u16string_view w) { return words.contains(std::u16string(w)); };
}

const CaseMapping ascii{
    [](char16_t c) { return c >= u'A' && c <= u'Z'; },
    [](char16_t c) { return c >= u'a' && c <= u'z' ? static_cast<char16_t>(c - 32) : c; },
    [](char16_t c) { return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c + 32) : c; },
};

}

TEST(Spelling, MarksMisspelledWordsWithInclusiveOffsets)
{
    const auto marks = checkTextAndBrackets(u"Hello wrold, 42 times", SubtitleFormat::Ass, segment,
                                            dictionary({u"Hello", u"times"}));
    EXPECT_EQ(marks.errors, (std::vector<int>{6, 10}));
    ASSERT_EQ(marks.misspells.size(), 1u);
    EXPECT_EQ(marks.misspells[0], (Misspell{u"wrold", 6, 10}));
    EXPECT_TRUE(checkTextAndBrackets(u"Hello wrold", SubtitleFormat::Ass, segment, {}).errors.empty());
}

TEST(Spelling, WordAcrossTagsIsMarkedAroundTheTags)
{
    const auto marks = checkTextAndBrackets(u"wo{\\i1}rd ok", SubtitleFormat::Ass, segment, dictionary({u"ok"}));
    EXPECT_EQ(marks.errors, (std::vector<int>{0, 1, 7, 8}));
    EXPECT_EQ(marks.misspells, (std::vector<Misspell>{{u"word", 0, 8}, {u"word", 0, 8}}));
}

TEST(Spelling, SkipsDrawingsAndReadsBreaks)
{
    const auto check = dictionary({u"good"});
    const auto drawing = checkTextAndBrackets(u"{\\p1}m 0 0 l 10 10{\\p0}gud", SubtitleFormat::Ass, segment, check);
    EXPECT_EQ(drawing.errors, (std::vector<int>{23, 25}));
    const auto breaks = checkTextAndBrackets(u"good\\Nbda\\hgood\\nxx", SubtitleFormat::Ass, segment, check);
    EXPECT_EQ(breaks.misspells, (std::vector<Misspell>{{u"bda", 6, 8}, {u"xx", 17, 18}}));
    const auto escape = checkTextAndBrackets(u"go\\iod", SubtitleFormat::Ass, segment, check);
    EXPECT_EQ(escape.misspells, (std::vector<Misspell>{{u"go", 0, 1}, {u"iod", 3, 5}}));
    const auto trailing = checkTextAndBrackets(u"good\\", SubtitleFormat::Ass, segment, check);
    EXPECT_TRUE(trailing.errors.empty());
}

TEST(Spelling, FormatsChooseBracketsAndWrapMarks)
{
    const auto check = dictionary({u"good"});
    const auto srt = checkTextAndBrackets(u"<i>gud</i> {x}", SubtitleFormat::Srt, segment, check);
    EXPECT_EQ(srt.misspells, (std::vector<Misspell>{{u"gud", 3, 5}, {u"x", 12, 12}}));
    const auto tmp = checkTextAndBrackets(u"good|gud", SubtitleFormat::TMPlayer, segment, check);
    EXPECT_EQ(tmp.misspells, (std::vector<Misspell>{{u"gud", 5, 7}}));
    const auto mdvd = checkTextAndBrackets(u"{y:i}gud", SubtitleFormat::MicroDvd, segment, check);
    EXPECT_EQ(mdvd.misspells, (std::vector<Misspell>{{u"gud", 5, 7}}));
}

TEST(Spelling, BracketErrors)
{
    const auto marks = [](std::u16string_view text) {
        return checkTextAndBrackets(text, SubtitleFormat::Ass, segment, {}).errors;
    };
    EXPECT_EQ(marks(u"{\\b1 text"), (std::vector<int>{0, 0}));
    EXPECT_EQ(marks(u"text}"), (std::vector<int>{4, 4}));
    EXPECT_EQ(marks(u"{a{b}"), (std::vector<int>{0, 0}));
    EXPECT_EQ(marks(u"{\\\\b1}"), (std::vector<int>{2, 2}));
    EXPECT_EQ(marks(u"{\\pos(1,2}"), (std::vector<int>{5, 5}));
    EXPECT_EQ(marks(u"{\\pos(1,2))}"), (std::vector<int>{10, 10}));
    EXPECT_EQ(marks(u"{\\pos((1,2)}"), (std::vector<int>{5, 5}));
    EXPECT_TRUE(marks(u"{\\t(\\b1)}").empty());
    EXPECT_EQ(marks(u"{\\t(\\b1}"), (std::vector<int>{3, 3}));
    const auto both = checkTextAndBrackets(u"gud}", SubtitleFormat::Ass, segment, dictionary({}));
    EXPECT_EQ(both.errors, (std::vector<int>{3, 3, 0, 2}));
    EXPECT_EQ(both.misspells, (std::vector<Misspell>{{}, {u"gud", 0, 2}}));
}

TEST(Spelling, SwappedTagOffsets)
{
    const auto check = dictionary({u"good"});
    const auto marks = checkTextAndBrackets(u"{\\b1}gud{\\b0} gud", SubtitleFormat::Ass, segment, check, 1);
    EXPECT_EQ(marks.errors, (std::vector<int>{1, 3, 6, 8}));
    EXPECT_EQ(checkTextAndBrackets(u"good}", SubtitleFormat::Ass, segment, check, 1).errors, std::vector<int>{});
    EXPECT_EQ(checkTextAndBrackets(u"good{\\b1", SubtitleFormat::Ass, segment, check, 1).errors,
              (std::vector<int>{4, 4}));
}

TEST(Spelling, CheckTextForTheWindow)
{
    const auto check = dictionary({u"good"});
    EXPECT_EQ(checkText(u"wo{\\i1}rd} good\\Ngud {", SubtitleFormat::Ass, segment, check),
              (std::vector<Misspell>{{u"word", 0, 8}, {u"gud", 17, 19}}));
    EXPECT_TRUE(checkText(u"gud", SubtitleFormat::Ass, segment, {}).empty());
}

TEST(Spelling, FindMisspellsIgnoresCase)
{
    EXPECT_EQ(findMisspells(u"Gud gud GUDS {gud} G{\\i1}UD", u"gud", SubtitleFormat::Ass, segment, ascii),
              (std::vector<Misspell>{{u"Gud", 0, 2}, {u"gud", 4, 6}, {u"GUD", 19, 26}}));
}

TEST(Spelling, ReplaceMisspellKeepsBlocks)
{
    std::u16string text = u"wo{\\i1}rd!";
    EXPECT_EQ(replaceMisspell(u"word", u"world", 0, 8, text), 10);
    EXPECT_EQ(text, u"wo{\\i1}rld!");
    text = u"wo{\\i1}rd";
    EXPECT_EQ(replaceMisspell(u"word", u"wd", 0, 8, text), 7);
    EXPECT_EQ(text, u"wd{\\i1}");
    text = u"a gud day";
    EXPECT_EQ(replaceMisspell(u"gud", u"good", 2, 4, text), 6);
    EXPECT_EQ(text, u"a good day");
    text = u"wo{}rd";
    EXPECT_EQ(replaceMisspell(u"word", u"world", 0, 5, text), 5);
    EXPECT_EQ(text, u"world");
    text = u"x y";
    EXPECT_EQ(replaceMisspell(u"x", u"yz", 0, 0, text), 2);
    EXPECT_EQ(text, u"yz y");
}

TEST(Spelling, RightCase)
{
    EXPECT_EQ(rightCase(u"world", u"WROLD", ascii), u"WORLD");
    EXPECT_EQ(rightCase(u"world", u"Wrold", ascii), u"World");
    EXPECT_EQ(rightCase(u"WoRlD", u"wROLD", ascii), u"World");
    EXPECT_EQ(rightCase(u"WORLD", u"wrold", ascii), u"world");
    EXPECT_TRUE(isAllUpperCase(u"ABC", ascii));
    EXPECT_FALSE(isAllUpperCase(u"ABC1", ascii));
    EXPECT_TRUE(isAllUpperCase(u"", ascii));
}

TEST(Spelling, UserDictionaryFile)
{
    const std::u16string content = u"alpha\r\n\n  beta \r\n123\n-\n+7\n12a\n   \ngamma";
    EXPECT_EQ(userDictionaryWords(content), (std::vector<std::u16string>{u"alpha", u"  beta", u"12a", u"gamma"}));
    EXPECT_EQ(userDictionaryEntries(content),
              (std::vector<std::u16string>{u"alpha", u"  beta", u"123", u"-", u"+7", u"12a", u"", u"gamma"}));
    EXPECT_TRUE(legacyIsNumber(u""));
    EXPECT_FALSE(legacyIsNumber(u"1.5"));
    EXPECT_EQ(appendUserWord(std::nullopt, u"alpha"), u"alpha");
    EXPECT_EQ(appendUserWord(std::u16string_view(u""), u"alpha"), u"alpha");
    EXPECT_EQ(appendUserWord(std::u16string_view(u"\n"), u"alpha"), u"\n\nalpha");
    EXPECT_EQ(appendUserWord(std::u16string_view(u"alpha\r\n"), u"beta"), u"alpha\r\n\nbeta");
    const auto removed = removeUserWords(u"alpha\nBeta\n\nbeta \ngamma", {u"beta", u"gamma"});
    EXPECT_EQ(removed.content, u"alpha\r\nBeta\r\n");
    EXPECT_EQ(removed.removed, (std::vector<std::u16string>{u"beta", u"gamma"}));
    EXPECT_TRUE(removeUserWords(u"alpha", {u"beta"}).removed.empty());
}
