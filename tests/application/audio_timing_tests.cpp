#include "hikari/application/audio_timing.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/subtitle_load.h"

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
        timing.drawn(view, selection);
        return result;
    }
};

}

TEST(AudioTiming, KeyFromPositionWithoutTheSafeFallbacks)
{
    const std::vector<AudioLineSpan> lines{{0, 1, true}, {0, 1, false}, {0, 1, true}, {0, 1, false}};
    EXPECT_EQ(legacyKeyFromPositionOrNone(lines, 0, 1), 2);
    EXPECT_EQ(legacyKeyFromPositionOrNone(lines, 2, 1), -1);
    EXPECT_EQ(legacyKeyFromPositionOrNone(lines, 2, -1), 0);
    EXPECT_EQ(legacyKeyFromPositionOrNone(lines, 0, -1), 0);
    EXPECT_EQ(legacyKeyFromPositionOrNone(lines, 1, -1), 0);
    const std::vector<AudioLineSpan> hidden{{0, 1, false}, {0, 1, true}};
    EXPECT_EQ(legacyKeyFromPositionOrNone(hidden, 1, -1), -1);
    EXPECT_EQ(legacyKeyFromPosition(lines, 2, 1), 2);
}

TEST(AudioTiming, LeadInStopsAtZeroLeadOutDoesNot)
{
    EXPECT_EQ(legacyAddLead(1000, 2000, true, false, 200, 300), std::pair(800, 2000));
    EXPECT_EQ(legacyAddLead(150, 2000, true, false, 200, 300), std::pair(0, 2000));
    EXPECT_EQ(legacyAddLead(1000, 2000, false, true, 200, 300), std::pair(1000, 2300));
    EXPECT_EQ(legacyAddLead(1000, 2000, true, true, 15, 7), std::pair(985, 2007));
}

TEST(AudioTiming, FieldTimesKeepTheFormatsPrecision)
{
    EXPECT_EQ(legacyFieldTime(core::SubtitleFormat::Ass, 1239), 1230);
    EXPECT_EQ(legacyFieldTime(core::SubtitleFormat::TMPlayer, 1999), 1000);
    EXPECT_EQ(legacyFieldTime(core::SubtitleFormat::Srt, 1239), 1239);
    EXPECT_EQ(legacyFieldTime(core::SubtitleFormat::Ass, -50), 0);
    EXPECT_EQ(legacyZeroIt(-15), -10);
}

TEST(AudioTiming, TheCommittedTimeIsTheOneTheFieldKeeps)
{
    EXPECT_EQ(committedAudioTime(load("[Events]\n"), 999), 990);
    const std::string_view sub = "{25}{50}text\n";
    std::vector<std::byte> bytes(sub.size());
    std::memcpy(bytes.data(), sub.data(), sub.size());
    auto loaded = core::loadSubtitle(bytes, u8"sub");
    ASSERT_TRUE(loaded);
    auto &document = loaded->document;
    ASSERT_EQ(document.format(), core::SubtitleFormat::MicroDvd);
    document.setFrameRate(*core::FrameRate::make(25, 1));
    EXPECT_EQ(committedAudioTime(document, 1001), 1040);
    EXPECT_EQ(committedAudioTime(document, 1000), 1000);
}

TEST_F(TimingTest, SnapIsOffByDefaultAndStillDropsTheMilliseconds)
{
    EXPECT_EQ(legacyBoundarySnap(snap, 1234, 16, false, false, true), 1230);
    EXPECT_EQ(legacyBoundarySnap(snap, 1234, 0, false, false, true), 1234);
}

TEST_F(TimingTest, SnapsToKeyframesInViewWithinSixteenColumns)
{
    const std::vector<int> keys{3000, 40000};
    const std::vector<int> keyStarts{2985, 39990};
    snap.keyframesMs = keys;
    snap.keyframeSnapMs = keyStarts;
    snap.snapToKeyframes = true;
    EXPECT_EQ(legacyBoundarySnap(snap, 3300, 16, false, false, true), 2980);
    EXPECT_EQ(legacyBoundarySnap(snap, 3460, 16, false, false, true), 2980);
    EXPECT_EQ(legacyBoundarySnap(snap, 3461, 16, false, false, true), 3460);
    EXPECT_EQ(legacyBoundarySnap(snap, 39900, 16, false, false, true), 39900);
    EXPECT_EQ(legacyBoundarySnap(snap, 2975, 16, false, true, true), 2970);
    EXPECT_EQ(legacyBoundarySnap(snap, 3300, 16, true, false, true), 3300);
    snap.snapToKeyframes = false;
    EXPECT_EQ(legacyBoundarySnap(snap, 3300, 16, true, false, true), 2980);
    snap.drawKeyframes = false;
    EXPECT_EQ(legacyBoundarySnap(snap, 3300, 16, true, false, true), 3300);
    snap.drawKeyframes = true;
    snap.keyframeSnapMs = {};
    EXPECT_EQ(legacyBoundarySnap(snap, 3300, 16, true, false, true), 2970);
}

TEST_F(TimingTest, SnapsToTheShadedLinesBoundaries)
{
    lines = {{100, 1005, true}, {1300, 2000, false}, {3000, 6000, true}, {6505, 8000, true}, {9000, 9500, true}};
    snap.lines = lines;
    snap.active = 2;
    snap.snapToOtherLines = true;
    EXPECT_EQ(legacyBoundarySnap(snap, 1300, 16, false, false, true), 1000);
    EXPECT_EQ(legacyBoundarySnap(snap, 6200, 16, false, false, true), 6500);
    EXPECT_EQ(legacyBoundarySnap(snap, 9100, 16, false, false, true), 9100);
    EXPECT_EQ(legacyBoundarySnap(snap, 3100, 16, false, false, true), 3100);
    EXPECT_EQ(legacyBoundarySnap(snap, 7800, 16, false, false, true), 8000);
    snap.inactiveLines = 2;
    EXPECT_EQ(legacyBoundarySnap(snap, 9100, 16, false, false, true), 9000);
    EXPECT_EQ(legacyBoundarySnap(snap, 1800, 16, false, false, true), 1800);
    EXPECT_EQ(legacyBoundarySnap(snap, 9100, 16, false, false, false), 9100);
    EXPECT_EQ(legacyBoundarySnap(snap, 9100, 16, true, false, true), 9100);
    snap.inactiveLines = 0;
    EXPECT_EQ(legacyBoundarySnap(snap, 9100, 16, false, false, true), 9100);
}

TEST_F(TimingTest, DraggingTheStartSnapsAndCommitsOnRelease)
{
    auto r = send(hover(103));
    EXPECT_EQ(r.sizeCursor, std::optional(true));
    r = send(hover(150));
    EXPECT_EQ(r.sizeCursor, std::optional(false));
    r = send(hover(160));
    EXPECT_FALSE(r.sizeCursor);
    r = send(press(102));
    EXPECT_EQ(timing.hold(), 1);
    EXPECT_TRUE(r.focus);
    EXPECT_EQ(selection.startMs, 3060);
    EXPECT_TRUE(selection.modified);
    EXPECT_EQ(r.playEnd, std::optional<std::int64_t>(200 * 1440));
    r = send(drag(90));
    EXPECT_EQ(selection.startMs, 2700);
    EXPECT_FALSE(r.commit);
    r = send(release(90));
    EXPECT_EQ(r.commit, std::optional(false));
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
    auto r = send(press(400));
    EXPECT_EQ(timing.hold(), 3);
    EXPECT_EQ(selection.startMs, 12000);
    EXPECT_EQ(selection.endMs, 6000);
    r = send(drag(405));
    EXPECT_EQ(timing.hold(), 3);
    r = send(drag(407));
    EXPECT_EQ(timing.hold(), 2);
    EXPECT_EQ(selection.startMs, 12000);
    EXPECT_EQ(selection.endMs, 12210);
    r = send(release(407));
    EXPECT_EQ(r.commit, std::optional(true));
    EXPECT_EQ(selection.endMs, 12210);
    send(press(600));
    r = send(release(600));
    EXPECT_EQ(r.commit, std::optional(false));
    EXPECT_EQ(selection.startMs, 18000);
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
    send(drag(214));
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
    m = press(100);
    m.ctrl = true;
    send(m);
    EXPECT_EQ(timing.hold(), 0);
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
    EXPECT_FALSE(r.markAdded);
    EXPECT_EQ(timing.markMs(), 9300);
    send(release(310, AudioMouse::Button::Right, ruler));
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
    send(press(400, AudioMouse::Button::Left, 200));
    EXPECT_EQ(timing.hold(), 0);
    EXPECT_EQ(selection.startMs, 3000);
}

namespace {

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

}

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
    EXPECT_EQ(ms(line(0).start.value), 1230);
    EXPECT_EQ(ms(line(0).end.value), 2560);
    EXPECT_FALSE(session.draftLine());
}

TEST_F(CommitTest, NothingChangedMovesOnWithoutAStep)
{
    select({a}, a);
    const auto steps = session.historySize();
    const auto outcome = commitAudioTimes(session, {1005, 2009, true, true}, shown);
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
    session.editDraftText(b, u8"b2");
    const auto outcome = commitAudioTimes(session, {4800, 6000, true, false}, shown);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome->next, std::nullopt);
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(lastStep(), "Changing time on audio spectrum");
    EXPECT_EQ(line(1).text, u8"b2");
    EXPECT_EQ(ms(line(1).start.value), 4800);
}

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
    EXPECT_EQ(ms(added.start.value), 8500);
    EXPECT_EQ(ms(added.end.value), 13500);
    EXPECT_EQ(added.text, u8"");
    EXPECT_EQ(added.style, u8"Default");
    select({added.id}, added.id);
    const auto again = commitAudioTimes(session, {8500, 13500, true, true}, shown);
    ASSERT_TRUE(again);
    EXPECT_EQ(lastStep(), "Adding a new line");
    EXPECT_EQ(session.document().lines().size(), 5u);
}

TEST_F(CommitTest, TheAppendedLineCopiesTheLineAtTheShownRowCountWithHiddenLines)
{
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
    ASSERT_TRUE(commitAudioTimes(session, {700, 900, true, false}, shown));
    EXPECT_EQ(ms(line(2).start.value), 700);
}

TEST_F(CommitTest, AnActiveOnlyCommitLeavesTheOtherSelectedLinesAlone)
{
    select({a, c}, a);
    AudioCommitRequest request{500, 900, true, false};
    request.karaokeText = u"{\\k20}ta{\\k20}p";
    request.stepName = "Tap timing";
    request.activeOnly = true;
    ASSERT_TRUE(commitAudioTimes(session, request, shown));
    EXPECT_EQ(ms(line(0).start.value), 500);
    EXPECT_EQ(ms(line(0).end.value), 900);
    EXPECT_EQ(line(0).text, u8"{\\k20}ta{\\k20}p");
    EXPECT_EQ(ms(line(2).start.value), 6500);
    EXPECT_EQ(ms(line(2).end.value), 8000);
    EXPECT_EQ(line(2).text, u8"c");
    EXPECT_EQ(lastStep(), "Tap timing");
}

TEST_F(CommitTest, AnActiveOnlyDraftStaysTheActiveLinesWhenCommitted)
{
    select({a, c}, a);
    AudioCommitRequest draft{500, 900, false, false};
    draft.karaokeText = u"{\\k20}ka{\\k20}ra";
    draft.activeOnly = true;
    ASSERT_TRUE(commitAudioTimes(session, draft, shown));
    ASSERT_TRUE(session.draftOwnFields());
    EXPECT_TRUE(session.draftOwnFields()->text);
    ASSERT_TRUE(commitAudioTimes(session, {500, 900, true, false}, shown));
    EXPECT_EQ(line(0).text, u8"{\\k20}ka{\\k20}ra");
    EXPECT_EQ(ms(line(0).start.value), 500);
    EXPECT_EQ(line(2).text, u8"c");
    EXPECT_EQ(ms(line(2).start.value), 6500);

    AudioCommitRequest again = draft;
    again.karaokeText = u"{\\k20}ki{\\k20}ra";
    ASSERT_TRUE(commitAudioTimes(session, again, shown));
    ASSERT_TRUE(session.commitDraftToSelected());
    EXPECT_EQ(line(0).text, u8"{\\k20}ki{\\k20}ra");
    EXPECT_EQ(line(2).text, u8"c");

    ASSERT_TRUE(commitAudioTimes(session, {700, 900, false, false}, shown));
    EXPECT_FALSE(session.draftOwnFields()->start);
    ASSERT_TRUE(commitAudioTimes(session, {700, 900, true, false}, shown));
    EXPECT_EQ(ms(line(2).start.value), 700);

    ASSERT_TRUE(session.editDraft(a, DraftChange{.start = core::DocumentTime(300'000)}));
    AudioCommitRequest joined{300, 900, false, false};
    joined.karaokeText = u"{\\k30}ka{\\k30}ru";
    joined.activeOnly = true;
    ASSERT_TRUE(commitAudioTimes(session, joined, shown));
    EXPECT_FALSE(session.draftOwnFields()->start);
    ASSERT_TRUE(commitAudioTimes(session, {300, 900, true, false}, shown));
    EXPECT_EQ(ms(line(0).start.value), 300);
    EXPECT_EQ(line(0).text, u8"{\\k30}ka{\\k30}ru");
    EXPECT_EQ(ms(line(2).start.value), 300);
    EXPECT_EQ(line(2).text, u8"c");

    ASSERT_TRUE(session.editDraft(a, DraftChange{.actor = u8"Kara"}));
    AudioCommitRequest moved{250, 900, false, false};
    moved.karaokeText = u"{\\k35}ka{\\k30}ru";
    moved.activeOnly = true;
    ASSERT_TRUE(commitAudioTimes(session, moved, shown));
    EXPECT_TRUE(session.draftOwnFields()->start);
    ASSERT_TRUE(session.commitDraftToSelected());
    EXPECT_EQ(ms(line(0).start.value), 250);
    EXPECT_EQ(line(0).actor, u8"Kara");
    EXPECT_EQ(ms(line(2).start.value), 300);
    EXPECT_EQ(line(2).actor, u8"Kara");
    EXPECT_EQ(line(2).text, u8"c");
    ASSERT_TRUE(commitAudioTimes(session, moved, shown));
    ASSERT_TRUE(session.editDraftText(a, u8"shared"));
    EXPECT_FALSE(session.draftOwnFields()->text);
    ASSERT_TRUE(session.commitDraftToSelected());
    EXPECT_EQ(line(2).text, u8"shared");
}

TEST_F(CommitTest, AnActiveOnlySaveKeepsAPendingEditForEverySelectedLine)
{
    select({a, c}, a);
    ASSERT_TRUE(session.editDraft(a, DraftChange{.actor = u8"Kara"}));
    AudioCommitRequest request{1000, 2000, true, false};
    request.karaokeText = u"{\\k50}ka{\\k50}ra";
    request.activeOnly = true;
    ASSERT_TRUE(commitAudioTimes(session, request, shown));
    EXPECT_EQ(line(0).text, u8"{\\k50}ka{\\k50}ra");
    EXPECT_EQ(line(0).actor, u8"Kara");
    EXPECT_EQ(line(2).text, u8"c");
    EXPECT_EQ(line(2).actor, u8"Kara");
    EXPECT_EQ(ms(line(2).start.value), 6500);
}

TEST(AudioTimingCommit, AKeptStartFrameLeavesTheSharedEndFrame)
{
    const std::string_view sub = "{25}{50}a\n{75}{100}c\n";
    std::vector<std::byte> bytes(sub.size());
    std::memcpy(bytes.data(), sub.data(), sub.size());
    auto loaded = core::loadSubtitle(bytes, u8"sub");
    ASSERT_TRUE(loaded);
    loaded->document.setFrameRate(*core::FrameRate::make(25, 1));
    EditSession session{std::move(loaded->document)};
    const auto ids = [&] {
        std::vector<core::LineId> out;
        for (const auto *l : session.document().lines())
            out.push_back(l->id);
        return out;
    }();
    session.setSelection(Selection{ids[0], {ids[0], ids[1]}, ids[0], {}});
    const LineVisible shown;
    ASSERT_TRUE(commitAudioTimes(session, {1000, 5000, false, false}, shown));
    AudioCommitRequest start{800, 5000, false, false};
    start.activeOnly = true;
    ASSERT_TRUE(commitAudioTimes(session, start, shown));
    ASSERT_TRUE(commitAudioTimes(session, {800, 5000, true, false}, shown));
    const auto &first = *session.document().lines()[0];
    const auto &other = *session.document().lines()[1];
    EXPECT_EQ(first.startFrame, std::optional<std::int64_t>(20));
    EXPECT_EQ(first.endFrame, std::optional<std::int64_t>(125));
    EXPECT_EQ(other.startFrame, std::optional<std::int64_t>(75));
    EXPECT_EQ(other.endFrame, std::optional<std::int64_t>(125));
    EXPECT_EQ(ms(other.end.value), 5000);
    EXPECT_EQ(ms(other.start.value), 3000);
}

TEST_F(CommitTest, AnEndBeforeTheStartIsBlockedUnlessLegacyCommitsIt)
{
    select({b}, b);
    const auto steps = session.historySize();
    const auto refused = commitAudioTimes(session, {6000, 5000, true, true}, shown);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error(), CommandRefusal::InvalidDraft);
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_EQ(session.draftLine(), std::optional(b));
    session.setInvalidCommitPolicy(InvalidCommitPolicy::Legacy);
    const auto outcome = commitAudioTimes(session, {6000, 5000, true, true}, shown);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(ms(line(1).start.value), 6000);
    EXPECT_EQ(ms(line(1).end.value), 5000);
}

TEST_F(CommitTest, AltReleaseMovesTheNeighbourInTheSameStep)
{
    select({a}, a);
    const auto steps = session.historySize();
    ASSERT_TRUE(commitAudioTimes(session, {1000, 5500, true, false, false, AudioAdjacent::Next}, shown));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(ms(line(1).start.value), 5500);
    EXPECT_EQ(ms(line(1).end.value), 6000);
    ASSERT_TRUE(commitAudioTimes(session, {1000, 7000, true, false, false, AudioAdjacent::Next}, shown));
    EXPECT_EQ(ms(line(1).start.value), 7000);
    EXPECT_EQ(ms(line(1).end.value), 12000);
    select({c}, c);
    ASSERT_TRUE(commitAudioTimes(session, {6000, 8000, true, false, false, AudioAdjacent::Previous}, shown));
    EXPECT_EQ(ms(line(1).end.value), 6000);
    EXPECT_EQ(ms(line(1).start.value), 1000);
}

TEST_F(CommitTest, AltOnTheFirstLinesStartMovesTheLineItself)
{
    select({a}, a);
    ASSERT_TRUE(commitAudioTimes(session, {500, 2000, true, false, false, AudioAdjacent::Previous}, shown));
    EXPECT_EQ(ms(line(0).start.value), 500);
    EXPECT_EQ(ms(line(0).end.value), 500);
}

TEST_F(CommitTest, AltWithoutAutoCommitIsAStepOfItsOwn)
{
    select({a}, a);
    const auto steps = session.historySize();
    ASSERT_TRUE(commitAudioTimes(session, {1000, 5500, false, false, false, AudioAdjacent::Next}, shown));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(ms(line(1).start.value), 5500);
    EXPECT_EQ(session.draftLine(), std::optional(a));
    EXPECT_EQ(ms(line(0).end.value), 2000);
}

TEST(AudioTiming, TheMarkIsDrawnWithItsTime)
{
    const AudioView view = minuteView();
    AudioMarks marks;
    marks.startMs = 3000;
    marks.endMs = 6000;
    marks.markMs = 9000;
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
    marks.markMs = 40000;
    for (const auto &s : audioScene(view, {}, marks, options, [](AudioShape::Font, std::string_view) { return 30; }))
        EXPECT_FALSE(s.kind == AudioShape::Kind::Text && s.text == "0:00:40.00");
}

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
