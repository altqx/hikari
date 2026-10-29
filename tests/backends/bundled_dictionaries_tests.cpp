// DK1 (#473): the dictionaries every package bundles (HikariDictionaries.cmake,
// as the build fetched them) through the production Hunspell adapter: the
// Dictionary folder lists each of them, and each knows the fixture's words
// and rejects its misspellings (tests/fixtures/spelling/bundled_words.txt).

#include "hikari/application/spell_checker.h"
#include "hikari/backends/legacy_spelling.h"

#include <gtest/gtest.h>

#include <QString>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace hikari;
namespace fs = std::filesystem;

namespace {

const fs::path kDictionaries = fs::path(HIKARI_BUNDLED_DICTIONARIES) / "Dictionary";

struct Word {
    std::string dictionary;
    bool right;
    std::u16string text;
};

std::vector<Word> fixtureWords()
{
    std::ifstream in(fs::path(HIKARI_SPELLING_FIXTURES) / "bundled_words.txt", std::ios::binary);
    std::vector<Word> out;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line.front() == '#')
            continue;
        const auto a = line.find('\t'), b = line.find('\t', a + 1);
        out.push_back({line.substr(0, a), line.substr(a + 1, b - a - 1) == "right",
                       QString::fromUtf8(line.substr(b + 1)).toStdU16String()});
    }
    return out;
}

} // namespace

// Settings › Spelling lists these for the Primary language.
TEST(BundledDictionaries, TheFolderListsEveryLanguage)
{
    auto symbols = application::availableDictionaries(kDictionaries);
    std::ranges::sort(symbols);
    EXPECT_EQ(symbols, (std::vector<std::u16string>{u"en_US", u"ko_KR", u"pl", u"ta_IN", u"th_TH"}));
}

TEST(BundledDictionaries, EachKnowsItsWordsAndRejectsMisspellings)
{
    const auto words = fixtureWords();
    ASSERT_EQ(words.size(), 18u);
    std::map<std::string, std::unique_ptr<application::SpellingBackend>> loaded;
    for (const Word &word : words) {
        auto &backend = loaded[word.dictionary];
        if (!backend) {
            const fs::path base = kDictionaries / word.dictionary;
            backend = backends::hunspellSpellingLoader()(fs::path(base).concat(".aff"), fs::path(base).concat(".dic"));
            ASSERT_TRUE(backend) << word.dictionary;
        }
        EXPECT_EQ(backend->spell(word.text), word.right)
            << word.dictionary << ": " << QString::fromStdU16String(word.text).toStdString();
    }
    EXPECT_EQ(loaded.size(), 5u);
}

// SP1 (#474): all five check together, each the words of its script, the
// primary English; a mixed Line marks only the real misspellings.
TEST(BundledDictionaries, MixedLinesMarkOnlyRealMisspellings)
{
    application::SpellChecker checker(kDictionaries, backends::hunspellSpellingLoader());
    ASSERT_EQ(checker.initialize(u"en_US"), application::SpellChecker::Status::Ready);
    const auto spelling = backends::legacySpellingText();
    auto marked = [&](std::u16string_view text) {
        std::vector<std::u16string> out;
        for (const auto &m : application::editorMarks(text, core::SubtitleFormat::Ass, checker, spelling, true).misspells)
            out.push_back(m.word);
        return out;
    };
    using Words = std::vector<std::u16string>;
    EXPECT_EQ(marked(u"Hello spelling, 안녕하세요 학교!"), Words{});
    EXPECT_EQ(marked(u"Hello speling, 안녕하세여 학교!"), (Words{u"speling", u"안녕하세여"}));
    EXPECT_EQ(marked(u"{\\i1}The źdźbło{\\i0} spelling"), Words{});
    EXPECT_EQ(marked(u"The źdźbłoo"), Words{u"źdźbłoo"});
    EXPECT_EQ(marked(u"வணக்கம் தமிழ் spelling"), Words{});
    EXPECT_EQ(marked(u"வணக்கமம தமிழ் speling"), (Words{u"வணக்கமம", u"speling"}));
    EXPECT_EQ(marked(u"ภาษา spelling"), Words{});
    EXPECT_EQ(marked(u"привет speling"), Words{u"speling"}); // no Cyrillic dictionary
    // A Polish primary marks the same words.
    ASSERT_EQ(checker.initialize(u"pl"), application::SpellChecker::Status::Ready);
    EXPECT_EQ(marked(u"Hello speling, 안녕하세여 학교!"), (Words{u"speling", u"안녕하세여"}));
}
