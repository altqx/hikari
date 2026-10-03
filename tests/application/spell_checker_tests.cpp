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

std::vector<std::string> names(const std::vector<std::u16string> &words)
{
    std::vector<std::string> out;
    for (const auto &w : words)
        out.push_back(utf8(core::toUtf8(w)));
    return out;
}

} // namespace

// AvailableDics pairs the n-th .dic with the n-th .aff of the listing: a
// .dic without its .aff shifts every later pair out of step.
TEST_F(Spelling, AvailableDictionariesPairByPosition)
{
    writeFile(folder / "pl.dic", "1\nkot\n");
    writeFile(folder / "pl.aff", "");
    writeFile(folder / "readme.txt", "");
    EXPECT_EQ(names(availableDictionaries(folder)), (std::vector<std::string>{"en_US", "pl"}));
    writeFile(folder / "de.dic", "1\nHaus\n");
    EXPECT_TRUE(availableDictionaries(folder).empty());
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
// alone for a new file); RemoveWords rewrites the remaining lines with CRLF.
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
    // The next AddWord reads the CRLF as LF (text mode) and appends "\n" + word.
    EXPECT_TRUE(checker->addWord(u"zzz"));
    EXPECT_EQ(readFile(checker->userDictionary()), "\xEF\xBB\xBFGda\xC5\x84sk\n\nzzz");
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
    EXPECT_EQ(walk.replace(session, u"", options), false);
    ASSERT_EQ(walk.replace(session, u"The", options), true);
    EXPECT_EQ(session.historySize(), 2u);
    EXPECT_EQ(session.history().back().name, "Correcting spelling errors");
    EXPECT_EQ(utf8(session.document().lines()[0]->text), "The wo{\\i1}rdd");
    EXPECT_EQ(walk.current()->word, u"wordd");
    // The block stays inside the corrected word.
    ASSERT_EQ(walk.replace(session, u"world", options), true);
    EXPECT_EQ(utf8(session.document().lines()[0]->text), "The wo{\\i1}rld");
    EXPECT_EQ(walk.current()->word, u"xyz");
    // "xyz" -> "qqq" is still misspelled at offset 0: skipped, on to "txet".
    ASSERT_EQ(walk.replace(session, u"qqq", options), true);
    EXPECT_EQ(walk.current()->word, u"txet");
    // "txet" -> "text" is correct; the next word is "abc" at offset 0 on the
    // next Line, which is not txet's offset (4): found.
    ASSERT_EQ(walk.replace(session, u"text", options), true);
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
    ASSERT_EQ(quirk.replace(two, u"good", options), true);
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
    ASSERT_EQ(walk.replaceAll(session, u"wrold", u"WoRlD", options), true);
    EXPECT_EQ(session.historySize(), 2u);
    EXPECT_EQ(session.history().back().name, "Correcting spelling errors");
    const auto lines = session.document().lines();
    EXPECT_EQ(utf8(lines[0]->text), "world World WORLD");
    EXPECT_EQ(utf8(lines[1]->text), "wrold");
    EXPECT_EQ(utf8(lines[2]->text), "{wrold} wroldly");
    // Nothing matched: no step.
    EXPECT_EQ(walk.replaceAll(session, u"zzz", u"x", options), false);
    EXPECT_EQ(session.historySize(), 2u);

    EditSession tl{load("[Script Info]\nTLMode: Yes\n\n" + std::string(kHeader) +
                        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,wrold\n")};
    const auto line = tl.document().lines()[0]->id;
    tl.setSelection(Selection{line, {line}, line, {}});
    ASSERT_TRUE(spellsTranslation(tl.document()));
    SpellCheckWalk tlWalk(*checker, text);
    EXPECT_FALSE(tlWalk.next(tl, options)); // the empty translation is checked
    ASSERT_EQ(tlWalk.replaceAll(tl, u"wrold", u"world", options), true);
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
