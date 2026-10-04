// A3: the audio box's timing, pinned against legacy AudioDisplay.cpp
// (OnMouseEvent without karaoke, GetBoundarySnap, AddLead, CommitChanges),
// SubsGrid (GetKeyFromPosition, ChangeLine, NextLine, CopyDialogueWithOffset)
// and EditBox::Send at 20d647c4. Expected values are worked out from legacy's
// arithmetic: the view is 48 kHz at zoom 50 (1440 samples, 30 ms a column),
// 1000 columns by 128 rows over a 22-row ruler, so a column is ms / 30 and
// the snap range of 16 columns is 480 ms.

#include "hikari/application/audio_timing.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

AudioView minuteView()
{
    AudioView view;
    view.setSamplesPercent(50, false);
    view.setSource(48000, 48000 * 60);
    view.resize(1000, 150, 22, 16);
    return view;
}

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

std::int64_t ms(core::DocumentTime t)
{
    return t.microseconds() / 1000;
}

AudioMouse press(int x, AudioMouse::Button button = AudioMouse::Button::Left, int y = 50)
{
    AudioMouse m;
    m.type = AudioMouse::Type::Press;
    m.button = button;
    m.x = x;
    m.y = y;
    m.leftHeld = button == AudioMouse::Button::Left;
    m.rightHeld = button == AudioMouse::Button::Right;
    m.middleHeld = button == AudioMouse::Button::Middle;
    return m;
}

AudioMouse drag(int x, AudioMouse::Button held = AudioMouse::Button::Left, int y = 50)
{
    AudioMouse m;
    m.type = AudioMouse::Type::Move;
    m.x = x;
    m.y = y;
    m.leftHeld = held == AudioMouse::Button::Left;
    m.rightHeld = held == AudioMouse::Button::Right;
    return m;
}

AudioMouse release(int x, AudioMouse::Button button = AudioMouse::Button::Left, int y = 50)
{
    AudioMouse m;
    m.type = AudioMouse::Type::Release;
    m.button = button;
    m.x = x;
    m.y = y;
    return m;
}

AudioMouse hover(int x, int y = 50)
{
    return drag(x, AudioMouse::Button::None, y);
}

// The display with the active Line's selection 3000-6000 (columns 100-200).
struct TimingTest : ::testing::Test {
    AudioView view = minuteView();
    AudioTiming timing;
    AudioTiming::Selection selection{3000, 6000, false};
    AudioTimingOptions options;
    std::vector<AudioLineSpan> lines{{1000, 2000}, {3000, 6000}, {6500, 8000}};
    AudioSnapContext snap{&view, false, false, true, 1, {}, {}, lines, 1};

    void SetUp() override { timing.drawn(view, selection); }
    AudioMouseResult send(const AudioMouse &event)
    {
        auto result = timing.mouse(event, view, selection, snap, options, 16);
        timing.drawn(view, selection); // the redraw legacy's UpdateImage makes
        return result;
    }
};

} // namespace

TEST(AudioTiming, KeyFromPositionWithoutTheSafeFallbacks)
{
    const std::vector<AudioLineSpan> lines{{0, 1, true}, {0, 1, false}, {0, 1, true}, {0, 1, false}};
    EXPECT_EQ(legacyKeyFromPositionOrNone(lines, 0, 1), 2);  // the hidden Line is skipped
    EXPECT_EQ(legacyKeyFromPositionOrNone(lines, 2, 1), -1); // only hidden Lines after it
    EXPECT_EQ(legacyKeyFromPositionOrNone(lines, 2, -1), 0);
    EXPECT_EQ(legacyKeyFromPositionOrNone(lines, 0, -1), 0); // position 0 going back is itself
    EXPECT_EQ(legacyKeyFromPositionOrNone(lines, 1, -1), 0);
    const std::vector<AudioLineSpan> hidden{{0, 1, false}, {0, 1, true}};
    EXPECT_EQ(legacyKeyFromPositionOrNone(hidden, 1, -1), -1);
    // the safe variant (A1's) falls back where this one gives none
    EXPECT_EQ(legacyKeyFromPosition(lines, 2, 1), 2);
}

TEST(AudioTiming, LeadInStopsAtZeroLeadOutDoesNot)
{
    EXPECT_EQ(legacyAddLead(1000, 2000, true, false, 200, 300), std::pair(800, 2000));
    EXPECT_EQ(legacyAddLead(150, 2000, true, false, 200, 300), std::pair(0, 2000));
    EXPECT_EQ(legacyAddLead(1000, 2000, false, true, 200, 300), std::pair(1000, 2300));
    // values from the settings are taken as they are
    EXPECT_EQ(legacyAddLead(1000, 2000, true, true, 15, 7), std::pair(985, 2007));
}

TEST(AudioTiming, FieldTimesKeepTheFormatsPrecision)
{
    EXPECT_EQ(legacyFieldTime(core::SubtitleFormat::Ass, 1239), 1230);
    EXPECT_EQ(legacyFieldTime(core::SubtitleFormat::TMPlayer, 1999), 1000);
    EXPECT_EQ(legacyFieldTime(core::SubtitleFormat::Srt, 1239), 1239);
    EXPECT_EQ(legacyFieldTime(core::SubtitleFormat::Ass, -50), 0);
    EXPECT_EQ(legacyZeroIt(-15), -10); // ZEROIT truncates towards zero
}

TEST_F(TimingTest, SnapIsOffByDefaultAndStillDropsTheMilliseconds)
{
    EXPECT_EQ(legacyBoundarySnap(snap, 1234, 16, false, false, true), 1230);
    EXPECT_EQ(legacyBoundarySnap(snap, 1234, 0, false, false, true), 1234); // no range: as it is
}

TEST_F(TimingTest, SnapsToKeyframesInViewWithinSixteenColumns)
{
    const std::vector<int> keys{3000, 40000}; // columns 100 and 1333 (out of view)
    const std::vector<int> keyStarts{2985, 39990};
    snap.keyframesMs = keys;
    snap.keyframeSnapMs = keyStarts;
    snap.snapToKeyframes = true;
    EXPECT_EQ(legacyBoundarySnap(snap, 3300, 16, false, false, true), 2980); // ZEROIT(2985)
    EXPECT_EQ(legacyBoundarySnap(snap, 3460, 16, false, false, true), 2980); // 480 away
    EXPECT_EQ(legacyBoundarySnap(snap, 3461, 16, false, false, true), 3460); // 481: out of range
    EXPECT_EQ(legacyBoundarySnap(snap, 39900, 16, false, false, true), 39900); // not drawn, not snapped
    EXPECT_EQ(legacyBoundarySnap(snap, 2975, 16, false, true, true), 2970);   // keysnap: under 10 ms skipped
    // Shift inverts the option; keyframes that are not drawn never snap
    EXPECT_EQ(legacyBoundarySnap(snap, 3300, 16, true, false, true), 3300);
    snap.snapToKeyframes = false;
    EXPECT_EQ(legacyBoundarySnap(snap, 3300, 16, true, false, true), 2980);
    snap.drawKeyframes = false;
    EXPECT_EQ(legacyBoundarySnap(snap, 3300, 16, true, false, true), 3300);
    // without a video: 21 ms before the keyframe
    snap.drawKeyframes = true;
    snap.keyframeSnapMs = {};
    EXPECT_EQ(legacyBoundarySnap(snap, 3300, 16, true, false, true), 2970); // ZEROIT(2979)
}

TEST_F(TimingTest, SnapsToTheShadedLinesBoundaries)
{
    lines = {{100, 1005, true}, {1300, 2000, false}, {3000, 6000, true}, {6505, 8000, true}, {9000, 9500, true}};
    snap.lines = lines;
    snap.active = 2;
    snap.snapToOtherLines = true;
    // mode 1: the previous and next shown Lines (the hidden one between is skipped)
    EXPECT_EQ(legacyBoundarySnap(snap, 1300, 16, false, false, true), 1000); // 1005 zeroed
    EXPECT_EQ(legacyBoundarySnap(snap, 6200, 16, false, false, true), 6500);
    EXPECT_EQ(legacyBoundarySnap(snap, 9100, 16, false, false, true), 9100); // not the Line after next
    EXPECT_EQ(legacyBoundarySnap(snap, 3100, 16, false, false, true), 3100); // nor the active Line
    // the nearest wins
    EXPECT_EQ(legacyBoundarySnap(snap, 7800, 16, false, false, true), 8000);
    // mode 2: every shown Line
    snap.inactiveLines = 2;
    EXPECT_EQ(legacyBoundarySnap(snap, 9100, 16, false, false, true), 9000);
    EXPECT_EQ(legacyBoundarySnap(snap, 1800, 16, false, false, true), 1800); // the hidden Line's end
    // mode 0, Alt (otherLines false) or Shift: no Lines
    EXPECT_EQ(legacyBoundarySnap(snap, 9100, 16, false, false, false), 9100);
    EXPECT_EQ(legacyBoundarySnap(snap, 9100, 16, true, false, true), 9100);
    snap.inactiveLines = 0;
    EXPECT_EQ(legacyBoundarySnap(snap, 9100, 16, false, false, true), 9100);
}

TEST_F(TimingTest, DraggingTheStartSnapsAndCommitsOnRelease)
{
    // the cursor changes over a boundary and back
    auto r = send(hover(103));
    EXPECT_EQ(r.sizeCursor, std::optional(true));
    r = send(hover(150));
    EXPECT_EQ(r.sizeCursor, std::optional(false));
    r = send(hover(160));
    EXPECT_FALSE(r.sizeCursor); // already the default
    // grab the start (within 6 columns) and move it
    r = send(press(102));
    EXPECT_EQ(timing.hold(), 1);
    EXPECT_TRUE(r.focus);
    EXPECT_EQ(selection.startMs, 3060);
    EXPECT_TRUE(selection.modified);
    EXPECT_EQ(r.playEnd, std::optional<std::int64_t>(200 * 1440)); // the player's end: the end column
    r = send(drag(90));
    EXPECT_EQ(selection.startMs, 2700);
    EXPECT_FALSE(r.commit);
    r = send(release(90));
    EXPECT_EQ(r.commit, std::optional(false)); // Commit(hold == 2)
    EXPECT_EQ(r.adjacent, AudioAdjacent::None);
    EXPECT_EQ(timing.hold(), 0);
    EXPECT_EQ(selection.startMs, 2700);
    EXPECT_EQ(selection.endMs, 6000);
}

TEST_F(TimingTest, DraggingTheEndCommitsMovingToTheEnd)
{
    send(press(199));
    EXPECT_EQ(timing.hold(), 2);
    send(drag(250, AudioMouse::Button::Left));
    EXPECT_EQ(selection.endMs, 7500);
    auto r = send(release(251));
    EXPECT_EQ(r.commit, std::optional(true));
}

TEST_F(TimingTest, TimingFromNothing)
{
    // a click sets the start (snapped), a drag past the sensitivity times from there
    auto r = send(press(400));
    EXPECT_EQ(timing.hold(), 3);
    EXPECT_EQ(selection.startMs, 12000);
    EXPECT_EQ(selection.endMs, 6000);
    r = send(drag(405));
    EXPECT_EQ(timing.hold(), 3); // 5 columns: not past AUDIO_START_DRAG_SENSITIVITY (6)
    r = send(drag(407));
    EXPECT_EQ(timing.hold(), 2);
    EXPECT_EQ(selection.startMs, 12000);
    EXPECT_EQ(selection.endMs, 12210);
    r = send(release(407));
    EXPECT_EQ(r.commit, std::optional(true));
    EXPECT_EQ(selection.endMs, 12210); // ZEROIT on release
    // a click without a drag commits as a start (hold 3)
    send(press(600));
    r = send(release(600));
    EXPECT_EQ(r.commit, std::optional(false));
    EXPECT_EQ(selection.startMs, 18000);
    // a right click sets the end, snapped like a dragged end
    send(press(700, AudioMouse::Button::Right));
    EXPECT_EQ(selection.endMs, 21000);
    r = send(release(700, AudioMouse::Button::Right));
    EXPECT_EQ(r.commit, std::optional(true));
}

TEST_F(TimingTest, ReleasingBelowZeroClampsAndAltMovesTheNeighbour)
{
    send(press(100));
    send(drag(-10));
    EXPECT_EQ(selection.startMs, -300);
    AudioMouse up = release(-10);
    up.alt = true;
    auto r = send(up);
    EXPECT_EQ(selection.startMs, 0);
    EXPECT_EQ(r.adjacent, AudioAdjacent::Previous);
    send(press(200));
    up = release(200);
    up.alt = true;
    r = send(up);
    EXPECT_EQ(r.adjacent, AudioAdjacent::Next);
}

TEST_F(TimingTest, AltDraggingDoesNotSnapToLines)
{
    snap.snapToOtherLines = true;
    lines = {{1000, 2000}, {3000, 6000}, {6500, 8000}};
    snap.lines = lines;
    send(press(200));
    send(drag(214)); // 6420: 80 ms from the next Line's start
    EXPECT_EQ(selection.endMs, 6500);
    AudioMouse m = drag(213);
    m.alt = true;
    send(m);
    EXPECT_EQ(selection.endMs, 6390);
}

TEST_F(TimingTest, CtrlAndMiddleClicksSeekTheVideoWithoutTiming)
{
    AudioMouse m = press(400);
    m.ctrl = true;
    auto r = send(m);
    EXPECT_EQ(r.seekVideoMs, std::optional(12000));
    EXPECT_EQ(timing.hold(), 0);
    EXPECT_EQ(selection.startMs, 3000);
    r = send(press(500, AudioMouse::Button::Middle));
    EXPECT_EQ(r.seekVideoMs, std::optional(15000));
    // Ctrl over a boundary: still no timing
    m = press(100);
    m.ctrl = true;
    send(m);
    EXPECT_EQ(timing.hold(), 0);
    // a middle double click plays the selection (A4)
    AudioMouse dbl;
    dbl.type = AudioMouse::Type::DoubleClick;
    dbl.button = AudioMouse::Button::Middle;
    dbl.x = 500;
    dbl.y = 50;
    r = send(dbl);
    EXPECT_TRUE(r.playSelection);
}

TEST_F(TimingTest, TheRulerSetsAndDragsTheMark)
{
    const int ruler = 128 + 5;
    auto r = send(press(300, AudioMouse::Button::Right, ruler));
    EXPECT_TRUE(timing.hasMark());
    EXPECT_TRUE(r.markAdded);
    EXPECT_EQ(timing.markMs(), 9000);
    EXPECT_FALSE(r.commit);
    send(release(300, AudioMouse::Button::Right, ruler));
    r = send(press(310, AudioMouse::Button::Right, ruler));
    EXPECT_FALSE(r.markAdded); // only the first
    EXPECT_EQ(timing.markMs(), 9300);
    send(release(310, AudioMouse::Button::Right, ruler));
    // over the waveform the mark is grabbed before the boundaries
    r = send(hover(312));
    EXPECT_EQ(r.sizeCursor, std::optional(true));
    send(press(312));
    EXPECT_EQ(timing.hold(), 4);
    send(drag(320));
    EXPECT_EQ(timing.markMs(), 9600);
    r = send(release(320));
    EXPECT_FALSE(r.commit);
    EXPECT_EQ(selection.startMs, 3000);
    EXPECT_FALSE(selection.modified);
}

TEST_F(TimingTest, DraggingTheRulerScrolls)
{
    const int ruler = 128 + 5;
    send(press(500, AudioMouse::Button::Left, ruler));
    send(drag(450, AudioMouse::Button::Left, ruler));
    EXPECT_EQ(view.position(), 50);
    send(drag(470, AudioMouse::Button::Left, ruler));
    EXPECT_EQ(view.position(), 30);
    send(release(470, AudioMouse::Button::Left, ruler));
    send(drag(400, AudioMouse::Button::None, ruler));
    EXPECT_EQ(view.position(), 30);
    EXPECT_EQ(selection.startMs, 3000);
}

TEST_F(TimingTest, OutsideTheWaveformNothingStarts)
{
    send(press(400, AudioMouse::Button::Left, 200)); // below the ruler, never inside before
    EXPECT_EQ(timing.hold(), 0);
    EXPECT_EQ(selection.startMs, 3000);
}

namespace {

// a 1-2 s, b 5-6 s, c 6.5-8 s.
constexpr std::string_view kThree = "[Events]\n"
                                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                    "Dialogue: 0,0:00:05.00,0:00:06.00,Sign,,0,0,0,,b\n"
                                    "Dialogue: 0,0:00:06.50,0:00:08.00,Default,,0,0,0,,c\n";

struct CommitTest : ::testing::Test {
    EditSession session{load(kThree)};
    core::LineId a{1}, b{2}, c{3};
    std::set<core::LineId> hidden;
    LineVisible shown = [this](core::LineId id) { return !hidden.contains(id); };
    void select(std::set<core::LineId> lines, core::LineId active)
    {
        session.setSelection(Selection{active, std::move(lines), active, {}});
    }
    const core::LineRecord &line(std::size_t i) const { return *session.document().lines()[i]; }
    std::string lastStep() const { return session.history().back().name; }
};

} // namespace

TEST_F(CommitTest, ACommitIsOneNamedStepAndGoesToTheNextLine)
{
    select({a}, a);
    const auto steps = session.historySize();
    const auto outcome = commitAudioTimes(session, {1234, 2567, true, true}, shown);
    ASSERT_TRUE(outcome);
    EXPECT_TRUE(outcome->stepped);
    EXPECT_EQ(outcome->next, std::optional(b));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(lastStep(), "Changing time on audio spectrum");
    EXPECT_EQ(ms(line(0).start.value), 1230); // the field's centiseconds
    EXPECT_EQ(ms(line(0).end.value), 2560);
    EXPECT_FALSE(session.draftLine());
}

TEST_F(CommitTest, NothingChangedMovesOnWithoutAStep)
{
    select({a}, a);
    const auto steps = session.historySize();
    const auto outcome = commitAudioTimes(session, {1005, 2009, true, true}, shown); // the fields show 1.00 and 2.00
    ASSERT_TRUE(outcome);
    EXPECT_FALSE(outcome->stepped);
    EXPECT_EQ(outcome->next, std::optional(b));
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_FALSE(session.draftLine());
}

TEST_F(CommitTest, WithoutSaveTheTimesWaitInTheEditor)
{
    select({b}, b);
    const auto steps = session.historySize();
    ASSERT_TRUE(commitAudioTimes(session, {4800, 6000, false, false}, shown));
    EXPECT_EQ(session.historySize(), steps);
    ASSERT_EQ(session.draftLine(), std::optional(b));
    EXPECT_EQ(ms(session.draftRecord()->start.value), 4800);
    EXPECT_EQ(ms(line(1).start.value), 5000);
    // the editor's other changes go with the commit, under the box's name
    session.editDraftText(b, u8"b2");
    const auto outcome = commitAudioTimes(session, {4800, 6000, true, false}, shown);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->next, std::nullopt);
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(lastStep(), "Changing time on audio spectrum");
    EXPECT_EQ(line(1).text, u8"b2");
    EXPECT_EQ(ms(line(1).start.value), 4800);
}

// EditBox::Send carries every field of the Line's own pending draft that
// differs from the Line (SubsGrid::ChangeLine's cells), to each selected Line;
// the rewrite's draft holds the text, the translation, the times and the
// margins (the Line editor has no Comment, Layer, Style, Actor or Effect
// field yet), and fields the draft leaves alone stay each Line's own.
TEST_F(CommitTest, TheLinesOwnDraftGoesWithTheCommitFieldByField)
{
    select({a, c}, a);
    DraftChange change;
    change.text = u8"a2";
    change.marginLeft = 12;
    ASSERT_TRUE(session.editDraft(a, change));
    const auto steps = session.historySize();
    ASSERT_TRUE(commitAudioTimes(session, {1000, 2500, true, false}, shown));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(lastStep(), "Changing time on audio spectrum");
    EXPECT_FALSE(session.draftLine());
    for (const std::size_t i : {std::size_t(0), std::size_t(2)}) {
        EXPECT_EQ(line(i).text, u8"a2") << i;
        EXPECT_EQ(line(i).marginLeft.value, 12) << i;
        EXPECT_EQ(ms(line(i).end.value), 2500) << i;
    }
    // the start was not changed: c keeps its own; so do the fields no draft holds
    EXPECT_EQ(ms(line(2).start.value), 6500);
    EXPECT_EQ(line(2).style, u8"Default");
    EXPECT_EQ(line(1).text, u8"b");
    EXPECT_EQ(line(1).marginLeft.value, 0);
}

TEST_F(CommitTest, TheNextLineIsTheNextShownOne)
{
    hidden = {b};
    select({a}, a);
    const auto outcome = commitAudioTimes(session, {1000, 2000, true, true}, shown);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->next, std::optional(c));
    // while the display holds a boundary NextLine does nothing
    const auto held = commitAudioTimes(session, {1000, 2100, true, true, true}, shown);
    ASSERT_TRUE(held);
    EXPECT_EQ(held->next, std::nullopt);
    EXPECT_TRUE(held->stepped);
}

TEST_F(CommitTest, OnTheLastLineTheAppendedLineNamesTheOneStep)
{
    select({c}, c);
    const auto steps = session.historySize();
    const auto outcome = commitAudioTimes(session, {6500, 8500, true, true}, shown);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(lastStep(), "Adding a new line");
    ASSERT_EQ(session.document().lines().size(), 4u);
    EXPECT_EQ(ms(line(2).end.value), 8500);
    const auto &added = line(3);
    EXPECT_EQ(outcome->next, std::optional(added.id));
    EXPECT_EQ(ms(added.start.value), 8500); // the committed end
    EXPECT_EQ(ms(added.end.value), 13500);
    EXPECT_EQ(added.text, u8"");
    EXPECT_EQ(added.style, u8"Default");
    // nothing changed: the append alone, the same name
    select({added.id}, added.id);
    const auto again = commitAudioTimes(session, {8500, 13500, true, true}, shown);
    ASSERT_TRUE(again);
    EXPECT_EQ(lastStep(), "Adding a new line");
    EXPECT_EQ(session.document().lines().size(), 5u);
}

TEST_F(CommitTest, TheAppendedLineCopiesTheLineAtTheShownRowCountWithHiddenLines)
{
    // GetElementByKey(size - 1) is the last Line's row (1: b is hidden), and
    // GetDialogue reads it as a key: b is copied, not c.
    hidden = {b};
    select({c}, c);
    const auto outcome = commitAudioTimes(session, {6500, 8000, true, true}, shown);
    ASSERT_TRUE(outcome);
    const auto &added = line(3);
    EXPECT_EQ(added.style, u8"Sign");
    EXPECT_EQ(ms(added.start.value), 6000);
    EXPECT_EQ(ms(added.end.value), 11000);
}

TEST_F(CommitTest, SeveralSelectedLinesAllTakeTheTimes)
{
    select({a, c}, a);
    ASSERT_TRUE(commitAudioTimes(session, {500, 900, true, false}, shown));
    EXPECT_EQ(ms(line(0).start.value), 500);
    EXPECT_EQ(ms(line(2).start.value), 500);
    EXPECT_EQ(ms(line(2).end.value), 900);
    EXPECT_EQ(ms(line(1).start.value), 5000);
    // only the fields that changed: the same end leaves the others' ends alone
    ASSERT_TRUE(commitAudioTimes(session, {700, 900, true, false}, shown));
    EXPECT_EQ(ms(line(2).start.value), 700);
}

TEST_F(CommitTest, AnEndBeforeTheStartIsBlockedUnlessLegacyCommitsIt)
{
    select({b}, b);
    const auto steps = session.historySize();
    const auto refused = commitAudioTimes(session, {6000, 5000, true, true}, shown);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error(), CommandRefusal::InvalidDraft);
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_EQ(session.draftLine(), std::optional(b)); // the draft and its reason stay
    session.setInvalidCommitPolicy(InvalidCommitPolicy::Legacy);
    const auto outcome = commitAudioTimes(session, {6000, 5000, true, true}, shown);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(ms(line(1).start.value), 6000);
    EXPECT_EQ(ms(line(1).end.value), 5000); // Send does not correct it
}

TEST_F(CommitTest, AltReleaseMovesTheNeighbourInTheSameStep)
{
    select({a}, a);
    const auto steps = session.historySize();
    ASSERT_TRUE(commitAudioTimes(session, {1000, 5500, true, false, false, AudioAdjacent::Next}, shown));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(ms(line(1).start.value), 5500);
    EXPECT_EQ(ms(line(1).end.value), 6000);
    // past the neighbour's end: it lasts five seconds from there
    ASSERT_TRUE(commitAudioTimes(session, {1000, 7000, true, false, false, AudioAdjacent::Next}, shown));
    EXPECT_EQ(ms(line(1).start.value), 7000);
    EXPECT_EQ(ms(line(1).end.value), 12000);
    // the previous one ends at the start
    select({c}, c);
    ASSERT_TRUE(commitAudioTimes(session, {6000, 8000, true, false, false, AudioAdjacent::Previous}, shown));
    EXPECT_EQ(ms(line(1).end.value), 6000);
    EXPECT_EQ(ms(line(1).start.value), 1000); // 6000 - 5000: its end fell before its start
}

TEST_F(CommitTest, AltOnTheFirstLinesStartMovesTheLineItself)
{
    // CopyDialogueWithOffset(0, -1) is Line 0 itself: its end goes to the new
    // start, and Send leaves the end the editor did not change.
    select({a}, a);
    ASSERT_TRUE(commitAudioTimes(session, {500, 2000, true, false, false, AudioAdjacent::Previous}, shown));
    EXPECT_EQ(ms(line(0).start.value), 500);
    EXPECT_EQ(ms(line(0).end.value), 500);
}

TEST_F(CommitTest, AltWithoutAutoCommitIsAStepOfItsOwn)
{
    // A3-adjacent-step (proposed): legacy's change joined the next step
    select({a}, a);
    const auto steps = session.historySize();
    ASSERT_TRUE(commitAudioTimes(session, {1000, 5500, false, false, false, AudioAdjacent::Next}, shown));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(ms(line(1).start.value), 5500);
    EXPECT_EQ(session.draftLine(), std::optional(a)); // the box's end waits in the editor
    EXPECT_EQ(ms(line(0).end.value), 2000);
}

// DoUpdateImage after DrawTimescale: the mark two pixels wide one column to
// the right of its x, its time centred in a 300-pixel rect above the bottom.
TEST(AudioTiming, TheMarkIsDrawnWithItsTime)
{
    const AudioView view = minuteView();
    AudioMarks marks;
    marks.startMs = 3000;
    marks.endMs = 6000;
    marks.markMs = 9000; // column 300
    marks.markTextHeight = 14;
    const AudioDisplayOptions options;
    const auto scene = audioScene(view, {}, marks, options, [](AudioShape::Font, std::string_view) { return 30; });
    const AudioShape *line = nullptr, *label = nullptr;
    for (const auto &s : scene) {
        if (s.kind == AudioShape::Kind::Line && s.colour == options.lineBoundaryMark && s.width == 2)
            line = &s;
        if (s.kind == AudioShape::Kind::Text && s.text == "0:00:09.00")
            label = &s;
    }
    ASSERT_TRUE(line);
    EXPECT_EQ(line->x1, 301);
    EXPECT_EQ(line->y1, 0);
    EXPECT_EQ(line->y2, 128);
    ASSERT_TRUE(label);
    EXPECT_EQ(label->x1, 150);
    EXPECT_EQ(label->x2, 450);
    EXPECT_EQ(label->y1, 128 - 14 - 2);
    EXPECT_EQ(label->align, AudioShape::Align::TopCenter);
    EXPECT_TRUE(label->outlined);
    // out of view: neither
    marks.markMs = 40000;
    for (const auto &s : audioScene(view, {}, marks, options, [](AudioShape::Font, std::string_view) { return 30; }))
        EXPECT_FALSE(s.kind == AudioShape::Kind::Text && s.text == "0:00:40.00");
}

// A modified selection is drawn as legacy NeedCommit draws it.
TEST(AudioTiming, AModifiedSelectionSaysSo)
{
    const AudioView view = minuteView();
    AudioMarks marks;
    marks.startMs = 3000;
    marks.endMs = 6000;
    marks.modified = true;
    bool said = false;
    for (const auto &s : audioScene(view, {}, marks, AudioDisplayOptions{},
                                    [](AudioShape::Font, std::string_view) { return 30; }))
        said = said || (s.kind == AudioShape::Kind::Text && s.text == "Modified");
    EXPECT_TRUE(said);
}
