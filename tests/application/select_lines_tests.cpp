// F2: select lines against legacy SelectLines (SelectOnTab, SaveOptions,
// AddRecent, OnChooseStyles) at 20d647c4.

#include "hikari/application/select_lines.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/srt.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;
using S = SelectLinesSettings;

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

constexpr std::string_view kHeader =
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

constexpr std::string_view kLines =
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,Anna,0,0,0,,Hello World\n"
    "Comment: 0,0:00:03.00,0:00:04.00,Sign,,0,0,0,,hello sign\n"
    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,Bob,0,0,0,fx,third\n"
    "Dialogue: 0,0:00:07.00,0:00:08.00,Sign,,0,0,0,,\n";

struct Select : ::testing::Test {
    EditSession session{load(std::string(kHeader) + std::string(kLines))};
    core::LineId row(std::size_t i) const { return session.document().lines()[i]->id; }
    std::set<std::size_t> selectedRows() const
    {
        std::set<std::size_t> out;
        const auto lines = session.document().lines();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (session.selection().selected.contains(lines[i]->id))
                out.insert(i);
        return out;
    }
    std::size_t activeRow() const
    {
        const auto lines = session.document().lines();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (session.selection().active == lines[i]->id)
                return i;
        return 99;
    }
    void select(std::set<std::size_t> rows, std::size_t active)
    {
        std::set<core::LineId> ids;
        for (auto r : rows)
            ids.insert(row(r));
        session.setSelection(Selection{row(active), ids, row(active), {}});
    }
    std::vector<std::string> texts() const
    {
        std::vector<std::string> out;
        for (const auto *l : session.document().lines())
            out.emplace_back(l->text.begin(), l->text.end());
        return out;
    }
    int run(S s, const LineVisible &visible = {})
    {
        auto r = selectLines(session, s, visible);
        EXPECT_TRUE(r.has_value());
        return r ? r->count : -1;
    }
};

S find(std::u8string text, S::Field field = S::Field::Text)
{
    S s;
    s.find = std::move(text);
    s.field = field;
    return s;
}

} // namespace

TEST_F(Select, TextIgnoresCaseUnlessAskedAndCountsDialoguesOnly)
{
    select({3}, 3);
    EXPECT_EQ(run(find(u8"HELLO")), 1); // the Comment is not a dialogue
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{0}));
    EXPECT_EQ(activeRow(), 0u);
    S s = find(u8"hello");
    s.comments = true;
    EXPECT_EQ(run(s), 2);
    s.matchCase = true;
    s.find = u8"Hello";
    EXPECT_EQ(run(s), 1);
    EXPECT_EQ(session.historySize(), 1u); // selection is no history step
}

TEST_F(Select, EmptySearchFindsOnlyEmptyFieldsAndWithoutInverts)
{
    S s = find(u8"");
    EXPECT_EQ(run(s), 1); // row 3's empty text
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{3}));
    s.field = S::Field::Actor;
    EXPECT_EQ(run(s), 1); // the Comment's empty actor is not a dialogue
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{3}));
    s = find(u8"o", S::Field::Actor);
    s.with = false;
    // Without "o": Anna and the empty actor of row 3 (Bob contains o).
    EXPECT_EQ(run(s), 2);
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{0, 3}));
}

TEST_F(Select, FieldsStyleEffectAndTimes)
{
    EXPECT_EQ(run(find(u8"Sign", S::Field::Style)), 1);
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{3}));
    EXPECT_EQ(run(find(u8"fx", S::Field::Effect)), 1);
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{2}));
    EXPECT_EQ(run(find(u8"0:00:05.00", S::Field::Start)), 1);
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{2}));
    EXPECT_EQ(run(find(u8"08.", S::Field::End)), 1);
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{3}));
}

TEST_F(Select, RegularExpressionsAndAnInvalidOne)
{
    S s = find(u8"^h.*d$");
    s.regex = true;
    EXPECT_EQ(run(s), 1); // icase: "Hello World"
    s.matchCase = true;
    EXPECT_EQ(run(s), 0);
    select({0, 2}, 2);
    s.find = u8"(";
    EXPECT_EQ(run(s), 0);
    EXPECT_TRUE(selectedRows().empty()); // Select cleared it before compiling
    EXPECT_EQ(activeRow(), 2u);
    select({0, 2}, 2);
    s.mode = S::Mode::AddToSelection;
    EXPECT_EQ(run(s), 0);
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{0, 2}));
}

TEST_F(Select, AddAndDeselectCount)
{
    select({2}, 2);
    S s = find(u8"Default", S::Field::Style);
    s.mode = S::Mode::AddToSelection;
    EXPECT_EQ(run(s), 2); // both Default Lines count, row 2 was already selected
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{0, 2}));
    select({0, 3}, 3);
    s.mode = S::Mode::Deselect;
    EXPECT_EQ(run(s), 1); // only row 0 was selected
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{3}));
    EXPECT_EQ(activeRow(), 3u);
}

TEST_F(Select, HiddenLinesAreSkippedUnlessFilteringIsIgnored)
{
    const auto hidden = row(0);
    const LineVisible visible = [&](core::LineId id) { return id != hidden; };
    EXPECT_EQ(run(find(u8"Default", S::Field::Style), visible), 1);
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{2}));
    EXPECT_EQ(run(find(u8"Default", S::Field::Style)), 2);
}

TEST_F(Select, CopyAndCutWriteTheRawLines)
{
    S s = find(u8"Default", S::Field::Style);
    s.action = S::Action::Copy;
    auto r = selectLines(session, s);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->count, 2);
    EXPECT_EQ(*r->clipboard, u8"Dialogue: 0,0:00:01.00,0:00:02.00,Default,Anna,0,0,0,,Hello World\r\n"
                             u8"Dialogue: 0,0:00:05.00,0:00:06.00,Default,Bob,0,0,0,fx,third\r\n");
    EXPECT_EQ(session.historySize(), 1u);
    const auto copied = *r->clipboard;
    s.action = S::Action::Cut;
    r = selectLines(session, s);
    ASSERT_TRUE(r);
    EXPECT_EQ(*r->clipboard, copied);
    EXPECT_EQ(texts(), (std::vector<std::string>{"hello sign", ""}));
    EXPECT_TRUE(selectedRows().empty());
    EXPECT_EQ(activeRow(), 0u);
    EXPECT_EQ(session.history().back().name, "Selecting lines");
    session.undo();
    EXPECT_EQ(texts().size(), 4u);
}

TEST_F(Select, CopyWithNothingSelectedClearsTheClipboard)
{
    S s = find(u8"nothing");
    s.action = S::Action::Copy;
    const auto r = selectLines(session, s);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->count, 0);
    EXPECT_EQ(r->clipboard, std::u8string());
}

TEST_F(Select, MoveToBeginningAndEndKeepLineIds)
{
    const auto third = row(2), first = row(0);
    S s = find(u8"Default", S::Field::Style);
    s.action = S::Action::MoveToEnd;
    EXPECT_EQ(run(s), 2);
    EXPECT_EQ(texts(), (std::vector<std::string>{"hello sign", "", "Hello World", "third"}));
    EXPECT_EQ(row(2), first);
    EXPECT_EQ(row(3), third);
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{2, 3}));
    EXPECT_EQ(activeRow(), 2u);
    s.action = S::Action::MoveToBeginning;
    s.find = u8"Sign";
    EXPECT_EQ(run(s), 1); // the Comment is not a dialogue
    EXPECT_EQ(texts(), (std::vector<std::string>{"", "hello sign", "Hello World", "third"}));
    EXPECT_EQ(session.historySize(), 3u);
}

TEST_F(Select, MoveTakesHiddenSelectedLinesToo)
{
    // Approved F2-move-hidden: legacy deleted a hidden selected Line and
    // inserted only the Lines its walk saw; every selected Line moves now.
    const auto hidden = row(3);
    const LineVisible visible = [&](core::LineId id) { return id != hidden; };
    select({3}, 0);
    S s = find(u8"third");
    s.mode = S::Mode::AddToSelection;
    s.action = S::Action::MoveToBeginning;
    EXPECT_EQ(run(s, visible), 1);
    EXPECT_EQ(texts(), (std::vector<std::string>{"third", "", "Hello World", "hello sign"}));
    EXPECT_EQ(row(1), hidden);
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{0, 1}));
}

TEST_F(Select, SetAsCommentAndDelete)
{
    S s = find(u8"Default", S::Field::Style);
    s.action = S::Action::SetAsComment;
    EXPECT_EQ(run(s), 2);
    EXPECT_TRUE(session.document().lines()[0]->comment);
    EXPECT_TRUE(session.document().lines()[2]->comment);
    EXPECT_EQ(selectedRows(), (std::set<std::size_t>{0, 2}));
    s = find(u8"");
    s.with = false;
    s.dialogues = s.comments = true;
    s.action = S::Action::Delete;
    EXPECT_EQ(run(s), 3);
    EXPECT_EQ(texts(), (std::vector<std::string>{""}));
    s = find(u8"");
    s.action = S::Action::Delete;
    EXPECT_EQ(run(s), 1);
    // An empty Document gets the default Line.
    ASSERT_EQ(session.document().lines().size(), 1u);
    EXPECT_EQ(session.document().lines()[0]->end.value.microseconds(), 5'000'000);
}

TEST_F(Select, AnActionOnAnEarlierSelectionIsOneStepEvenWithoutMatches)
{
    // Departure: legacy records no step when nothing matched (the change
    // joins the next step); here it is its own "Selecting lines" step.
    select({2}, 2);
    S s = find(u8"nothing");
    s.mode = S::Mode::AddToSelection;
    s.action = S::Action::Delete;
    EXPECT_EQ(run(s), 0);
    EXPECT_EQ(texts().size(), 3u);
    EXPECT_EQ(session.historySize(), 2u);
}

TEST_F(Select, TranslationModeSearchesTheTranslation)
{
    EditSession tl{load("[Script Info]\nTLMode: Yes\nTLMode Style: TLmode\n\n" + std::string(kHeader) +
                        "Dialogue: 0,0:00:01.00,0:00:02.00,TLmode,,0,0,0,,original\n"
                        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,translated\n")};
    ASSERT_EQ(tl.document().lines().size(), 1u);
    auto r = selectLines(tl, find(u8"translated"));
    ASSERT_TRUE(r);
    EXPECT_EQ(r->count, 1);
    r = selectLines(tl, find(u8"original"));
    EXPECT_EQ(r->count, 0);
}

TEST_F(Select, SrtTimesAndCopyWithoutCueNumbers)
{
    auto loaded = core::loadSrt(bytes("1\r\n00:00:01,500 --> 00:00:02,000\r\nfirst\r\n\r\n"
                                      "2\r\n00:00:03,000 --> 00:00:04,000\r\nsecond\r\n\r\n"));
    EditSession srt{std::move(loaded.document)};
    S s = find(u8"01,5", S::Field::Start);
    s.action = S::Action::Copy;
    const auto r = selectLines(srt, s);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->count, 1);
    EXPECT_EQ(*r->clipboard, u8"00:00:01,500 --> 00:00:02,000\r\nfirst\r\n\r\n");
}

TEST(SelectLinesOptions, LegacyBits)
{
    EXPECT_EQ(selectLinesOptions(S{}), 1 | 16 | 1024 | 4096 | 32768);
    S s;
    s.with = false;
    s.matchCase = s.regex = true;
    s.field = S::Field::End;
    s.dialogues = false;
    s.comments = true;
    s.mode = S::Mode::Deselect;
    s.action = S::Action::Delete;
    const int bits = selectLinesOptions(s);
    EXPECT_EQ(bits, 2 | 4 | 8 | 512 | 2048 | (1 << 14) | (1 << 21));
    const S back = selectLinesFromOptions(bits);
    EXPECT_EQ(selectLinesOptions(back), bits);
    // Unset: With, Text, Dialogue on (no Comments bit), Select, Do nothing.
    EXPECT_EQ(selectLinesOptions(selectLinesFromOptions(0)), selectLinesOptions(S{}));
    // The first field bit wins.
    EXPECT_EQ(selectLinesFromOptions(32 | 64).field, S::Field::Style);
}

TEST(SelectLinesOptions, RecentListAndStylesPattern)
{
    std::vector<std::u8string> recent;
    for (int i = 0; i < 20; ++i)
        recent.push_back(std::u8string(1, static_cast<char8_t>(u8'a' + i)));
    recent = addRecentSelection(recent, u8"new");
    EXPECT_EQ(recent.size(), 21u); // cut only when it had more than 20
    EXPECT_EQ(recent.front(), u8"new");
    recent = addRecentSelection(recent, u8"c"); // moves to the front, still 21
    EXPECT_EQ(recent.size(), 21u);
    EXPECT_EQ(recent[0], u8"c");
    EXPECT_EQ(recent[1], u8"new");
    recent = addRecentSelection(recent, u8"other");
    EXPECT_EQ(recent.size(), 20u);
    EXPECT_EQ(recent[0], u8"other");
    EXPECT_EQ(addRecentSelection({u8"x", u8"x"}, u8"x"), (std::vector<std::u8string>{u8"x", u8"x"}));
    EXPECT_EQ(stylesPattern({u8"Default", u8"Sign (top).1", u8"a|b?"}),
              u8"^Default|Sign \\(top\\)\\.1|a\\|b?$");
}
