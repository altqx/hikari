// F1: find and replace against legacy FindReplace (findreplace.cpp),
// FindReplaceDialog's TabWindow and FindReplaceResultsDialog at 20d647c4.

#include "hikari/application/find_replace.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <cstring>
#include <deque>
#include <functional>
#include <set>
#include <map>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;
using S = FindReplaceSettings;
using K = FindQuestion::Kind;

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

std::string str(std::u8string_view s)
{
    return {s.begin(), s.end()};
}

std::string str(std::u16string_view s)
{
    return str(core::toUtf8(s));
}

std::u16string u16(std::string_view s)
{
    return core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}

constexpr std::string_view kHeader = "[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                                     "Format: Name, Fontname, Fontsize\nStyle: Default,Arial,20\nStyle: Sign,Arial,20\n\n"
                                     "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

constexpr std::string_view kLines = "Dialogue: 0,0:00:01.00,0:00:02.00,Default,Anna,0,0,0,,Hello hello\n"
                                    "Comment: 0,0:00:03.00,0:00:04.00,Sign,,0,0,0,,hello sign\n"
                                    "Dialogue: 0,0:00:05.00,0:00:06.00,Sign,Bob,0,0,0,fx,{\\i1}hello{\\b1}\n"
                                    "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,the end hello\n";

// The shell as a script: answers queued per question, files in memory.
struct Host : FindReplaceHost {
    struct Doc {
        DocumentId id;
        std::unique_ptr<EditSession> session;
        std::u8string name;
        std::u8string path;
    };
    std::vector<Doc> docs;
    std::size_t target = 0;
    std::vector<FindQuestion> questions;
    std::deque<FindAnswer> answers;
    std::vector<std::string> logs;
    struct Shown {
        std::uint64_t document;
        core::LineId line;
        bool keepSelection;
        int role, start, end;
    };
    std::vector<Shown> shown;
    LineVisible visible;
    // Files: path -> text as read (LF), the listing per filter, and what was written.
    std::map<std::u8string, std::u16string> files;
    std::vector<std::u8string> listed;
    bool folderValid = true;
    std::vector<std::u8string> backups;
    std::vector<std::pair<std::u8string, std::u16string>> writes;
    std::vector<std::u8string> opened;

    EditSession &add(std::string_view text, std::u8string name = u8"a.ass")
    {
        docs.push_back({DocumentId{docs.size() + 1}, std::make_unique<EditSession>(load(text)), name, {}});
        return *docs.back().session;
    }
    std::optional<FindTab> current() override
    {
        if (target >= docs.size())
            return std::nullopt;
        return FindTab{docs[target].id, docs[target].session.get(), docs[target].name};
    }
    std::vector<FindTab> tabs() override
    {
        std::vector<FindTab> out;
        for (auto &d : docs)
            out.push_back({d.id, d.session.get(), d.name});
        return out;
    }
    LineVisible actionLines(EditSession &) override { return visible; }
    // Answers at once from the queue (Ok when it is empty), or keeps the
    // question open when `hold` is set.
    bool hold = false;
    std::function<void(FindAnswer)> pending;
    void ask(const FindQuestion &q, std::function<void(FindAnswer)> answer) override
    {
        questions.push_back(q);
        if (!answer)
            return;
        if (hold) {
            pending = std::move(answer);
            return;
        }
        FindAnswer a = FindAnswer::Ok;
        if (!answers.empty()) {
            a = answers.front();
            answers.pop_front();
        }
        answer(a);
    }
    int finishedCount = 0;
    void finished() override { ++finishedCount; }
    void log(const std::u8string &text) override { logs.push_back(str(text)); }
    void showLine(DocumentId document, core::LineId line, bool keep, int role, int start, int end) override
    {
        for (std::size_t i = 0; i < docs.size(); ++i)
            if (docs[i].id == document)
                target = i;
        auto &s = *docs[target].session;
        Selection sel = s.selection();
        if (!keep)
            sel.selected = {line};
        sel.active = line;
        s.setSelection(sel);
        shown.push_back({document.value, line, keep, role, start, end});
    }
    std::optional<std::vector<std::u8string>> listFiles(const std::u8string &, const std::u8string &filter, bool,
                                                        bool) override
    {
        if (!folderValid)
            return std::nullopt;
        std::vector<std::u8string> out;
        const auto ext = filter.size() > 1 ? filter.substr(1) : std::u8string();
        for (const auto &p : listed)
            if (filter.empty() || filter == u8"*" || p.ends_with(ext))
                out.push_back(p);
        return out;
    }
    std::optional<std::u16string> readFile(const std::u8string &path) override
    {
        const auto it = files.find(path);
        if (it == files.end() || it->second.empty())
            return std::nullopt;
        return it->second;
    }
    bool fileExists(const std::u8string &path) override { return files.contains(path); }
    std::set<std::u8string> unwritable;
    bool backupFile(const std::u8string &path) override
    {
        backups.push_back(path);
        return true;
    }
    bool writeFile(const std::u8string &path, const std::u16string &text) override
    {
        if (unwritable.contains(path))
            return false;
        writes.emplace_back(path, text);
        files[path] = text;
        return true;
    }
    std::optional<DocumentId> openFile(const std::u8string &path) override
    {
        opened.push_back(path);
        return std::nullopt;
    }
};

struct Find : ::testing::Test {
    Host host;
    EditSession *session = nullptr;
    FindReplace fr{host};
    S window;

    void SetUp() override
    {
        session = &host.add(std::string(kHeader) + std::string(kLines));
        window.tab = S::Tab::Find;
    }
    core::LineId row(std::size_t i) const { return session->document().lines()[i]->id; }
    std::size_t activeRow() const
    {
        const auto lines = session->document().lines();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (session->selection().active == lines[i]->id)
                return i;
        return 99;
    }
    std::vector<std::string> texts() const
    {
        std::vector<std::string> out;
        for (const auto *l : session->document().lines())
            out.push_back(str(l->text));
        return out;
    }
    void select(std::set<std::size_t> rows, std::size_t active)
    {
        std::set<core::LineId> ids;
        for (auto r : rows)
            ids.insert(row(r));
        session->setSelection(Selection{row(active), ids, row(active), {}});
    }
    // The last match: row, editor and range.
    std::tuple<std::size_t, int, int, int> last() const
    {
        const auto &s = host.shown.back();
        const auto lines = session->document().lines();
        std::size_t r = 99;
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i]->id == s.line)
                r = i;
        return {r, s.role, s.start, s.end};
    }
};

using Match = std::tuple<std::size_t, int, int, int>;

} // namespace

TEST(FindReplaceOptions, ReadAsTheConstructorAndSetValuesDo)
{
    // Of several field or Lines bits the last radio button set wins.
    S s = findReplaceFromOptions(16 | 64 | 1024 | 2048 | 1 | 2 | (1 << 14) | (1 << 16));
    EXPECT_EQ(s.field, S::Field::Style);
    EXPECT_EQ(s.lines, S::Lines::FromSelection);
    EXPECT_TRUE(s.matchCase);
    EXPECT_TRUE(s.regex);
    EXPECT_TRUE(s.includeComments);
    EXPECT_FALSE(s.skipTags);
    EXPECT_TRUE(s.skipText);
    // None: the first button of each group (Text, All lines).
    s = findReplaceFromOptions(0);
    EXPECT_EQ(s.field, S::Field::Text);
    EXPECT_EQ(s.lines, S::Lines::All);
    // The constructor puts the hidden-folders bit into "Search in subfolders"
    // and leaves "Search in hidden folders" unchecked.
    s = findReplaceFromOptions(4096);
    EXPECT_FALSE(s.subfolders);
    s = findReplaceFromOptions(8192);
    EXPECT_TRUE(s.subfolders);
    EXPECT_FALSE(s.hiddenFolders);
    // SetValues: without a Lines bit no Lines button stays checked; the
    // folder boxes are not touched.
    S current;
    current.subfolders = true;
    current.hiddenFolders = true;
    s = findReplaceSetValues(16, current);
    EXPECT_EQ(s.lines, S::Lines::None);
    EXPECT_TRUE(s.subfolders);
    EXPECT_TRUE(s.hiddenFolders);
}

TEST(FindReplaceOptions, SaveValuesPerTab)
{
    S s;
    s.tab = S::Tab::Find;
    s.field = S::Field::Actor;
    s.lines = S::Lines::Selected;
    s.endOfText = true;
    s.skipTags = true;
    s.subfolders = true; // not on this tab
    EXPECT_EQ(findReplaceOptions(s, 0xFFFFF), 128 | 1024 | 8 | (1 << 15));
    // Find in subtitles keeps every bit from 512 up and adds its own: it can
    // never clear "Include comments" or the folder bits (kept legacy quirk).
    s.tab = S::Tab::FindInFiles;
    s.lines = S::Lines::All;
    s.includeComments = false;
    s.hiddenFolders = true;
    const int previous = 512 | (1 << 14) | 4096 | 1 | 16;
    EXPECT_EQ(findReplaceOptions(s, previous), 512 | (1 << 14) | 4096 | 8192 | 128 | 8 | (1 << 15));
    s.lines = S::Lines::None;
    s.tab = S::Tab::Replace;
    EXPECT_EQ(findReplaceOptions(s, 0) & (512 | 1024 | 2048), 0);
}

TEST_F(Find, NextWalksMatchesSkippingCommentsAndSelectsTheRow)
{
    window.find = u8"HELLO";
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 0, 5)); // TextEdit, case folded
    EXPECT_EQ(session->selection().selected, (std::set<core::LineId>{row(0)}));
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 6, 11)); // same Line, further on
    fr.find(window);
    EXPECT_EQ(last(), Match(2, 1, 5, 10)); // the Comment is skipped
    fr.find(window);
    EXPECT_EQ(last(), Match(3, 1, 8, 13));
    EXPECT_TRUE(host.questions.empty());
    // The end: "Reached end. Search from the beginning?" No keeps the place.
    host.answers = {FindAnswer::No};
    fr.find(window);
    ASSERT_EQ(host.questions.size(), 1u);
    EXPECT_EQ(host.questions[0].kind, K::Wrap);
    EXPECT_EQ(host.shown.size(), 4u);
    // Next time it starts at row 0 again.
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 0, 5));
    EXPECT_EQ(str(fr.recent().finds[0]), "HELLO");
}

TEST_F(Find, WrapYesStartsAtTheSelectionAndTheNextEndJustReportsNotFound)
{
    window.find = u8"hello";
    window.lines = S::Lines::FromSelection;
    select({2}, 2);
    fr.find(window);
    EXPECT_EQ(last(), Match(2, 1, 5, 10));
    fr.find(window);
    EXPECT_EQ(last(), Match(3, 1, 8, 13));
    host.answers = {FindAnswer::Yes};
    fr.find(window);
    // From the beginning means from the first selected Line (row 3 now).
    EXPECT_EQ(last(), Match(3, 1, 8, 13));
    // Having wrapped once, reaching the end only says it found nothing.
    fr.find(window);
    ASSERT_EQ(host.questions.size(), 2u);
    EXPECT_EQ(host.questions[1].kind, K::Message);
    EXPECT_EQ(str(host.questions[1].text), "Could not find the specified phrase \"hello\".");
}

TEST_F(Find, TranslationModeMarksTheOriginalOrTheTranslation)
{
    // Text is the original, "\n", then the translation (GetTextElement);
    // a match after the "\n" is marked in TextEdit (the translation).
    host.add("[Script Info]\nTLMode: Yes\nTLMode Style: TLmode\n\n[V4+ Styles]\nFormat: Name\nStyle: Default\nStyle: "
             "TLmode\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
             "Comment: 0,0:00:01.00,0:00:02.00,TLmode,,0,0,0,,aaa orig\n"
             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,translated aa\n",
             u8"tl.ass");
    host.target = 1;
    window.find = u8"aa";
    fr.find(window);
    ASSERT_EQ(host.shown.size(), 1u);
    EXPECT_EQ(host.shown[0].role, 0); // TextEditOrig
    EXPECT_EQ(host.shown[0].start, 0);
    EXPECT_EQ(host.shown[0].end, 2);
    fr.find(window);
    EXPECT_EQ(host.shown[1].role, 1);
    EXPECT_EQ(host.shown[1].start, 11);
    EXPECT_EQ(host.shown[1].end, 13);
    // Replace next writes the original and the translation back apart.
    window.tab = S::Tab::Replace;
    window.replace = u8"b";
    fr.replace(window);
    const auto &line = *host.docs[1].session->document().lines()[0];
    EXPECT_EQ(str(line.text), "aaa orig");
    EXPECT_EQ(str(line.translation), "translated b");
}

TEST_F(Find, BeginningOfTextWithoutRegexFindsTheFirstMatchAnywhere)
{
    // Legacy's else belongs to "End of text": a plain "Beginning of text"
    // search finds the first match in each Line, wherever it is.
    window.find = u8"hello";
    window.startOfText = true;
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 0, 5));
    fr.find(window);
    EXPECT_EQ(last(), Match(2, 1, 5, 10)); // one per Line
    fr.find(window);
    EXPECT_EQ(last(), Match(3, 1, 8, 13));
    // With a regular expression it is anchored.
    window.regex = true;
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 0, 5));
    host.answers = {FindAnswer::No};
    fr.find(window);
    EXPECT_EQ(host.questions.back().kind, K::Wrap);
}

TEST_F(Find, EndOfTextAndRegexOnTheRestOfTheText)
{
    window.find = u8"HELLO";
    window.endOfText = true;
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 6, 11));
    fr.find(window);
    EXPECT_EQ(last(), Match(3, 1, 8, 13)); // row 2 ends with a tag
    // A regular expression runs on the text after the last match, so "^"
    // matches there again (kept legacy quirk).
    window = {};
    window.find = u8"^l";
    window.regex = true;
    session->run(Command{"t", session->revision(), {row(0)}, [&](core::Document &d) {
                             return d.editLine(row(0), [](core::LineRecord &l) { l.text = u8"lll"; });
                         }});
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 0, 1));
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 1, 2));
}

TEST_F(Find, SkipTagsChecksOnlyTheFirstMatchOfALine)
{
    window.find = u8"b";
    window.includeComments = true;
    window.skipTags = true; // only outside tags
    // Row 2 "{\i1}hello{\b1}": the "b" is inside a tag, so the Line is left.
    window.lines = S::Lines::FromSelection;
    select({2}, 2);
    host.answers = {FindAnswer::No};
    fr.find(window);
    EXPECT_TRUE(host.shown.empty());
    window.skipTags = false;
    window.skipText = true; // only inside tags
    fr.reset();
    fr.find(window);
    EXPECT_EQ(last(), Match(2, 1, 12, 13));
}

TEST_F(Find, StylesOrSelectedLinesAndTheStylesQuestion)
{
    window.find = u8"hello";
    window.styles = u8"Sign";
    window.lines = S::Lines::Selected;
    select({0}, 0);
    // Find takes Lines in the styles OR selected (kept legacy quirk).
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 0, 5));
    EXPECT_EQ(session->selection().selected, (std::set<core::LineId>{row(0)})); // the selection stays
    fr.find(window);
    fr.find(window);
    EXPECT_EQ(last(), Match(2, 1, 5, 10));
    // A missing style asks: Ignore remembers, Ok removes the missing ones.
    S w;
    w.find = u8"hello";
    w.styles = u8"Sign,Nope,Gone";
    host.answers = {FindAnswer::No};
    auto asked = host.questions.size();
    fr.find(w);
    ASSERT_EQ(host.questions[asked].kind, K::Styles);
    EXPECT_EQ(str(host.questions[asked].argument), "Nope, Gone,");
    asked = host.questions.size();
    fr.find(w);
    for (std::size_t i = asked; i < host.questions.size(); ++i)
        EXPECT_NE(host.questions[i].kind, K::Styles); // ignored from now on
    // No style found at all always asks; Cancel stops.
    w.styles = u8"Nope";
    host.answers = {FindAnswer::Cancel};
    const auto shownBefore = host.shown.size();
    asked = host.questions.size();
    fr.find(w);
    ASSERT_EQ(host.questions.size(), asked + 1);
    EXPECT_EQ(host.questions.back().kind, K::NoStyles);
    EXPECT_EQ(host.shown.size(), shownBefore);
    // Yes removes the styles.
    host.answers = {FindAnswer::Yes};
    fr.find(w);
    EXPECT_EQ(fr.window().styles, u8"");
    // Ok keeps the found ones.
    FindReplace other{host};
    w.styles = u8"Sign,Nope";
    host.answers = {FindAnswer::Ok};
    other.find(w);
    EXPECT_EQ(other.window().styles, u8"Sign");
}

TEST_F(Find, InvalidRegularExpressionIsLoggedAndFindsNothing)
{
    window.find = u8"(";
    window.regex = true;
    fr.find(window);
    EXPECT_TRUE(host.shown.empty());
    EXPECT_TRUE(host.questions.empty());
    ASSERT_EQ(host.logs.size(), 1u);
    EXPECT_TRUE(host.logs[0].starts_with("Invalid regular expression '(':"));
}

TEST_F(Find, RegularExpressionsRunAsWxRegExOverPcre2)
{
    // R1-pcre2: wxRegEx(find, wxRE_ADVANCED | wxRE_ICASE) is PCRE2 with UTF
    // and DOTALL: lookbehind is valid and Unicode case folds.
    window.find = u8"(?<=hel)LO";
    window.regex = true;
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 3, 5));
    EXPECT_TRUE(host.logs.empty());
    auto &unicode = host.add(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,żółw ŁÓDŹ\n",
                             u8"u.ass");
    host.target = 1;
    window.find = u8"łódź";
    fr.find(window);
    ASSERT_EQ(host.shown.size(), 2u);
    EXPECT_EQ(host.shown[1].start, 5);
    EXPECT_EQ(host.shown[1].end, 9);
    // Match case off folds plain searches too; with it on, nothing matches.
    window.matchCase = true;
    host.answers = {FindAnswer::No};
    fr.find(window);
    EXPECT_EQ(host.shown.size(), 2u);
    EXPECT_EQ(host.questions.back().kind, K::Wrap);
    EXPECT_EQ(str(unicode.document().lines()[0]->text), "żółw ŁÓDŹ");
    // "." matches the "\n" that joins the original and the translation.
    host.add("[Script Info]\nTLMode: Yes\nTLMode Style: TLmode\n\n[V4+ Styles]\nFormat: Name\nStyle: Default\nStyle: "
             "TLmode\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
             "Comment: 0,0:00:01.00,0:00:02.00,TLmode,,0,0,0,,aaa orig\n"
             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,translated aa\n",
             u8"tl.ass");
    host.target = 2;
    window.matchCase = false;
    window.find = u8"orig.translated";
    fr.find(window);
    ASSERT_EQ(host.shown.size(), 3u);
    EXPECT_EQ(host.shown[2].role, 0); // starts in the original
    EXPECT_EQ(host.shown[2].start, 4);
    EXPECT_EQ(host.shown[2].end, 19);
}

TEST_F(Find, RegexMatchErrorsAreLoggedAsNoMatch)
{
    // PCRE2's match limit (catastrophic backtracking): wx_regexec logs the
    // error and the Line counts as not matching; nothing throws.
    host.add(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,," + std::string(40, 'a') + "b\n",
             u8"long.ass");
    host.target = 1;
    window.find = u8"(a+)+$";
    window.regex = true;
    host.answers = {FindAnswer::No};
    fr.find(window);
    EXPECT_TRUE(host.shown.empty());
    ASSERT_FALSE(host.logs.empty());
    EXPECT_TRUE(host.logs[0].starts_with("Failed to find match for regular expression: ")) << host.logs[0];
    EXPECT_EQ(host.questions.back().kind, K::Wrap);
    // Replace all and Find all go on the same way.
    window.tab = S::Tab::Replace;
    window.replace = u8"x";
    fr.replaceAll(window);
    EXPECT_EQ(str(host.questions.back().text), "Replaced 0 times.");
    fr.findAllInCurrent(window);
    EXPECT_TRUE(fr.results().empty());
}

TEST_F(Find, EmptyRegexMatchesMoveOnInsteadOfHanging)
{
    // F1-empty-match (R3-hang-crash-loss): legacy wxRegEx::Replace repeated an
    // empty match forever, hanging Replace all and Replace checked; the
    // match is replaced once and the search moves on.
    host.add(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,ab\n", u8"e.ass");
    host.target = 1;
    window.tab = S::Tab::Replace;
    window.find = u8"x*";
    window.regex = true;
    window.replace = u8"-";
    fr.replaceAll(window);
    EXPECT_EQ(str(host.docs[1].session->document().lines()[0]->text), "-ab");
    EXPECT_EQ(str(host.questions.back().text), "Replaced 1 times.");
    // Replace checked of an empty match.
    window.tab = S::Tab::Find;
    window.find = u8"b*";
    fr.findAllInCurrent(window);
    ASSERT_FALSE(fr.results().empty());
    fr.replaceChecked(u8"+");
    EXPECT_FALSE(str(host.docs[1].session->document().lines()[0]->text).empty());
}

TEST_F(Find, ReplaceNextReplacesTheMatchAsOneStepAndMovesOn)
{
    window.tab = S::Tab::Replace;
    window.find = u8"hello";
    window.replace = u8"bye";
    // A new search text runs Find first, then replaces what it found at once.
    fr.replace(window);
    EXPECT_EQ(texts()[0], "bye hello");
    EXPECT_EQ(session->history().back().name, "Replace");
    EXPECT_EQ(last(), Match(0, 1, 4, 9)); // the next one, after the replacement
    fr.replace(window);
    EXPECT_EQ(texts()[0], "bye bye");
    EXPECT_EQ(last(), Match(2, 1, 5, 10));
    EXPECT_EQ(session->historySize(), 3u);
    // A regular expression replaces within the match, once.
    window.find = u8"(h)ello";
    window.regex = true;
    window.replace = u8"\\1i";
    fr.replace(window);
    EXPECT_EQ(texts()[2], "{\\i1}hi{\\b1}");
    EXPECT_EQ(last(), Match(3, 1, 8, 13));
    // Moving the active Line makes Replace search again first; when that
    // finds nothing, the last match, still there, is replaced (kept legacy quirk).
    select({0}, 0);
    host.answers = {FindAnswer::No};
    fr.replace(window);
    EXPECT_EQ(texts()[3], "the end hi");
    EXPECT_EQ(host.questions.back().kind, K::Wrap);
    // F3 searches again with the last values.
    const auto asked = host.questions.size();
    fr.findNext();
    EXPECT_EQ(host.questions.size(), asked + 1);
}

TEST_F(Find, ReplaceNextNeverReplacesAStaleMatch)
{
    // F1-stale-replace (R3-hang-crash-loss): after Replace next replaced the
    // last match and its search found nothing more, legacy replaced the old
    // [start, end) again, overwriting text that did not match ("c x" -> "c").
    host.add(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,cat dog cat x\n", u8"s.ass");
    host.target = 1;
    auto &s = *host.docs[1].session;
    const auto text = [&] { return str(s.document().lines()[0]->text); };
    window.tab = S::Tab::Replace;
    window.find = u8"cat";
    window.replace = u8"c";
    fr.replace(window);
    EXPECT_EQ(text(), "c dog cat x");
    host.answers = {FindAnswer::No};
    fr.replace(window); // the second, then "Reached end", No
    EXPECT_EQ(text(), "c dog c x");
    EXPECT_EQ(host.questions.back().kind, K::Wrap);
    const auto steps = s.historySize();
    host.answers = {FindAnswer::No};
    fr.replace(window); // searches again, finds nothing, replaces nothing
    EXPECT_EQ(text(), "c dog c x");
    EXPECT_EQ(s.historySize(), steps);
    // An edit of the matched Line makes the match stale too.
    s.run(Command{"e", s.revision(), {s.document().lines()[0]->id}, [&](core::Document &d) {
                      return d.editLine(s.document().lines()[0]->id, [](core::LineRecord &l) { l.text = u8"cat"; });
                  }});
    fr.find(window);
    EXPECT_EQ(host.shown.back().start, 0);
    s.run(Command{"e", s.revision(), {s.document().lines()[0]->id}, [&](core::Document &d) {
                      return d.editLine(s.document().lines()[0]->id, [](core::LineRecord &l) { l.text = u8"xyz cat"; });
                  }});
    host.answers = {FindAnswer::Yes};
    fr.replace(window); // searches again ("Reached end", Yes), finds "cat" at 4 and replaces that
    EXPECT_EQ(text(), "xyz c");
}

TEST_F(Find, QuestionsWaitWithoutBlockingAndRefuseOtherWork)
{
    // A question box does not block: the operation waits for its answer and
    // every other call (F3, buttons, Replace checked) is ignored meanwhile,
    // as legacy's modal boxes block the window.
    host.hold = true;
    window.find = u8"zzz";
    fr.find(window);
    ASSERT_TRUE(fr.busy());
    ASSERT_EQ(host.questions.size(), 1u);
    EXPECT_EQ(host.questions[0].kind, K::Wrap);
    const int finished = host.finishedCount;
    fr.findNext();
    fr.find(window);
    window.tab = S::Tab::Replace;
    fr.replaceAll(window);
    fr.replaceChecked(u8"x");
    EXPECT_EQ(host.questions.size(), 1u);
    EXPECT_EQ(host.finishedCount, finished);
    // The answer resumes it: Yes searches from the start, then says it found nothing.
    host.hold = false;
    auto answer = std::move(host.pending);
    answer(FindAnswer::Yes);
    EXPECT_FALSE(fr.busy());
    EXPECT_EQ(host.questions.back().kind, K::Message);
    EXPECT_EQ(host.questions.back().info, FindQuestion::Info::NotFound);
    EXPECT_EQ(host.finishedCount, finished + 1);
    answer(FindAnswer::Yes); // a second answer is ignored
    EXPECT_EQ(host.finishedCount, finished + 1);
}

TEST_F(Find, ADocumentClosedDuringAQuestionIsNotTouched)
{
    // The styles question waits; the Document closes meanwhile (legacy could
    // not close it under the modal box): answering does nothing to it.
    host.hold = true;
    window.tab = S::Tab::Replace;
    window.find = u8"hello";
    window.replace = u8"X";
    window.styles = u8"Nope";
    fr.replaceAll(window);
    ASSERT_TRUE(fr.busy());
    EXPECT_EQ(host.questions.back().kind, K::NoStyles);
    auto closed = std::move(host.docs[0]);
    host.docs.clear();
    auto answer = std::move(host.pending);
    answer(FindAnswer::Yes);
    EXPECT_FALSE(fr.busy());
    EXPECT_EQ(str(closed.session->document().lines()[0]->text), "Hello hello");
    EXPECT_EQ(closed.session->historySize(), 1u);
}

TEST_F(Find, FromSelectedWithOnlyHiddenLinesSelectedStartsThere)
{
    // S57-from-selected: legacy FirstSelection skipped hidden Lines and gave
    // -1 when only hidden ones were selected, so "From selected" searched
    // from row 0, before the selection. It starts at the selection now; a
    // shown selected Line (the characterized ordinary case) is unchanged.
    session->run(Command{"h", session->revision(), {row(2)}, [&](core::Document &d) {
                             return d.editLine(row(2), [](core::LineRecord &l) {
                                 l.visibility = core::LineVisibility::Hidden;
                             });
                         }});
    window.find = u8"hello";
    window.lines = S::Lines::FromSelection;
    select({2}, 2);
    fr.find(window);
    EXPECT_EQ(last(), Match(3, 1, 8, 13)); // legacy: row 0
    fr.reset();
    select({0, 2}, 0);
    fr.find(window);
    EXPECT_EQ(last(), Match(0, 1, 0, 5));
    // Find all and Replace all start there too.
    select({2}, 2);
    fr.findAllInCurrent(window);
    ASSERT_EQ(fr.results().size(), 2u);
    EXPECT_EQ(fr.results()[1].keyLine, 3);
    window.tab = S::Tab::Replace;
    window.replace = u8"X";
    fr.replaceAll(window);
    // (The test host ignores filtering, so the hidden row 2 is replaced too.)
    EXPECT_EQ(texts(), (std::vector<std::string>{"Hello hello", "hello sign", "{\\i1}X{\\b1}", "the end X"}));
}

TEST_F(Find, ReplaceAllIsOneStepWithTheLegacyCount)
{
    window.tab = S::Tab::Replace;
    window.find = u8"hello";
    window.replace = u8"bye";
    fr.replaceAll(window);
    EXPECT_EQ(texts(), (std::vector<std::string>{"bye bye", "hello sign", "{\\i1}bye{\\b1}", "the end bye"}));
    EXPECT_EQ(session->historySize(), 2u);
    EXPECT_EQ(session->history().back().name, "Replace all");
    EXPECT_EQ(str(host.questions.back().text), "Replaced 4 times.");
    EXPECT_EQ(str(fr.recent().replacements[0]), "bye");
    // Undo restores every Line.
    session->undo();
    EXPECT_EQ(texts()[0], "Hello hello");
    // Nothing found: the message, no step.
    window.find = u8"zzz";
    fr.replaceAll(window);
    EXPECT_EQ(str(host.questions.back().text), "Replaced 0 times.");
    EXPECT_EQ(session->historySize(), 2u);
}

TEST_F(Find, ReplaceAllScopesStylesHiddenAndComments)
{
    window.tab = S::Tab::Replace;
    window.find = u8"hello";
    window.replace = u8"X";
    window.includeComments = true;
    // Styles AND Selected lines for Replace all (Find uses OR).
    window.styles = u8"Sign";
    window.lines = S::Lines::Selected;
    select({0, 2}, 0);
    fr.replaceAll(window);
    EXPECT_EQ(texts(), (std::vector<std::string>{"Hello hello", "hello sign", "{\\i1}X{\\b1}", "the end hello"}));
    // From selection starts at the first selected Line.
    window.styles.clear();
    window.lines = S::Lines::FromSelection;
    select({1}, 1);
    fr.replaceAll(window);
    EXPECT_EQ(texts(), (std::vector<std::string>{"Hello hello", "X sign", "{\\i1}X{\\b1}", "the end X"}));
    // Hidden Lines are left unless "Ignore filtering in some actions".
    session->undo();
    session->undo();
    const auto hidden = row(3);
    host.visible = [hidden](core::LineId id) { return id != hidden; };
    window.lines = S::Lines::All;
    fr.replaceAll(window);
    EXPECT_EQ(texts()[3], "the end hello");
    EXPECT_EQ(texts()[0], "X X");
}

TEST_F(Find, ReplaceAllAnchoredRegexReplacesNothingAndThePatternLengthQuirk)
{
    window.tab = S::Tab::Replace;
    window.replace = u8"X";
    // Kept legacy defect: with "Beginning/End of text" Replace all compares
    // "^hello" / "hello$" literally, so a regular expression replaces nothing.
    window.find = u8"hello";
    window.regex = true;
    window.endOfText = true;
    fr.replaceAll(window);
    EXPECT_EQ(str(host.questions.back().text), "Replaced 0 times.");
    // Plain "Beginning of text" replaces at the start of each Line.
    window.regex = false;
    window.endOfText = false;
    window.startOfText = true;
    window.find = u8"the";
    fr.replaceAll(window);
    EXPECT_EQ(texts()[3], "X end hello");
    // An empty search with "End of text" appends.
    window.startOfText = false;
    window.endOfText = true;
    window.find = u8"";
    window.replace = u8"!";
    fr.replaceAll(window);
    EXPECT_EQ(texts()[0], "Hello hello!");
    EXPECT_EQ(str(host.questions.back().text), "Replaced 3 times.");
    // A regular expression continues after the pattern's length, not the
    // match's: "(l)" (3 characters) skips the next two letters.
    window = {};
    window.tab = S::Tab::Replace;
    window.find = u8"(l)";
    window.regex = true;
    window.replace = u8"L";
    window.lines = S::Lines::Selected;
    select({0}, 0);
    fr.replaceAll(window);
    EXPECT_EQ(texts()[0], "HeLlo heLlo!");
}

TEST_F(Find, ReplaceInAllOpenDocumentsIsOneStepEach)
{
    auto &second = host.add(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,hello again\n",
                            u8"b.ass");
    window.tab = S::Tab::Replace;
    window.find = u8"hello";
    window.replace = u8"bye";
    fr.replaceInAllOpened(window);
    EXPECT_EQ(str(host.questions.back().text), "Replaced 5 times.");
    EXPECT_EQ(session->history().back().name, "Replace all");
    EXPECT_EQ(second.history().back().name, "Replace all");
    EXPECT_EQ(str(second.document().lines()[0]->text), "bye again");
}

TEST_F(Find, FindAllListsOverlappingMatchesWithShownRowNumbers)
{
    auto &tl = host.add("[Script Info]\nTLMode: Yes\nTLMode Style: TLmode\n\n[V4+ Styles]\nFormat: Name\nStyle: "
                        "Default\nStyle: TLmode\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
                        "MarginV, Effect, Text\n"
                        "Comment: 0,0:00:01.00,0:00:02.00,TLmode,,0,0,0,,aaa orig\n"
                        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,translated aa\n",
                        u8"tl.ass");
    ASSERT_EQ(tl.document().lines().size(), 1u);
    // Row 1 hidden: shown numbers skip it, a skipped comment still counts.
    session->run(Command{"h", session->revision(), {row(1)}, [&](core::Document &d) {
                             return d.editLine(row(1), [](core::LineRecord &l) {
                                 l.visibility = core::LineVisibility::Hidden;
                             });
                         }});
    window.find = u8"aa";
    fr.findInAllOpened(window);
    ASSERT_TRUE(fr.resultsShown());
    const auto &r = fr.results();
    // Only the translation mode Document matches: "aaa orig" twice
    // (overlapping, one character on) and "translated aa" once.
    ASSERT_EQ(r.size(), 4u);
    EXPECT_TRUE(r[0].header);
    EXPECT_EQ(str(r[0].text), "tl.ass");
    EXPECT_EQ(str(r[1].text), "aaa orig");
    EXPECT_EQ(r[1].start, 0);
    EXPECT_EQ(r[2].start, 1);
    EXPECT_FALSE(r[2].translation);
    EXPECT_EQ(str(r[3].text), "translated aa");
    EXPECT_TRUE(r[3].translation);
    EXPECT_EQ(r[3].start, 11);
    EXPECT_EQ(r[3].idLine, 1);
    // In the first Document: hello is in rows 0, 2, 3 (row 1 hidden).
    window.find = u8"hello";
    host.target = 0;
    fr.findAllInCurrent(window);
    std::vector<std::pair<int, int>> lines;
    for (const auto &x : fr.results())
        if (!x.header)
            lines.emplace_back(x.keyLine, x.idLine);
    EXPECT_EQ(lines, (std::vector<std::pair<int, int>>{{0, 1}, {0, 1}, {2, 2}, {3, 3}}));
    // Another field shows "field  ->  text".
    window.field = S::Field::Actor;
    window.find = u8"bo";
    fr.findAllInCurrent(window);
    ASSERT_EQ(fr.results().size(), 2u);
    EXPECT_EQ(str(fr.results()[1].text), "Bob  ->  {\\i1}hello{\\b1}");
}

TEST_F(Find, FindAllEndOfTextRecordsALineOnce)
{
    // Legacy records a plain "End of text" match forever (it never leaves
    // the Line); here it is recorded once (F1-end-of-text).
    window.find = u8"hello";
    window.endOfText = true;
    fr.findAllInCurrent(window);
    ASSERT_EQ(fr.results().size(), 3u);
    EXPECT_EQ(fr.results()[1].keyLine, 0);
    EXPECT_EQ(fr.results()[2].keyLine, 3);
}

TEST_F(Find, ResultsChecksAndGroups)
{
    host.add(std::string(kHeader) + "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,hello\n", u8"b.ass");
    window.find = u8"hello";
    fr.findInAllOpened(window);
    const auto &r = fr.results();
    ASSERT_EQ(r.size(), 7u); // a.ass + 4, b.ass + 1
    ASSERT_TRUE(r[5].header);
    fr.toggleChecked(0); // a header unchecks its group
    for (std::size_t i = 0; i < 5; ++i)
        EXPECT_FALSE(r[i].checked);
    EXPECT_TRUE(r[6].checked);
    fr.toggleChecked(2); // one result checks its header again
    EXPECT_TRUE(r[0].checked);
    fr.toggleChecked(2);
    EXPECT_FALSE(r[0].checked);
    fr.toggleGroup(0);
    EXPECT_FALSE(r[1].visible);
    EXPECT_TRUE(r[6].visible);
    fr.toggleGroup(0);
    EXPECT_TRUE(r[1].visible);
    fr.checkAll(true);
    EXPECT_TRUE(r[0].checked && r[3].checked);
    // Double click goes to the match.
    host.target = 1;
    fr.showResult(4);
    EXPECT_EQ(host.target, 0u);
    EXPECT_EQ(last(), Match(3, 1, 8, 13));
}

TEST_F(Find, ReplaceCheckedAppliesTheCheckedMatchesAsOneStepPerDocument)
{
    window.find = u8"hello";
    window.includeComments = true;
    fr.findAllInCurrent(window);
    ASSERT_EQ(fr.results().size(), 6u);
    fr.toggleChecked(2); // the second hello of row 0
    // Row 3 edited since the search: skipped and logged.
    session->run(Command{"e", session->revision(), {row(3)}, [&](core::Document &d) {
                             return d.editLine(row(3), [](core::LineRecord &l) { l.text = u8"changed hello"; });
                         }});
    fr.replaceChecked(u8"Bye!");
    EXPECT_FALSE(fr.canReplaceChecked());
    EXPECT_EQ(texts(), (std::vector<std::string>{"Bye! hello", "Bye! sign", "{\\i1}Bye!{\\b1}", "changed hello"}));
    EXPECT_EQ(session->history().back().name, "Replace all");
    EXPECT_EQ(session->historySize(), 3u);
    ASSERT_EQ(host.logs.size(), 1u);
    EXPECT_EQ(host.logs[0], "Line 4 cannot be replaced,\ncause it was edited.");
    // Overlapping matches shift by the earlier replacement in the same Line.
    session->undo();
    window.find = u8"l";
    fr.findAllInCurrent(window);
    fr.replaceChecked(u8"LL");
    EXPECT_EQ(texts()[0], "HeLLLLo heLLLLo");
}

TEST_F(Find, ReplaceCheckedOnlyReplacesTextResults)
{
    // A result in another field never equals the Line's text: skipped.
    window.find = u8"Anna";
    window.field = S::Field::Actor;
    fr.findAllInCurrent(window);
    ASSERT_EQ(fr.results().size(), 2u);
    fr.replaceChecked(u8"X");
    EXPECT_EQ(session->historySize(), 1u);
    EXPECT_EQ(host.logs.size(), 1u);
}

namespace {

struct Files : Find {
    void SetUp() override
    {
        Find::SetUp();
        window.tab = S::Tab::FindInFiles;
        window.folder = u8"/subs";
        host.files[u8"/subs/a.ass"] =
            u16("[Script Info]\nTitle: x\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
                "MarginV, Effect, Text\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello cat   \n"
                "Comment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,a cat comment\n\n"
                "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,no match\n");
        host.files[u8"/subs/b.srt"] = u16("1\n00:00:01,000 --> 00:00:02,000\nfirst cat\nsecond\n\n2\n"
                                          "00:00:03,000 --> 00:00:04,000\nnothing\n");
        host.files[u8"/subs/c.txt"] = u16("plain cat line\nother\n");
        host.files[u8"/subs/d.ass"] = u16("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,dog\n");
        host.files[u8"/subs/video.mkv"] = u16("cat");
        host.listed = {u8"/subs/a.ass", u8"/subs/d.ass", u8"/subs/b.srt", u8"/subs/c.txt", u8"/subs/video.mkv"};
    }
};

} // namespace

TEST_F(Files, FindInFilesListsMatchesPerFile)
{
    window.find = u8"cat";
    fr.findInFiles(window);
    ASSERT_TRUE(fr.resultsShown());
    std::vector<std::string> rows;
    for (const auto &r : fr.results())
        rows.push_back(r.header ? "# " + str(r.text) : std::to_string(r.idLine) + ":" + str(r.text));
    // The Comment is skipped but counted; the video is left out by extension.
    EXPECT_EQ(rows, (std::vector<std::string>{"# /subs/a.ass", "1:Hello cat", "# /subs/b.srt", "1:first cat\\Nsecond",
                                              "# /subs/c.txt", "1:plain cat line"}));
    EXPECT_EQ(fr.results()[1].path, u8"/subs/a.ass");
    EXPECT_TRUE(host.writes.empty());
    EXPECT_EQ(str(fr.recent().paths[0]), "/subs");
    // An invalid folder says so and finds nothing.
    host.folderValid = false;
    fr.findInFiles(window);
    EXPECT_EQ(str(host.questions.back().text), "Search path is invalid");
}

TEST_F(Files, ReplaceInFilesWritesOnlyChangedTargetsAfterABackup)
{
    window.find = u8"cat";
    window.replace = u8"dog";
    // No: nothing happens.
    host.answers = {FindAnswer::No};
    fr.replaceInFiles(window);
    EXPECT_TRUE(host.writes.empty());
    host.answers = {FindAnswer::Yes};
    fr.replaceInFiles(window);
    EXPECT_EQ(str(host.questions.back().text), "Replaced 3 times.");
    // Only the listed subtitle files with a replacement are written, each
    // after its backup; d.ass (no match) and the video stay untouched.
    std::vector<std::string> written;
    for (const auto &[path, text] : host.writes)
        written.push_back(str(path));
    EXPECT_EQ(written, (std::vector<std::string>{"/subs/a.ass", "/subs/b.srt", "/subs/c.txt"}));
    EXPECT_EQ(host.backups, (std::vector<std::u8string>{u8"/subs/a.ass", u8"/subs/b.srt", u8"/subs/c.txt"}));
    // The header keeps its lines with CRLF; a changed Line is trimmed, every
    // other line is written as it was, with CRLF. Legacy left out the skipped
    // comment and the blank line (R3-hang-crash-loss: kept here).
    EXPECT_EQ(str(host.writes[0].second),
              "[Script Info]\r\nTitle: x\r\n\r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, "
              "MarginV, Effect, Text\r\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello dog\r\n"
              "Comment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,a cat comment\r\n\r\n"
              "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,no match\r\n");
    // SRT cues are numbered again.
    EXPECT_EQ(str(host.writes[1].second), "1\r\n00:00:01,000 --> 00:00:02,000\r\nfirst dog\r\nsecond\r\n\r\n"
                                          "2\r\n00:00:03,000 --> 00:00:04,000\r\nnothing\r\n\r\n");
    // A plain text Line that changed is written as an ASS Dialogue line
    // (legacy GetRaw of a Format 0 Line); the others stay.
    EXPECT_EQ(str(host.writes[2].second),
              "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,plain dog line\r\nother\r\n");
    // Including comments keeps and replaces them.
    host.files[u8"/subs/a.ass"] = u16("[Events]\nComment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,a cat comment\n");
    host.writes.clear();
    window.includeComments = true;
    window.filters = u8"*.ass";
    host.answers = {FindAnswer::Yes};
    fr.replaceInFiles(window);
    ASSERT_EQ(host.writes.size(), 1u);
    EXPECT_EQ(str(host.writes[0].second),
              "[Events]\r\nComment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,a dog comment\r\n");
}

TEST_F(Files, OtherFieldsSearchOnlyAssFiles)
{
    window.find = u8"Default";
    window.field = S::Field::Style;
    fr.findInFiles(window);
    std::vector<std::string> headers;
    for (const auto &r : fr.results())
        if (r.header)
            headers.push_back(str(r.text));
    EXPECT_EQ(headers, (std::vector<std::string>{"/subs/a.ass", "/subs/d.ass"}));
}

TEST_F(Files, ReplaceCheckedInFilesRewritesTheCheckedLines)
{
    host.files[u8"/subs/e.ass"] = u16("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,cat one\n"
                                      "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,cat two\n"
                                      "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,three\n");
    host.listed = {u8"/subs/e.ass", u8"/subs/c.txt"};
    window.find = u8"cat";
    fr.findInFiles(window);
    ASSERT_EQ(fr.results().size(), 5u);
    fr.toggleChecked(4); // c.txt's match stays
    fr.toggleChecked(2); // so does "cat two"
    fr.replaceChecked(u8"dog");
    ASSERT_EQ(host.writes.size(), 1u);
    EXPECT_EQ(str(host.writes[0].second), "[Events]\r\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,dog one\r\n"
                                          "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,cat two\r\n"
                                          "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,three\r\n");
}

TEST_F(Files, ReplaceCheckedInFilesKeepsAnEditedLineAndTheRest)
{
    // R3-hang-crash-loss: legacy left a checked Line that changed on disk out
    // of the file together with every Line after it; it is logged and kept
    // as it is now, and the later Lines (and blank lines) stay.
    host.files[u8"/subs/e.ass"] = u16("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,cat one\n"
                                      "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,cat two\n"
                                      "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,three cat\n");
    host.listed = {u8"/subs/e.ass"};
    window.find = u8"cat";
    fr.findInFiles(window);
    ASSERT_EQ(fr.results().size(), 4u);
    host.files[u8"/subs/e.ass"] = u16("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,cat one\n\n"
                                      "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,cat 2  \n"
                                      "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,three cat\n");
    fr.replaceChecked(u8"dog");
    ASSERT_EQ(host.writes.size(), 1u);
    EXPECT_EQ(str(host.writes[0].second), "[Events]\r\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,dog one\r\n\r\n"
                                          "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,cat 2  \r\n"
                                          "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,three dog\r\n");
    ASSERT_EQ(host.logs.size(), 1u);
    EXPECT_EQ(host.logs[0], "Line 2 cannot be replaced,\ncause it was edited.");
}

TEST_F(Files, AFileThatCannotBeWrittenCountsNoReplacements)
{
    window.find = u8"cat";
    window.replace = u8"dog";
    host.unwritable = {u8"/subs/a.ass"};
    host.answers = {FindAnswer::Yes};
    fr.replaceInFiles(window);
    // a.ass (1) is not counted; b.srt and c.txt are.
    EXPECT_EQ(str(host.questions.back().text), "Replaced 2 times.");
    EXPECT_EQ(host.questions.back().info, FindQuestion::Info::Replaced);
    EXPECT_EQ(str(host.questions.back().argument), "2");
}

TEST_F(Files, ResultsFromTabsAfterAFilesSearchAreNotReplaced)
{
    // FindReplaceResultsDialog::findInFiles stays set after a files search,
    // so Replace treats later Document results as files (kept legacy quirk).
    window.find = u8"cat";
    fr.findInFiles(window);
    S tabs;
    tabs.find = u8"hello";
    fr.findAllInCurrent(tabs);
    fr.replaceChecked(u8"X");
    EXPECT_EQ(texts()[0], "Hello hello");
    EXPECT_TRUE(host.writes.empty());
}

TEST(FindReplaceRecent, AddRecentPerTabAndTwentyWhenLoaded)
{
    struct NoHost : FindReplaceHost {
        std::optional<FindTab> current() override { return std::nullopt; }
        std::vector<FindTab> tabs() override { return {}; }
        void ask(const FindQuestion &, std::function<void(FindAnswer)>) override {}
        void showLine(DocumentId, core::LineId, bool, int, int, int) override {}
        std::optional<std::vector<std::u8string>> listFiles(const std::u8string &, const std::u8string &, bool,
                                                            bool) override
        {
            return std::vector<std::u8string>{};
        }
        std::optional<std::u16string> readFile(const std::u8string &) override { return std::nullopt; }
        bool fileExists(const std::u8string &) override { return false; }
        bool backupFile(const std::u8string &) override { return true; }
        bool writeFile(const std::u8string &, const std::u16string &) override { return true; }
        std::optional<DocumentId> openFile(const std::u8string &) override { return std::nullopt; }
    } host;
    FindReplace fr{host};
    FindReplace::Recent recent;
    for (int i = 0; i < 25; ++i)
        recent.finds.push_back(std::u8string(1, static_cast<char8_t>(u8'a' + i)));
    fr.setRecent(recent);
    EXPECT_EQ(fr.recent().finds.size(), 20u);
}
