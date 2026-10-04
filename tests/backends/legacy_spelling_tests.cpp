// F3 / R2-hunspell: the production spelling adapters. Hunspell 1.7.3 over
// tiny dictionaries written for these tests (tests/fixtures/spelling:
// en_TEST in UTF-8 with one suffix rule, pl_TEST in ISO8859-2), and legacy's
// word segmentation, boost::locale's boundary::word over ICU
// (SpellChecker::Check / CheckText at 20d647c4).

#include "hikari/backends/legacy_spelling.h"
#include "hikari/core/spelling.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

using namespace hikari;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = HIKARI_SPELLING_FIXTURES;

std::unique_ptr<application::SpellingBackend> load(const char *name)
{
    const fs::path base = kFixtures / name;
    return backends::hunspellSpellingLoader()(fs::path(base).concat(".aff"), fs::path(base).concat(".dic"));
}

bool contains(const std::vector<std::u16string> &list, std::u16string_view word)
{
    return std::ranges::find(list, word) != list.end();
}

struct Segment {
    std::u16string text;
    bool letters;
    bool number;
    bool operator==(const Segment &) const = default;
};

std::vector<Segment> words(std::u16string_view text)
{
    std::vector<Segment> out;
    for (const auto &s : backends::legacySpellingText().segment(text))
        out.push_back({std::u16string(text.substr(s.start, s.length)), s.letters, s.number});
    return out;
}

} // namespace

// Hunspell checks with the affix rules, suggests, and takes runtime words.
TEST(HunspellSpelling, ChecksSuggestsAndAddsWords)
{
    const auto backend = load("en_TEST");
    ASSERT_TRUE(backend);
    EXPECT_TRUE(backend->spell(u"Hello"));
    EXPECT_TRUE(backend->spell(u"worlds")); // world/S
    EXPECT_TRUE(backend->spell(u"don't"));
    EXPECT_FALSE(backend->spell(u"wrold"));
    EXPECT_FALSE(backend->spell(u""));
    EXPECT_TRUE(contains(backend->suggest(u"wrold"), u"world"));
    EXPECT_TRUE(contains(backend->suggest(u"Teh"), u"The"));
    // A lone surrogate cannot be converted to the dictionary's UTF-8: misspelled.
    EXPECT_FALSE(backend->spell(std::u16string(1, char16_t(0xD800))));
    EXPECT_TRUE(backend->suggest(std::u16string(1, char16_t(0xD800))).empty());
    backend->add(u"Hikari");
    EXPECT_TRUE(backend->spell(u"Hikari"));
    EXPECT_TRUE(backend->remove(u"Hikari"));
    EXPECT_FALSE(backend->spell(u"Hikari"));
    // Legacy adds a failed conversion's null buffer (undefined, a crash); it
    // is skipped here (R3-hang-crash-loss).
    backend->add(std::u16string(1, char16_t(0xDC00)));
}

// Words travel in the dictionary's own encoding (SET ISO8859-2) both ways; a
// character it cannot hold makes the word misspelled.
TEST(HunspellSpelling, ConvertsToTheDictionaryEncoding)
{
    const auto backend = load("pl_TEST");
    ASSERT_TRUE(backend);
    EXPECT_TRUE(backend->spell(u"gęś"));
    EXPECT_FALSE(backend->spell(u"gęsi"));
    EXPECT_FALSE(backend->spell(u"日本"));
    EXPECT_TRUE(contains(backend->suggest(u"gęź"), u"gęś"));
    backend->add(u"źdźbło");
    EXPECT_TRUE(backend->spell(u"źdźbło"));
}

// Missing files load an empty dictionary, as Hunspell's constructor does.
TEST(HunspellSpelling, MissingFilesCheckNothingAsCorrect)
{
    const auto backend = load("missing");
    ASSERT_TRUE(backend);
    EXPECT_FALSE(backend->spell(u"Hello"));
}

// The SpellChecker over Hunspell and a folder holding the fixtures.
// On Windows Hunspell gets the long-path form (UTF-8 names, no MAX_PATH);
// a UNC location (a redirected AppData under \\server\share) takes the
// \\?\UNC\ form, since \\?\\\server\share names nothing.
TEST(HunspellSpelling, WindowsLongPathForm)
{
    EXPECT_EQ(backends::windowsLongPath("C:\\Users\\a\\Dictionary\\en_US.aff"), "\\\\?\\C:\\Users\\a\\Dictionary\\en_US.aff");
    EXPECT_EQ(backends::windowsLongPath("\\\\server\\share\\AppData\\Dictionary\\en_US.dic"),
              "\\\\?\\UNC\\server\\share\\AppData\\Dictionary\\en_US.dic");
    EXPECT_EQ(backends::windowsLongPath("\\\\?\\C:\\a.aff"), "\\\\?\\C:\\a.aff");
    EXPECT_EQ(backends::windowsLongPath("\\\\?\\UNC\\server\\share\\a.aff"), "\\\\?\\UNC\\server\\share\\a.aff");
    EXPECT_EQ(backends::windowsLongPath("\\\\.\\C:\\a.aff"), "\\\\.\\C:\\a.aff");
}

#ifdef _WIN32
// The loader normalizes the path before prefixing it (the prefix turns off
// Windows' own handling of "." and ".."), so a relative-looking route opens.
TEST(HunspellSpelling, WindowsPathWithDotsOpens)
{
    const fs::path base = kFixtures / "subdir-that-does-not-exist" / ".." / "en_TEST";
    auto backend = backends::hunspellSpellingLoader()(fs::path(base).concat(".aff"), fs::path(base).concat(".dic"));
    ASSERT_TRUE(backend);
    EXPECT_TRUE(backend->spell(u"Hello")); // an unopened pair knows no word
}
#endif

TEST(HunspellSpelling, SpellCheckerInitializesFromTheFolder)
{
    std::random_device random;
    const fs::path user = fs::temp_directory_path() / ("hikari-hunspell-" + std::to_string(random()));
    fs::create_directories(user);
    {
        std::ofstream out(user / "UserDic.udic", std::ios::binary);
        out << "\xEF\xBB\xBFHikari\r\nwrold";
    }
    application::SpellChecker checker(std::vector<std::filesystem::path>{user, kFixtures}, backends::hunspellSpellingLoader());
    EXPECT_EQ(checker.initialize(u"en_TEST"), application::SpellChecker::Status::Ready);
    EXPECT_TRUE(checker.checkWord(u"Hikari"));
    EXPECT_TRUE(checker.checkWord(u"wrold"));
    EXPECT_FALSE(checker.checkWord(u"Teh"));
    EXPECT_EQ(checker.userDictionary(), user / "UserDic.udic");
    std::error_code ec;
    fs::remove_all(user, ec);
}

// boost::locale over ICU: segments cover the text; apostrophes and decimal
// points stay inside words and numbers; letters then digits take the status
// of ICU's last rule (a number, so legacy never checks "abc123"), digits
// then letters are a letters word; punctuation and spaces are neither.
TEST(LegacyWordSegmentation, FollowsIcuWordRules)
{
    EXPECT_EQ(words(u"don't 3.14 abc123, 42!"),
              (std::vector<Segment>{{u"don't", true, false},
                                    {u" ", false, false},
                                    {u"3.14", false, true},
                                    {u" ", false, false},
                                    {u"abc123", false, true},
                                    {u",", false, false},
                                    {u" ", false, false},
                                    {u"42", false, true},
                                    {u"!", false, false}}));
    EXPECT_EQ(words(u"4ever"), (std::vector<Segment>{{u"4ever", true, false}}));
    // Polish letters are letters; ICU's dictionary segmentation keeps "世界"
    // one ideographic word (word_ideo, inside word_letters).
    const auto pl = words(u"Zażółć 世界");
    ASSERT_EQ(pl.size(), 3u);
    EXPECT_EQ(pl[0], (Segment{u"Zażółć", true, false}));
    EXPECT_EQ(pl[2], (Segment{u"世界", true, false}));
    // Offsets are UTF-16 units: a supplementary letter counts two.
    const std::u16string gothic = u"a \U00010330\U00010331 b";
    const auto segments = backends::legacySpellingText().segment(gothic);
    ASSERT_EQ(segments.size(), 5u);
    EXPECT_EQ(segments[2].start, 2u);
    EXPECT_EQ(segments[2].length, 4u);
    EXPECT_TRUE(segments[2].letters);
    EXPECT_EQ(segments[4].start, 7u);
    EXPECT_TRUE(words(u"").empty());
}

// The core walk with ICU segmentation: tag-aware offsets in the raw text.
TEST(LegacyWordSegmentation, TagAwareMarks)
{
    const auto spelling = backends::legacySpellingText();
    const std::vector<std::u16string> dictionary{u"gęślą", u"don't"};
    const core::legacy::WordCheck check = [&](std::u16string_view w) {
        return std::ranges::find(dictionary, w) != dictionary.end();
    };
    const std::u16string text = u"{\\i1}Zażółć{\\i0} gęślą don't\\Ndont {\\p1}m 0 0{\\p0}";
    const auto marks = core::legacy::checkTextAndBrackets(text, core::SubtitleFormat::Ass, spelling.segment, check);
    EXPECT_EQ(marks.errors, (std::vector<int>{5, 10, 30, 33}));
    ASSERT_EQ(marks.misspells.size(), 2u);
    EXPECT_EQ(marks.misspells[0].word, u"Zażółć");
    // Per-unit case functions: GetRightCase keeps a capital.
    EXPECT_EQ(core::legacy::rightCase(u"żółw", u"Zólw", spelling.cases), u"Żółw");
    EXPECT_TRUE(core::legacy::isAllUpperCase(u"ŻÓŁW", spelling.cases));
}
