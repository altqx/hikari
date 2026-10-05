// T3: Scale and the Z and X/Y rotations. The tools replay the legacy probe's
// captures (tools/legacy-capture/visual_t3_capture.cpp over
// inputs/visual-t3-cases.txt; tests/fixtures/legacy-observations/
// local-t3-visual-20261005): every Line's text and translation after each
// step, the editor's text on the one-Line path and the tools' handles must
// be legacy's exactly.

#include "hikari/application/automation_services.h"
#include "hikari/application/visual_rotation.h"
#include "hikari/application/visual_scale.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_transform.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
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

std::vector<core::LineId> ids(const EditSession &s)
{
    std::vector<core::LineId> out;
    for (const auto *l : s.document().lines())
        out.push_back(l->id);
    return out;
}

const core::LineRecord *lineOf(const EditSession &s, core::LineId id)
{
    for (const auto *l : s.document().lines())
        if (l->id == id)
            return l;
    return nullptr;
}

// The probe's GetLineTextExtents (visual_t3_capture.cpp): a character half
// the font size wide, the font size high, a descent of a fifth and a leading
// of a tenth, times ScaleX / ScaleY. The fields are Styles::GetRaw's.
class FixedMeasure : public TextMeasurePort {
public:
    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        const auto number = [](const std::string &s) {
            char *end = nullptr;
            const double v = std::strtod(s.c_str(), &end);
            return (end != s.c_str() && *end == '\0') ? v : static_cast<double>(std::atoi(s.c_str()));
        };
        const double fs = number(style[2]);
        const double sx = number(style[11]) / 100.0;
        const double sy = number(style[12]) / 100.0;
        const std::u16string u = core::toUtf16(std::u8string(text.begin(), text.end()));
        TextExtents e;
        e.width = static_cast<float>(static_cast<double>(u.size()) * fs * 0.5 * sx);
        e.height = static_cast<float>(fs * sy);
        e.descent = static_cast<float>(fs * 0.2 * sy);
        e.externalLeading = static_cast<float>(fs * 0.1 * sy);
        return e;
    }
};

class TestHost : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    std::vector<core::LineId> targets;
    std::int64_t time = 0;
    LegacyTimebase base;
    mutable FixedMeasure measure;
    std::pair<long, long> caret{0, 0};
    std::vector<std::u16string> logged;
    // Every history step as legacy recorded its commit: "dummy <action>" for
    // the several-Line path's SetModified, "<action>" for the one-Line path's
    // EditBox::Send (Visuals.cpp:791-796, 820-825); the action is the
    // history's VISUAL_* number (SubsFile.h:67-69).
    std::vector<std::string> history, sent;
    // A refusal for the next commit (a stale revision, a draft that cannot
    // commit): nothing is written.
    std::optional<CommandRefusal> refuse;
    // What the app does when a commit changed the Document, before the
    // commit returns: VisualToolsController::commitGesture's m_edited reloads
    // the Line editor and refresh() resets the tool on a new revision unless
    // the tool keeps its state, as legacy's Scale and rotations did.
    std::function<void()> edited;

    const VideoView &view() const override { return v; }
    const EditSession *session() const override { return s.get(); }
    std::optional<core::LineId> activeLine() const override { return s ? s->selection().active : std::nullopt; }
    std::vector<core::LineId> batchTargets() const override { return targets; }
    std::expected<Gesture *, CommandRefusal> beginGesture(std::vector<core::LineId> t, std::string history) override
    {
        if (!s || g)
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
        const auto &name = g->history();
        const std::string action = name == familyInfo(Family::Scale).history        ? "40"
                                   : name == familyInfo(Family::RotationZ).history ? "41"
                                   : name == familyInfo(Family::RotationXY).history ? "42"
                                                                                    : name;
        const bool several = !(g->targets().size() == 1 && g->targets().front() == activeLine());
        if (refuse) {
            g.reset();
            const CommandRefusal r = *refuse;
            refuse.reset();
            return std::unexpected(r);
        }
        const std::size_t steps = s->historySize();
        auto r = g->commit(*s);
        g.reset();
        if (s->historySize() > steps) {
            (several ? history : sent).push_back(several ? "dummy " + action : action);
            if (edited)
                edited();
        }
        return r;
    }
    void cancelGesture() override { g.reset(); }
    std::pair<int, int> measureLabel(std::u16string_view text) const override
    {
        return {7 * static_cast<int>(text.size()), 14};
    }
    void toolChanged() override {}
    std::int64_t videoTimeMs() const override { return time; }
    LegacyTimebase timebase() const override { return base; }
    TextMeasurePort *textMeasure() const override { return &measure; }
    std::pair<long, long> editorSelection() const override { return caret; }
    void setEditorSelection(long from, long to) override { caret = {from, to}; }
    void log(std::u16string_view text) override { logged.emplace_back(text); }
};

std::string assTime(int ms)
{
    char buf[32];
    const int cs = ms / 10;
    std::snprintf(buf, sizeof buf, "%d:%02d:%02d.%02d", cs / 360000, (cs / 6000) % 60, (cs / 100) % 60, cs % 100);
    return buf;
}

// One case of inputs/visual-t3-cases.txt.
struct Case {
    std::string name;
    int clientW = 0, clientH = 0, panel = 0;
    SourceGeometry frame;
    std::vector<std::string> viewZoom;
    IntRect rect;
    PointF zoomMove{0, 0}, zoomScale{1, 1};
    int scriptW = 0, scriptH = 0;
    int time = 0;
    double fps = 0;
    bool tl = false;
    std::vector<std::string> styles, lines;
    std::vector<std::string> translations;
    int active = 0;
    std::vector<int> select;
    long caretFrom = 0, caretTo = 0;
    std::vector<std::string> ops; // tool and later
};

std::vector<Case> readCases()
{
    std::ifstream in(HIKARI_VISUAL_T3_CASES);
    std::vector<Case> out;
    Case c;
    std::string text;
    bool inOps = false;
    while (std::getline(in, text)) {
        std::istringstream line(text);
        std::string key;
        if (!(line >> key) || key[0] == '#')
            continue;
        if (key == "case") {
            c = Case{};
            inOps = false;
            line >> c.name;
        } else if (key == "end") {
            out.push_back(c);
        } else if (key == "tool" || inOps) {
            inOps = true;
            c.ops.push_back(text);
        } else if (key == "client") {
            line >> c.clientW >> c.clientH >> c.panel;
        } else if (key == "frame") {
            line >> c.frame.width >> c.frame.height >> c.frame.sarNum >> c.frame.sarDen;
        } else if (key == "viewzoom") {
            c.viewZoom.push_back(text);
        } else if (key == "rect") {
            line >> c.rect.left >> c.rect.top >> c.rect.right >> c.rect.bottom;
        } else if (key == "zoom") {
            line >> c.zoomMove.x >> c.zoomMove.y >> c.zoomScale.x >> c.zoomScale.y;
        } else if (key == "script") {
            line >> c.scriptW >> c.scriptH;
        } else if (key == "time") {
            line >> c.time;
        } else if (key == "fps") {
            line >> c.fps;
        } else if (key == "tlmode") {
            c.tl = true;
        } else if (key == "style") {
            std::string name, fs, sx, sy, angle, outline, shadow, an, ml, mr, mv;
            line >> name >> fs >> sx >> sy >> angle >> outline >> shadow >> an >> ml >> mr >> mv;
            // Styles() but for these fields (styles.cpp:249-275).
            c.styles.push_back("Style: " + name + ",Garamond," + fs + ",&H00FFFFFF,&H00000000,&H00FF0000,&H00000000,0,0,0,0," +
                               sx + "," + sy + ",0," + angle + ",0," + outline + "," + shadow + "," + an + "," + ml + "," + mr +
                               "," + mv + ",1");
        } else if (key == "line") {
            int start, end, ml, mr, mv, comment;
            std::string style;
            line >> start >> end >> style >> ml >> mr >> mv >> comment;
            const auto bar = text.find('|');
            const auto bar2 = text.find('|', bar + 1);
            const std::string body = text.substr(bar + 1, bar2 == std::string::npos ? std::string::npos : bar2 - bar - 1);
            c.translations.push_back(bar2 == std::string::npos ? std::string() : text.substr(bar2 + 1));
            c.lines.push_back(std::string(comment ? "Comment: " : "Dialogue: ") + "0," + assTime(start) + "," + assTime(end) +
                              "," + style + ",," + std::to_string(ml) + "," + std::to_string(mr) + "," +
                              std::to_string(mv) + ",," + body);
        } else if (key == "active") {
            line >> c.active;
        } else if (key == "select") {
            int i;
            while (line >> i)
                c.select.push_back(i);
        } else if (key == "caret") {
            line >> c.caretFrom >> c.caretTo;
        }
    }
    return out;
}

std::map<std::string, std::vector<QJsonObject>> readObservations()
{
    QFile f(QStringLiteral(HIKARI_VISUAL_T3_OBSERVATIONS));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    std::map<std::string, std::vector<QJsonObject>> out;
    for (const QByteArray &l : f.readAll().split('\n')) {
        if (l.trimmed().isEmpty())
            continue;
        const QJsonObject o = QJsonDocument::fromJson(l).object();
        out[o[QStringLiteral("case")].toString().toStdString()].push_back(o);
    }
    return out;
}

float f(const QJsonValue &v)
{
    return static_cast<float>(v.toDouble());
}

PointF pair(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    return {f(a[0]), f(a[1])};
}

std::u8string u8(const QJsonValue &v)
{
    return core::toUtf8(v.toString().toStdU16String());
}

void setUp(TestHost &host, const Case &c)
{
    host.v.setClient(c.clientW, c.clientH, c.panel);
    host.v.setScript(c.scriptW, c.scriptH);
    host.v.open(c.frame);
    for (const std::string &op : c.viewZoom) {
        std::istringstream in(op);
        std::string kind;
        float percent;
        int x, y;
        in >> kind >> percent >> x >> y;
        host.v.zoomAt(percent, x, y);
        host.v.refreshToolTransform();
    }
    std::string script = "[Script Info]\nScriptType: v4.00+\nPlayResX: " + std::to_string(c.scriptW) +
                         "\nPlayResY: " + std::to_string(c.scriptH) + "\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, "
                         "PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
                         "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, "
                         "MarginV, Encoding\n";
    for (const auto &s : c.styles)
        script += s + "\n";
    script += "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (const auto &l : c.lines)
        script += l + "\n";
    host.s = std::make_unique<EditSession>(load(script));
    const auto all = ids(*host.s);
    ASSERT_EQ(all.size(), c.lines.size());
    ASSERT_TRUE(host.s->run({"set up", host.s->revision(), {all.begin(), all.end()}, [&](core::Document &d) {
                                 if (c.tl)
                                     d.setScriptInfo(u8"TLMode", u8"Yes");
                                 for (std::size_t i = 0; i < all.size(); ++i)
                                     if (!d.editLine(all[i], [&](core::LineRecord &l) {
                                             l.translation = std::u8string(c.translations[i].begin(), c.translations[i].end());
                                         }))
                                         return false;
                                 return true;
                             }}));
    const core::LineId active = all[static_cast<std::size_t>(c.active)];
    host.s->setSelection({active, {active}, active, std::nullopt});
    host.targets.clear();
    if (c.select.empty())
        host.targets.push_back(active);
    for (int i : c.select)
        host.targets.push_back(all[static_cast<std::size_t>(i)]);
    host.time = c.time;
    host.base = LegacyTimebase({}, c.fps);
    host.caret = {c.caretFrom, c.caretTo};
}

Pointer pointerOf(const std::string &kind, std::istringstream &in)
{
    Pointer p;
    in >> p.x >> p.y;
    p.kind = kind == "press" ? Pointer::Kind::Press : kind == "release" ? Pointer::Kind::Release : Pointer::Kind::Move;
    std::string word;
    while (in >> word) {
        if (word == "shift") {
            p.shift = true;
            continue;
        }
        const Pointer::Button b = word == "left" ? Pointer::Button::Left
                                  : word == "right" ? Pointer::Button::Right
                                                     : Pointer::Button::Middle;
        if (p.kind != Pointer::Kind::Move)
            p.button = b;
        if (p.kind != Pointer::Kind::Release) {
            p.leftDown = p.leftDown || b == Pointer::Button::Left;
            p.rightDown = p.rightDown || b == Pointer::Button::Right;
            p.middleDown = p.middleDown || b == Pointer::Button::Middle;
        }
    }
    return p;
}

// The editor's text as legacy's TextEdit held it: the gesture's staged text
// while one runs, else the Line's (the translation in TLMode unless empty).
std::u8string editorText(const TestHost &host, const Case &c)
{
    const core::LineId active = *host.s->selection().active;
    const core::LineRecord *line = lineOf(*host.s, active);
    const bool tl = c.tl && !line->translation.empty();
    if (host.g)
        if (const auto staged = host.g->staged(active, tl))
            return *staged;
    return tl ? line->translation : line->text;
}

// The approved departures (docs/qt/compatibility-decisions.md, the T3 rows)
// where the capture holds legacy's defect: the rewrite gives `expected`, and
// legacy's text, kept as the old evidence, must still differ from it.
struct Departure {
    const char *entry;
    const char *caseName;
    std::size_t step;
    std::size_t line;
    const char8_t *expected;
};

const std::vector<Departure> &departures()
{
    static const std::vector<Departure> table{
        // Scale keeps the vector clip's scale and moves its points (in
        // half pixels) about the position by 1.2, then 1.21 after D.
        {"T3-scale-vector-clip", "scale-several-preserve-proportions", 1, 2,
         u8"{\\fscy120\\fscx120\\pos(768,252)\\clip(2,m -384 -216 l -264 -216 -264 -96 -384 -96)}Third"},
        {"T3-scale-vector-clip", "scale-several-preserve-proportions", 2, 2,
         u8"{\\fscy121\\fscx121\\pos(766.4,249.6)\\clip(2,m -403 -226 l -282 -226 -282 -105 -403 -105)}Third"},
        // The \org after \an7 is replaced, \frz30 kept.
        {"T3-change-org-end", "rotz-org-several", 0, 1, u8"{\\an7\\org(40,50)\\frz30}Second"},
        // The rectangle turned without growing: its bottom corners are
        // legacy's less 2 turned by the angle.
        {"T3-rotz-rect-clip", "rotz-several-preserve", 1, 1,
         u8"{\\pos(1050.922,777.889)\\clip(m 521 -265 l 667 -128 530 18 384 -119)\\frz1.941}Second"},
        // A 10-pixel drag at 250% with the coefficient 3 moves the \org by
        // 12; legacy added the zoom's offset (72, 48) times 3.
        {"T3-org-zoom", "rotz-zoom-org", 1, 0, u8"{\\org(972,552)\\pos(960,540)}Zoom"},
        {"T3-change-org-end, T3-org-zoom", "rotz-zoom-org", 1, 1, u8"{\\pos(30,40)\\org(112,112)}Other"},
    };
    return table;
}

const Departure *departureFor(const std::string &caseName, std::size_t step, std::size_t line)
{
    for (const Departure &d : departures())
        if (caseName == d.caseName && step == d.step && line == d.line)
            return &d;
    return nullptr;
}

} // namespace

TEST(VisualTransformText, GetfloatAndNumbersFollowLegacy)
{
    // getfloat (config.cpp:1085-1101).
    EXPECT_EQ(transform::getfloat(150.f), u"150");
    EXPECT_EQ(transform::getfloat(98.9f), u"98.9");
    EXPECT_EQ(transform::getfloat(1.0006f), u"1.001");
    EXPECT_EQ(transform::getfloat(1.0005f), u"1"); // 1.00049996 as a float
    EXPECT_EQ(transform::getfloat(-0.0001f), u"-0");
    EXPECT_EQ(transform::getfloat(960.f, "6.0f"), u"960");
    EXPECT_EQ(transform::getfloat(10.5f, "5.0f"), u"10");
    double v = 7;
    EXPECT_FALSE(transform::toDouble(u"12px", v));
    EXPECT_EQ(v, 7);
    EXPECT_TRUE(transform::toDouble(u" 12.5", v));
    EXPECT_EQ(v, 12.5);
    EXPECT_EQ(transform::atof(u"-3.5x"), -3.5);
    EXPECT_EQ(transform::atoi(u"8an"), 8);
}

TEST(VisualTransformText, ReplaceAllAddsTheFirstBlocksTag)
{
    // TagFindReplace::ReplaceAll with returnPosWhenNoTags (TagFindReplace.cpp:290-302):
    // a text that starts without a block gets one even when later blocks have the tag.
    transform::TagFind find;
    std::u16string text = u"Hello{\\fscx50}x";
    const int n = find.replaceAll(u"fscx([0-9.-]+)", u"fscx", text,
                                  [](const transform::FindData &d, std::u16string &out) {
                                      out = d.finding.empty() ? u"100" : u"200";
                                  },
                                  true);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(text, u"{\\fscx100}Hello{\\fscx200}x");
}

// T3-scale-vector-clip and T3-rotz-rect-clip: Scale keeps a vector clip's
// scale and scales its points about the pivot in the vector's units (legacy
// wrote `\clip(m ...)`); RotationZ's rewrite leaves a rectangle's size alone
// (legacy added 2 to the bottom on every edit).
TEST(VisualTransformText, ClipRewritesKeepTheScaleAndTheRectangle)
{
    transform::TagFind find;
    std::u16string vector = u"{\\clip(2,m 0 0 l 200 0 200 100 0 100)}A";
    // Twice the size about (50, 25) pixels: (100, 50) in half pixels.
    transform::changeClipScale(find, vector, {50, 25}, 2.f, 2.f);
    EXPECT_EQ(vector, u"{\\clip(2,m -100 -50 l 300 -50 300 150 -100 150)}A");

    std::u16string unscaled = u"{\\iclip(m 10 10 l 20 10 20 20)}A";
    transform::changeClipScale(find, unscaled, {10, 10}, 2.f, 1.f);
    EXPECT_EQ(unscaled, u"{\\iclip(m 10 10 l 30 10 30 20)}A");

    // No turn, twice: the rectangle (written as a vector) keeps its corners.
    std::u16string rect = u"{\\clip(100,50,300,150)}A";
    transform::changeClipRotationZ(find, rect, {0, 0}, 0.f, 1.f);
    EXPECT_EQ(rect, u"{\\clip(m 100 50 l 300 50 300 150 100 150)}A");
    transform::changeClipRotationZ(find, rect, {0, 0}, 0.f, 1.f);
    EXPECT_EQ(rect, u"{\\clip(m 100 50 l 300 50 300 150 100 150)}A");
}

// T3-change-org-end: ChangeOrg replaces an \org that is not the block's
// first tag and keeps what follows it (legacy: `{\an7\org(40,50)30}`).
TEST(VisualTransformText, ChangeOrgReplacesALaterOrgExactly)
{
    const auto cases = readCases();
    const Case c = *std::find_if(cases.begin(), cases.end(), [](const Case &k) { return k.name == "rotz-org-several"; });
    TestHost host;
    setUp(host, c);
    const transform::Context ctx = transform::context(host);
    for (const auto &[before, after] : std::vector<std::pair<std::u16string, std::u16string>>{
             {u"{\\an7\\org(10,20)\\frz30}Second", u"{\\an7\\org(40,50)\\frz30}Second"},
             {u"{\\pos(30,40)\\org(100,100)}Other", u"{\\pos(30,40)\\org(130,130)}Other"},
             {u"{\\org(1,2)}x", u"{\\org(31,32)}x"}}) {
        transform::TagFind find;
        std::u16string text = before;
        transform::changeOrg(ctx, find, text, *lineOf(*host.s, *host.activeLine()), 30.f, 30.f);
        EXPECT_EQ(text, after);
    }
}

// T3-two-points-change-all: choosing RotationZ with "two points" and
// "change all" both on takes the two-point mode (legacy's ChangeTool did not
// when "change all" changed in the same call, VisualRotationZ.cpp:422-441).
TEST(VisualTransformEdit, TwoPointsHoldWithChangeAll)
{
    const auto cases = readCases();
    const Case c = *std::find_if(cases.begin(), cases.end(), [](const Case &k) { return k.name == "rotz-two-points"; });
    TestHost host;
    setUp(host, c);
    RotationZTool tool;
    tool.setToggled(1 | 2);
    tool.selected(host);
    tool.reset(host);
    EXPECT_TRUE(tool.hasTwoPoints());
    // "Change all" off keeps it, "two points" off drops it, on takes it again.
    tool.setToggled(1, &host);
    EXPECT_TRUE(tool.hasTwoPoints());
    tool.setToggled(2, &host);
    EXPECT_FALSE(tool.hasTwoPoints());
    tool.setToggled(3, &host);
    EXPECT_TRUE(tool.hasTwoPoints());
}

TEST(VisualCapture, ReplaysTheLegacyT3Probe)
{
    const auto observations = readObservations();
    const auto cases = readCases();
    ASSERT_EQ(cases.size(), observations.size());
    int states = 0;
    std::size_t departed = 0;
    for (const Case &c : cases) {
        SCOPED_TRACE(c.name);
        ASSERT_TRUE(observations.contains(c.name));
        const auto &steps = observations.at(c.name);
        TestHost host;
        setUp(host, c);
        // The view as legacy's renderer had it.
        ASSERT_EQ(host.v.videoRect(), c.rect);
        ASSERT_EQ(host.v.zoomMove(), c.zoomMove);
        ASSERT_EQ(host.v.zoomScale(), c.zoomScale);
        std::unique_ptr<VisualTool> tool;
        std::size_t step = 0;
        std::size_t noStepSends = 0; // below
        host.edited = [&] {
            if (!tool->keepsStateAfterCommit())
                tool->reset(host);
        };
        bool several = !c.select.empty() && !(c.select.size() == 1 && c.select[0] == c.active);
        for (const std::string &op : c.ops) {
            std::istringstream in(op);
            std::string kind;
            in >> kind;
            SCOPED_TRACE(op);
            if (kind == "tool") {
                std::string name;
                int bits;
                in >> name >> bits;
                if (name == "scale") {
                    auto t = std::make_unique<ScaleTool>();
                    t->setToggled(bits);
                    tool = std::move(t);
                } else if (name == "rotz") {
                    auto t = std::make_unique<RotationZTool>();
                    t->setToggled(bits);
                    tool = std::move(t);
                } else {
                    auto t = std::make_unique<RotationXYTool>();
                    t->setToggled(bits);
                    tool = std::move(t);
                }
                tool->selected(host);
                tool->reset(host);
            } else if (kind == "option") {
                int bits;
                in >> bits;
                if (auto *t = dynamic_cast<ScaleTool *>(tool.get()))
                    t->setToggled(bits, &host);
                else if (auto *z = dynamic_cast<RotationZTool *>(tool.get()))
                    z->setToggled(bits, &host);
                else
                    dynamic_cast<RotationXYTool *>(tool.get())->setToggled(bits, &host);
            } else if (kind == "setvisual") {
                // RendererVideo::SetVisual after the editor took the Line again.
                host.caret = {c.caretFrom, c.caretTo};
                tool->reset(host);
            } else if (kind == "select") {
                const auto all = ids(*host.s);
                host.targets.clear();
                std::vector<int> picked;
                int i;
                while (in >> i) {
                    picked.push_back(i);
                    host.targets.push_back(all[static_cast<std::size_t>(i)]);
                }
                several = !(picked.size() == 1 && picked[0] == c.active);
            } else if (kind == "press" || kind == "move" || kind == "release") {
                tool->pointer(pointerOf(kind, in), host);
            } else if (kind == "key") {
                std::string k;
                in >> k;
                Key key;
                key.key = k[0];
                std::string word;
                while (in >> word) {
                    key.shift = key.shift || word == "shift";
                    key.alt = key.alt || word == "alt";
                    key.control = key.control || word == "ctrl";
                }
                (void)tool->key(key, host);
            } else if (kind == "draw") {
                (void)tool->overlay(host);
            } else if (kind == "state") {
                ASSERT_LT(step, steps.size());
                const QJsonObject &o = steps[step++];
                ++states;
                SCOPED_TRACE("step " + std::to_string(step - 1));
                // Every Line as legacy's grid held it.
                const QJsonArray lines = o[QStringLiteral("lines")].toArray();
                const auto all = ids(*host.s);
                ASSERT_EQ(static_cast<std::size_t>(lines.size()), all.size());
                for (std::size_t i = 0; i < all.size(); ++i) {
                    const QJsonArray l = lines[static_cast<qsizetype>(i)].toArray();
                    if (const Departure *d = departureFor(c.name, step - 1, i)) {
                        ++departed;
                        EXPECT_NE(u8(l[0]), d->expected) << d->entry << ": legacy's text is the old evidence";
                        EXPECT_EQ(lineOf(*host.s, all[i])->text, d->expected) << d->entry << ", line " << i;
                        EXPECT_EQ(lineOf(*host.s, all[i])->translation, u8(l[1])) << "line " << i;
                        continue;
                    }
                    EXPECT_EQ(lineOf(*host.s, all[i])->text, u8(l[0])) << "line " << i;
                    EXPECT_EQ(lineOf(*host.s, all[i])->translation, u8(l[1])) << "line " << i;
                }
                // The editor's text and caret, on the one-Line path. Legacy
                // moved the caret to the tag on every sample (Visuals.cpp:
                // 813-815); the rewrite's editor shows the Document, so the
                // tool holds the caret while the gesture runs and the editor
                // takes it with the written text on release.
                const QJsonArray caret = o[QStringLiteral("caret")].toArray();
                const std::pair<long, long> legacyCaret{caret[0].toInteger(), caret[1].toInteger()};
                if (!several) {
                    EXPECT_EQ(editorText(host, c), u8(o[QStringLiteral("editor")]));
                    std::pair<long, long> held;
                    if (const auto *s = dynamic_cast<const ScaleTool *>(tool.get()))
                        held = s->editorCaret();
                    else if (const auto *z = dynamic_cast<const RotationZTool *>(tool.get()))
                        held = z->editorCaret();
                    else
                        held = dynamic_cast<const RotationXYTool *>(tool.get())->editorCaret();
                    EXPECT_EQ(held, legacyCaret) << "the tool's caret";
                }
                if (!host.g)
                    EXPECT_EQ(host.caret, legacyCaret) << "the editor's caret";
                // The commits (SetModified, Send) and the log, in order.
                const auto strings = [](const QJsonValue &v) {
                    std::vector<std::string> out;
                    for (const QJsonValue &e : v.toArray())
                        out.push_back(e.toString().toStdString());
                    return out;
                };
                // Legacy committed on every release (RotationZ::OnMouseEvent,
                // VisualRotationZ.cpp:150-152), so the release after the
                // two-point angle's first point and after a click off both
                // points sent the unchanged text, and that made an undo step:
                // CopyDialogue marks the file edited (SubsFile.cpp:408-417)
                // and SetModified saves the undo state (SubsGridBase.cpp:
                // 1125-1157). The rewrite opens no gesture there, and a
                // release with nothing staged records nothing (visual-tools.md,
                // the gesture rule): those steps are left out here, a
                // difference named in the report.
                if (c.name == "rotz-two-points" && (step - 1 == 1 || step - 1 == 5))
                    ++noStepSends;
                auto legacySent = strings(o[QStringLiteral("sent")]);
                legacySent.resize(legacySent.size() - noStepSends);
                EXPECT_EQ(host.history, strings(o[QStringLiteral("history")]));
                EXPECT_EQ(host.sent, legacySent);
                std::vector<std::string> logged;
                for (const auto &l : host.logged)
                    logged.push_back(QString::fromStdU16String(l).toStdString());
                EXPECT_EQ(logged, strings(o[QStringLiteral("log")]));
                // The handles.
                if (const auto *s = dynamic_cast<const ScaleTool *>(tool.get())) {
                    EXPECT_EQ(s->from(), pair(o[QStringLiteral("from")]));
                    EXPECT_EQ(s->to(), pair(o[QStringLiteral("to")]));
                    EXPECT_EQ(s->scale(), pair(o[QStringLiteral("scale")]));
                    EXPECT_EQ(s->arrowLengths(), pair(o[QStringLiteral("arrowLengths")]));
                    const QJsonArray r = o[QStringLiteral("sizingRectangle")].toArray();
                    for (int i = 0; i < 4; ++i)
                        EXPECT_EQ(s->sizingRectangle()[static_cast<std::size_t>(i)], pair(r[i])) << "corner " << i;
                    EXPECT_EQ(s->originalSize(), pair(o[QStringLiteral("originalSize")]));
                    EXPECT_EQ(s->border(), pair(o[QStringLiteral("border")]));
                    EXPECT_EQ(s->rectangleVisible(), o[QStringLiteral("rectangleVisible")].toBool());
                    EXPECT_EQ(s->originalRectangleVisible(), o[QStringLiteral("originalRectangleVisible")].toBool());
                } else if (const auto *z = dynamic_cast<const RotationZTool *>(tool.get())) {
                    EXPECT_EQ(z->from(), pair(o[QStringLiteral("from")]));
                    EXPECT_EQ(z->to(), pair(o[QStringLiteral("to")]));
                    EXPECT_EQ(z->org(), pair(o[QStringLiteral("org")]));
                    EXPECT_EQ(z->lastmove(), pair(o[QStringLiteral("lastmove")]));
                    EXPECT_EQ(z->lastAngle(), f(o[QStringLiteral("lastAngle")]));
                    EXPECT_EQ(z->hasTwoPoints(), o[QStringLiteral("hasTwoPoints")].toBool());
                    const QJsonArray tp = o[QStringLiteral("twoPoints")].toArray();
                    const QJsonArray vis = o[QStringLiteral("visibility")].toArray();
                    for (int i = 0; i < 2; ++i) {
                        EXPECT_EQ(z->visibility()[static_cast<std::size_t>(i)], vis[i].toBool());
                        if (vis[i].toBool())
                            EXPECT_EQ(z->twoPoints()[static_cast<std::size_t>(i)], pair(tp[i]));
                    }
                } else if (const auto *xy = dynamic_cast<const RotationXYTool *>(tool.get())) {
                    EXPECT_EQ(xy->from(), pair(o[QStringLiteral("from")]));
                    EXPECT_EQ(xy->to(), pair(o[QStringLiteral("to")]));
                    EXPECT_EQ(xy->org(), pair(o[QStringLiteral("org")]));
                    EXPECT_EQ(xy->angle(), pair(o[QStringLiteral("angle")]));
                    EXPECT_EQ(xy->oldAngle(), pair(o[QStringLiteral("oldAngle")]));
                    EXPECT_EQ(xy->firstmove(), pair(o[QStringLiteral("firstmove")]));
                    EXPECT_EQ(xy->type(), o[QStringLiteral("type")].toInt());
                    EXPECT_EQ(xy->an(), o[QStringLiteral("an")].toInt());
                }
            }
        }
        EXPECT_EQ(step, steps.size());
    }
    EXPECT_GT(states, 70);
    EXPECT_EQ(departed, departures().size());
}

// A commit the session refuses (a stale revision, a draft that cannot
// commit) writes nothing, so the editor's caret stays where it was rather
// than going to a tag that was never written, and the tool reads the
// unchanged text again, as after Esc. Legacy had no refusal: its
// SetVisual(false) always sent the editor (Visuals.cpp:817-829).
TEST(VisualTransformEdit, ARefusedCommitLeavesTheCaretAndTheText)
{
    for (const char *name : {"scale-width-left-drag", "rotz-drag", "rotxy-left-right-middle"}) {
        SCOPED_TRACE(name);
        const auto cases = readCases();
        const auto it = std::find_if(cases.begin(), cases.end(), [&](const Case &c) { return c.name == name; });
        ASSERT_NE(it, cases.end());
        Case c = *it;
        c.caretFrom = c.caretTo = 7; // in the first block
        TestHost host;
        setUp(host, c);
        std::unique_ptr<VisualTool> tool;
        if (c.name.starts_with("scale"))
            tool = std::make_unique<ScaleTool>();
        else if (c.name.starts_with("rotz"))
            tool = std::make_unique<RotationZTool>();
        else
            tool = std::make_unique<RotationXYTool>();
        tool->selected(host);
        tool->reset(host);
        const auto handles = [&] {
            if (const auto *s = dynamic_cast<const ScaleTool *>(tool.get()))
                return std::pair{s->to(), s->editorCaret()};
            if (const auto *z = dynamic_cast<const RotationZTool *>(tool.get()))
                return std::pair{z->to(), z->editorCaret()};
            const auto *xy = dynamic_cast<const RotationXYTool *>(tool.get());
            return std::pair{xy->to(), xy->editorCaret()};
        };
        const auto before = handles();
        const std::u8string text = lineOf(*host.s, *host.activeLine())->text;
        const std::size_t steps = host.s->historySize();
        const auto at = [](int x, int y, Pointer::Kind kind, bool down) {
            Pointer p;
            p.x = x;
            p.y = y;
            p.kind = kind;
            p.button = Pointer::Button::Left;
            p.leftDown = down;
            return p;
        };
        // The drags of the cases above: a press on the width arrow (Scale),
        // off the \org (RotationZ), anywhere (RotationXY), then a move.
        const std::pair<int, int> press = c.name.starts_with("rotxy") ? std::pair{100, 100} : std::pair{420, 180};
        tool->pointer(at(press.first, press.second, Pointer::Kind::Press, true), host);
        Pointer move = at(press.first + 40, press.second + 30, Pointer::Kind::Move, true);
        move.button = Pointer::Button::None;
        tool->pointer(move, host);
        ASSERT_TRUE(host.g && host.g->hasChanges());
        EXPECT_NE(handles().second, before.second); // the tool holds the caret at the tag
        host.refuse = CommandRefusal::StaleRevision;
        tool->pointer(at(press.first + 40, press.second + 30, Pointer::Kind::Release, false), host);
        EXPECT_FALSE(host.g);
        EXPECT_EQ(host.s->historySize(), steps);
        EXPECT_EQ(lineOf(*host.s, *host.activeLine())->text, text);
        EXPECT_TRUE(host.sent.empty());
        EXPECT_EQ(host.caret, (std::pair<long, long>{7, 7})); // not put at the unwritten tag
        EXPECT_EQ(handles(), before);                         // the unchanged text read again
    }
}

namespace {

// A case of the probe with its one Line's text replaced.
Case caseWith(const char *name, const std::string &from, const std::string &to)
{
    const auto cases = readCases();
    const auto it = std::find_if(cases.begin(), cases.end(), [&](const Case &c) { return c.name == name; });
    EXPECT_NE(it, cases.end());
    Case c = *it;
    const auto at = c.lines[0].find(from);
    EXPECT_NE(at, std::string::npos);
    c.lines[0].replace(at, from.size(), to);
    return c;
}

void expectNear(PointF a, PointF b, float tolerance = 1e-3f)
{
    EXPECT_NEAR(a.x, b.x, tolerance);
    EXPECT_NEAR(a.y, b.y, tolerance);
}

} // namespace

// RotationZ::DrawVisual (VisualRotationZ.cpp:34-137) with an \org away from
// the position: the ring between the radius (the distance from the \org
// to the position, plus 40) and the radius + 10, its two circles, the angle
// handle, the cross and the line to `to`.
TEST(VisualOverlay, RotationZDrawsLegacysRingAndHandle)
{
    const Case c = caseWith("rotz-drag", "{\\pos(960,540)}Turn", "{\\pos(960,540)\\org(1200,600)\\frz30}Turn");
    TestHost host;
    setUp(host, c);
    RotationZTool tool;
    tool.setToggled(0);
    tool.selected(host);
    tool.reset(host);
    const PointF org{400, 200}, from{320, 180}; // the coefficient 3
    expectNear(tool.org(), org);
    expectNear(tool.from(), from);
    EXPECT_EQ(tool.lastmove().y, 30.f);
    const Overlay o = tool.overlay(host);
    const float rad = 0.01745329251994329576923690768489f;
    const float radius = std::sqrt(80.f * 80.f + 20.f * 20.f) + 40;
    // The triangle strip v5[0..361] (lines 66-77): the outer and inner
    // vertices at 2j degrees, x by sin and y by cos, as one polygon (outer
    // forwards, inner backwards) that fills the same annulus.
    ASSERT_EQ(o.polygons.size(), 1u);
    const OverlayPolygon &ring = o.polygons[0];
    EXPECT_EQ(ring.fill, 0xAA121150u);
    ASSERT_EQ(ring.points.size(), 362u);
    for (int j = 0; j < 181; ++j) {
        SCOPED_TRACE(j);
        const float a = static_cast<float>(j * 2) * rad;
        expectNear(ring.points[static_cast<std::size_t>(j)],
                   {org.x + (radius + 10.f) * std::sin(a), org.y + (radius + 10.f) * std::cos(a)});
        expectNear(ring.points[static_cast<std::size_t>(361 - j)], {org.x + radius * std::sin(a), org.y + radius * std::cos(a)});
    }
    // Its area is the annulus's (two 180-gons), so the inner circle is a hole
    // whatever the fill rule: the outer and inner runs wind opposite ways.
    double area = 0;
    for (std::size_t i = 0; i < ring.points.size(); ++i) {
        const PointF a = ring.points[i], b = ring.points[(i + 1) % ring.points.size()];
        area += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    const double gon = 90.0 * std::sin(2.0 * 3.14159265358979323846 / 180.0);
    EXPECT_NEAR(std::fabs(area / 2), gon * ((radius + 10.0) * (radius + 10.0) - radius * radius), 0.5);
    // The two circles (LINESTRIP over v5[364..544] and v5[545..725]), then
    // the handle (lines 78-106), the cross and org to `to` (108-135).
    ASSERT_EQ(o.lines.size(), 360u + 3u + 3u);
    for (int j = 0; j < 180; ++j) {
        const OverlayLine &outer = o.lines[static_cast<std::size_t>(2 * j)];
        const OverlayLine &inner = o.lines[static_cast<std::size_t>(2 * j + 1)];
        EXPECT_EQ(outer.argb, 0xAAFF0000u);
        EXPECT_EQ(inner.argb, 0xAAFF0000u);
        expectNear(outer.from, ring.points[static_cast<std::size_t>(j)]);
        expectNear(outer.to, ring.points[static_cast<std::size_t>(j + 1)]);
        expectNear(inner.from, ring.points[static_cast<std::size_t>(361 - j)]);
        expectNear(inner.to, ring.points[static_cast<std::size_t>(360 - j)]);
    }
    const float xx1 = org.x + ((radius - 40) * std::sin(30.f * rad));
    const float yy1 = org.y + ((radius - 40) * std::cos(30.f * rad));
    const OverlayLine *l = &o.lines[360];
    EXPECT_EQ(l[0].width, 10.f);
    EXPECT_EQ(l[0].argb, 0xAAFF0000u);
    expectNear(l[0].from, {xx1 - 5.f, yy1});
    expectNear(l[0].to, {xx1 + 5.f, yy1});
    expectNear(l[1].from, org);
    expectNear(l[1].to, {xx1, yy1});
    expectNear(l[2].from, {xx1 + radius * std::sin(120.f * rad), yy1 + radius * std::cos(120.f * rad)});
    expectNear(l[2].to, {xx1 + radius * std::sin(-60.f * rad), yy1 + radius * std::cos(-60.f * rad)});
    expectNear(l[3].from, {org.x - 10, org.y});
    expectNear(l[3].to, {org.x + 10, org.y});
    expectNear(l[4].from, {org.x, org.y - 10});
    expectNear(l[4].to, {org.x, org.y + 10});
    expectNear(l[5].from, org);
    expectNear(l[5].to, tool.to());
    for (int i = 1; i < 6; ++i) {
        EXPECT_EQ(l[i].width, 2.f);
        EXPECT_EQ(l[i].argb, 0xFFBB0000u);
    }
}

// RotationXY::DrawVisual's grid (VisualRotationXY.cpp:37-181) against the
// D3DX pipeline worked by hand: the world D3DXMatrixRotationYawPitchRoll
// (-angle.x, angle.y, 0) (after the translation to the position when it is
// not the \org), the view D3DXMatrixLookAtLH from (0, 0, -17.2) (a model
// point at depth z + 17.2), the projection D3DXMatrixPerspectiveFovLH(120
// degrees, the window's ratio) translated by the \org in clip space. So a
// point (x, y, z) of the turned model lands at org + (x * xs, -y * ys) / d
// * (W, H) / 2, with ys = 1 / tan(60 degrees), xs = ys / (W / H) and d its
// depth.
TEST(VisualOverlay, RotationXYGridProjectsAsD3DX)
{
    const float W = 640, H = 360;
    const float ys = 1.f / std::tan(60.f * 0.017453292519943295769f), xs = ys / (W / H);
    const PointF org{400, 150};
    const auto expected = [&](float x, float y, float d) {
        return PointF{org.x + x * xs / d * W / 2, org.y - y * ys / d * H / 2};
    };
    XYProjection p;
    p.width = W;
    p.height = H;
    p.org = org;
    p.from = org;
    // Unturned: the grid's lines (VERTEX 0-87) in legacy's order, each
    // LINELIST pair cut in two at the middle, eleven each way 5 apart from
    // 25 to -25; the alpha rises by 155 / 12 a line (`ster` never turns, g
    // never reaching 1), the sixth orange.
    Overlay o;
    drawXYGrid(o, p, 2);
    ASSERT_EQ(o.lines.size(), 44u + 3u);
    ASSERT_EQ(o.polygons.size(), 3u * 4u); // three TRIANGLEFANs of six vertices
    float g = 1.f / 12.f;
    for (int k = 0; k < 11; ++k) {
        SCOPED_TRACE(k);
        const float j = 25.f - 5.f * static_cast<float>(k);
        const OverlayLine *l = &o.lines[static_cast<std::size_t>(4 * k)];
        expectNear(l[0].from, expected(j, -30, 17.2f));
        expectNear(l[0].to, expected(j, 0, 17.2f));
        expectNear(l[1].to, expected(j, 30, 17.2f));
        expectNear(l[2].from, expected(-30, j, 17.2f));
        expectNear(l[3].to, expected(30, j, 17.2f));
        const std::uint32_t rgb = k == 5 ? 0xFF9B24u : 0x7A3924u;
        for (int i = 0; i < 4; ++i)
            EXPECT_EQ(l[i].argb, (static_cast<std::uint32_t>(static_cast<int>(g * 155)) << 24) | rgb);
        g += 1.f / 12.f;
    }
    // The axes (VERTEX 176-180, lines 126-128): y up for an1-3 (else down),
    // x right unless an3, an6 or an9 ...
    expectNear(o.lines[44].from, expected(0, 9, 17.2f));
    expectNear(o.lines[44].to, org);
    expectNear(o.lines[45].to, expected(9, 0, 17.2f));
    expectNear(o.lines[46].to, expected(0, 0, 17.2f + 9.f));
    for (int i = 44; i < 47; ++i)
        EXPECT_EQ(o.lines[static_cast<std::size_t>(i)].argb, 0xFFBB0000u);
    // ... the y arrow's cone (181-186) from its tip at 10.
    expectNear(o.polygons[0].points[0], expected(0, 10, 17.2f));
    // an9: y down, x left (addy, addx -9).
    Overlay o9;
    drawXYGrid(o9, p, 9);
    expectNear(o9.lines[44].from, expected(0, -9, 17.2f));
    expectNear(o9.lines[45].to, expected(-9, 0, 17.2f));
    // \fry30 (angle.x 30, the yaw -30 degrees): x turns into depth, (10, 0, 0)
    // to (10 cos 30, 0, 10 sin 30).
    p.angle = {30, 0};
    const float c30 = std::cos(30.f * 0.017453292519943295769f), s30 = std::sin(30.f * 0.017453292519943295769f);
    expectNear(*projectXY(p, 10, 0, 0), expected(10 * c30, 0, 17.2f + 10 * s30));
    expectNear(*projectXY(p, -10, 0, 0), expected(-10 * c30, 0, 17.2f - 10 * s30));
    // \frx30 (angle.y 30, the pitch): (0, 10, 0) to (0, 10 cos 30, 10 sin 30).
    p.angle = {0, 30};
    expectNear(*projectXY(p, 0, 10, 0), expected(0, 10 * c30, 17.2f + 10 * s30));
    // Behind the near plane (depth under 1) nothing is drawn.
    p.angle = {90, 0};
    EXPECT_FALSE(projectXY(p, -20, 0, 0));
    // A position away from the \org: the model moves by (from - org) / W * 60
    // and (org - from) / H * 60 before it turns.
    p.angle = {0, 0};
    p.from = {320, 180};
    expectNear(*projectXY(p, 0, 0, 0), expected((320 - org.x) / W * 60, (org.y - 180) / H * 60, 17.2f));
}
