// E5: translation mode controls against legacy EditBox (OnTlMode, SetTlMode,
// OnDoubtfulTl, SetTextWithTags) and SubsGrid (SetTlMode, showOriginal,
// LoadSubtitles, DoUndo) at 20d647c4.

#include "hikari/application/grid_translation.h"
#include "hikari/application/translation_mode.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"
#include "hikari/core/subtitle_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

constexpr std::string_view kStyles =
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "Style: O,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"
    "\n";

// Three TLMode pairs as the legacy save writes them (SubsGridBase.cpp:337-350):
// an Unconfirmed one ("\fD" effect on the original), a translated one, and an
// untranslated one (an original and an empty translation line, as legacy
// loads such a file; it saves it as one line).
const std::string kPairs = std::string("[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: O\n\n") +
                           std::string(kStyles) +
                           "[Events]\n"
                           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                           "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,\fD,Gate\n"
                           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"
                           "Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,,Tower\n"
                           "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Wieza\n"
                           "Dialogue: 0,0:00:05.00,0:00:06.00,O,,0,0,0,,Wall\n"
                           "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,\n";

const std::string kPlain = std::string("[Script Info]\nScriptType: v4.00+\n\n") + std::string(kStyles) +
                           "[Events]\n"
                           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,A\n"
                           "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,B\n";

std::vector<std::byte> bytesOf(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

core::Document load(std::string_view text)
{
    return core::loadAss(bytesOf(text)).document;
}

std::string str(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::string saved(const core::Document &d)
{
    const auto bytes = core::encodeAss(d);
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::string events(const std::string &file)
{
    return file.substr(file.find("Format: Layer"));
}

void selectRows(EditSession &s, std::initializer_list<std::size_t> rows)
{
    Selection sel;
    for (const auto row : rows)
        sel.selected.insert(s.document().lines()[row]->id);
    sel.active = s.document().lines()[*rows.begin()]->id;
    s.setSelection(sel);
}

} // namespace

TEST(TranslatorMode, TurningOnIsOneStepAndUndoTakesItBack)
{
    // EditBox::OnTlMode -> SubsGrid::SetTlMode(true) (SubsGridBase.cpp:1313-1334)
    // then SetModified(GRID_TURN_ON_TLMODE): "Turning on translator mode".
    EditSession session{load(kPlain)};
    const auto steps = session.historySize();
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Turning on translator mode");
    const auto &d = session.document();
    EXPECT_EQ(d.scriptInfo(u8"TLMode"), u8"Yes");
    EXPECT_EQ(d.scriptInfo(u8"TLMode Style"), u8"TLmode");
    const auto styles = core::decodeStyles(d);
    ASSERT_EQ(styles.size(), 3u);
    EXPECT_EQ(styles.back().name, u8"TLmode"); // a copy of Default with alignment 8
    EXPECT_EQ(styles.back().alignment, u8"8");
    EXPECT_EQ(styles.back().fontname, u8"Arial");
    // Already on: the check box is checked, nothing turns it on again.
    EXPECT_EQ(turnOnTranslationModeStep(session).error(), CommandRefusal::Invalid);
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(session.document().scriptInfo(u8"TLMode"));
    EXPECT_FALSE(session.document().scriptInfo(u8"TLMode Style"));
    EXPECT_EQ(core::decodeStyles(session.document()).size(), 2u);
    EXPECT_EQ(saved(session.document()), kPlain); // back to the opened bytes
}

TEST(TranslatorMode, OnlyForAss)
{
    // The check box is enabled for ASS only (HikariSubFrame.cpp:2401,
    // SubsGridBase.cpp:1239-1246).
    const auto srt = core::loadSubtitle(bytesOf("1\n00:00:01,000 --> 00:00:02,000\nA\n\n"), u8"srt");
    ASSERT_TRUE(srt);
    EditSession session{srt->document};
    EXPECT_EQ(turnOnTranslationModeStep(session).error(), CommandRefusal::Invalid);
}

TEST(TranslatorMode, TurningOffAndOnRoundTripsTheTLModeFile)
{
    // C87-unconfirmed-roundtrip: the "\fD" original loads Unconfirmed.
    EditSession session{load(kPairs)};
    auto lines = session.document().lines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_TRUE(lines[0]->unconfirmed);
    EXPECT_EQ(lines[1]->translation, u8"Wieza");
    EXPECT_TRUE(lines[2]->translation.empty());
    // Unchanged, the file saves as it was opened.
    EXPECT_EQ(saved(session.document()), kPairs);

    // SetTlMode(false) (SubsGridBase.cpp:1336-1373): TLMode and the TLMode
    // Style go, translations become the text, Unconfirmed clears; one step.
    const auto steps = session.historySize();
    ASSERT_TRUE(turnOffTranslationMode(session));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Turning off translator mode");
    // SaveFile without TLMode writes every Dialogue as one line (GetRaw):
    // the untranslated pair too, in its translation line's fields with the
    // original's text.
    const std::string off = saved(session.document());
    EXPECT_EQ(events(off), "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"
                           "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Wieza\n"
                           "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,Wall\n");
    EXPECT_EQ(off.find("TLMode"), std::string::npos);
    EXPECT_EQ(off.find("Style: O,"), std::string::npos);
    // Reloaded: three plain Lines, nothing Unconfirmed.
    {
        const auto reloaded = load(off);
        ASSERT_EQ(reloaded.lines().size(), 3u);
        for (const auto *l : reloaded.lines()) {
            EXPECT_FALSE(l->unconfirmed);
            EXPECT_TRUE(l->translation.empty());
        }
    }

    // One Undo brings the pairs back, Unconfirmed included, and they save as
    // they were opened.
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(saved(session.document()), kPairs);
    lines = session.document().lines();
    EXPECT_TRUE(lines[0]->unconfirmed);

    // Mark the second pair Unconfirmed too: saved and reloaded, both are.
    selectRows(session, {1});
    ASSERT_TRUE(toggleUnconfirmed(session));
    const std::string marked = saved(session.document());
    EXPECT_NE(marked.find("Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,\fD,Tower\n"
                          "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Wieza\n"),
              std::string::npos)
        << marked;
    const auto reloaded = load(marked);
    ASSERT_EQ(reloaded.lines().size(), 3u);
    EXPECT_TRUE(reloaded.lines()[0]->unconfirmed);
    EXPECT_TRUE(reloaded.lines()[1]->unconfirmed);
    EXPECT_FALSE(reloaded.lines()[2]->unconfirmed);
}

TEST(TranslatorMode, TurnedOffThenOnAgainMakesANewTLModeStyle)
{
    // After SetTlMode(false) the file has no TLMode: turning it on again
    // copies Default as TLmode (the O Style went with the mode).
    EditSession session{load(kPairs)};
    ASSERT_TRUE(turnOffTranslationMode(session));
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    EXPECT_EQ(session.document().scriptInfo(u8"TLMode Style"), u8"TLmode");
    EXPECT_EQ(session.document().scriptInfo(u8"TLMode"), u8"Yes");
    // No Line has a translation, so each saves as one line (GetRaw(&raw, false)).
    EXPECT_EQ(events(saved(session.document())),
              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"
              "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Wieza\n"
              "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,Wall\n");
}

TEST(NotConfirmed, FlipsEverySelectedLineAsOneStep)
{
    // EditBox::OnDoubtfulTl (EditBox.cpp:1946-1962): ChangeState(4) on each
    // selected Line, so a mixed selection flips each one.
    EditSession session{load(kPairs)};
    selectRows(session, {0, 2});
    const auto steps = session.historySize();
    ASSERT_TRUE(toggleUnconfirmed(session));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Mark unconfirmed");
    auto lines = session.document().lines();
    EXPECT_FALSE(lines[0]->unconfirmed);
    EXPECT_FALSE(lines[1]->unconfirmed); // not selected
    EXPECT_TRUE(lines[2]->unconfirmed);
    // An untranslated Unconfirmed Line saves as its pair (SubsGridBase.cpp:342).
    EXPECT_NE(saved(session.document()).find("Dialogue: 0,0:00:05.00,0:00:06.00,O,,0,0,0,\fD,Wall\n"
                                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,\n"),
              std::string::npos);
    ASSERT_TRUE(session.undo());
    lines = session.document().lines();
    EXPECT_TRUE(lines[0]->unconfirmed);
    EXPECT_FALSE(lines[2]->unconfirmed);
}

TEST(NotConfirmed, IsItsOwnStepNotJoinedToTheNextEdit)
{
    // E5-unconfirmed-own-step: legacy OnDoubtfulTl (EditBox.cpp:1946-1962)
    // flips State bit 4 without SetModified, so the flip joins whichever step
    // comes next and one Undo takes both away. Here it is its own
    // "Mark unconfirmed" step: the next edit undoes on its own.
    EditSession session{load(kPairs)};
    selectRows(session, {1});
    const auto id = session.document().lines()[1]->id;
    const auto steps = session.historySize();
    ASSERT_TRUE(toggleUnconfirmed(session));
    ASSERT_TRUE(session.editDraftText(id, u8"Turret"));
    ASSERT_TRUE(session.commitDraft());
    ASSERT_EQ(session.historySize(), steps + 2);
    EXPECT_EQ(session.history()[steps].name, "Mark unconfirmed");
    ASSERT_TRUE(session.undo()); // the text edit only
    EXPECT_EQ(session.document().lines()[1]->text, u8"Tower");
    EXPECT_TRUE(session.document().lines()[1]->unconfirmed);
    ASSERT_TRUE(session.undo()); // then the flip
    EXPECT_FALSE(session.document().lines()[1]->unconfirmed);

    // A pending draft on the Line is committed as its own step first, then the
    // flip follows as "Mark unconfirmed"; Undo takes the flip away alone.
    EditSession drafted{load(kPairs)};
    selectRows(drafted, {1});
    const auto draftedId = drafted.document().lines()[1]->id;
    const auto before = drafted.historySize();
    ASSERT_TRUE(drafted.editDraftText(draftedId, u8"Turret"));
    ASSERT_TRUE(toggleUnconfirmed(drafted));
    ASSERT_EQ(drafted.historySize(), before + 2);
    EXPECT_NE(drafted.history()[before].name, "Mark unconfirmed");
    EXPECT_EQ(drafted.history()[before + 1].name, "Mark unconfirmed");
    EXPECT_FALSE(drafted.draftLine());
    ASSERT_TRUE(drafted.undo());
    EXPECT_FALSE(drafted.document().lines()[1]->unconfirmed);
    EXPECT_EQ(drafted.document().lines()[1]->text, u8"Turret");
}

TEST(NotConfirmed, OnlyInTranslationMode)
{
    // `if (!grid->hasTLMode){ wxBell(); return; }`
    EditSession plain{load(kPlain)};
    selectRows(plain, {0});
    EXPECT_EQ(toggleUnconfirmed(plain).error(), CommandRefusal::Invalid);
    EditSession pairs{load(kPairs)};
    pairs.setSelection({});
    EXPECT_EQ(toggleUnconfirmed(pairs).error(), CommandRefusal::Invalid);
}

// Legacy SetTextWithTags (EditBox.cpp:1811-1858), worked through by hand on
// wxString: Replace("}{", ""), Find('}'), find("{"), Mid and SubString.
TEST(MovingTags, SplitsTheOverrideBlocksFromTheText)
{
    struct Case {
        std::u8string text, original, translation;
        std::size_t caret;
    };
    const Case cases[] = {
        // A leading block: the caret goes after it (pos = txtTl.length()).
        {u8"{\\i1}Gate keeper", u8"Gate keeper", u8"{\\i1}", 5},
        // Blocks inside the text: the caret stays at 0.
        {u8"Gate {\\i1}keeper{\\i0}", u8"Gate keeper", u8"{\\i1}{\\i0}", 0},
        // "}{" is removed first, so adjacent blocks become one.
        {u8"{\\b1}{\\i1}A}{B", u8"AB", u8"{\\b1\\i1}", 8},
        // A '}' with no '{' before it: the text up to it stays in the original.
        {u8"a}b", u8"a}b", u8"", 0},
        // A '{' after the first '}': SubString(brackets, getr) with getr before
        // brackets takes the rest, then the loop reads the same block again.
        {u8"a}b{c}", u8"a}bb", u8"{c}{c}", 0},
        // An unclosed block after the last '}' stays in the original.
        {u8"{\\an8}top {open", u8"top {open", u8"{\\an8}", 6},
        // The caret counts UTF-16 units of the leading block.
        {u8"{\\fn\U0001D400}x", u8"x", u8"{\\fn\U0001D400}", 7},
    };
    for (const auto &c : cases) {
        const auto moved = moveTagsFromOriginal(c.text);
        ASSERT_TRUE(moved) << str(c.text);
        EXPECT_EQ(str(moved->original), str(c.original)) << str(c.text);
        EXPECT_EQ(str(moved->translation), str(c.translation)) << str(c.text);
        EXPECT_EQ(moved->caret, c.caret) << str(c.text);
    }
    // No '}': shown whole (Text.Find('}') == -1).
    EXPECT_FALSE(moveTagsFromOriginal(u8"Gate keeper"));
    EXPECT_FALSE(moveTagsFromOriginal(u8"{\\i1 never closed"));
    EXPECT_FALSE(moveTagsFromOriginal(u8""));
}

TEST(OriginalColumns, LoadTakesTheDocumentAndTheOption)
{
    // LoadSubtitles (SubsGridBase.cpp:1214-1216): hasTLMode && (Showtl || option).
    const auto doc = [](const char *extra) {
        return load(std::string("[Script Info]\n") + extra + "[Events]\n");
    };
    EXPECT_TRUE(OriginalColumns{}.observe(doc("TLMode: Yes\nTLMode Showtl: Yes\n"), false));
    EXPECT_TRUE(OriginalColumns{}.observe(doc("TLMode: Yes\n"), true));
    EXPECT_FALSE(OriginalColumns{}.observe(doc("TLMode: Yes\n"), false));
    EXPECT_FALSE(OriginalColumns{}.observe(doc("TLMode Showtl: Yes\n"), true)); // not in translation mode
    // The option changing later does not change the Grid (legacy reads it
    // only at these moments).
    OriginalColumns columns;
    const auto on = doc("TLMode: Yes\n");
    EXPECT_FALSE(columns.observe(on, false));
    EXPECT_FALSE(columns.observe(on, true));
}

TEST(OriginalColumns, FollowsTheSwitchPasteAndUndo)
{
    EditSession session{load(kPlain)};
    OriginalColumns columns;
    EXPECT_FALSE(columns.observe(session.document(), false));
    // SetTlMode(true) without TL_MODE_SHOW_ORIGINAL leaves it as it was.
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    columns.turnedOn(session.document(), false);
    EXPECT_FALSE(columns.observe(session.document(), false));
    // OnPasteTextTl shows it and writes "TLMode Showtl: Yes".
    ASSERT_TRUE(pasteTranslation(session, u8"a\nb\n", u8"txt"));
    columns.pasted(session.document());
    EXPECT_TRUE(columns.observe(session.document(), false));
    // SetTlMode(false) hides it; Showtl stays in Script Info.
    ASSERT_TRUE(turnOffTranslationMode(session));
    columns.turnedOff(session.document());
    EXPECT_FALSE(columns.observe(session.document(), false));
    // Turning on again without the option: legacy keeps it hidden although
    // the Document says Showtl.
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    columns.turnedOn(session.document(), false);
    EXPECT_FALSE(columns.observe(session.document(), false));
    // DoUndo when TLMode changes (SubsGridBase.cpp:994-998): Showtl, or
    // TLMode Yes with the option. Undoing the turn-on leaves no TLMode but
    // Showtl: legacy shows the two columns outside translation mode.
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(session.document().scriptInfo(u8"TLMode"));
    EXPECT_TRUE(columns.observe(session.document(), false));
    // Undoing the turn-off: TLMode is Yes again, and Showtl shows them.
    ASSERT_TRUE(session.undo());
    EXPECT_TRUE(columns.observe(session.document(), false));
    // Observing again without a change keeps them.
    EXPECT_TRUE(columns.observe(session.document(), false));
}

TEST(OriginalColumns, UndoWithoutShowtlFollowsTheOption)
{
    EditSession session{load(kPlain)};
    OriginalColumns columns;
    columns.observe(session.document(), true);
    ASSERT_TRUE(turnOnTranslationModeStep(session));
    columns.turnedOn(session.document(), true);
    EXPECT_TRUE(columns.shown());
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(columns.observe(session.document(), true)); // no TLMode, no Showtl
    ASSERT_TRUE(session.redo());
    EXPECT_TRUE(columns.observe(session.document(), true));
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(columns.observe(session.document(), false));
    ASSERT_TRUE(session.redo());
    EXPECT_FALSE(columns.observe(session.document(), false)); // option off now
}
