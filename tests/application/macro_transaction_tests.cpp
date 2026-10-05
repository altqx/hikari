// L4 (A33-transaction): a macro's staged edits against a snapshot, applied as
// one undo step or rejected without changing anything; script indices and
// the returned selection follow the legacy rules (Automation.cpp LuaCommand::Run).

#include "hikari/application/macro_transaction.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

// Two info lines and one style: dialogue script indices start at 4.
constexpr std::string_view kScript =
    "[Script Info]\n"
    "Title: macro\n"
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
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two\n"
    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,three\n";

constexpr std::string_view kFormat =
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n";

std::string save(const EditSession &s)
{
    const auto bytes = core::encodeAss(s.document());
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::vector<std::string> texts(const EditSession &s)
{
    std::vector<std::string> out;
    for (const auto *l : s.document().lines())
        out.emplace_back(l->text.begin(), l->text.end());
    return out;
}

MacroResult unchanged(const MacroSnapshot &s)
{
    return MacroResult{s.info, s.styles, s.dialogues, std::nullopt, std::nullopt};
}

struct MacroTest : ::testing::Test {
    EditSession session{load(kScript)};
    core::LineId l1{1}, l2{2}, l3{3};

    void select(std::set<core::LineId> lines, std::optional<core::LineId> active)
    {
        session.setSelection(Selection{active, std::move(lines)});
    }
    MacroSnapshot snapshot()
    {
        auto s = snapshotForMacro(session);
        EXPECT_TRUE(s);
        return s ? *s : MacroSnapshot{};
    }
};

} // namespace

TEST_F(MacroTest, SnapshotListsInfoStylesAndDialogueWithLegacyIndices)
{
    select({l2, l3}, l3);
    const auto s = snapshot();
    EXPECT_EQ(s.revision, session.revision());
    ASSERT_EQ(s.info.size(), 2u);
    EXPECT_EQ(s.info[0], (MacroInfoLine{"Title", "macro"}));
    ASSERT_EQ(s.styles.size(), 1u);
    EXPECT_EQ(s.styles[0].fields.front(), "Default");
    ASSERT_EQ(s.dialogues.size(), 3u);
    EXPECT_EQ(s.dialogues[1].text, "two");
    EXPECT_EQ(s.dialogues[1].startMs, 3000);
    EXPECT_EQ(s.dialogues[1].raw, "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two");
    EXPECT_EQ(s.selected, (std::vector<int>{5, 6}));
    EXPECT_EQ(s.active, 6);
}

TEST_F(MacroTest, AllStagedEditsApplyAsOneUndoStep)
{
    const auto s = snapshot();
    const auto steps = session.historySize();
    MacroResult r = unchanged(s);
    r.dialogues[0].text = "{\\be1}one";                // modify
    r.dialogues.erase(r.dialogues.begin() + 1);          // delete "two"
    MacroDialogueLine added = r.dialogues[0];
    added.id = 0;
    added.text = "added";
    r.dialogues.push_back(added);                        // append
    std::swap(r.dialogues[0], r.dialogues[1]);           // reorder
    ASSERT_TRUE(applyMacroResult(session, s, r, "Blur"));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"three", "{\\be1}one", "added"}));
    // A reordered Line keeps its identity.
    EXPECT_EQ(session.document().lines()[0]->id, l3);
    EXPECT_EQ(session.document().lines()[1]->id, l1);
    EXPECT_EQ(session.historySize(), steps + 1);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(session), (std::vector<std::string>{"one", "two", "three"}));
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(texts(session), (std::vector<std::string>{"three", "{\\be1}one", "added"}));
}

TEST_F(MacroTest, InsertedLinesTakeTheStagedFieldsAndPosition)
{
    const auto s = snapshot();
    MacroResult r = unchanged(s);
    MacroDialogueLine first;
    first.comment = true;
    first.layer = 3;
    first.startMs = 500;
    first.endMs = 900;
    first.style = "Default";
    first.actor = "Narrator";
    first.marginL = 12;
    first.effect = "fx";
    first.text = "first";
    r.dialogues.insert(r.dialogues.begin(), first);
    ASSERT_TRUE(applyMacroResult(session, s, r, "Insert"));
    const auto lines = session.document().lines();
    ASSERT_EQ(lines.size(), 4u);
    const auto &l = *lines[0];
    EXPECT_EQ(l.text, u8"first");
    EXPECT_TRUE(l.comment);
    EXPECT_EQ(l.layer.value, 3);
    EXPECT_EQ(l.start.value.microseconds(), 500'000);
    EXPECT_EQ(l.end.value.microseconds(), 900'000);
    EXPECT_EQ(l.actor, u8"Narrator");
    EXPECT_EQ(l.marginLeft.value, 12);
    EXPECT_EQ(l.effect, u8"fx");
    EXPECT_EQ(lines[1]->id, l1); // existing Lines keep their identity
}

TEST_F(MacroTest, AnUnchangedResultAddsNoStep)
{
    const auto s = snapshot();
    const auto steps = session.historySize();
    ASSERT_TRUE(applyMacroResult(session, s, unchanged(s), "Nothing"));
    EXPECT_EQ(session.historySize(), steps);
}

TEST_F(MacroTest, AStaleResultChangesNothing)
{
    const auto s = snapshot();
    ASSERT_TRUE(session.run(Command{"Edit", session.revision(), {l1},
                                    [this](core::Document &d) { return d.setLineText(l1, u8"meanwhile"); }}));
    MacroResult r = unchanged(s);
    r.dialogues[1].text = "late";
    const auto applied = applyMacroResult(session, s, r, "Late");
    ASSERT_FALSE(applied);
    EXPECT_EQ(applied.error().error, MacroApplyError::Refused);
    EXPECT_EQ(applied.error().refusal, CommandRefusal::StaleRevision);
    EXPECT_EQ(texts(session), (std::vector<std::string>{"meanwhile", "two", "three"}));
    // Even an unchanged result reports the revision it missed.
    EXPECT_EQ(applyMacroResult(session, s, unchanged(s), "Late").error().refusal, CommandRefusal::StaleRevision);
}

TEST_F(MacroTest, AProtectedReferenceRefusesTheResult)
{
    EditSession reference(load(kScript), /*protectedReference=*/true);
    const auto s = snapshotForMacro(reference);
    ASSERT_TRUE(s);
    MacroResult r = unchanged(*s);
    r.dialogues[0].text = "changed";
    const auto applied = applyMacroResult(reference, *s, r, "Edit");
    ASSERT_FALSE(applied);
    EXPECT_EQ(applied.error().refusal, CommandRefusal::Protected);
    EXPECT_EQ(texts(reference), (std::vector<std::string>{"one", "two", "three"}));
}

TEST_F(MacroTest, APendingDraftIsCommittedBeforeTheSnapshot)
{
    select({l2}, l2);
    ASSERT_TRUE(session.editDraftText(l2, u8"typed"));
    const auto steps = session.historySize();
    const auto s = snapshot();
    EXPECT_FALSE(session.draftLine());
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(s.dialogues[1].text, "typed");
    MacroResult r = unchanged(s);
    r.dialogues[1].text += "!";
    ASSERT_TRUE(applyMacroResult(session, s, r, "Bang"));
    // The draft and the macro are separate steps.
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(session)[1], "typed");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(session)[1], "two");
}

TEST_F(MacroTest, TheTargetIsReadOnlyWhileTheMacroRuns)
{
    const auto s = snapshot();
    session.setReadOnly(true);
    EXPECT_FALSE(session.editDraftText(l1, u8"typed"));
    EXPECT_EQ(session.run(Command{"Edit", session.revision(), {l1},
                                  [this](core::Document &d) { return d.setLineText(l1, u8"x"); }})
                  .error(),
              CommandRefusal::ReadOnly);
    select({l3}, l3); // selection still moves
    EXPECT_EQ(session.selection().active, l3);
    session.setReadOnly(false);
    MacroResult r = unchanged(s);
    r.dialogues[0].text = "done";
    EXPECT_TRUE(applyMacroResult(session, s, r, "Edit"));
}

// S4: Style and Script Info edits (legacy AutoToFile edits the SInfo and
// Styles lists in place, AutomationToFile.cpp:611-625, 822-830) apply with
// the Lines in the macro's one step, which Undo and Redo restore together.
TEST_F(MacroTest, StyleAndInfoEditsApplyWithTheLinesAsOneStep)
{
    const auto s = snapshot();
    const auto steps = session.historySize();
    MacroResult r = unchanged(s);
    r.styles[0].fields[1] = "Times";
    r.styles.push_back(MacroStyleLine{{"Sign", "Arial", "30", "&H00FFFFFF", "&H000000FF", "&H00000000", "&H00000000",
                                       "0", "0", "0", "0", "100", "100", "0", "0", "1", "2", "2", "8", "10", "10",
                                       "10", "1"}});
    r.info[0].value = "renamed";
    r.info.push_back({"PlayResX", "640"});
    r.dialogues[0].text = "changed";
    ASSERT_TRUE(applyMacroResult(session, s, r, "Header"));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(texts(session)[0], "changed");
    EXPECT_EQ(save(session),
              "[Script Info]\n"
              "Title: renamed\n"
              "ScriptType: v4.00+\n"
              "PlayResX: 640\n"
              "\n"
              "[V4+ Styles]\n" +
                  std::string(kFormat) +
                  "Style: Default,Times,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
                  "Style: Sign,Arial,30,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"
                  "\n"
                  "[Events]\n"
                  "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,changed\n"
                  "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two\n"
                  "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,three\n");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(save(session), std::string(kScript));
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(session.document().scriptInfo(u8"PlayResX"), std::u8string(u8"640"));
}

// Deleting and reordering: the properties and Styles left keep their bytes;
// comments in Script Info stay where they were.
TEST_F(MacroTest, InfoAndStyleDeletionsKeepTheRest)
{
    constexpr std::string_view kTwoStyles =
        "[Script Info]\n"
        "; a comment\n"
        "Title: macro\n"
        "ScriptType: v4.00+\n"
        "WrapStyle:0\n"
        "\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize\n"
        "Style: A,Arial,20\n"
        "Style: B,Arial,  30\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:02.00,A,,0,0,0,,one\n";
    EditSession other{load(kTwoStyles)};
    const auto s = *snapshotForMacro(other);
    ASSERT_EQ(s.info.size(), 3u);
    MacroResult r{s.info, s.styles, s.dialogues, std::nullopt, std::nullopt};
    r.info.erase(r.info.begin() + 1); // ScriptType
    std::swap(r.styles[0], r.styles[1]);
    ASSERT_TRUE(applyMacroResult(other, s, r, "Delete"));
    const auto bytes = core::encodeAss(other.document());
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()),
              "[Script Info]\n"
              "; a comment\n"
              "Title: macro\n"
              "WrapStyle:0\n"
              "\n"
              "[V4+ Styles]\n"
              "Format: Name, Fontname, Fontsize\n"
              "Style: B,Arial,  30\n"
              "Style: A,Arial,20\n"
              "\n"
              "[Events]\n"
              "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
              "Dialogue: 0,0:00:01.00,0:00:02.00,A,,0,0,0,,one\n");
}

// A header change is refused on a stale target like a Line change.
TEST_F(MacroTest, StaleHeaderChangesAreRefused)
{
    const auto s = snapshot();
    ASSERT_TRUE(session.run(Command{"Edit", session.revision(), {l1},
                                    [this](core::Document &d) { return d.setLineText(l1, u8"x"); }}));
    MacroResult r = unchanged(s);
    r.info[0].value = "renamed";
    const auto applied = applyMacroResult(session, s, r, "Info");
    ASSERT_FALSE(applied);
    EXPECT_EQ(applied.error().refusal, CommandRefusal::StaleRevision);
    EXPECT_EQ(session.document().scriptInfo(u8"Title"), std::u8string(u8"macro"));
}

// Legacy LuaCommand::Run reads the returned rows after the run, against the
// SInfo and Styles sizes the macro left (Automation.cpp:1010).
TEST_F(MacroTest, ReturnedRowsCountTheHeaderTheMacroLeft)
{
    select({l1}, l1);
    const auto s = snapshot(); // dialogue rows 4-6
    MacroResult r = unchanged(s);
    r.info.push_back({"PlayResY", "360"}); // now 5-7
    r.selected = std::vector<int>{6};
    ASSERT_TRUE(applyMacroResult(session, s, r, "Rows"));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{l2}));
    EXPECT_EQ(session.selection().active, l2);
}

TEST_F(MacroTest, ReturnedSelectionFollowsTheLegacyRules)
{
    select({l1}, l1);
    auto s = snapshot();
    // Selected rows: the first becomes active when no active row is given.
    MacroResult r = unchanged(s);
    r.selected = std::vector<int>{5, 6};
    ASSERT_TRUE(applyMacroResult(session, s, r, "Select"));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{l2, l3}));
    EXPECT_EQ(session.selection().active, l2);

    // An active row in range wins over the first selected.
    s = snapshot();
    r = unchanged(s);
    r.selected = std::vector<int>{4, 5};
    r.active = 6;
    ASSERT_TRUE(applyMacroResult(session, s, r, "Select"));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{l1, l2}));
    EXPECT_EQ(session.selection().active, l3);

    // Selected rows stop at the first out of range; an active row out of
    // range keeps the original active Line.
    s = snapshot();
    r = unchanged(s);
    r.selected = std::vector<int>{6, 2, 4};
    r.active = 99;
    ASSERT_TRUE(applyMacroResult(session, s, r, "Select"));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{l3}));
    EXPECT_EQ(session.selection().active, l3);

    // Indices address the Document after the macro's edits.
    s = snapshot();
    r = unchanged(s);
    r.dialogues.erase(r.dialogues.begin());
    r.selected = std::vector<int>{4};
    ASSERT_TRUE(applyMacroResult(session, s, r, "Delete"));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{l2}));
}

// S4-validation-edits: a result whose validation function answered false
// applies nothing, whatever it carries; the Document, its dirty state and
// history stay as they were (legacy AutoToFile kept validation's edits in
// the file without an undo step, LuaCommand::Validate, Automation.cpp:936-969).
TEST_F(MacroTest, AResultThatFailedValidationChangesNothing)
{
    const auto s = snapshot();
    const auto steps = session.historySize();
    const bool dirty = session.isDirty();
    MacroResult r = unchanged(s);
    r.info.push_back({"Validated", "yes"});
    r.dialogues[0].text = "edited while validating";
    r.valid = false;
    const auto applied = applyMacroResult(session, s, r, "Guarded");
    ASSERT_FALSE(applied);
    EXPECT_EQ(applied.error().error, MacroApplyError::Refused);
    EXPECT_EQ(applied.error().refusal, CommandRefusal::Invalid);
    EXPECT_EQ(save(session), std::string(kScript));
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_EQ(session.isDirty(), dirty);
}
