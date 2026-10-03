// F3: the spell checker's text walks, pinned against legacy SpellChecker::
// CheckTextAndBrackets, Check, CheckText, FindMisspells and ReplaceMisspell,
// SpellCheckerDialog::GetRightCase and IsAllUpperCase, and the UserDic.udic
// handling of Initialize, AddWord, RemoveWords and LoadAddedMisspels at
// 20d647c4. Words are segmented by a small ASCII stand-in for boost::locale
// here; the Qt segmenter the application uses is pinned in the UI tests.

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

// Runs of ASCII letters and digits are words (letters when any letter is in
// them, as ICU's rule status); every other character is its own segment.
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

} // namespace

// Words outside override blocks are checked; a misspelled word is marked
// from its first to its last character (inclusive offsets).
TEST(Spelling, MarksMisspelledWordsWithInclusiveOffsets)
{
    const auto marks = checkTextAndBrackets(u"Hello wrold, 42 times", SubtitleFormat::Ass, segment,
                                            dictionary({u"Hello", u"times"}));
    EXPECT_EQ(marks.errors, (std::vector<int>{6, 10}));
    ASSERT_EQ(marks.misspells.size(), 1u);
    EXPECT_EQ(marks.misspells[0], (Misspell{u"wrold", 6, 10}));
    // Numbers are never checked; spell checking off leaves bracket errors only.
    EXPECT_TRUE(checkTextAndBrackets(u"Hello wrold", SubtitleFormat::Ass, segment, {}).errors.empty());
}

// A word interrupted by an override block is one word; it is marked in one
// piece per run of adjacent characters, each with the whole word's span.
TEST(Spelling, WordAcrossTagsIsMarkedAroundTheTags)
{
    const auto marks = checkTextAndBrackets(u"wo{\\i1}rd ok", SubtitleFormat::Ass, segment, dictionary({u"ok"}));
    EXPECT_EQ(marks.errors, (std::vector<int>{0, 1, 7, 8}));
    EXPECT_EQ(marks.misspells, (std::vector<Misspell>{{u"word", 0, 8}, {u"word", 0, 8}}));
}

// Drawings (\p1 until \p0) and block content are skipped; \N ends a wrap and
// \h separates words (both read as two spaces); offsets stay raw.
TEST(Spelling, SkipsDrawingsAndReadsBreaks)
{
    const auto check = dictionary({u"good"});
    const auto drawing = checkTextAndBrackets(u"{\\p1}m 0 0 l 10 10{\\p0}gud", SubtitleFormat::Ass, segment, check);
    EXPECT_EQ(drawing.errors, (std::vector<int>{23, 25}));
    const auto breaks = checkTextAndBrackets(u"good\\Nbda\\hgood\\nxx", SubtitleFormat::Ass, segment, check);
    EXPECT_EQ(breaks.misspells, (std::vector<Misspell>{{u"bda", 6, 8}, {u"xx", 17, 18}}));
    // Another escape is read as written: "\i" outside a block joins the word.
    const auto escape = checkTextAndBrackets(u"go\\iod", SubtitleFormat::Ass, segment, check);
    EXPECT_EQ(escape.misspells, (std::vector<Misspell>{{u"go", 0, 1}, {u"iod", 3, 5}}));
    // A trailing backslash is read with itself as the next character.
    const auto trailing = checkTextAndBrackets(u"good\\", SubtitleFormat::Ass, segment, check);
    EXPECT_TRUE(trailing.errors.empty());
}

// SRT tags are <...>; TMPlayer, MicroDVD and MPL2 end wraps at "|".
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

// Bracket errors: each marks one position with an empty MisspellData.
TEST(Spelling, BracketErrors)
{
    const auto marks = [](std::u16string_view text) {
        return checkTextAndBrackets(text, SubtitleFormat::Ass, segment, {}).errors;
    };
    EXPECT_EQ(marks(u"{\\b1 text"), (std::vector<int>{0, 0}));       // unclosed block
    EXPECT_EQ(marks(u"text}"), (std::vector<int>{4, 4}));             // stray closing brace
    EXPECT_EQ(marks(u"{a{b}"), (std::vector<int>{0, 0}));             // nested: the outer brace
    EXPECT_EQ(marks(u"{\\\\b1}"), (std::vector<int>{2, 2}));          // "\\" inside a block
    EXPECT_EQ(marks(u"{\\pos(1,2}"), (std::vector<int>{5, 5}));       // unclosed parenthesis
    EXPECT_EQ(marks(u"{\\pos(1,2))}"), (std::vector<int>{10, 10}));  // extra closing parenthesis
    EXPECT_EQ(marks(u"{\\pos((1,2)}"), (std::vector<int>{5, 5}));     // reopened parenthesis
    EXPECT_TRUE(marks(u"{\\t(\\b1)}").empty());                      // one-letter tag's parenthesis
    EXPECT_EQ(marks(u"{\\t(\\b1}"), (std::vector<int>{3, 3}));       // closed by the brace
    const auto both = checkTextAndBrackets(u"gud}", SubtitleFormat::Ass, segment, dictionary({}));
    // Brace errors are recorded while walking, words when their wrap ends.
    EXPECT_EQ(both.errors, (std::vector<int>{3, 3, 0, 2}));
    EXPECT_EQ(both.misspells, (std::vector<Misspell>{{}, {u"gud", 0, 2}}));
}

// The Grid with tags swapped (replaceTagsLen >= 0): offsets count a block as
// the swap text's length and brace errors inside the text are not reported,
// though the end-of-text checks still are.
TEST(Spelling, SwappedTagOffsets)
{
    const auto check = dictionary({u"good"});
    const auto marks = checkTextAndBrackets(u"{\\b1}gud{\\b0} gud", SubtitleFormat::Ass, segment, check, 1);
    EXPECT_EQ(marks.errors, (std::vector<int>{1, 3, 6, 8}));
    EXPECT_EQ(checkTextAndBrackets(u"good}", SubtitleFormat::Ass, segment, check, 1).errors, std::vector<int>{});
    EXPECT_EQ(checkTextAndBrackets(u"good{\\b1", SubtitleFormat::Ass, segment, check, 1).errors,
              (std::vector<int>{4, 4}));
}

// The Spellchecker window's walk: misspelled words only, the whole text at once.
TEST(Spelling, CheckTextForTheWindow)
{
    const auto check = dictionary({u"good"});
    EXPECT_EQ(checkText(u"wo{\\i1}rd} good\\Ngud {", SubtitleFormat::Ass, segment, check),
              (std::vector<Misspell>{{u"word", 0, 8}, {u"gud", 17, 19}}));
    // Without a dictionary nothing is misspelled.
    EXPECT_TRUE(checkText(u"gud", SubtitleFormat::Ass, segment, {}).empty());
}

// Replace all finds the word in any case (wxString::CmpNoCase).
TEST(Spelling, FindMisspellsIgnoresCase)
{
    EXPECT_EQ(findMisspells(u"Gud gud GUDS {gud} G{\\i1}UD", u"gud", SubtitleFormat::Ass, segment, ascii),
              (std::vector<Misspell>{{u"Gud", 0, 2}, {u"gud", 4, 6}, {u"GUD", 19, 26}}));
}

// ReplaceMisspell keeps {...} blocks inside the word; legacy's length test
// decides when, with its quirks.
TEST(Spelling, ReplaceMisspellKeepsBlocks)
{
    std::u16string text = u"wo{\\i1}rd!";
    EXPECT_EQ(replaceMisspell(u"word", u"world", 0, 8, text), 10);
    EXPECT_EQ(text, u"wo{\\i1}rld!");
    // A shorter replacement drops the word's remaining letters.
    text = u"wo{\\i1}rd";
    EXPECT_EQ(replaceMisspell(u"word", u"wd", 0, 8, text), 7);
    EXPECT_EQ(text, u"wd{\\i1}");
    // Plain words are replaced as a range.
    text = u"a gud day";
    EXPECT_EQ(replaceMisspell(u"gud", u"good", 2, 4, text), 6);
    EXPECT_EQ(text, u"a good day");
    // A block of two characters or less is not detected: it is replaced away.
    text = u"wo{}rd";
    EXPECT_EQ(replaceMisspell(u"word", u"world", 0, 5, text), 5);
    EXPECT_EQ(text, u"world");
    // A one-letter word takes the block-keeping path (size_t(-1)); same result.
    text = u"x y";
    EXPECT_EQ(replaceMisspell(u"x", u"yz", 0, 0, text), 2);
    EXPECT_EQ(text, u"yz y");
}

// GetRightCase and IsAllUpperCase count upper-case characters.
TEST(Spelling, RightCase)
{
    EXPECT_EQ(rightCase(u"world", u"WROLD", ascii), u"WORLD");
    EXPECT_EQ(rightCase(u"world", u"Wrold", ascii), u"World");
    EXPECT_EQ(rightCase(u"WoRlD", u"wROLD", ascii), u"World"); // some upper: capitalized
    EXPECT_EQ(rightCase(u"WORLD", u"wrold", ascii), u"world");
    EXPECT_TRUE(isAllUpperCase(u"ABC", ascii));
    EXPECT_FALSE(isAllUpperCase(u"ABC1", ascii));
    EXPECT_TRUE(isAllUpperCase(u"", ascii));
}

// UserDic.udic: one word per line; Initialize skips empty and number lines
// (wxString::IsNumber: an optional sign and ASCII digits, "-" alone too).
TEST(Spelling, UserDictionaryFile)
{
    const std::u16string content = u"alpha\r\n\n  beta \r\n123\n-\n+7\n12a\n   \ngamma";
    EXPECT_EQ(userDictionaryWords(content), (std::vector<std::u16string>{u"alpha", u"  beta", u"12a", u"gamma"}));
    // The removal list keeps every line, trimmed on the right only.
    EXPECT_EQ(userDictionaryEntries(content),
              (std::vector<std::u16string>{u"alpha", u"  beta", u"123", u"-", u"+7", u"12a", u"", u"gamma"}));
    EXPECT_TRUE(legacyIsNumber(u""));
    EXPECT_FALSE(legacyIsNumber(u"1.5"));
    // AddWord: the word alone without a readable file or with an empty one
    // (OpenWrite::FileOpen returns false for empty text), else "\n" + word.
    EXPECT_EQ(appendUserWord(std::nullopt, u"alpha"), u"alpha");
    EXPECT_EQ(appendUserWord(std::u16string_view(u""), u"alpha"), u"alpha");
    EXPECT_EQ(appendUserWord(std::u16string_view(u"\n"), u"alpha"), u"\n\nalpha");
    EXPECT_EQ(appendUserWord(std::u16string_view(u"alpha\r\n"), u"beta"), u"alpha\r\n\nbeta");
    // RemoveWords: exact (case-sensitive) matches go; the rest is written
    // trimmed, each line followed by CRLF.
    const auto removed = removeUserWords(u"alpha\nBeta\n\nbeta \ngamma", {u"beta", u"gamma"});
    EXPECT_EQ(removed.content, u"alpha\r\nBeta\r\n");
    EXPECT_EQ(removed.removed, (std::vector<std::u16string>{u"beta", u"gamma"}));
    EXPECT_TRUE(removeUserWords(u"alpha", {u"beta"}).removed.empty());
}
