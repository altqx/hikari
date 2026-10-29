#include "hikari/application/spell_checker.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include "spelling/fake_spelling.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>
#include <iterator>
#include <mutex>
#include <random>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#endif

using namespace hikari;
using namespace hikari::application;
namespace fs = std::filesystem;

namespace {

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> b(text.size());
    std::memcpy(b.data(), text.data(), text.size());
    return b;
}

core::Document load(std::string_view text)
{
    return core::loadAss(bytes(text)).document;
}

void writeFile(const fs::path &path, std::string_view content)
{
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string readFile(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string utf8(const std::u8string &s)
{
    return {s.begin(), s.end()};
}

constexpr std::string_view kHeader =
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

struct Spelling : ::testing::Test {
    fs::path folder;
    fakes::FakeSpelling *backend = nullptr;
    std::unique_ptr<SpellChecker> checker;
    const SpellingText text = fakes::asciiSpellingText();

    void SetUp() override
    {
        std::random_device random;
        folder = fs::temp_directory_path() / ("hikari-spelling-" + std::to_string(random()));
        fs::create_directories(folder);
        writeFile(folder / "en_US.aff", "SET UTF-8\n");
        writeFile(folder / "en_US.dic", "8\nHello\nworld/M\nthe\nThe\nis\ngood\ntext\nbig\n");
        checker = std::make_unique<SpellChecker>(folder, fakes::FakeSpelling::loader(&backend));
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(folder, ec);
    }
};

#ifndef _WIN32
std::vector<std::string> readdirPairs(const fs::path &folder)
{
    std::vector<std::string> dics, affs;
    if (DIR *dir = opendir(folder.c_str())) {
        while (const dirent *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name.front() == '.' || fs::is_directory(folder / name))
                continue;
            if (name.size() > 4 && name.ends_with(".dic"))
                dics.push_back(name.substr(0, name.size() - 4));
            else if (name.size() > 4 && name.ends_with(".aff"))
                affs.push_back(name.substr(0, name.size() - 4));
        }
        closedir(dir);
    }
    std::vector<std::string> out;
    for (const auto &dic : dics)
        if (std::ranges::find(affs, dic) != affs.end())
            out.push_back(dic);
    return out;
}
#endif

std::vector<std::string> names(const std::vector<std::u16string> &words)
{
    std::vector<std::string> out;
    for (const auto &w : words)
        out.push_back(utf8(core::toUtf8(w)));
    return out;
}

}

TEST_F(Spelling, AvailableDictionariesPairByName)
{
    writeFile(folder / "pl.dic", "1\nkot\n");
    writeFile(folder / "pl.aff", "");
    writeFile(folder / "readme.txt", "");
    fs::create_directories(folder / "dir.dic");
    fs::create_directories(folder / "dir.aff");
#ifdef _WIN32
    EXPECT_EQ(names(availableDictionaries(folder)), (std::vector<std::string>{"en_US", "pl"}));
    writeFile(folder / "hidden.dic", "1\nx\n");
    writeFile(folder / "hidden.aff", "");
    SetFileAttributesW((folder / "hidden.dic").c_str(), FILE_ATTRIBUTE_HIDDEN);
    SetFileAttributesW((folder / "hidden.aff").c_str(), FILE_ATTRIBUTE_HIDDEN);
    EXPECT_EQ(names(availableDictionaries(folder)), (std::vector<std::string>{"en_US", "pl"}));
    writeFile(folder / "de.dic", "1\nHaus\n");
    writeFile(folder / "ko_KR.aff", "");
    writeFile(folder / "ta.dic", "1\nx\n");
    writeFile(folder / "ta.aff", "");
    EXPECT_EQ(names(availableDictionaries(folder)), (std::vector<std::string>{"en_US", "pl", "ta"}));
#else
    writeFile(folder / ".hidden.dic", "1\nx\n");
    writeFile(folder / ".hidden.aff", "");
    const auto listed = names(availableDictionaries(folder));
    EXPECT_EQ(listed, readdirPairs(folder));
    EXPECT_TRUE(std::ranges::find(listed, ".hidden") == listed.end());
    writeFile(folder / "de.dic", "1\nHaus\n");
    writeFile(folder / "ko_KR.aff", "");
    writeFile(folder / "ta.dic", "1\nx\n");
    writeFile(folder / "ta.aff", "");
    auto sorted = names(availableDictionaries(folder));
    EXPECT_EQ(sorted, readdirPairs(folder));
    std::ranges::sort(sorted);
    EXPECT_EQ(sorted, (std::vector<std::string>{"en_US", "pl", "ta"}));
    writeFile(folder / "EN.DIC", "");
    writeFile(folder / "EN.AFF", "");
    EXPECT_EQ(names(availableDictionaries(folder)), readdirPairs(folder));
#endif
    EXPECT_TRUE(availableDictionaries(folder / "missing").empty());
}

TEST_F(Spelling, InitializeLoadsTheUserDictionary)
{
    writeFile(checker->userDictionary(), "\xEF\xBB\xBFkotek\r\n123\r\n\r\nzzz \r\n");
    EXPECT_EQ(checker->initialize(u"pl"), SpellChecker::Status::NoDictionary);
    EXPECT_FALSE(checker->ready());
    EXPECT_TRUE(checker->checkWord(u"wrold"));
    EXPECT_EQ(checker->initialize(u""), SpellChecker::Status::Ready);
    ASSERT_TRUE(backend);
    EXPECT_EQ(names(backend->added), (std::vector<std::string>{"kotek", "zzz"}));
    EXPECT_TRUE(checker->checkWord(u"kotek"));
    EXPECT_FALSE(checker->checkWord(u"wrold"));
    EXPECT_EQ(checker->suggestions(u"wrold").front(), u"world");
    SpellChecker noBackend(folder, {});
    EXPECT_EQ(noBackend.initialize(u"en_US"), SpellChecker::Status::LoadFailed);
}

TEST_F(Spelling, UserWordsPersist)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EXPECT_FALSE(checker->addWord(u""));
    EXPECT_FALSE(checker->addWord(u"-12"));
    EXPECT_FALSE(checker->checkWord(u"wrold"));
    EXPECT_TRUE(checker->addWord(u"wrold"));
    EXPECT_TRUE(checker->checkWord(u"wrold"));
    EXPECT_TRUE(checker->addWord(u"Gdańsk"));
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBFwrold\nGda\xC5\x84sk");
    EXPECT_EQ(names(checker->addedWords()), (std::vector<std::string>{"wrold", "Gda\xC5\x84sk"}));
    EXPECT_FALSE(checker->removeWords({u"nothing"}));
    EXPECT_TRUE(checker->removeWords({u"wrold"}));
    EXPECT_EQ(names(backend->removed), (std::vector<std::string>{"wrold"}));
    EXPECT_FALSE(checker->checkWord(u"wrold"));
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBFGda\xC5\x84sk\r\n");
    EXPECT_TRUE(checker->addWord(u"zzz"));
#ifdef _WIN32
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBFGda\xC5\x84sk\n\nzzz");
#else
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBFGda\xC5\x84sk\r\n\nzzz");
#endif
    EXPECT_TRUE(checker->removeWords({u"Gda\u0144sk", u"zzz"}));
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBF");
    EXPECT_FALSE(readUserDictionary(checker->userDictionary()));
    EXPECT_TRUE(checker->addedWords().empty());
    EXPECT_TRUE(checker->addWord(u"alpha"));
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBF" "alpha");
    writeFile(checker->userDictionary(), "");
    EXPECT_TRUE(checker->addWord(u"beta"));
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBF" "beta");
}

TEST_F(Spelling, UserWordsWithoutADictionary)
{
    EXPECT_FALSE(checker->addWord(u"wrold"));
    EXPECT_FALSE(checker->addUserWord(u""));
    EXPECT_FALSE(checker->addUserWord(u"-12"));
    EXPECT_TRUE(checker->addUserWord(u"wrold"));
    EXPECT_EQ(names(checker->addedWords()), (std::vector<std::string>{"wrold"}));
    EXPECT_TRUE(checker->addUserWord(u"gone"));
    EXPECT_TRUE(checker->removeWords({u"gone"}));
    EXPECT_EQ(names(checker->addedWords()), (std::vector<std::string>{"wrold"}));
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EXPECT_TRUE(checker->checkWord(u"wrold"));
    EXPECT_TRUE(checker->addUserWord(u"zzyzx"));
    EXPECT_TRUE(checker->checkWord(u"zzyzx"));
    EXPECT_EQ(names(checker->addedWords()), (std::vector<std::string>{"wrold", "zzyzx"}));
}

TEST_F(Spelling, UserDictionaryReadsPerPlatform)
{
    writeFile(checker->userDictionary(), "\xEF\xBB\xBF" "alpha\r\n\r\nbeta\x1Agamma\r");
#ifdef _WIN32
    EXPECT_EQ(readUserDictionary(checker->userDictionary()), std::u16string(u"alpha\n\nbeta"));
    EXPECT_EQ(names(checker->addedWords()), (std::vector<std::string>{"alpha", "beta"}));
#else
    EXPECT_EQ(readUserDictionary(checker->userDictionary()), std::u16string(u"alpha\r\n\r\nbeta\x1Agamma\r"));
    EXPECT_EQ(names(checker->addedWords()), (std::vector<std::string>{"alpha", "", "beta\x1Agamma"}));
#endif
    writeFile(checker->userDictionary(), std::string("\xFF\xFE" "a\0\r\0\n\0b\0", 10));
    EXPECT_EQ(readUserDictionary(checker->userDictionary()), std::u16string(u"a\r\nb"));
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EXPECT_EQ(names(backend->added), (std::vector<std::string>{"a", "b"}));
}

TEST_F(Spelling, DictionaryFoldersInSearchOrder)
{
    const fs::path bundled = folder / "bundled";
    fs::create_directories(bundled);
    writeFile(bundled / "en_US.aff", "");
    writeFile(bundled / "en_US.dic", "1\nbundled\n");
    writeFile(bundled / "pl.aff", "");
    writeFile(bundled / "pl.dic", "1\nkot\n");
#ifdef _WIN32
    const std::vector<std::string> listed{"en_US", "pl"};
#else
    std::vector<std::string> listed = readdirPairs(folder);
    for (auto &symbol : readdirPairs(bundled))
        if (std::ranges::find(listed, symbol) == listed.end())
            listed.push_back(symbol);
    ASSERT_EQ(listed.front(), "en_US");
#endif
    EXPECT_EQ(names(availableDictionaries(std::vector<fs::path>{folder, bundled})), listed);
    SpellChecker both(std::vector<fs::path>{folder, bundled}, fakes::FakeSpelling::loader(&backend));
    ASSERT_EQ(both.initialize(u"pl"), SpellChecker::Status::Ready);
    EXPECT_TRUE(both.checkWord(u"kot"));
    ASSERT_EQ(both.initialize(u"en_US"), SpellChecker::Status::Ready);
    EXPECT_TRUE(both.checkWord(u"Hello"));
    EXPECT_FALSE(both.checkWord(u"bundled", true));
    EXPECT_EQ(both.userDictionary(), folder / "UserDic.udic");
    EXPECT_TRUE(both.addWord(u"Hikari"));
    EXPECT_TRUE(fs::exists(folder / "UserDic.udic"));
    EXPECT_FALSE(fs::exists(bundled / "UserDic.udic"));
    EXPECT_EQ(both.initialize(u"de"), SpellChecker::Status::NoDictionary);
}

TEST_F(Spelling, MultilingualRoutesWordsByScript)
{
    writeFile(folder / "pl.aff", "SET UTF-8\n");
    writeFile(folder / "pl.dic", "3\nAla\nma\nkota\n");
    writeFile(folder / "ko_KR.aff", "SET UTF-8\n");
    writeFile(folder / "ko_KR.dic", "2\n\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94\n\xEC\x84\xB8\xEA\xB3\x84\n");
    writeFile(folder / "ta.aff", "SET UTF-8\n");
    writeFile(folder / "ta.dic", "1\n\xE0\xAE\xB5\xE0\xAE\xA3\xE0\xAE\x95\xE0\xAF\x8D\xE0\xAE\x95\xE0\xAE\xAE\xE0\xAF\x8D\n");
    writeFile(folder / "custom.aff", "SET UTF-8\nTRY \xCE\xB1\xCE\xB5\xCE\xB9\n");
    writeFile(folder / "custom.dic", "1\n\xCE\xB3\xCE\xB5\xCE\xB9\xCE\xAC\n");
    writeFile(checker->userDictionary(), "\xEF\xBB\xBFHikari\n\xEC\x84\x9C\xEC\x9A\xB8");
    std::mutex mutex;
    std::vector<std::string> loaded;
    SpellChecker multi(folder, [&, inner = fakes::FakeSpelling::loader()](const fs::path &aff, const fs::path &dic) {
        const std::lock_guard lock(mutex);
        loaded.push_back(utf8(dic.stem().u8string()));
        return inner(aff, dic);
    });
    ASSERT_EQ(multi.initialize(u"en_US"), SpellChecker::Status::Ready);
    EXPECT_EQ(loaded, (std::vector<std::string>{"en_US"}));
    EXPECT_TRUE(multi.checkWord(u"Hello", true));
    EXPECT_TRUE(multi.checkWord(u"kota", true));
    EXPECT_TRUE(multi.checkWord(u"Hikari", true));
    EXPECT_FALSE(multi.checkWord(u"wrold", true));
    EXPECT_FALSE(multi.checkWord(u"kotta", true));
    std::ranges::sort(loaded);
    EXPECT_EQ(loaded, (std::vector<std::string>{"en_US", "pl"}));
    EXPECT_TRUE(multi.checkWord(u"안녕하세요", true));
    EXPECT_TRUE(multi.checkWord(u"서울", true));
    EXPECT_FALSE(multi.checkWord(u"세게계", true));
    EXPECT_TRUE(multi.checkWord(u"வணக்கம்", true));
    EXPECT_FALSE(multi.checkWord(u"வணக்கமம்", true));
    EXPECT_TRUE(multi.checkWord(u"γειά", true));
    EXPECT_FALSE(multi.checkWord(u"γεια", true));
    EXPECT_EQ(loaded.size(), 5u);
    EXPECT_TRUE(multi.checkWord(u"привет", true));
    EXPECT_TRUE(multi.checkWord(u"こんにちは", true));
    EXPECT_TRUE(multi.checkWord(u"Привет세계", true));
    EXPECT_FALSE(multi.checkWord(u"Seoul세게계", true));
    EXPECT_TRUE(multi.checkWord(u"123", true));
    EXPECT_FALSE(multi.checkWord(u"١wrold", true));
    EXPECT_FALSE(multi.checkWord(u"", true));
    EXPECT_TRUE(multi.addWord(u"세게계"));
    EXPECT_TRUE(multi.checkWord(u"세게계", true));
    EXPECT_TRUE(multi.removeWords({u"세게계"}));
    EXPECT_FALSE(multi.checkWord(u"세게계", true));
    ASSERT_EQ(multi.initialize(u"pl"), SpellChecker::Status::Ready);
    EXPECT_TRUE(multi.checkWord(u"Hello", true));
    EXPECT_TRUE(multi.checkWord(u"안녕하세요", true));
    EXPECT_FALSE(multi.checkWord(u"wrold", true));
}

TEST_F(Spelling, MultilingualMarksWaitForNoLoad)
{
    writeFile(folder / "ko_KR.aff", "SET UTF-8\n");
    writeFile(folder / "ko_KR.dic", "1\n\xEC\x84\xB8\xEA\xB3\x84\n");
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    std::promise<void> loadedOnce;
    std::atomic<int> handled = 0;
    SpellChecker multi(folder, [&, inner = fakes::FakeSpelling::loader()](const fs::path &aff, const fs::path &dic) {
        if (dic.stem() == "ko_KR")
            released.wait();
        return inner(aff, dic);
    });
    multi.setLoadedHandler([&] {
        if (++handled == 1)
            loadedOnce.set_value();
    });
    ASSERT_EQ(multi.initialize(u"en_US"), SpellChecker::Status::Ready);
    EXPECT_TRUE(multi.checkWord(u"세게계"));
    EXPECT_TRUE(multi.checkWord(u"세게계"));
    EXPECT_FALSE(multi.checkWord(u"wrold"));
    EXPECT_EQ(handled, 0);
    release.set_value();
    ASSERT_EQ(loadedOnce.get_future().wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_FALSE(multi.checkWord(u"세게계"));
    EXPECT_TRUE(multi.checkWord(u"세계"));
    EXPECT_EQ(handled, 1);
}

TEST_F(Spelling, MultilingualSuggestionsFollowThePrimaryThenTheNeighbours)
{
    writeFile(folder / "en_US.dic", "3\nHello\nthe\ncot\n");
    writeFile(folder / "pl.aff", "");
    writeFile(folder / "pl.dic", "5\nAla\nma\nkot\nkota\ncot\n");
    writeFile(folder / "ko_KR.aff", "");
    writeFile(folder / "ko_KR.dic", "1\n\xEC\x84\xB8\xEA\xB3\x84\n");
    const std::vector<std::u16string> polish{u"Ala", u"ma", u"kott"};
    const std::vector<std::u16string> english{u"Hello", u"the", u"kott"};
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EXPECT_EQ(names(checker->suggestions(u"kott", polish)), (std::vector<std::string>{"cot", "kot", "kota"}));
    EXPECT_EQ(names(checker->suggestions(u"kott")), (std::vector<std::string>{"cot", "kot", "kota"}));
    ASSERT_EQ(checker->initialize(u"pl"), SpellChecker::Status::Ready);
    EXPECT_EQ(names(checker->suggestions(u"kott", english)), (std::vector<std::string>{"kot", "kota", "cot"}));
    ASSERT_EQ(checker->initialize(u"ko_KR"), SpellChecker::Status::Ready);
    EXPECT_EQ(names(checker->suggestions(u"kott", polish)), (std::vector<std::string>{"kot", "kota", "cot"}));
    EXPECT_EQ(names(checker->suggestions(u"kott", english)), (std::vector<std::string>{"cot", "kot", "kota"}));
    EXPECT_EQ(names(checker->suggestions(u"세게")), (std::vector<std::string>{"\xEC\x84\xB8\xEA\xB3\x84"}));
    EXPECT_EQ(names(lineWords(u"{\\i1}Ala ma kott, 42", core::SubtitleFormat::Ass, text)),
              (std::vector<std::string>{"Ala", "ma", "kott"}));

    core::Document document = load(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Ala ma kott\n");
    EditSession session(std::move(document));
    SpellCheckWalk walk(*checker, text);
    ASSERT_TRUE(walk.next(session, {}));
    EXPECT_EQ(names(walk.current()->suggestions), (std::vector<std::string>{"kot", "kota", "cot"}));
}

TEST_F(Spelling, WalkFindsIgnoresAndSkips)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EditSession session{load(std::string(kHeader) +
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello wrold\n"
                             "Comment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,cmnt\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,Teh WROLD is BAD\n"
                             "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,{\\i1}good{\\i0} txet\n")};
    const auto lines = session.document().lines();
    std::vector<core::LineId> ids;
    for (const auto *l : lines)
        ids.push_back(l->id);
    session.setSelection(Selection{ids[0], {ids[0]}, ids[0], {}});
    SpellCheckWalk walk(*checker, text);
    SpellCheckWalk::Options options;
    ASSERT_TRUE(walk.next(session, options));
    EXPECT_EQ(walk.current()->word, u"wrold");
    EXPECT_EQ(walk.current()->line, ids[0]);
    EXPECT_EQ(walk.current()->start, 6);
    EXPECT_EQ(walk.current()->end, 10);
    EXPECT_EQ(walk.current()->suggestions.front(), u"world");
    walk.ignore(session, options);
    EXPECT_EQ(walk.current()->word, u"cmnt");
    EXPECT_EQ(session.selection().active, ids[1]);
    EXPECT_EQ(session.selection().selected, std::set<core::LineId>{ids[1]});
    walk.ignore(session, options);
    EXPECT_EQ(walk.current()->word, u"Teh");
    walk.ignoreAll(session, u"wrold", options);
    EXPECT_EQ(walk.current()->word, u"Teh");
    walk.ignoreAll(session, u"teh", options);
    EXPECT_EQ(walk.current()->word, u"BAD");
    walk.ignore(session, options);
    EXPECT_EQ(walk.current()->word, u"txet");
    EXPECT_EQ(walk.current()->start, 15);
    walk.ignore(session, options);
    EXPECT_FALSE(walk.current());
    EXPECT_EQ(session.historySize(), 1u);

    session.setSelection(Selection{ids[0], {ids[0]}, ids[0], {}});
    SpellCheckWalk fresh(*checker, text);
    options = {.ignoreComments = true, .ignoreUpperCase = true};
    ASSERT_TRUE(fresh.next(session, options));
    fresh.ignore(session, options);
    EXPECT_EQ(fresh.current()->word, u"Teh");
    fresh.ignore(session, options);
    EXPECT_EQ(fresh.current()->word, u"txet");
}

TEST_F(Spelling, ReplaceIsOneStepAndSkipsTheSameOffset)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EditSession session{load(std::string(kHeader) +
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Teh wo{\\i1}rdd\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,xyz txet\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,abc\n")};
    const auto first = session.document().lines()[0]->id;
    session.setSelection(Selection{first, {first}, first, {}});
    SpellCheckWalk walk(*checker, text);
    const SpellCheckWalk::Options options;
    ASSERT_TRUE(walk.next(session, options));
    EXPECT_EQ(walk.replace(session, u"", options), SpellCheckWalk::Result::Unchanged);
    ASSERT_EQ(walk.replace(session, u"The", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(session.historySize(), 2u);
    EXPECT_EQ(session.history().back().name, "Correcting spelling errors");
    EXPECT_EQ(utf8(session.document().lines()[0]->text), "The wo{\\i1}rdd");
    EXPECT_EQ(walk.current()->word, u"wordd");
    ASSERT_EQ(walk.replace(session, u"world", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(utf8(session.document().lines()[0]->text), "The wo{\\i1}rld");
    EXPECT_EQ(walk.current()->word, u"xyz");
    ASSERT_EQ(walk.replace(session, u"qqq", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(walk.current()->word, u"txet");
    ASSERT_EQ(walk.replace(session, u"text", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(walk.current()->word, u"abc");
    EXPECT_EQ(session.historySize(), 5u);

    EditSession two{load(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,qqq\n"
                                                "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,abc def\n")};
    const auto top = two.document().lines()[0]->id;
    two.setSelection(Selection{top, {top}, top, {}});
    SpellCheckWalk quirk(*checker, text);
    ASSERT_TRUE(quirk.next(two, options));
    ASSERT_EQ(quirk.replace(two, u"good", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(quirk.current()->word, u"def");
}

TEST_F(Spelling, ReplaceAllKeepsCaseAndTranslationQuirk)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EditSession session{load(std::string(kHeader) +
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,wrold Wrold WROLD\n"
                             "Comment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,wrold\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,{wrold} wroldly\n")};
    const auto first = session.document().lines()[0]->id;
    session.setSelection(Selection{first, {first}, first, {}});
    SpellCheckWalk walk(*checker, text);
    SpellCheckWalk::Options options{.ignoreComments = true};
    ASSERT_TRUE(walk.next(session, options));
    ASSERT_EQ(walk.replaceAll(session, u"wrold", u"WoRlD", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(session.historySize(), 2u);
    EXPECT_EQ(session.history().back().name, "Correcting spelling errors");
    const auto lines = session.document().lines();
    EXPECT_EQ(utf8(lines[0]->text), "world World WORLD");
    EXPECT_EQ(utf8(lines[1]->text), "wrold");
    EXPECT_EQ(utf8(lines[2]->text), "{wrold} wroldly");
    EXPECT_EQ(walk.replaceAll(session, u"zzz", u"x", options), SpellCheckWalk::Result::NothingReplaced);
    EXPECT_EQ(walk.replaceAll(session, u"", u"x", options), SpellCheckWalk::Result::Unchanged);
    EXPECT_EQ(session.historySize(), 2u);

    EditSession tl{load("[Script Info]\nTLMode: Yes\n\n" + std::string(kHeader) +
                        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,wrold\n")};
    const auto line = tl.document().lines()[0]->id;
    tl.setSelection(Selection{line, {line}, line, {}});
    ASSERT_TRUE(spellsTranslation(tl.document()));
    SpellCheckWalk tlWalk(*checker, text);
    EXPECT_FALSE(tlWalk.next(tl, options));
    ASSERT_EQ(tlWalk.replaceAll(tl, u"wrold", u"world", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(utf8(tl.document().lines()[0]->text), "wrold");
    EXPECT_EQ(utf8(tl.document().lines()[0]->translation), "world");
}

TEST_F(Spelling, AddWordAndActivation)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EditSession session{load(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,aaa bbb\n"
                                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,ccc\n")};
    const auto lines = session.document().lines();
    const auto first = lines[0]->id, second = lines[1]->id;
    session.setSelection(Selection{first, {first}, first, {}});
    SpellCheckWalk walk(*checker, text);
    const SpellCheckWalk::Options options;
    ASSERT_TRUE(walk.next(session, options));
    EXPECT_TRUE(walk.addWord(session, u"aaa", options));
    EXPECT_EQ(walk.current()->word, u"bbb");
    EXPECT_EQ(names(checker->addedWords()), (std::vector<std::string>{"aaa"}));
    EXPECT_FALSE(walk.activated(session, options));
    session.setSelection(Selection{second, {second}, second, {}});
    EXPECT_TRUE(walk.activated(session, options));
    EXPECT_EQ(walk.current()->word, u"ccc");
    ASSERT_TRUE(session.run(Command{"Line editing", session.revision(), {second}, [&](core::Document &d) {
                            return d.editLine(second, [](core::LineRecord &l) { l.text = u8"ddd ccc"; });
                        }}));
    EXPECT_TRUE(walk.activated(session, options));
    EXPECT_EQ(walk.current()->word, u"ddd");
    walk.ignore(session, options);
    walk.ignore(session, options);
    EXPECT_FALSE(walk.current());
    EXPECT_FALSE(walk.activated(session, options));
    session.setSelection(Selection{first, {first}, first, {}});
    EXPECT_TRUE(walk.activated(session, options));
    EXPECT_EQ(walk.current()->word, u"bbb");
}

TEST_F(Spelling, EditorReplacementCommitsTheDraft)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EditSession session{load(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,good\n")};
    const auto line = session.document().lines()[0]->id;
    session.setSelection(Selection{line, {line}, line, {}});
    ASSERT_TRUE(session.editDraft(line, DraftChange{.text = u8"good wrold}", .start = core::DocumentTime(1500000)}));
    const auto marks = editorMarks(u"good wrold}", core::SubtitleFormat::Ass, *checker, text);
    EXPECT_EQ(marks.errors, (std::vector<int>{10, 10, 5, 9}));
    EXPECT_EQ(misspellAt(marks, 9), std::optional<std::size_t>(1));
    EXPECT_EQ(misspellAt(marks, 10), std::nullopt);
    EXPECT_TRUE(editorMarks(u"", core::SubtitleFormat::Ass, *checker, text).errors.empty());
    const auto caret = replaceInEditor(session, line, false, marks.misspells[1], u"world");
    ASSERT_TRUE(caret);
    EXPECT_EQ(*caret, 10);
    EXPECT_EQ(session.historySize(), 2u);
    EXPECT_EQ(session.history().back().name, "Correcting spelling errors in the text field");
    EXPECT_EQ(utf8(session.document().lines()[0]->text), "good world}");
    EXPECT_EQ(session.document().lines()[0]->start.value.microseconds(), 1500000);
    EXPECT_FALSE(session.draftLine());
}

TEST_F(Spelling, GrammarReplacementKeepsTagsAndRefusesStaleText)
{
    EditSession session{load(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,I has a apple\n")};
    const auto line = session.document().lines()[0]->id;
    session.setSelection(Selection{line, {line}, line, {}});
    const auto refusal = [](const std::expected<std::size_t, CommandRefusal> &r) {
        return r ? std::nullopt : std::optional(r.error());
    };
    const std::u16string checked = uR"(I {\i1}has{\i0} a apple)";
    ASSERT_TRUE(session.editDraft(line, DraftChange{.text = core::toUtf8(checked), .start = core::DocumentTime(1500000)}));
    EXPECT_EQ(refusal(replaceGrammar(session, line, false, u"I has a apple", 2, 5, u"have")), CommandRefusal::StaleRevision);
    EXPECT_EQ(refusal(replaceGrammar(session, line, true, checked, 2, 5, u"have")), CommandRefusal::StaleRevision);
    const auto caret = replaceGrammar(session, line, false, checked, 2, 5, u"have");
    ASSERT_TRUE(caret);
    EXPECT_EQ(*caret, 11u);
    EXPECT_EQ(session.historySize(), 2u);
    EXPECT_EQ(session.history().back().name, "Correcting grammar errors in the text field");
    EXPECT_EQ(utf8(session.document().lines()[0]->text), R"(I {\i1}have{\i0} a apple)");
    EXPECT_EQ(session.document().lines()[0]->start.value.microseconds(), 1500000);
    EXPECT_FALSE(session.draftLine());

    const std::u16string drawing = uR"(a {\p1}m 0 0{\p0}b)";
    ASSERT_TRUE(session.editDraft(line, DraftChange{.text = core::toUtf8(drawing)}));
    ASSERT_TRUE(session.commitDraftAs("Typing"));
    EXPECT_EQ(refusal(replaceGrammar(session, line, false, drawing, 1, 3, u"x")), CommandRefusal::Invalid);
    EXPECT_EQ(utf8(session.document().lines()[0]->text), R"(a {\p1}m 0 0{\p0}b)");
}

TEST_F(Spelling, WalkReachesHiddenLines)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EditSession session{load(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,good\n"
                                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,wrold\n")};
    const auto lines = session.document().lines();
    const auto first = lines[0]->id, hidden = lines[1]->id;
    ASSERT_TRUE(session.run(Command{"Filtering", session.revision(), {hidden}, [&](core::Document &d) {
                            return d.editLine(hidden, [](core::LineRecord &l) { l.visibility = core::LineVisibility::Hidden; });
                        }}));
    session.setSelection(Selection{first, {first}, first, {}});
    SpellCheckWalk walk(*checker, text);
    const SpellCheckWalk::Options options;
    ASSERT_TRUE(walk.next(session, options));
    EXPECT_EQ(walk.current()->word, u"wrold");
    EXPECT_EQ(session.selection().active, hidden);
    EXPECT_EQ(session.selection().selected, std::set<core::LineId>{hidden});
}

TEST_F(Spelling, WalkRefusedByABlockedDraft)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EditSession session{load(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,good\n"
                                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,wrold\n")};
    const auto lines = session.document().lines();
    const auto first = lines[0]->id;
    session.setSelection(Selection{first, {first}, first, {}});
    ASSERT_TRUE(session.editDraft(first, DraftChange{.end = core::DocumentTime(0)}));
    ASSERT_TRUE(session.draftProblem());
    SpellCheckWalk walk(*checker, text);
    const SpellCheckWalk::Options options;
    EXPECT_FALSE(walk.next(session, options));
    EXPECT_TRUE(walk.refused());
    EXPECT_FALSE(walk.current());
    EXPECT_EQ(session.selection().active, first);
    EXPECT_EQ(walk.replace(session, u"world", options), SpellCheckWalk::Result::Unchanged);
    EXPECT_FALSE(walk.activated(session, options));
    session.discardDraft();
    EXPECT_TRUE(walk.activated(session, options));
    EXPECT_FALSE(walk.refused());
    EXPECT_EQ(walk.current()->word, u"wrold");
    EXPECT_EQ(session.selection().active, lines[1]->id);
}

TEST_F(Spelling, StaleWordRestarts)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EditSession session{load(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,good wrold\n")};
    const auto line = session.document().lines()[0]->id;
    session.setSelection(Selection{line, {line}, line, {}});
    SpellCheckWalk walk(*checker, text);
    const SpellCheckWalk::Options options;
    ASSERT_TRUE(walk.next(session, options));
    EXPECT_FALSE(walk.stale(session));
    EXPECT_TRUE(walk.stale(session, true));
    ASSERT_TRUE(session.editDraft(line, DraftChange{.text = u8"xyz good wrold"}));
    EXPECT_FALSE(walk.stale(session));
    ASSERT_TRUE(session.commitDraft());
    EXPECT_TRUE(walk.stale(session));
    walk.restart(session, options);
    EXPECT_EQ(walk.current()->word, u"xyz");
    EXPECT_EQ(walk.current()->start, 0);
    EXPECT_FALSE(walk.stale(session));
}
