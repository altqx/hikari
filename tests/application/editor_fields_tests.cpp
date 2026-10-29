#include "hikari/application/editor_fields.h"

#include <gtest/gtest.h>

#include <climits>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

core::LineRecord line(std::int64_t startMs, std::int64_t endMs)
{
    core::LineRecord l;
    l.start.value = core::DocumentTime(startMs * 1000);
    l.end.value = core::DocumentTime(endMs * 1000);
    return l;
}

LegacyTimebase cfr()
{
    std::vector<int> t;
    for (int i = 0; i < 500; ++i)
        t.push_back(i * 40);
    return LegacyTimebase(std::move(t), 25.0);
}

LegacyTimebase vfr()
{
    return LegacyTimebase({0, 40, 81, 120, 200, 210, 300}, 23.976);
}

}

TEST(EditorTimes, EachFormatShowsItsOwnFieldText)
{
    const auto l = line(1234, 5678);
    auto t = editorTimeTexts(l, core::SubtitleFormat::Ass, nullptr);
    EXPECT_EQ(t.start, u8"0:00:01.23");
    EXPECT_EQ(t.end, u8"0:00:05.67");
    EXPECT_EQ(t.duration, u8"0:00:04.44");
    t = editorTimeTexts(l, core::SubtitleFormat::Srt, nullptr);
    EXPECT_EQ(t.start, u8"00:00:01,234");
    EXPECT_EQ(t.duration, u8"00:00:04,444");
    t = editorTimeTexts(l, core::SubtitleFormat::TMPlayer, nullptr);
    EXPECT_EQ(t.start, u8"00:00:01");
    EXPECT_EQ(t.end, u8"00:00:05");
    t = editorTimeTexts(l, core::SubtitleFormat::Mpl2, nullptr);
    EXPECT_EQ(t.start, u8"13");
    EXPECT_EQ(t.end, u8"57");
    EXPECT_EQ(t.duration, u8"45");
    auto m = l;
    m.startFrame = 30;
    m.endFrame = 136;
    t = editorTimeTexts(m, core::SubtitleFormat::MicroDvd, nullptr);
    EXPECT_EQ(t.start, u8"30");
    EXPECT_EQ(t.end, u8"136");
    EXPECT_EQ(t.duration, u8"106");
    t = editorTimeTexts(line(5000, 1000), core::SubtitleFormat::Ass, nullptr);
    EXPECT_EQ(t.duration, u8"0:00:00.00");
}

TEST(EditorTimes, FramesOnACfrTimebase)
{
    const auto tb = cfr();
    const auto t = editorTimeTexts(line(1000, 2000), core::SubtitleFormat::Ass, &tb);
    EXPECT_EQ(t.start, u8"25");
    EXPECT_EQ(t.end, u8"49");
    EXPECT_EQ(t.duration, u8"25");
    EXPECT_EQ(editorTimeTexts(line(1001, 2000), core::SubtitleFormat::Ass, &tb).start, u8"26");
    EXPECT_EQ(editorTimeTexts(line(2000, 1000), core::SubtitleFormat::Ass, &tb).duration, u8"1");
    EXPECT_EQ(editedDurationText(line(1000, 2000), core::SubtitleFormat::Ass, &tb), u8"25");
    EXPECT_EQ(editedDurationText(line(1000, 1050), core::SubtitleFormat::Ass, &tb), u8"2");
    EXPECT_EQ(editedDurationText(line(1000, 2000), core::SubtitleFormat::Ass, nullptr), u8"0:00:01.00");
}

TEST(EditorTimes, FramesOnAVfrTimebase)
{
    const auto tb = vfr();
    const auto t = editorTimeTexts(line(41, 200), core::SubtitleFormat::Ass, &tb);
    EXPECT_EQ(t.start, u8"2");
    EXPECT_EQ(t.end, u8"3");
    EXPECT_EQ(t.duration, u8"2");
    auto start = typedTime(u8"2", TimeFieldRole::Start, core::SubtitleFormat::Ass, std::nullopt, &tb);
    ASSERT_TRUE(start);
    EXPECT_EQ(start->ms, 60);
    auto end = typedTime(u8"3", TimeFieldRole::End, core::SubtitleFormat::Ass, std::nullopt, &tb);
    ASSERT_TRUE(end);
    EXPECT_EQ(end->ms, 160);
    auto duration = typedTime(u8"3", TimeFieldRole::Duration, core::SubtitleFormat::Ass, std::nullopt, &tb);
    ASSERT_TRUE(duration);
    EXPECT_EQ(duration->ms, 120);
}

TEST(EditorTimes, FramesNeedAnExactTimebase)
{
    const LegacyTimebase estimated({}, 25.0);
    EXPECT_EQ(editorTimeTexts(line(1000, 2000), core::SubtitleFormat::Ass, &estimated).start, u8"0:00:01.00");
    EXPECT_EQ(typedTime(u8"0:00:03.00", TimeFieldRole::Start, core::SubtitleFormat::Ass, std::nullopt, &estimated)->ms,
              3000);
}

TEST(EditorTimes, TypedTimesInEachFormat)
{
    const auto at = [](const char8_t *text, core::SubtitleFormat format) {
        return typedTime(text, TimeFieldRole::Start, format, std::nullopt, nullptr);
    };
    EXPECT_EQ(at(u8"0:01:02.34", core::SubtitleFormat::Ass)->ms, 62340);
    EXPECT_EQ(at(u8"01:01:02,345", core::SubtitleFormat::Srt)->ms, 3662345);
    EXPECT_EQ(at(u8"00:01:02", core::SubtitleFormat::TMPlayer)->ms, 62000);
    EXPECT_EQ(at(u8"15", core::SubtitleFormat::Mpl2)->ms, 1500);
    EXPECT_FALSE(at(u8"0:01:02,34", core::SubtitleFormat::Ass));
    EXPECT_FALSE(at(u8"0:01:02.34", core::SubtitleFormat::Srt));
    EXPECT_FALSE(at(u8"00:61:02", core::SubtitleFormat::TMPlayer));
    EXPECT_FALSE(at(u8"1.5", core::SubtitleFormat::Mpl2));
    EXPECT_FALSE(at(u8"-3", core::SubtitleFormat::MicroDvd));
    EXPECT_EQ(at(u8"", core::SubtitleFormat::Mpl2)->ms, 0);
    EXPECT_EQ(at(u8"", core::SubtitleFormat::MicroDvd)->frame, 0);
}

namespace {

TimeFieldState at(const char16_t *text, std::size_t start, std::size_t end)
{
    return TimeFieldState{text, start, end};
}

TimeFieldState at(const char16_t *text, std::size_t caret)
{
    return at(text, caret, caret);
}

std::pair<TimeFieldState, int> typed(TimeFieldForm form, TimeFieldState field, std::u16string_view keys)
{
    int bells = 0;
    for (const char16_t c : keys) {
        const auto e = typeIntoTimeField(form, field, c);
        bells += e.bell;
        field = e.state;
    }
    return {field, bells};
}

}

TEST(TimeField, TheFormFollowsTheFormatAndTheFramesSwitch)
{
    EXPECT_EQ(timeFieldForm(core::SubtitleFormat::Ass, nullptr), TimeFieldForm::Ass);
    EXPECT_EQ(timeFieldForm(core::SubtitleFormat::PlainText, nullptr), TimeFieldForm::Ass);
    EXPECT_EQ(timeFieldForm(core::SubtitleFormat::Srt, nullptr), TimeFieldForm::Srt);
    EXPECT_EQ(timeFieldForm(core::SubtitleFormat::TMPlayer, nullptr), TimeFieldForm::TMPlayer);
    EXPECT_EQ(timeFieldForm(core::SubtitleFormat::Mpl2, nullptr), TimeFieldForm::Number);
    EXPECT_EQ(timeFieldForm(core::SubtitleFormat::MicroDvd, nullptr), TimeFieldForm::Number);
    const auto tb = cfr();
    EXPECT_EQ(timeFieldForm(core::SubtitleFormat::Srt, &tb), TimeFieldForm::Number);
    const LegacyTimebase estimated({}, 25.0);
    EXPECT_EQ(timeFieldForm(core::SubtitleFormat::Srt, &estimated), TimeFieldForm::Srt);
}

TEST(TimeField, TypingOverwritesAndSkipsTheSeparators)
{
    auto [field, bells] = typed(TimeFieldForm::Ass, at(u"0:00:01.50", 0), u"12345678");
    EXPECT_EQ(field.text, u"1:23:45.67");
    EXPECT_EQ(field.selectionStart, 10u);
    EXPECT_EQ(bells, 1);
    std::tie(field, bells) = typed(TimeFieldForm::Srt, at(u"00:00:01,500", 3), u"1234567");
    EXPECT_EQ(field.text, u"00:12:34,567");
    EXPECT_EQ(bells, 0);
    std::tie(field, bells) = typed(TimeFieldForm::TMPlayer, at(u"00:00:01", 2), u"0203");
    EXPECT_EQ(field.text, u"00:02:03");
    EXPECT_EQ(field.selectionStart, 8u);
    std::tie(field, bells) = typed(TimeFieldForm::Ass, at(u"0:00:01.50", 0), u"0:00:02.50x");
    EXPECT_EQ(field.text, u"0:00:02.50");
    EXPECT_EQ(bells, 4);
}

TEST(TimeField, ADigitAfterAColonIsAtMostFive)
{
    EXPECT_EQ(typed(TimeFieldForm::Ass, at(u"0:00:01.50", 2), u"9").first.text, u"0:50:01.50");
    EXPECT_EQ(typed(TimeFieldForm::Ass, at(u"0:00:01.50", 4), u"9").first.text, u"0:00:51.50");
    EXPECT_EQ(typed(TimeFieldForm::Ass, at(u"0:00:01.50", 0), u"9").first.text, u"9:00:01.50");
    EXPECT_EQ(typed(TimeFieldForm::Ass, at(u"0:00:01.50", 7), u"9").first.text, u"0:00:01.90");
    EXPECT_EQ(typed(TimeFieldForm::Srt, at(u"00:00:01,500", 6), u"7").first.text, u"00:00:51,500");
}

TEST(TimeField, ADigitOverASelectionZeroesItFirst)
{
    auto e = typeIntoTimeField(TimeFieldForm::Ass, at(u"1:23:45.67", 2, 9), u'4');
    EXPECT_EQ(e.state.text, u"1:40:00.07");
    EXPECT_EQ(e.state.selectionStart, 3u);
    EXPECT_EQ(e.state.selectionEnd, 3u);
    EXPECT_EQ(typed(TimeFieldForm::Ass, at(u"1:23:45.67", 0, 10), u"0:00:03.25").first.text, u"0:00:03.25");
    EXPECT_EQ(typed(TimeFieldForm::Ass, at(u"1:23:45.67", 0, 10), u"3").first.text, u"3:00:00.00");
    e = typeIntoTimeField(TimeFieldForm::Ass, at(u"1:23:45.67", 2, 9), u'a');
    EXPECT_TRUE(e.bell);
    EXPECT_FALSE(e.changed);
    EXPECT_EQ(e.state.text, u"1:23:45.67");
}

TEST(TimeField, BackspaceZeroesDigits)
{
    auto e = backspaceTimeField(at(u"1:23:45.67", 4));
    EXPECT_EQ(e.state.text, u"1:20:45.67");
    EXPECT_EQ(e.state.selectionStart, 3u);
    e = backspaceTimeField(at(u"1:23:45.67", 3));
    EXPECT_EQ(e.state.text, u"1:03:45.67");
    EXPECT_EQ(e.state.selectionStart, 1u);
    e = backspaceTimeField(at(u"1:23:45.67", 5));
    EXPECT_EQ(e.state.text, u"1:23:45.67");
    EXPECT_FALSE(e.changed);
    EXPECT_EQ(e.state.selectionStart, 4u);
    e = backspaceTimeField(at(u"1:23:45.67", 2, 9));
    EXPECT_EQ(e.state.text, u"1:00:00.07");
    EXPECT_EQ(e.state.selectionStart, 1u);
    e = backspaceTimeField(at(u"1:23:45.67", 0));
    EXPECT_FALSE(e.changed);
    EXPECT_FALSE(e.bell);
    e = backspaceTimeField(at(u"00:12:34,567", 0, 12));
    EXPECT_EQ(e.state.text, u"00:00:00,000");
    EXPECT_EQ(e.state.selectionStart, 0u);
}

TEST(TimeField, PasteTakesOnlyAWholeTimeOfTheForm)
{
    auto e = pasteIntoTimeField(TimeFieldForm::Ass, at(u"0:00:01.50", 3), u"1:02:03.04");
    EXPECT_TRUE(e.changed);
    EXPECT_EQ(e.state.text, u"1:02:03.04");
    EXPECT_EQ(e.state.selectionStart, 0u);
    EXPECT_EQ(e.state.selectionEnd, 10u);
    EXPECT_EQ(pasteIntoTimeField(TimeFieldForm::Srt, at(u"00:00:01,500", 0), u"01:02:03,456").state.text,
              u"01:02:03,456");
    EXPECT_EQ(pasteIntoTimeField(TimeFieldForm::TMPlayer, at(u"00:00:01", 0), u"01:02:03").state.text, u"01:02:03");
    for (const char16_t *bad : {u"1:02:03,04", u"1:62:03.04", u"1:02:63.04", u"01:02:03.04", u"1:02:03.0", u"12",
                                u" 1:02:03.04", u""}) {
        e = pasteIntoTimeField(TimeFieldForm::Ass, at(u"0:00:01.50", 3), bad);
        EXPECT_TRUE(e.bell);
        EXPECT_FALSE(e.changed);
        EXPECT_EQ(e.state.text, u"0:00:01.50");
    }
    EXPECT_TRUE(pasteIntoTimeField(TimeFieldForm::Srt, at(u"00:00:01,500", 0), u"1:02:03.04").bell);
    EXPECT_TRUE(pasteIntoTimeField(TimeFieldForm::TMPlayer, at(u"00:00:01", 0), u"01:02:03,456").bell);
}

TEST(TimeField, NumbersTakeDigitsOnly)
{
    auto [field, bells] = typed(TimeFieldForm::Number, at(u"12", 2), u"3a4");
    EXPECT_EQ(field.text, u"1234");
    EXPECT_EQ(bells, 1);
    std::tie(field, bells) = typed(TimeFieldForm::Number, at(u"1234", 1, 3), u"9");
    EXPECT_EQ(field.text, u"194");
    EXPECT_EQ(field.selectionStart, 2u);
    std::tie(field, bells) = typed(TimeFieldForm::Number, at(u"123456789", 9), u"0");
    EXPECT_EQ(field.text, u"123456789");
    EXPECT_EQ(bells, 1);
    auto e = pasteIntoTimeField(TimeFieldForm::Number, at(u"12", 1, 2), u"345");
    EXPECT_EQ(e.state.text, u"1345");
    EXPECT_EQ(e.state.selectionStart, 4u);
    EXPECT_EQ(pasteIntoTimeField(TimeFieldForm::Number, at(u"1234567", 7), u"8901").state.text, u"123456789");
    for (const char16_t *bad : {u"-5", u"1.5", u"0:00:01.00", u""})
        EXPECT_TRUE(pasteIntoTimeField(TimeFieldForm::Number, at(u"12", 2), bad).bell);
}

TEST(TimeField, TheMouseStepsTheColumnsUnit)
{
    using F = TimeFieldForm;
    for (const auto [column, step] : std::vector<std::pair<std::size_t, std::int64_t>>{
             {0, 3600000}, {1, 3600000}, {2, 60000}, {4, 60000}, {5, 1000}, {7, 1000}, {8, 10}, {10, 10}})
        EXPECT_EQ(timeFieldStep(F::Ass, column), step) << column;
    for (const auto [column, step] : std::vector<std::pair<std::size_t, std::int64_t>>{
             {2, 3600000}, {3, 60000}, {5, 60000}, {6, 1000}, {8, 1000}, {9, 10}, {12, 10}})
        EXPECT_EQ(timeFieldStep(F::Srt, column), step) << column;
    EXPECT_EQ(timeFieldStep(F::TMPlayer, 8), 1000);
}

TEST(TimeField, TheWheelStepsWholeNotches)
{
    using F = TimeCtrlForm;
    const auto wheel = [](F form, std::u16string_view text, std::size_t column, int notches) {
        const auto r = wheelTimeField({form, false}, text, column, notches);
        return r ? std::string(r->begin(), r->end()) : std::string("refused");
    };
    EXPECT_EQ(wheel(F::Ass, u"0:01:02.03", 9, 1), "0:01:02.04");
    EXPECT_EQ(wheel(F::Ass, u"0:01:02.03", 6, -2), "0:01:00.03");
    EXPECT_EQ(wheel(F::Ass, u"0:01:02.03", 3, 3), "0:04:02.03");
    EXPECT_EQ(wheel(F::Ass, u"0:01:02.03", 0, 1), "1:01:02.03");
    EXPECT_EQ(wheel(F::Ass, u"0:00:59.99", 9, 1), "0:01:00.00");
    EXPECT_EQ(wheel(F::Ass, u"0:00:00.05", 9, -1), "0:00:00.04");
    EXPECT_EQ(wheel(F::Ass, u"0:00:00.05", 6, -1), "refused");
    EXPECT_EQ(wheel(F::Ass, u"9:30:00.00", 0, 1), "refused");
    EXPECT_EQ(wheel(F::Ass, u"9:59:59.99", 9, 1), "refused");
    EXPECT_EQ(wheel(F::Ass, u"0:00:01.00", 9, 0), "refused");
    EXPECT_EQ(wheel(F::Srt, u"00:00:01,005", 10, 1), "00:00:01,015");
    EXPECT_EQ(wheel(F::Srt, u"01:00:01,005", 1, -1), "00:00:01,005");
    EXPECT_EQ(wheel(F::TMPlayer, u"00:00:01", 8, 2), "00:00:03");
    EXPECT_EQ(wheel(F::Ass, u"", 9, 1), "0:00:00.01");
}

TEST(TimeField, TheWheelStepsTheNumberShown)
{
    using F = TimeCtrlForm;
    const auto wheel = [](TimeFieldMouse mouse, std::u16string_view text, std::size_t column, int notches) {
        const auto r = wheelTimeField(mouse, text, column, notches);
        return r ? std::string(r->begin(), r->end()) : std::string("refused");
    };
    EXPECT_EQ(wheel({F::MicroDvd, false}, u"120", 1, 1), "121");
    EXPECT_EQ(wheel({F::MicroDvd, false}, u"120", 0, -3), "117");
    EXPECT_EQ(wheel({F::MicroDvd, false}, u"1", 0, -1), "0");
    EXPECT_EQ(wheel({F::MicroDvd, false}, u"3", 0, -3), "0");
    EXPECT_EQ(wheel({F::MicroDvd, false}, u"0", 0, -1), "refused");
    EXPECT_EQ(wheel({F::MicroDvd, false}, u"", 0, 1), "1");
    EXPECT_EQ(wheel({F::MicroDvd, false}, u"999999999", 0, 1), "refused");
    EXPECT_EQ(wheel({F::Mpl2, false}, u"12", 1, 1), "13");
    EXPECT_EQ(wheel({F::Mpl2, false}, u"12", 0, 5), "17");
    EXPECT_EQ(wheel({F::Mpl2, false}, u"12", 1, -1), "11");
    EXPECT_EQ(wheel({F::Mpl2, false}, u"12", 1, -12), "0");
    EXPECT_EQ(wheel({F::Mpl2, false}, u"0", 0, -1), "refused");
    EXPECT_EQ(wheel({F::Mpl2, false}, u"359998", 0, 1), "359999");
    EXPECT_EQ(wheel({F::Mpl2, false}, u"359999", 0, 1), "refused");
    EXPECT_EQ(wheel({F::MicroDvd, true}, u"25", 1, 2), "27");
    EXPECT_EQ(wheel({F::Ass, true}, u"24", 0, 1), "25");
    EXPECT_EQ(wheel({F::Ass, true}, u"0024", 3, -1), "23");
    EXPECT_EQ(wheel({F::Srt, true}, u"24", 1, -1), "23");
    EXPECT_EQ(wheel({F::Mpl2, true}, u"24", 0, -1), "23");
    EXPECT_EQ(wheel({F::TMPlayer, true}, u"0", 0, -1), "refused");
    EXPECT_EQ(wheel({F::Ass, true}, u"", 0, 1), "1");
    EXPECT_EQ(wheel({F::Ass, true}, u"999999999", 0, 1), "refused");
    EXPECT_EQ(timeFieldStep(TimeFieldMouse{F::Mpl2, false}, 0), 1);
    EXPECT_EQ(timeFieldStep(TimeFieldMouse{F::TMPlayer, true}, 2), 1);
    EXPECT_EQ(timeFieldStep(TimeFieldMouse{F::TMPlayer, false}, 2), 3600000);
}

TEST(TimeField, TheRightButtonDragsTheTime)
{
    using F = TimeCtrlForm;
    const TimeFieldMouse ass{F::Ass, false};
    const auto text = [](const std::optional<std::u16string> &r) {
        return r ? std::string(r->begin(), r->end()) : std::string("none");
    };
    auto drag = beginTimeFieldDrag(ass, u"0:01:02.03", 6, 100, 50);
    EXPECT_EQ(drag.value, 62030);
    EXPECT_EQ(drag.step, 1000);
    EXPECT_EQ(text(dragTimeField(ass, drag, 100, 43)), "none");
    EXPECT_EQ(text(dragTimeField(ass, drag, 100, 42)), "0:01:03.03");
    EXPECT_EQ(text(dragTimeField(ass, drag, 103, 30)), "0:01:04.03");
    EXPECT_EQ(text(dragTimeField(ass, drag, 103, 38)), "0:01:03.03");
    EXPECT_EQ(text(dragTimeField(ass, drag, 112, 38)), "none");
    EXPECT_EQ(text(dragTimeField(ass, drag, 113, 38)), "0:01:13.03");
    EXPECT_EQ(text(dragTimeField(ass, drag, 100, 40)), "0:01:03.03");
    EXPECT_EQ(text(dragTimeField(ass, drag, 110, 30)), "0:01:04.03");
    auto hours = beginTimeFieldDrag(ass, u"0:30:00.00", 0, 0, 0);
    EXPECT_EQ(text(dragTimeField(ass, hours, 0, 8)), "0:00:00.00");
    for (int i = 1; i <= 11; ++i)
        dragTimeField(ass, hours, 0, 8 - 8 * i);
    EXPECT_EQ(hours.value, kTimeFieldMaxMs);
    EXPECT_EQ(text(dragTimeField(ass, hours, 0, -100)), "9:59:59.99");
    const TimeFieldMouse srt{F::Srt, false};
    auto s = beginTimeFieldDrag(srt, u"00:00:01,005", 11, 0, 0);
    EXPECT_EQ(text(dragTimeField(srt, s, 0, -8)), "00:00:01,015");
}

TEST(TimeField, TheRightButtonDragsTheNumberShown)
{
    using F = TimeCtrlForm;
    const auto text = [](const std::optional<std::u16string> &r) {
        return r ? std::string(r->begin(), r->end()) : std::string("none");
    };
    const TimeFieldMouse mpl2{F::Mpl2, false};
    auto m = beginTimeFieldDrag(mpl2, u"12", 1, 0, 0);
    EXPECT_EQ(m.value, 12);
    EXPECT_EQ(m.step, 1);
    EXPECT_EQ(text(dragTimeField(mpl2, m, 0, -7)), "none");
    EXPECT_EQ(text(dragTimeField(mpl2, m, 0, -8)), "13");
    EXPECT_EQ(text(dragTimeField(mpl2, m, 0, -16)), "14");
    EXPECT_EQ(text(dragTimeField(mpl2, m, 10, -16)), "24");
    EXPECT_EQ(text(dragTimeField(mpl2, m, 10, -8)), "23");
    auto down = beginTimeFieldDrag(mpl2, u"1", 0, 0, 0);
    EXPECT_EQ(text(dragTimeField(mpl2, down, -10, 0)), "0");
    EXPECT_EQ(text(dragTimeField(mpl2, down, -10, 8)), "none");
    EXPECT_EQ(down.value, 0);
    auto top = beginTimeFieldDrag(mpl2, u"359995", 0, 0, 0);
    EXPECT_EQ(text(dragTimeField(mpl2, top, 10, 0)), "359999");

    const TimeFieldMouse mdvd{F::MicroDvd, false};
    auto d = beginTimeFieldDrag(mdvd, u"25", 0, 0, 0);
    EXPECT_EQ(text(dragTimeField(mdvd, d, 0, -8)), "26");
    EXPECT_EQ(text(dragTimeField(mdvd, d, 0, 0)), "25");
    EXPECT_EQ(text(dragTimeField(mdvd, d, 0, 8)), "24");
    EXPECT_EQ(text(dragTimeField(mdvd, d, -10, 8)), "14");
    auto zero = beginTimeFieldDrag(mdvd, u"0", 0, 0, 0);
    EXPECT_EQ(text(dragTimeField(mdvd, zero, 0, 8)), "none");
    EXPECT_EQ(text(dragTimeField(mdvd, zero, 0, -8)), "1");

    const TimeFieldMouse frames{F::Ass, true};
    auto f = beginTimeFieldDrag(frames, u"24", 1, 0, 0);
    EXPECT_EQ(f.step, 1);
    EXPECT_EQ(text(dragTimeField(frames, f, 0, -8)), "25");
    EXPECT_EQ(text(dragTimeField(frames, f, 0, 0)), "24");
    EXPECT_EQ(text(dragTimeField(frames, f, 10, 0)), "34");
    const TimeFieldMouse mdvdFrames{F::MicroDvd, true};
    auto g = beginTimeFieldDrag(mdvdFrames, u"25", 1, 0, 0);
    EXPECT_EQ(text(dragTimeField(mdvdFrames, g, 0, -8)), "26");
}

TEST(NumberField, TheMouseStepsTheValueWithinTheRange)
{
    const auto text = [](const std::optional<std::u16string> &r) {
        return r ? std::string(r->begin(), r->end()) : std::string("none");
    };
    std::int64_t v = 5;
    EXPECT_EQ(text(wheelNumberField(v, 1, kMarginRange)), "6");
    EXPECT_EQ(text(wheelNumberField(v, -3, kMarginRange)), "3");
    EXPECT_EQ(text(wheelNumberField(v, 0, kMarginRange)), "none");
    std::int64_t top = 9998;
    EXPECT_EQ(text(wheelNumberField(top, 3, kMarginRange)), "9999");
    EXPECT_EQ(top, 9999);
    EXPECT_EQ(text(wheelNumberField(top, 1, kMarginRange)), "none");
    EXPECT_EQ(text(wheelNumberField(top, -1, kMarginRange)), "9998");
    std::int64_t low = 2;
    EXPECT_EQ(text(wheelNumberField(low, -3, kMarginRange)), "0");
    EXPECT_EQ(low, 0);
    EXPECT_EQ(text(wheelNumberField(low, 1, kMarginRange)), "1");
    std::int64_t layer = -10000000;
    EXPECT_EQ(text(wheelNumberField(layer, -1, kLayerRange)), "none");
    EXPECT_EQ(text(wheelNumberField(layer, 2, kLayerRange)), "-9999998");
    std::int64_t typed = 20000;
    EXPECT_EQ(text(wheelNumberField(typed, 1, kMarginRange)), "9999");
    typed = 20000;
    EXPECT_EQ(text(wheelNumberField(typed, -1, kMarginRange)), "9999");
    NumberFieldDrag d{9997, 0, 0};
    EXPECT_EQ(text(dragNumberField(d, 0, -7, kMarginRange)), "none");
    EXPECT_EQ(text(dragNumberField(d, 0, -8, kMarginRange)), "9998");
    EXPECT_EQ(text(dragNumberField(d, 0, -16, kMarginRange)), "9999");
    EXPECT_EQ(text(dragNumberField(d, 0, -24, kMarginRange)), "none");
    EXPECT_EQ(d.y, -16);
    EXPECT_EQ(text(dragNumberField(d, 0, -8, kMarginRange)), "9998");
    EXPECT_EQ(text(dragNumberField(d, 10, -8, kMarginRange)), "9999");
    EXPECT_EQ(text(dragNumberField(d, 20, -8, kMarginRange)), "none");
    EXPECT_EQ(text(dragNumberField(d, 0, -8, kMarginRange)), "9989");
    NumberFieldDrag z{3, 0, 0};
    EXPECT_EQ(text(dragNumberField(z, -10, 0, kMarginRange)), "0");
    NumberFieldDrag out{10001, 0, 0};
    EXPECT_EQ(text(dragNumberField(out, 0, 8, kMarginRange)), "9999");
    EXPECT_EQ(text(dragNumberField(out, -10, 8, kMarginRange)), "9989");
    EXPECT_EQ(numberFieldValue(u"12"), 12);
    EXPECT_EQ(numberFieldValue(u"-3"), -3);
    EXPECT_EQ(numberFieldValue(u"20000"), 20000);
    EXPECT_FALSE(numberFieldValue(u""));
    EXPECT_FALSE(numberFieldValue(u"-"));
    EXPECT_FALSE(numberFieldValue(u"x"));
}

TEST(EditorTimes, MicroDvdFramesUseTheDocumentRate)
{
    auto t = typedTime(u8"50", TimeFieldRole::Start, core::SubtitleFormat::MicroDvd, std::nullopt, nullptr);
    ASSERT_TRUE(t);
    EXPECT_EQ(t->frame, 50);
    EXPECT_FALSE(t->timeResolved);
    const auto rate = core::FrameRate::make(25, 1);
    ASSERT_TRUE(rate);
    t = typedTime(u8"50", TimeFieldRole::Start, core::SubtitleFormat::MicroDvd, *rate, nullptr);
    ASSERT_TRUE(t);
    EXPECT_TRUE(t->timeResolved);
    EXPECT_EQ(t->ms, 2000);
    const auto f = microDvdFieldTime(2001, *rate);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->frame, 51);
    EXPECT_EQ(f->ms, 2040);
    EXPECT_FALSE(microDvdFieldTime(2001, std::nullopt));
}

TEST(EditorTimes, FieldPrecision)
{
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::Ass, 1239), 1230);
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::Srt, 1239), 1239);
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::TMPlayer, 1999), 1000);
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::Mpl2, 1201), 1300);
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::Mpl2, 1200), 1200);
    EXPECT_EQ(fieldPrecisionTime(core::SubtitleFormat::Mpl2, -5), 0);
}

TEST(EditorFields, LayerFollowsNumCtrl)
{
    EXPECT_EQ(layerValue(u8"12"), 12);
    EXPECT_EQ(layerValue(u8"-3"), -3);
    EXPECT_EQ(layerValue(u8"99999999"), 10000000);
    EXPECT_EQ(layerValue(u8"-99999999"), -10000000);
    EXPECT_EQ(layerValue(u8"999999999999999999999999"), 10000000);
    EXPECT_FALSE(layerValue(u8"-"));
    EXPECT_FALSE(layerValue(u8""));
    EXPECT_FALSE(layerValue(u8"1-2"));
    EXPECT_FALSE(layerValue(u8"abc"));
}

TEST(EditorFields, AlignmentFromTheTextOrTheStyle)
{
    core::LineRecord l;
    l.text = u8"{\\an8}top";
    EXPECT_EQ(legacyAlignment(l, 2), 8);
    l.text = u8"plain";
    EXPECT_EQ(legacyAlignment(l, 2), 2);
    l.text = u8"{\\an7}original";
    l.translation = u8"translated";
    EXPECT_EQ(legacyAlignment(l, 3), 3);
    l.translation = u8"{\\pos(1,2)\\an9}x{\\an1}";
    EXPECT_EQ(legacyAlignment(l, 3), 9);
}
