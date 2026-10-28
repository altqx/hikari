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

// Settings › Spelling lists these (Application::dictionaries).
TEST(BundledDictionaries, TheFolderListsEveryLanguage)
{
    auto symbols = application::availableDictionaries(kDictionaries);
    std::ranges::sort(symbols);
    EXPECT_EQ(symbols, (std::vector<std::u16string>{u"en_US", u"ko_KR", u"pl", u"ta_IN", u"th_TH"}));
}

TEST(BundledDictionaries, EachKnowsItsWordsAndRejectsMisspellings)
{
    const auto words = fixtureWords();
    ASSERT_EQ(words.size(), 16u);
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
