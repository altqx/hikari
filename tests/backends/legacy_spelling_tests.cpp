#include "hikari/backends/legacy_spelling.h"
#include "hikari/application/misspell_replacer.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/spelling.h"
#include "hikari/core/srt.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <ostream>
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

}

TEST(HunspellSpelling, ChecksSuggestsAndAddsWords)
{
    const auto backend = load("en_TEST");
    ASSERT_TRUE(backend);
    EXPECT_TRUE(backend->spell(u"Hello"));
    EXPECT_TRUE(backend->spell(u"worlds"));
    EXPECT_TRUE(backend->spell(u"don't"));
    EXPECT_FALSE(backend->spell(u"wrold"));
    EXPECT_FALSE(backend->spell(u""));
    EXPECT_TRUE(contains(backend->suggest(u"wrold"), u"world"));
    EXPECT_TRUE(contains(backend->suggest(u"Teh"), u"The"));
    EXPECT_FALSE(backend->spell(std::u16string(1, char16_t(0xD800))));
    EXPECT_TRUE(backend->suggest(std::u16string(1, char16_t(0xD800))).empty());
    backend->add(u"Hikari");
    EXPECT_TRUE(backend->spell(u"Hikari"));
    EXPECT_TRUE(backend->remove(u"Hikari"));
    EXPECT_FALSE(backend->spell(u"Hikari"));
    backend->add(std::u16string(1, char16_t(0xDC00)));
}

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

TEST(HunspellSpelling, MissingFilesCheckNothingAsCorrect)
{
    const auto backend = load("missing");
    ASSERT_TRUE(backend);
    EXPECT_FALSE(backend->spell(u"Hello"));
}

// Windows: Hunspell gets the long-path form; a UNC path takes \\?\UNC\, since \\?\\\server\share names nothing.
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
// The \\?\ prefix turns off Windows' handling of "." and "..", so the loader normalizes the path first.
TEST(HunspellSpelling, WindowsPathWithDotsOpens)
{
    const fs::path base = kFixtures / "subdir-that-does-not-exist" / ".." / "en_TEST";
    auto backend = backends::hunspellSpellingLoader()(fs::path(base).concat(".aff"), fs::path(base).concat(".dic"));
    ASSERT_TRUE(backend);
    EXPECT_TRUE(backend->spell(u"Hello"));
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
    EXPECT_FALSE(checker.checkWord(u"Teh", true));
    EXPECT_EQ(checker.userDictionary(), user / "UserDic.udic");
    std::error_code ec;
    fs::remove_all(user, ec);
}

TEST(HunspellSpelling, MixedLinesMarkOnlyRealMisspellings)
{
    std::random_device random;
    const fs::path user = fs::temp_directory_path() / ("hikari-mixed-" + std::to_string(random()));
    fs::create_directories(user);
    application::SpellChecker checker(std::vector<std::filesystem::path>{user, kFixtures}, backends::hunspellSpellingLoader());
    ASSERT_EQ(checker.initialize(u"en_TEST"), application::SpellChecker::Status::Ready);
    const auto spelling = backends::legacySpellingText();
    auto marked = [&](std::u16string_view text) {
        std::vector<std::u16string> out;
        for (const auto &m : application::editorMarks(text, core::SubtitleFormat::Ass, checker, spelling, true).misspells)
            out.push_back(m.word);
        return out;
    };
    using Words = std::vector<std::u16string>;
    EXPECT_EQ(marked(u"Hello world, 안녕하세요 세계!"), Words{});
    EXPECT_EQ(marked(u"Hello wrold, 안녕하세요 세게계!"), (Words{u"wrold", u"세게계"}));
    EXPECT_EQ(marked(u"The good kot, gęś"), Words{});
    EXPECT_EQ(marked(u"The good kott, gęśi"), (Words{u"kott", u"gęśi"}));
    EXPECT_EQ(marked(u"{\\i1}Hello{\\i0} வணக்கம் உலகம்"), Words{});
    EXPECT_EQ(marked(u"Hello வணக்கமம் உலகம் texts"), (Words{u"வணக்கமம்"}));
    EXPECT_EQ(marked(u"привет こんにちは Teh"), Words{u"Teh"});
    EXPECT_TRUE(contains(checker.suggestions(u"세게계"), u"세계"));
    EXPECT_TRUE(contains(checker.suggestions(u"kott", {u"The", u"good", u"kott"}), u"kot"));
    std::error_code ec;
    fs::remove_all(user, ec);
}

// ICU: letters then digits take its last rule's status (a number); digits then letters are a letters word.
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
    // ICU's dictionary segmentation keeps "世界" one ideographic word.
    const auto pl = words(u"Zażółć 世界");
    ASSERT_EQ(pl.size(), 3u);
    EXPECT_EQ(pl[0], (Segment{u"Zażółć", true, false}));
    EXPECT_EQ(pl[2], (Segment{u"世界", true, false}));
    const std::u16string gothic = u"a \U00010330\U00010331 b";
    const auto segments = backends::legacySpellingText().segment(gothic);
    ASSERT_EQ(segments.size(), 5u);
    EXPECT_EQ(segments[2].start, 2u);
    EXPECT_EQ(segments[2].length, 4u);
    EXPECT_TRUE(segments[2].letters);
    EXPECT_EQ(segments[4].start, 7u);
    EXPECT_TRUE(words(u"").empty());
}

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
    // The "C" locale (English interface): only A-Z count as capitals and change.
    EXPECT_EQ(core::legacy::rightCase(u"żółw", u"Zólw", spelling.cases), u"żółw");
    EXPECT_FALSE(core::legacy::isAllUpperCase(u"ŻÓŁW", spelling.cases));
    const auto everyLetter = backends::legacySpellingText(true);
    EXPECT_EQ(core::legacy::rightCase(u"żółw", u"Zólw", everyLetter.cases), u"Żółw");
    EXPECT_TRUE(core::legacy::isAllUpperCase(u"ŻÓŁW", everyLetter.cases));
}

// glibc's "C" locale towlower/towupper/iswupper leave U+0141 alone (glibc 2.39 and 2.44).
TEST(LegacyCaseMapping, FollowsTheInterfaceLanguage)
{
    const auto c = backends::legacyCaseMapping(false);
    EXPECT_EQ(c.toLower(u'A'), u'a');
    EXPECT_EQ(c.toUpper(u'z'), u'Z');
    EXPECT_EQ(c.toLower(u'Ł'), u'Ł');
    EXPECT_EQ(c.toUpper(u'ł'), u'ł');
    EXPECT_EQ(c.toLower(u'À'), u'À');
    EXPECT_FALSE(c.isUpper(u'Ł'));
    EXPECT_TRUE(c.isUpper(u'Q'));
    const auto e = backends::legacyCaseMapping(true);
    EXPECT_EQ(e.toLower(u'Ł'), u'ł');
    EXPECT_EQ(e.toUpper(u'ł'), u'Ł');
    EXPECT_EQ(e.toLower(u'Σ'), u'σ');
    EXPECT_TRUE(e.isUpper(u'Ł'));
    const auto replacer = [](const core::legacy::CaseMapping &m) {
        return application::ReplacerCase{m.isUpper, m.toLower, m.toUpper};
    };
    EXPECT_EQ(application::moveCase(u"Ąb", u"xy", 0, replacer(c)), u"xy");
    EXPECT_EQ(application::moveCase(u"Ąb", u"xy", 0, replacer(e)), u"Xy");
    EXPECT_EQ(application::moveCase(u"x", u"żółw", application::kReplacerUpperCase, replacer(c)), u"żółW");
    EXPECT_EQ(application::moveCase(u"x", u"żółw", application::kReplacerUpperCase, replacer(e)), u"ŻÓŁW");
    EXPECT_EQ(application::moveCase(u"Ab", u"źdź", 0, replacer(c)), u"źdź");
    EXPECT_EQ(application::moveCase(u"Ab", u"źdź", 0, replacer(e)), u"Źdź");
}

// Compared in code points: the legacy Linux build's wxString counted UTF-32 units.
namespace {

struct Walked {
    int row;
    int start, end;
    bool operator==(const Walked &) const = default;
};

std::ostream &operator<<(std::ostream &os, const Walked &w)
{
    return os << "{" << w.row << ", " << w.start << ", " << w.end << "}";
}

int codePoints(std::u16string_view text, int units)
{
    int n = 0;
    for (int i = 0; i < units; ++i)
        if (!(text[i] >= 0xDC00 && text[i] <= 0xDFFF))
            ++n;
    return n;
}

std::vector<Walked> windowWalk(const char *input, core::SubtitleFormat format)
{
    const auto backend = load("en_TEST");
    EXPECT_TRUE(backend);
    std::ifstream in(fs::path(HIKARI_CAPTURE_INPUTS) / input, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    const auto doc = format == core::SubtitleFormat::Srt ? core::loadSrt(bytes).document : core::loadAss(bytes).document;
    const auto spelling = backends::legacySpellingText();
    const core::legacy::WordCheck check = [&](std::u16string_view w) { return backend->spell(w); };
    std::vector<Walked> out;
    const auto lines = doc.lines();
    for (std::size_t row = 0; row < lines.size(); ++row) {
        const std::u16string t = core::toUtf16(lines[row]->text);
        for (const auto &m : core::legacy::checkText(t, format, spelling.segment, check))
            out.push_back({static_cast<int>(row), codePoints(t, m.start), codePoints(t, m.end + 1) - 1});
    }
    return out;
}

}

TEST(LegacySpellingCapture, WindowWalkMatchesTheLinuxCapture)
{
    const std::vector<Walked> legacy{
        {0, 6, 10},   {0, 16, 20},  {1, 0, 8},    {1, 10, 11},  {2, 23, 25},  {3, 6, 8},    {3, 17, 18},
        {4, 0, 1},    {4, 3, 5},    {5, 6, 9},    {5, 23, 27},  {6, 0, 5},    {6, 7, 8},    {6, 10, 14},
        {7, 0, 0},    {7, 2, 3},    {7, 5, 5},    {8, 0, 8},    {8, 17, 19},  {10, 12, 14}, {10, 16, 20},
        {11, 0, 4},   {11, 6, 12},  {12, 0, 3},   {12, 5, 11},  {12, 14, 19}, {12, 22, 27}, {13, 26, 30},
        {13, 39, 42}, {13, 44, 46}, {15, 20, 28}, {15, 30, 30}, {15, 32, 35}, {16, 14, 16}, {17, 0, 2},
    };
    EXPECT_EQ(windowWalk("spell-walk.ass", core::SubtitleFormat::Ass), legacy);
    // spell-walk.srt: <...> are tags, {x} is text, "|" is not a wrap in SRT.
    EXPECT_EQ(windowWalk("spell-walk.srt", core::SubtitleFormat::Srt),
              (std::vector<Walked>{{0, 3, 5}, {0, 12, 12}, {0, 15, 19}, {1, 6, 10}}));
}
