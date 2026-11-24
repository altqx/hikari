#include "hikari/application/visual_vector.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace hikari::application::visual;

namespace {

VectorFrame plainFrame()
{
    VectorFrame frame;
    frame.videoRect = {0, 0, 640, 360};
    return frame;
}

std::u16string text(const std::vector<VectorPoint> &points)
{
    return serializeVectorPoints(points, kClipPointFormat);
}

std::u16string normalised(std::u16string_view drawing)
{
    return text(parseVectorPoints(drawing));
}

PointF onCubic(const std::array<PointF, 4> &c, float t)
{
    const float s = 1 - t;
    return {s * s * s * c[0].x + 3 * s * s * t * c[1].x + 3 * s * t * t * c[2].x + t * t * t * c[3].x,
            s * s * s * c[0].y + 3 * s * s * t * c[1].y + 3 * s * t * t * c[2].y + t * t * t * c[3].y};
}

double distanceToFit(PointF p, const std::vector<std::array<PointF, 4>> &fit)
{
    double best = INFINITY;
    for (const auto &c : fit)
        for (int k = 0; k <= 400; ++k) {
            const PointF q = onCubic(c, static_cast<float>(k) / 400);
            best = std::min(best, std::hypot(double(p.x - q.x), double(p.y - q.y)));
        }
    return best;
}

std::vector<PointF> wave()
{
    std::vector<PointF> out;
    for (int i = 0; i <= 200; ++i)
        out.push_back({static_cast<float>(i * 2),
                       static_cast<float>(100 + 40 * std::sin(i * std::numbers::pi / 50))});
    return out;
}

struct Recorder {
    int previews = 0, commits = 0;
    VectorEditor::Callbacks callbacks()
    {
        VectorEditor::Callbacks cb;
        cb.apply = [this](bool commit) { ++(commit ? commits : previews); };
        cb.bell = [] {};
        cb.notice = [](std::u16string_view) {};
        return cb;
    }
};

Pointer press(int x, int y, Pointer::Button button = Pointer::Button::Left)
{
    Pointer p;
    p.kind = Pointer::Kind::Press;
    p.x = x;
    p.y = y;
    p.button = button;
    p.leftDown = button == Pointer::Button::Left;
    p.rightDown = button == Pointer::Button::Right;
    return p;
}

Pointer drag(int x, int y)
{
    Pointer p;
    p.x = x;
    p.y = y;
    p.leftDown = true;
    return p;
}

Pointer release(int x, int y)
{
    Pointer p;
    p.kind = Pointer::Kind::Release;
    p.x = x;
    p.y = y;
    p.button = Pointer::Button::Left;
    return p;
}

void drawWave(VectorEditor &editor, Recorder &recorder)
{
    const VectorFrame frame = plainFrame();
    const auto samples = wave();
    editor.pointer(press(static_cast<int>(samples.front().x), static_cast<int>(samples.front().y)), frame,
                   recorder.callbacks());
    for (const PointF &p : samples)
        editor.pointer(drag(static_cast<int>(p.x), static_cast<int>(p.y)), frame, recorder.callbacks());
    editor.pointer(release(static_cast<int>(samples.back().x), static_cast<int>(samples.back().y)), frame,
                   recorder.callbacks());
}

}

TEST(FreehandFit, SmoothingSetsTheTolerance)
{
    EXPECT_DOUBLE_EQ(freehandTolerance(0), 0.5);
    EXPECT_DOUBLE_EQ(freehandTolerance(kDefaultSmoothing), 0.5 + 45.0 * 45.0 / 400);
    EXPECT_DOUBLE_EQ(freehandTolerance(100), 25.5);
    EXPECT_DOUBLE_EQ(freehandTolerance(-3), freehandTolerance(0));
    EXPECT_DOUBLE_EQ(freehandTolerance(250), freehandTolerance(100));
}

TEST(FreehandFit, AStraightStrokeIsOneCurveBetweenItsEnds)
{
    std::vector<PointF> line;
    for (int i = 0; i <= 50; ++i)
        line.push_back({static_cast<float>(10 + i * 3), static_cast<float>(20 + i)});
    const auto fit = fitStroke(line, 1);
    ASSERT_EQ(fit.size(), 1u);
    EXPECT_EQ(fit.front()[0], line.front());
    EXPECT_EQ(fit.front()[3], line.back());
}

TEST(FreehandFit, EverySampleIsWithinTheTolerance)
{
    const auto samples = wave();
    for (const double tolerance : {0.5, 2.0, 8.0}) {
        const auto fit = fitStroke(samples, tolerance);
        ASSERT_FALSE(fit.empty());
        EXPECT_EQ(fit.front()[0], samples.front());
        EXPECT_EQ(fit.back()[3], samples.back());
        for (std::size_t i = 1; i < fit.size(); ++i)
            EXPECT_EQ(fit[i][0], fit[i - 1][3]);
        for (const PointF &p : samples)
            EXPECT_LE(distanceToFit(p, fit), tolerance * 1.01) << tolerance;
    }
}

TEST(FreehandFit, MoreSmoothingGivesFewerCurves)
{
    const auto samples = wave();
    EXPECT_GT(fitStroke(samples, freehandTolerance(0)).size(), fitStroke(samples, freehandTolerance(100)).size());
}

TEST(FreehandFit, AStrokeWithoutLengthHasNoCurves)
{
    EXPECT_TRUE(fitStroke({}, 1).empty());
    EXPECT_TRUE(fitStroke({{5, 5}}, 1).empty());
    EXPECT_TRUE(fitStroke({{5, 5}, {5, 5}, {5, 5}}, 1).empty());
}

TEST(FreehandStroke, AReleaseFitsTheStrokeAsOneCommit)
{
    VectorEditor editor;
    editor.points = parseVectorPoints(u"m 10 10 l 50 10 l 50 50");
    editor.mode = VectorEditor::Freehand;
    Recorder recorder;
    drawWave(editor, recorder);
    EXPECT_FALSE(editor.stroking());
    EXPECT_EQ(recorder.previews, 1);
    EXPECT_EQ(recorder.commits, 1);
    const auto curves = editor.strokeCurves();
    ASSERT_TRUE(curves);
    ASSERT_GT(*curves, 0);
    ASSERT_EQ(editor.points.size(), 3u + 1u + 3u * static_cast<std::size_t>(*curves));
    EXPECT_EQ(editor.points[3].type, u'm');
    EXPECT_EQ(editor.points[3].x, 0);
    EXPECT_EQ(editor.points[3].y, 100);
    for (std::size_t i = 4; i < editor.points.size(); ++i) {
        EXPECT_EQ(editor.points[i].type, u'b');
        EXPECT_EQ(editor.points[i].start, (i - 4) % 3 == 0);
    }
}

TEST(FreehandStroke, ARefitReplacesTheStrokeAndKeepsThePointsBeforeIt)
{
    VectorEditor editor;
    editor.points = parseVectorPoints(u"m 10 10 l 50 10 l 50 50");
    editor.mode = VectorEditor::Freehand;
    editor.smoothing = 100;
    Recorder recorder;
    drawWave(editor, recorder);
    const int smooth = *editor.strokeCurves();
    editor.smoothing = 0;
    ASSERT_TRUE(editor.refitStroke());
    const int close = *editor.strokeCurves();
    EXPECT_GT(close, smooth);
    EXPECT_EQ(editor.points.size(), 3u + 1u + 3u * static_cast<std::size_t>(close));
    EXPECT_EQ(text({editor.points.begin(), editor.points.begin() + 3}), normalised(u"m 10 10 l 50 10 l 50 50"));
    EXPECT_EQ(recorder.commits, 1);
}

TEST(FreehandStroke, AnotherEditEndsTheRefits)
{
    VectorEditor editor;
    editor.mode = VectorEditor::Freehand;
    Recorder recorder;
    drawWave(editor, recorder);
    ASSERT_TRUE(editor.strokeCurves());
    Key del;
    del.key = keys::Delete;
    editor.key(del, plainFrame(), recorder.callbacks());
    EXPECT_FALSE(editor.strokeCurves());
    EXPECT_FALSE(editor.refitStroke());

    drawWave(editor, recorder);
    ASSERT_TRUE(editor.strokeCurves());
    editor.pointer(press(5, 5, Pointer::Button::Middle), plainFrame(), recorder.callbacks());
    EXPECT_FALSE(editor.strokeCurves());
}

TEST(FreehandStroke, AClickWithoutAStrokeAddsNothing)
{
    VectorEditor editor;
    editor.mode = VectorEditor::Freehand;
    Recorder recorder;
    editor.pointer(press(20, 20), plainFrame(), recorder.callbacks());
    EXPECT_TRUE(editor.stroking());
    editor.pointer(release(20, 20), plainFrame(), recorder.callbacks());
    EXPECT_TRUE(editor.points.empty());
    EXPECT_FALSE(editor.strokeCurves());
    EXPECT_EQ(recorder.commits, 0);
}

TEST(FreehandStroke, TheWheelLeavesFreehandAsFromBesideSpline)
{
    VectorEditor editor;
    editor.mode = VectorEditor::Freehand;
    editor.wheel(1);
    EXPECT_EQ(editor.mode, VectorEditor::Spline);
    editor.mode = VectorEditor::Freehand;
    editor.wheel(-1);
    EXPECT_EQ(editor.mode, VectorEditor::Move);
}

TEST(VectorSegments, ARightClickFindsTheSegmentUnderIt)
{
    VectorEditor editor;
    editor.points = parseVectorPoints(u"m 0 0 l 60 0 b 60 20 40 40 20 40");
    const VectorFrame frame = plainFrame();
    EXPECT_EQ(editor.segmentAt({30, 2}, frame), (Segment{1, false}));
    const auto curve = editor.segmentAt({48, 28}, frame);
    ASSERT_TRUE(curve);
    EXPECT_EQ(*curve, (Segment{2, true}));
    EXPECT_FALSE(editor.segmentAt({60, 0}, frame));
    EXPECT_FALSE(editor.segmentAt({10, 20}, frame));
}

TEST(VectorSegments, MakeCurveAndMakeStraight)
{
    VectorEditor editor;
    editor.points = parseVectorPoints(u"m 0 0 l 30 0 l 30 30");
    const VectorFrame frame = plainFrame();
    ASSERT_TRUE(editor.editSegment({1, false}, SegmentEdit::MakeCurve, {}, frame));
    EXPECT_EQ(text(editor.points), normalised(u"m 0 0 b 10 0 20 0 30 0 l 30 30"));
    EXPECT_FALSE(editor.editSegment({1, false}, SegmentEdit::MakeCurve, {}, frame));
    ASSERT_TRUE(editor.editSegment({1, true}, SegmentEdit::MakeStraight, {}, frame));
    EXPECT_EQ(text(editor.points), normalised(u"m 0 0 l 30 0 l 30 30"));
}

TEST(VectorSegments, InsertPointHere)
{
    VectorEditor editor;
    const VectorFrame frame = plainFrame();
    editor.points = parseVectorPoints(u"m 0 0 l 30 0 l 30 30");
    ASSERT_TRUE(editor.editSegment({1, false}, SegmentEdit::InsertPoint, {15, 1}, frame));
    EXPECT_EQ(text(editor.points), normalised(u"m 0 0 l 15 1 l 30 0 l 30 30"));

    editor.points = parseVectorPoints(u"m 0 0 b 0 30 30 30 30 0");
    ASSERT_TRUE(editor.editSegment({1, true}, SegmentEdit::InsertPoint, {15, 22}, frame));
    EXPECT_EQ(text(editor.points), normalised(u"m 0 0 b 0 15 7.5 22.5 15 22.5 b 22.5 22.5 30 15 30 0"));
}

TEST(VectorSegments, DeleteSegmentOpensTheContourThere)
{
    VectorEditor editor;
    const VectorFrame frame = plainFrame();
    const std::u16string square = u"m 0 0 l 30 0 l 30 30 l 0 30";
    editor.points = parseVectorPoints(square);
    ASSERT_TRUE(editor.editSegment({2, false}, SegmentEdit::Delete, {}, frame));
    EXPECT_EQ(text(editor.points), normalised(u"m 0 0 l 30 0 m 30 30 l 0 30"));

    editor.points = parseVectorPoints(square);
    ASSERT_TRUE(editor.editSegment({1, false}, SegmentEdit::Delete, {}, frame));
    EXPECT_EQ(text(editor.points), normalised(u"m 30 0 l 30 30 l 0 30"));

    editor.points = parseVectorPoints(square);
    ASSERT_TRUE(editor.editSegment({3, false}, SegmentEdit::Delete, {}, frame));
    EXPECT_EQ(text(editor.points), normalised(u"m 0 0 l 30 0 l 30 30"));

    editor.points = parseVectorPoints(u"m 0 0 l 30 0 b 30 10 20 30 0 30");
    ASSERT_TRUE(editor.editSegment({2, true}, SegmentEdit::Delete, {}, frame));
    EXPECT_EQ(text(editor.points), normalised(u"m 0 0 l 30 0"));
}

TEST(VectorSegments, AStaleSegmentIsRefused)
{
    VectorEditor editor;
    editor.points = parseVectorPoints(u"m 0 0 l 30 0");
    const auto before = editor.points;
    EXPECT_FALSE(editor.editSegment({1, true}, SegmentEdit::MakeStraight, {}, plainFrame()));
    EXPECT_FALSE(editor.editSegment({4, false}, SegmentEdit::Delete, {}, plainFrame()));
    EXPECT_EQ(editor.points, before);
}
