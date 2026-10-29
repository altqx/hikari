#include "hikari/application/automation_services.h"
#include "hikari/application/visual_rotation.h"
#include "hikari/application/visual_scale.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_transform.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include "legacy_visual_keys.h"

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
#include <stdexcept>
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

class CapturedMeasure : public TextMeasurePort {
public:
    static std::string key(const std::string &text, const std::string &font, const std::string &size, bool bold,
                           bool italic, const std::string &scaleX, const std::string &scaleY, const std::string &spacing)
    {
        return text + "|" + font + "|" + size + "|" + (bold ? "b" : "") + "|" + (italic ? "i" : "") + "|" + scaleX +
               "|" + scaleY + "|" + spacing;
    }
    static std::string key(const QJsonObject &e)
    {
        const auto str = [&](const char *name) { return e[QLatin1StringView(name)].toString().toStdString(); };
        return key(str("text"), str("fontname"), str("fontsize"), e[QStringLiteral("bold")].toBool(),
                   e[QStringLiteral("italic")].toBool(), str("scaleX"), str("scaleY"), str("spacing"));
    }

    std::map<std::string, TextExtents> answers;
    std::vector<std::string> calls;

    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        const std::string k = key(text, style[1], style[2], std::atoi(style[7].c_str()) != 0,
                                  std::atoi(style[8].c_str()) != 0, style[11], style[12], style[13]);
        calls.push_back(k);
        const auto it = answers.find(k);
        if (it == answers.end()) {
            ADD_FAILURE() << "legacy never measured " << k;
            return std::nullopt;
        }
        return it->second;
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
    std::vector<std::string> history, sent;
    std::optional<CommandRefusal> refuse;
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
    TextMeasurePort *textMeasure() const override { return measurePort ? measurePort : &measure; }
    TextMeasurePort *measurePort = nullptr;
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
    struct Style {
        std::string name, fontname = "Garamond", fontsize, scaleX, scaleY, spacing = "0", angle, outline, shadow,
                                     alignment, marginL, marginR, marginV;
        bool bold = false, italic = false;
        std::string line() const
        {
            return "Style: " + name + "," + fontname + "," + fontsize +
                   ",&H00FFFFFF,&H00000000,&H00FF0000,&H00000000," + (bold ? "-1" : "0") + "," +
                   (italic ? "-1" : "0") + ",0,0," + scaleX + "," + scaleY + "," + spacing + "," + angle + ",0," +
                   outline + "," + shadow + "," + alignment + "," + marginL + "," + marginR + "," + marginV + ",1";
        }
    };
    std::vector<Style> styles;
    std::vector<std::string> lines;
    std::vector<std::string> translations;
    int active = 0;
    std::vector<int> select;
    long caretFrom = 0, caretTo = 0;
    std::vector<std::string> ops;
};

std::vector<Case> readCases(const char *path = HIKARI_VISUAL_T3_CASES)
{
    std::ifstream in(path);
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
            Case::Style st;
            line >> st.name >> st.fontsize >> st.scaleX >> st.scaleY >> st.angle >> st.outline >> st.shadow >>
                st.alignment >> st.marginL >> st.marginR >> st.marginV;
            c.styles.push_back(st);
        } else if (key == "stylefont") {
            int bold = 0, italic = 0;
            Case::Style &st = c.styles.back();
            line >> bold >> italic >> st.spacing;
            std::getline(line >> std::ws, st.fontname);
            st.bold = bold != 0;
            st.italic = italic != 0;
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

std::map<std::string, std::vector<QJsonObject>> readObservations(const char *path = HIKARI_VISUAL_T3_OBSERVATIONS)
{
    QFile f(QString::fromUtf8(path));
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

// JSON has no infinity: the probe writes it as "inf".
float f(const QJsonValue &v)
{
    if (v.isString())
        return std::strtof(v.toString().toStdString().c_str(), nullptr);
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
        script += s.line() + "\n";
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
        {"T3-scale-vector-clip", "scale-several-preserve-proportions", 1, 2,
         u8"{\\fscy120\\fscx120\\pos(768,252)\\clip(2,m -384 -216 l -264 -216 -264 -96 -384 -96)}Third"},
        {"T3-scale-vector-clip", "scale-several-preserve-proportions", 2, 2,
         u8"{\\fscy121\\fscx121\\pos(766.4,249.6)\\clip(2,m -403 -226 l -282 -226 -282 -105 -403 -105)}Third"},
        {"T3-change-org-end", "rotz-org-several", 0, 1, u8"{\\an7\\org(40,50)\\frz30}Second"},
        {"T3-rotz-rect-clip", "rotz-several-preserve", 1, 1,
         u8"{\\pos(1050.922,777.889)\\clip(m 521 -265 l 667 -128 530 18 384 -119)\\frz1.941}Second"},
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

}

TEST(VisualTransformText, GetfloatAndNumbersFollowLegacy)
{
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

TEST(VisualTransformText, ClipRewritesKeepTheScaleAndTheRectangle)
{
    transform::TagFind find;
    std::u16string vector = u"{\\clip(2,m 0 0 l 200 0 200 100 0 100)}A";
    transform::changeClipScale(find, vector, {50, 25}, 2.f, 2.f);
    EXPECT_EQ(vector, u"{\\clip(2,m -100 -50 l 300 -50 300 150 -100 150)}A");

    std::u16string unscaled = u"{\\iclip(m 10 10 l 20 10 20 20)}A";
    transform::changeClipScale(find, unscaled, {10, 10}, 2.f, 1.f);
    EXPECT_EQ(unscaled, u"{\\iclip(m 10 10 l 30 10 30 20)}A");

    std::u16string rect = u"{\\clip(100,50,300,150)}A";
    transform::changeClipRotationZ(find, rect, {0, 0}, 0.f, 1.f);
    EXPECT_EQ(rect, u"{\\clip(m 100 50 l 300 50 300 150 100 150)}A");
    transform::changeClipRotationZ(find, rect, {0, 0}, 0.f, 1.f);
    EXPECT_EQ(rect, u"{\\clip(m 100 50 l 300 50 300 150 100 150)}A");
}

TEST(VisualTransformText, RectangleClipDropsValuesPastTheFourth)
{
    transform::TagFind find;
    std::u16string scaled = u"{\\clip(100,50,300,150,7,9)}A";
    transform::changeClipScale(find, scaled, {0, 0}, 1.f, 1.f);
    EXPECT_EQ(scaled, u"{\\clip(m 100 50 l 300 50 300 150 100 150)}A");
    std::u16string doubled = u"{\\iclip(100,50,300,150,7,9,11)}A";
    transform::changeClipScale(find, doubled, {100, 50}, 2.f, 2.f);
    EXPECT_EQ(doubled, u"{\\iclip(m 100 50 l 500 50 500 250 100 250)}A");
    std::u16string turned = u"{\\clip(100,50,300,150,7)}A";
    transform::changeClipRotationZ(find, turned, {0, 0}, 0.f, 1.f);
    EXPECT_EQ(turned, u"{\\clip(m 100 50 l 300 50 300 150 100 150)}A");

    for (const std::u16string bad : {u"{\\clip(100,50,x,150)}A", u"{\\clip(100,50,300,150,x)}A"}) {
        std::u16string text = bad;
        transform::changeClipScale(find, text, {0, 0}, 2.f, 2.f);
        EXPECT_EQ(text, bad);
        transform::changeClipRotationZ(find, text, {0, 0}, 1.f, 0.f);
        EXPECT_EQ(text, bad);
    }
}

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
    tool.setToggled(1, &host);
    EXPECT_TRUE(tool.hasTwoPoints());
    tool.setToggled(2, &host);
    EXPECT_FALSE(tool.hasTwoPoints());
    tool.setToggled(3, &host);
    EXPECT_TRUE(tool.hasTwoPoints());
}

namespace {

struct ReplayTotals {
    int states = 0;
    std::size_t departed = 0;
    std::vector<std::string> unchangedReleases;
};

struct ReplayHooks {
    TextMeasurePort *measure = nullptr;
    std::function<bool(const Case &, std::size_t step, const QJsonObject &, const VisualTool &)> atState;
};

void replay(const std::vector<Case> &cases, const std::map<std::string, std::vector<QJsonObject>> &observations,
            const ReplayHooks &hooks, ReplayTotals &totals)
{
    ASSERT_EQ(cases.size(), observations.size());
    int &states = totals.states;
    std::size_t &departed = totals.departed;
    std::vector<std::string> &unchangedReleases = totals.unchangedReleases;
    for (const Case &c : cases) {
        SCOPED_TRACE(c.name);
        ASSERT_TRUE(observations.contains(c.name));
        const auto &steps = observations.at(c.name);
        TestHost host;
        host.measurePort = hooks.measure;
        setUp(host, c);
        ASSERT_EQ(host.v.videoRect(), c.rect);
        ASSERT_EQ(host.v.zoomMove(), c.zoomMove);
        ASSERT_EQ(host.v.zoomScale(), c.zoomScale);
        std::unique_ptr<VisualTool> tool;
        std::size_t step = 0;
        std::size_t noStepSends = 0, noStepHistory = 0;
        QJsonArray previousLines;
        std::size_t previousSent = 0, previousHistory = 0;
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
            } else if (kind == "active") {
                int i;
                in >> i;
                const core::LineId id = ids(*host.s)[static_cast<std::size_t>(i)];
                host.s->setSelection({id, {id}, id, std::nullopt});
                host.targets = {id};
            } else if (kind == "setvisual") {
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
                bool shift = false, control = false, alt = false;
                std::string word;
                while (in >> word) {
                    shift = shift || word == "shift";
                    alt = alt || word == "alt";
                    control = control || word == "ctrl";
                }
                const Key key = legacy_keys::legacyKey(k, shift, control, alt, tool->family());
                (void)tool->key(key, host);
            } else if (kind == "draw") {
                (void)tool->overlay(host);
            } else if (kind == "state") {
                ASSERT_LT(step, steps.size());
                const QJsonObject &o = steps[step++];
                ++states;
                SCOPED_TRACE("step " + std::to_string(step - 1));
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
                const auto strings = [](const QJsonValue &v) {
                    std::vector<std::string> out;
                    for (const QJsonValue &e : v.toArray())
                        out.push_back(e.toString().toStdString());
                    return out;
                };
                auto legacySent = strings(o[QStringLiteral("sent")]);
                auto legacyHistory = strings(o[QStringLiteral("history")]);
                if (step > 1 && lines == previousLines) {
                    const std::size_t sends = legacySent.size() - previousSent;
                    const std::size_t marks = legacyHistory.size() - previousHistory;
                    if (sends + marks > 0)
                        unchangedReleases.push_back(c.name + " step " + std::to_string(step - 1));
                    noStepSends += sends;
                    noStepHistory += marks;
                }
                previousLines = lines;
                previousSent = legacySent.size();
                previousHistory = legacyHistory.size();
                legacySent.resize(legacySent.size() - noStepSends);
                legacyHistory.resize(legacyHistory.size() - noStepHistory);
                EXPECT_EQ(host.history, legacyHistory);
                EXPECT_EQ(host.sent, legacySent);
                std::vector<std::string> logged;
                for (const auto &l : host.logged)
                    logged.push_back(QString::fromStdU16String(l).toStdString());
                EXPECT_EQ(logged, strings(o[QStringLiteral("log")]));
                if (hooks.atState && hooks.atState(c, step - 1, o, *tool))
                    continue;
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
}

}

TEST(VisualCapture, ReplaysTheLegacyT3Probe)
{
    ReplayTotals totals;
    replay(readCases(), readObservations(), {}, totals);
    EXPECT_GT(totals.states, 70);
    EXPECT_EQ(totals.departed, departures().size());
    EXPECT_EQ(totals.unchangedReleases, (std::vector<std::string>{"scale-rectangle step 4", "rotz-two-points step 1",
                                                           "rotz-two-points step 5"}));
}

TEST(VisualCapture, ReplaysTheLegacyT3MeasureProbe)
{
    const auto observations = readObservations(HIKARI_VISUAL_T3_MEASURE_OBSERVATIONS);
    CapturedMeasure measure;
    for (const auto &[name, steps] : observations)
        for (const QJsonObject &o : steps)
            for (const QJsonValue &v : o[QStringLiteral("extents")].toArray()) {
                const QJsonObject e = v.toObject();
                ASSERT_TRUE(e[QStringLiteral("ok")].toBool());
                measure.answers[CapturedMeasure::key(e)] = {e[QStringLiteral("width")].toDouble(),
                                                            e[QStringLiteral("height")].toDouble(),
                                                            e[QStringLiteral("descent")].toDouble(),
                                                            e[QStringLiteral("extlead")].toDouble()};
            }
    int borderDeparture = 0;
    ReplayHooks hooks;
    hooks.measure = &measure;
    hooks.atState = [&](const Case &c, std::size_t step, const QJsonObject &o, const VisualTool &tool) {
        std::vector<std::string> legacy;
        for (const QJsonValue &v : o[QStringLiteral("extents")].toArray())
            if (!v.toObject()[QStringLiteral("text")].toString().isEmpty())
                legacy.push_back(CapturedMeasure::key(v.toObject()));
        EXPECT_EQ(measure.calls, legacy) << "the pieces measured";
        measure.calls.clear();
        if (c.name != "measure-empty-line" || step != 1)
            return false;
        ++borderDeparture;
        const auto &s = dynamic_cast<const ScaleTool &>(tool);
        const PointF legacyBorder = pair(o[QStringLiteral("border")]);
        EXPECT_EQ(legacyBorder, (PointF{13, 13})) << "legacy's border is the previous Line's";
        EXPECT_EQ(s.border(), (PointF{0, 0}));
        EXPECT_EQ(s.originalSize(), pair(o[QStringLiteral("originalSize")]));
        EXPECT_EQ(s.from(), pair(o[QStringLiteral("from")]));
        EXPECT_EQ(s.scale(), pair(o[QStringLiteral("scale")]));
        const QJsonArray r = o[QStringLiteral("sizingRectangle")].toArray();
        const PointF half{legacyBorder.x / 2, legacyBorder.y / 2};
        const PointF topLeft = pair(r[2]), bottomRight = pair(r[3]);
        EXPECT_EQ(s.sizingRectangle()[2], (PointF{topLeft.x + half.x, topLeft.y + half.y}));
        EXPECT_EQ(s.sizingRectangle()[3], (PointF{bottomRight.x - half.x, bottomRight.y - half.y}));
        EXPECT_EQ(s.originalRectangleVisible(), o[QStringLiteral("originalRectangleVisible")].toBool());
        return true;
    };
    ReplayTotals totals;
    replay(readCases(HIKARI_VISUAL_T3_MEASURE_CASES), observations, hooks, totals);
    EXPECT_EQ(totals.states, 65);
    EXPECT_EQ(borderDeparture, 1);
    EXPECT_EQ(totals.departed, 0u);
    EXPECT_TRUE(totals.unchangedReleases.empty());
}

TEST(VisualTransformEdit, ARefusedCommitLeavesTheCaretAndTheText)
{
    for (const char *name : {"scale-width-left-drag", "rotz-drag", "rotxy-left-right-middle"}) {
        SCOPED_TRACE(name);
        const auto cases = readCases();
        const auto it = std::find_if(cases.begin(), cases.end(), [&](const Case &c) { return c.name == name; });
        ASSERT_NE(it, cases.end());
        Case c = *it;
        c.caretFrom = c.caretTo = 7;
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
        const std::pair<int, int> press = c.name.starts_with("rotxy") ? std::pair{100, 100} : std::pair{420, 180};
        tool->pointer(at(press.first, press.second, Pointer::Kind::Press, true), host);
        Pointer move = at(press.first + 40, press.second + 30, Pointer::Kind::Move, true);
        move.button = Pointer::Button::None;
        tool->pointer(move, host);
        ASSERT_TRUE(host.g && host.g->hasChanges());
        EXPECT_NE(handles().second, before.second);
        host.refuse = CommandRefusal::StaleRevision;
        tool->pointer(at(press.first + 40, press.second + 30, Pointer::Kind::Release, false), host);
        EXPECT_FALSE(host.g);
        EXPECT_EQ(host.s->historySize(), steps);
        EXPECT_EQ(lineOf(*host.s, *host.activeLine())->text, text);
        EXPECT_TRUE(host.sent.empty());
        EXPECT_EQ(host.caret, (std::pair<long, long>{7, 7}));
        EXPECT_EQ(handles(), before);
    }
}

TEST(VisualTransformEdit, AReleaseThatChangesNoLineRecordsNothing)
{
    for (const char *name : {"scale-several-lines", "rotz-org-several", "rotxy-several"}) {
        SCOPED_TRACE(name);
        const auto cases = readCases();
        const auto it = std::find_if(cases.begin(), cases.end(), [&](const Case &c) { return c.name == name; });
        ASSERT_NE(it, cases.end());
        const Case &c = *it;
        TestHost host;
        setUp(host, c);
        std::unique_ptr<VisualTool> tool;
        std::string lastRelease;
        for (const std::string &op : c.ops) {
            std::istringstream in(op);
            std::string kind;
            in >> kind;
            if (kind == "tool") {
                std::string tool_;
                int bits;
                in >> tool_ >> bits;
                if (tool_ == "scale") {
                    auto t = std::make_unique<ScaleTool>();
                    t->setToggled(bits);
                    tool = std::move(t);
                } else if (tool_ == "rotz") {
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
            } else if (kind == "press" || kind == "move" || kind == "release") {
                tool->pointer(pointerOf(kind, in), host);
                if (kind == "release")
                    lastRelease = op.substr(op.find(' '));
            }
        }
        ASSERT_FALSE(host.history.empty());
        const auto texts = [&] {
            std::vector<std::u8string> out;
            for (const auto id : ids(*host.s))
                out.push_back(lineOf(*host.s, id)->text);
            return out;
        };
        const auto before = texts();
        const std::size_t steps = host.s->historySize();
        const auto recorded = host.history;
        host.s->markSaved(host.s->contentId());
        for (const char *kind : {"press", "release"}) {
            std::istringstream in(lastRelease);
            tool->pointer(pointerOf(kind, in), host);
        }
        EXPECT_FALSE(host.g);
        EXPECT_EQ(texts(), before);
        EXPECT_EQ(host.s->historySize(), steps);
        EXPECT_EQ(host.history, recorded);
        EXPECT_FALSE(host.s->isDirty());
    }
}

namespace {

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

}

TEST(VisualOverlay, RotationZDrawsLegacysRingAndHandle)
{
    const Case c = caseWith("rotz-drag", "{\\pos(960,540)}Turn", "{\\pos(960,540)\\org(1200,600)\\frz30}Turn");
    TestHost host;
    setUp(host, c);
    RotationZTool tool;
    tool.setToggled(0);
    tool.selected(host);
    tool.reset(host);
    const PointF org{400, 200}, from{320, 180};
    expectNear(tool.org(), org);
    expectNear(tool.from(), from);
    EXPECT_EQ(tool.lastmove().y, 30.f);
    const Overlay o = tool.overlay(host);
    const float rad = 0.01745329251994329576923690768489f;
    const float radius = std::sqrt(80.f * 80.f + 20.f * 20.f) + 40;
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
    double area = 0;
    for (std::size_t i = 0; i < ring.points.size(); ++i) {
        const PointF a = ring.points[i], b = ring.points[(i + 1) % ring.points.size()];
        area += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    const double gon = 90.0 * std::sin(2.0 * 3.14159265358979323846 / 180.0);
    EXPECT_NEAR(std::fabs(area / 2), gon * ((radius + 10.0) * (radius + 10.0) - radius * radius), 0.5);
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
    Overlay o;
    drawXYGrid(o, p, 2);
    ASSERT_EQ(o.lines.size(), 44u + 3u);
    ASSERT_EQ(o.polygons.size(), 3u * 4u);
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
    expectNear(o.lines[44].from, expected(0, 9, 17.2f));
    expectNear(o.lines[44].to, org);
    expectNear(o.lines[45].to, expected(9, 0, 17.2f));
    expectNear(o.lines[46].to, expected(0, 0, 17.2f + 9.f));
    for (int i = 44; i < 47; ++i)
        EXPECT_EQ(o.lines[static_cast<std::size_t>(i)].argb, 0xFFBB0000u);
    expectNear(o.polygons[0].points[0], expected(0, 10, 17.2f));
    Overlay o9;
    drawXYGrid(o9, p, 9);
    expectNear(o9.lines[44].from, expected(0, -9, 17.2f));
    expectNear(o9.lines[45].to, expected(-9, 0, 17.2f));
    p.angle = {30, 0};
    const float c30 = std::cos(30.f * 0.017453292519943295769f), s30 = std::sin(30.f * 0.017453292519943295769f);
    expectNear(*projectXY(p, 10, 0, 0), expected(10 * c30, 0, 17.2f + 10 * s30));
    expectNear(*projectXY(p, -10, 0, 0), expected(-10 * c30, 0, 17.2f - 10 * s30));
    p.angle = {0, 30};
    expectNear(*projectXY(p, 0, 10, 0), expected(0, 10 * c30, 17.2f + 10 * s30));
    p.angle = {90, 0};
    EXPECT_FALSE(projectXY(p, -20, 0, 0));
    p.angle = {0, 0};
    p.from = {320, 180};
    expectNear(*projectXY(p, 0, 0, 0), expected((320 - org.x) / W * 60, (org.y - 180) / H * 60, 17.2f));
}

namespace {

const Case &caseNamed(const char *name)
{
    static const auto cases = readCases();
    const auto it = std::find_if(cases.begin(), cases.end(), [&](const Case &c) { return c.name == name; });
    if (it == cases.end())
        throw std::runtime_error(std::string("no case ") + name);
    return *it;
}

void send(VisualTool &tool, TestHost &host, const std::string &event)
{
    std::istringstream in(event);
    std::string kind;
    in >> kind;
    tool.pointer(pointerOf(kind, in), host);
}

}

TEST(VisualTransformPointer, ScaleArrowEndsShowTheirAxisShape)
{
    TestHost host;
    setUp(host, caseNamed("scale-width-left-drag"));
    ScaleTool tool;
    tool.setToggled(6);
    tool.selected(host);
    tool.reset(host);
    ASSERT_EQ(tool.to(), (PointF{420, 280}));
    for (const auto &[at, shape] : std::vector<std::pair<std::string, PointerShape>>{
             {"move 420 280", PointerShape::Sizing},
             {"move 430 289", PointerShape::Sizing},
             {"move 420 180", PointerShape::SizeWE},
             {"move 411 171", PointerShape::SizeWE},
             {"move 320 280", PointerShape::SizeNS},
             {"move 500 50", PointerShape::Arrow},
         }) {
        send(tool, host, at);
        EXPECT_EQ(host.pointerShape(), shape) << at;
        send(tool, host, "move 500 50");
        EXPECT_EQ(host.pointerShape(), PointerShape::Arrow) << at;
    }
    send(tool, host, "move 420 280");
    send(tool, host, "move 420 185");
    EXPECT_EQ(host.pointerShape(), PointerShape::Sizing);
    send(tool, host, "move 500 50");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
}

TEST(VisualTransformPointer, ScaleDragsShowTheScaledAxis)
{
    TestHost host;
    setUp(host, caseNamed("scale-width-left-drag"));
    ScaleTool tool;
    tool.setToggled(6);
    tool.selected(host);
    tool.reset(host);
    send(tool, host, "press 420 180 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::SizeWE);
    send(tool, host, "move 470 185 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::SizeWE);
    send(tool, host, "release 470 185 left");
    EXPECT_EQ(tool.to().x, 470.f);
    EXPECT_EQ(host.pointerShape(), PointerShape::SizeWE);
    send(tool, host, "move 600 50");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "press 600 50 right");
    EXPECT_EQ(host.pointerShape(), PointerShape::SizeNS);
    send(tool, host, "release 600 50 right");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "press 600 50 middle");
    EXPECT_EQ(host.pointerShape(), PointerShape::Sizing);
    send(tool, host, "release 600 50 middle");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "press 600 50 left shift");
    EXPECT_EQ(host.pointerShape(), PointerShape::Sizing);
    send(tool, host, "release 600 50 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
}

TEST(VisualTransformPointer, ScaleShiftChangesTheShapeOnlyDuringADrag)
{
    TestHost host;
    setUp(host, caseNamed("scale-width-left-drag"));
    ScaleTool tool;
    tool.setToggled(6);
    tool.selected(host);
    tool.reset(host);
    for (const auto &[at, shape] : std::vector<std::pair<std::string, PointerShape>>{
             {"move 500 50", PointerShape::Arrow},
             {"move 501 50 shift", PointerShape::Arrow},
             {"move 502 50", PointerShape::Arrow},
             {"move 420 180", PointerShape::SizeWE},
             {"move 421 180 shift", PointerShape::SizeWE},
             {"move 422 180", PointerShape::SizeWE},
             {"move 500 50", PointerShape::Arrow},
         }) {
        send(tool, host, at);
        EXPECT_EQ(host.pointerShape(), shape) << at;
    }
    send(tool, host, "press 600 50 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::SizeWE);
    send(tool, host, "move 610 50 left shift");
    EXPECT_EQ(host.pointerShape(), PointerShape::Sizing);
    send(tool, host, "move 640 70 left shift");
    EXPECT_EQ(host.pointerShape(), PointerShape::Sizing);
    ASSERT_GE(std::abs(tool.to().x - 640.f), 11.f);
    send(tool, host, "release 640 70 left shift");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "move 641 70 shift");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "move 642 70");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);

    TestHost ends;
    setUp(ends, caseNamed("scale-width-left-drag"));
    ScaleTool grabbed;
    grabbed.setToggled(6);
    grabbed.selected(ends);
    grabbed.reset(ends);
    send(grabbed, ends, "press 420 180 left");
    EXPECT_EQ(ends.pointerShape(), PointerShape::SizeWE);
    send(grabbed, ends, "move 420 180 left shift");
    EXPECT_EQ(ends.pointerShape(), PointerShape::Sizing);
    send(grabbed, ends, "release 420 180 left shift");
    EXPECT_EQ(ends.pointerShape(), PointerShape::SizeWE);

    TestHost linked;
    setUp(linked, caseNamed("scale-width-left-drag"));
    ScaleTool proportions;
    proportions.setToggled(6 | 64);
    proportions.selected(linked);
    proportions.reset(linked);
    send(proportions, linked, "move 420 180");
    EXPECT_EQ(linked.pointerShape(), PointerShape::SizeWE);
    send(proportions, linked, "move 421 180 shift");
    EXPECT_EQ(linked.pointerShape(), PointerShape::SizeWE);
    send(proportions, linked, "move 422 180");
    EXPECT_EQ(linked.pointerShape(), PointerShape::SizeWE);
    send(proportions, linked, "press 422 180 left");
    EXPECT_EQ(linked.pointerShape(), PointerShape::Sizing);
    send(proportions, linked, "move 422 180 left shift");
    EXPECT_EQ(linked.pointerShape(), PointerShape::Sizing);
    send(proportions, linked, "release 422 180 left shift");
    EXPECT_EQ(linked.pointerShape(), PointerShape::SizeWE);
}

TEST(VisualTransformPointer, ScaleRectanglesEdgesAndCornersShowTheirResizeShapes)
{
    TestHost host;
    setUp(host, caseNamed("scale-rectangle"));
    ScaleTool tool;
    tool.setToggled(7);
    tool.selected(host);
    tool.reset(host);
    send(tool, host, "press 300 150 left");
    send(tool, host, "move 400 200 left");
    send(tool, host, "release 400 200 left");
    ASSERT_TRUE(tool.rectangleVisible());
    EXPECT_EQ(host.pointerShape(), PointerShape::SizeNWSE);
    for (const auto &[at, shape] : std::vector<std::pair<std::string, PointerShape>>{
             {"move 300 175", PointerShape::SizeWE},
             {"move 400 175", PointerShape::SizeWE},
             {"move 350 150", PointerShape::SizeNS},
             {"move 350 200", PointerShape::SizeNS},
             {"move 300 150", PointerShape::SizeNWSE},
             {"move 400 200", PointerShape::SizeNWSE},
             {"move 400 150", PointerShape::SizeNESW},
             {"move 300 200", PointerShape::SizeNESW},
             {"move 350 175", PointerShape::Arrow},
         }) {
        send(tool, host, at);
        EXPECT_EQ(host.pointerShape(), shape) << at;
        send(tool, host, "move 600 300");
        EXPECT_EQ(host.pointerShape(), PointerShape::Arrow) << at;
    }
    send(tool, host, "move 400 175");
    send(tool, host, "press 400 175 left");
    send(tool, host, "move 450 100 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::SizeWE);
    send(tool, host, "release 450 100 left");
    send(tool, host, "move 600 300");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);

    TestHost other;
    setUp(other, caseNamed("scale-original-rectangle"));
    ScaleTool original;
    original.setToggled(55);
    original.selected(other);
    original.reset(other);
    send(original, other, "press 50 50 right");
    send(original, other, "move 150 100 right");
    send(original, other, "release 150 100 right");
    ASSERT_TRUE(original.originalRectangleVisible());
    send(original, other, "move 50 75");
    EXPECT_EQ(other.pointerShape(), PointerShape::SizeWE);
    send(original, other, "move 120 100");
    EXPECT_EQ(other.pointerShape(), PointerShape::SizeNS);
    send(original, other, "move 120 75");
    EXPECT_EQ(other.pointerShape(), PointerShape::Arrow);
}

TEST(VisualTransformPointer, RotationZDragsShowTheFourWayShape)
{
    TestHost host;
    setUp(host, caseNamed("rotz-drag"));
    RotationZTool tool;
    tool.setToggled(0);
    tool.selected(host);
    tool.reset(host);
    send(tool, host, "move 320 180");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "press 420 180 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Sizing);
    send(tool, host, "move 380 250 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Sizing);
    send(tool, host, "release 380 250 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "press 320 180 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Sizing);
    send(tool, host, "release 330 190 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
}

TEST(VisualTransformPointer, RotationZTwoPointsShowTheFourWayShapeFromTheSecond)
{
    TestHost host;
    setUp(host, caseNamed("rotz-two-points"));
    RotationZTool tool;
    tool.setToggled(1);
    tool.selected(host);
    tool.reset(host);
    send(tool, host, "press 100 100 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "release 100 100 left");
    send(tool, host, "press 300 200 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Sizing);
    send(tool, host, "release 300 200 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "press 301 199 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Sizing);
    send(tool, host, "release 310 150 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "press 50 300 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    send(tool, host, "release 50 300 left");
}

TEST(VisualTransformPointer, RotationXYDragsShowTheTurnedAxis)
{
    TestHost host;
    setUp(host, caseNamed("rotxy-left-right-middle"));
    RotationXYTool tool;
    tool.setToggled(-1);
    tool.selected(host);
    tool.reset(host);
    send(tool, host, "move 100 100");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    for (const auto &[button, shape] : std::vector<std::pair<std::string, PointerShape>>{
             {"left", PointerShape::SizeWE}, {"right", PointerShape::SizeNS}, {"middle", PointerShape::Sizing}}) {
        send(tool, host, "press 100 100 " + button);
        EXPECT_EQ(host.pointerShape(), shape) << button;
        send(tool, host, "move 130 120 " + button);
        EXPECT_EQ(host.pointerShape(), shape) << button;
        send(tool, host, "release 130 120 " + button);
        EXPECT_EQ(host.pointerShape(), PointerShape::Arrow) << button;
    }
    send(tool, host, "press 320 180 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::SizeWE);
    send(tool, host, "release 330 180 left");
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
}

TEST(VisualTransformValues, TypedAndScrubbedValuesAreOneStepEach)
{
    const auto cases = readCases();
    const auto caseNamed = [&](const char *name) {
        return *std::find_if(cases.begin(), cases.end(), [&](const Case &k) { return k.name == name; });
    };
    const auto textOf = [](const TestHost &host) {
        const std::u8string &t = lineOf(*host.s, *host.activeLine())->text;
        return std::string(t.begin(), t.end());
    };
    const auto valueOf = [](const VisualTool &tool, const VisualHost &host, const std::string &name) {
        for (const auto &v : tool.values(host))
            if (v.name == name)
                return v;
        return ToolValue{};
    };
    const auto escape = [](TestHost &host, VisualTool &tool) {
        host.cancelGesture();
        tool.reset(host);
    };
    {
        SCOPED_TRACE("scale");
        TestHost host;
        setUp(host, caseNamed("scale-width-left-drag"));
        ScaleTool tool;
        tool.setToggled(6);
        tool.selected(host);
        tool.reset(host);
        const std::size_t steps = host.s->historySize();
        const ToolValue x = valueOf(tool, host, "fscx");
        EXPECT_TRUE(x.editable);
        EXPECT_EQ(x.label, u"X");
        EXPECT_EQ(x.unit, u"%");
        EXPECT_EQ(x.text, u"100");
        EXPECT_FALSE(tool.setValue("fscx", u"abc", host));
        EXPECT_FALSE(tool.setValue("fscx", u"1e100", host));
        EXPECT_FALSE(tool.setValue("fscx", u"-5", host, ValueEdit::Sample));
        EXPECT_FALSE(host.g);
        EXPECT_EQ(host.s->historySize(), steps);
        EXPECT_TRUE(tool.setValue("fscx", u"150", host));
        EXPECT_FALSE(host.g);
        EXPECT_EQ(host.s->historySize(), steps + 1);
        EXPECT_NE(textOf(host).find("\\fscx150"), std::string::npos) << textOf(host);
        EXPECT_EQ(valueOf(tool, host, "fscx").text, u"150");
        EXPECT_TRUE(tool.setValue("fscy", u"110", host, ValueEdit::Sample));
        ASSERT_TRUE(host.g);
        EXPECT_TRUE(tool.setValue("fscy", u"120", host, ValueEdit::Sample));
        EXPECT_EQ(host.s->historySize(), steps + 1);
        EXPECT_EQ(textOf(host).find("\\fscy"), std::string::npos) << textOf(host);
        EXPECT_TRUE(tool.setValue("fscy", u"120", host, ValueEdit::Commit));
        EXPECT_FALSE(host.g);
        EXPECT_EQ(host.s->historySize(), steps + 2);
        EXPECT_NE(textOf(host).find("\\fscy120"), std::string::npos) << textOf(host);
        EXPECT_NE(textOf(host).find("\\fscx150"), std::string::npos) << textOf(host);
        const std::string before = textOf(host);
        EXPECT_TRUE(tool.setValue("fscx", u"10", host, ValueEdit::Sample));
        ASSERT_TRUE(host.g);
        escape(host, tool);
        EXPECT_EQ(textOf(host), before);
        EXPECT_EQ(host.s->historySize(), steps + 2);
        EXPECT_EQ(valueOf(tool, host, "fscx").text, u"150");
    }
    {
        SCOPED_TRACE("rotz");
        TestHost host;
        setUp(host, caseNamed("rotz-drag"));
        RotationZTool tool;
        tool.setToggled(0);
        tool.selected(host);
        tool.reset(host);
        const std::size_t steps = host.s->historySize();
        EXPECT_EQ(valueOf(tool, host, "frz").unit, u"°");
        EXPECT_EQ(valueOf(tool, host, "orgx").group, u"Origin");
        EXPECT_TRUE(tool.setValue("frz", u"30", host));
        EXPECT_EQ(host.s->historySize(), steps + 1);
        EXPECT_NE(textOf(host).find("\\frz30"), std::string::npos) << textOf(host);
        EXPECT_EQ(valueOf(tool, host, "frz").text, u"30");
        const std::string before = textOf(host);
        EXPECT_TRUE(tool.setValue("frz", u"45", host, ValueEdit::Sample));
        ASSERT_TRUE(host.g);
        escape(host, tool);
        EXPECT_EQ(textOf(host), before);
        EXPECT_EQ(valueOf(tool, host, "frz").text, u"30");
        EXPECT_TRUE(tool.setValue("orgx", u"100", host));
        EXPECT_EQ(host.s->historySize(), steps + 2);
        EXPECT_NE(textOf(host).find("\\org(100,540)"), std::string::npos) << textOf(host);
        EXPECT_NE(textOf(host).find("\\frz30"), std::string::npos) << textOf(host);
        EXPECT_EQ(valueOf(tool, host, "orgx").text, u"100");
        EXPECT_EQ(valueOf(tool, host, "orgy").text, u"540");
    }
    {
        SCOPED_TRACE("rotxy");
        TestHost host;
        setUp(host, caseNamed("rotxy-left-right-middle"));
        RotationXYTool tool;
        tool.setToggled(0);
        tool.selected(host);
        tool.reset(host);
        const std::size_t steps = host.s->historySize();
        EXPECT_EQ(valueOf(tool, host, "frx").text, u"10");
        EXPECT_TRUE(tool.setValue("fry", u"20", host));
        EXPECT_EQ(host.s->historySize(), steps + 1);
        EXPECT_NE(textOf(host).find("\\fry20"), std::string::npos) << textOf(host);
        EXPECT_NE(textOf(host).find("\\frx10"), std::string::npos) << textOf(host);
        EXPECT_TRUE(tool.setValue("frx", u"-15", host, ValueEdit::Sample));
        ASSERT_TRUE(host.g);
        EXPECT_TRUE(tool.setValue("frx", u"-15", host, ValueEdit::Commit));
        EXPECT_EQ(host.s->historySize(), steps + 2);
        EXPECT_NE(textOf(host).find("\\frx345"), std::string::npos) << textOf(host);
        EXPECT_NE(textOf(host).find("\\fry20"), std::string::npos) << textOf(host);
        EXPECT_EQ(valueOf(tool, host, "frx").text, u"345");
        EXPECT_EQ(valueOf(tool, host, "fry").text, u"20");
    }
}

namespace {
std::string rowText(const TestHost &host, std::size_t row)
{
    const std::u8string &t = lineOf(*host.s, ids(*host.s)[row])->text;
    return std::string(t.begin(), t.end());
}
std::u16string valueText(const VisualTool &tool, const VisualHost &host, const std::string &name)
{
    for (const auto &v : tool.values(host))
        if (v.name == name)
            return v.text;
    return {};
}
}

TEST(VisualTransformValues, BatchAnglesStartFromTheActiveLine)
{
    {
        SCOPED_TRACE("rotz");
        TestHost host;
        setUp(host, caseNamed("rotz-several-preserve"));
        const auto all = ids(*host.s);
        host.targets = {all[1], all[0]};
        RotationZTool tool;
        tool.setToggled(2);
        tool.selected(host);
        tool.reset(host);
        EXPECT_EQ(valueText(tool, host, "frz"), u"10");
        EXPECT_TRUE(tool.setValue("frz", u"20", host));
        EXPECT_NE(rowText(host, 0).find("\\frz20"), std::string::npos) << rowText(host, 0);
        EXPECT_NE(rowText(host, 1).find("\\frz55"), std::string::npos) << rowText(host, 1);
        EXPECT_EQ(valueText(tool, host, "frz"), u"20");
        EXPECT_TRUE(tool.setValue("frz", u"30", host));
        EXPECT_NE(rowText(host, 0).find("\\frz30"), std::string::npos) << rowText(host, 0);
        EXPECT_NE(rowText(host, 1).find("\\frz65"), std::string::npos) << rowText(host, 1);
    }
    {
        SCOPED_TRACE("rotxy");
        TestHost host;
        setUp(host, caseNamed("rotxy-several"));
        const auto all = ids(*host.s);
        host.targets = {all[0], all[1]};
        RotationXYTool tool;
        tool.setToggled(0);
        tool.selected(host);
        tool.reset(host);
        EXPECT_EQ(valueText(tool, host, "fry"), u"5");
        EXPECT_TRUE(tool.setValue("fry", u"15", host));
        EXPECT_NE(rowText(host, 0).find("\\fry15"), std::string::npos) << rowText(host, 0);
        EXPECT_NE(rowText(host, 1).find("\\fry50"), std::string::npos) << rowText(host, 1);
        EXPECT_EQ(valueText(tool, host, "fry"), u"15");
        EXPECT_TRUE(tool.setValue("fry", u"25", host));
        EXPECT_NE(rowText(host, 0).find("\\fry25"), std::string::npos) << rowText(host, 0);
        EXPECT_NE(rowText(host, 1).find("\\fry60"), std::string::npos) << rowText(host, 1);
    }
}

TEST(VisualTransformValues, AnglesOfSeveralTurnsAreInOneTurn)
{
    {
        TestHost host;
        setUp(host, caseNamed("rotz-drag"));
        RotationZTool tool;
        tool.setToggled(0);
        tool.selected(host);
        tool.reset(host);
        EXPECT_TRUE(tool.setValue("frz", u"-721", host));
        EXPECT_NE(rowText(host, 0).find("\\frz359"), std::string::npos) << rowText(host, 0);
        EXPECT_EQ(valueText(tool, host, "frz"), u"359");
    }
    {
        TestHost host;
        setUp(host, caseNamed("rotxy-left-right-middle"));
        RotationXYTool tool;
        tool.setToggled(0);
        tool.selected(host);
        tool.reset(host);
        EXPECT_TRUE(tool.setValue("frx", u"-735", host));
        EXPECT_NE(rowText(host, 0).find("\\frx345"), std::string::npos) << rowText(host, 0);
        EXPECT_TRUE(tool.setValue("fry", u"1090", host));
        EXPECT_NE(rowText(host, 0).find("\\fry10"), std::string::npos) << rowText(host, 0);
    }
}

TEST(VisualTransformValues, ChangeAllFromAZeroScaleIsRefused)
{
    TestHost host;
    setUp(host, caseNamed("scale-width-left-drag"));
    ScaleTool tool;
    tool.setToggled(6 | 32);
    tool.selected(host);
    tool.reset(host);
    const auto textOf = [&] {
        const std::u8string &t = lineOf(*host.s, *host.activeLine())->text;
        return std::string(t.begin(), t.end());
    };
    const std::size_t steps = host.s->historySize();
    EXPECT_TRUE(tool.setValue("fscx", u"0", host));
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_NE(textOf().find("\\fscx0"), std::string::npos) << textOf();
    tool.reset(host);
    tool.reset(host);
    EXPECT_FALSE(tool.setValue("fscx", u"150", host));
    EXPECT_FALSE(tool.setValue("fscx", u"150", host, ValueEdit::Sample));
    EXPECT_FALSE(host.g);
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(textOf().find("nan"), std::string::npos) << textOf();
    EXPECT_TRUE(tool.setValue("fscy", u"120", host));
    EXPECT_EQ(host.s->historySize(), steps + 2);
    EXPECT_EQ(textOf().find("nan"), std::string::npos) << textOf();
}

TEST(VisualTransformValues, ChangeAllRefusesARatioThatOverflows)
{
    TestHost host;
    setUp(host, caseNamed("scale-width-left-drag"));
    const core::LineId active = *host.activeLine();
    ASSERT_TRUE(host.s->editDraft(
        active, DraftChange{.text = u8"{\\fscx0.0000000000000000000000000000000000001\\fscy100}x"}));
    ASSERT_TRUE(host.s->commitDraft());
    ScaleTool tool;
    tool.setToggled(6 | 32);
    tool.selected(host);
    tool.reset(host);
    const auto textOf = [&] {
        const std::u8string &t = lineOf(*host.s, active)->text;
        return std::string(t.begin(), t.end());
    };
    const std::size_t steps = host.s->historySize();
    EXPECT_FALSE(tool.setValue("fscx", u"100000000", host));
    EXPECT_FALSE(tool.setValue("fscx", u"100000000", host, ValueEdit::Sample));
    EXPECT_FALSE(host.g);
    EXPECT_EQ(host.s->historySize(), steps);
    EXPECT_EQ(textOf().find("inf"), std::string::npos) << textOf();
    EXPECT_TRUE(tool.setValue("fscy", u"120", host));
    EXPECT_EQ(textOf().find("inf"), std::string::npos) << textOf();
}

TEST(VisualTransformValues, ChangeAllScrubsStartFromTheGesturesText)
{
    const auto textOf = [](const TestHost &host) {
        const std::u8string &t = lineOf(*host.s, *host.activeLine())->text;
        return std::string(t.begin(), t.end());
    };
    {
        SCOPED_TRACE("scale");
        TestHost host;
        setUp(host, caseNamed("scale-width-left-drag"));
        ScaleTool tool;
        tool.setToggled(6 | 32);
        tool.selected(host);
        tool.reset(host);
        ASSERT_EQ(valueText(tool, host, "fscx"), u"100");
        const std::size_t steps = host.s->historySize();
        EXPECT_TRUE(tool.setValue("fscx", u"110", host, ValueEdit::Sample));
        EXPECT_TRUE(tool.setValue("fscx", u"120", host, ValueEdit::Sample));
        EXPECT_TRUE(tool.setValue("fscx", u"120", host, ValueEdit::Commit));
        EXPECT_EQ(host.s->historySize(), steps + 1);
        EXPECT_NE(textOf(host).find("\\fscx120"), std::string::npos) << textOf(host);
        EXPECT_EQ(valueText(tool, host, "fscx"), u"120");
    }
    {
        SCOPED_TRACE("rotz");
        TestHost host;
        setUp(host, caseNamed("rotz-drag"));
        RotationZTool tool;
        tool.setToggled(2);
        tool.selected(host);
        tool.reset(host);
        EXPECT_TRUE(tool.setValue("frz", u"10", host));
        const std::size_t steps = host.s->historySize();
        EXPECT_TRUE(tool.setValue("frz", u"20", host, ValueEdit::Sample));
        EXPECT_TRUE(tool.setValue("frz", u"30", host, ValueEdit::Sample));
        EXPECT_TRUE(tool.setValue("frz", u"30", host, ValueEdit::Commit));
        EXPECT_EQ(host.s->historySize(), steps + 1);
        EXPECT_NE(textOf(host).find("\\frz30"), std::string::npos) << textOf(host);
        EXPECT_EQ(valueText(tool, host, "frz"), u"30");
    }
    {
        SCOPED_TRACE("rotxy");
        TestHost host;
        setUp(host, caseNamed("rotxy-left-right-middle"));
        RotationXYTool tool;
        tool.setToggled(0);
        tool.selected(host);
        tool.reset(host);
        EXPECT_TRUE(tool.setValue("fry", u"10", host));
        const std::size_t steps = host.s->historySize();
        EXPECT_TRUE(tool.setValue("fry", u"20", host, ValueEdit::Sample));
        EXPECT_TRUE(tool.setValue("fry", u"30", host, ValueEdit::Sample));
        EXPECT_TRUE(tool.setValue("fry", u"30", host, ValueEdit::Commit));
        EXPECT_EQ(host.s->historySize(), steps + 1);
        EXPECT_NE(textOf(host).find("\\fry30"), std::string::npos) << textOf(host);
        EXPECT_EQ(valueText(tool, host, "fry"), u"30");
    }
}

TEST(VisualTransformValues, BatchScaleValuesTakeTheirOwnAxisRatio)
{
    TestHost host;
    setUp(host, caseNamed("scale-several-lines"));
    const auto all = ids(*host.s);
    host.targets = {all.begin(), all.end()};
    ScaleTool tool;
    tool.setToggled(32);
    tool.selected(host);
    tool.reset(host);
    ASSERT_EQ(valueText(tool, host, "fscx"), u"100");
    const std::size_t steps = host.s->historySize();
    EXPECT_TRUE(tool.setValue("fscx", u"50", host));
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(rowText(host, 0), "{\\fscx50\\pos(960,540)}Active");
    EXPECT_EQ(rowText(host, 1), "{\\fscx75}Second{\\fscx20}x");
    EXPECT_EQ(rowText(host, 2), "{\\fscx50\\fscy70}Third");
    EXPECT_EQ(valueText(tool, host, "fscx"), u"50");
    EXPECT_TRUE(tool.setValue("fscy", u"120", host, ValueEdit::Sample));
    EXPECT_TRUE(tool.setValue("fscy", u"150", host, ValueEdit::Sample));
    EXPECT_TRUE(tool.setValue("fscy", u"150", host, ValueEdit::Commit));
    EXPECT_EQ(host.s->historySize(), steps + 2);
    EXPECT_NE(rowText(host, 0).find("\\fscx50"), std::string::npos) << rowText(host, 0);
    EXPECT_EQ(rowText(host, 1), "{\\fscy75\\fscx75}Second{\\fscx20}x");
    EXPECT_NE(rowText(host, 2).find("\\fscx50\\fscy105"), std::string::npos) << rowText(host, 2);
}

TEST(VisualTransformValues, AProportionalScrubThroughZeroKeepsTheProportion)
{
    TestHost host;
    setUp(host, caseNamed("scale-width-left-drag"));
    ScaleTool tool;
    tool.setToggled(6 | 64);
    tool.selected(host);
    tool.reset(host);
    ASSERT_EQ(valueText(tool, host, "fscy"), u"100");
    EXPECT_TRUE(tool.setValue("fscx", u"50", host, ValueEdit::Sample));
    EXPECT_TRUE(tool.setValue("fscx", u"0", host, ValueEdit::Sample));
    EXPECT_TRUE(tool.setValue("fscx", u"10", host, ValueEdit::Sample));
    EXPECT_TRUE(tool.setValue("fscx", u"10", host, ValueEdit::Commit));
    EXPECT_FALSE(host.g);
    const std::u8string &t = lineOf(*host.s, *host.activeLine())->text;
    const std::string text(t.begin(), t.end());
    EXPECT_NE(text.find("\\fscx10"), std::string::npos) << text;
    EXPECT_NE(text.find("\\fscy10"), std::string::npos) << text;
}

TEST(VisualTransformValues, TheOriginIsTypedWithTwoPointsOn)
{
    TestHost host;
    setUp(host, caseNamed("rotz-two-points"));
    RotationZTool tool;
    tool.setToggled(1);
    tool.selected(host);
    tool.reset(host);
    ASSERT_TRUE(tool.hasTwoPoints());
    const std::size_t steps = host.s->historySize();
    EXPECT_TRUE(tool.setValue("orgx", u"100", host));
    EXPECT_FALSE(host.g);
    EXPECT_EQ(host.s->historySize(), steps + 1);
    const std::u8string &t = lineOf(*host.s, *host.activeLine())->text;
    EXPECT_NE(std::string(t.begin(), t.end()).find("\\org(100,"), std::string::npos) << std::string(t.begin(), t.end());
}
