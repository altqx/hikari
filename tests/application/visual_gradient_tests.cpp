#include "hikari/application/visual_gradient.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstddef>
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

class Host : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    BatchPicker picker;
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
    GradientTool tool;

    explicit Fixture(const std::vector<std::string> &texts)
    {
        std::string ass(kHeader);
        for (const auto &t : texts)
            ass += "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,," + t + "\n";
        host.s = std::make_unique<EditSession>(load(ass));
        host.v.setClient(640, 360, 0);
        host.v.setScript(640, 360);
        host.v.open({640, 360, 1, 1});
        select(0);
        tool.selected(host);
    }
    std::vector<core::LineId> ids() const
    {
        std::vector<core::LineId> out;
        for (const auto *l : host.s->document().lines())
            out.push_back(l->id);
        return out;
    }
    void select(std::size_t index)
    {
        Selection sel;
        sel.active = ids()[index];
        sel.selected = {*sel.active};
        host.s->setSelection(sel);
    }
    const core::LineRecord &line(std::size_t index) const { return *host.s->document().lines()[index]; }
    std::string text(std::size_t index) const
    {
        const auto &t = line(index).text;
        return {t.begin(), t.end()};
    }
    std::size_t count() const { return host.s->document().lines().size(); }
    PointF view(double x, double y) const
    {
        const VideoView &v = host.v;
        return {static_cast<float>(((x / v.coeffW()) - v.zoomMove().x) * v.zoomScale().x),
                static_cast<float>(((y / v.coeffH()) - v.zoomMove().y) * v.zoomScale().y)};
    }
    void press(double x, double y)
    {
        const PointF p = view(x, y);
        tool.pointer({Pointer::Kind::Press, static_cast<int>(p.x), static_cast<int>(p.y), Pointer::Button::Left, true},
                     host);
    }
    void move(double x, double y)
    {
        const PointF p = view(x, y);
        tool.pointer({Pointer::Kind::Move, static_cast<int>(p.x), static_cast<int>(p.y), Pointer::Button::None, true},
                     host);
    }
    void release(double x, double y)
    {
        const PointF p = view(x, y);
        tool.pointer({Pointer::Kind::Release, static_cast<int>(p.x), static_cast<int>(p.y), Pointer::Button::Left},
                     host);
    }
    void drag(double x1, double y1, double x2, double y2)
    {
        press(x1, y1);
        move((x1 + x2) / 2, (y1 + y2) / 2);
        move(x2, y2);
        release(x2, y2);
    }
    bool enter()
    {
        Key k;
        k.key = keys::Return;
        return tool.key(k, host);
    }
    void mode(gradient::Mode m) { ASSERT_TRUE(tool.setOption("mode" + std::to_string(static_cast<int>(m)), 1, host)); }
};

constexpr const char *kSign = "{\\pos(320,180)\\clip(100,100,300,200)}Sign";

}

TEST(VisualGradient, ComesBeforeTextOnPathOnTheRailAndIsChosenByItsCommand)
{
    EXPECT_EQ(railOrder()[kFamilyCount - 2], Family::Gradient);
    EXPECT_EQ(familyOfHotkey("VIDEO_TOOL_GRADIENT"), Family::Gradient);
    EXPECT_EQ(makeVisualTool(Family::Gradient)->family(), Family::Gradient);
}

TEST(VisualGradient, ADragPreviewsStripsThatEnterCommitsAsOneStep)
{
    Fixture f({kSign});
    const auto original = f.ids().front();
    EXPECT_FALSE(f.tool.hasPending());
    f.drag(100, 150, 300, 150);
    ASSERT_TRUE(f.tool.hasPending());
    EXPECT_FALSE(f.tool.problem());
    const auto preview = f.tool.previewLines(f.host);
    ASSERT_GT(preview.size(), 1u);
    EXPECT_EQ(f.tool.hiddenLines(f.host), std::vector<core::LineId>{original});
    for (const auto &l : preview)
        EXPECT_NE(std::u8string_view(l.text).find(u8"\\clip("), std::u8string_view::npos);
    EXPECT_EQ(f.count(), 1u);

    EXPECT_TRUE(f.enter());
    EXPECT_FALSE(f.tool.hasPending());
    ASSERT_EQ(f.count(), 1 + preview.size());
    EXPECT_TRUE(f.line(0).comment);
    EXPECT_EQ(f.text(0), kSign);
    for (std::size_t i = 0; i < preview.size(); ++i) {
        EXPECT_FALSE(f.line(i + 1).comment);
        EXPECT_EQ(f.line(i + 1).text, preview[i].text);
        EXPECT_TRUE(f.host.s->selection().selected.contains(f.line(i + 1).id));
    }
    EXPECT_EQ(f.host.s->selection().active, f.line(1).id);
    ASSERT_TRUE(f.host.s->canUndo());
    ASSERT_TRUE(f.host.s->undo());
    EXPECT_EQ(f.count(), 1u);
    EXPECT_FALSE(f.line(0).comment);
}

TEST(VisualGradient, EscDropsThePreviewWithoutTouchingTheFile)
{
    Fixture f({kSign});
    f.drag(100, 150, 300, 150);
    ASSERT_TRUE(f.tool.hasPending());
    EXPECT_TRUE(f.tool.cancelPending(f.host));
    EXPECT_FALSE(f.tool.hasPending());
    EXPECT_TRUE(f.tool.previewLines(f.host).empty());
    EXPECT_TRUE(f.tool.hiddenLines(f.host).empty());
    EXPECT_FALSE(f.enter());
    EXPECT_EQ(f.count(), 1u);
    EXPECT_FALSE(f.host.s->canUndo());
}

TEST(VisualGradient, ValuesAndOptionsChangeThePreviewLive)
{
    Fixture f({kSign});
    f.drag(100, 150, 300, 150);
    const auto before = f.tool.previewLines(f.host).size();
    ASSERT_TRUE(f.tool.setValue("strip", u"50", f.host, ValueEdit::Sample));
    EXPECT_EQ(f.tool.previewLines(f.host).size(), 4u);
    EXPECT_LT(f.tool.previewLines(f.host).size(), before);
    ASSERT_TRUE(f.tool.setValue("to0", u"0", f.host));
    ASSERT_TRUE(f.tool.setValue("to1", u"0", f.host));
    ASSERT_TRUE(f.tool.setValue("to2", u"255", f.host));
    EXPECT_NE(std::u8string_view(f.tool.previewLines(f.host).back().text).find(u8"\\1c&HFF0000&"),
              std::u8string_view::npos);
    EXPECT_FALSE(f.tool.setValue("strip", u"wide", f.host));
    EXPECT_EQ(f.count(), 1u);
    EXPECT_FALSE(f.host.s->canUndo());
}

TEST(VisualGradient, WithoutATagThePreviewExplainsAndApplyRefuses)
{
    Fixture f({kSign});
    ASSERT_EQ(f.tool.currentTag(), "1c");
    ASSERT_TRUE(f.tool.setOption("removeTag", 1, f.host));
    EXPECT_TRUE(f.tool.spec().tags.empty());
    f.drag(100, 150, 300, 150);
    ASSERT_EQ(f.tool.problem(), gradient::Problem::NoTags);
    EXPECT_FALSE(f.tool.apply(f.host));
    EXPECT_EQ(f.host.bells, 1);
    ASSERT_EQ(f.host.notices.size(), 1u);
    EXPECT_EQ(f.host.notices.front(), gradientProblemText(gradient::Problem::NoTags));
    EXPECT_FALSE(f.tool.overlay(f.host).texts.empty());
    EXPECT_EQ(f.count(), 1u);
}

TEST(VisualGradient, PickedLinesCommentEveryStopAndFollowTheLast)
{
    Fixture f({"{\\clip(100,100,300,200)\\1c&H0000FF&}Sign", "{\\clip(100,100,300,200)\\1c&HFF0000&}Sign"});
    f.host.picker.pick(f.ids());
    f.tool.targetsChanged(f.host);
    ASSERT_TRUE(f.tool.setOption("stops", 1, f.host));
    f.drag(100, 150, 300, 150);
    ASSERT_FALSE(f.tool.problem());
    const auto strips = f.tool.previewLines(f.host).size();
    ASSERT_GT(strips, 1u);
    EXPECT_EQ(f.tool.hiddenLines(f.host).size(), 2u);
    ASSERT_TRUE(f.tool.apply(f.host));
    ASSERT_EQ(f.count(), 2 + strips);
    EXPECT_TRUE(f.line(0).comment);
    EXPECT_TRUE(f.line(1).comment);
    EXPECT_NE(f.text(2).find("\\1c&H0000FF&"), std::string::npos);
    EXPECT_NE(f.text(f.count() - 1).find("\\1c&HFF0000&"), std::string::npos);
}

TEST(VisualGradient, ByCharacterPreviewsAsSoonAsItIsChosen)
{
    Fixture f({"{\\1c&H0000FF&}abcd"});
    ASSERT_TRUE(f.tool.setOption("steps", 1, f.host));
    f.mode(gradient::Mode::ByChar);
    ASSERT_TRUE(f.tool.hasPending());
    const auto preview = f.tool.previewLines(f.host);
    ASSERT_EQ(preview.size(), 1u);
    const std::u8string_view text = preview.front().text;
    EXPECT_NE(text.find(u8"}a{"), std::u8string_view::npos);
    EXPECT_NE(text.find(u8"}d"), std::u8string_view::npos);
    ASSERT_TRUE(f.enter());
    EXPECT_EQ(f.count(), 1u);
    EXPECT_EQ(f.line(0).text, preview.front().text);
    EXPECT_FALSE(f.tool.hasPending());
}

TEST(VisualGradient, WedgeTurnsTheLineIntoAMaskFromItsThreePointClip)
{
    Fixture f({"{\\clip(m 100 100 l 300 100 300 160)\\1c&H0000FF&}Sign"});
    f.mode(gradient::Mode::Wedge);
    ASSERT_TRUE(f.tool.hasPending());
    EXPECT_EQ(f.tool.spec().mask.p1, (gradient::Point{100, 100}));
    EXPECT_EQ(f.tool.spec().mask.p3, (gradient::Point{300, 160}));
    const auto preview = f.tool.previewLines(f.host);
    ASSERT_EQ(preview.size(), 1u);
    const std::u8string_view text = preview.front().text;
    EXPECT_NE(text.find(u8"\\p1}m "), std::u8string_view::npos);
    EXPECT_EQ(text.find(u8"\\clip"), std::u8string_view::npos);
}

TEST(VisualGradient, RefusesWhileTheLineIsEditedElsewhere)
{
    Fixture f({kSign});
    f.drag(100, 150, 300, 150);
    ASSERT_TRUE(f.host.beginGesture(f.ids(), "Other"));
    EXPECT_FALSE(f.tool.apply(f.host));
    f.host.cancelGesture();
    EXPECT_EQ(f.count(), 1u);
}
