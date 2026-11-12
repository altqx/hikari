#include "hikari/application/automation_services.h"
#include "hikari/application/visual_text_on_path.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::visual;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

class FixedMeasure : public TextMeasurePort {
public:
    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        const double fs = std::strtod(style[2].c_str(), nullptr);
        const std::u16string u = core::toUtf16(std::u8string(text.begin(), text.end()));
        TextExtents e;
        e.width = static_cast<double>(u.size()) * fs * 0.5;
        e.height = fs;
        return e;
    }
};

class Host : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    BatchPicker picker;
    mutable FixedMeasure measurer;
    bool measures = true;
    LegacyTimebase frames;
    int bells = 0;
    std::vector<std::u16string> notices;

    const VideoView &view() const override { return v; }
    const EditSession *session() const override { return s.get(); }
    std::optional<core::LineId> activeLine() const override { return s ? s->selection().active : std::nullopt; }
    std::vector<core::LineId> batchTargets() const override { return s ? picker.targets(*s) : std::vector<core::LineId>{}; }
    std::expected<Gesture *, CommandRefusal> beginGesture(std::vector<core::LineId> targets, std::string history) override
    {
        if (!s || g)
            return std::unexpected(CommandRefusal::Invalid);
        auto r = Gesture::begin(*s, std::move(targets), std::move(history));
        if (!r)
            return std::unexpected(r.error());
        g.emplace(std::move(*r));
        return &*g;
    }
    Gesture *gesture() override { return g ? &*g : nullptr; }
    std::expected<void, CommandRefusal> commitGesture() override
    {
        auto r = g->commit(*s);
        g.reset();
        return r;
    }
    void cancelGesture() override { g.reset(); }
    std::pair<int, int> measureLabel(std::u16string_view text) const override
    {
        return {7 * static_cast<int>(text.size()), 14};
    }
    void toolChanged() override {}
    LegacyTimebase timebase() const override { return frames; }
    TextMeasurePort *textMeasure() const override { return measures ? &measurer : nullptr; }
    void bell() override { ++bells; }
    void notice(std::u16string_view text) override { notices.emplace_back(text); }
};

constexpr std::string_view kHeader = "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n"
                                     "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                                     "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, "
                                     "Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
                                     "MarginV, Encoding\nStyle: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,"
                                     "&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n\n[Events]\nFormat: Layer, "
                                     "Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

struct Fixture {
    Host host;
    TextOnPathTool tool;

    explicit Fixture(const std::string &text, const std::string &end = "0:00:01.00")
    {
        host.s = std::make_unique<EditSession>(
            load(std::string(kHeader) + "Dialogue: 0,0:00:00.00," + end + ",Default,,0,0,0,," + text + "\n"));
        host.v.setClient(640, 360, 0);
        host.v.setScript(640, 360);
        host.v.open({640, 360, 1, 1});
        Selection sel;
        sel.active = host.s->document().lines()[0]->id;
        sel.selected = {*sel.active};
        host.s->setSelection(sel);
        tool.selected(host);
    }
    std::string text(std::size_t index) const
    {
        const auto &t = host.s->document().lines()[index]->text;
        return {t.begin(), t.end()};
    }
    std::size_t count() const { return host.s->document().lines().size(); }
    bool enter()
    {
        Key k;
        k.key = keys::Return;
        return tool.key(k, host);
    }
    void click(double x, double y)
    {
        const int px = static_cast<int>(x), py = static_cast<int>(y);
        tool.pointer({Pointer::Kind::Press, px, py, Pointer::Button::Left, true}, host);
        tool.pointer({Pointer::Kind::Release, px, py, Pointer::Button::Left}, host);
    }
    std::string value(const std::string &name) const
    {
        for (const auto &v : tool.values(host))
            if (v.name == name) {
                const std::u8string text = core::toUtf8(v.text);
                return {text.begin(), text.end()};
            }
        return "-";
    }
};

}

TEST(VisualTextOnPath, IsTheLastFamilyOnTheRailWithItsOwnCommand)
{
    EXPECT_EQ(railOrder().back(), Family::TextOnPath);
    EXPECT_EQ(familyOfHotkey("VIDEO_TOOL_TEXT_ON_PATH"), Family::TextOnPath);
    EXPECT_EQ(familyInfo(Family::TextOnPath).history, "Text on path");
    EXPECT_EQ(makeVisualTool(Family::TextOnPath)->family(), Family::TextOnPath);
}

TEST(VisualTextOnPath, TheClipSeedsAPreviewThatEnterCommitsAsOneStep)
{
    Fixture f("{\\clip(m 100 300 l 500 300)\\c&H0000FF&}Sign");
    const auto original = f.host.s->document().lines()[0]->id;
    const auto preview = f.tool.previewLines(f.host);
    ASSERT_EQ(preview.size(), 4u);
    EXPECT_EQ(std::string(preview[0].text.begin(), preview[0].text.end()), "{\\c&H0000FF&\\an2\\pos(110,300)\\frz0}S");
    EXPECT_EQ(f.tool.hiddenLines(f.host), std::vector<core::LineId>{original});
    EXPECT_EQ(f.value("lines"), "4");
    const auto steps = f.host.s->historySize();

    EXPECT_TRUE(f.enter());
    EXPECT_EQ(f.host.s->historySize(), steps + 1);
    EXPECT_EQ(f.host.s->history().back().name, "Text on path");
    ASSERT_EQ(f.count(), 5u);
    EXPECT_TRUE(f.host.s->document().lines()[0]->comment) << "the original is commented and kept";
    EXPECT_EQ(f.text(4), "{\\c&H0000FF&\\an2\\pos(170,300)\\frz0}n");
    EXPECT_EQ(f.host.s->selection().selected.size(), 4u);
    ASSERT_TRUE(f.host.s->undo());
    EXPECT_EQ(f.count(), 1u);
}

TEST(VisualTextOnPath, WithoutAPathNothingIsPendingAndApplyRings)
{
    Fixture f("Sign");
    EXPECT_FALSE(f.tool.hasPending());
    EXPECT_EQ(f.tool.problem(), TextOnPathTool::Problem::NoPath);
    EXPECT_FALSE(f.tool.apply(f.host));
    EXPECT_EQ(f.host.bells, 1);
    ASSERT_EQ(f.host.notices.size(), 1u);
    EXPECT_EQ(f.host.notices[0], u"Draw a path, or give the line a vector clip");
}

TEST(VisualTextOnPath, ThePointEditorDrawsThePath)
{
    Fixture f("Sign");
    f.click(100, 200);
    f.click(400, 200);
    EXPECT_TRUE(f.tool.hasPending());
    EXPECT_EQ(f.tool.previewLines(f.host).size(), 4u);
}

TEST(VisualTextOnPath, LayoutOptionsChangeThePreview)
{
    Fixture f("{\\clip(m 100 300 l 500 300)}Sign");
    ASSERT_TRUE(f.tool.setOption("align1", 1, f.host));
    const auto centred = f.tool.previewLines(f.host);
    EXPECT_EQ(std::string(centred[0].text.begin(), centred[0].text.end()), "{\\an2\\pos(270,300)\\frz0}S");
    ASSERT_TRUE(f.tool.setValue("offset", u"10", f.host));
    const auto moved = f.tool.previewLines(f.host);
    EXPECT_EQ(std::string(moved[0].text.begin(), moved[0].text.end()), "{\\an2\\pos(280,300)\\frz0}S");
    ASSERT_TRUE(f.tool.setOption("rotate", 0, f.host));
    const auto flat = f.tool.previewLines(f.host);
    EXPECT_EQ(std::string(flat[0].text.begin(), flat[0].text.end()), "{\\an2\\pos(280,300)}S");
}

TEST(VisualTextOnPath, TextLongerThanThePathShowsHowFarItRunsOn)
{
    Fixture f("{\\clip(m 100 300 l 140 300)}Sign");
    EXPECT_NEAR(f.tool.overflow(), 40, 1e-9);
    EXPECT_EQ(f.value("overflow"), "40");
    EXPECT_TRUE(f.enter()) << "Apply still works";
    EXPECT_EQ(f.count(), 5u);
}

TEST(VisualTextOnPath, SlideMakesALinePerGraphemeForEachStep)
{
    Fixture f("{\\clip(m 100 300 l 500 300)}Sign", "0:00:01.00");
    std::vector<int> t;
    for (int i = 0; i < 60; ++i)
        t.push_back(i * 40);
    f.host.frames = LegacyTimebase(std::move(t), 25.0);
    ASSERT_TRUE(f.tool.setOption("slide", 1, f.host));
    ASSERT_TRUE(f.tool.setValue("every", u"5", f.host));
    EXPECT_EQ(f.value("lines"), "20") << "25 frames in steps of 5, four graphemes";
    const auto preview = f.tool.previewLines(f.host);
    ASSERT_EQ(preview.size(), 20u);
    EXPECT_EQ(preview[0].start.value.microseconds(), 0);
    EXPECT_EQ(preview[4].start.value.microseconds(), preview[0].end.value.microseconds()) << "steps abut";
    EXPECT_EQ(preview[19].end.value.microseconds(), 1'000'000);
    EXPECT_TRUE(f.enter());
    EXPECT_EQ(f.count(), 21u);
    EXPECT_EQ(f.host.s->document().lines()[20]->end.value.microseconds(), 1'000'000);
}

TEST(VisualTextOnPath, SlideWithoutVideoFramesSaysSo)
{
    Fixture f("{\\clip(m 100 300 l 500 300)}Sign");
    ASSERT_TRUE(f.tool.setOption("slide", 1, f.host));
    EXPECT_EQ(f.tool.problem(), TextOnPathTool::Problem::NoFrames);
    EXPECT_FALSE(f.tool.hasPending());
}

TEST(VisualTextOnPath, EscDropsThePreview)
{
    Fixture f("{\\clip(m 100 300 l 500 300)}Sign");
    ASSERT_TRUE(f.tool.hasPending());
    EXPECT_TRUE(f.tool.cancelPending(f.host));
    EXPECT_FALSE(f.tool.hasPending());
    EXPECT_TRUE(f.tool.previewLines(f.host).empty());
    EXPECT_EQ(f.count(), 1u);
}
