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
#include <set>

using namespace hikari;
using namespace hikari::application;
using namespace hikari::application::visual;
using namespace hikari::application::visual::perspective;

namespace {

constexpr double kZ = 312.5;

double dist(PointF a, PointF b) { return std::hypot(a.x - b.x, a.y - b.y); }
PointF add(PointF a, PointF b) { return {a.x + b.x, a.y + b.y}; }

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
    EXPECT_NEAR(back->org.x, 320, 1e-3);
    EXPECT_NEAR(back->org.y, 280, 1e-3);
}

TEST(PerspectiveMaths, ABoxUnderAUnitKeepsItsScale)
{
    Transform t;
    t.pos = {320, 200};
    t.org = t.pos;
    t.an = 7;
    t.fscx = t.fscy = 10000;
    const Box box{{0, 0}, {0.5f, 0.5f}};
    const Quad q = textToQuad(t, box, kZ);
    const auto back = quadToTransform(q, box, OrgMode::Keep, t, kZ);
    ASSERT_TRUE(back);
    EXPECT_NEAR(back->fscx, 10000, 1e-2);
    EXPECT_NEAR(back->fscy, 10000, 1e-2);
    expectQuadNear(textToQuad(*back, box, kZ), q, 1e-3);
    EXPECT_FALSE(quadToTransform(q, Box{{0, 0}, {0, 0.5f}}, OrgMode::Keep, t, kZ));
}

TEST(PerspectiveMaths, ABoxUnderAUnitIsAnchoredByItsOwnSize)
{
    for (const int an : {2, 3, 5, 9}) {
        Transform t;
        t.pos = {300, 200};
        t.org = t.pos;
        t.an = an;
        t.fscx = t.fscy = 10000;
        const Box box{{0, 0}, {0.5f, 0.5f}};
        Quad moved = textToQuad(t, box, kZ);
        for (PointF &p : moved)
            p.x += 10;
        const auto back = quadToTransform(moved, box, OrgMode::Keep, t, kZ);
        ASSERT_TRUE(back) << an;
        expectQuadNear(textToQuad(*back, box, kZ), moved, 1e-2);
    }
}

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

TEST(PerspectiveMaths, APlaneHoldsShortOfItsHorizon)
{
    const Quad rect{PointF{0, 0}, PointF{100, 0}, PointF{100, 100}, PointF{0, 100}};
    EXPECT_TRUE(planeHolds(rect, {0.25f, 0.25f}, {0.75f, 0.75f}));
    const Quad steep{PointF{45, 0}, PointF{55, 0}, PointF{100, 100}, PointF{0, 100}};
    EXPECT_FALSE(planeHolds(steep, {0.25f, 0.25f}, {0.75f, 0.75f}));
    EXPECT_TRUE(planeHolds(steep, {0.f, 0.f}, {1.f, 1.f}));
    EXPECT_FALSE(planeHolds(rect, {0.75f, 0.25f}, {0.25f, 0.75f}));
}

TEST(PerspectiveMaths, QuadsAndThePlane)
{
    const Quad q{PointF{100, 100}, PointF{300, 120}, PointF{280, 260}, PointF{90, 240}};
    EXPECT_TRUE(convex(q));
    EXPECT_FALSE(convex(Quad{PointF{100, 100}, PointF{280, 260}, PointF{300, 120}, PointF{90, 240}}));
    for (const PointF uv : {PointF{0, 0}, PointF{1, 0}, PointF{1, 1}, PointF{0, 1}, PointF{0.3f, 0.7f}}) {
        const PointF xy = fromUV(q, uv);
        const PointF back = toUV(q, xy);
        EXPECT_NEAR(back.x, uv.x, 1e-4);
        EXPECT_NEAR(back.y, uv.y, 1e-4);
    }
    expectQuadNear(subQuad(q, {0, 0}, {1, 1}), q, 1e-3);
    EXPECT_LT(dist(centre(q), fromUV(q, {0.5f, 0.5f})), 1e-3);
    EXPECT_DOUBLE_EQ(screenZ(360, 360), 312.5);
    EXPECT_DOUBLE_EQ(screenZ(360, 720), 156.25);
    EXPECT_DOUBLE_EQ(screenZ(0, 0), 312.5);
}

class FixedMeasure : public TextMeasurePort {
public:
    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        const double fs = std::strtod(style[2].c_str(), nullptr);
        ++calls;
        lastScaleX = style[11];
        lastFont = style[1];
        lastBold = style[7];
        lastItalic = style[8];
        lastSpacing = style[13];
        lastText = text;
        const std::u16string u = core::toUtf16(std::u8string(text.begin(), text.end()));
        TextExtents e;
        e.width = static_cast<float>(static_cast<double>(u.size()) * fs * 0.5);
        e.height = static_cast<float>(fs);
        return e;
    }
    std::string lastScaleX, lastFont, lastBold, lastItalic, lastSpacing, lastText;
    int calls = 0;
};

std::string scriptOf(const std::vector<std::string> &texts, const std::string &extraInfo = {},
                     const std::string &extraStyles = {}, bool withDefault = true)
{
    std::string s = "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n" + extraInfo +
                    "\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
                    "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n" +
                    std::string(withDefault ? "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,"
                                   "100,0,0,1,2,1,2,10,10,10,1\n"
                                            : "") +
                    extraStyles +
                    "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (const std::string &t : texts)
        s += t.starts_with("Dialogue:") || t.starts_with("Comment:")
                 ? t + "\n"
                 : "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,," + t + "\n";
    return s;
}

// One Layer each: libass stacks only Lines on the same Layer.
std::vector<std::string> apart(const std::vector<std::string> &texts)
{
    std::vector<std::string> out;
    for (std::size_t i = 0; i < texts.size(); ++i)
        out.push_back("Dialogue: " + std::to_string(i) + ",0:00:01.00,0:00:03.00,Default,,0,0,0,," + texts[i]);
    return out;
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
                  const std::string &extraStyles = {}, bool withDefault = true)
    {
        v.setClient(640, 400, 40);
        v.setScript(640, 360);
        v.open({1280, 720, 0, 1});
        v.refreshToolTransform();
        script = scriptOf(texts, extraInfo, extraStyles, withDefault);
        reopen();
    }
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
    std::set<std::string> leased;
    bool fontLeased(const std::string &name) const override
    {
        std::string lower = name;
        for (char &c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return leased.contains(lower);
    }
    std::u16string uncovered;
    bool fallsBackToLeased(const std::string &, bool, bool, std::u16string_view text) const override
    {
        return !leased.empty() && text.find_first_of(uncovered) != std::u16string_view::npos;
    }
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
    EXPECT_EQ(a.transform.frz, 30);
    EXPECT_EQ(a.transform.fax, 0.25);
    EXPECT_EQ(a.transform.fscx, 80);
    EXPECT_EQ(a.transform.fscy, 120);
    EXPECT_EQ(a.transform.bord.x, 3);
    EXPECT_EQ(a.transform.bord.y, 5);
    EXPECT_EQ(a.transform.shad.x, 2);
    EXPECT_EQ(a.transform.pos.x, 9);
    EXPECT_EQ(a.transform.org.x, 9);

    const LineState b = readTags(host, lineWith(host, 1));
    EXPECT_EQ(b.transform.an, 8);
    EXPECT_EQ(b.transform.org.x, 1);
    EXPECT_EQ(b.transform.org.y, 2);
    EXPECT_EQ(b.transform.pos.y, 50);
    EXPECT_EQ(b.transform.bord.x, 2);
    EXPECT_EQ(b.transform.shad.y, 1);

    const LineState c = readTags(host, lineWith(host, 2));
    EXPECT_TRUE(c.hasMove);
    EXPECT_EQ(c.transform.pos.x, 200);
    EXPECT_EQ(c.transform.pos.y, 150);
    EXPECT_EQ(c.moveTimes, u",0,1000");
}

TEST(PerspectiveTags, MeasuresTheTextAtAScaleOf100)
{
    Host host(apart({"{\\fscx200\\fs20\\fnMono}Hello\\Nab", "{\\p1}m 10 20 l 110 20 110 70 10 70", "{\\p2}m 0 0 l 100 50"}));
    const auto a = readLine(host, lineWith(host, 0));
    ASSERT_TRUE(a);
    EXPECT_EQ(host.measure.lastScaleX, "100");
    EXPECT_EQ(host.measure.lastFont, "Mono");
    EXPECT_FLOAT_EQ(a->box.width(), 5 * 10);
    EXPECT_FLOAT_EQ(a->box.height(), 40);
    const auto b = readLine(host, lineWith(host, 1));
    ASSERT_TRUE(b);
    EXPECT_EQ(b->box.topLeft.x, 10);
    EXPECT_EQ(b->box.topLeft.y, 20);
    EXPECT_EQ(b->box.bottomRight.x, 110);
    EXPECT_EQ(b->box.bottomRight.y, 70);
    const auto c = readLine(host, lineWith(host, 2));
    ASSERT_TRUE(c);
    EXPECT_EQ(c->box.bottomRight.x, 50);
    host.withMeasure = false;
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_TRUE(readLine(host, lineWith(host, 1)));
}

TEST(PerspectiveTags, ABaselineOffsetMovesADrawingsBox)
{
    // libass: ascent height - \pbo and descent \pbo, neither below 0; \r keeps \pbo, \p2 halves it.
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
    t.fax = 0;
    t.frx = 0;
    t.frz = 0;
    t.fscy = 100;
    t.bord = {2, 2};
    EXPECT_EQ(writeTransform(u"Text", t, style), u"{\\pos(320,180.5)\\org(300,170)}Text");
    EXPECT_EQ(writeTransform(u"{\\move(100,100,300,200,0,1000)}Text", t, style, PointF{200, 150}),
              u"{\\move(220,130.5,420,230.5,0,1000)}Text");
}

TEST(PerspectiveTags, ReadsAStyleReset)
{
    Host host({"{\\frx30\\fscx50\\r}Text", "{\\frx30\\rDefault\\fry5}Text", "{\\fax1}{\\frx30\\rNone}Text"});
    const LineState a = readTags(host, lineWith(host, 0));
    EXPECT_EQ(a.transform.frx, 0);
    EXPECT_EQ(a.transform.fscx, 100);
    const LineState b = readTags(host, lineWith(host, 1));
    EXPECT_EQ(b.transform.frx, 0);
    EXPECT_EQ(b.transform.fry, 5);
    const LineState c = readTags(host, lineWith(host, 2));
    EXPECT_EQ(c.transform.frx, 0);
    EXPECT_EQ(c.transform.fax, 0);
}

TEST(PerspectiveTags, MeasuresASoftBreakAsASpace)
{
    Host host({"ab\\ncd"});
    const auto a = readLine(host, lineWith(host, 0));
    ASSERT_TRUE(a);
    EXPECT_FLOAT_EQ(a->box.width(), 5 * 20);
    EXPECT_FLOAT_EQ(a->box.height(), 40);
    Host wrap({"ab\\ncd"}, "WrapStyle: 2");
    const auto b = readLine(wrap, lineWith(wrap, 0));
    ASSERT_TRUE(b);
    EXPECT_FLOAT_EQ(b->box.width(), 2 * 20);
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
    EXPECT_EQ(writeTransform(u"{\\frx10}{\\frx20\\blur1}Text", t, style),
              u"{\\pos(320,180.5)\\org(300,170)\\frx5}{\\blur1}Text");
    EXPECT_EQ(writeTransform(u"{\\frx10}{\\frx20}Text", t, style), u"{\\pos(320,180.5)\\org(300,170)\\frx5}Text");
    EXPECT_EQ(writeTransform(u"{\\frx30\\r\\blur1}Text", t, style),
              u"{\\r\\pos(320,180.5)\\org(300,170)\\fax0\\fscx100\\fscy100\\frz0\\frx5\\fry0\\xbord2\\ybord2\\xshad1"
              u"\\yshad1\\blur1}Text");
    EXPECT_EQ(writeTransform(u"{\\an7}{\\frx30\\rAlt}Text", t, style),
              u"{\\an7}{\\rAlt\\pos(320,180.5)\\org(300,170)\\fax0\\fscx100\\fscy100\\frz0\\frx5\\fry0\\xbord2"
              u"\\ybord2\\xshad1\\yshad1}Text");
    EXPECT_EQ(writeTransform(u"{\\pos(10,20)}a{\\move(1,2,3,4)}", t, style, PointF{10, 20}),
              u"{\\pos(320,180.5)\\org(300,170)\\frx5}a{\\move(1,2,3,4)}");
}

TEST(PerspectiveTags, ReadsThePositionAsLibass)
{
    Host host({"{\\move(100,100,300,200,0,0)}Moving", "{\\a4}x", "{\\a8}x", "{\\a99\\an5}x",
               "{\\pos(1,2,3)\\org(1,2,3)}a{\\pos(5,6)\\org(7,8)}", "{\\pos(a,7)}x"});
    const LineState a = readTags(host, lineWith(host, 0));
    EXPECT_EQ(a.transform.pos.x, 150);
    EXPECT_EQ(a.transform.pos.y, 125);
    EXPECT_EQ(readTags(host, lineWith(host, 1)).transform.an, 7); // VSFilter's \a4 is \a5
    EXPECT_EQ(readTags(host, lineWith(host, 2)).transform.an, 7);
    EXPECT_EQ(readTags(host, lineWith(host, 3)).transform.an, 2);
    const LineState e = readTags(host, lineWith(host, 4));
    EXPECT_EQ(e.transform.pos.x, 5);
    EXPECT_EQ(e.transform.org.x, 7);
    EXPECT_EQ(readTags(host, lineWith(host, 5)).transform.pos.x, 0);
}

TEST(PerspectiveTags, MeasuresTheTextAsLibassShowsIt)
{
    Host host(apart({"{\\q2}ab\\ncd", "Hello \\{w\\}", "{\\fs+10}ab", "{\\fs0}ab", "{\\fs-5}ab", "a{b",
                     "{\\p1}m 0 0 l 10 0 10 10{\\c&HFF&}l 100 100", "{\\p1}m 0 0 l 10 0 10 10{\\p0}",
                     "ab{\\p1}m 0 0 l 1 1{\\p0}"}));
    const auto box = [&host](std::size_t i) { return readLine(host, lineWith(host, i)); };
    ASSERT_TRUE(box(0));
    EXPECT_FLOAT_EQ(box(0)->box.height(), 80);
    ASSERT_TRUE(box(1));
    EXPECT_FLOAT_EQ(box(1)->box.width(), 9 * 20);
    EXPECT_FLOAT_EQ(box(2)->box.width(), 2 * 40);
    EXPECT_FLOAT_EQ(box(3)->box.width(), 2 * 20);
    EXPECT_FLOAT_EQ(box(4)->box.width(), 2 * 10);
    EXPECT_FLOAT_EQ(box(5)->box.width(), 3 * 20);
    EXPECT_FALSE(box(6));
    ASSERT_TRUE(box(7));
    EXPECT_EQ(box(7)->box.bottomRight.x, 10);
    EXPECT_FALSE(box(8));
    Host wrap({"{\\q0}ab\\ncd"}, "WrapStyle: 2");
    EXPECT_FLOAT_EQ(readLine(wrap, lineWith(wrap, 0))->box.height(), 40);
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
    for (int i = 0; i < 70000; ++i)
        blocks += "{\\frx1}";
    Host host(apart({braces, escapes, blocks + "x"}), "WrapStyle: 2");
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(readLine(host, lineWith(host, i)).has_value(), i == 2) << i;
        const LineState state = readTags(host, lineWith(host, i));
        const std::u16string text = core::toUtf16(lineWith(host, i).text);
        EXPECT_FALSE(writeTransform(text, state.transform, core::StyleValues{}).empty());
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    EXPECT_LT(ms, 5000);
}

TEST(PerspectiveTags, ManyResetsAndStylesTakeLinearTime)
{
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
    EXPECT_EQ(state->transform.frx, 5);
    EXPECT_LT(ms, 2000);
}

TEST(PerspectiveTags, AGlyphChangeAfterTheTextIsNotMeasured)
{
    Host host(apart({"A{\\fs80}B", "A{\\c&HFF&}B", "\\N{\\fscx50}B", "A{\\rDefault}B", "{\\fs80}A"}));
    const auto box = [&host](std::size_t i) { return readLine(host, lineWith(host, i)); };
    EXPECT_FALSE(box(0));
    ASSERT_TRUE(box(1));
    EXPECT_FLOAT_EQ(box(1)->box.width(), 2 * 20);
    EXPECT_FALSE(box(2));
    EXPECT_FALSE(box(3));
    ASSERT_TRUE(box(4));
    EXPECT_FLOAT_EQ(box(4)->box.width(), 40);
}

TEST(PerspectiveTags, ALineLibassWrapsIsNotMeasured)
{
    const std::string wide(40, 'a');
    Host host(apart({wide, "{\\q2}" + wide, "{\\fscx50}" + wide}));
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_TRUE(readLine(host, lineWith(host, 1)));
    EXPECT_TRUE(readLine(host, lineWith(host, 2)));
    Host unwrapped({wide}, "WrapStyle: 2");
    EXPECT_TRUE(readLine(unwrapped, lineWith(unwrapped, 0)));
}

TEST(PerspectiveTags, MeasuresWithTheLeadingSpacingWeightAndSlant)
{
    Host host({"{\\fsp20\\b1\\i1}AB"});
    ASSERT_TRUE(readLine(host, lineWith(host, 0)));
    EXPECT_EQ(host.measure.lastSpacing, "20");
    EXPECT_NE(host.measure.lastBold, "0");
    EXPECT_NE(host.measure.lastItalic, "0");
    Host reset({"{\\b1\\b\\i1\\i}AB"});
    ASSERT_TRUE(readLine(reset, lineWith(reset, 0)));
    EXPECT_EQ(reset.measure.lastBold, "0");
    EXPECT_EQ(reset.measure.lastItalic, "0");
}

TEST(PerspectiveTags, AnAnimatedQuadIsNotMeasured)
{
    Host host({"{\\t(0,1000,\\frx60)}Text", "{\\t(0,1000,\\fs80)}Text", "Te{\\t(\\fscx50)}xt",
               "{\\t(0,1000,\\c&HFF&)}Text", "{\\p1\\t(\\frz30)}m 0 0 l 10 0 10 10"});
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_FALSE(readLine(host, lineWith(host, 1)));
    EXPECT_FALSE(readLine(host, lineWith(host, 2)));
    EXPECT_TRUE(readLine(host, lineWith(host, 3)));
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
    EXPECT_EQ(b.transform.fscx, 100);
    EXPECT_EQ(b.transform.frz, 0);
    EXPECT_EQ(b.transform.bord.x, 2);
    EXPECT_EQ(b.transform.shad.y, 1);
}

TEST(PerspectiveTags, AHostileLineIsMeasuredBoundedly)
{
    std::string rows = "{\\pos(320,200)}a", tooMany = rows;
    for (int i = 1; i < 64; ++i)
        rows += "\\Na";
    tooMany = rows + "\\Na";
    std::string breaks = "{\\pos(320,200)}";
    for (int i = 0; i < 200000; ++i)
        breaks += "\\N";
    Host host({rows, tooMany, breaks, "{\\pos(320,200)}" + std::string(4097, 'a'),
               "{\\pos(320,200)}" + std::string(4096, 'a')},
              "WrapStyle: 2");
    EXPECT_TRUE(readLine(host, lineWith(host, 0)));
    EXPECT_EQ(host.measure.calls, 64);
    host.measure.calls = 0;
    for (const std::size_t i : {1, 2, 3})
        EXPECT_FALSE(readLine(host, lineWith(host, i))) << i;
    EXPECT_EQ(host.measure.calls, 0);
    EXPECT_TRUE(readLine(host, lineWith(host, 4)));
}

TEST(PerspectiveTags, AStackIsMeasuredInOneBudget)
{
    std::string rows = "a";
    for (int i = 1; i < 8; ++i)
        rows += "\\Na";
    const auto read = [&rows](int lines) {
        Host host(std::vector<std::string>(lines, rows), "WrapStyle: 2");
        const bool ok = readLine(host, lineWith(host, lines - 1)).has_value();
        return std::pair{ok, host.measure.calls};
    };
    EXPECT_EQ(read(8), (std::pair{true, 64}));
    const auto [ok, calls] = read(9);
    EXPECT_FALSE(ok);
    EXPECT_LE(calls, 64);
}

TEST(PerspectiveTags, AHostileDrawingIsMeasuredBoundedly)
{
    const auto drawing = [](int steps) {
        std::string d = "{\\pos(320,200)\\p1}m 0 0 l 100 0 100 50 0 50";
        for (int i = 0; i < steps; ++i)
            d += " l 10 10";
        return d;
    };
    Host host({drawing(30000), drawing(40000)});
    EXPECT_TRUE(readLine(host, lineWith(host, 0)));
    const auto started = std::chrono::steady_clock::now();
    EXPECT_FALSE(readLine(host, lineWith(host, 1)));
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(),
              100);
}

TEST(PerspectiveTags, TheTextOneReadParsesIsBounded)
{
    const auto tags = [](std::size_t bytes) {
        std::string t = "{";
        while (t.size() < bytes)
            t += "\\frx0";
        return t + "}";
    };
    Host host(apart({"{\\pos(320,200)}" + tags(600 * 1024) + "A", "{\\pos(320,200)}" + tags(400 * 1024) + "A"}));
    const auto started = std::chrono::steady_clock::now();
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(),
              50);
    EXPECT_TRUE(readLine(host, lineWith(host, 1)));
    Host stacked({tags(300 * 1024) + "First", tags(300 * 1024) + "Second"});
    EXPECT_FALSE(readLine(stacked, lineWith(stacked, 1)));
    EXPECT_TRUE(readLine(stacked, lineWith(stacked, 0)));
}

TEST(PerspectiveTags, TheWrapTakesTheLinesOwnStyleMargins)
{
    // libass keeps the Line's Style's margins after a \r.
    const std::string styles =
        "Style: Bare,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,0,0,10,1\n"
        "Style: Wide,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,300,300,10,1\n";
    Host host({"{\\pos(320,200)\\rBare}" + std::string(32, 'a'), "{\\pos(320,200)\\rWide}" + std::string(20, 'a')}, {},
              styles);
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_TRUE(readLine(host, lineWith(host, 1)));
}

TEST(PerspectiveTags, AMovingLineWithoutAnOriginKeepsIt)
{
    const core::StyleValues st;
    Transform t;
    t.pos = {150, 100};
    t.org = {150, 100};
    t.frx = 20;
    EXPECT_EQ(writeTransform(u"{\\move(100,100,300,200)}a", t, st, PointF{150, 100}).find(u"\\org"),
              std::u16string::npos);
    Transform flat = t;
    flat.frx = 0;
    flat.org = {10, 10};
    EXPECT_EQ(writeTransform(u"{\\move(100,100,300,200)}a", flat, st, PointF{150, 100}).find(u"\\org"),
              std::u16string::npos);
    Transform away = t;
    away.org = {10, 10};
    EXPECT_NE(writeTransform(u"{\\move(100,100,300,200)}a", away, st, PointF{150, 100}).find(u"\\org(10,10)"),
              std::u16string::npos);
    EXPECT_NE(writeTransform(u"{\\move(100,100,300,200)\\org(5,5)}a", t, st, PointF{150, 100})
                  .find(u"\\org(150,100)"),
              std::u16string::npos);
    EXPECT_NE(writeTransform(u"{\\pos(150,100)}a", t, st).find(u"\\org(150,100)"), std::u16string::npos);
}

TEST(PerspectiveTags, TheStylesAreDecodedOnceARevision)
{
    Host host({"{\\pos(320,200)}Text"});
    StyleCache cache;
    const auto first = cache.get(host);
    EXPECT_EQ(cache.get(host).get(), first.get());
    ASSERT_TRUE(readLine(host, lineWith(host), cache));
    EXPECT_EQ(cache.get(host).get(), first.get());
    ASSERT_TRUE(host.beginGesture(host.targets, "edit"));
    host.gesture()->stage(host.line(0), u8"{\\pos(10,10)}Text", false);
    ASSERT_TRUE(host.commitGesture());
    EXPECT_NE(cache.get(host).get(), first.get());
}

TEST(PerspectiveTags, ScalesOutlinesAndShadowsAreHeldAsLibassHoldsThem)
{
    // libass holds \fscx, \fscy, the outlines and \shad (not \xshad, \yshad) and a Style's metrics at 0 and above.
    const std::string neg = "Style: Neg,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,-50,-20,-5,0,1,"
                            "-2,-1,2,10,10,10,1\n";
    Host host({"{\\pos(320,200)\\fscx-50\\fscy-1\\bord-3\\shad-2}Text",
               "{\\pos(320,200)\\xbord-1\\ybord-1\\xshad-2\\yshad-3}Text",
               "Dialogue: 0,0:00:01.00,0:00:03.00,Neg,,0,0,0,,{\\pos(320,200)}Text"},
              {}, neg);
    const Transform a = readTags(host, lineWith(host, 0)).transform;
    EXPECT_EQ(a.fscx, 0);
    EXPECT_EQ(a.fscy, 0);
    EXPECT_EQ(a.bord.x, 0);
    EXPECT_EQ(a.bord.y, 0);
    EXPECT_EQ(a.shad.x, 0);
    EXPECT_EQ(a.shad.y, 0);
    const Transform b = readTags(host, lineWith(host, 1)).transform;
    EXPECT_EQ(b.bord.x, 0);
    EXPECT_EQ(b.bord.y, 0);
    EXPECT_EQ(b.shad.x, -2);
    EXPECT_EQ(b.shad.y, -3);
    const Transform c = readTags(host, lineWith(host, 2)).transform;
    EXPECT_EQ(c.fscx, 0);
    EXPECT_EQ(c.fscy, 0);
    EXPECT_EQ(c.bord.x, 0);
    EXPECT_EQ(c.shad.y, 0);
    ASSERT_TRUE(readLine(host, lineWith(host, 2)));
    EXPECT_EQ(host.measure.lastSpacing, "0");
    PerspectiveTool tool;
    tool.reset(host);
    EXPECT_FALSE(tool.hasQuad());
}

TEST(PerspectiveTags, ArgumentsAreSplitAsLibassSplitsThem)
{
    // libass trims each argument and drops an empty one; spaces may follow the backslash.
    Host host({"{\\pos(100,)\\org(5, )}Text", "{\\pos( 100 , 50 )\\ frx20}Text", "{\\move(1,2,3,4,,0,1000)}Text",
               "{\\move(1,2,,3,4)}Text"});
    const LineState a = readTags(host, lineWith(host, 0));
    EXPECT_TRUE(a.stacks);
    EXPECT_FALSE(a.ownOrg);
    const LineState b = readTags(host, lineWith(host, 1));
    EXPECT_EQ(b.transform.pos.x, 100);
    EXPECT_EQ(b.transform.pos.y, 50);
    EXPECT_EQ(b.transform.frx, 20);
    const LineState c = readTags(host, lineWith(host, 2));
    EXPECT_TRUE(c.hasMove);
    EXPECT_EQ(c.moveTimes, u",0,1000");
    const LineState d = readTags(host, lineWith(host, 3));
    EXPECT_TRUE(d.hasMove);
    EXPECT_EQ(d.move, (std::array<double, 4>{1, 2, 3, 4}));
}

TEST(PerspectiveTags, ANameEndsBeforeItsSpaces)
{
    // libass looks up a \r's Style and a \fn's font without trailing spaces; \fn0 is the Style's font.
    const std::string other = "Style: Other,Other Face,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,50,100,"
                              "0,0,1,2,1,2,10,10,10,1\n";
    Host host({"{\\pos(320,200)\\rOther \\frx20}Text", "{\\pos(320,200)\\fnSome Face  \\frx1}Text",
               "{\\pos(320,200)\\fn0}Text"},
              {}, other);
    const LineState a = readTags(host, lineWith(host, 0));
    EXPECT_EQ(a.transform.fscx, 50);
    EXPECT_EQ(a.transform.frx, 20);
    ASSERT_TRUE(readLine(host, lineWith(host, 0)));
    EXPECT_EQ(host.measure.lastFont, "Other Face");
    ASSERT_TRUE(readLine(host, lineWith(host, 1)));
    EXPECT_EQ(host.measure.lastFont, "Some Face");
    ASSERT_TRUE(readLine(host, lineWith(host, 2)));
    EXPECT_EQ(host.measure.lastFont, "Arial");
}

TEST(PerspectiveTags, AWeightTheMeasureCannotTakeIsNotMeasured)
{
    // libass renders \b500 at that weight; the measure has normal and bold.
    Host host({"{\\pos(320,200)\\b500}AB", "{\\pos(320,200)\\b700}AB", "{\\pos(320,200)\\b400}AB",
               "{\\pos(320,200)\\b50}AB", "{\\pos(320,200)\\b900}AB"});
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    ASSERT_TRUE(readLine(host, lineWith(host, 1)));
    EXPECT_NE(host.measure.lastBold, "0");
    ASSERT_TRUE(readLine(host, lineWith(host, 2)));
    EXPECT_EQ(host.measure.lastBold, "0");
    ASSERT_TRUE(readLine(host, lineWith(host, 3)));
    EXPECT_EQ(host.measure.lastBold, "0");
    EXPECT_FALSE(readLine(host, lineWith(host, 4)));
}

TEST(PerspectiveTags, FscResetsBothScales)
{
    Host host({"{\\pos(320,200)\\fscx50\\fscy70\\fsc}T", "{\\pos(320,200)}Te{\\fsc}xt", "{\\pos(320,200)\\t(\\fsc)}T"});
    const LineState a = readTags(host, lineWith(host, 0));
    EXPECT_EQ(a.transform.fscx, 100);
    EXPECT_EQ(a.transform.fscy, 100);
    EXPECT_FALSE(readLine(host, lineWith(host, 1)));
    EXPECT_FALSE(readLine(host, lineWith(host, 2)));
    Transform t;
    t.pos = {1, 2};
    t.org = t.pos;
    const std::u16string out = writeTransform(u"{\\fscx50\\fsc}a", t, core::StyleValues{});
    for (std::size_t i = out.find(u"\\fsc"); i != std::u16string::npos; i = out.find(u"\\fsc", i + 1))
        EXPECT_TRUE(out[i + 4] == u'x' || out[i + 4] == u'y') << core::toUtf8(out).size();
}

TEST(PerspectiveTags, ATabIsMeasuredAsASpace)
{
    Host host({"{\\pos(320,200)}A\tB"});
    ASSERT_TRUE(readLine(host, lineWith(host)));
    EXPECT_EQ(host.measure.lastText, "A B");
}

TEST(PerspectiveTags, ARowsEndSpacesAreDroppedAsLibassDropsThem)
{
    // libass trim_whitespace: spaces (a tab, a soft \n) at a row's ends show nothing; \h is no space.
    Host host(apart({"{\\pos(320,200)} \tA B\\n", "{\\pos(320,200)}\\hA ", "{\\pos(320,200)}A \\N B"}),
              "WrapStyle: 0");
    ASSERT_TRUE(readLine(host, lineWith(host, 0)));
    EXPECT_EQ(host.measure.lastText, "A B");
    ASSERT_TRUE(readLine(host, lineWith(host, 1)));
    EXPECT_EQ(host.measure.lastText, " A");
    ASSERT_TRUE(readLine(host, lineWith(host, 2)));
    EXPECT_EQ(host.measure.lastText, "B");
}

TEST(PerspectiveTags, AFallbackOnlyTheRendererSeesIsNotMeasured)
{
    Host host({"{\\pos(320,200)}A中", "{\\pos(320,200)}A"});
    host.uncovered = u"中";
    EXPECT_TRUE(readLine(host, lineWith(host, 0)));
    host.leased = {"other"};
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_TRUE(readLine(host, lineWith(host, 1)));
}

TEST(PerspectiveTags, ASpacedSurrogatePairIsNotMeasured)
{
    Host host({"{\\pos(320,200)\\fsp2}A\U0001F600", "{\\pos(320,200)}A\U0001F600", "{\\pos(320,200)\\fsp2}A"});
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_TRUE(readLine(host, lineWith(host, 1)));
    EXPECT_TRUE(readLine(host, lineWith(host, 2)));
}

TEST(PerspectiveTags, ATranslationIsChargedAsTheTextItShows)
{
    Host host({"{\\pos(320,200)}a"});
    core::LineRecord line = lineWith(host, 0);
    std::string big = "{\\pos(320,200)";
    while (big.size() < 600 * 1024)
        big += "\\frx0";
    big += "}b";
    line.translation = std::u8string(big.begin(), big.end());
    EXPECT_FALSE(readLine(host, line));
    line.translation = u8"{\\pos(320,200)}b";
    EXPECT_TRUE(readLine(host, line));
}

TEST(PerspectiveTags, ADrawingIsReadAsLibassReadsIt)
{
    // libass drawing_tokenize: nothing before the first m with a point; b points in threes, s three or none.
    Host host(apart({"{\\pos(320,200)\\p1}l 0 0 100 100", "{\\pos(320,200)\\p1}n 5 5 m 0 0 l 10 10",
                     "{\\pos(320,200)\\p1}m 0 0 l 100 0 100 50 b 500 500 600 600",
                     "{\\pos(320,200)\\p1}x l 900 900 m 0 0 l 40 0 40 20",
                     "{\\pos(320,200)\\p1}m 0 0 l 30 30 s 300 300 400", "{\\pos(320,200)\\p1}m 0 0 s 10 0 10 10 0 10 p 70 70",
                     "{\\pos(320,200)\\p1}m 0 0 l 1e1 +2.5 -.5 5"}));
    const auto box = [&host](std::size_t i) { return readLine(host, lineWith(host, i)); };
    EXPECT_FALSE(box(0));
    EXPECT_FALSE(box(1));
    ASSERT_TRUE(box(2));
    EXPECT_FLOAT_EQ(box(2)->box.width(), 100);
    EXPECT_FLOAT_EQ(box(2)->box.height(), 50);
    ASSERT_TRUE(box(3));
    EXPECT_FLOAT_EQ(box(3)->box.width(), 40);
    ASSERT_TRUE(box(4));
    EXPECT_FLOAT_EQ(box(4)->box.width(), 30);
    ASSERT_TRUE(box(5));
    EXPECT_FLOAT_EQ(box(5)->box.width(), 70);
    ASSERT_TRUE(box(6));
    EXPECT_FLOAT_EQ(box(6)->box.width(), 10.5);
    EXPECT_FLOAT_EQ(box(6)->box.height(), 5);
}

TEST(PerspectiveTags, ATagRunInATransformIsTheQuads)
{
    // libass applies a tag in a \t that cannot animate (\pos, \an, \fn) at once.
    Host host(apart({"{\\t(\\pos(100,100))}Text", "{\\t(\\an7)}Text", "{\\t(0,500,\\fn Other)}Text",
                     "{\\t(\\c&HFF&\\1a&H80&\\3c&H00FF00&\\alpha&H10&\\blur2\\be1\\clip(0,0,10,10))}Text",
                     "{\\t(\\iclip(0,0,10,10))}Text"}));
    for (const std::size_t i : {0, 1, 2})
        EXPECT_FALSE(readLine(host, lineWith(host, i))) << i;
    for (const std::size_t i : {3, 4})
        EXPECT_TRUE(readLine(host, lineWith(host, i))) << i;
}

TEST(PerspectiveTags, ARelativeSizeHasItsSignFirst)
{
    // libass reads \fs +10 as 10: a sign is relative only where the argument starts.
    Host host(apart({"{\\pos(320,200)\\fs +10}ab", "{\\pos(320,200)\\fs+10}ab"}));
    const auto a = readLine(host, lineWith(host, 0));
    const auto b = readLine(host, lineWith(host, 1));
    ASSERT_TRUE(a && b);
    EXPECT_FLOAT_EQ(a->box.height(), 10);
    EXPECT_FLOAT_EQ(b->box.height(), 80);
}

TEST(PerspectiveTags, TheStackIsFittedAsLibassFitsIt)
{
    // libass fit_rect: each Line is placed clear of those placed before it.
    const std::string at = "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,";
    const std::string text = ",,{\\bord0\\fs20}A";
    Host host({at + "285" + text, at + "260" + text, at + "260" + text});
    Host alone({at + "260" + text});
    const auto stacked = readLine(host, lineWith(host, 2));
    const auto nominal = readLine(alone, lineWith(alone, 0));
    ASSERT_TRUE(stacked && nominal);
    EXPECT_NEAR(stacked->transform.pos.y, nominal->transform.pos.y - 45, 0.05);
    Host middle({"{\\an5\\bord0\\fs20}A", "{\\an5\\bord0\\fs20}A"});
    const auto first = readLine(middle, lineWith(middle, 0));
    const auto second = readLine(middle, lineWith(middle, 1));
    ASSERT_TRUE(first && second);
    EXPECT_NEAR(second->transform.pos.y, first->transform.pos.y + 20, 0.05);
}

TEST(PerspectiveTags, AStyleOfTooManyBytesIsNotRead)
{
    const std::string big = "Style: Big," + std::string(70 * 1024, 'F') +
                            ",40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n";
    Host host(apart({"{\\pos(320,200)}A", "{\\pos(320,200)\\rBig}A"}), {}, big);
    EXPECT_TRUE(readLine(host, lineWith(host, 0)));
    EXPECT_FALSE(readLine(host, lineWith(host, 1)));
    Host on({"Dialogue: 0,0:00:01.00,0:00:03.00,Big,,0,0,0,,{\\pos(320,200)}A"}, {}, big);
    EXPECT_FALSE(readLine(on, lineWith(on, 0)));
}

TEST(PerspectiveTags, EveryLineShownWithItIsCounted)
{
    const auto read = [](int banners) {
        std::vector<std::string> texts(banners, "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,Banner;10,");
        texts.push_back("Second");
        Host host(texts);
        return readLine(host, lineWith(host, texts.size() - 1)).has_value();
    };
    EXPECT_TRUE(read(4000));
    const auto started = std::chrono::steady_clock::now();
    EXPECT_FALSE(read(5000));
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(),
              2000);
}

TEST(PerspectiveTags, AWrittenPositionStaysAheadOfAMove)
{
    // libass takes the first \pos or \move.
    Transform t;
    t.pos = {10, 20};
    t.org = t.pos;
    for (const std::u16string text : {u"{\\pos(1,2)\\move(1,2,3,4)}a", u"{\\pos(1,2)}{\\move(1,2,3,4)}a"}) {
        const std::u16string out = writeTransform(text, t, core::StyleValues{});
        EXPECT_EQ(out.find(u"\\move"), std::u16string::npos);
        const std::u8string u8 = core::toUtf8(out);
        Host host({std::string(u8.begin(), u8.end())});
        const LineState read = readTags(host, lineWith(host));
        EXPECT_FALSE(read.hasMove);
        EXPECT_EQ(read.transform.pos.x, 10);
    }
}

TEST(PerspectiveTags, AFontOnlyTheRendererSeesIsNotMeasured)
{
    Host host({"{\\pos(320,200)}Text", "{\\pos(320,200)\\fnOther}Text", "{\\pos(320,200)\\fn@Other}Text"});
    host.leased = {"arial"};
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_TRUE(readLine(host, lineWith(host, 1)));
    EXPECT_FALSE(readLine(host, lineWith(host, 2)));
}

TEST(PerspectiveTags, AStyleIsFoundAsLibassFindsIt)
{
    // libass: the last Style of a name wins, leading *s drop; "default" in any case or a missing name is the last Default.
    const std::string style = "Style: %s,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,%d,100,0,0,1,2,"
                              "1,2,10,10,10,1\n";
    const auto styleLine = [&style](const char *name, int scale) {
        char buf[256];
        std::snprintf(buf, sizeof buf, style.c_str(), name, scale);
        return std::string(buf);
    };
    const std::string at = "Dialogue: 0,0:00:01.00,0:00:03.00,";
    const std::string styles = styleLine("Twice", 50) + styleLine("Twice", 70) + styleLine("Default", 90);
    const auto read = [&](const std::string &styleName, bool withDefault = true) {
        Host h({at + styleName + ",,0,0,0,,{\\pos(320,200)}T"}, {}, styles, withDefault);
        return std::pair{readTags(h, lineWith(h)).transform.fscx, readLine(h, lineWith(h)).has_value()};
    };
    EXPECT_EQ(read("Twice").first, 70);
    EXPECT_EQ(read("*Twice").first, 70);
    EXPECT_EQ(read("dEfAuLt").first, 90);
    EXPECT_EQ(read("Missing").first, 90);
    EXPECT_TRUE(read("Missing").second);
    const std::string noDefault = styleLine("Twice", 50);
    Host bare(apart({"{\\pos(320,200)}T", "{\\pos(320,200)\\rTwice\\rDefault}T", "{\\pos(320,200)\\b1}T",
                     "{\\pos(320,200)\\p1}m 0 0 l 10 0 10 10"}),
              {}, noDefault, false);
    EXPECT_FALSE(readLine(bare, lineWith(bare, 0))); // libass's own Default
    EXPECT_FALSE(readLine(bare, lineWith(bare, 1)));
    ASSERT_TRUE(readLine(bare, lineWith(bare, 2)));
    EXPECT_EQ(bare.measure.lastFont, "Arial");
    const auto drawing = readLine(bare, lineWith(bare, 3));
    ASSERT_TRUE(drawing);
    EXPECT_EQ(drawing->transform.bord.x, 2);
    EXPECT_EQ(drawing->transform.shad.y, 3);
}

TEST(PerspectiveTags, AnOutlineChangedAfterTheTextIsNotTaken)
{
    Host host(apart({"{\\pos(320,200)\\bord2}A{\\bord10}B", "{\\pos(320,200)\\bord2}A{\\yshad3}B",
                     "{\\pos(320,200)\\bord2}A{\\c&HFF&}B"}));
    EXPECT_FALSE(readLine(host, lineWith(host, 0)));
    EXPECT_FALSE(readLine(host, lineWith(host, 1)));
    EXPECT_TRUE(readLine(host, lineWith(host, 2)));
}

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
        tool.reset(host);
    }
    std::size_t steps() const { return host.s->historySize(); }
};

TEST(PerspectiveTool, DrawsTheTextBoxAsTheInnerQuad)
{
    Fixture f;
    ASSERT_TRUE(f.tool.hasQuad());
    expectQuadNear(f.tool.inner(), {PointF{210, 160}, PointF{430, 160}, PointF{430, 200}, PointF{210, 200}}, 1e-3);
    const Overlay o = f.tool.overlay(f.host);
    EXPECT_FALSE(o.lines.empty());
    EXPECT_FALSE(o.polygons.empty());
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
    f.host.cancelGesture();
    f.tool.reset(f.host);
    EXPECT_EQ(f.host.text(), text);
    EXPECT_LT(dist(f.tool.inner()[2], {430, 200}), 1e-3);
}

TEST(PerspectiveTool, ShiftLocksTheCornerToAnEdge)
{
    Fixture f;
    f.drag({430, 200}, {470, 207}, true);
    EXPECT_NEAR(f.tool.inner()[2].y, 200, 0.05);
    EXPECT_NEAR(f.tool.inner()[2].x, 470, 0.05);
}

TEST(PerspectiveTool, CtrlSnapsToAnotherHandle)
{
    Fixture f;
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
    EXPECT_LT(dist(f.tool.inner()[0], {190, 150}), 0.05);
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
    f.send(Pointer::Kind::Move, {150, 120});
    expectQuadNear(f.tool.inner(), last, 1e-3);
    f.send(Pointer::Kind::Release, {150, 120});
    f.tool.reset(f.host);
    EXPECT_LT(dist(f.tool.inner()[2], {440, 210}), 0.05);
}

TEST(PerspectiveTool, EveryPickedLineGetsTheTags)
{
    Fixture f({"{\\pos(320,200)}Perspective", "{\\pos(320,200)\\bord4}Backing", "{\\move(0,0,100,0)\\org(5,5)}Moving"});
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
    EXPECT_LT(dist(outer[0], {100, 140}), 0.05);
    EXPECT_LT(dist(outer[2], {540, 220}), 0.05);
    f.drag({430, 200}, {450, 210});
    expectQuadNear(f.tool.outer(), outer, 0.1);
    EXPECT_NEAR(f.tool.transform().frx, 0, 0.01);
    EXPECT_NEAR(f.tool.transform().fry, 0, 0.01);
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
    f.drag({540, 220}, {580, 240});
    expectQuadNear(f.tool.inner(), inner, 0.1);
    EXPECT_LT(dist(f.tool.outer()[2], {580, 240}), 0.1);
    f.drag({430, 200}, {440, 220});
    EXPECT_GT(std::abs(f.tool.transform().frx) + std::abs(f.tool.transform().fry), 1);
}

TEST(PerspectiveTool, ThePlaneIsKeptPerLine)
{
    Fixture f({"{\\pos(320,200)}Perspective", "{\\pos(320,100)}Other"});
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
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
    EXPECT_LT(dist(f.tool.outer()[2], {540, 220}), 0.1);
}

TEST(PerspectiveTool, AHiddenPlaneStaysWhereItWas)
{
    Fixture f;
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    const Quad outer = f.tool.outer();
    ASSERT_TRUE(f.tool.setOption("plane", 0, f.host));
    f.drag({320, 180}, {340, 170});
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    expectQuadNear(f.tool.outer(), outer, 0.05);
    EXPECT_LT(dist(f.tool.inner()[0], {230, 150}), 0.05);
}

TEST(PerspectiveTool, ALockedResizeIsKept)
{
    Fixture f({"{\\pos(320,200)}Perspective", "{\\pos(320,100)}Other"});
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    f.drag({430, 200}, {440, 205});
    ASSERT_TRUE(f.tool.setOption("lockOuter", 1, f.host));
    const PointF corner = f.tool.outer()[2];
    const std::size_t before = f.steps();
    f.drag(corner, {corner.x + 30, corner.y + 20});
    EXPECT_EQ(f.steps(), before);
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
    f.drag({540, 220}, {520, 250});
    ASSERT_GT(dist(f.tool.outer()[2], {540, 220}), 1);
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
    EXPECT_LT(f.tool.inner()[2].y, first - 1);
}

TEST(PerspectiveTool, AnOriginKeepsALineOutOfTheStack)
{
    // libass places a Line with \org without collisions.
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
    expectQuadNear(f.tool.inner(), shown, 0.05);
}

TEST(PerspectiveTool, AShownPlaneStaysAsTheQuadMoves)
{
    Fixture f({"{\\move(100,100,300,200,0,1000)}Moving"});
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    const Quad plane = f.tool.outer();
    const PointF was = centre(f.tool.inner());
    f.host.time = 2000;
    (void)f.tool.overlay(f.host);
    EXPECT_GT(dist(centre(f.tool.inner()), was), 50);
    expectQuadNear(f.tool.outer(), plane, 0.05);
}

TEST(PerspectiveTool, AnEditOfTheLineKeepsThePlane)
{
    Fixture f;
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    const Quad plane = f.tool.outer();
    auto g = f.host.beginGesture(f.host.targets, "Edit");
    ASSERT_TRUE(g);
    (*g)->stage(f.host.line(0), u8"{\\pos(320,200)}Perspective!!", false);
    ASSERT_TRUE(f.host.commitGesture());
    f.tool.reset(f.host);
    EXPECT_GT(f.tool.inner()[2].x, 431);
    expectQuadNear(f.tool.outer(), plane, 0.05);
}

TEST(PerspectiveTool, KeepLetsTheOriginBeDragged)
{
    Fixture f;
    ASSERT_TRUE(f.tool.setOption("orgMode", 2, f.host));
    f.drag({430, 200}, {450, 230});
    const Quad quad = f.tool.inner();
    const PointF org = f.tool.transform().org;
    f.drag(org, {org.x + 30, org.y - 20});
    EXPECT_NEAR(f.tool.transform().org.x, org.x + 30, 0.05);
    EXPECT_NEAR(f.tool.transform().org.y, org.y - 20, 0.05);
    expectQuadNear(f.tool.inner(), quad, 0.1);
}

TEST(PerspectiveTool, AnEditedOriginIsWrittenOnAMovingLine)
{
    Fixture f({"{\\move(100,100,300,200)}Text"});
    ASSERT_TRUE(f.tool.setOption("orgMode", 2, f.host));
    ASSERT_TRUE(f.tool.setValue("orgx", u"50", f.host));
    f.tool.reset(f.host);
    EXPECT_NE(f.host.text().find("\\org(50,"), std::string::npos) << f.host.text();
    EXPECT_NEAR(f.tool.transform().org.x, 50, 0.05);
}

TEST(PerspectiveTool, TheValuesAreTheTags)
{
    Fixture f;
    const auto values = f.tool.values(f.host);
    ASSERT_EQ(values.size(), 6u);
    EXPECT_EQ(values[0].name, "orgx");
    EXPECT_FALSE(values[0].editable);
    EXPECT_EQ(values[2].group, u"Rotation");
    EXPECT_EQ(values[5].label, u"Shear");
    const std::size_t before = f.steps();
    EXPECT_TRUE(f.tool.setValue("fry", u"30", f.host));
    f.tool.reset(f.host);
    EXPECT_EQ(f.steps(), before + 1);
    EXPECT_NE(f.host.text().find("\\fry30"), std::string::npos) << f.host.text();
    EXPECT_NEAR(f.tool.transform().fry, 30, 1e-6);
    EXPECT_FALSE(f.tool.setValue("orgx", u"10", f.host));
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
    EXPECT_FALSE(f.tool.setValue("fry", u"90", f.host));
    EXPECT_FALSE(f.host.gesture());
    EXPECT_FALSE(f.tool.setValue("fry", u"90", f.host, ValueEdit::Sample));
    EXPECT_FALSE(f.host.gesture());
    EXPECT_EQ(f.host.text(), text);
    EXPECT_TRUE(f.tool.setValue("fry", u"30", f.host));
    EXPECT_TRUE(f.tool.setValue("frx", u"5", f.host, ValueEdit::Sample));
    EXPECT_FALSE(f.tool.setValue("frx", u"90", f.host, ValueEdit::Sample));
    EXPECT_TRUE(f.host.gesture());
}

TEST(PerspectiveTool, AMovingQuadFollowsTheVideo)
{
    Fixture f({"{\\move(100,100,300,200,0,1000)}Moving"});
    EXPECT_LT(dist(centre(f.tool.inner()), {200, 130}), 0.05);
    f.host.time = 2000;
    (void)f.tool.overlay(f.host);
    EXPECT_LT(dist(centre(f.tool.inner()), {300, 180}), 0.05);
    f.drag({300, 180}, {310, 180});
    EXPECT_NE(f.host.text().find("\\move(110,100,310,200,0,1000)"), std::string::npos) << f.host.text();
}

TEST(PerspectiveTool, TheArrowsNudgeTheLastCorner)
{
    Fixture f;
    f.drag({430, 200}, {440, 205});
    EXPECT_EQ(f.tool.selectedCorner(), 2);
    const std::size_t before = f.steps();
    const Key right{keys::Right};
    EXPECT_TRUE(f.tool.takesKey(right));
    EXPECT_TRUE(f.tool.key(right, f.host));
    EXPECT_TRUE(f.tool.key({keys::Right, false, true}, f.host));
    Key shiftDown{keys::Down};
    shiftDown.shift = true;
    EXPECT_TRUE(f.tool.key(shiftDown, f.host));
    EXPECT_TRUE(f.tool.key({keys::Right, true}, f.host));
    f.tool.reset(f.host);
    EXPECT_EQ(f.steps(), before + 1);
    EXPECT_NEAR(f.tool.inner()[2].x, 442, 0.05);
    EXPECT_NEAR(f.tool.inner()[2].y, 205.1, 0.05);
    Key ctrl{keys::Right};
    ctrl.control = true;
    EXPECT_FALSE(f.tool.takesKey(ctrl));
}

TEST(PerspectiveTool, TheOptions)
{
    Fixture f;
    const auto options = f.tool.options(f.host);
    ASSERT_EQ(options.size(), 4u);
    EXPECT_EQ(options[0].iconRole, "perspective-plane");
    EXPECT_EQ(options[1].iconRole, "perspective-lock-outer");
    EXPECT_FALSE(options[1].enabled);
    EXPECT_EQ(options[2].iconRole, "perspective-grid");
    EXPECT_EQ(options[3].kind, ToolOption::Kind::Choice);
    EXPECT_EQ(options[3].group, u"Origin");
    EXPECT_EQ(options[3].choices.size(), 3u);
    EXPECT_FALSE(f.tool.setOption("lockOuter", 1, f.host));
    EXPECT_TRUE(f.tool.setOption("grid", 1, f.host));
    EXPECT_TRUE(f.tool.gridShown());
    EXPECT_TRUE(f.tool.setOption("orgMode", 1, f.host));
    EXPECT_EQ(f.tool.orgMode(), OrgMode::NoFax);
    f.tool.selected(f.host);
    EXPECT_TRUE(f.tool.gridShown());
    EXPECT_EQ(f.tool.orgMode(), OrgMode::NoFax);
}

TEST(PerspectiveTool, TheStackTakesTheOtherLinesScaleAndOutline)
{
    const auto second = [](const std::string &first) {
        Fixture f({first, "Second"});
        f.host.s->setSelection({f.host.line(1), {f.host.line(1)}, f.host.line(1), std::nullopt});
        f.host.targets = {f.host.line(1)};
        f.tool.reset(f.host);
        return f.tool.inner()[2].y;
    };
    const float plain = second("First");
    EXPECT_NEAR(plain - second("{\\fscy200}First"), 40, 0.05);
    EXPECT_NEAR(plain - second("{\\bord6}First"), 8, 0.05);
}

TEST(PerspectiveTool, ARefusedNudgeOrMoveChangesNothing)
{
    Fixture f({"{\\frx20\\an2\\pos(320,200)}Perspective", "{\\move(0,0,100,0)}Moving"});
    f.host.targets = {f.host.line(0), f.host.line(1)};
    ASSERT_TRUE(f.tool.hasQuad());
    const std::string a = f.host.text(0), b = f.host.text(1);
    const std::size_t before = f.steps();
    EXPECT_TRUE(f.tool.key({keys::Right}, f.host));
    EXPECT_TRUE(f.tool.key({keys::Right, true}, f.host));
    f.tool.reset(f.host);
    f.drag(centre(f.tool.inner()), add(centre(f.tool.inner()), {10, 0}));
    f.drag(f.tool.inner()[2], add(f.tool.inner()[2], {10, 10}));
    EXPECT_EQ(f.steps(), before);
    EXPECT_EQ(f.host.text(0), a);
    EXPECT_EQ(f.host.text(1), b);
}

TEST(PerspectiveTool, AMovingLineIsTurnedAboutItsPosition)
{
    // libass turns a \move Line without an \org about its moving position.
    Fixture f({"{\\move(100,100,300,200,0,1000)}Moving"});
    const PointF to = add(f.tool.inner()[2], {10, 15});
    const std::size_t before = f.steps();
    f.drag(f.tool.inner()[2], to);
    ASSERT_EQ(f.steps(), before + 1);
    const std::string text = f.host.text();
    EXPECT_EQ(text.find("\\org"), std::string::npos) << text;
    EXPECT_NE(text.find("\\move("), std::string::npos) << text;
    EXPECT_NE(text.find("\\frx"), std::string::npos) << text;
    EXPECT_LT(dist(f.tool.inner()[2], to), 0.05);
    EXPECT_EQ(f.tool.transform().org.x, f.tool.transform().pos.x);
    EXPECT_EQ(f.tool.transform().org.y, f.tool.transform().pos.y);
}

TEST(PerspectiveTool, ThePlaneIsShownWhereItHolds)
{
    bool found = false;
    for (int frx = 40; frx <= 85 && !found; frx += 5) {
        Fixture f({"{\\an5\\pos(320,180)\\frx" + std::to_string(frx) + "\\p1}m 0 0 l 600 0 600 300 0 300"});
        if (!f.tool.hasQuad() || planeHolds(f.tool.inner(), {0.25f, 0.25f}, {0.75f, 0.75f}))
            continue;
        found = true;
        EXPECT_TRUE(f.tool.setOption("plane", 1, f.host));
        const Quad o = f.tool.outer();
        for (const PointF &p : o)
            EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y));
        EXPECT_TRUE(convex(o));
        f.drag(f.tool.inner()[2], add(f.tool.inner()[2], {2, 2}));
        EXPECT_TRUE(convex(f.tool.outer()));
    }
    EXPECT_TRUE(found);
}

TEST(PerspectiveTool, TheStackIsLibassCollisions)
{
    const auto second = [](const std::string &first, const std::string &mine = "Second") -> std::optional<float> {
        Fixture f({first, mine});
        f.host.s->setSelection({f.host.line(1), {f.host.line(1)}, f.host.line(1), std::nullopt});
        f.host.targets = {f.host.line(1)};
        f.tool.reset(f.host);
        if (!f.tool.hasQuad())
            return std::nullopt;
        return f.tool.inner()[2].y;
    };
    const std::string at = "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,";
    const float plain = second("First").value_or(0);
    const float nominal = second("{\\pos(0,0)}First").value_or(0);
    ASSERT_LT(plain, nominal - 1);
    EXPECT_NEAR(second("{\\yshad-20}First").value_or(0), plain, 0.05);
    EXPECT_NEAR(second("{\\shad30}First").value_or(0), plain, 0.05);
    EXPECT_NEAR(second("Dialogue: 1,0:00:01.00,0:00:03.00,Default,,0,0,0,,First").value_or(0), nominal, 0.05);
    EXPECT_NEAR(second("{\\t(\\c&HFF&)}First").value_or(0), nominal, 0.05);
    EXPECT_NEAR(second(at + "Scroll up;0;100;10,First").value_or(0), nominal, 0.05);
    EXPECT_NEAR(second(at + "Scroll up;10,First").value_or(0), plain, 0.05);
    EXPECT_NEAR(second("First", "{\\t(\\c&HFF&)}Second").value_or(0), nominal, 0.05);
    EXPECT_NEAR(second("{\\c&HFF&}").value_or(0), nominal, 0.05);
    EXPECT_FALSE(second("A{\\ybord20}B"));
    EXPECT_NEAR(second("{\\shad9}A{\\shad20}B").value_or(0), second("AB").value_or(0), 0.05);
    // libass drops spaces but shows \h.
    EXPECT_NEAR(second(" \t ").value_or(0), nominal, 0.05);
    EXPECT_NEAR(second("\\h").value_or(0), plain, 0.05);
    EXPECT_FALSE(second("A{\\fs80}B"));
    EXPECT_FALSE(second("First", at + "Banner;10,Second"));
}

TEST(PerspectiveTool, OnlyObstaclesCountTowardsTheStacksBound)
{
    const auto second = [](int empties) {
        std::vector<std::string> texts(empties, "{\\p1\\bord0}m 0 0 l 0 10");
        texts.push_back("First");
        texts.push_back("Second");
        Fixture f(texts);
        const core::LineId mine = f.host.line(texts.size() - 1);
        f.host.s->setSelection({mine, {mine}, mine, std::nullopt});
        f.host.targets = {mine};
        f.tool.reset(f.host);
        return f.tool.inner()[2].y;
    };
    EXPECT_NEAR(second(300), second(0), 0.05);
}

TEST(PerspectiveTool, ACommentIsNoObstacle)
{
    const auto second = [](const std::string &first) {
        Fixture f({first, "Second"});
        f.host.s->setSelection({f.host.line(1), {f.host.line(1)}, f.host.line(1), std::nullopt});
        f.host.targets = {f.host.line(1)};
        f.tool.reset(f.host);
        return f.tool.inner()[2].y;
    };
    EXPECT_NEAR(second("Comment: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,First"), second("{\\pos(0,0)}First"), 0.05);
}

TEST(PerspectiveTool, AQuadTheTimeCannotReadIsDropped)
{
    Fixture f({"Dialogue: 0,0:00:02.00,0:00:02.50,Default,,0,0,0,,A{\\fs80}B", "Second"});
    f.host.s->setSelection({f.host.line(1), {f.host.line(1)}, f.host.line(1), std::nullopt});
    f.host.targets = {f.host.line(1)};
    f.tool.reset(f.host);
    ASSERT_TRUE(f.tool.hasQuad());
    const Quad before = f.tool.inner();
    f.host.time = 2200;
    (void)f.tool.overlay(f.host);
    EXPECT_FALSE(f.tool.hasQuad());
    const std::size_t steps = f.steps();
    f.drag(before[2], add(before[2], {10, 10}));
    EXPECT_EQ(f.steps(), steps);
    f.host.time = 2700;
    (void)f.tool.overlay(f.host);
    EXPECT_TRUE(f.tool.hasQuad());
    expectQuadNear(f.tool.inner(), before, 0.05);
}

TEST(PerspectiveTool, ADroppedPlaneComesBackWhereItWas)
{
    Fixture f({"Dialogue: 0,0:00:02.00,0:00:02.50,Default,,0,0,0,,A{\\fs80}B",
               "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,First", "Second"});
    f.host.s->setSelection({f.host.line(2), {f.host.line(2)}, f.host.line(2), std::nullopt});
    f.host.targets = {f.host.line(2)};
    f.tool.reset(f.host);
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    ASSERT_TRUE(f.tool.setOption("lockOuter", 1, f.host));
    const PointF corner = f.tool.outer()[0];
    f.drag(corner, {corner.x - 20, corner.y - 100});
    const Quad plane = f.tool.outer();
    const float bottom = f.tool.inner()[2].y;
    f.host.time = 2200;
    (void)f.tool.overlay(f.host);
    ASSERT_FALSE(f.tool.hasQuad());
    f.host.time = 2700;
    (void)f.tool.overlay(f.host);
    ASSERT_TRUE(f.tool.hasQuad());
    ASSERT_LT(f.tool.inner()[2].y, bottom - 1);
    expectQuadNear(f.tool.outer(), plane, 0.1);
}

TEST(PerspectiveTool, AStillLineIsNotReadEveryFrame)
{
    Fixture f;
    const int calls = f.host.measure.calls;
    for (const std::int64_t t : {1600, 1700, 1800}) {
        f.host.time = t;
        (void)f.tool.overlay(f.host);
    }
    EXPECT_EQ(f.host.measure.calls, calls);
    EXPECT_TRUE(f.tool.setValue("frz", u"10", f.host));
    f.host.time = 1900;
    (void)f.tool.overlay(f.host);
    EXPECT_GT(f.host.measure.calls, calls);
    EXPECT_NEAR(f.tool.transform().frz, 10, 1e-3);

    Fixture g({"Unpositioned"});
    const int before = g.host.measure.calls;
    g.host.time = 1600;
    (void)g.tool.overlay(g.host);
    EXPECT_GT(g.host.measure.calls, before);
}

TEST(PerspectiveTool, AnEditOfTooMuchTextIsRefused)
{
    std::string big = "{\\pos(320,200)";
    while (big.size() < 600 * 1024)
        big += "\\frx0";
    Fixture f({"{\\pos(320,200)}Perspective", big + "}B"});
    f.host.targets = {f.host.line(0), f.host.line(1)};
    const std::size_t steps = f.steps();
    f.drag({430, 200}, {450, 230});
    EXPECT_EQ(f.steps(), steps);
    f.host.targets = {f.host.line(0)};
    f.drag({430, 200}, {450, 230});
    EXPECT_EQ(f.steps(), steps + 1);
}

TEST(PerspectiveTool, ALockedResizeWritesNoTags)
{
    Fixture f;
    ASSERT_TRUE(f.tool.setOption("plane", 1, f.host));
    ASSERT_TRUE(f.tool.setOption("lockOuter", 1, f.host));
    const std::string text = f.host.text();
    const std::size_t steps = f.steps();
    const PointF corner = f.tool.outer()[2];
    f.drag(corner, {corner.x + 30, corner.y + 20});
    EXPECT_LT(dist(f.tool.outer()[2], {corner.x + 30, corner.y + 20}), 0.1);
    EXPECT_EQ(f.host.text(), text);
    EXPECT_EQ(f.steps(), steps);
}

TEST(PerspectiveTool, ARescaleIsRefusedWhileATargetChangesItsOutlineAfterItsText)
{
    Fixture f({"{\\pos(320,200)}Perspective", "{\\pos(320,200)\\bord2}A{\\bord10}B"});
    f.host.targets = {f.host.line(0), f.host.line(1)};
    const std::size_t steps = f.steps();
    f.drag({430, 200}, {450, 230});
    EXPECT_EQ(f.steps(), steps);
    EXPECT_EQ(f.host.text(1), "{\\pos(320,200)\\bord2}A{\\bord10}B");
    EXPECT_TRUE(f.tool.setValue("frz", u"10", f.host));
    EXPECT_EQ(f.steps(), steps + 1);
}

TEST(PerspectiveTool, ARescaleIsRefusedWhileATargetResetsOrRescalesAfterItsText)
{
    for (const std::string other : {"{\\pos(320,200)}A{\\r}B", "{\\pos(320,200)}A{\\fscx50}B",
                                    "{\\pos(320,200)}A{\\fsc}B"}) {
        Fixture f({"{\\pos(320,200)}Perspective", other});
        f.host.targets = {f.host.line(0), f.host.line(1)};
        const std::size_t steps = f.steps();
        f.drag({430, 200}, {450, 230});
        EXPECT_EQ(f.steps(), steps) << other;
        EXPECT_EQ(f.host.text(1), other);
    }
}

TEST(PerspectiveTool, ARescaleIsRefusedWhileATargetAnimatesItsBox)
{
    const std::string other = "{\\pos(320,200)\\bord2\\t(0,1000,\\bord10)}Other";
    Fixture f({"{\\pos(320,200)}Perspective", other});
    f.host.targets = {f.host.line(0), f.host.line(1)};
    const std::size_t steps = f.steps();
    f.drag({430, 200}, {450, 230});
    EXPECT_EQ(f.steps(), steps);
    EXPECT_EQ(f.host.text(1), other);
}

TEST(PerspectiveTool, APlaneFarOffTheViewIsDashedOnlyAcrossIt)
{
    for (const std::string line :
         {"{\\q2\\pos(320,200)\\fscx10000000}Perspective", "{\\pos(100000000,200)}Perspective",
          "{\\pos(320,200)\\p1}m 0 0 l 100000000 0 100000000 100 0 100",
          "{\\an7\\pos(0,0)\\p1}m 0 0 l 1e20 0 1e20 100 0 100"}) {
        Fixture f({line});
        ASSERT_TRUE(f.tool.hasQuad()) << line;
        ASSERT_TRUE(f.tool.setOption("plane", 1, f.host)) << line;
        EXPECT_LT(f.tool.overlay(f.host).lines.size(), 2000u) << line;
    }
}

TEST(PerspectiveTool, IsTheTwelfthFamily)
{
    EXPECT_EQ(familyInfo(Family::Perspective).tooltip, "Perspective");
    EXPECT_EQ(familyInfo(Family::Perspective).history, "Visual perspective tool");
    EXPECT_EQ(familyInfo(Family::Perspective).hotkey, "VIDEO_TOOL_PERSPECTIVE");
    EXPECT_NE(dynamic_cast<PerspectiveTool *>(makeVisualTool(Family::Perspective).get()), nullptr);
    const auto &order = railOrder();
    EXPECT_EQ(order[5], Family::RotationXY);
    EXPECT_EQ(order[6], Family::Perspective);
    EXPECT_EQ(order[7], Family::RectangleClip);
}

}
