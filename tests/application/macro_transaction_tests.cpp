#include "hikari/application/macro_transaction.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

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

}

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
    r.dialogues[0].text = "{\\be1}one";
    r.dialogues.erase(r.dialogues.begin() + 1);
    MacroDialogueLine added = r.dialogues[0];
    added.id = 0;
    added.text = "added";
    r.dialogues.push_back(added);
    std::swap(r.dialogues[0], r.dialogues[1]);
    ASSERT_TRUE(applyMacroResult(session, s, r, "Blur"));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"three", "{\\be1}one", "added"}));
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
    EXPECT_EQ(lines[1]->id, l1);
}

TEST_F(MacroTest, LinesAfterAReplacementKeepTheirPlaceAndIdentity)
{
    const auto s = snapshot();
    MacroResult r = unchanged(s);
    std::vector<MacroDialogueLine> fresh(2, r.dialogues[1]);
    for (std::size_t i = 0; i < fresh.size(); ++i) {
        fresh[i].id = 0;
        fresh[i].text = "two " + std::to_string(i + 1);
    }
    r.dialogues.erase(r.dialogues.begin() + 1);
    r.dialogues.insert(r.dialogues.begin() + 1, fresh.begin(), fresh.end());
    ASSERT_TRUE(applyMacroResult(session, s, r, "Regenerate"));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"one", "two 1", "two 2", "three"}));
    const auto lines = session.document().lines();
    EXPECT_EQ(lines.front()->id, l1);
    EXPECT_EQ(lines.back()->id, l3);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(session), (std::vector<std::string>{"one", "two", "three"}));
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
    select({l3}, l3);
    EXPECT_EQ(session.selection().active, l3);
    session.setReadOnly(false);
    MacroResult r = unchanged(s);
    r.dialogues[0].text = "done";
    EXPECT_TRUE(applyMacroResult(session, s, r, "Edit"));
}

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
    r.info.erase(r.info.begin() + 1);
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

TEST_F(MacroTest, ReturnedRowsCountTheHeaderTheMacroLeft)
{
    select({l1}, l1);
    const auto s = snapshot();
    MacroResult r = unchanged(s);
    r.info.push_back({"PlayResY", "360"});
    r.selected = std::vector<int>{6};
    ASSERT_TRUE(applyMacroResult(session, s, r, "Rows"));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{l2}));
    EXPECT_EQ(session.selection().active, l2);
}

TEST_F(MacroTest, ReturnedSelectionFollowsTheLegacyRules)
{
    select({l1}, l1);
    auto s = snapshot();
    MacroResult r = unchanged(s);
    r.selected = std::vector<int>{5, 6};
    ASSERT_TRUE(applyMacroResult(session, s, r, "Select"));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{l2, l3}));
    EXPECT_EQ(session.selection().active, l2);

    s = snapshot();
    r = unchanged(s);
    r.selected = std::vector<int>{4, 5};
    r.active = 6;
    ASSERT_TRUE(applyMacroResult(session, s, r, "Select"));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{l1, l2}));
    EXPECT_EQ(session.selection().active, l3);

    s = snapshot();
    r = unchanged(s);
    r.selected = std::vector<int>{6, 2, 4};
    r.active = 99;
    ASSERT_TRUE(applyMacroResult(session, s, r, "Select"));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{l3}));
    EXPECT_EQ(session.selection().active, l3);

    s = snapshot();
    r = unchanged(s);
    r.dialogues.erase(r.dialogues.begin());
    r.selected = std::vector<int>{4};
    ASSERT_TRUE(applyMacroResult(session, s, r, "Delete"));
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{l2}));
}

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
