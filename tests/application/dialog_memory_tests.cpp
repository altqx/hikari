// AD2 (#311): Repeat last run's matching, filling a dialog by name and the
// safe-prefill rule (docs/automation.md).
#include "hikari/application/dialog_memory.h"

#include <gtest/gtest.h>

using namespace hikari::application;

namespace {

DialogControl control(std::string kind, std::string name, int x, int y)
{
    DialogControl c;
    c.kind = std::move(kind);
    c.name = std::move(name);
    c.x = x;
    c.y = y;
    return c;
}

// GradientEverything's dialog in small: a label, tags, accel, a border box
// and a mode dropdown, with Apply and Cancel.
DialogRequest gradient()
{
    DialogRequest r;
    DialogControl label = control("label", "", 0, 0);
    label.label = "Tags:";
    DialogControl tags = control("edit", "tags", 1, 0);
    tags.text = "\\c\\3c\\alpha\\blur";
    DialogControl accel = control("floatedit", "accel", 1, 1);
    accel.number = 1;
    accel.numberMin = 0.1;
    accel.numberMax = 10;
    accel.step = 0.05;
    DialogControl bord = control("checkbox", "bord", 1, 2);
    bord.checked = true;
    DialogControl mode = control("dropdown", "mode", 1, 3);
    mode.items = {"character", "line"};
    mode.text = "character";
    DialogControl count = control("intedit", "count", 1, 4);
    count.intValue = 3;
    count.intMin = 1;
    count.intMax = 10;
    r.controls = {label, tags, accel, bord, mode, count};
    r.buttons = {"Apply", "Cancel"};
    r.buttonIds = {"ok", "cancel"};
    return r;
}

DialogResult answered(const DialogRequest &r)
{
    DialogResult a = initialDialogResult(r);
    a.pressed = 0;
    a.values[1] = std::string("\\c\\3c\\blur");
    a.values[2] = 1.6;
    a.values[3] = false;
    a.values[4] = std::string("line");
    a.values[5] = 7;
    return a;
}

} // namespace

TEST(DialogMemory, SameDialogIsTheSameControlsTextAndButtonsNotValues)
{
    const DialogRequest last = gradient();
    DialogRequest now = gradient();
    // The script works out another value: still the same dialog.
    now.controls[1].text = "\\bord";
    now.controls[2].number = 4;
    now.controls[3].checked = false;
    EXPECT_TRUE(sameDialog(last, now));

    const auto differs = [&](auto change) {
        DialogRequest r = gradient();
        change(r);
        return !sameDialog(last, r);
    };
    EXPECT_TRUE(differs([](DialogRequest &r) { r.controls[2].kind = "intedit"; }));      // class
    EXPECT_TRUE(differs([](DialogRequest &r) { r.controls[2].name = "acceleration"; })); // name
    EXPECT_TRUE(differs([](DialogRequest &r) { r.controls[2].y = 5; }));                 // place
    EXPECT_TRUE(differs([](DialogRequest &r) { r.controls[1].width = 3; }));             // span
    EXPECT_TRUE(differs([](DialogRequest &r) { r.controls[4].items.push_back("syllable"); }));
    EXPECT_TRUE(differs([](DialogRequest &r) { r.controls.push_back(control("checkbox", "clip", 1, 8)); }));
    EXPECT_TRUE(differs([](DialogRequest &r) { std::swap(r.controls[2], r.controls[3]); })); // order
    EXPECT_TRUE(differs([](DialogRequest &r) { r.buttons[0] = "Go"; }));
    EXPECT_TRUE(differs([](DialogRequest &r) { r.buttonIds[1] = "close"; }));
    EXPECT_TRUE(differs([](DialogRequest &r) { r.buttons.push_back("Help"); }));
    // What the dialog says: a trust question naming another feed in a label
    // is not answered as the last one was.
    EXPECT_TRUE(differs([](DialogRequest &r) { r.controls[0].label = "From https://example.org/feed.json"; }));
    EXPECT_TRUE(differs([](DialogRequest &r) { r.controls[3].label = "Ignore the border"; })); // a check box's
    EXPECT_TRUE(differs([](DialogRequest &r) { r.controls[1].hint = "a new hint"; }));
}

TEST(DialogMemory, AQuestionHasNothingToFillIn)
{
    DialogRequest trust;
    trust.controls = {control("label", "", 0, 0)};
    trust.buttons = {"Cancel", "Trust this time"};
    EXPECT_TRUE(isQuestion(trust));
    EXPECT_TRUE(isQuestion(DialogRequest{})); // the default OK / Cancel and nothing else
    EXPECT_FALSE(isQuestion(gradient()));
    trust.controls.push_back(control("checkbox", "always", 0, 1));
    EXPECT_FALSE(isQuestion(trust));
}

TEST(DialogMemory, ReplaySendsTheRecordedAnswerAsTheControlsTakeIt)
{
    const DialogRequest r = gradient();
    const DialogResult recorded = answered(r);
    DialogRequest now = gradient();
    now.controls[1].text = "\\bord"; // the script's value differs: the answer is still sent
    const DialogResult replay = replayedAnswer(now, recorded);
    EXPECT_EQ(replay.pressed, 0);
    EXPECT_EQ(replay.values, recorded.values);

    // A range that shrank since: the value is clamped as the dialog would.
    now.controls[5].intMax = 5;
    now.controls[2].numberMax = 1.5;
    const DialogResult clamped = replayedAnswer(now, recorded);
    EXPECT_EQ(clamped.values[5], DialogValue(5));
    EXPECT_EQ(clamped.values[2], DialogValue(1.5));
}

TEST(DialogMemory, CancelIsNoButtonTheDefaultCancelOrACancelId)
{
    const DialogRequest r = gradient();
    EXPECT_TRUE(cancelAnswer(r, -1));
    EXPECT_FALSE(cancelAnswer(r, 0));
    EXPECT_TRUE(cancelAnswer(r, 1));
    DialogRequest pair = r;
    pair.buttons.clear();
    pair.buttonIds.clear();
    EXPECT_FALSE(cancelAnswer(pair, 0)); // OK
    EXPECT_TRUE(cancelAnswer(pair, 1));  // Cancel
    DialogRequest noIds = r;
    noIds.buttonIds = {"", ""};
    EXPECT_FALSE(cancelAnswer(noIds, 1)); // a script's own "Cancel" with no id is a button like any
}

TEST(DialogMemory, FillByNameSkipsWhatAControlDoesNotTake)
{
    const DialogRequest r = gradient();
    NamedDialogValues values{
        {"tags", std::string("\\fs")},
        {"accel", 99.0},                    // clamped to 10
        {"bord", std::string("yes")},       // no boolean: skipped
        {"mode", std::string("syllable")},  // not one of the items: skipped
        {"count", 4.0},                     // a whole number fills an intedit
        {"gone", true},                     // no such control
    };
    const DialogResult filled = filledByName(r, initialDialogResult(r), values);
    EXPECT_EQ(filled.values[1], DialogValue(std::string("\\fs")));
    EXPECT_EQ(filled.values[2], DialogValue(10.0));
    EXPECT_EQ(filled.values[3], DialogValue(true));
    EXPECT_EQ(filled.values[4], DialogValue(std::string("character")));
    EXPECT_EQ(filled.values[5], DialogValue(4));
    EXPECT_EQ(filledByName(r, initialDialogResult(r), {{"count", 4.5}}).values[5], DialogValue(3));
}

TEST(DialogMemory, SafePrefillKeepsAValueTheScriptWorkedOut)
{
    const DialogRequest r = gradient();
    const LastDialogAnswer last = lastDialogAnswer(r, answered(r));
    EXPECT_EQ(last.pressed, 0);
    EXPECT_EQ(last.sent.count(""), 0u); // the label has no value

    // The script sends what it sent last time: every last answer is filled in.
    EXPECT_EQ(safePrefill(r, last).values, answered(r).values);

    // It works out tags and count now (from the selection, say): those win.
    DialogRequest now = gradient();
    now.controls[1].text = "\\1c";
    now.controls[5].intValue = 2;
    // and a control it did not have last time keeps its value.
    DialogControl clip = control("checkbox", "clip", 1, 8);
    clip.checked = true;
    now.controls.push_back(clip);
    const DialogResult shown = safePrefill(now, last);
    EXPECT_EQ(shown.values[1], DialogValue(std::string("\\1c")));
    EXPECT_EQ(shown.values[2], DialogValue(1.6));
    EXPECT_EQ(shown.values[3], DialogValue(false));
    EXPECT_EQ(shown.values[4], DialogValue(std::string("line")));
    EXPECT_EQ(shown.values[5], DialogValue(2));
    EXPECT_EQ(shown.values[6], DialogValue(true));

    // No last answer: the script's values.
    EXPECT_EQ(safePrefill(r, {}).values, initialDialogResult(r).values);
}
