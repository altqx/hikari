#include "hikari/application/timing_helpers.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

struct Spec {
    int first = 0, after = 0;
    bool comment = false;
};

std::vector<int> ntscTimecodes()
{
    std::vector<int> t;
    for (int f = 0; f < 3000; ++f)
        t.push_back(static_cast<int>(std::int64_t{f} * 1001 / 24));
    return t;
}

std::vector<int> vfrTimecodes()
{
    std::vector<int> t;
    for (int f = 0, ms = 0; f < 3000; ++f, ms += f % 3 == 0 ? 50 : 33)
        t.push_back(ms);
    return t;
}

std::string assTime(int ms)
{
    const int cs = ms / 10;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d:%02d.%02d", cs / 360000, cs / 6000 % 60, cs / 100 % 60, cs % 100);
    return buf;
}

struct Timebase {
    const char *name;
    LegacyTimebase timebase;
};

class TimingHelpers : public ::testing::TestWithParam<Timebase> {
protected:
    const LegacyTimebase &tb() const { return GetParam().timebase; }
    int startOf(int frame) const { return (tb().startTimeFor(frame) / 10) * 10; }

    void load(const std::vector<Spec> &specs)
    {
        std::string text = "[Events]\n";
        for (std::size_t i = 0; i < specs.size(); ++i)
            text += std::string(specs[i].comment ? "Comment" : "Dialogue") + ": 0," + assTime(startOf(specs[i].first)) + "," +
                    assTime(startOf(specs[i].after)) + ",Default,,0,0,0,,line " + std::to_string(i) + "\n";
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        session.emplace(core::loadAss(bytes).document);
    }
    core::LineId id(std::size_t row) const { return session->document().lines()[row]->id; }
    void select(std::vector<std::size_t> rows, std::size_t active)
    {
        std::set<core::LineId> ids;
        for (const auto r : rows)
            ids.insert(id(r));
        session->setSelection(Selection{id(active), ids, id(active), {}});
    }
    std::pair<int, int> frames(std::size_t row) const
    {
        const auto &l = *session->document().lines()[row];
        return {tb().frameAt(static_cast<int>(l.start.value.microseconds() / 1000)),
                tb().frameAt(static_cast<int>(l.end.value.microseconds() / 1000))};
    }
    int startMs(std::size_t row) const
    {
        return static_cast<int>(session->document().lines()[row]->start.value.microseconds() / 1000);
    }
    int endMs(std::size_t row) const
    {
        return static_cast<int>(session->document().lines()[row]->end.value.microseconds() / 1000);
    }
    std::size_t steps() const { return session->historySize(); }

    std::optional<EditSession> session;
    const std::vector<int> keyframes{0, 48, 96, 130, 200, 300};
    const std::vector<Spec> standard{{24, 72}, {100, 160}, {160, 170, true}, {175, 230}};
};

using F = std::pair<int, int>;

}

TEST_P(TimingHelpers, SnapsTheStartToTheKeyframeBeforeOrAfterInOneStep)
{
    load(standard);
    select({1, 3}, 1);
    const auto before = steps();
    ASSERT_TRUE(snapToAdjacentKeyframe(*session, tb(), keyframes, TimingEdge::Start, TimingToward::Previous));
    EXPECT_EQ(steps(), before + 1);
    EXPECT_EQ(frames(1), (F{96, 160}));
    EXPECT_EQ(frames(3), (F{130, 230}));
    ASSERT_TRUE(snapToAdjacentKeyframe(*session, tb(), keyframes, TimingEdge::Start, TimingToward::Previous));
    EXPECT_EQ(frames(1), (F{48, 160}));
    ASSERT_TRUE(session->undo());
    ASSERT_TRUE(session->undo());
    EXPECT_EQ(frames(1), (F{100, 160}));
    ASSERT_TRUE(snapToAdjacentKeyframe(*session, tb(), keyframes, TimingEdge::Start, TimingToward::Next));
    EXPECT_EQ(frames(1), (F{130, 160}));
    EXPECT_EQ(frames(3), (F{200, 230}));
    EXPECT_EQ(startMs(1), startOf(130));
}

TEST_P(TimingHelpers, SnapsTheEndAndNeverCrossesTheOtherEdge)
{
    load({{100, 160}, {100, 120}});
    select({0, 1}, 0);
    ASSERT_TRUE(snapToAdjacentKeyframe(*session, tb(), keyframes, TimingEdge::End, TimingToward::Previous));
    EXPECT_EQ(frames(0), (F{100, 130}));
    EXPECT_EQ(frames(1), (F{100, 120}));
    ASSERT_TRUE(snapToAdjacentKeyframe(*session, tb(), keyframes, TimingEdge::End, TimingToward::Next));
    EXPECT_EQ(frames(0), (F{100, 200}));
    EXPECT_EQ(frames(1), (F{100, 130}));
    select({1}, 1);
    EXPECT_FALSE(snapToAdjacentKeyframe(*session, tb(), keyframes, TimingEdge::Start, TimingToward::Next));
}

TEST_P(TimingHelpers, AnEdgeWithNoKeyframeThatWayChangesNothingAndRecordsNoStep)
{
    load({{0, 40}, {310, 400}});
    select({0}, 0);
    const auto before = steps();
    EXPECT_FALSE(snapToAdjacentKeyframe(*session, tb(), keyframes, TimingEdge::Start, TimingToward::Previous));
    select({1}, 1);
    EXPECT_FALSE(snapToAdjacentKeyframe(*session, tb(), keyframes, TimingEdge::End, TimingToward::Next));
    EXPECT_FALSE(snapToAdjacentKeyframe(*session, tb(), {}, TimingEdge::End, TimingToward::Previous));
    EXPECT_EQ(steps(), before);
    EXPECT_EQ(frames(1), (F{310, 400}));
}

TEST_P(TimingHelpers, LeadInStopsAtThePreviousUncommentedEndAndAtZero)
{
    load(standard);
    select({0, 1, 3}, 1);
    const auto before = steps();
    ASSERT_TRUE(changeLead(*session, tb(), TimingEdge::Start, true, 100000));
    EXPECT_EQ(steps(), before + 1);
    EXPECT_EQ(frames(0), (F{0, 72}));
    EXPECT_EQ(frames(1), (F{72, 160}));
    EXPECT_EQ(frames(3), (F{160, 230}));
}

TEST_P(TimingHelpers, LeadInByAnAmountLandsOnAFrameStart)
{
    load(standard);
    select({3}, 3);
    ASSERT_TRUE(changeLead(*session, tb(), TimingEdge::Start, true, 150));
    const int wanted = tb().frameAt(startOf(175) - 150);
    EXPECT_EQ(frames(3), (F{wanted, 230}));
    EXPECT_EQ(startMs(3), startOf(wanted));
}

TEST_P(TimingHelpers, LeadOutStopsAtTheNextUncommentedStartAndTheLastLineIsFree)
{
    load(standard);
    select({1, 3}, 1);
    ASSERT_TRUE(changeLead(*session, tb(), TimingEdge::End, true, 100000));
    EXPECT_EQ(frames(1), (F{100, 175}));
    EXPECT_GT(frames(3).second, 1000);
}

TEST_P(TimingHelpers, RemovingALeadKeepsOneFrame)
{
    load(standard);
    select({1}, 1);
    ASSERT_TRUE(changeLead(*session, tb(), TimingEdge::Start, false, 100000));
    EXPECT_EQ(frames(1), (F{159, 160}));
    ASSERT_TRUE(session->undo());
    ASSERT_TRUE(changeLead(*session, tb(), TimingEdge::End, false, 100000));
    EXPECT_EQ(frames(1), (F{100, 101}));
}

TEST_P(TimingHelpers, NudgesOneEdgeByWholeFrames)
{
    load(standard);
    select({1, 3}, 1);
    ASSERT_TRUE(nudgeEdgeByFrames(*session, tb(), TimingEdge::Start, -1));
    EXPECT_EQ(frames(1), (F{99, 160}));
    EXPECT_EQ(frames(3), (F{174, 230}));
    ASSERT_TRUE(nudgeEdgeByFrames(*session, tb(), TimingEdge::End, 1));
    EXPECT_EQ(frames(1), (F{99, 161}));
    ASSERT_TRUE(nudgeEdgeByFrames(*session, tb(), TimingEdge::Start, 500));
    EXPECT_EQ(frames(1), (F{160, 161}));
    EXPECT_FALSE(nudgeEdgeByFrames(*session, tb(), TimingEdge::End, -500));
    EXPECT_EQ(frames(1), (F{160, 161}));
    select({0}, 0);
    ASSERT_TRUE(nudgeEdgeByFrames(*session, tb(), TimingEdge::Start, -100));
    EXPECT_EQ(frames(0), (F{0, 72}));
    EXPECT_FALSE(nudgeEdgeByFrames(*session, tb(), TimingEdge::Start, -1));
}

TEST_P(TimingHelpers, ExtendsTheUncommentedNeighbourToTheActiveLine)
{
    load(standard);
    select({3}, 3);
    ASSERT_TRUE(extendNeighbour(*session, TimingToward::Previous));
    EXPECT_EQ(endMs(1), startMs(3));
    EXPECT_EQ(frames(2), (F{160, 170}));
    EXPECT_EQ(frames(3), (F{175, 230}));
    select({0}, 0);
    ASSERT_TRUE(extendNeighbour(*session, TimingToward::Next));
    EXPECT_EQ(startMs(1), endMs(0));
    EXPECT_FALSE(extendNeighbour(*session, TimingToward::Previous));
    select({3}, 3);
    EXPECT_FALSE(extendNeighbour(*session, TimingToward::Next));
}

namespace {

ShiftTimesSettings assistant(int rules = 15)
{
    ShiftTimesSettings s;
    s.postprocessor = rules;
    s.leadIn = 120;
    s.leadOut = 400;
    s.thresholdStart = 620;
    s.keyframeBeforeStart = 350;
    s.keyframeAfterStart = 100;
    s.keyframeBeforeEnd = 300;
    s.keyframeAfterEnd = 900;
    return s;
}

}

TEST_P(TimingHelpers, AssistantSnapsTheStartBackAndTheEndForward)
{
    load({{99, 190}});
    const auto s = suggestTiming(session->document(), id(0), tb(), keyframes, assistant());
    ASSERT_TRUE(s);
    EXPECT_EQ(s->startReason, TimingReason::KeyframeBefore);
    EXPECT_EQ(s->startMs, startOf(96));
    EXPECT_EQ(s->startFrames, 3);
    EXPECT_EQ(s->endReason, TimingReason::KeyframeAfter);
    EXPECT_EQ(s->endMs, startOf(200));
    EXPECT_EQ(s->endFrames, 10);
    EXPECT_FALSE(s->linked);
}

TEST_P(TimingHelpers, AssistantKeepsTimesOnKeyframes)
{
    load({{96, 130}});
    const auto s = suggestTiming(session->document(), id(0), tb(), keyframes, assistant());
    ASSERT_TRUE(s);
    EXPECT_EQ(s->startReason, TimingReason::OnKeyframe);
    EXPECT_EQ(s->endReason, TimingReason::OnKeyframe);
    EXPECT_EQ(s->startMs, s->startBefore);
    EXPECT_EQ(s->endMs, s->endBefore);
}

TEST_P(TimingHelpers, AssistantLinksThePreviousLineAndAcceptsInOneStep)
{
    const std::vector<int> far{0, 2000};
    load({{500, 600}, {610, 650, true}, {606, 640}});
    const auto s = suggestTiming(session->document(), id(2), tb(), far, assistant());
    ASSERT_TRUE(s);
    EXPECT_EQ(s->startReason, TimingReason::Linked);
    ASSERT_TRUE(s->linked);
    EXPECT_EQ(*s->linked, id(0));
    const int linked = startOf(tb().frameAt(startOf(606) - 120));
    EXPECT_EQ(s->startMs, linked);
    EXPECT_EQ(s->endReason, TimingReason::LeadOut);
    EXPECT_EQ(s->endMs, startOf(tb().frameAt(startOf(640) + 400)));
    const auto before = steps();
    ASSERT_TRUE(acceptTiming(*session, *s));
    EXPECT_EQ(steps(), before + 1);
    EXPECT_EQ(session->history().back().name, "Timing assistant");
    EXPECT_EQ(endMs(0), linked);
    EXPECT_EQ(startMs(2), linked);
    ASSERT_TRUE(session->undo());
    EXPECT_EQ(frames(0), (F{500, 600}));
    EXPECT_EQ(frames(2), (F{606, 640}));
}

TEST_P(TimingHelpers, AssistantAddsTheLeadInWhenNothingIsNear)
{
    const std::vector<int> far{0, 2000};
    load({{100, 120}, {600, 650}});
    const auto s = suggestTiming(session->document(), id(1), tb(), far, assistant());
    ASSERT_TRUE(s);
    EXPECT_EQ(s->startReason, TimingReason::LeadIn);
    EXPECT_EQ(s->startMs, startOf(tb().frameAt(startOf(600) - 120)));
    EXPECT_FALSE(s->linked);
}

TEST_P(TimingHelpers, AssistantUsesOnlyTheRulesThatAreOn)
{
    load({{99, 190}, {300, 310, true}});
    const auto none = suggestTiming(session->document(), id(0), tb(), keyframes, assistant(0));
    ASSERT_TRUE(none);
    EXPECT_EQ(none->startReason, TimingReason::Kept);
    EXPECT_EQ(none->endReason, TimingReason::Kept);
    const auto leads = suggestTiming(session->document(), id(0), tb(), keyframes, assistant(3));
    ASSERT_TRUE(leads);
    EXPECT_EQ(leads->startReason, TimingReason::LeadIn);
    EXPECT_EQ(leads->endReason, TimingReason::LeadOut);
    EXPECT_FALSE(suggestTiming(session->document(), id(1), tb(), keyframes, assistant()));
}

TEST_P(TimingHelpers, AssistantAcceptRefusesAnEndBeforeTheStart)
{
    load({{100, 160}});
    auto s = *suggestTiming(session->document(), id(0), tb(), keyframes, assistant());
    s.endMs = s.startMs - 10;
    EXPECT_FALSE(acceptTiming(*session, s));
}

INSTANTIATE_TEST_SUITE_P(Timebases, TimingHelpers,
                         ::testing::Values(Timebase{"Ntsc23976", LegacyTimebase(ntscTimecodes(), 0.0)},
                                           Timebase{"Vfr", LegacyTimebase(vfrTimecodes(), 0.0)}),
                         [](const auto &info) { return std::string(info.param.name); });
