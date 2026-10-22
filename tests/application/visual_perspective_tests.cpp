// VT3: the Perspective tool (card #273): the quad-to-tags maths, the tags it
// reads and writes, and its gestures, options and values. The render against
// libass is hikari_backends_visual_perspective_render_tests.

#include "hikari/application/automation_services.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/visual_perspective.h"
#include "hikari/application/visual_tools.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstring>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::visual;
using namespace hikari::application::visual::perspective;

namespace {

constexpr double kZ = 312.5;

double dist(PointF a, PointF b) { return std::hypot(a.x - b.x, a.y - b.y); }

void expectQuadNear(const Quad &a, const Quad &b, double tolerance)
{
    for (int i = 0; i < 4; ++i)
        EXPECT_LT(dist(a[static_cast<std::size_t>(i)], b[static_cast<std::size_t>(i)]), tolerance)
            << "corner " << i << ": " << a[static_cast<std::size_t>(i)].x << "," << a[static_cast<std::size_t>(i)].y
            << " vs " << b[static_cast<std::size_t>(i)].x << "," << b[static_cast<std::size_t>(i)].y;
}

Transform sample(double frx, double fry, double frz, double fax, int an, PointF pos, PointF org)
{
    Transform t;
    t.frx = frx;
    t.fry = fry;
    t.frz = frz;
    t.fax = fax;
    t.fscx = 120;
    t.fscy = 90;
    t.an = an;
    t.pos = pos;
    t.org = org;
    return t;
}

// ------------------------------------------------------------------ maths

TEST(PerspectiveMaths, ALevelBoxIsItsOwnQuad)
{
    Transform t;
    t.pos = {320, 300};
    t.org = t.pos;
    t.an = 2;
    const Box box{{0, 0}, {200, 40}};
    const Quad q = textToQuad(t, box, kZ);
    expectQuadNear(q, {PointF{220, 260}, PointF{420, 260}, PointF{420, 300}, PointF{220, 300}}, 1e-3);

    const auto back = quadToTransform(q, box, OrgMode::Center, t, kZ);
    ASSERT_TRUE(back);
    EXPECT_NEAR(back->frx, 0, 1e-6);
    EXPECT_NEAR(back->fry, 0, 1e-6);
    EXPECT_NEAR(back->frz, 0, 1e-6);
    EXPECT_NEAR(back->fax, 0, 1e-6);
    EXPECT_NEAR(back->fscx, 100, 1e-4);
    EXPECT_NEAR(back->fscy, 100, 1e-4);
    EXPECT_NEAR(back->pos.x, 320, 1e-3);
    EXPECT_NEAR(back->pos.y, 300, 1e-3);
    EXPECT_NEAR(back->org.x, 320, 1e-3); // the quad's centre
    EXPECT_NEAR(back->org.y, 280, 1e-3);
}

// Any quad the tags make, the tags it gives back render onto it again, in
// every \org mode and for every alignment.
TEST(PerspectiveMaths, TheTagsAQuadGivesRenderOntoIt)
{
    const Box box{{0, 0}, {180, 36}};
    const Transform cases[] = {
        sample(20, 30, 10, 0, 2, {320, 200}, {320, 200}),   sample(-35, 15, -20, 0.2, 7, {100, 80}, {200, 120}),
        sample(10, -40, 45, -0.3, 5, {400, 180}, {380, 160}), sample(0, 25, 0, 0, 3, {600, 340}, {500, 300}),
        sample(-15, 0, 170, 0.1, 9, {500, 60}, {320, 180}),  sample(30, 30, -60, 0, 1, {50, 330}, {60, 300})};
    for (const Transform &t : cases) {
        const Quad q = textToQuad(t, box, kZ);
        ASSERT_TRUE(convex(q));
        for (const OrgMode mode : {OrgMode::Center, OrgMode::NoFax, OrgMode::Keep}) {
            SCOPED_TRACE(static_cast<int>(mode));
            const auto back = quadToTransform(q, box, mode, t, kZ);
            ASSERT_TRUE(back);
            EXPECT_EQ(back->an, t.an);
            expectQuadNear(textToQuad(*back, box, kZ), q, 1e-2);
        }
        // Keep with the tags' own \org gives the tags back.
        const auto kept = quadToTransform(q, box, OrgMode::Keep, t, kZ);
        ASSERT_TRUE(kept);
        const auto turn = [](double a) { return std::remainder(a, 360.); };
        EXPECT_NEAR(turn(kept->frx - t.frx), 0, 1e-3);
        EXPECT_NEAR(turn(kept->fry - t.fry), 0, 1e-3);
        EXPECT_NEAR(turn(kept->frz - t.frz), 0, 1e-3);
        EXPECT_NEAR(kept->fax, t.fax, 1e-4);
        EXPECT_NEAR(kept->fscx, t.fscx, 1e-3);
        EXPECT_NEAR(kept->fscy, t.fscy, 1e-3);
        EXPECT_NEAR(kept->pos.x, t.pos.x, 1e-2);
        EXPECT_NEAR(kept->pos.y, t.pos.y, 1e-2);
    }
}

TEST(PerspectiveMaths, NoFaxFindsAnOrgWithoutShear)
{
    // A box turned without shear about an \org off its centre: Center needs
    // \fax for it, No \fax finds an \org that needs none.
    const Box box{{0, 0}, {200, 50}};
    const Transform t = sample(25, -30, 12, 0, 7, {200, 120}, {260, 210});
    const Quad q = textToQuad(t, box, kZ);
    const auto centre = quadToTransform(q, box, OrgMode::Center, t, kZ);
    const auto noFax = quadToTransform(q, box, OrgMode::NoFax, t, kZ);
    ASSERT_TRUE(centre && noFax);
    EXPECT_GT(std::abs(centre->fax), 1e-3);
    EXPECT_NEAR(noFax->fax, 0, 1e-4);
    expectQuadNear(textToQuad(*noFax, box, kZ), q, 1e-2);
}

TEST(PerspectiveMaths, TheBorderKeepsItsProportion)
{
    Transform t;
    t.pos = {320, 200};
    t.org = t.pos;
    t.bord = {2, 2};
    t.shad = {1, 3};
    const Box box{{0, 0}, {100, 20}};
    // The same box twice as wide and half as tall.
    Quad q = textToQuad(t, box, kZ);
    q[1].x += 100;
    q[2].x += 100;
    q[2].y -= 10;
    q[3].y -= 10;
    const auto back = quadToTransform(q, box, OrgMode::Center, t, kZ);
    ASSERT_TRUE(back);
    EXPECT_NEAR(back->fscx, 200, 1e-3);
    EXPECT_NEAR(back->fscy, 50, 1e-3);
    EXPECT_NEAR(back->bord.x, 4, 1e-4);
    EXPECT_NEAR(back->bord.y, 1, 1e-4);
    EXPECT_NEAR(back->shad.x, 2, 1e-4);
    EXPECT_NEAR(back->shad.y, 1.5, 1e-4);
}

TEST(PerspectiveMaths, QuadsAndThePlane)
{
    const Quad q{PointF{100, 100}, PointF{300, 120}, PointF{280, 260}, PointF{90, 240}};
    EXPECT_TRUE(convex(q));
    EXPECT_FALSE(convex(Quad{PointF{100, 100}, PointF{280, 260}, PointF{300, 120}, PointF{90, 240}})); // folded
    for (const PointF uv : {PointF{0, 0}, PointF{1, 0}, PointF{1, 1}, PointF{0, 1}, PointF{0.3f, 0.7f}}) {
        const PointF xy = fromUV(q, uv);
        const PointF back = toUV(q, xy);
        EXPECT_NEAR(back.x, uv.x, 1e-4);
        EXPECT_NEAR(back.y, uv.y, 1e-4);
    }
    expectQuadNear(subQuad(q, {0, 0}, {1, 1}), q, 1e-3);
    // The centre is the image of the plane's middle.
    EXPECT_LT(dist(centre(q), fromUV(q, {0.5f, 0.5f})), 1e-3);
    EXPECT_DOUBLE_EQ(screenZ(360, 360), 312.5);
    EXPECT_DOUBLE_EQ(screenZ(360, 720), 156.25);
    EXPECT_DOUBLE_EQ(screenZ(0, 0), 312.5);
}

// ------------------------------------------------------------------ tags

class FixedMeasure : public TextMeasurePort {
public:
    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        const double fs = std::strtod(style[2].c_str(), nullptr);
        lastScaleX = style[11];
        lastFont = style[1];
        lastBold = style[7];
        lastItalic = style[8];
        lastSpacing = style[13];
        const std::u16string u = core::toUtf16(std::u8string(text.begin(), text.end()));
        TextExtents e;
        e.width = static_cast<float>(static_cast<double>(u.size()) * fs * 0.5);
        e.height = static_cast<float>(fs);
        return e;
    }
    std::string lastScaleX, lastFont, lastBold, lastItalic, lastSpacing;
};

std::string scriptOf(const std::vector<std::string> &texts, const std::string &extraInfo = {},
                     const std::string &extraStyles = {})
{
    std::string s = "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n" + extraInfo +
                    "\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
                    "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,"
                    "2,10,10,10,1\n" + extraStyles +
                    "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (const std::string &t : texts)
        s += "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,," + t + "\n";
    return s;
}

class Host : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    std::vector<core::LineId> targets;
    std::int64_t time = 1500;
    mutable FixedMeasure measure;
    bool withMeasure = true;
    int changes = 0;

    explicit Host(const std::vector<std::string> &texts, const std::string &extraInfo = {},
                  const std::string &extraStyles = {})
    {
        v.setClient(640, 400, 40);
        v.setScript(640, 360);
        v.open({1280, 720, 0, 1});
        v.refreshToolTransform();
        script = scriptOf(texts, extraInfo, extraStyles);
        reopen();
    }
    // The script again in a new session (its LineIds start over).
    void reopen()
    {
        std::vector<std::byte> bytes(script.size());
        std::memcpy(bytes.data(), script.data(), script.size());
        s = std::make_unique<EditSession>(core::loadAss(bytes).document);
        const core::LineId id = s->document().lines().front()->id;
        s->setSelection({id, {id}, id, std::nullopt});
        targets = {id};
    }
    std::string script;
    core::LineId line(std::size_t i) const { return s->document().lines()[i]->id; }
    std::string text(std::size_t i = 0) const
    {
        const auto &t = s->document().lines()[i]->text;
        return std::string(t.begin(), t.end());
    }
    const VideoView &view() const override { return v; }
    const EditSession *session() const override { return s.get(); }
    std::optional<core::LineId> activeLine() const override { return s->selection().active; }
    std::vector<core::LineId> batchTargets() const override { return targets; }
    std::expected<Gesture *, CommandRefusal> beginGesture(std::vector<core::LineId> t, std::string history) override
    {
        if (g)
            return std::unexpected(CommandRefusal::Invalid);
        auto r = Gesture::begin(*s, std::move(t), std::move(history));
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
    std::pair<int, int> measureLabel(std::u16string_view) const override { return {0, 0}; }
    void toolChanged() override { ++changes; }
    std::int64_t videoTimeMs() const override { return time; }
    LegacyTimebase timebase() const override { return {}; }
    TextMeasurePort *textMeasure() const override { return withMeasure ? &measure : nullptr; }
};

core::LineRecord lineWith(const Host &host, std::size_t i = 0) { return *host.s->document().lines()[i]; }

TEST(PerspectiveTags, ReadsTheFirstBlockAndTheFirstPosition)
{
    Host host({"{\\an7\\frx10\\fry20\\fr30\\fax0.25\\fscx80\\fscy120\\bord3\\ybord5\\shad2}Hello{\\pos(9,9)\\frx99}",
               "{\\a6\\org(1,2)}Hi{\\pos(100,50)}", "{\\move(100,100,300,200,0,1000)}Moving"});
    const LineState a = readTags(host, lineWith(host, 0));
    EXPECT_EQ(a.transform.an, 7);
    EXPECT_EQ(a.transform.frx, 10);
    EXPECT_EQ(a.transform.fry, 20);
    EXPECT_EQ(a.transform.frz, 30); // \fr
    EXPECT_EQ(a.transform.fax, 0.25);
    EXPECT_EQ(a.transform.fscx, 80);
    EXPECT_EQ(a.transform.fscy, 120);
    EXPECT_EQ(a.transform.bord.x, 3);
    EXPECT_EQ(a.transform.bord.y, 5);
    EXPECT_EQ(a.transform.shad.x, 2);
    EXPECT_EQ(a.transform.pos.x, 9); // the first \pos, wherever it is
    EXPECT_EQ(a.transform.org.x, 9); // none: \pos

    const LineState b = readTags(host, lineWith(host, 1));
    EXPECT_EQ(b.transform.an, 8); // \a6: top centre
    EXPECT_EQ(b.transform.org.x, 1);
    EXPECT_EQ(b.transform.org.y, 2);
    EXPECT_EQ(b.transform.pos.y, 50);
    EXPECT_EQ(b.transform.bord.x, 2); // the Style's
    EXPECT_EQ(b.transform.shad.y, 1);

    // \move at the video's time: 500 ms into the Line, half way.
    const LineState c = readTags(host, lineWith(host, 2));
    EXPECT_TRUE(c.hasMove);
    EXPECT_EQ(c.transform.pos.x, 200);
    EXPECT_EQ(c.transform.pos.y, 150);
    EXPECT_EQ(c.moveTimes, u",0,1000");
}

TEST(PerspectiveTags, MeasuresTheTextAtAScaleOf100)
{
    Host host({"{\\fscx200\\fs20\\fnMono}Hello\\Nab", "{\\p1}m 10 20 l 110 20 110 70 10 70", "{\\p2}m 0 0 l 100 50"});
    const auto a = readLine(host, lineWith(host, 0));
    ASSERT_TRUE(a);
    EXPECT_EQ(host.measure.lastScaleX, "100");
    EXPECT_EQ(host.measure.lastFont, "Mono");
    EXPECT_FLOAT_EQ(a->box.width(), 5 * 10); // the widest line at \fs20
    EXPECT_FLOAT_EQ(a->box.height(), 40);    // two lines
    const auto b = readLine(host, lineWith(host, 1));
    ASSERT_TRUE(b);
    EXPECT_EQ(b->box.topLeft.x, 10);
    EXPECT_EQ(b->box.topLeft.y, 20);
    EXPECT_EQ(b->box.bottomRight.x, 110);
    EXPECT_EQ(b->box.bottomRight.y, 70);
    const auto c = readLine(host, lineWith(host, 2));
    ASSERT_TRUE(c);
    EXPECT_EQ(c->box.bottomRight.x, 50); // \p2: half
    host.withMeasure = false;
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_TRUE(readLine(host, lineWith(host, 1))); // a drawing needs no measure
}

TEST(PerspectiveTags, ABaselineOffsetMovesADrawingsBox)
{
    // libass: ascent height - \pbo and descent \pbo, neither below 0 on the
    // line; \r leaves \pbo, and \p2 halves it with the points.
    Host host({"{\\pbo20\\p1}m 0 0 l 100 0 100 40 0 40", "{\\pbo50\\r\\p1}m 0 0 l 100 0 100 40 0 40",
               "{\\pbo-10\\p1}m 0 0 l 100 0 100 40 0 40", "{\\pbo100\\p2}m 0 0 l 200 0 200 80 0 80"});
    const auto inside = readLine(host, lineWith(host, 0));
    ASSERT_TRUE(inside);
    EXPECT_EQ(inside->box.topLeft.y, 0);
    EXPECT_EQ(inside->box.layoutHeight(), 40);
    for (const int i : {1, 3}) {
        const auto below = readLine(host, lineWith(host, i));
        ASSERT_TRUE(below);
        EXPECT_EQ(below->box.topLeft.y, 10);
        EXPECT_EQ(below->box.bottomRight.y, 50);
        EXPECT_EQ(below->box.layoutHeight(), 50);
    }
    const auto above = readLine(host, lineWith(host, 2));
    ASSERT_TRUE(above);
    EXPECT_EQ(above->box.topLeft.y, 0);
    EXPECT_EQ(above->box.layoutHeight(), 50);
}

TEST(PerspectiveTags, WritesIntoTheFirstBlock)
{
    core::StyleValues style;
    style.scaleX = style.scaleY = u8"100";
    style.angle = u8"0";
    style.outlineWidth = u8"2";
    style.shadow = u8"1";
    Transform t;
    t.pos = {320.004f, 180.5f};
    t.org = {300, 170};
    t.frx = 12.34567;
    t.fry = 0;
    t.frz = -5;
    t.fax = 0.1234567;
    t.fscx = 100;
    t.fscy = 87.5;
    t.bord = {2, 3};
    t.shad = {1, 1};
    EXPECT_EQ(writeTransform(u"{\\blur2\\frx3\\pos(1,2)\\fr9\\bord5\\t(\\frz90)\\c&HFF&}Text", t, style),
              u"{\\blur2\\pos(320,180.5)\\org(300,170)\\fax0.123457\\fscy87.5\\frz-5\\frx12.3457\\ybord3"
              u"\\t(\\frz90)\\c&HFF&}Text");
    // No block: one goes in; the Style's values stay out.
    t.fax = 0;
    t.frx = 0;
    t.frz = 0;
    t.fscy = 100;
    t.bord = {2, 2};
    EXPECT_EQ(writeTransform(u"Text", t, style), u"{\\pos(320,180.5)\\org(300,170)}Text");
    // A \move keeps its motion: both points move by the change.
    EXPECT_EQ(writeTransform(u"{\\move(100,100,300,200,0,1000)}Text", t, style, PointF{200, 150}),
              u"{\\move(220,130.5,420,230.5,0,1000)\\org(300,170)}Text");
}

TEST(PerspectiveTags, ReadsAStyleReset)
{
    Host host({"{\\frx30\\fscx50\\r}Text", "{\\frx30\\rDefault\\fry5}Text", "{\\fax1}{\\frx30\\rNone}Text"});
    const LineState a = readTags(host, lineWith(host, 0));
    EXPECT_EQ(a.transform.frx, 0); // the Style's, as libass renders it
    EXPECT_EQ(a.transform.fscx, 100);
    const LineState b = readTags(host, lineWith(host, 1));
    EXPECT_EQ(b.transform.frx, 0);
    EXPECT_EQ(b.transform.fry, 5); // after the reset
    const LineState c = readTags(host, lineWith(host, 2));
    EXPECT_EQ(c.transform.frx, 0); // a Style it cannot find: the Line's
    EXPECT_EQ(c.transform.fax, 0); // across the leading blocks
}

TEST(PerspectiveTags, MeasuresASoftBreakAsASpace)
{
    Host host({"ab\\ncd"});
    const auto a = readLine(host, lineWith(host, 0));
    ASSERT_TRUE(a);
    EXPECT_FLOAT_EQ(a->box.width(), 5 * 20); // "ab cd" at 40
    EXPECT_FLOAT_EQ(a->box.height(), 40);
    Host wrap({"ab\\ncd"}, "WrapStyle: 2");
    const auto b = readLine(wrap, lineWith(wrap, 0));
    ASSERT_TRUE(b);
    EXPECT_FLOAT_EQ(b->box.width(), 2 * 20); // WrapStyle 2: a break
    EXPECT_FLOAT_EQ(b->box.height(), 80);
}

TEST(PerspectiveTags, WritesAcrossTheLeadingBlocks)
{
    core::StyleValues style;
    style.scaleX = style.scaleY = u8"100";
    style.angle = u8"0";
    style.outlineWidth = u8"2";
    style.shadow = u8"1";
    Transform t;
    t.pos = {320, 180.5f};
    t.org = {300, 170};
    t.frx = 5;
    t.fry = t.frz = t.fax = 0;
    t.fscx = t.fscy = 100;
    t.bord = {2, 2};
    t.shad = {1, 1};
    // A later leading block's tag would win: each loses it.
    EXPECT_EQ(writeTransform(u"{\\frx10}{\\frx20\\blur1}Text", t, style),
              u"{\\pos(320,180.5)\\org(300,170)\\frx5}{\\blur1}Text");
    EXPECT_EQ(writeTransform(u"{\\frx10}{\\frx20}Text", t, style), u"{\\pos(320,180.5)\\org(300,170)\\frx5}Text");
    // After a \r, every tag (the reset's base is another Style's).
    EXPECT_EQ(writeTransform(u"{\\frx30\\r\\blur1}Text", t, style),
              u"{\\r\\pos(320,180.5)\\org(300,170)\\fax0\\fscx100\\fscy100\\frz0\\frx5\\fry0\\xbord2\\ybord2\\xshad1"
              u"\\yshad1\\blur1}Text");
    EXPECT_EQ(writeTransform(u"{\\an7}{\\frx30\\rAlt}Text", t, style),
              u"{\\an7}{\\rAlt\\pos(320,180.5)\\org(300,170)\\fax0\\fscx100\\fscy100\\frz0\\frx5\\fry0\\xbord2"
              u"\\ybord2\\xshad1\\yshad1}Text");
    // The first \pos decides: a later \move stays as it is.
    EXPECT_EQ(writeTransform(u"{\\pos(10,20)}a{\\move(1,2,3,4)}", t, style, PointF{10, 20}),
              u"{\\pos(320,180.5)\\org(300,170)\\frx5}a{\\move(1,2,3,4)}");
}

TEST(PerspectiveTags, ReadsThePositionAsLibass)
{
    Host host({"{\\move(100,100,300,200,0,0)}Moving", "{\\a4}x", "{\\a8}x", "{\\a99\\an5}x",
               "{\\pos(1,2,3)\\org(1,2,3)}a{\\pos(5,6)\\org(7,8)}", "{\\pos(a,7)}x"});
    // Neither time above 0: over the Line's span, 500 ms of 2000 in.
    const LineState a = readTags(host, lineWith(host, 0));
    EXPECT_EQ(a.transform.pos.x, 150);
    EXPECT_EQ(a.transform.pos.y, 125);
    EXPECT_EQ(readTags(host, lineWith(host, 1)).transform.an, 7); // VSFilter's \a4 is \a5
    EXPECT_EQ(readTags(host, lineWith(host, 2)).transform.an, 7);
    EXPECT_EQ(readTags(host, lineWith(host, 3)).transform.an, 2); // the first decides: the Style's
    // Too many values: skipped.
    const LineState e = readTags(host, lineWith(host, 4));
    EXPECT_EQ(e.transform.pos.x, 5);
    EXPECT_EQ(e.transform.org.x, 7);
    EXPECT_EQ(readTags(host, lineWith(host, 5)).transform.pos.x, 0); // a word is 0
}

TEST(PerspectiveTags, MeasuresTheTextAsLibassShowsIt)
{
    Host host({"{\\q2}ab\\ncd", "Hello \\{w\\}", "{\\fs+10}ab", "{\\fs0}ab", "{\\fs-5}ab", "a{b",
               "{\\p1}m 0 0 l 10 0 10 10{\\c&HFF&}l 100 100", "{\\p1}m 0 0 l 10 0 10 10{\\p0}",
               "ab{\\p1}m 0 0 l 1 1{\\p0}"});
    const auto box = [&host](std::size_t i) { return readLine(host, lineWith(host, i)); };
    ASSERT_TRUE(box(0));
    EXPECT_FLOAT_EQ(box(0)->box.height(), 80); // \q2: \n breaks
    ASSERT_TRUE(box(1));
    EXPECT_FLOAT_EQ(box(1)->box.width(), 9 * 20); // "Hello {w}"
    EXPECT_FLOAT_EQ(box(2)->box.width(), 2 * 40); // \fs+10: twice 40
    EXPECT_FLOAT_EQ(box(3)->box.width(), 2 * 20); // \fs0: the Style's
    EXPECT_FLOAT_EQ(box(4)->box.width(), 2 * 10); // \fs-5: half
    EXPECT_FLOAT_EQ(box(5)->box.width(), 3 * 20); // a { with no } is text
    // A drawing with more shown after it, or within text: not measured.
    EXPECT_FALSE(box(6));
    ASSERT_TRUE(box(7));
    EXPECT_EQ(box(7)->box.bottomRight.x, 10);
    EXPECT_FALSE(box(8));
    Host wrap({"{\\q0}ab\\ncd"}, "WrapStyle: 2");
    EXPECT_FLOAT_EQ(readLine(wrap, lineWith(wrap, 0))->box.height(), 40); // \q0: a space
}

TEST(PerspectiveTags, KeepsTheBlocksComments)
{
    core::StyleValues style;
    style.scaleX = style.scaleY = u8"100";
    style.angle = u8"0";
    style.outlineWidth = u8"2";
    style.shadow = u8"1";
    Transform t;
    t.pos = {320, 180.5f};
    t.org = {300, 170};
    t.frx = 5;
    t.fry = t.frz = t.fax = 0;
    t.fscx = t.fscy = 100;
    t.bord = {2, 2};
    t.shad = {1, 1};
    EXPECT_EQ(writeTransform(u"{\\frx10 note\\blur1\\pos(1,2) here}Text", t, style),
              u"{ note here\\blur1\\pos(320,180.5)\\org(300,170)\\frx5}Text");
    EXPECT_EQ(writeTransform(u"{\\move(1,2,3,4,0,0) m}x", t, style, PointF{310, 160.5f}),
              u"{\\move(11,22,13,24,0,0) m\\org(300,170)\\frx5}x");
    // \pos(1,2,3) is no position: the \pos after it is.
    EXPECT_EQ(writeTransform(u"{\\an7}a{\\pos(1,2,3)\\pos(5,6)}", t, style),
              u"{\\an7\\pos(320,180.5)\\org(300,170)\\frx5}a{\\pos(1,2,3)\\pos(5,6)}");
}

TEST(PerspectiveTags, AHostileLineTakesLinearTime)
{
    const std::string braces(400000, '{');
    std::string escapes;
    for (int i = 0; i < 100000; ++i)
        escapes += "\\h";
    std::string blocks;
    for (int i = 0; i < 100000; ++i)
        blocks += "{\\frx1}";
    Host host({braces, escapes, blocks + "x"}, "WrapStyle: 2"); // unwrapped: measured
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < 3; ++i) {
        const auto state = readLine(host, lineWith(host, i));
        ASSERT_TRUE(state) << i;
        const std::u16string text = core::toUtf16(lineWith(host, i).text);
        EXPECT_FALSE(writeTransform(text, state->transform, core::StyleValues{}).empty());
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    EXPECT_LT(ms, 5000); // quadratic, each took minutes
}

TEST(PerspectiveTags, ManyResetsAndStylesTakeLinearTime)
{
    // Every \r looked up among every Style was their product.
    std::string styles;
    for (int i = 0; i < 20000; ++i)
        styles += "Style: S" + std::to_string(i) +
                  ",Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n";
    std::string resets = "{";
    for (int i = 0; i < 20000; ++i)
        resets += "\\rNone";
    Host host({resets + "\\frx5}x"}, {}, styles);
    const auto started = std::chrono::steady_clock::now();
    const auto state = readLine(host, lineWith(host, 0));
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    ASSERT_TRUE(state);
    EXPECT_EQ(state->transform.frx, 5); // after the last reset
    EXPECT_LT(ms, 2000);
}

TEST(PerspectiveTags, AGlyphChangeAfterTheTextIsNotMeasured)
{
    Host host({"A{\\fs80}B", "A{\\c&HFF&}B", "\\N{\\fscx50}B", "A{\\rDefault}B", "{\\fs80}A"});
    const auto box = [&host](std::size_t i) { return readLine(host, lineWith(host, i)); };
    EXPECT_FALSE(box(0)); // libass sizes B at 80
    ASSERT_TRUE(box(1));  // a colour changes no glyph
    EXPECT_FLOAT_EQ(box(1)->box.width(), 2 * 20);
    EXPECT_FALSE(box(2));
    EXPECT_FALSE(box(3));
    ASSERT_TRUE(box(4)); // before the text: the run's
    EXPECT_FLOAT_EQ(box(4)->box.width(), 40);
}

TEST(PerspectiveTags, ALineLibassWrapsIsNotMeasured)
{
    const std::string wide(40, 'a'); // 800 wide, the room 620
    Host host({wide, "{\\q2}" + wide, "{\\fscx50}" + wide});
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_TRUE(readLine(host, lineWith(host, 1))); // \q2: no wrapping
    EXPECT_TRUE(readLine(host, lineWith(host, 2))); // 400 at half the width
    Host unwrapped({wide}, "WrapStyle: 2");
    EXPECT_TRUE(readLine(unwrapped, lineWith(unwrapped, 0)));
}

TEST(PerspectiveTags, MeasuresWithTheLeadingSpacingWeightAndSlant)
{
    // One Line a Host: the stacking measures the Lines shown with it too.
    Host host({"{\\fsp20\\b1\\i1}AB"});
    ASSERT_TRUE(readLine(host, lineWith(host, 0)));
    EXPECT_EQ(host.measure.lastSpacing, "20");
    EXPECT_NE(host.measure.lastBold, "0");
    EXPECT_NE(host.measure.lastItalic, "0");
    Host reset({"{\\b1\\b\\i1\\i}AB"});
    ASSERT_TRUE(readLine(reset, lineWith(reset, 0)));
    EXPECT_EQ(reset.measure.lastBold, "0"); // without a value: the Style's
    EXPECT_EQ(reset.measure.lastItalic, "0");
}

TEST(PerspectiveTags, AnAnimatedQuadIsNotMeasured)
{
    Host host({"{\\t(0,1000,\\frx60)}Text", "{\\t(0,1000,\\fs80)}Text", "Te{\\t(\\fscx50)}xt",
               "{\\t(0,1000,\\c&HFF&)}Text", "{\\p1\\t(\\frz30)}m 0 0 l 10 0 10 10"});
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_FALSE(readLine(host, lineWith(host, 1)));
    EXPECT_FALSE(readLine(host, lineWith(host, 2)));
    EXPECT_TRUE(readLine(host, lineWith(host, 3))); // a colour moves nothing
    EXPECT_FALSE(readLine(host, lineWith(host, 4)));
}

TEST(PerspectiveTags, AValuelessTagResets)
{
    Host host({"{\\frx30\\frx\\fry20\\fry\\fax0.2\\fax}T", "{\\fscx50\\fscx\\frz20\\frz\\bord5\\bord\\shad3\\shad}T"});
    const LineState a = readTags(host, lineWith(host, 0));
    EXPECT_EQ(a.transform.frx, 0);
    EXPECT_EQ(a.transform.fry, 0);
    EXPECT_EQ(a.transform.fax, 0);
    const LineState b = readTags(host, lineWith(host, 1));
    EXPECT_EQ(b.transform.fscx, 100); // the Style's
    EXPECT_EQ(b.transform.frz, 0);
    EXPECT_EQ(b.transform.bord.x, 2);
    EXPECT_EQ(b.transform.shad.y, 1);
}

// ------------------------------------------------------------------ the tool

struct Fixture {
    Host host;
    PerspectiveTool tool;
    explicit Fixture(const std::vector<std::string> &texts = {"{\\pos(320,200)}Perspective"})
        : host(texts)
    {
        tool.selected(host);
        tool.reset(host);
    }
    PointF dev(PointF script) const { return host.v.scriptToView(script); }
    void send(Pointer::Kind kind, PointF script, bool shift = false, bool ctrl = false, bool alt = false)
    {
        const PointF d = dev(script);
        Pointer p{kind, static_cast<int>(std::lround(d.x)), static_cast<int>(std::lround(d.y))};
        p.button = (kind == Pointer::Kind::Move) ? Pointer::Button::None : Pointer::Button::Left;
        p.leftDown = kind == Pointer::Kind::Press || kind == Pointer::Kind::Move;
        p.shift = shift;
        p.control = ctrl;
        p.alt = alt;
        tool.pointer(p, host);
    }
    void drag(PointF from, PointF to, bool shift = false, bool ctrl = false, bool alt = false)
    {
        send(Pointer::Kind::Press, from);
        send(Pointer::Kind::Move, {(from.x + to.x) / 2, (from.y + to.y) / 2}, shift, ctrl, alt);
        send(Pointer::Kind::Move, to, shift, ctrl, alt);
        send(Pointer::Kind::Release, to);
        tool.reset(host); // the host does on the new revision
    }
    std::size_t steps() const { return host.s->historySize(); }
};

TEST(PerspectiveTool, DrawsTheTextBoxAsTheInnerQuad)
{
    Fixture f;
    ASSERT_TRUE(f.tool.hasQuad());
    // "Perspective" at 40: 11 * 20 wide, 40 high, \an2 at (320, 200).
    expectQuadNear(f.tool.inner(), {PointF{210, 160}, PointF{430, 160}, PointF{430, 200}, PointF{210, 200}}, 1e-3);
    const Overlay o = f.tool.overlay(f.host);
    EXPECT_FALSE(o.lines.empty());
    EXPECT_FALSE(o.polygons.empty()); // the corners' handles
}

TEST(PerspectiveTool, ACornerDragIsOneStepAndTheQuadFollows)
{
    Fixture f;
    const std::size_t before = f.steps();
    f.drag({430, 200}, {450, 230});
    EXPECT_EQ(f.steps(), before + 1);
    EXPECT_EQ(f.host.s->history().back().name, "Visual perspective tool");
    const std::string text = f.host.text();
    EXPECT_NE(text.find("\\org("), std::string::npos) << text;
    EXPECT_NE(text.find("\\frx"), std::string::npos) << text;
    EXPECT_NE(text.find("\\fry"), std::string::npos) << text;
    // Read again, the Line's tags draw the dragged quad.
    EXPECT_LT(dist(f.tool.inner()[2], {450, 230}), 0.05);
    EXPECT_LT(dist(f.tool.inner()[0], {210, 160}), 0.05);
    EXPECT_LT(dist(f.tool.inner()[1], {430, 160}), 0.05);
}

TEST(PerspectiveTool, AClickRecordsNothing)
{
    Fixture f;
    const std::size_t before = f.steps();
    const std::string text = f.host.text();
    f.send(Pointer::Kind::Press, {430, 200});
    f.send(Pointer::Kind::Release, {430, 200});
    EXPECT_EQ(f.steps(), before);
    EXPECT_EQ(f.host.text(), text);
    EXPECT_FALSE(f.host.gesture());
}

TEST(PerspectiveTool, EscDropsTheDrag)
{
    Fixture f;
    const std::string text = f.host.text();
    f.send(Pointer::Kind::Press, {430, 200});
    f.send(Pointer::Kind::Move, {460, 240});
    ASSERT_TRUE(f.host.gesture());
    ASSERT_TRUE(f.host.gesture()->changesAnyLine());
    f.host.cancelGesture(); // the controller's escape
    f.tool.reset(f.host);
    EXPECT_EQ(f.host.text(), text);
    EXPECT_LT(dist(f.tool.inner()[2], {430, 200}), 1e-3);
}

TEST(PerspectiveTool, ShiftLocksTheCornerToAnEdge)
{
    Fixture f;
    // Mostly along the bottom edge: the corner stays on it.
    f.drag({430, 200}, {470, 207}, true);
    EXPECT_NEAR(f.tool.inner()[2].y, 200, 0.05);
    EXPECT_NEAR(f.tool.inner()[2].x, 470, 0.05);
}

TEST(PerspectiveTool, CtrlSnapsToAnotherHandle)
{
    Fixture f;
    // Within ten units of the top right corner's x: it takes it.
    f.drag({430, 200}, {437, 226}, false, true);
    EXPECT_NEAR(f.tool.inner()[2].x, 430, 0.05);
    EXPECT_NEAR(f.tool.inner()[2].y, 226, 0.05);
}

TEST(PerspectiveTool, AltScalesFromTheCentre)
{
    Fixture f;
    const PointF middle = centre(f.tool.inner());
    f.drag({430, 200}, {450, 210}, false, false, true);
    EXPECT_LT(dist(centre(f.tool.inner()), middle), 0.05);
    EXPECT_LT(dist(f.tool.inner()[0], {190, 150}), 0.05); // the opposite corner mirrored
}

TEST(PerspectiveTool, ADragInsideMovesTheQuad)
{
    Fixture f;
    f.drag({320, 180}, {340, 170});
    expectQuadNear(f.tool.inner(), {PointF{230, 150}, PointF{450, 150}, PointF{450, 190}, PointF{230, 190}}, 0.05);
    EXPECT_NE(f.host.text().find("\\pos(340,190)"), std::string::npos) << f.host.text();
}

TEST(PerspectiveTool, ADoubleClickPutsTheNearestCornerThere)
{
    Fixture f;
    const std::size_t before = f.steps();
    f.send(Pointer::Kind::Press, {500, 260});
    f.send(Pointer::Kind::Release, {500, 260});
    f.send(Pointer::Kind::Press, {500, 260});
    f.send(Pointer::Kind::DoubleClick, {500, 260});
    f.send(Pointer::Kind::Release, {500, 260});
    f.tool.reset(f.host);
    EXPECT_EQ(f.steps(), before + 1);
    EXPECT_LT(dist(f.tool.inner()[2], {500, 260}), 0.05);
}

TEST(PerspectiveTool, AFoldedQuadIsNotTaken)
{
    Fixture f;
    f.send(Pointer::Kind::Press, {430, 200});
    f.send(Pointer::Kind::Move, {440, 210});
    const Quad last = f.tool.inner();
    f.send(Pointer::Kind::Move, {150, 120}); // past the opposite corner
    expectQuadNear(f.tool.inner(), last, 1e-3);
    f.send(Pointer::Kind::Release, {150, 120});
    f.tool.reset(f.host);
    EXPECT_LT(dist(f.tool.inner()[2], {440, 210}), 0.05);
}

TEST(PerspectiveTool, EveryPickedLineGetsTheTags)
{
    Fixture f({"{\\pos(320,200)}Perspective", "{\\pos(320,200)\\bord4}Backing", "{\\move(0,0,100,0)}Moving"});
    f.host.targets = {f.host.line(0), f.host.line(1), f.host.line(2)};
    f.drag({430, 200}, {450, 230});
    const auto tagOf = [](const std::string &text, const std::string &tag) {
        const auto at = text.find("\\" + tag);
        return at == std::string::npos ? std::string() : text.substr(at, text.find('\\', at + 1) - at);
    };
    const std::string a = f.host.text(0), b = f.host.text(1), c = f.host.text(2);
    for (const char *tag : {"org", "frx", "fry", "frz", "fax", "fscx", "fscy"}) {
        EXPECT_EQ(tagOf(a, tag), tagOf(b, tag)) << tag;
        EXPECT_EQ(tagOf(a, tag), tagOf(c, tag)) << tag;
    }
    EXPECT_EQ(tagOf(a, "pos"), tagOf(b, "pos"));
    // Each keeps its own outline (scaled as the active Line's) and motion.
    EXPECT_NE(tagOf(a, "xbord"), tagOf(b, "xbord"));
    EXPECT_EQ(c.find("\\pos"), std::string::npos) << c;
    EXPECT_NE(c.find("\\move("), std::string::npos) << c;
    EXPECT_EQ(f.host.s->history().back().name, "Visual perspective tool");
}

TEST(PerspectiveTool, ThePlaneHoldsThePerspective)
{
    Fixture f;
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    const Quad outer = f.tool.outer();
    // The outer quad is twice the inner one about its centre.
    EXPECT_LT(dist(outer[0], {100, 140}), 0.05);
    EXPECT_LT(dist(outer[2], {540, 220}), 0.05);
    // An inner corner moves within the plane: the outer quad stays.
    f.drag({430, 200}, {450, 210});
    expectQuadNear(f.tool.outer(), outer, 0.1);
    EXPECT_NEAR(f.tool.transform().frx, 0, 0.01);
    EXPECT_NEAR(f.tool.transform().fry, 0, 0.01);
    // An outer corner sets the perspective and the inner quad follows.
    f.drag({540, 220}, {520, 250});
    EXPECT_GT(std::abs(f.tool.transform().frx) + std::abs(f.tool.transform().fry), 1);
    EXPECT_LT(dist(f.tool.outer()[2], {520, 250}), 0.1);
}

TEST(PerspectiveTool, LockedTheOuterQuadIsResizedInstead)
{
    Fixture f;
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    ASSERT_TRUE(f.tool.setOption("lockOuter", 1, f.host));
    const Quad inner = f.tool.inner();
    // An outer corner: the inner quad stays.
    f.drag({540, 220}, {580, 240});
    expectQuadNear(f.tool.inner(), inner, 0.1);
    EXPECT_LT(dist(f.tool.outer()[2], {580, 240}), 0.1);
    // An inner corner sets the perspective; the outer quad follows it.
    f.drag({430, 200}, {440, 220});
    EXPECT_GT(std::abs(f.tool.transform().frx) + std::abs(f.tool.transform().fry), 1);
}

TEST(PerspectiveTool, ThePlaneIsKeptPerLine)
{
    Fixture f({"{\\pos(320,200)}Perspective", "{\\pos(320,100)}Other"});
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    // The inner quad moved within the plane: the plane's rectangle changes.
    f.drag({430, 200}, {450, 210});
    const Quad outer = f.tool.outer();
    f.host.s->setSelection({f.host.line(1), {f.host.line(1)}, f.host.line(1), std::nullopt});
    f.host.targets = {f.host.line(1)};
    f.tool.reset(f.host);
    f.host.s->setSelection({f.host.line(0), {f.host.line(0)}, f.host.line(0), std::nullopt});
    f.host.targets = {f.host.line(0)};
    f.tool.reset(f.host);
    ASSERT_TRUE(f.tool.planeShown());
    expectQuadNear(f.tool.outer(), outer, 0.1);
    // The plane the drag kept, not the one a Line not seen before starts
    // from around the wider inner quad (its corner at 570, 235).
    EXPECT_LT(dist(f.tool.outer()[2], {540, 220}), 0.1);
}

TEST(PerspectiveTool, AHiddenPlaneStaysWhereItWas)
{
    Fixture f;
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    const Quad outer = f.tool.outer();
    ASSERT_TRUE(f.tool.setOption("plane", 0, f.host));
    // The inner quad moves while the plane is hidden: shown again, the
    // plane is where it was, the inner quad elsewhere in it.
    f.drag({320, 180}, {340, 170});
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    expectQuadNear(f.tool.outer(), outer, 0.05);
    EXPECT_LT(dist(f.tool.inner()[0], {230, 150}), 0.05);
}

TEST(PerspectiveTool, ALockedResizeIsKept)
{
    Fixture f({"{\\pos(320,200)}Perspective", "{\\pos(320,100)}Other"});
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    f.drag({430, 200}, {440, 205}); // the Line takes the tool's tags
    ASSERT_TRUE(f.tool.setOption("lockOuter", 1, f.host));
    const PointF corner = f.tool.outer()[2];
    const std::size_t before = f.steps();
    f.drag(corner, {corner.x + 30, corner.y + 20});
    EXPECT_EQ(f.steps(), before); // no Line changes
    const Quad resized = f.tool.outer();
    EXPECT_LT(dist(resized[2], {corner.x + 30, corner.y + 20}), 0.1);
    f.host.s->setSelection({f.host.line(1), {f.host.line(1)}, f.host.line(1), std::nullopt});
    f.tool.reset(f.host);
    f.host.s->setSelection({f.host.line(0), {f.host.line(0)}, f.host.line(0), std::nullopt});
    f.tool.reset(f.host);
    expectQuadNear(f.tool.outer(), resized, 0.1);
}

TEST(PerspectiveTool, ThePlanesAreTheSessions)
{
    Fixture f;
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    f.drag({540, 220}, {520, 250}); // an outer corner: the plane changes
    ASSERT_GT(dist(f.tool.outer()[2], {540, 220}), 1);
    // Another session, its first Line with the same LineId: its own plane.
    f.host.reopen();
    f.tool.reset(f.host);
    EXPECT_LT(dist(f.tool.outer()[0], {100, 140}), 0.05);
    EXPECT_LT(dist(f.tool.outer()[2], {540, 220}), 0.05);
}

TEST(PerspectiveTool, AnUnpositionedLineIsWhereItIsStacked)
{
    Fixture f({"First", "Second"});
    const float first = f.tool.inner()[2].y;
    f.host.s->setSelection({f.host.line(1), {f.host.line(1)}, f.host.line(1), std::nullopt});
    f.host.targets = {f.host.line(1)};
    f.tool.reset(f.host);
    // Shown with the first, the second goes above it, as the other tools
    // place it.
    EXPECT_LT(f.tool.inner()[2].y, first - 1);
}

TEST(PerspectiveTool, AnOriginKeepsALineOutOfTheStack)
{
    // libass places a Line with \org without collisions, and its place
    // takes none from the others.
    Fixture f({"{\\org(1,1)}First", "Second"});
    const float nominal = f.tool.inner()[2].y;
    f.host.s->setSelection({f.host.line(1), {f.host.line(1)}, f.host.line(1), std::nullopt});
    f.host.targets = {f.host.line(1)};
    f.tool.reset(f.host);
    EXPECT_NEAR(f.tool.inner()[2].y, nominal, 0.05);
}

TEST(PerspectiveTool, AValueEditDropsTheShearItDoesNotWrite)
{
    Fixture f({"{\\pos(320,200)\\fay0.1}Perspective"});
    EXPECT_TRUE(f.tool.setValue("frx", u"5", f.host, ValueEdit::Sample));
    const Quad shown = f.tool.inner();
    EXPECT_TRUE(f.tool.setValue("frx", u"5", f.host, ValueEdit::Commit));
    f.tool.reset(f.host);
    EXPECT_EQ(f.host.text().find("\\fay"), std::string::npos) << f.host.text();
    expectQuadNear(f.tool.inner(), shown, 0.05); // the Line renders what was shown
}

TEST(PerspectiveTool, AShownPlaneStaysAsTheQuadMoves)
{
    Fixture f({"{\\move(100,100,300,200,0,1000)}Moving"});
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    const Quad plane = f.tool.outer();
    const PointF was = centre(f.tool.inner());
    f.host.time = 2000;
    (void)f.tool.overlay(f.host);
    EXPECT_GT(dist(centre(f.tool.inner()), was), 50); // the quad followed
    expectQuadNear(f.tool.outer(), plane, 0.05);
}

TEST(PerspectiveTool, AnEditOfTheLineKeepsThePlane)
{
    Fixture f;
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    const Quad plane = f.tool.outer();
    // The Line editor (or an Undo) changes the text: a wider quad, the
    // same plane.
    auto g = f.host.beginGesture(f.host.targets, "Edit");
    ASSERT_TRUE(g);
    (*g)->stage(f.host.line(0), u8"{\\pos(320,200)}Perspective!!", false);
    ASSERT_TRUE(f.host.commitGesture());
    f.tool.reset(f.host);
    EXPECT_GT(f.tool.inner()[2].x, 431); // it grew
    expectQuadNear(f.tool.outer(), plane, 0.05);
}

TEST(PerspectiveTool, KeepLetsTheOriginBeDragged)
{
    Fixture f;
    ASSERT_TRUE(f.tool.setOption("orgMode", 2, f.host));
    f.drag({430, 200}, {450, 230}); // some perspective first
    const Quad quad = f.tool.inner();
    const PointF org = f.tool.transform().org;
    f.drag(org, {org.x + 30, org.y - 20});
    EXPECT_NEAR(f.tool.transform().org.x, org.x + 30, 0.05);
    EXPECT_NEAR(f.tool.transform().org.y, org.y - 20, 0.05);
    expectQuadNear(f.tool.inner(), quad, 0.1); // the quad stays
}

TEST(PerspectiveTool, TheValuesAreTheTags)
{
    Fixture f;
    const auto values = f.tool.values(f.host);
    ASSERT_EQ(values.size(), 6u);
    EXPECT_EQ(values[0].name, "orgx");
    EXPECT_FALSE(values[0].editable); // Center computes it
    EXPECT_EQ(values[2].group, u"Rotation");
    EXPECT_EQ(values[5].label, u"Shear");
    // A typed angle is one step and the quad follows it.
    const std::size_t before = f.steps();
    EXPECT_TRUE(f.tool.setValue("fry", u"30", f.host));
    f.tool.reset(f.host);
    EXPECT_EQ(f.steps(), before + 1);
    EXPECT_NE(f.host.text().find("\\fry30"), std::string::npos) << f.host.text();
    EXPECT_NEAR(f.tool.transform().fry, 30, 1e-6);
    EXPECT_FALSE(f.tool.setValue("orgx", u"10", f.host)); // not in Center
    // A scrub stages, then commits once.
    EXPECT_TRUE(f.tool.setValue("frx", u"5", f.host, ValueEdit::Sample));
    EXPECT_TRUE(f.tool.setValue("frx", u"8", f.host, ValueEdit::Sample));
    EXPECT_TRUE(f.tool.setValue("frx", u"10", f.host, ValueEdit::Commit));
    f.tool.reset(f.host);
    EXPECT_EQ(f.steps(), before + 2);
    EXPECT_NE(f.host.text().find("\\frx10"), std::string::npos) << f.host.text();
}

TEST(PerspectiveTool, ARefusedValueLeavesNoGesture)
{
    Fixture f;
    const std::string text = f.host.text();
    // Edge on, the quad folds: refused, and the gesture it opened goes.
    EXPECT_FALSE(f.tool.setValue("fry", u"90", f.host));
    EXPECT_FALSE(f.host.gesture());
    EXPECT_FALSE(f.tool.setValue("fry", u"90", f.host, ValueEdit::Sample));
    EXPECT_FALSE(f.host.gesture());
    EXPECT_EQ(f.host.text(), text);
    // So the next one is taken.
    EXPECT_TRUE(f.tool.setValue("fry", u"30", f.host));
    // A scrub's later refusal keeps what it took (the controller's to drop).
    EXPECT_TRUE(f.tool.setValue("frx", u"5", f.host, ValueEdit::Sample));
    EXPECT_FALSE(f.tool.setValue("frx", u"90", f.host, ValueEdit::Sample));
    EXPECT_TRUE(f.host.gesture());
}

TEST(PerspectiveTool, AMovingQuadFollowsTheVideo)
{
    Fixture f({"{\\move(100,100,300,200,0,1000)}Moving"});
    // 500 ms in: half way.
    EXPECT_LT(dist(centre(f.tool.inner()), {200, 130}), 0.05);
    // 1000 ms in: the end, though only the overlay is asked again.
    f.host.time = 2000;
    (void)f.tool.overlay(f.host);
    EXPECT_LT(dist(centre(f.tool.inner()), {300, 180}), 0.05);
    // A drag moves the motion by its own change, from there.
    f.drag({300, 180}, {310, 180});
    EXPECT_NE(f.host.text().find("\\move(110,100,310,200,0,1000)"), std::string::npos) << f.host.text();
}

TEST(PerspectiveTool, TheArrowsNudgeTheLastCorner)
{
    Fixture f;
    f.drag({430, 200}, {440, 205}); // grabs the bottom right corner
    EXPECT_EQ(f.tool.selectedCorner(), 2);
    const std::size_t before = f.steps();
    const Key right{keys::Right};
    EXPECT_TRUE(f.tool.takesKey(right));
    EXPECT_TRUE(f.tool.key(right, f.host));
    EXPECT_TRUE(f.tool.key({keys::Right, false, true}, f.host)); // auto-repeat
    Key shiftDown{keys::Down};
    shiftDown.shift = true;
    EXPECT_TRUE(f.tool.key(shiftDown, f.host));
    EXPECT_TRUE(f.tool.key({keys::Right, true}, f.host)); // the release commits
    f.tool.reset(f.host);
    EXPECT_EQ(f.steps(), before + 1);
    EXPECT_NEAR(f.tool.inner()[2].x, 442, 0.05);
    EXPECT_NEAR(f.tool.inner()[2].y, 205.1, 0.05);
    Key ctrl{keys::Right};
    ctrl.control = true;
    EXPECT_FALSE(f.tool.takesKey(ctrl)); // Ctrl+arrows stay the Global bindings'
}

TEST(PerspectiveTool, TheOptions)
{
    Fixture f;
    const auto options = f.tool.options(f.host);
    ASSERT_EQ(options.size(), 4u);
    EXPECT_EQ(options[0].iconRole, "perspective-plane");
    EXPECT_EQ(options[1].iconRole, "perspective-lock-outer");
    EXPECT_FALSE(options[1].enabled); // no plane shown
    EXPECT_EQ(options[2].iconRole, "perspective-grid");
    EXPECT_EQ(options[3].kind, ToolOption::Kind::Choice);
    EXPECT_EQ(options[3].group, u"Origin");
    EXPECT_EQ(options[3].choices.size(), 3u);
    EXPECT_FALSE(f.tool.setOption("lockOuter", 1, f.host));
    EXPECT_TRUE(f.tool.setOption("grid", 1, f.host));
    EXPECT_TRUE(f.tool.gridShown());
    EXPECT_TRUE(f.tool.setOption("orgMode", 1, f.host));
    EXPECT_EQ(f.tool.orgMode(), OrgMode::NoFax);
    // The toolbar's state stays when the family is chosen again.
    f.tool.selected(f.host);
    EXPECT_TRUE(f.tool.gridShown());
    EXPECT_EQ(f.tool.orgMode(), OrgMode::NoFax);
}

TEST(PerspectiveTool, IsTheTwelfthFamily)
{
    EXPECT_EQ(familyInfo(Family::Perspective).tooltip, "Perspective");
    EXPECT_EQ(familyInfo(Family::Perspective).history, "Visual perspective tool");
    EXPECT_EQ(familyInfo(Family::Perspective).hotkey, "VIDEO_TOOL_PERSPECTIVE");
    EXPECT_NE(dynamic_cast<PerspectiveTool *>(makeVisualTool(Family::Perspective).get()), nullptr);
    // The rail shows it after Text X / Y rotation.
    const auto &order = railOrder();
    EXPECT_EQ(order[5], Family::RotationXY);
    EXPECT_EQ(order[6], Family::Perspective);
    EXPECT_EQ(order[7], Family::RectangleClip);
}

} // namespace
