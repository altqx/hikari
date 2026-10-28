// T6: the Position shifter and the all-tags tool. The tools replay the
// legacy probe's captures (tests/fixtures/legacy-cases/inputs/visual-t6-cases.txt; tests/fixtures/legacy-observations/
// local-t6-visual-20261006): every Line's text and translation after each
// step, the editor's text and caret on the one-Line path, the commits, the
// tools' state and what they draw must be legacy's exactly, but for the
// approved departures (departureFor), which keep the capture as legacy's
// record. The tag definitions' file is read and written as legacy's
// LoadSettings and SaveSettings did, and the "Tag editing" dialog's model
// follows AllTagsEdition.

#include "hikari/application/all_tags.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/visual_all_tags.h"
#include "hikari/application/visual_shift.h"
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
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
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

// The probe's GetLineTextExtents (the T3 probe's).
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
    VideoView v, closed;
    bool noVideo = false;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    std::vector<core::LineId> targets;
    std::set<core::LineId> hidden;
    std::int64_t time = 0;
    LegacyTimebase base;
    mutable FixedMeasure measure;
    std::pair<long, long> caret{0, 0};
    std::vector<std::u16string> logged;
    int bells = 0;
    std::vector<AllTagsSetting> tags = defaultAllTags();
    // Every history step as legacy recorded its commit: SetModified's
    // "<action>" (MoveAll) or "dummy <action>" (Visuals' several-Line
    // path), EditBox::Send's "<action>" (the one-Line path).
    std::vector<std::string> history, sent;
    std::function<void()> edited;

    const VideoView &view() const override { return noVideo ? closed : v; }
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
        const bool shifter = g->history() == familyInfo(Family::PositionShifter).history;
        const bool several = !(g->targets().size() == 1 && g->targets().front() == activeLine());
        const std::size_t steps = s->historySize();
        auto r = g->commit(*s);
        g.reset();
        if (s->historySize() > steps) {
            if (shifter)
                history.push_back("46");
            else if (several)
                history.push_back("dummy 47");
            else
                sent.push_back("47");
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
    void bell() override
    {
        ++bells;
        logged.emplace_back(u"bell");
    }
    // How often a tool asked which Lines the Grid shows.
    mutable int shownAsked = 0;
    std::function<bool(core::LineId)> shownLines() const override
    {
        ++shownAsked;
        return [this](core::LineId line) { return !hidden.contains(line); };
    }
    const std::vector<AllTagsSetting> *allTagsSettings() const override { return &tags; }
    // HY1: the controller's (the tool's own last step, nothing since).
    bool undoStep(std::uint64_t revision) override
    {
        if (!s || g || s->revision() != revision || s->draftLine() || !s->canUndo())
            return false;
        return s->undo();
    }
};

std::string assTime(int ms)
{
    char buf[32];
    const int cs = ms / 10;
    std::snprintf(buf, sizeof buf, "%d:%02d:%02d.%02d", cs / 360000, (cs / 6000) % 60, (cs / 100) % 60, cs % 100);
    return buf;
}

// One case of inputs/visual-t6-cases.txt.
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
    std::vector<int> hidden;
    int active = 0;
    std::vector<int> select;
    long caretFrom = 0, caretTo = 0;
    std::vector<std::string> ops; // definitions, tool and later
};

std::vector<Case> readCases()
{
    std::ifstream in(HIKARI_VISUAL_T6_CASES);
    std::vector<Case> out;
    Case c;
    std::string text;
    bool inOps = false;
    std::string colours = "&H00FFFFFF,&H00000000,&H00FF0000,&H00000000";
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
        } else if (key == "tool" || key == "config" || key == "file" || key == "loadtags" || inOps) {
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
            c.styles.push_back("Style: " + name + ",Garamond," + fs + "," + colours + ",0,0,0,0," + sx + "," + sy + ",0," +
                               angle + ",0," + outline + "," + shadow + "," + an + "," + ml + "," + mr + "," + mv + ",1");
        } else if (key == "stylecolour") {
            // The last Style's colours.
            std::string c1, c2, c3, c4;
            line >> c1 >> c2 >> c3 >> c4;
            const auto strip = [](std::string s) {
                if (!s.empty() && s.back() == '&')
                    s.pop_back();
                return s;
            };
            std::string &style = c.styles.back();
            const auto comma = style.find(',', style.find(',', style.find(',') + 1) + 1); // after Fontsize
            const auto end = style.find(",0,0,0,0,", comma);
            style = style.substr(0, comma + 1) + strip(c1) + "," + strip(c2) + "," + strip(c3) + "," + strip(c4) +
                    style.substr(end);
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
        } else if (key == "hidden") {
            int i;
            while (line >> i)
                c.hidden.push_back(i);
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
    QFile f(QStringLiteral(HIKARI_VISUAL_T6_OBSERVATIONS));
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

std::u16string u16(const QJsonValue &v)
{
    return v.toString().toStdU16String();
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
    if (all.empty())
        return;
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
    for (int i : c.hidden)
        host.hidden.insert(all[static_cast<std::size_t>(i)]);
    host.time = c.time;
    host.base = LegacyTimebase({}, c.fps);
    host.caret = {c.caretFrom, c.caretTo};
}

Pointer pointerOf(const std::string &kind, std::istringstream &in)
{
    Pointer p;
    in >> p.x >> p.y;
    p.kind = kind == "press"     ? Pointer::Kind::Press
             : kind == "release" ? Pointer::Kind::Release
             : kind == "leave"   ? Pointer::Kind::Leave
                                 : Pointer::Kind::Move;
    if (kind == "wheel") {
        p.kind = Pointer::Kind::Wheel;
        in >> p.wheelSteps;
    }
    std::string word;
    while (in >> word) {
        if (word == "shift") {
            p.shift = true;
            continue;
        }
        const Pointer::Button b = word == "left" ? Pointer::Button::Left
                                  : word == "right" ? Pointer::Button::Right
                                                     : Pointer::Button::Middle;
        if (p.kind == Pointer::Kind::Press || p.kind == Pointer::Kind::Release)
            p.button = b;
        if (p.kind != Pointer::Kind::Release) {
            p.leftDown = p.leftDown || b == Pointer::Button::Left;
            p.rightDown = p.rightDown || b == Pointer::Button::Right;
            p.middleDown = p.middleDown || b == Pointer::Button::Middle;
        }
    }
    return p;
}

std::vector<std::string> strings(const QJsonValue &v, bool dropSame = false)
{
    std::vector<std::string> out;
    for (const QJsonValue &e : v.toArray()) {
        const std::string s = e.toString().toStdString();
        if (dropSame && s.ends_with(" same"))
            continue;
        out.push_back(s);
    }
    return out;
}

// The legacy definitions a state printed against the rewrite's.
void expectTags(const std::vector<AllTagsSetting> &tags, const QJsonArray &legacy)
{
    ASSERT_EQ(tags.size(), static_cast<std::size_t>(legacy.size()));
    for (std::size_t i = 0; i < tags.size(); ++i) {
        SCOPED_TRACE("definition " + std::to_string(i));
        const QJsonObject o = legacy[static_cast<qsizetype>(i)].toObject();
        const AllTagsSetting &t = tags[i];
        EXPECT_EQ(t.name, u16(o[QStringLiteral("name")]));
        EXPECT_EQ(t.tag, u16(o[QStringLiteral("tag")]));
        EXPECT_EQ(t.rangeMin, f(o[QStringLiteral("min")]));
        EXPECT_EQ(t.rangeMax, f(o[QStringLiteral("max")]));
        EXPECT_EQ(t.step, f(o[QStringLiteral("step")]));
        const QJsonArray values = o[QStringLiteral("values")].toArray();
        for (int v = 0; v < 4; ++v)
            EXPECT_EQ(t.values[static_cast<std::size_t>(v)], f(values[v])) << "value " << v;
        EXPECT_EQ(t.mode, o[QStringLiteral("mode")].toInt());
        EXPECT_EQ(t.digitsAfterDot, o[QStringLiteral("digits")].toInt());
        EXPECT_EQ(t.numOfValues, o[QStringLiteral("count")].toInt());
        EXPECT_EQ(t.tagMode, o[QStringLiteral("tagMode")].toInt());
    }
}

// What the tool draws against legacy's recorded Direct3D drawing: lines
// (each D3DX line strip a run of segments), the handles' and sliders'
// rectangles (a triangle strip and its line strip border), the \move end
// circles (a fan) and the texts (DRAWOUTTEXT: centred in their rectangle).
void expectDrawing(const Overlay &overlay, const QJsonArray &drawn, const TestHost &host)
{
    std::vector<OverlayLine> lines;
    std::vector<OverlayPolygon> polygons;
    std::vector<std::pair<PointF, std::uint32_t>> circles; // centre, fill
    std::vector<QJsonObject> texts;
    for (const QJsonValue &v : drawn) {
        const QJsonObject d = v.toObject();
        const QString kind = d[QStringLiteral("kind")].toString();
        if (kind == QLatin1String("line")) {
            const QJsonArray p = d[QStringLiteral("points")].toArray();
            for (qsizetype i = 0; i + 1 < p.size(); ++i)
                lines.push_back({pair(p[i]), pair(p[i + 1]), 2, static_cast<std::uint32_t>(d[QStringLiteral("colour")].toDouble())});
        } else if (kind == QLatin1String("primitive")) {
            const int type = d[QStringLiteral("type")].toInt();
            const QJsonArray p = d[QStringLiteral("points")].toArray();
            const QJsonArray colours = d[QStringLiteral("colours")].toArray();
            if (type == 5) { // a filled rectangle: 0 1 2 3 as left-top, right-top, left-bottom, right-bottom
                polygons.push_back({{pair(p[0]), pair(p[1]), pair(p[3]), pair(p[2])},
                                    static_cast<std::uint32_t>(colours[0].toDouble()),
                                    0});
            } else if (type == 3 && p.size() == 5 && !polygons.empty() && polygons.back().border == 0) {
                polygons.back().border = static_cast<std::uint32_t>(colours[0].toDouble());
            } else if (type == 6) {
                circles.push_back({pair(p[0]), static_cast<std::uint32_t>(colours[0].toDouble())});
            }
        } else if (kind == QLatin1String("text")) {
            texts.push_back(d);
        }
    }
    ASSERT_EQ(overlay.lines.size(), lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        SCOPED_TRACE("line " + std::to_string(i));
        EXPECT_EQ(overlay.lines[i].from, lines[i].from);
        EXPECT_EQ(overlay.lines[i].to, lines[i].to);
        EXPECT_EQ(overlay.lines[i].argb, lines[i].argb);
        EXPECT_EQ(overlay.lines[i].width, 2);
    }
    ASSERT_EQ(overlay.polygons.size(), polygons.size());
    for (std::size_t i = 0; i < polygons.size(); ++i) {
        SCOPED_TRACE("rectangle " + std::to_string(i));
        EXPECT_EQ(overlay.polygons[i].points, polygons[i].points);
        EXPECT_EQ(overlay.polygons[i].fill, polygons[i].fill);
        EXPECT_EQ(overlay.polygons[i].border, polygons[i].border);
    }
    // DrawCircle draws a fill and a border (two Overlay circles).
    ASSERT_EQ(overlay.circles.size(), circles.size() * 2);
    for (std::size_t i = 0; i < circles.size(); ++i) {
        EXPECT_EQ(overlay.circles[i * 2].centre, circles[i].first);
        EXPECT_EQ(overlay.circles[i * 2].radius, 6.f);
        EXPECT_EQ(overlay.circles[i * 2].argb, circles[i].second);
    }
    ASSERT_EQ(overlay.texts.size(), texts.size());
    for (std::size_t i = 0; i < texts.size(); ++i) {
        SCOPED_TRACE("text " + std::to_string(i));
        const QJsonObject &d = texts[i];
        const QJsonArray r = d[QStringLiteral("rect")].toArray();
        const OverlayText &t = overlay.texts[i];
        EXPECT_EQ(t.text, u16(d[QStringLiteral("text")]));
        EXPECT_EQ(t.argb, static_cast<std::uint32_t>(d[QStringLiteral("colour")].toDouble()));
        EXPECT_TRUE(t.outline);
        const auto [w, h] = host.measureLabel(t.text);
        const int left = r[0].toInt(), top = r[1].toInt(), right = r[2].toInt(), bottom = r[3].toInt();
        EXPECT_EQ(t.rect.left, left + ((right - left) - w) / 2);
        const int align = d[QStringLiteral("align")].toInt();
        EXPECT_EQ(t.rect.top, (align & 0x8) ? bottom - h : top);
    }
}

} // namespace

TEST(AllTagsDefinitions, DefaultsAreLegacysTwentyTwo)
{
    const auto tags = defaultAllTags();
    ASSERT_EQ(tags.size(), 22u);
    EXPECT_EQ(tags[0].name, u"blur");
    EXPECT_EQ(tags[0].step, 0.5f);
    EXPECT_EQ(tags[0].digitsAfterDot, 1);
    EXPECT_EQ(tags[0].tagMode, PasteInsert);
    EXPECT_EQ(tags[7].tag, u"1c");
    EXPECT_EQ(tags[7].numOfValues, 3);
    EXPECT_EQ(tags[20].tag, u"pos");
    EXPECT_EQ(tags[20].mode, 1);
    EXPECT_EQ(tags[20].tagMode, PasteMultiply);
    EXPECT_EQ(tags[21].tag, u"t");
    EXPECT_EQ(tags[21].numOfValues, 2);
    // AllTagsSetting(name): written as named, step 1, 0-100.
    const AllTagsSetting fresh = AllTagsSetting::named(u"fsp");
    EXPECT_EQ(fresh.tag, u"fsp");
    EXPECT_EQ(fresh.rangeMax, 100.f);
    EXPECT_EQ(fresh.step, 1.f);
    EXPECT_EQ(fresh.numOfValues, 1);
}

TEST(AllTagsDefinitions, ATextWithoutTheVersionGivesTheDefaultsAndReplacesIt)
{
    bool write = true;
    EXPECT_EQ(parseAllTags(u"", &write), defaultAllTags());
    EXPECT_FALSE(write); // no file: nothing written
    EXPECT_EQ(parseAllTags(u"Tag: old, blur, 0, 10, 1, 1, 0, 0, 0\n", &write), defaultAllTags());
    EXPECT_TRUE(write);
    EXPECT_TRUE(parseAllTags(u"HYDRA2.0\n", &write).empty());
    EXPECT_FALSE(write);
    // Saving writes nothing for no definitions.
    EXPECT_TRUE(writeAllTags({}).empty());
}

// PU5: the "Edit tags" window keeps each change as it is made, a step of
// its history.
TEST(AllTagsEdition, EachChangeIsKeptAndUndoneInSteps)
{
    using Check = AllTagsEdition::Check;
    using Number = AllTagsEdition::Number;
    AllTagsEdition e(defaultAllTags(), 40);
    EXPECT_EQ(e.selection(), 0); // out of range: the first
    EXPECT_EQ(e.current().name, u"blur");
    // Typing a field is one step however many keys it takes.
    EXPECT_EQ(e.setNumber(Number::Max, u"5"), Check::Ok);
    EXPECT_EQ(e.setNumber(Number::Max, u"50"), Check::Ok);
    EXPECT_EQ(e.tags()[0].rangeMax, 50.f); // kept at once
    EXPECT_EQ(e.undoStep(), AllTagsEdition::EditRange);
    EXPECT_EQ(e.setNumber(Number::Step, u"0,25"), Check::Ok); // a comma for the point
    EXPECT_EQ(e.current().step, 0.25f);
    EXPECT_EQ(e.setNumber(Number::Value0, u"2.5"), Check::Ok);
    EXPECT_EQ(e.setTag(u" \\bord\t"), Check::Ok); // pasted: the blanks around it go first
    EXPECT_EQ(e.current().tag, u"bord");
    EXPECT_EQ(e.setTag(u"\\\\be"), Check::Ok); // its backslashes dropped
    EXPECT_EQ(e.current().tag, u"be");
    e.setCount(2);
    e.setChangeOption(PasteMultiplyPlus);
    e.setPlacing(2);
    EXPECT_EQ(e.current().numOfValues, 2);
    EXPECT_EQ(e.current().tagMode, PasteMultiplyPlus);
    EXPECT_EQ(e.current().mode, 2);
    for (const int step : {AllTagsEdition::ChangePlacing, AllTagsEdition::ChangeOption, AllTagsEdition::EditCount,
                           AllTagsEdition::EditTag, AllTagsEdition::EditValue, AllTagsEdition::EditStep,
                           AllTagsEdition::EditRange}) {
        EXPECT_EQ(e.undoStep(), step);
        EXPECT_TRUE(e.undo());
    }
    EXPECT_FALSE(e.canUndo());
    EXPECT_EQ(e.tags(), defaultAllTags());
    EXPECT_TRUE(e.redo());
    EXPECT_EQ(e.current().rangeMax, 50.f);
}

TEST(AllTagsEdition, AValueTheToolCannotUseIsRefusedAndTheLastKept)
{
    using Check = AllTagsEdition::Check;
    using Number = AllTagsEdition::Number;
    AllTagsEdition e(defaultAllTags(), 7); // color1, 0 to 255, step 1
    std::u16string taken;
    EXPECT_EQ(e.setName(u""), Check::Empty);
    EXPECT_EQ(e.setName(u"a,b"), Check::Separator); // the file's separator
    EXPECT_EQ(e.setName(u"BLUR", &taken), Check::Taken); // ignoring case
    EXPECT_EQ(taken, u"blur");
    EXPECT_EQ(e.setTag(u""), Check::Empty);
    EXPECT_EQ(e.setTag(u"\\"), Check::Empty);
    EXPECT_EQ(e.setTag(u" \\ "), Check::Empty);
    EXPECT_EQ(e.setTag(u"1c,2"), Check::Separator);
    EXPECT_EQ(e.setNumber(Number::Max, u"-"), Check::NotANumber);
    EXPECT_EQ(e.setNumber(Number::Max, u"1e9"), Check::OutOfRange);
    EXPECT_EQ(e.setNumber(Number::Max, u"inf"), Check::NotANumber);
    // Legacy Save's checks, said for the field typed.
    EXPECT_EQ(e.setNumber(Number::Max, u"-5"), Check::MaxNotAboveMin);
    EXPECT_EQ(e.setNumber(Number::Min, u"300"), Check::MinNotBelowMax);
    EXPECT_EQ(e.setNumber(Number::Step, u"0"), Check::StepNotPositive);
    EXPECT_EQ(e.setNumber(Number::Step, u"200"), Check::StepTooLarge); // more than half the range
    EXPECT_EQ(e.setNumber(Number::Max, u"1"), Check::StepTooLarge);
    EXPECT_EQ(e.setNumber(Number::Digits, u"7"), Check::OutOfRange);
    EXPECT_EQ(e.setNumber(Number::Digits, u"1.5"), Check::OutOfRange);
    EXPECT_EQ(e.current(), defaultAllTags()[7]);
    EXPECT_FALSE(e.canUndo());
    EXPECT_EQ(e.setNumber(Number::Digits, u"2"), Check::Ok);
    EXPECT_EQ(e.setNumber(Number::Step, u"127.5"), Check::Ok); // half the range
}

TEST(AllTagsEdition, NewDuplicateDeleteReplaceAndRestore)
{
    AllTagsEdition e(defaultAllTags(), 3);
    // New tag: no tag yet, so not among the definitions the tool takes.
    e.addTag();
    EXPECT_EQ(e.selection(), 22);
    EXPECT_EQ(e.current().name, u"New tag");
    EXPECT_TRUE(e.current().tag.empty());
    EXPECT_EQ(e.current().rangeMax, 100.f);
    EXPECT_EQ(e.current().step, 1.f);
    EXPECT_EQ(e.usable().size(), 22u);
    EXPECT_EQ(e.setTag(u"be"), AllTagsEdition::Check::Ok);
    EXPECT_EQ(e.usable().size(), 23u);
    e.addTag();
    EXPECT_EQ(e.current().name, u"New tag 2");
    // Duplicate after it, "copy".
    e.select(0);
    e.duplicate();
    EXPECT_EQ(e.selection(), 1);
    EXPECT_EQ(e.current().name, u"blur copy");
    EXPECT_EQ(e.current().tag, u"blur");
    // A taken name replaced: the other goes, one step.
    std::u16string taken;
    EXPECT_EQ(e.setName(u"Border", &taken), AllTagsEdition::Check::Taken);
    e.replaceNamed(u"Border");
    EXPECT_EQ(e.current().name, u"Border");
    EXPECT_EQ(e.current().tag, u"blur");
    EXPECT_EQ(e.tags().size(), 24u);
    EXPECT_EQ(e.undoStep(), AllTagsEdition::ReplaceTag);
    // Delete: never the last one.
    EXPECT_TRUE(e.remove());
    EXPECT_EQ(e.undoStep(), AllTagsEdition::DeleteTag);
    // Restore: legacy's 22, one step; Undo all: the opening ones.
    e.select(5);
    const std::u16string name = e.current().name;
    e.restoreDefaults();
    EXPECT_EQ(e.tags(), defaultAllTags());
    EXPECT_EQ(e.current().name, name);
    EXPECT_EQ(e.undoStep(), AllTagsEdition::RestoreDefaults);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.tags().size(), 23u);
    EXPECT_TRUE(e.undoAll());
    EXPECT_EQ(e.tags(), defaultAllTags());
    EXPECT_EQ(e.undoStep(), AllTagsEdition::UndoAll);
    AllTagsEdition one({AllTagsSetting::named(u"x")}, 0);
    EXPECT_FALSE(one.remove());
    // Nor the last one with a tag while a new one waits for its own: the
    // tool and the file keep a definition.
    AllTagsSetting glow = AllTagsSetting::named(u"glow");
    glow.tag = u"be";
    AllTagsEdition draft({glow}, 0);
    draft.addTag();
    EXPECT_TRUE(draft.canRemove()); // the new one
    draft.select(0);
    EXPECT_FALSE(draft.canRemove());
    EXPECT_FALSE(draft.remove());
    // Nor replaced by it: its name taken says to fill in Tag first, and
    // Replace does nothing (Codex on #349).
    draft.select(1);
    std::u16string takenName;
    EXPECT_EQ(draft.setName(u"Glow", &takenName), AllTagsEdition::Check::TakenLastUsable);
    EXPECT_EQ(takenName, u"glow");
    draft.replaceNamed(u"Glow");
    EXPECT_EQ(draft.tags().size(), 2u);
    EXPECT_EQ(draft.usable(), std::vector<AllTagsSetting>{glow});
    ASSERT_EQ(draft.setTag(u"blur"), AllTagsEdition::Check::Ok);
    EXPECT_EQ(draft.setName(u"Glow"), AllTagsEdition::Check::Taken);
    draft.replaceNamed(u"Glow");
    ASSERT_EQ(draft.tags().size(), 1u);
    EXPECT_EQ(draft.current().tag, u"blur");
    ASSERT_TRUE(draft.undo());
    ASSERT_TRUE(draft.undo());
    EXPECT_TRUE(draft.remove());
    EXPECT_EQ(draft.tags(), std::vector<AllTagsSetting>{glow});
    // The preview is what Set writes: the Line's own tag replaced in place,
    // else a block of its own at the start (Codex on #349).
    AllTagsSetting border = AllTagsSetting::named(u"border");
    border.tag = u"bord";
    const auto replaced = allTagsSetPreview(border, u"{\\bord5\\i1}text");
    EXPECT_EQ(replaced.before + replaced.tag + replaced.after, u"{\\bord0\\i1}text");
    EXPECT_EQ(replaced.tag, u"\\bord0");
    const auto placed = allTagsSetPreview(border, u"text");
    EXPECT_EQ(placed.before + placed.tag + placed.after, u"{\\bord0}text");
    const auto empty = allTagsSetPreview(border, u"");
    EXPECT_EQ(empty.before + empty.tag + empty.after, u"{\\bord0}");
    // A range refused for its step names half the range as typed.
    AllTagsSetting wide = AllTagsSetting::named(u"wide");
    wide.tag = u"fscx";
    wide.rangeMin = 0;
    wide.rangeMax = 100;
    wide.step = 50;
    AllTagsEdition ranged({wide}, 0);
    float limit = -1;
    EXPECT_EQ(ranged.setNumber(AllTagsEdition::Number::Max, u"80", &limit), AllTagsEdition::Check::StepTooLarge);
    EXPECT_EQ(limit, 40.f);
    EXPECT_EQ(ranged.setNumber(AllTagsEdition::Number::Max, u"-5", &limit), AllTagsEdition::Check::MaxNotAboveMin);
    EXPECT_EQ(limit, 0.f);
    EXPECT_EQ(ranged.setNumber(AllTagsEdition::Number::Min, u"100", &limit), AllTagsEdition::Check::MinNotBelowMax);
    EXPECT_EQ(limit, 100.f);
}

TEST(AllTagsEdition, TheStripsNamesSectionsAndSetText)
{
    const auto tags = defaultAllTags();
    const auto byName = [&](std::u16string_view n) {
        return *std::ranges::find_if(tags, [&](const AllTagsSetting &t) { return t.name == n; });
    };
    // HY1's sections: Common, More, the user's own.
    std::vector<AllTagsSetting> list{byName(u"blur"), byName(u"fax"), AllTagsSetting::named(u"glow")};
    EXPECT_EQ(allTagsSections(list), (std::vector<int>{0, 1, 2}));
    // The values' names.
    EXPECT_EQ(allTagsValueNames(byName(u"blur")), std::vector<std::u16string>{u"Value"});
    EXPECT_EQ(allTagsValueNames(byName(u"color1")), (std::vector<std::u16string>{u"R", u"G", u"B"}));
    EXPECT_EQ(allTagsValueNames(byName(u"position")), (std::vector<std::u16string>{u"X", u"Y"}));
    EXPECT_EQ(allTagsValueNames(byName(u"fading")), (std::vector<std::u16string>{u"In", u"Out"}));
    EXPECT_EQ(allTagsValueNames(byName(u"tanimation")), (std::vector<std::u16string>{u"Start", u"End"}));
    AllTagsSetting three = AllTagsSetting::named(u"be");
    three.numOfValues = 3;
    EXPECT_EQ(allTagsValueNames(three), (std::vector<std::u16string>{u"1", u"2", u"3"}));
    // What Set writes at the starting values.
    AllTagsSetting blur = byName(u"blur");
    blur.values[0] = 2.5f;
    EXPECT_EQ(allTagsSetText(blur), u"\\blur2.5");
    AllTagsSetting colour = byName(u"color1");
    colour.values = {250.f, 214.f, 92.f, 0.f};
    EXPECT_EQ(allTagsSetText(colour), u"\\1c&H5CD6FA&");
    AllTagsSetting alpha = byName(u"alpha");
    alpha.values[0] = 128.f;
    EXPECT_EQ(allTagsSetText(alpha), u"\\alpha&H80&");
    AllTagsSetting fad = byName(u"fading");
    fad.values = {200.f, 350.f, 0.f, 0.f};
    EXPECT_EQ(allTagsSetText(fad), u"\\fad(200,350)");
    EXPECT_EQ(allTagsSetText(byName(u"tanimation")), u"\\t(0,0,)");
}

// Esc during a drag (the host cancels the gesture and resets the tool):
// nothing is recorded, and moving on or releasing the button afterwards
// writes nothing either. Legacy had no Esc for these tools.
TEST(VisualShiftEdit, EscEndsTheDragWithoutAStep)
{
    TestHost host;
    Case c;
    c.clientW = 640;
    c.clientH = 400;
    c.panel = 40;
    c.frame.width = 1280;
    c.frame.height = 720;
    c.frame.sarNum = 0;
    c.frame.sarDen = 1;
    c.scriptW = 1920;
    c.scriptH = 1080;
    c.time = 1500;
    c.fps = 25;
    c.styles.push_back("Style: Default,Garamond,40,&H00FFFFFF,&H00000000,&H00FF0000,&H00000000,0,0,0,0,100,100,0,0,0,2,"
                       "2,2,20,20,20,1");
    c.lines.push_back("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(960,540)\\blur2}Hello");
    c.translations.push_back({});
    setUp(host, c);
    const std::size_t steps = host.s->historySize();
    const auto press = [&](VisualTool &tool, int x, int y) {
        Pointer p;
        p.kind = Pointer::Kind::Press;
        p.button = Pointer::Button::Left;
        p.leftDown = true;
        p.x = x;
        p.y = y;
        tool.pointer(p, host);
    };
    const auto move = [&](VisualTool &tool, int x, int y) {
        Pointer p;
        p.kind = Pointer::Kind::Move;
        p.leftDown = true;
        p.x = x;
        p.y = y;
        tool.pointer(p, host);
    };
    const auto release = [&](VisualTool &tool, int x, int y) {
        Pointer p;
        p.kind = Pointer::Kind::Release;
        p.button = Pointer::Button::Left;
        p.x = x;
        p.y = y;
        tool.pointer(p, host);
    };
    const auto escape = [&](VisualTool &tool) {
        host.cancelGesture();
        tool.reset(host);
    };
    {
        PositionShifterTool shifter;
        shifter.selected(host);
        shifter.reset(host);
        press(shifter, 320, 180);
        move(shifter, 340, 180);
        ASSERT_TRUE(host.g);
        escape(shifter);
        EXPECT_FALSE(host.g);
        move(shifter, 360, 180);
        release(shifter, 360, 180);
        EXPECT_FALSE(host.g);
        EXPECT_EQ(host.s->historySize(), steps);
        EXPECT_EQ(lineOf(*host.s, *host.s->selection().active)->text, u8"{\\pos(960,540)\\blur2}Hello");
    }
    {
        AllTagsTool tags;
    tags.setLegacySliders(true); // the sliders' coordinates
        tags.setLegacySliders(true); // the sliders' coordinates
        tags.setToggled(0); // Add, blur
        tags.selected(host);
        tags.reset(host);
        const float thumb = 30 + 2 * 5.8f;
        press(tags, static_cast<int>(thumb), 30);
        move(tags, 100, 30);
        ASSERT_TRUE(host.g);
        escape(tags);
        EXPECT_FALSE(tags.sliders()[0].holding);
        move(tags, 150, 30);
        release(tags, 150, 30);
        EXPECT_FALSE(host.g);
        EXPECT_EQ(host.s->historySize(), steps);
        EXPECT_EQ(lineOf(*host.s, *host.s->selection().active)->text, u8"{\\pos(960,540)\\blur2}Hello");
    }
}

// A gesture over a large batch asks the host for the shown Lines once, not
// once per target, and still leaves the hidden ones out.
TEST(VisualShiftEdit, ALargeBatchAsksForTheShownLinesOnce)
{
    TestHost host;
    Case c;
    c.clientW = 640;
    c.clientH = 400;
    c.panel = 40;
    c.frame.width = 1280;
    c.frame.height = 720;
    c.frame.sarNum = 0;
    c.frame.sarDen = 1;
    c.scriptW = 1920;
    c.scriptH = 1080;
    c.time = 1500;
    c.fps = 25;
    c.styles.push_back("Style: Default,Garamond,40,&H00FFFFFF,&H00000000,&H00FF0000,&H00000000,0,0,0,0,100,100,0,0,0,2,"
                       "2,2,20,20,20,1");
    constexpr int count = 2000;
    for (int i = 0; i < count; ++i) {
        c.lines.push_back("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(960,540)\\blur2}Line " + std::to_string(i));
        c.translations.push_back({});
        c.select.push_back(i);
        if (i % 3 == 1)
            c.hidden.push_back(i);
    }
    setUp(host, c);
    std::vector<core::LineId> shown;
    for (const core::LineId id : host.targets)
        if (!host.hidden.contains(id))
            shown.push_back(id);
    const auto press = [&](VisualTool &tool, int x, int y) {
        Pointer p;
        p.kind = Pointer::Kind::Press;
        p.button = Pointer::Button::Left;
        p.leftDown = true;
        p.x = x;
        p.y = y;
        tool.pointer(p, host);
    };
    {
        PositionShifterTool shifter;
        shifter.selected(host);
        shifter.reset(host);
        host.shownAsked = 0;
        press(shifter, 320, 180);
        ASSERT_TRUE(host.g);
        EXPECT_EQ(host.shownAsked, 1);
        EXPECT_EQ(host.g->targets(), shown);
        host.cancelGesture();
        shifter.reset(host);
    }
    {
        AllTagsTool tags;
    tags.setLegacySliders(true); // the sliders' coordinates
        tags.setLegacySliders(true); // the sliders' coordinates
        tags.setToggled(0); // Add, blur
        tags.selected(host);
        tags.reset(host);
        host.shownAsked = 0;
        const float thumb = 30 + 2 * 5.8f;
        press(tags, static_cast<int>(thumb), 30);
        Pointer drag;
        drag.kind = Pointer::Kind::Move;
        drag.leftDown = true;
        drag.x = 100;
        drag.y = 30;
        tags.pointer(drag, host);
        ASSERT_TRUE(host.g);
        EXPECT_EQ(host.shownAsked, 1);
        EXPECT_EQ(host.g->targets(), shown);
        host.cancelGesture();
        tags.reset(host);
    }
}

namespace {

// The probe's usual view (640x400 with a 40-pixel panel over a 1280x720
// frame, a 1920x1080 script: the coefficient 3) with these Lines, the first
// active.
Case departureCase(std::vector<std::string> texts)
{
    Case c;
    c.clientW = 640;
    c.clientH = 400;
    c.panel = 40;
    c.frame.width = 1280;
    c.frame.height = 720;
    c.frame.sarNum = 0;
    c.frame.sarDen = 1;
    c.scriptW = 1920;
    c.scriptH = 1080;
    c.time = 1500;
    c.fps = 25;
    c.styles.push_back("Style: Default,Garamond,40,&H00FFFFFF,&H00000000,&H00FF0000,&H00000000,0,0,0,0,100,100,0,0,0,2,"
                       "2,2,20,20,20,1");
    for (const auto &text : texts) {
        c.lines.push_back("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,," + text);
        c.translations.push_back({});
    }
    return c;
}

Pointer pointerAt(Pointer::Kind kind, int x, int y, Pointer::Button button = Pointer::Button::None, bool left = false)
{
    Pointer p;
    p.kind = kind;
    p.x = x;
    p.y = y;
    p.button = button;
    p.leftDown = left;
    return p;
}

} // namespace

// T6-gradient-hard-space (compatibility-decisions.md): a \h right after a
// block is one character with the next, as a \h at the start is. Legacy's
// ReplaceAllByChar stepped past the block onto the "h" and put the tag
// inside the \h ("{\blur12.1}\{\blur9.1}h…", tags-gradient-text).
TEST(VisualShiftDeparture, TheTextGradientKeepsAHardSpaceAfterABlock)
{
    // tags-gradient-text: an increasing gradient, then a decreasing one.
    TestHost host;
    setUp(host, departureCase({"\\hAb c{\\i1}d\\Ne\\h"}));
    AllTagsTool tags;
    tags.setLegacySliders(true); // the sliders' coordinates
    tags.setToggled(PasteGradientTextIncrease << 20); // blur
    tags.selected(host);
    tags.reset(host);
    tags.pointer(pointerAt(Pointer::Kind::Press, 41, 30, Pointer::Button::Left, true), host);
    tags.pointer(pointerAt(Pointer::Kind::Move, 100, 30, Pointer::Button::None, true), host);
    tags.pointer(pointerAt(Pointer::Kind::Release, 100, 30, Pointer::Button::Left), host);
    const core::LineId active = *host.s->selection().active;
    EXPECT_EQ(lineOf(*host.s, active)->text, u8"{\\blur0}\\hA{\\blur0.5}b {\\blur0.9}c{\\i1\\blur1.4}d\\N{\\blur1.9}e\\h");
    tags.setToggled(PasteGradientTextDecrease << 20, &host);
    tags.reset(host);
    tags.pointer(pointerAt(Pointer::Kind::Press, 100, 38, Pointer::Button::Left, true), host);
    tags.pointer(pointerAt(Pointer::Kind::Release, 100, 38, Pointer::Button::Left), host);
    EXPECT_EQ(lineOf(*host.s, active)->text, u8"{\\blur12.1}\\hA{\\blur9.6}b {\\blur6.9}c{\\i1\\blur4.4}d\\N{\\blur1.9}e\\h");
}

// T6-gradient-line-break (compatibility-decisions.md): a \N right after a
// block takes the next character with the block's tag, as a \h does.
// Legacy's ReplaceAllByChar stepped past the block onto the "N" and put a
// tag inside the \N ("{\blur0}A{\i1\blur0.9}\{\blur1.9}N{\blur2.8}b …",
// four tags for three characters).
TEST(VisualShiftDeparture, TheTextGradientKeepsALineBreakAfterABlock)
{
    TestHost host;
    setUp(host, departureCase({"A{\\i1}\\Nb c"}));
    AllTagsTool tags;
    tags.setLegacySliders(true); // the sliders' coordinates
    tags.setToggled(PasteGradientTextIncrease << 20); // blur
    tags.selected(host);
    tags.reset(host);
    tags.pointer(pointerAt(Pointer::Kind::Press, 41, 30, Pointer::Button::Left, true), host);
    tags.pointer(pointerAt(Pointer::Kind::Move, 100, 30, Pointer::Button::None, true), host);
    tags.pointer(pointerAt(Pointer::Kind::Release, 100, 30, Pointer::Button::Left), host);
    const std::u8string text = lineOf(*host.s, *host.s->selection().active)->text;
    EXPECT_EQ(text.find(u8"\\{"), std::u8string::npos) << std::string(text.begin(), text.end());
    EXPECT_EQ(text, u8"{\\blur0}A{\\i1\\blur0.9}\\Nb {\\blur1.9}c");
}

// T6-clip-scale-carry (compatibility-decisions.md): a vector clip's scale is
// each Line's own. Legacy's MoveAll set vectorClipScale only when a Line's
// clip had a scale, so the next Line's clip was drawn at the last one's.
TEST(VisualShiftDeparture, AVectorClipsScaleIsEachLinesOwn)
{
    TestHost host;
    setUp(host, departureCase({"{\\clip(3,m 0 0 l 1200 0 1200 1200)}A", "{\\clip(m 30 30 l 300 30 300 300)}B"}));
    PositionShifterTool shifter;
    shifter.setToggled(PositionShifterTool::Clip);
    shifter.selected(host);
    shifter.reset(host);
    EXPECT_EQ(shifter.vectorClipScale(), 4.f);
    const core::LineId second = ids(*host.s)[1];
    host.s->setSelection({second, {second}, second, std::nullopt});
    host.targets = {second};
    shifter.reset(host);
    EXPECT_EQ(shifter.vectorClipScale(), 1.f);
    ASSERT_EQ(shifter.elements().size(), 1u);
    ASSERT_TRUE(shifter.elements()[0].points);
    EXPECT_EQ((*shifter.elements()[0].points)[0].x, 10.f); // 30 script pixels at the coefficient 3
    EXPECT_EQ((*shifter.elements()[0].points)[2].y, 100.f);
}

// T6-drag-offset (compatibility-decisions.md): a drag keeps the exact offset
// between the pointer and the handle. Legacy kept it as a wxPoint, so the
// handle jumped by the fraction at the first move and a 10-pixel drag of
// \pos(961,541) wrote \pos(990,540).
TEST(VisualShiftDeparture, ADragKeepsTheExactOffset)
{
    TestHost host;
    setUp(host, departureCase({"{\\pos(961,541)}Hello"}));
    PositionShifterTool shifter;
    shifter.selected(host);
    shifter.reset(host);
    ASSERT_EQ(shifter.elements().size(), 1u);
    const PointF handle = shifter.elements()[0].elem;
    shifter.pointer(pointerAt(Pointer::Kind::Press, 320, 180, Pointer::Button::Left, true), host);
    shifter.pointer(pointerAt(Pointer::Kind::Move, 330, 180, Pointer::Button::None, true), host);
    EXPECT_EQ(shifter.elements()[0].elem.x, handle.x + 10);
    EXPECT_EQ(shifter.elements()[0].elem.y, handle.y);
    shifter.pointer(pointerAt(Pointer::Kind::Release, 330, 180, Pointer::Button::Left), host);
    EXPECT_EQ(lineOf(*host.s, *host.s->selection().active)->text, u8"{\\pos(991,541)}Hello");
}

// T6-wheel-one-step (compatibility-decisions.md): one wheel step over a
// definition of several values is one history step. Legacy's every slider
// took the wheel and wrote its own value as a step: three for a colour
// (tags-wheel-colour).
TEST(VisualShiftDeparture, OneWheelStepIsOneHistoryStep)
{
    TestHost host;
    Case c = departureCase({"{\\1c&H102030&}Colour"});
    c.caretFrom = c.caretTo = 1;
    setUp(host, c);
    AllTagsTool tags;
    tags.setLegacySliders(true); // the sliders' coordinates
    tags.setToggled((PasteInsert << 20) + 7); // Insert, 1c
    tags.selected(host);
    tags.reset(host);
    const std::size_t steps = host.s->historySize();
    Pointer wheel = pointerAt(Pointer::Kind::Wheel, 300, 200);
    wheel.wheelSteps = 1;
    tags.pointer(wheel, host);
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(host.sent.size(), 1u);
    EXPECT_EQ(lineOf(*host.s, *host.s->selection().active)->text, u8"{\\1c&H112131&}Colour");
}

namespace {

// HY1: three Lines picked (the first active), the probe's usual view.
Case hydraCase(std::vector<std::string> texts, bool picked)
{
    Case c = departureCase(std::move(texts));
    if (picked)
        for (int i = 0; i < static_cast<int>(c.lines.size()); ++i)
            c.select.push_back(i);
    return c;
}

const ToolValue *valueNamed(const std::vector<ToolValue> &values, const std::string &name)
{
    for (const ToolValue &v : values)
        if (v.name == name)
            return &v;
    return nullptr;
}

std::u16string u16(const std::string &s)
{
    return std::u16string(s.begin(), s.end());
}

std::string textOf(const EditSession &s, core::LineId id)
{
    const std::u8string t = lineOf(s, id)->text;
    return std::string(t.begin(), t.end());
}

} // namespace

// HY1 (#271): every built-in definition takes every change option through
// the strip: a value typed for the change's marked field is one history
// step ("Visual Hydra tool") that puts the tag in each picked Line.
TEST(HydraStrip, EveryDefinitionTakesEveryChangeOption)
{
    const auto defaults = defaultAllTags();
    ASSERT_EQ(defaults.size(), 22u);
    for (std::size_t def = 0; def < defaults.size(); ++def) {
        for (int mode = PasteAdd; mode <= PasteGradientLineDecrease; ++mode) {
            SCOPED_TRACE(std::string(defaults[def].tag.begin(), defaults[def].tag.end()) + " mode " +
                         std::to_string(mode));
            TestHost host;
            setUp(host, hydraCase({"{\\pos(960,540)}Hello there", "Second line", "{\\an7}Third line"}, true));
            AllTagsTool tags;
            tags.setToggled((mode << 20) + static_cast<int>(def));
            tags.selected(host);
            tags.reset(host);
            const auto values = tags.values(host);
            ASSERT_FALSE(values.empty());
            const auto marked = std::ranges::find_if(values, [](const ToolValue &v) { return v.marked; });
            ASSERT_NE(marked, values.end());
            double now = 0;
            ASSERT_TRUE(parseValue(marked->text, now));
            const AllTagsSetting &d = defaults[def];
            // A move of three steps, inside the tag's range for the values
            // that are kept there.
            double to = now + 3 * d.step;
            if (marked->name.starts_with("value") || marked->name.starts_with("to"))
                to = to > d.rangeMax ? now - 3 * d.step : to;
            const std::size_t steps = host.s->historySize();
            EXPECT_TRUE(tags.setValue(marked->name, u16(std::to_string(to)), host));
            EXPECT_EQ(host.s->historySize(), steps + 1);
            EXPECT_EQ(host.s->history().back().name, "Visual Hydra tool");
            EXPECT_FALSE(host.g);
            const std::string tag = "\\" + std::string(d.tag.begin(), d.tag.end());
            for (const core::LineId id : host.targets)
                EXPECT_NE(textOf(*host.s, id).find(tag), std::string::npos) << textOf(*host.s, id);
        }
    }
}

// HY1: Set's three placings for each definition on the active Line: at the
// caret, at the text's start, and the clip rectangle with \t; each is one
// step. With several Lines the placing is greyed (the tag goes at their
// text's start).
TEST(HydraStrip, SetsPlacingsOnTheActiveLine)
{
    const auto defaults = defaultAllTags();
    for (std::size_t def = 0; def < defaults.size(); ++def) {
        for (int placing = 0; placing < 3; ++placing) {
            const AllTagsSetting &d = defaults[def];
            SCOPED_TRACE(std::string(d.tag.begin(), d.tag.end()) + " placing " + std::to_string(placing));
            TestHost host;
            Case c = hydraCase({"{\\pos(960,540)}Hello there"}, false);
            c.caretFrom = c.caretTo = 20;
            setUp(host, c);
            host.caret = {20, 20};
            AllTagsTool tags;
            tags.setToggled((PasteInsert << 20) + static_cast<int>(def));
            tags.selected(host);
            tags.reset(host);
            const auto options = tags.options(host);
            const auto variant = std::ranges::find_if(options, [](const ToolOption &o) { return o.name == "variant"; });
            ASSERT_NE(variant, options.end());
            EXPECT_TRUE(variant->enabled);
            EXPECT_EQ(variant->choices.size(), 3u);
            // The host changes and saves the definition (the controller).
            host.tags[def].mode = static_cast<unsigned char>(placing);
            ASSERT_TRUE(tags.setOption("variant", placing, host));
            EXPECT_EQ(static_cast<int>(tags.actualTag().mode), placing) << "the placing is the definition's";
            const auto values = tags.values(host); // the pointer below is into it
            const ToolValue *value = valueNamed(values, "value0");
            ASSERT_TRUE(value);
            double now = 0;
            ASSERT_TRUE(parseValue(value->text, now));
            const double to = now + d.step <= d.rangeMax ? now + d.step : now - d.step;
            const std::size_t steps = host.s->historySize();
            EXPECT_TRUE(tags.setValue("value0", u16(std::to_string(to)), host));
            EXPECT_EQ(host.s->historySize(), steps + 1);
            const std::string text = textOf(*host.s, host.targets.front());
            EXPECT_NE(text.find("\\" + std::string(d.tag.begin(), d.tag.end())), std::string::npos) << text;
        }
    }
    TestHost host;
    setUp(host, hydraCase({"A", "B"}, true));
    AllTagsTool tags;
    tags.setToggled(PasteInsert << 20);
    tags.selected(host);
    tags.reset(host);
    const auto options = tags.options(host);
    const auto variant = std::ranges::find_if(options, [](const ToolOption &o) { return o.name == "variant"; });
    ASSERT_NE(variant, options.end());
    EXPECT_FALSE(variant->enabled);
}

// HY1: the change switch and what each entry shows beside it; the values
// say what they do.
TEST(HydraStrip, TheSwitchNamesTheValues)
{
    TestHost host;
    setUp(host, hydraCase({"{\\blur2\\1c&H302010&}Hello"}, false));
    AllTagsTool tags;
    tags.setToggled(PasteInsert << 20); // blur
    tags.selected(host);
    tags.reset(host);
    const auto names = [&] {
        std::vector<std::string> out;
        for (const ToolValue &v : tags.values(host))
            out.push_back(v.name + "=" + std::string(v.label.begin(), v.label.end()) + "/" +
                          std::string(v.group.begin(), v.group.end()) + "/" +
                          std::string(v.text.begin(), v.text.end()) + (v.marked ? "*" : ""));
        return out;
    };
    EXPECT_EQ(names(), std::vector<std::string>{"value0=Value//2*"});
    const auto option = [&](const std::string &name) {
        for (const ToolOption &o : tags.options(host))
            if (o.name == name)
                return o;
        return ToolOption{};
    };
    EXPECT_EQ(option("change").kind, ToolOption::Kind::Switch);
    EXPECT_EQ(option("change").index, 0);
    EXPECT_TRUE(option("change").afterValues);
    EXPECT_TRUE(option("tag").searchable);
    EXPECT_EQ(option("tag").notes.front(), u"\\blur");
    ASSERT_TRUE(tags.setOption("change", 1, host)); // Add
    EXPECT_EQ(tags.mode(), static_cast<int>(PasteAdd));
    EXPECT_EQ(names(), std::vector<std::string>{"by0=By//0*"});
    EXPECT_TRUE(option("variant").name.empty());
    ASSERT_TRUE(tags.setOption("change", 2, host)); // Multiply
    EXPECT_EQ(names(), (std::vector<std::string>{"start0=Start//2", "perLine0=Per line//0*"}));
    ASSERT_TRUE(tags.setOption("variant", 1, host)); // Add by line number
    EXPECT_EQ(tags.mode(), static_cast<int>(PasteMultiplyPlus));
    EXPECT_EQ(names(), std::vector<std::string>{"perLine0=Per line//0*"});
    ASSERT_TRUE(tags.setOption("change", 3, host)); // Gradient
    EXPECT_EQ(names(), (std::vector<std::string>{"from0=From//2", "to0=To//2*"}));
    EXPECT_TRUE(tags.markValue("from0", host));
    EXPECT_FALSE(tags.markValue("by0", host));
    EXPECT_EQ(names(), (std::vector<std::string>{"from0=From//2*", "to0=To//2"}));
    // A colour: R G B under the role, the swatch on the first.
    ASSERT_TRUE(tags.setOption("tag", 7, host)); // color1
    ASSERT_TRUE(tags.setOption("change", 0, host));
    const auto values = tags.values(host);
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0].label, u"R");
    EXPECT_EQ(values[0].text, u"16");
    EXPECT_EQ(values[2].text, u"48");
    EXPECT_EQ(values[0].swatch, 0xFF102030u);
    EXPECT_EQ(values[1].swatch, 0u);
}

// HY1: the picked Lines are counted and outlined (numbered) before a change.
TEST(HydraStrip, ThePickedLinesAreOutlinedAndNumbered)
{
    TestHost host;
    setUp(host, hydraCase({"{\\pos(400,200)}One", "{\\pos(900,500)}Two", "{\\pos(1400,800)}Three"}, true));
    AllTagsTool tags;
    tags.setToggled(PasteAdd << 20);
    tags.selected(host);
    tags.reset(host);
    const Overlay o = tags.overlay(host);
    ASSERT_EQ(o.polygons.size(), 3u);
    for (const auto &p : o.polygons) {
        EXPECT_EQ(p.fill, 0u);
        EXPECT_EQ(p.border, 0xFF6FD3FFu);
        EXPECT_EQ(p.points.size(), 4u);
    }
    ASSERT_EQ(o.texts.size(), 3u);
    EXPECT_EQ(o.texts[0].text, u"1");
    EXPECT_EQ(o.texts[2].text, u"3");
    // \pos(400,200) at the coefficient 3, \an2: the box's bottom centre.
    EXPECT_NEAR((o.polygons[0].points[0].x + o.polygons[0].points[1].x) / 2, 400.f / 3, 1.f);
    EXPECT_NEAR(o.polygons[0].points[2].y, 200.f / 3, 1.f);
    // The active Line alone: one outline, no number.
    host.targets = {host.targets.front()};
    tags.targetsChanged(host);
    const Overlay one = tags.overlay(host);
    EXPECT_EQ(one.polygons.size(), 1u);
    EXPECT_TRUE(one.texts.empty());
}

// HY1: a drag sideways over the video changes the marked value, previewing
// it, and applies it on release as one step; Esc drops it. The wheel steps
// the value; Shift and the wheel step through the list's order.
TEST(HydraStrip, ADragOrTheWheelChangesTheMarkedValue)
{
    TestHost host;
    setUp(host, hydraCase({"{\\blur2}Hello"}, false));
    host.caret = {3, 3};
    AllTagsTool tags;
    tags.setToggled(PasteAdd << 20); // blur, step 0.5
    tags.selected(host);
    tags.reset(host);
    const core::LineId line = host.targets.front();
    const std::size_t steps = host.s->historySize();
    tags.pointer(pointerAt(Pointer::Kind::Press, 300, 200, Pointer::Button::Left, true), host);
    tags.pointer(pointerAt(Pointer::Kind::Move, 301, 200, Pointer::Button::None, true), host);
    EXPECT_FALSE(host.g); // under three pixels
    tags.pointer(pointerAt(Pointer::Kind::Move, 310, 200, Pointer::Button::None, true), host);
    ASSERT_TRUE(host.g);
    EXPECT_EQ(textOf(*host.s, line), "{\\blur2}Hello"); // staged only
    EXPECT_EQ(valueNamed(tags.values(host), "by0")->text, u"5");
    const Overlay hud = tags.overlay(host);
    ASSERT_FALSE(hud.texts.empty());
    EXPECT_EQ(hud.texts.back().text, u"blur  0 → 5");
    tags.pointer(pointerAt(Pointer::Kind::Release, 310, 200, Pointer::Button::Left), host);
    EXPECT_FALSE(host.g);
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(textOf(*host.s, line), "{\\blur7}Hello");
    // Esc in a drag: nothing written.
    tags.pointer(pointerAt(Pointer::Kind::Press, 300, 200, Pointer::Button::Left, true), host);
    tags.pointer(pointerAt(Pointer::Kind::Move, 320, 200, Pointer::Button::None, true), host);
    ASSERT_TRUE(host.g);
    host.cancelGesture();
    tags.reset(host);
    tags.pointer(pointerAt(Pointer::Kind::Release, 320, 200, Pointer::Button::Left), host);
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(textOf(*host.s, line), "{\\blur7}Hello");
    // The wheel: one step of the marked value, one history step.
    Pointer wheel = pointerAt(Pointer::Kind::Wheel, 300, 200);
    wheel.wheelSteps = 1;
    tags.pointer(wheel, host);
    EXPECT_EQ(host.s->historySize(), steps + 2);
    EXPECT_EQ(textOf(*host.s, line), "{\\blur7.5}Hello"); // By's one step, not By again
    EXPECT_EQ(valueNamed(tags.values(host), "by0")->text, u"5"); // what Enter applies
    // Shift and the wheel: the next tag in the list's order (Common first).
    wheel.shift = true;
    wheel.wheelSteps = -1;
    tags.pointer(wheel, host);
    EXPECT_EQ(tags.actualTag().name, u"border"); // Common in the file's order
}

// HY1: the last change card: a value typed there undoes the change's step
// and does it again with the value on the same Lines, still one step; the
// card closes once anything else changed the Document.
TEST(HydraStrip, TheLastChangeIsDoneAgainInPlaceOfItsStep)
{
    TestHost host;
    setUp(host, hydraCase({"{\\blur1}One", "{\\blur2}Two", "Three"}, true));
    AllTagsTool tags;
    tags.setToggled(PasteAdd << 20);
    tags.selected(host);
    tags.reset(host);
    EXPECT_FALSE(tags.lastChange(host));
    const std::size_t steps = host.s->historySize();
    ASSERT_TRUE(tags.setValue("by0", u"2", host));
    EXPECT_EQ(host.s->historySize(), steps + 1);
    const auto all = ids(*host.s);
    EXPECT_EQ(textOf(*host.s, all[0]), "{\\blur3}One");
    EXPECT_EQ(textOf(*host.s, all[1]), "{\\blur4}Two");
    const auto last = tags.lastChange(host);
    ASSERT_TRUE(last);
    EXPECT_EQ(last->tag, u"blur");
    EXPECT_EQ(last->change, 1);
    EXPECT_EQ(last->lines, 3);
    ASSERT_EQ(last->values.size(), 1u);
    EXPECT_EQ(last->values[0].text, u"2");
    // The picker changes: the card still does the change on its Lines.
    host.targets = {all[0]};
    ASSERT_TRUE(tags.editLastChange("by0", u"5", host));
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(textOf(*host.s, all[0]), "{\\blur6}One");
    EXPECT_EQ(textOf(*host.s, all[1]), "{\\blur7}Two");
    EXPECT_EQ(tags.lastChange(host)->values[0].text, u"5");
    // One undo brings the Lines back as they were before the change.
    ASSERT_TRUE(host.s->undo());
    EXPECT_EQ(textOf(*host.s, all[0]), "{\\blur1}One");
    EXPECT_EQ(textOf(*host.s, all[1]), "{\\blur2}Two");
    EXPECT_FALSE(tags.editLastChange("by0", u"1", host)); // undone: the card is gone
    EXPECT_FALSE(tags.lastChange(host));
    EXPECT_EQ(textOf(*host.s, all[0]), "{\\blur1}One");
}

// HY1 (Codex on #339): the card keeps the Lines its change edited (the
// picked ones the Grid showed) and the values before it; done again it
// edits those Lines, whatever the Grid shows now.
TEST(HydraStrip, TheCardKeepsTheLinesAndValuesOfItsChange)
{
    TestHost host;
    setUp(host, hydraCase({"{\\blur1}One", "{\\blur2}Two", "{\\blur1}Three"}, true));
    const auto all = ids(*host.s);
    host.hidden.insert(all[1]);
    AllTagsTool tags;
    tags.setToggled(PasteAdd << 20);
    tags.selected(host);
    tags.reset(host);
    ASSERT_TRUE(tags.setValue("by0", u"2", host));
    EXPECT_EQ(textOf(*host.s, all[1]), "{\\blur2}Two");
    ASSERT_TRUE(tags.lastChange(host));
    EXPECT_EQ(tags.lastChange(host)->lines, 2);
    host.hidden.clear();
    ASSERT_TRUE(tags.editLastChange("by0", u"5", host));
    EXPECT_EQ(textOf(*host.s, all[0]), "{\\blur6}One");
    EXPECT_EQ(textOf(*host.s, all[1]), "{\\blur2}Two");
    EXPECT_EQ(textOf(*host.s, all[2]), "{\\blur6}Three");
    // A decreasing gradient leaves the active Line at To: the card's From is
    // still what the Line had before.
    TestHost other;
    setUp(other, hydraCase({"{\\blur1}One", "{\\blur1}Two"}, true));
    other.caret = {3, 3};
    AllTagsTool gradient;
    gradient.setToggled(PasteGradientLineDecrease << 20);
    gradient.selected(other);
    gradient.reset(other);
    ASSERT_TRUE(gradient.setValue("to0", u"5", other));
    const auto last = gradient.lastChange(other);
    ASSERT_TRUE(last);
    const ToolValue *from = valueNamed(last->values, "from0");
    ASSERT_TRUE(from);
    EXPECT_EQ(from->text, u"1");
}

// HY1 (Codex on #339): what ends the card and the strip's values: another
// placing, the time key's own step, another change option, a definition
// renamed in the tag editor.
TEST(HydraStrip, OtherChangesCloseTheCard)
{
    TestHost host;
    setUp(host, hydraCase({"{\\fad(100,200)\\blur1}Hello"}, false));
    host.caret = {3, 3};
    AllTagsTool tags;
    tags.setToggled(PasteInsert << 20); // blur, Set
    tags.selected(host);
    tags.reset(host);
    ASSERT_TRUE(tags.setValue("value0", u"3", host));
    ASSERT_TRUE(tags.lastChange(host));
    host.tags[0].mode = 1;
    ASSERT_TRUE(tags.setOption("variant", 1, host));
    EXPECT_FALSE(tags.lastChange(host)) << "the card redoes its own placing only";
    // Add's By goes back to 0 with another change option.
    ASSERT_TRUE(tags.setOption("change", 1, host));
    ASSERT_TRUE(tags.setValue("by0", u"2", host));
    EXPECT_EQ(valueNamed(tags.values(host), "by0")->text, u"2");
    ASSERT_TRUE(tags.setOption("change", 0, host));
    ASSERT_TRUE(tags.setOption("change", 1, host));
    EXPECT_EQ(valueNamed(tags.values(host), "by0")->text, u"0");
    // A definition renamed: the first is chosen again from its own values.
    ASSERT_TRUE(tags.setValue("by0", u"2", host));
    ASSERT_TRUE(tags.lastChange(host));
    std::vector<std::u16string> names;
    for (const AllTagsSetting &t : host.tags)
        names.push_back(t.name);
    host.tags[0].name = u"soft edge";
    tags.definitionsChanged(names, host);
    EXPECT_FALSE(tags.lastChange(host));
    EXPECT_EQ(valueNamed(tags.values(host), "by0")->text, u"0");
    // The time key's step is the last one now.
    ASSERT_TRUE(tags.setOption("tag", 10, host)); // fading, with its own Set
    ASSERT_TRUE(tags.setOption("change", 1, host));
    host.caret = {3, 3}; // in \fad again
    tags.reset(host);
    ASSERT_TRUE(tags.setValue("by0", u"10", host));
    ASSERT_TRUE(tags.lastChange(host));
    Key key;
    key.key = ',';
    key.control = true;
    EXPECT_TRUE(tags.key(key, host));
    EXPECT_FALSE(tags.lastChange(host));
}

// HY1 (Codex security review on #339): another tab with the same revision
// and active Line id does not take the card: it would undo that tab's step.
TEST(HydraStrip, TheCardStaysWithItsTab)
{
    TestHost host;
    setUp(host, hydraCase({"{\\blur1}One"}, false));
    AllTagsTool tags;
    tags.setToggled(PasteAdd << 20);
    tags.selected(host);
    tags.reset(host);
    ASSERT_TRUE(tags.setValue("by0", u"2", host));
    ASSERT_TRUE(tags.lastChange(host));
    const auto first = std::move(host.s); // the first tab stays open
    const std::uint64_t revision = first->revision();
    setUp(host, hydraCase({"{\\blur1}One"}, false));
    ASSERT_TRUE(host.s->run({"other", host.s->revision(), {ids(*host.s).front()}, [&](core::Document &d) {
                                 return d.editLine(ids(*host.s).front(), [](core::LineRecord &l) { l.text = u8"{\\blur9}One"; });
                             }}));
    ASSERT_EQ(host.s->revision(), revision);
    ASSERT_EQ(host.activeLine(), first->selection().active);
    EXPECT_FALSE(tags.editLastChange("by0", u"5", host));
    EXPECT_EQ(textOf(*host.s, ids(*host.s).front()), "{\\blur9}One");
    EXPECT_FALSE(tags.lastChange(host));
}

// HY1 (Codex on #339): a press on the letterbox bars starts no drag.
TEST(HydraStrip, ADragStartsOnThePictureOnly)
{
    TestHost host;
    Case c = hydraCase({"{\\blur2}Hello"}, false);
    c.clientH = 520; // 640x360 of picture in 640x480: bars of 60 above and below
    setUp(host, c);
    ASSERT_GT(host.v.videoRect().top, 20);
    AllTagsTool tags;
    tags.setToggled(PasteAdd << 20);
    tags.selected(host);
    tags.reset(host);
    const std::size_t steps = host.s->historySize();
    tags.pointer(pointerAt(Pointer::Kind::Press, 300, 10, Pointer::Button::Left, true), host);
    tags.pointer(pointerAt(Pointer::Kind::Move, 340, 10, Pointer::Button::None, true), host);
    EXPECT_FALSE(host.g);
    tags.pointer(pointerAt(Pointer::Kind::Release, 340, 10, Pointer::Button::Left), host);
    EXPECT_EQ(host.s->historySize(), steps);
}

// HY1 (Codex on #339): a moving Line's outline is where the Line is at the
// video's time.
TEST(HydraStrip, AMovingLineIsOutlinedWhereItIs)
{
    TestHost host;
    setUp(host, hydraCase({"{\\move(300,300,1500,300)}Moving"}, false)); // 1.0 s to 2.0 s, the video at 1.5 s
    AllTagsTool tags;
    tags.setToggled(PasteAdd << 20);
    tags.selected(host);
    tags.reset(host);
    const Overlay o = tags.overlay(host);
    ASSERT_EQ(o.polygons.size(), 1u);
    // Half way: x 900 at the coefficient 3, \an2's bottom centre.
    EXPECT_NEAR((o.polygons[0].points[0].x + o.polygons[0].points[1].x) / 2, 900.f / 3, 1.f);
}

// HY1 (Codex security review on #339): a drawing whose Bézier coordinates
// overflow a float's range is measured and outlined in bounded time.
TEST(HydraStrip, AnOverflowingDrawingIsOutlined)
{
    TestHost host;
    setUp(host, hydraCase({"{\\p1}m 0 0 b 1e38 0 0 1 0 1", "{\\p1}m 0 0 b 1e30 0 0 1 0 1"}, true));
    AllTagsTool tags;
    tags.setToggled(PasteAdd << 20);
    tags.selected(host);
    tags.reset(host);
    const Overlay o = tags.overlay(host); // returns
    EXPECT_LE(o.polygons.size(), 2u);
}

// HY1 (Codex on #339): on one Line a drag through several samples, or a
// scrub, writes what typing its last value writes, once (Multiply's Lines
// after the first are the ones it changes: none here).
TEST(HydraStrip, ADragOnOneLineWritesItsLastValueOnce)
{
    const std::vector<std::pair<int, std::string>> cases{{PasteAdd, "by0"},
                                                          {PasteInsert, "value0"},
                                                          {PasteGradientTextIncrease, "to0"},
                                                          {PasteGradientTextDecrease, "from0"}};
    for (const auto &[mode, name] : cases) {
        SCOPED_TRACE("mode " + std::to_string(mode));
        const auto fresh = [&](TestHost &host, AllTagsTool &tags) {
            setUp(host, hydraCase({"{\\blur2}Hello there"}, false));
            host.caret = {3, 3};
            tags.setToggled(mode << 20);
            tags.selected(host);
            tags.reset(host);
        };
        TestHost dragged;
        AllTagsTool drag;
        fresh(dragged, drag);
        ASSERT_TRUE(drag.markValue(name, dragged));
        drag.pointer(pointerAt(Pointer::Kind::Press, 300, 200, Pointer::Button::Left, true), dragged);
        for (const int x : {306, 312, 318})
            drag.pointer(pointerAt(Pointer::Kind::Move, x, 200, Pointer::Button::None, true), dragged);
        const auto shown = drag.values(dragged);
        const std::u16string last = valueNamed(shown, name)->text;
        drag.pointer(pointerAt(Pointer::Kind::Release, 318, 200, Pointer::Button::Left), dragged);
        TestHost scrubbed;
        AllTagsTool scrub;
        fresh(scrubbed, scrub);
        for (const char16_t *v : {u"1", u"3"})
            ASSERT_TRUE(scrub.setValue(name, v, scrubbed, ValueEdit::Sample));
        ASSERT_TRUE(scrub.setValue(name, last, scrubbed, ValueEdit::Commit));
        TestHost typed;
        AllTagsTool type;
        fresh(typed, type);
        ASSERT_TRUE(type.setValue(name, last, typed));
        const std::string want = textOf(*typed.s, typed.targets.front());
        EXPECT_EQ(textOf(*dragged.s, dragged.targets.front()), want);
        EXPECT_EQ(textOf(*scrubbed.s, scrubbed.targets.front()), want);
    }
}

// HY1 (Codex on #339): without video the strip has not read the active
// Line, so a typed value writes nothing.
TEST(HydraStrip, NoVideoNoChange)
{
    TestHost host;
    setUp(host, hydraCase({"{\\blur2}Hello"}, false));
    host.noVideo = true;
    AllTagsTool tags;
    tags.setToggled(PasteInsert << 20);
    tags.selected(host);
    tags.reset(host);
    const std::size_t steps = host.s->historySize();
    EXPECT_FALSE(tags.setValue("value0", u"5", host));
    EXPECT_EQ(host.s->historySize(), steps);
    EXPECT_EQ(textOf(*host.s, host.targets.front()), "{\\blur2}Hello");
}

// HY1 (Codex on #339): the outlines are the Lines a change edits: not the
// picked ones the Grid hides; the active Line's draft where the video shows it.
TEST(HydraStrip, TheOutlinesFollowWhatIsEditedAndShown)
{
    TestHost host;
    setUp(host, hydraCase({"{\\pos(400,200)}One", "{\\pos(900,500)}Two", "{\\pos(1400,800)}Three"}, true));
    const auto all = ids(*host.s);
    host.hidden.insert(all[1]);
    AllTagsTool tags;
    tags.setToggled(PasteAdd << 20);
    tags.selected(host);
    tags.reset(host);
    const Overlay o = tags.overlay(host);
    ASSERT_EQ(o.polygons.size(), 2u);
    ASSERT_EQ(o.texts.size(), 2u);
    EXPECT_EQ(o.texts[1].text, u"2");
    EXPECT_NEAR((o.polygons[1].points[0].x + o.polygons[1].points[1].x) / 2, 1400.f / 3, 1.f);
    // The active Line's draft moves it.
    ASSERT_TRUE(host.s->editDraftText(all[0], u8"{\\pos(600,200)}One"));
    const Overlay moved = tags.overlay(host);
    ASSERT_EQ(moved.polygons.size(), 2u);
    EXPECT_NEAR((moved.polygons[0].points[0].x + moved.polygons[0].points[1].x) / 2, 600.f / 3, 1.f);
}

// HY1 (Codex on #339): outside the active Line's time (the warning, no
// pointer events) the strip's values write nothing either.
TEST(HydraStrip, ABlockedLineTakesNoValue)
{
    TestHost host;
    setUp(host, hydraCase({"{\\blur2}Hello"}, false));
    AllTagsTool tags;
    tags.setToggled(PasteAdd << 20);
    tags.selected(host);
    tags.reset(host);
    ASSERT_TRUE(tags.setValue("by0", u"1", host));
    ASSERT_TRUE(tags.lastChange(host));
    host.time = 5000; // the Line ends at 2.0 s
    const std::size_t steps = host.s->historySize();
    EXPECT_FALSE(tags.setValue("by0", u"2", host, ValueEdit::Sample));
    EXPECT_FALSE(tags.setValue("by0", u"2", host));
    EXPECT_FALSE(tags.editLastChange("by0", u"4", host));
    EXPECT_EQ(host.s->historySize(), steps);
    EXPECT_EQ(textOf(*host.s, host.targets.front()), "{\\blur3}Hello");
}

// HY1 (Codex on #339): during a drag the outline follows the staged
// preview; Esc puts it back.
TEST(HydraStrip, ADragsOutlineFollowsItsPreview)
{
    TestHost host;
    Case c = hydraCase({"{\\pos(900,500)}Hello"}, false);
    setUp(host, c);
    AllTagsTool tags;
    tags.setToggled((PasteAdd << 20) + 20); // position
    tags.selected(host);
    tags.reset(host);
    ASSERT_EQ(tags.actualTag().tag, u"pos");
    ASSERT_TRUE(tags.markValue("by0", host));
    const auto centre = [&] {
        const Overlay o = tags.overlay(host);
        EXPECT_FALSE(o.polygons.empty());
        return o.polygons.empty() ? 0.f : (o.polygons[0].points[0].x + o.polygons[0].points[1].x) / 2;
    };
    EXPECT_NEAR(centre(), 900.f / 3, 1.f);
    ASSERT_TRUE(tags.setValue("by0", u"300", host, ValueEdit::Sample));
    ASSERT_TRUE(host.g);
    EXPECT_NEAR(centre(), 1200.f / 3, 1.f);
    host.cancelGesture();
    tags.reset(host);
    EXPECT_NEAR(centre(), 900.f / 3, 1.f);
}

// HY1-time-keys-set: the Line editor's "Insert difference from the start"
// key puts the video's time from the Line's start into \fad's first value,
// as Set does, whatever the switch shows; one step.
TEST(HydraStrip, TheTimeKeySetsTheValue)
{
    TestHost host;
    setUp(host, hydraCase({"{\\fad(100,200)}Hello"}, false)); // 1.0 s to 2.0 s, the video at 1.5 s
    host.caret = {3, 3};
    AllTagsTool tags;
    tags.setToggled((PasteAdd << 20) + 10); // fading, under Add
    tags.selected(host);
    tags.reset(host);
    ASSERT_EQ(tags.actualTag().tag, u"fad");
    const std::size_t steps = host.s->historySize();
    Key key;
    key.key = ',';
    key.control = true;
    EXPECT_TRUE(tags.key(key, host));
    EXPECT_EQ(host.bells, 0);
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(textOf(*host.s, host.targets.front()), "{\\fad(500,200)}Hello");
}

namespace {

// Approved departures (docs/qt/compatibility-decisions.md): the states where
// the rewrite expects other Lines than the capture, which stays legacy's
// record (each changed value is checked to differ from it).
struct Departure {
    std::string caseName;
    std::size_t step = 0;
    // A Line's text (field 0) or translation (field 1) instead of legacy's.
    std::vector<std::tuple<std::size_t, int, std::u8string>> lines;
    std::optional<std::vector<std::string>> sent; // the commits instead of legacy's
    bool editor = false;  // the one-Line editor's text is the first Line's
    bool handles = false; // the shifter's state follows the moved handles: not compared
};

const Departure *departureFor(const std::string &caseName, std::size_t step)
{
    static const std::vector<Departure> list{
        // T6-drag-offset: the handle keeps the pointer's exact offset, so a
        // drag of a handle at a fractional place moves the Lines by the
        // pointer's whole pixels (legacy: the fraction dropped at the first
        // move).
        {"shift-clips", 1, {}, std::nullopt, false, true},
        {"shift-clips",
         2,
         {{0, 0, u8"{\\clip(2,m 206 218 l 406 218 b 456 268 456 368 406 418 s 306 468 256 468 206 418 c)}Scaled"},
          {1, 0, u8"{\\clip(m 13 19 l 23 19 23 29){\\iclip(4,12,33,50)}Two clips"},
          {2, 0, u8"{\\clip(3,9,1923,1089)\\clip(m 0 0 l 5 5)}Rect then vector"}},
         std::nullopt,
         false,
         true},
        {"shift-drawing",
         4,
         {{1, 0, u8"{\\an5\\pos(900,500)\\fscx200\\org(800,400)\\frz30\\p2}m -15 -96 b 85 -96 185 4 185 104 s 85 204 -15 104 -15 4 c{\\p0}"}},
         std::nullopt,
         false,
         true},
        {"shift-drawing",
         5,
         {{1, 0, u8"{\\an5\\pos(900,500)\\fscx200\\org(800,400)\\frz30\\p2}m -15 -96 b 85 -96 185 4 185 104 s 85 204 -15 104 -15 4 c{\\p0}"}}},
        {"shift-drawing",
         6,
         {{1, 0, u8"{\\an5\\pos(900,500)\\fscx200\\org(800,400)\\frz30\\p2}m -15 -96 b 85 -96 185 4 185 104 s 85 204 -15 104 -15 4 c{\\p0}"}}},
        {"shift-nothing", 3, {{1, 0, u8"{\\pos(abc,14)\\move(13,14)}Odd"}}, std::nullopt, false, true},
        {"shift-tlmode",
         1,
         {{0, 1, u8"{\\pos(930,530)}Translation"}, {1, 0, u8"{\\pos(60,60)}Only original"}},
         std::nullopt,
         false,
         true},
        {"shift-zoomed", 1, {{0, 0, u8"{\\pos(412,312)\\clip(m 312 312 l 512 312 512 512)\\an7\\p1}m 0 0 l 10 10{\\p0}"}}, std::nullopt, false, true},
        {"shift-zoomed", 2, {{0, 0, u8"{\\pos(412.04,312)\\clip(m 312 312 l 512 312 512 512)\\an7\\p1}m 0 0 l 10 10{\\p0}"}}, std::nullopt, false, true},
        {"shift-zoomed", 3, {{0, 0, u8"{\\pos(412.04,312)\\clip(m 312 312 l 512 312 512 512)\\an7\\p1}m 0 0 l 10 10{\\p0}"}}, std::nullopt, false, true},
        // T6-gradient-hard-space: the \h after the first block stays whole
        // and five tags take the five characters (legacy:
        // "{\blur12.1}\{\blur9.1}h{\blur6}A…{\blur-4.1}e\h").
        {"tags-gradient-text", 2, {{0, 0, u8"{\\blur12.1}\\hA{\\blur9.6}b {\\blur6.9}c{\\i1\\blur4.4}d\\N{\\blur1.9}e\\h"}}, std::nullopt, true},
        // T6-wheel-one-step: the colour's three values in one step (legacy: three).
        {"tags-wheel-colour", 0, {}, std::vector<std::string>{"47"}},
    };
    for (const auto &d : list)
        if (d.caseName == caseName && d.step == step)
            return &d;
    return nullptr;
}

} // namespace

TEST(VisualCapture, ReplaysTheLegacyT6Probe)
{
    const auto observations = readObservations();
    const auto cases = readCases();
    ASSERT_EQ(cases.size(), observations.size());
    int states = 0, drawings = 0;
    for (const Case &c : cases) {
        SCOPED_TRACE(c.name);
        ASSERT_TRUE(observations.contains(c.name));
        const auto &steps = observations.at(c.name);
        TestHost host;
        setUp(host, c);
        if (!c.lines.empty()) {
            // The view as legacy's renderer had it.
            ASSERT_EQ(host.v.videoRect(), c.rect);
            ASSERT_EQ(host.v.zoomMove(), c.zoomMove);
            ASSERT_EQ(host.v.zoomScale(), c.zoomScale);
        }
        std::unique_ptr<VisualTool> tool;
        auto shifter = [&] { return dynamic_cast<PositionShifterTool *>(tool.get()); };
        auto allTags = [&] { return dynamic_cast<AllTagsTool *>(tool.get()); };
        host.edited = [&] {
            if (!tool->keepsStateAfterCommit())
                tool->reset(host);
        };
        std::map<std::string, std::u16string> files;
        std::string config = "/probe";
        std::optional<Overlay> drawn;
        int active = c.active;
        std::pair<long, long> caret{c.caretFrom, c.caretTo};
        bool explicitSelection = !c.select.empty();
        std::size_t step = 0;
        const auto all = ids(*host.s);
        for (const std::string &op : c.ops) {
            std::istringstream in(op);
            std::string kind;
            in >> kind;
            SCOPED_TRACE(op);
            if (kind == "config") {
                in >> config;
            } else if (kind == "file") {
                const auto space = op.find(' ');
                const auto bar = op.find('|');
                std::string content = op.substr(bar + 1);
                for (std::size_t at; (at = content.find("\\n")) != std::string::npos;)
                    content.replace(at, 2, "\n");
                files[op.substr(space + 1, bar - space - 1)] = core::toUtf16(std::u8string(content.begin(), content.end()));
            } else if (kind == "loadtags") {
                // VideoToolbar::GetTagsSettings: LoadSettings on the file
                // (wxConvAuto drops a BOM), the defaults written over an old one.
                const std::string path = config + "/Config/AllTagsSettings.txt";
                std::u16string text = files.contains(path) ? files[path] : std::u16string();
                if (text.starts_with(u"﻿"))
                    text.erase(0, 1);
                bool writeDefaults = false;
                host.tags = parseAllTags(text, &writeDefaults);
                if (writeDefaults)
                    files[path] = u"﻿" + std::u16string(defaultAllTagsText());
            } else if (kind == "tagset") {
                std::size_t index;
                std::string field, value;
                in >> index >> field;
                std::getline(in, value);
                value.erase(0, value.find_first_not_of(' '));
                AllTagsSetting &t = host.tags[index];
                const double d = std::strtod(value.c_str(), nullptr);
                if (field == "name")
                    t.name = core::toUtf16(std::u8string(value.begin(), value.end()));
                else if (field == "value")
                    t.values[0] = static_cast<float>(d);
                else if (field == "step")
                    t.step = static_cast<float>(d);
                else if (field == "count")
                    t.numOfValues = static_cast<unsigned char>(d);
            } else if (kind == "savetags") {
                files[config + "/Config/AllTagsSettings.txt"] = u"﻿" + writeAllTags(host.tags);
            } else if (kind == "tool") {
                std::string name;
                int bits;
                in >> name >> bits;
                if (name == "moveall") {
                    auto t = std::make_unique<PositionShifterTool>();
                    t->setToggled(bits);
                    tool = std::move(t);
                } else {
                    auto t = std::make_unique<AllTagsTool>();
                    t->setLegacySliders(true); // HY1: the probe drives legacy's sliders
                    t->setToggled(bits);
                    tool = std::move(t);
                }
                tool->selected(host);
                tool->reset(host);
            } else if (kind == "option") {
                int bits;
                in >> bits;
                if (shifter())
                    shifter()->setToggled(bits, &host);
                else
                    allTags()->setToggled(bits, &host);
            } else if (kind == "active") {
                in >> active;
                const core::LineId id = all[static_cast<std::size_t>(active)];
                host.s->setSelection({id, {id}, id, std::nullopt});
                if (!explicitSelection)
                    host.targets = {id};
            } else if (kind == "caret") {
                in >> caret.first >> caret.second;
            } else if (kind == "time") {
                in >> host.time;
            } else if (kind == "state-video") {
                std::string s;
                in >> s;
                host.noVideo = s == "none";
            } else if (kind == "setvisual") {
                // RendererVideo::SetVisual after the editor took the Line again.
                host.caret = caret;
                tool->reset(host);
            } else if (kind == "press" || kind == "move" || kind == "release" || kind == "wheel" || kind == "leave") {
                tool->pointer(pointerOf(kind, in), host);
            } else if (kind == "dclick") {
                // Qt's press before the double click (legacy's LeftDClick
                // stood for it); the double click itself does nothing.
                Pointer p = pointerOf("press", in);
                p.button = Pointer::Button::Left;
                p.leftDown = true;
                tool->pointer(p, host);
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
                drawn = tool->overlay(host);
            } else if (kind == "state") {
                ASSERT_LT(step, steps.size());
                const QJsonObject &o = steps[step++];
                ++states;
                SCOPED_TRACE("step " + std::to_string(step - 1));
                const Departure *departure = departureFor(c.name, step - 1);
                // Every Line as legacy's grid held it, or as a departure
                // expects it.
                const QJsonArray lines = o[QStringLiteral("lines")].toArray();
                ASSERT_EQ(static_cast<std::size_t>(lines.size()), all.size());
                for (std::size_t i = 0; i < all.size(); ++i) {
                    const QJsonArray l = lines[static_cast<qsizetype>(i)].toArray();
                    std::u8string expected[2] = {u8(l[0]), u8(l[1])};
                    if (departure)
                        for (const auto &[index, field, value] : departure->lines)
                            if (index == i) {
                                EXPECT_NE(value, expected[field]) << "the old evidence, line " << i;
                                expected[field] = value;
                            }
                    EXPECT_EQ(lineOf(*host.s, all[i])->text, expected[0]) << "line " << i;
                    EXPECT_EQ(lineOf(*host.s, all[i])->translation, expected[1]) << "line " << i;
                }
                // The commits (SetModified, Send) that changed a Line, and
                // the log.
                EXPECT_EQ(host.history, strings(o[QStringLiteral("history")], true));
                if (departure && departure->sent) {
                    EXPECT_NE(*departure->sent, strings(o[QStringLiteral("sent")], true)) << "the old evidence";
                    EXPECT_EQ(host.sent, *departure->sent);
                } else {
                    EXPECT_EQ(host.sent, strings(o[QStringLiteral("sent")], true));
                }
                std::vector<std::string> logged;
                for (const auto &l : host.logged)
                    logged.push_back(QString::fromStdU16String(l).toStdString());
                EXPECT_EQ(logged, strings(o[QStringLiteral("log")]));
                // The definitions (legacy loads them when the tool or a
                // "loadtags" first wants them) and their file.
                if (!o[QStringLiteral("tags")].toArray().isEmpty())
                    expectTags(host.tags, o[QStringLiteral("tags")].toArray());
                for (const QJsonValue &file : o[QStringLiteral("files")].toArray()) {
                    const QJsonArray pf = file.toArray();
                    const std::string path = pf[0].toString().toStdString();
                    ASSERT_TRUE(files.contains(path)) << path;
                    EXPECT_EQ(files[path], u16(pf[1])) << path;
                }
                if (!tool)
                    continue;
                if (auto *m = shifter(); m && !(departure && departure->handles)) {
                    const QJsonArray elems = o[QStringLiteral("elems")].toArray();
                    ASSERT_EQ(m->elements().size(), static_cast<std::size_t>(elems.size()));
                    for (std::size_t i = 0; i < m->elements().size(); ++i) {
                        SCOPED_TRACE("element " + std::to_string(i));
                        const QJsonObject e = elems[static_cast<qsizetype>(i)].toObject();
                        const auto &mine = m->elements()[i];
                        EXPECT_EQ(mine.type, e[QStringLiteral("type")].toInt());
                        EXPECT_EQ(mine.elem, pair(e[QStringLiteral("elem")]));
                        ASSERT_EQ(mine.points.has_value(), e.contains(QStringLiteral("points")));
                        if (!mine.points)
                            continue;
                        const QJsonArray pts = e[QStringLiteral("points")].toArray();
                        ASSERT_EQ(mine.points->size(), static_cast<std::size_t>(pts.size()));
                        for (std::size_t p = 0; p < mine.points->size(); ++p) {
                            const QJsonArray pt = pts[static_cast<qsizetype>(p)].toArray();
                            EXPECT_EQ((*mine.points)[p].x, f(pt[0])) << "point " << p;
                            EXPECT_EQ((*mine.points)[p].y, f(pt[1])) << "point " << p;
                            EXPECT_EQ((*mine.points)[p].type, u16(pt[2])) << "point " << p;
                            EXPECT_EQ((*mine.points)[p].start, pt[3].toBool()) << "point " << p;
                        }
                    }
                    EXPECT_EQ(m->numElem(), o[QStringLiteral("numElem")].toInt());
                    EXPECT_EQ(m->selectedTags(), o[QStringLiteral("selectedTags")].toInt());
                    EXPECT_EQ(m->from(), pair(o[QStringLiteral("from")]));
                    EXPECT_EQ(m->beforeMove(), pair(o[QStringLiteral("beforeMove")]));
                    EXPECT_EQ(m->drawingPos(), pair(o[QStringLiteral("drawingPos")]));
                    EXPECT_EQ(m->drawingOriginalPos(), pair(o[QStringLiteral("drawingOriginalPos")]));
                    EXPECT_EQ(m->drawingScale(), pair(o[QStringLiteral("drawingScale")]));
                    EXPECT_EQ(m->scale(), pair(o[QStringLiteral("scale")]));
                    EXPECT_EQ(m->vectorClipScale(), f(o[QStringLiteral("vectorClipScale")]));
                    // Legacy never initialised moveValues (Visuals.cpp:28-41):
                    // the values past the count read are whatever the memory
                    // held, so only those read and the times are compared.
                    const QJsonArray mv = o[QStringLiteral("moveValues")].toArray();
                    for (int i = 0; i < 7; ++i)
                        if (i >= 4 || i < mv[6].toInt())
                            EXPECT_EQ(m->moveValues()[i], mv[i].toDouble()) << "moveValues " << i;
                } else if (auto *a = allTags()) {
                    const bool several = !(host.targets.size() == 1 && host.targets.front() == all[static_cast<std::size_t>(active)]);
                    const QJsonArray caretJson = o[QStringLiteral("caret")].toArray();
                    const std::pair<long, long> legacyCaret{caretJson[0].toInteger(), caretJson[1].toInteger()};
                    if (!several) {
                        // The editor's text: the gesture's staged text while
                        // one runs, else the Line's.
                        const core::LineRecord *line = lineOf(*host.s, all[static_cast<std::size_t>(active)]);
                        const bool tl = c.tl && !line->translation.empty();
                        std::u8string editor = tl ? line->translation : line->text;
                        if (host.g)
                            if (const auto staged = host.g->staged(all[static_cast<std::size_t>(active)], tl))
                                editor = *staged;
                        if (departure && departure->editor) {
                            EXPECT_NE(editor, u8(o[QStringLiteral("editor")])) << "the old evidence";
                            EXPECT_EQ(editor, std::get<2>(departure->lines.front()));
                        } else {
                            EXPECT_EQ(editor, u8(o[QStringLiteral("editor")]));
                        }
                        EXPECT_EQ(a->editorCaret(), legacyCaret) << "the tool's caret";
                    }
                    expectTags(std::vector<AllTagsSetting>{a->actualTag()}, o[QStringLiteral("actualTag")].toArray());
                    EXPECT_EQ(a->currentTag(), o[QStringLiteral("currentTag")].toInt());
                    EXPECT_EQ(a->mode(), o[QStringLiteral("mode")].toInt());
                    EXPECT_EQ(a->tagKind(), o[QStringLiteral("tagMode")].toInt());
                    EXPECT_EQ(a->multiplyCounter(), f(o[QStringLiteral("multiplyCounter")]));
                    EXPECT_EQ(a->sliderPositionY(), o[QStringLiteral("sliderPositionY")].toInt());
                    EXPECT_EQ(a->sliderPositionDiff(), o[QStringLiteral("sliderPositionDiff")].toInt());
                    EXPECT_EQ(a->rightHolding(), o[QStringLiteral("rholding")].toBool());
                    EXPECT_EQ(a->selectedTag(), u16(o[QStringLiteral("selectedTag")]));
                    EXPECT_EQ(QString::fromStdString(a->floatFormat()), o[QStringLiteral("floatFormat")].toString());
                    EXPECT_EQ(a->replaceTagsInCursorPosition(), o[QStringLiteral("inCursor")].toBool());
                    const QJsonArray sliders = o[QStringLiteral("sliders")].toArray();
                    for (int i = 0; i < 4; ++i) {
                        SCOPED_TRACE("slider " + std::to_string(i));
                        const QJsonObject s = sliders[i].toObject();
                        const auto &mine = a->sliders()[static_cast<std::size_t>(i)];
                        EXPECT_EQ(mine.thumbValue, f(s[QStringLiteral("thumb")]));
                        EXPECT_EQ(mine.firstThumbValue, f(s[QStringLiteral("first")]));
                        EXPECT_EQ(mine.lastThumbValue, f(s[QStringLiteral("last")]));
                        const QJsonArray r = s[QStringLiteral("rect")].toArray();
                        EXPECT_EQ(mine.left, f(r[0]));
                        EXPECT_EQ(mine.top, f(r[1]));
                        EXPECT_EQ(mine.right, f(r[2]));
                        EXPECT_EQ(mine.bottom, f(r[3]));
                        EXPECT_EQ((PointF{mine.x, mine.y}), pair(s[QStringLiteral("at")]));
                        EXPECT_EQ(mine.thumbState, s[QStringLiteral("thumbState")].toInt());
                        EXPECT_EQ(mine.onThumb, s[QStringLiteral("onThumb")].toBool());
                        EXPECT_EQ(mine.onSlider, s[QStringLiteral("onSlider")].toBool());
                        EXPECT_EQ(mine.holding, s[QStringLiteral("holding")].toBool());
                    }
                    // The toolbar's list follows Shift and the wheel
                    // (AllTagsItem::SetItemToggled).
                    const QJsonArray toggled = o[QStringLiteral("itemToggled")].toArray();
                    if (!toggled.isEmpty()) {
                        const int last = toggled.last().toInt();
                        const int count = static_cast<int>(host.tags.size());
                        int selection = (last << 12) >> 12;
                        selection = selection < 0 ? count - 1 : selection >= count ? 0 : selection;
                        EXPECT_EQ(a->toggled() & 0xFFFFF, selection);
                    }
                }
                if (drawn) {
                    ++drawings;
                    SCOPED_TRACE("the drawing");
                    expectDrawing(*drawn, o[QStringLiteral("drawn")].toArray(), host);
                    drawn.reset();
                } else {
                    EXPECT_TRUE(o[QStringLiteral("drawn")].toArray().isEmpty());
                }
            }
        }
        EXPECT_EQ(step, steps.size());
    }
    EXPECT_GT(states, 140);
    EXPECT_GT(drawings, 30);
}

// HY2 (#272, folded into PU5): every option Edit tags shows behind
// Advanced (range, step, decimal places, count, change option, placing) and
// its default values survive the definitions file both ways, in legacy's
// layout, beside the definitions it did not touch.
TEST(AllTagsEdition, EveryAdvancedOptionRoundTripsThroughTheFile)
{
    using Number = AllTagsEdition::Number;
    AllTagsEdition edition(defaultAllTags(), 0);
    edition.addTag();
    ASSERT_EQ(edition.setName(u"glow edge"), AllTagsEdition::Check::Ok);
    ASSERT_EQ(edition.setTag(u"\\be"), AllTagsEdition::Check::Ok);
    ASSERT_EQ(edition.setNumber(Number::Min, u"-20,5"), AllTagsEdition::Check::Ok);
    ASSERT_EQ(edition.setNumber(Number::Max, u"300"), AllTagsEdition::Check::Ok);
    ASSERT_EQ(edition.setNumber(Number::Step, u"0.25"), AllTagsEdition::Check::Ok);
    ASSERT_EQ(edition.setNumber(Number::Digits, u"3"), AllTagsEdition::Check::Ok);
    edition.setCount(4);
    for (const auto [field, text] : {std::pair{Number::Value0, u"1.5"}, std::pair{Number::Value1, u"-2"},
                                     std::pair{Number::Value2, u"3.75"}, std::pair{Number::Value3, u"4"}})
        ASSERT_EQ(edition.setNumber(field, text), AllTagsEdition::Check::Ok);
    edition.setChangeOption(PasteGradientLineIncrease);
    edition.setPlacing(2);
    const AllTagsSetting &edited = edition.current();
    EXPECT_EQ(edited.rangeMin, -20.5f);
    EXPECT_EQ(edited.rangeMax, 300.f);
    EXPECT_EQ(edited.step, 0.25f);
    EXPECT_EQ(edited.digitsAfterDot, 3);
    EXPECT_EQ(edited.numOfValues, 4);
    EXPECT_EQ(edited.values, (std::array<float, 4>{1.5f, -2.f, 3.75f, 4.f}));
    EXPECT_EQ(edited.tagMode, PasteGradientLineIncrease);
    EXPECT_EQ(edited.mode, 2);

    const std::u16string text = writeAllTags(edition.usable());
    EXPECT_TRUE(text.starts_with(u"HYDRA2.0\nTag: blur, blur, "));
    EXPECT_TRUE(text.ends_with(u"Tag: glow edge, be, -20.500000, 300.000000, 1.500000, 0.250000, 3, 2, 6, "
                               u"-2.000000, 3.750000, 4.000000\n"))
        << std::string(text.begin(), text.end());
    bool write = true;
    EXPECT_EQ(parseAllTags(text, &write), edition.usable());
    EXPECT_FALSE(write);
    // And back: the defaults, edited nowhere, are written as they were read.
    EXPECT_EQ(writeAllTags(parseAllTags(writeAllTags(defaultAllTags()))), writeAllTags(defaultAllTags()));
}
