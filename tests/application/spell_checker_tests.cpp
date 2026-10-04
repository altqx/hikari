// F3: the spell checker against legacy SpellChecker (AvailableDics,
// Initialize, CheckWord, AddWord, RemoveWords), SpellCheckerDialog
// (FindNextMisspell, SetNextMisspell, Replace, ReplaceAll, Ignore, IgnoreAll,
// AddWord, LoadAddedMisspels, OnActive) and the TextEditor's suggestion
// replacement (EditBox::Send(EDITBOX_SPELL_CHECKER)) at 20d647c4, over an
// in-memory fake of the Hunspell backend.

#include "hikari/application/spell_checker.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include "spelling/fake_spelling.h"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>
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
// Legacy's Linux AvailableDics: wxDir lists readdir's order, without hidden
// names or directories, and the n-th .dic pairs with the n-th .aff.
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
    for (std::size_t i = 0; i < dics.size() && i < affs.size(); ++i)
        if (dics[i] == affs[i])
            out.push_back(dics[i]);
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

} // namespace

// AvailableDics pairs the n-th .dic with the n-th .aff of the listing: a
// .dic without its .aff shifts every later pair out of step. Legacy then
// reads past the shorter .aff list (undefined); the pairing stops at its end
// (R3-hang-crash-loss). The listing is the file system's own order on each
// platform (R5-per-platform): NTFS's upper-cased order on Windows, readdir's
// on Linux; directories and hidden entries are not listed.
TEST_F(Spelling, AvailableDictionariesPairByPosition)
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
    EXPECT_TRUE(availableDictionaries(folder).empty());
#else
    writeFile(folder / ".hidden.dic", "1\nx\n");
    writeFile(folder / ".hidden.aff", "");
    const auto listed = names(availableDictionaries(folder));
    EXPECT_EQ(listed, readdirPairs(folder));
    EXPECT_TRUE(std::ranges::find(listed, ".hidden") == listed.end());
    writeFile(folder / "de.dic", "1\nHaus\n");
    EXPECT_EQ(names(availableDictionaries(folder)), readdirPairs(folder));
    // Case matters on Linux: "*.dic" does not match EN.DIC.
    writeFile(folder / "EN.DIC", "");
    writeFile(folder / "EN.AFF", "");
    EXPECT_EQ(names(availableDictionaries(folder)), readdirPairs(folder));
#endif
    EXPECT_TRUE(availableDictionaries(folder / "missing").empty());
}

// Initialize: DICTIONARY_LANGUAGE's pair (en_US when empty), then the user
// dictionary's words; without the pair, or without a backend, nothing loads.
TEST_F(Spelling, InitializeLoadsTheUserDictionary)
{
    writeFile(checker->userDictionary(), "\xEF\xBB\xBFkotek\r\n123\r\n\r\nzzz \r\n");
    EXPECT_EQ(checker->initialize(u"pl"), SpellChecker::Status::NoDictionary);
    EXPECT_FALSE(checker->ready());
    EXPECT_TRUE(checker->checkWord(u"wrold")); // spell checking off: every word is correct
    EXPECT_EQ(checker->initialize(u""), SpellChecker::Status::Ready);
    ASSERT_TRUE(backend);
    EXPECT_EQ(names(backend->added), (std::vector<std::string>{"kotek", "zzz"}));
    EXPECT_TRUE(checker->checkWord(u"kotek"));
    EXPECT_FALSE(checker->checkWord(u"wrold"));
    EXPECT_EQ(checker->suggestions(u"wrold").front(), u"world");
    SpellChecker noBackend(folder, {});
    EXPECT_EQ(noBackend.initialize(u"en_US"), SpellChecker::Status::LoadFailed);
}

// AddWord appends "\n" + word to UserDic.udic (UTF-8 with a BOM, the word
// alone for a new or empty file); RemoveWords rewrites the remaining lines
// with CRLF.
TEST_F(Spelling, UserWordsPersist)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EXPECT_FALSE(checker->addWord(u""));
    EXPECT_FALSE(checker->addWord(u"-12"));
    EXPECT_FALSE(checker->checkWord(u"wrold"));
    EXPECT_TRUE(checker->addWord(u"wrold"));
    EXPECT_TRUE(checker->checkWord(u"wrold")); // the cached result was dropped
    EXPECT_TRUE(checker->addWord(u"Gdańsk"));
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBFwrold\nGda\xC5\x84sk");
    EXPECT_EQ(names(checker->addedWords()), (std::vector<std::string>{"wrold", "Gda\xC5\x84sk"}));
    EXPECT_FALSE(checker->removeWords({u"nothing"}));
    EXPECT_TRUE(checker->removeWords({u"wrold"}));
    EXPECT_EQ(names(backend->removed), (std::vector<std::string>{"wrold"}));
    EXPECT_FALSE(checker->checkWord(u"wrold"));
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBFGda\xC5\x84sk\r\n");
    // The next AddWord appends "\n" + word to the text as read: Windows text
    // mode reads the CRLF as LF, the Linux build keeps "\r" (R5-per-platform).
    EXPECT_TRUE(checker->addWord(u"zzz"));
#ifdef _WIN32
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBFGda\xC5\x84sk\n\nzzz");
#else
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBFGda\xC5\x84sk\r\n\nzzz");
#endif
    // Removing every word leaves a BOM-only file; FileOpen reads it as a
    // failure, so the next AddWord writes the word alone.
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

// UserDic.udic is read as each legacy build read it (R5-per-platform):
// Windows text mode folds CR LF byte pairs and stops at Ctrl+Z, before
// decoding; Linux keeps every byte, so a "\r"-only line is a blank entry in
// the removal list there (wxStringTokenizer skips only empty tokens).
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
    // A UTF-16 file: the CRT folds bytes, so "\r\0\n\0" stays as it is.
    writeFile(checker->userDictionary(), std::string("\xFF\xFE" "a\0\r\0\n\0b\0", 10));
    EXPECT_EQ(readUserDictionary(checker->userDictionary()), std::u16string(u"a\r\nb"));
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EXPECT_EQ(names(backend->added), (std::vector<std::string>{"a", "b"}));
}

// Two Dictionary folders: the user's (UserDic.udic) first, then the bundled
// one beside the executable. A language loads from the first folder holding
// both files; the list names each symbol once.
TEST_F(Spelling, DictionaryFoldersInSearchOrder)
{
    const fs::path bundled = folder / "bundled";
    fs::create_directories(bundled);
    writeFile(bundled / "en_US.aff", "");
    writeFile(bundled / "en_US.dic", "1\nbundled\n");
    writeFile(bundled / "pl.aff", "");
    writeFile(bundled / "pl.dic", "1\nkot\n");
    EXPECT_EQ(names(availableDictionaries(std::vector<fs::path>{folder, bundled})),
              (std::vector<std::string>{"en_US", "pl"}));
    SpellChecker both(std::vector<fs::path>{folder, bundled}, fakes::FakeSpelling::loader(&backend));
    ASSERT_EQ(both.initialize(u"pl"), SpellChecker::Status::Ready);
    EXPECT_TRUE(both.checkWord(u"kot"));
    ASSERT_EQ(both.initialize(u"en_US"), SpellChecker::Status::Ready);
    EXPECT_TRUE(both.checkWord(u"Hello")); // the user's folder first
    EXPECT_FALSE(both.checkWord(u"bundled"));
    EXPECT_EQ(both.userDictionary(), folder / "UserDic.udic");
    EXPECT_TRUE(both.addWord(u"Hikari"));
    EXPECT_TRUE(fs::exists(folder / "UserDic.udic"));
    EXPECT_FALSE(fs::exists(bundled / "UserDic.udic"));
    EXPECT_EQ(both.initialize(u"de"), SpellChecker::Status::NoDictionary);
}

// The Spellchecker window walks from the active Line; the found word's Line
// becomes active and selected; Ignore skips one word, Ignore all every
// occurrence in any case; comments and upper-case words can be skipped.
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
    walk.ignoreAll(session, u"wrold", options); // also "WROLD"
    EXPECT_EQ(walk.current()->word, u"Teh");     // the current word is still the first unignored
    walk.ignoreAll(session, u"teh", options);
    EXPECT_EQ(walk.current()->word, u"BAD");
    walk.ignore(session, options);
    EXPECT_EQ(walk.current()->word, u"txet");
    EXPECT_EQ(walk.current()->start, 15); // raw offsets around the blocks
    walk.ignore(session, options);
    EXPECT_FALSE(walk.current());
    EXPECT_EQ(session.historySize(), 1u);

    // From the first Line again with the options: comments and BAD are skipped.
    session.setSelection(Selection{ids[0], {ids[0]}, ids[0], {}});
    SpellCheckWalk fresh(*checker, text);
    options = {.ignoreComments = true, .ignoreUpperCase = true};
    ASSERT_TRUE(fresh.next(session, options));
    fresh.ignore(session, options);
    EXPECT_EQ(fresh.current()->word, u"Teh");
    fresh.ignore(session, options);
    EXPECT_EQ(fresh.current()->word, u"txet");
}

// Replace writes the current word's replacement as one "Correcting spelling
// errors" step and goes on; a replacement still misspelled at the same
// offset is skipped (legacy compares the offset only, so a word at that
// offset in the next Line is skipped too).
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
    // The block stays inside the corrected word.
    ASSERT_EQ(walk.replace(session, u"world", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(utf8(session.document().lines()[0]->text), "The wo{\\i1}rld");
    EXPECT_EQ(walk.current()->word, u"xyz");
    // "xyz" -> "qqq" is still misspelled at offset 0: skipped, on to "txet".
    ASSERT_EQ(walk.replace(session, u"qqq", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(walk.current()->word, u"txet");
    // "txet" -> "text" is correct; the next word is "abc" at offset 0 on the
    // next Line, which is not txet's offset (4): found.
    ASSERT_EQ(walk.replace(session, u"text", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(walk.current()->word, u"abc");
    EXPECT_EQ(session.historySize(), 5u);

    // The offset quirk: Line 0's "qqq" corrected to "good" moves on to Line
    // 1's "abc", also at offset 0, which is skipped as if unchanged.
    EditSession two{load(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,qqq\n"
                                                "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,abc def\n")};
    const auto top = two.document().lines()[0]->id;
    two.setSelection(Selection{top, {top}, top, {}});
    SpellCheckWalk quirk(*checker, text);
    ASSERT_TRUE(quirk.next(two, options));
    ASSERT_EQ(quirk.replace(two, u"good", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(quirk.current()->word, u"def");
}

// Replace all: every Line's matches in any case, each in its own case
// (GetRightCase); one step; in translation mode the result goes to the
// translation even when it was read from an untranslated Line's text.
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
    // Nothing matched: no step.
    EXPECT_EQ(walk.replaceAll(session, u"zzz", u"x", options), SpellCheckWalk::Result::NothingReplaced);
    EXPECT_EQ(walk.replaceAll(session, u"", u"x", options), SpellCheckWalk::Result::Unchanged);
    EXPECT_EQ(session.historySize(), 2u);

    EditSession tl{load("[Script Info]\nTLMode: Yes\n\n" + std::string(kHeader) +
                        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,wrold\n")};
    const auto line = tl.document().lines()[0]->id;
    tl.setSelection(Selection{line, {line}, line, {}});
    ASSERT_TRUE(spellsTranslation(tl.document()));
    SpellCheckWalk tlWalk(*checker, text);
    EXPECT_FALSE(tlWalk.next(tl, options)); // the empty translation is checked
    ASSERT_EQ(tlWalk.replaceAll(tl, u"wrold", u"world", options), SpellCheckWalk::Result::Replaced);
    EXPECT_EQ(utf8(tl.document().lines()[0]->text), "wrold");
    EXPECT_EQ(utf8(tl.document().lines()[0]->translation), "world");
}

// Add to dictionary persists the word and goes on; OnActive starts again
// when the active Line or its text changed, but not right after "No
// spelling errors were found".
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
    EXPECT_FALSE(walk.activated(session, options)); // nothing changed
    // The active Line moved: start again there.
    session.setSelection(Selection{second, {second}, second, {}});
    EXPECT_TRUE(walk.activated(session, options));
    EXPECT_EQ(walk.current()->word, u"ccc");
    // Its text changed: start again from its first word.
    ASSERT_TRUE(session.run(Command{"Line editing", session.revision(), {second}, [&](core::Document &d) {
                            return d.editLine(second, [](core::LineRecord &l) { l.text = u8"ddd ccc"; });
                        }}));
    EXPECT_TRUE(walk.activated(session, options));
    EXPECT_EQ(walk.current()->word, u"ddd");
    walk.ignore(session, options);
    walk.ignore(session, options);
    EXPECT_FALSE(walk.current());
    EXPECT_FALSE(walk.activated(session, options)); // the message's own activation
    session.setSelection(Selection{first, {first}, first, {}});
    EXPECT_TRUE(walk.activated(session, options));
    EXPECT_EQ(walk.current()->word, u"bbb");
}

// The editor: marks of its spell-checked field, the misspelling under the
// caret (inclusive span), and a chosen suggestion committing the draft and
// the replacement as one "Correcting spelling errors in the text field" step.
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
    EXPECT_EQ(misspellAt(marks, 10), std::nullopt); // a bracket error has no span
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

// The walk goes over every Line of the Document, hidden ones (filtered or in
// a closed group) included, as legacy walks the file's keys; the found Line
// becomes active and selected.
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

// An invalid draft under the Block policy keeps its Line: the walk does not
// move the selection, shows no word and says it was refused; the window's
// next activation is its message's own and is ignored.
TEST_F(Spelling, WalkRefusedByABlockedDraft)
{
    ASSERT_EQ(checker->initialize(u"en_US"), SpellChecker::Status::Ready);
    EditSession session{load(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,good\n"
                                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,wrold\n")};
    const auto lines = session.document().lines();
    const auto first = lines[0]->id;
    session.setSelection(Selection{first, {first}, first, {}});
    ASSERT_TRUE(session.editDraft(first, DraftChange{.end = core::DocumentTime(0)})); // ends before it starts
    ASSERT_TRUE(session.draftProblem());
    SpellCheckWalk walk(*checker, text);
    const SpellCheckWalk::Options options;
    EXPECT_FALSE(walk.next(session, options));
    EXPECT_TRUE(walk.refused());
    EXPECT_FALSE(walk.current());
    EXPECT_EQ(session.selection().active, first);
    EXPECT_EQ(walk.replace(session, u"world", options), SpellCheckWalk::Result::Unchanged);
    EXPECT_FALSE(walk.activated(session, options));
    // Once the draft is fixed the next activation starts again.
    session.discardDraft();
    EXPECT_TRUE(walk.activated(session, options));
    EXPECT_FALSE(walk.refused());
    EXPECT_EQ(walk.current()->word, u"wrold");
    EXPECT_EQ(session.selection().active, lines[1]->id);
}

// stale(): the active Line or its text is no longer the one the word was
// found in; restart() starts again from the active Line's first word.
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
    EXPECT_TRUE(walk.stale(session, true)); // another Document
    ASSERT_TRUE(session.editDraft(line, DraftChange{.text = u8"xyz good wrold"}));
    EXPECT_FALSE(walk.stale(session)); // a pending draft is not the Line's text yet
    ASSERT_TRUE(session.commitDraft());
    EXPECT_TRUE(walk.stale(session));
    walk.restart(session, options);
    EXPECT_EQ(walk.current()->word, u"xyz");
    EXPECT_EQ(walk.current()->start, 0);
    EXPECT_FALSE(walk.stale(session));
}
