// E3: translation operations in the Grid against legacy SubsGrid::OnPasteTextTl,
// SetTlMode and MoveTextTL (TLDialog) at 20d647c4.

#include "hikari/application/grid_translation.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

constexpr std::string_view kScript =
    "[Script Info]\n"
    "ScriptType: v4.00+\n"
    "\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,A\n"
    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,B\n"
    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,C\n"
    "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,D\n";

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

std::string str(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::vector<std::string> texts(const EditSession &s)
{
    std::vector<std::string> out;
    for (const auto *l : s.document().lines())
        out.push_back(str(l->text) + "/" + str(l->translation));
    return out;
}

std::string saved(const EditSession &s)
{
    const auto bytes = core::encodeAss(s.document());
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

// A, B, C, D translated a, b, c, d, in translation mode.
struct Translated : ::testing::Test {
    EditSession session{load(kScript)};
    void SetUp() override { ASSERT_TRUE(pasteTranslation(session, u8"a\nb\nc\nd\n", u8"txt")); }
    void selectRow(std::size_t row)
    {
        const auto id = session.document().lines()[row]->id;
        session.setSelection(Selection{id, {id}, id, {}});
    }
};

} // namespace

TEST(PasteTranslation, FillsShownLinesThenAppendsAndTurnsOnTranslationMode)
{
    EditSession session{load(kScript)};
    const auto steps = session.historySize();
    ASSERT_TRUE(pasteTranslation(session, u8"one\ntwo\n\nthree\nfour\nfive\n", u8"txt"));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Pasting translation");
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/one", "B/two", "C/three", "D/four", "/five"}));
    const auto appended = session.document().lines()[4];
    EXPECT_EQ(appended->start.value.microseconds(), 0);
    EXPECT_EQ(appended->end.value.microseconds(), 0);
    EXPECT_EQ(appended->style, u8"Default");
    const auto &d = session.document();
    EXPECT_EQ(d.scriptInfo(u8"TLMode"), u8"Yes");
    EXPECT_EQ(d.scriptInfo(u8"TLMode Style"), u8"TLmode");
    EXPECT_EQ(d.scriptInfo(u8"TLMode Showtl"), u8"Yes");
    const auto styles = core::decodeStyles(d);
    ASSERT_EQ(styles.size(), 2u);
    EXPECT_EQ(styles[1].name, u8"TLmode");
    EXPECT_EQ(styles[1].alignment, u8"8");
    // Saved as legacy writes it: the header, the TLmode Style and pairs.
    const std::string file = saved(session);
    EXPECT_NE(file.find("ScriptType: v4.00+\nTLMode Style: TLmode\nTLMode: Yes\nTLMode Showtl: Yes\n\n"), std::string::npos)
        << file;
    EXPECT_NE(file.find("Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
                        "Style: TLmode,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"),
              std::string::npos)
        << file;
    EXPECT_NE(file.find("Dialogue: 0,0:00:01.00,0:00:02.00,TLmode,,0,0,0,,A\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"),
              std::string::npos)
        << file;
    EXPECT_NE(file.find("Dialogue: 0,0:00:00.00,0:00:00.00,TLmode,,0,0,0,,\nDialogue: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,,five"),
              std::string::npos)
        << file;
    // One step back restores the Lines, Script Info and Styles.
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(session.document().scriptInfo(u8"TLMode"));
    EXPECT_EQ(core::decodeStyles(session.document()).size(), 1u);
    EXPECT_EQ(texts(session)[0], "A/");
}

TEST(PasteTranslation, SrtBlocksAndTheLegacyLastBlock)
{
    EditSession session{load(kScript)};
    // LF: blank lines are skipped, so the last block is never followed by a
    // number and is not pasted.
    ASSERT_TRUE(pasteTranslation(session, u8"1\n00:00:01,000 --> 00:00:02,000\nHello\nthere\n\n2\n00:00:03,000 --> 00:00:04,000\nWorld\n",
                                 u8"srt"));
    EXPECT_EQ(texts(session)[0], "A/Hello\\Nthere");
    EXPECT_EQ(texts(session)[1], "B/");
    // A CRLF file as the Linux build read it (R5-per-platform; Windows' text
    // mode gave the LF text above): a blank line is "\r", which trims to an
    // empty "number" and ends the block, so the last block is pasted too.
    EditSession crlf{load(kScript)};
    ASSERT_TRUE(pasteTranslation(crlf, u8"1\r\n00:00:01,000 --> 00:00:02,000\r\nHello\r\n\r\n2\r\n00:00:03,000 --> 00:00:04,000\r\nWorld\r\n\r\n",
                                 u8"srt"));
    EXPECT_EQ(texts(crlf)[0], "A/Hello");
    EXPECT_EQ(texts(crlf)[1], "B/World");
}

TEST(PasteTranslation, LinuxCrlfTextKeepsItsBlankLinesAsEntries)
{
    // R5-per-platform: the Linux build kept "\r", so wxTOKEN_STRTOK did not
    // skip a blank CRLF line; its "\r" is an entry with no text. (Windows
    // read LF text, where the blank line is skipped.)
    EditSession session{load(kScript)};
    ASSERT_TRUE(pasteTranslation(session, u8"one\r\ntwo\r\n\r\nthree\r\n", u8"txt"));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/one", "B/two", "C/", "D/three"}));
}

TEST(PasteTranslation, AssFilesGiveTheirDialogueLinesAndShownLinesOnly)
{
    EditSession session{load(kScript)};
    const auto b = session.document().lines()[1]->id;
    ASSERT_TRUE(pasteTranslation(session,
                                 u8"[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,uno\n"
                                 u8"Comment: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,skipped\n"
                                 u8"Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,dos\n",
                                 u8"ass", [&](core::LineId id) { return id != b; }));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/uno", "B/", "C/dos", "D/"}));
}

TEST(PasteTranslation, AnEmptyFileStillTurnsTranslationModeOn)
{
    EditSession session{load(kScript)};
    EXPECT_TRUE(pasteTranslation(session, u8"", u8"txt"));
    EXPECT_EQ(session.document().scriptInfo(u8"TLMode"), u8"Yes");
}

TEST_F(Translated, DeleteTranslationLineMovesTheTranslationUp)
{
    selectRow(1);
    const auto steps = session.historySize();
    ASSERT_TRUE(moveTranslation(session, TranslationMove::DeleteTranslationLine));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/a", "B/c", "C/d", "D/d"}));
    EXPECT_EQ(session.history().back().name, "Moving translation text");
    EXPECT_EQ(session.historySize(), steps + 1);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/a", "B/b", "C/c", "D/d"}));
}

TEST_F(Translated, JoinTranslationJoinsWithTheNext)
{
    selectRow(1);
    ASSERT_TRUE(moveTranslation(session, TranslationMove::JoinTranslation));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/a", "B/b\\Nc", "C/d", "D/d"}));
}

TEST_F(Translated, AddTranslationLineMovesTheTranslationDown)
{
    selectRow(1);
    ASSERT_TRUE(moveTranslation(session, TranslationMove::AddTranslationLine));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/a", "B/", "C/b", "D/c", "/d"}));
    EXPECT_EQ(session.document().lines()[4]->style, u8"TLmode");
}

TEST_F(Translated, AddOriginalLineMovesTheOriginalDown)
{
    selectRow(1);
    ASSERT_TRUE(moveTranslation(session, TranslationMove::AddOriginalLine));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/a", "/b", "B/c", "C/d", "D/d"}));
    EXPECT_EQ(session.selection().active, session.document().lines()[1]->id);
}

TEST_F(Translated, JoinAndDeleteOriginalMoveTheOriginalUp)
{
    selectRow(1);
    const auto bStart = session.document().lines()[1]->start.value;
    ASSERT_TRUE(moveTranslation(session, TranslationMove::JoinOriginal));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/a", "B\\NC/b", "D/c", "/d"}));
    EXPECT_EQ(session.document().lines()[1]->start.value, bStart);
    ASSERT_TRUE(session.undo());
    selectRow(1);
    ASSERT_TRUE(moveTranslation(session, TranslationMove::DeleteOriginalLine));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/a", "C/b", "D/c", "/d"}));
}

TEST_F(Translated, TheCountIsTheDistanceBetweenTheFirstTwoSelected)
{
    const auto lines = session.document().lines();
    session.setSelection(Selection{lines[0]->id, {lines[0]->id, lines[2]->id}, lines[0]->id, {}});
    ASSERT_TRUE(moveTranslation(session, TranslationMove::DeleteTranslationLine));
    // Rows past the count without a partner keep their translation (legacy loop).
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A/c", "B/d", "C/c", "D/d"}));
}

TEST(MoveTranslation, OnlyInTranslationModeWithTheOriginalShown)
{
    EditSession session{load(kScript)};
    const auto id = session.document().lines()[0]->id;
    session.setSelection(Selection{id, {id}, id, {}});
    EXPECT_FALSE(moveTranslation(session, TranslationMove::DeleteTranslationLine));
}

TEST(TurnOffTranslationMode, TranslationsBecomeTheTextAndTheTlStyleGoes)
{
    EditSession session{load(kScript)};
    EXPECT_EQ(turnOffTranslationMode(session).error(), CommandRefusal::Invalid); // not in translation mode
    ASSERT_TRUE(pasteTranslation(session, u8"one\ntwo\n", u8"txt"));
    const auto third = session.document().lines()[2]->id;
    ASSERT_TRUE(session.run(Command{"x", session.revision(), {third}, [&](core::Document &d) {
        return d.setLineUnconfirmed(third, true);
    }}));
    const auto steps = session.historySize();
    ASSERT_TRUE(turnOffTranslationMode(session));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Turning off translator mode");
    EXPECT_EQ(texts(session), (std::vector<std::string>{"one/", "two/", "C/", "D/"}));
    EXPECT_FALSE(session.document().lines()[2]->unconfirmed);
    const auto &d = session.document();
    EXPECT_FALSE(d.scriptInfo(u8"TLMode"));
    EXPECT_FALSE(d.scriptInfo(u8"TLMode Style"));
    EXPECT_EQ(d.scriptInfo(u8"TLMode Showtl"), u8"Yes"); // legacy leaves it
    EXPECT_EQ(core::decodeStyles(d).size(), 1u);
    const std::string file = saved(session);
    EXPECT_EQ(file.find("TLmode"), std::string::npos) << file;
    EXPECT_NE(file.find(",one\n"), std::string::npos) << file;
    EXPECT_EQ(file.find(",A\n"), std::string::npos) << file;
}
