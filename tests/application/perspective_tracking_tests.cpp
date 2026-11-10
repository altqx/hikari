#include "hikari/application/automation_services.h"
#include "hikari/application/perspective_tracking.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/extradata.h"

#include <gtest/gtest.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace hikari;
using namespace hikari::application;
namespace persp = hikari::application::visual::perspective;

namespace {

struct FixedMeasure : TextMeasurePort {
    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        const double size = style.size() > 2 ? std::atof(style[2].c_str()) : 20;
        double points = 0;
        for (unsigned char c : text)
            points += (c & 0xC0) != 0x80;
        return TextExtents{points * size / 2, size, size / 5, 0};
    }
};

LegacyTimebase corpusVideo()
{
    std::vector<int> starts;
    for (int i = 0; i < 2398; ++i)
        starts.push_back(static_cast<int>(std::int64_t{i} * 1001 / 24));
    return LegacyTimebase(std::move(starts), 24000.0 / 1001.0);
}

std::string signs(const std::vector<std::string> &lines)
{
    std::string out = "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n[V4+ Styles]\n"
                      "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
                      "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
                      "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                      "Style: Sign,Arial,48,&H0000FFFF,&H000000FF,&H00000000,&H00000000,-1,0,0,0,100,100,0,0,1,1,0,8,"
                      "10,10,10,1\n\n[Events]\n"
                      "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (const auto &line : lines)
        out += line + "\n";
    return out;
}

core::Document load(const std::string &text)
{
    std::vector<std::byte> b(text.size());
    std::memcpy(b.data(), text.data(), text.size());
    return core::loadAss(b).document;
}

void selectAll(EditSession &session)
{
    Selection s;
    for (const auto *l : session.document().lines())
        s.selected.insert(l->id);
    s.active = session.document().lines().front()->id;
    session.setSelection(s);
}

std::string text(const core::Document &d, std::size_t row)
{
    const auto &t = d.lines()[row]->text;
    return std::string(t.begin(), t.end());
}

double tag(const std::string &text, std::string_view name, double initial)
{
    const std::string head = text.substr(0, text.find('}'));
    for (std::size_t at = head.find('\\'); at != std::string::npos; at = head.find('\\', at + 1)) {
        const std::string_view rest = std::string_view(head).substr(at + 1);
        if (rest.starts_with(name) && rest.size() > name.size() &&
            (std::isdigit(static_cast<unsigned char>(rest[name.size()])) || rest[name.size()] == '-' ||
             rest[name.size()] == '('))
            return std::strtod(head.c_str() + at + 1 + name.size() + (rest[name.size()] == '(' ? 1 : 0), nullptr);
    }
    return initial;
}

double second(const std::string &text, std::string_view name)
{
    const std::size_t at = text.find("\\" + std::string(name) + "(");
    return at == std::string::npos ? NAN : std::strtod(text.c_str() + text.find(',', at) + 1, nullptr);
}

std::string pin(int corner, const std::vector<int> &xs, int y)
{
    std::string out = "Effects\tCC Power Pin #1\tCC Power Pin-000" + std::to_string(corner) +
                      "\n\tFrame\tX pixels\tY pixels\n";
    for (std::size_t i = 0; i < xs.size(); ++i)
        out += "\t" + std::to_string(i) + "\t" + std::to_string(xs[i]) + "\t" + std::to_string(y) + "\n";
    return out + "\n";
}

std::string powerPin(int width = 1920, int height = 1080, int scale = 1)
{
    const auto s = [scale](std::vector<int> v) {
        for (int &x : v)
            x /= scale;
        return v;
    };
    return "Adobe After Effects 6.0 Keyframe Data\n\n\tUnits Per Second\t23.976\n\tSource Width\t" +
           std::to_string(width) + "\n\tSource Height\t" + std::to_string(height) +
           "\n\tSource Pixel Aspect Ratio\t1\n\tComp Pixel Aspect Ratio\t1\n\n" +
           pin(2, s({600, 610, 620, 630, 640}), 300 / scale) + pin(3, s({1000, 1010, 1020, 1030, 1040}), 300 / scale) +
           pin(4, s({600, 610, 620, 630, 640}), 500 / scale) + pin(5, s({1000, 1010, 1020, 1030, 1040}), 500 / scale) +
           "End of Keyframe Data\n";
}

PowerPin parsed(const std::string &data)
{
    const auto out = parsePowerPin(std::u8string_view(reinterpret_cast<const char8_t *>(data.data()), data.size()));
    EXPECT_TRUE(out.has_value());
    return out.value_or(PowerPin{});
}

TrackingOptions options(FixedMeasure &m)
{
    TrackingOptions o;
    o.measure.port = &m;
    o.videoWidth = 1920;
    o.videoHeight = 1080;
    return o;
}

const std::string kCorpusLine = "Dialogue: 3,0:00:14.00,0:00:14.21,Sign,,0,0,0,,{\\fad(80,0)\\pos(800,400)\\frx10\\fry-5}fbf";

void expectCorpusOutput(const core::Document &d)
{
    const double pos[] = {800, 810.01, 819.965, 829.864, 839.707};
    const double fscx[] = {100, 99.443, 98.888, 98.334, 97.781};
    const double fscy[] = {100, 99.728, 99.457, 99.185, 98.914};
    const double fry[] = {-5, -5.014, -5.028, -5.042, -5.056};
    const double frx[] = {10, 10.027, 10.055, 10.082, 10.11};
    const double fax[] = {0, -0.005, -0.011, -0.017, -0.022};
    const double xbord[] = {1, 0.99, 0.99, 0.98, 0.98};
    const double ybord[] = {1, 1, 0.99, 0.99, 0.99};
    const char *alpha[] = {"\\alpha&HD3&", "\\alpha&H50&", nullptr, nullptr, nullptr};
    const int starts[] = {13993, 14035, 14076, 14118, 14160};
    ASSERT_EQ(d.lines().size(), 5u);
    for (std::size_t i = 0; i < 5; ++i) {
        const std::string out = text(d, i);
        EXPECT_NEAR(tag(out, "pos", 0), pos[i], 0.006) << out;
        EXPECT_NEAR(second(out, "pos"), 400, 0.006) << out;
        EXPECT_NEAR(tag(out, "org", 0), 800, 0.006) << out;
        EXPECT_NEAR(second(out, "org"), 400, 0.006) << out;
        EXPECT_NEAR(tag(out, "fscx", 100), fscx[i], 0.006) << out;
        EXPECT_NEAR(tag(out, "fscy", 100), fscy[i], 0.006) << out;
        EXPECT_NEAR(tag(out, "fry", 0), fry[i], 0.0006) << out;
        EXPECT_NEAR(tag(out, "frx", 0), frx[i], 0.0006) << out;
        EXPECT_NEAR(tag(out, "fax", 0), fax[i], 0.0006) << out;
        EXPECT_NEAR(tag(out, "frz", 0), 0, 0.0006) << out;
        EXPECT_NEAR(tag(out, "xbord", 1), xbord[i], 0.006) << out;
        EXPECT_NEAR(tag(out, "ybord", 1), ybord[i], 0.006) << out;
        EXPECT_EQ(out.find("\\fad"), std::string::npos) << out;
        if (alpha[i])
            EXPECT_NE(out.find(alpha[i]), std::string::npos) << out;
        else
            EXPECT_EQ(out.find("\\alpha"), std::string::npos) << out;
        EXPECT_TRUE(out.ends_with("}fbf")) << out;
        EXPECT_NEAR(d.lines()[i]->start.value.microseconds() / 1000.0, starts[i], 10);
        const auto plane =
            persp::parsePlane(core::extradataValue(d.lines()[i]->extra, persp::kPlaneKey).value_or(u8""));
        ASSERT_TRUE(plane);
        const float x = 600.f + 10.f * static_cast<float>(i);
        EXPECT_FLOAT_EQ((*plane)[0].x, x);
        EXPECT_FLOAT_EQ((*plane)[0].y, 300);
        EXPECT_FLOAT_EQ((*plane)[2].x, x + 400);
        EXPECT_FLOAT_EQ((*plane)[2].y, 500);
    }
}

TEST(PowerPinData, ReadsTheFourCornersAndTheSource)
{
    const PowerPin data = parsed(powerPin());
    ASSERT_EQ(data.frames.size(), 5u);
    EXPECT_NEAR(data.fps, 23.976, 1e-9);
    EXPECT_EQ(data.width, 1920);
    EXPECT_EQ(data.height, 1080);
    const persp::Quad second{visual::PointF{610, 300}, visual::PointF{1010, 300}, visual::PointF{1010, 500},
                             visual::PointF{610, 500}};
    EXPECT_EQ(data.frames[1], second);
}

TEST(PowerPinData, AnythingElseIsRefused)
{
    const auto read = [](const std::string &s) {
        return parsePowerPin(std::u8string_view(reinterpret_cast<const char8_t *>(s.data()), s.size()));
    };
    EXPECT_FALSE(read(""));
    EXPECT_FALSE(read("Effects\tADBE Corner Pin #1\tADBE Corner Pin-0001\n\tFrame\tX pixels\tY pixels\n\t0\t600\t300\n"));
    EXPECT_FALSE(read(pin(2, {1, 2}, 0) + pin(3, {1, 2}, 0) + pin(4, {1, 2}, 0) + pin(5, {1}, 0)));
    EXPECT_FALSE(read(pin(2, {1}, 0) + pin(3, {1}, 0) + pin(4, {1}, 0)));
}

TEST(PerspectiveTracking, TracksTheCorpusLineAsPerspectiveMotionDoes)
{
    FixedMeasure m;
    EditSession session{load(signs({kCorpusLine}))};
    selectAll(session);
    ASSERT_TRUE(applyTracking(session, corpusVideo(), parsed(powerPin()), options(m)));
    expectCorpusOutput(session.document());
}

TEST(PerspectiveTracking, DataTrackedAtAnotherSizeIsScaledToTheVideo)
{
    FixedMeasure m;
    EditSession session{load(signs({kCorpusLine}))};
    selectAll(session);
    ASSERT_TRUE(applyTracking(session, corpusVideo(), parsed(powerPin(960, 540, 2)), options(m)));
    expectCorpusOutput(session.document());
}

TEST(PerspectiveTracking, IsOneUndoStepAndSelectsTheTrackedLines)
{
    FixedMeasure m;
    EditSession session{load(signs({kCorpusLine}))};
    selectAll(session);
    const std::u8string before = session.document().lines().front()->text;
    ASSERT_TRUE(applyTracking(session, corpusVideo(), parsed(powerPin()), options(m)));
    EXPECT_EQ(session.history().back().name, "Perspective tracking");
    EXPECT_EQ(session.selection().selected.size(), 5u);
    ASSERT_TRUE(session.undo());
    ASSERT_EQ(session.document().lines().size(), 1u);
    EXPECT_EQ(session.document().lines().front()->text, before);
    EXPECT_TRUE(session.document().lines().front()->extra.empty());
}

TEST(PerspectiveTracking, TheDataMustCoverTheSelectedFrames)
{
    EditSession session{load(signs({kCorpusLine}))};
    selectAll(session);
    const PowerPin data = parsed(powerPin());
    PowerPin longer = data;
    longer.frames.push_back(data.frames.back());
    const TrackingCheck check = checkTracking(session, corpusVideo(), longer, 1);
    EXPECT_EQ(check.problem, TrackingProblem::Length);
    EXPECT_EQ(check.frames.count, 5);
    EXPECT_FALSE(checkTracking(session, corpusVideo(), data, 1).problem);
    FixedMeasure m;
    EXPECT_FALSE(applyTracking(session, corpusVideo(), longer, options(m)));
    EXPECT_EQ(session.document().lines().size(), 1u);
}

TEST(PerspectiveTracking, OverlappingLinesMustAllShowAtTheRelativeFrame)
{
    EditSession session{load(signs({"Dialogue: 0,0:00:14.00,0:00:14.21,Sign,,0,0,0,,{\\pos(800,400)}a",
                                    "Dialogue: 0,0:00:14.10,0:00:14.21,Sign,,0,0,0,,{\\pos(800,450)}b"}))};
    selectAll(session);
    const PowerPin data = parsed(powerPin());
    const TrackingCheck first = checkTracking(session, corpusVideo(), data, 1);
    EXPECT_EQ(first.problem, TrackingProblem::Overlap);
    EXPECT_EQ(first.row, 2);
    EXPECT_FALSE(checkTracking(session, corpusVideo(), data, 4).problem);
}

TEST(PerspectiveTracking, ClipsFollowThePlane)
{
    FixedMeasure m;
    EditSession session{load(signs({"Dialogue: 3,0:00:14.00,0:00:14.21,Sign,,0,0,0,,"
                                    "{\\pos(800,400)\\frx10\\clip(700,350,900,450)}fbf"}))};
    selectAll(session);
    ASSERT_TRUE(applyTracking(session, corpusVideo(), parsed(powerPin()), options(m)));
    EXPECT_NE(text(session.document(), 0).find("\\clip(m 700 350 l 900 350 900 450 700 450)"), std::string::npos)
        << text(session.document(), 0);
    EXPECT_NE(text(session.document(), 4).find("\\clip(m 740 350 l 940 350 940 450 740 450)"), std::string::npos)
        << text(session.document(), 4);
}

TEST(PerspectiveTracking, OutlinesCanKeepTheirSize)
{
    FixedMeasure m;
    EditSession session{load(signs({kCorpusLine}))};
    selectAll(session);
    TrackingOptions o = options(m);
    o.scaleBorders = false;
    ASSERT_TRUE(applyTracking(session, corpusVideo(), parsed(powerPin()), o));
    const std::string last = text(session.document(), 4);
    EXPECT_NEAR(tag(last, "xbord", 1), 1, 1e-9) << last;
    EXPECT_NEAR(tag(last, "fscx", 100), 97.781, 0.006) << last;
}

}

namespace {

std::string narrowed(const std::u8string &text)
{
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

void setPlane(EditSession &session, std::size_t row, std::u8string_view plane)
{
    const core::LineId id = session.document().lines()[row]->id;
    ASSERT_TRUE(session.run(Command{"Plane", session.revision(), {id}, [&](core::Document &d) {
        return d.editLine(id, [&](core::LineRecord &l) { l.extra = core::withExtradata(l.extra, persp::kPlaneKey, std::u8string(plane)); });
    }}));
}

}

TEST(PerspectiveTracking, PowerPinFromTrackedLinesReadsBackAsTheirPlanes)
{
    FixedMeasure m;
    EditSession session{load(signs({kCorpusLine}))};
    selectAll(session);
    ASSERT_TRUE(applyTracking(session, corpusVideo(), parsed(powerPin()), options(m)));
    selectAll(session);
    const auto text = derivePowerPinTrack(session, corpusVideo(), 23.976, 1080, options(m).measure);
    ASSERT_TRUE(text);
    const PowerPin back = parsed(narrowed(*text));
    EXPECT_EQ(back.width, 1920);
    EXPECT_EQ(back.height, 1080);
    EXPECT_DOUBLE_EQ(back.fps, 23.976);
    ASSERT_EQ(back.frames.size(), session.document().lines().size());
    for (std::size_t i = 0; i < back.frames.size(); ++i) {
        const auto plane = persp::parsePlane(*core::extradataValue(session.document().lines()[i]->extra, persp::kPlaneKey));
        ASSERT_TRUE(plane);
        for (std::size_t c = 0; c < 4; ++c) {
            EXPECT_FLOAT_EQ(back.frames[i][c].x, (*plane)[c].x) << i << " " << c;
            EXPECT_FLOAT_EQ(back.frames[i][c].y, (*plane)[c].y) << i << " " << c;
        }
    }
}

TEST(PerspectiveTracking, PowerPinIsWrittenAsDerivePerspectiveTrackWritesIt)
{
    FixedMeasure m;
    EditSession session{load(signs({"Dialogue: 0,0:00:14.00,0:00:14.05,Sign,,0,0,0,,{\\pos(800,400)}a",
                                    "Dialogue: 0,0:00:14.13,0:00:14.17,Sign,,0,0,0,,{\\pos(800,400)}b"}))};
    setPlane(session, 0, u8"600.5;300|1000;300.25|1000;500|600;500");
    setPlane(session, 1, u8"610;-3|1010;300|1010;500|610;500");
    selectAll(session);
    const auto text = derivePowerPinTrack(session, corpusVideo(), 23.976, 1080, options(m).measure);
    ASSERT_TRUE(text);
    const std::string expected = "Adobe After Effects 6.0 Keyframe Data\n\n\tUnits Per Second\t23.976\n"
                                 "\tSource Width\t1920\n\tSource Height\t1080\n\tSource Pixel Aspect Ratio\t1\n"
                                 "\tComp Pixel Aspect Ratio\t1\n\n"
                                 "Effects\tCC Power Pin #1\tCC Power Pin-0002\n\tFrame\tX pixels\tY pixels\n"
                                 "\t0\t600.5\t300\n\t1\t600.5\t300\n\t2\t600.5\t300\n\t3\t610\t-3\n\n"
                                 "Effects\tCC Power Pin #1\tCC Power Pin-0003\n\tFrame\tX pixels\tY pixels\n"
                                 "\t0\t1000\t300.25\n\t1\t1000\t300.25\n\t2\t1000\t300.25\n\t3\t1010\t300\n\n"
                                 "Effects\tCC Power Pin #1\tCC Power Pin-0004\n\tFrame\tX pixels\tY pixels\n"
                                 "\t0\t600\t500\n\t1\t600\t500\n\t2\t600\t500\n\t3\t610\t500\n\n"
                                 "Effects\tCC Power Pin #1\tCC Power Pin-0005\n\tFrame\tX pixels\tY pixels\n"
                                 "\t0\t1000\t500\n\t1\t1000\t500\n\t2\t1000\t500\n\t3\t1010\t500\n\n"
                                 "End of Keyframe Data";
    EXPECT_EQ(narrowed(*text), expected);
}

TEST(PerspectiveTracking, PowerPinFallsBackToTheTagsAndRefusesWhatItCannotRead)
{
    FixedMeasure m;
    EditSession session{load(signs({kCorpusLine}))};
    selectAll(session);
    const auto text = derivePowerPinTrack(session, corpusVideo(), 23.976, 1080, options(m).measure);
    ASSERT_TRUE(text);
    const PowerPin pin = parsed(narrowed(*text));
    ASSERT_EQ(pin.frames.size(), 5u);
    EXPECT_TRUE(persp::convex(pin.frames.front()));
    EXPECT_EQ(pin.frames.front(), pin.frames.back());
    EXPECT_EQ(parsed(narrowed(writePowerPin(pin))).frames, pin.frames);

    EditSession moving{load(signs({"Dialogue: 0,0:00:14.00,0:00:14.21,Sign,,0,0,0,,{\\move(1,2,3,4)}a"}))};
    selectAll(moving);
    EXPECT_EQ(derivePowerPinTrack(moving, corpusVideo(), 23.976, 1080, options(m).measure).error().problem,
              PowerPinProblem::Move);

    EditSession overlapping{load(signs({kCorpusLine, kCorpusLine}))};
    selectAll(overlapping);
    const auto overlap = derivePowerPinTrack(overlapping, corpusVideo(), 23.976, 1080, options(m).measure);
    ASSERT_FALSE(overlap);
    EXPECT_EQ(overlap.error().problem, PowerPinProblem::Overlap);
    EXPECT_EQ(overlap.error().row, 2);

    EditSession unmeasured{load(signs({kCorpusLine}))};
    selectAll(unmeasured);
    EXPECT_EQ(derivePowerPinTrack(unmeasured, corpusVideo(), 23.976, 1080, {}).error().problem,
              PowerPinProblem::Unmeasured);
}
