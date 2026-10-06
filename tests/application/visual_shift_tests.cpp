// T6: the Position shifter and the all-tags tool. The tools replay the
// legacy probe's captures (tools/legacy-capture/visual_t6_capture.cpp over
// inputs/visual-t6-cases.txt; tests/fixtures/legacy-observations/
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

TEST(AllTagsEdition, AddDeleteSaveAndRestoreFollowLegacy)
{
    AllTagsEdition e(defaultAllTags(), 40);
    EXPECT_EQ(e.selection(), 0); // out of range: the first
    EXPECT_EQ(e.name, u"blur");
    EXPECT_EQ(e.step.text(), u"0.5");
    EXPECT_EQ(e.digitsAfterDot.text(), u"1");
    EXPECT_FALSE(e.valueEnabled(1));
    EXPECT_FALSE(e.modified());
    // OnAddTag.
    ASSERT_TRUE(e.addTag());
    EXPECT_EQ(e.addTag()->text, u"Enter a name for the new tag.");
    e.newTagName = u"BLUR";
    ASSERT_TRUE(e.addTag());
    EXPECT_EQ(e.addTag()->text, u"New tag already exists, enter another name.");
    e.newTagName = u"wide";
    EXPECT_FALSE(e.addTag());
    EXPECT_EQ(e.selection(), 22);
    EXPECT_EQ(e.list().back(), u"wide");
    EXPECT_EQ(e.tag, u"wide");
    EXPECT_EQ(e.maxValue.text(), u"100");
    // Edits and the list's question, which names the tag.
    e.tag = u"fscx";
    e.maxValue.setText(u"400");
    e.step.setText(u"2.5");
    e.additionalValues = 1;
    e.values[1].setText(u"7");
    EXPECT_TRUE(e.modified());
    EXPECT_EQ(e.saveChangesQuestion().text, u"Save changes to tag \"wide\"?");
    EXPECT_FALSE(e.save());
    EXPECT_FALSE(e.modified());
    EXPECT_EQ(e.tags()[22].tag, u"fscx");
    EXPECT_EQ(e.tags()[22].numOfValues, 2);
    EXPECT_EQ(e.tags()[22].values[1], 7.f);
    EXPECT_EQ(e.list()[22], u"wide"); // the name was not changed
    // Save's checks, in legacy's order.
    e.tag.clear();
    EXPECT_EQ(e.save()->text, u"Field \"Tag\" cannot be empty.");
    e.tag = u"fscx";
    e.name.clear();
    e.maxValue.setText(u"-20");
    EXPECT_EQ(e.save()->text, u"Field \"Maximum value\" must contain a value\ngreater than field \"Minimum value\".");
    EXPECT_EQ(e.name, u"fscx"); // an empty name became the tag first
    e.maxValue.setText(u"10");
    e.step.setText(u"0");
    EXPECT_EQ(e.save()->text, u"Field \"Step\" must contain a value greater than zero.");
    e.step.setText(u"6");
    EXPECT_EQ(e.save()->text, u"Field \"Step\" contains a number too large for the current min-max range.");
    e.step.setText(u"5");
    EXPECT_FALSE(e.save());
    // A number field falls back to the last valid text typed.
    e.minValue.setText(u"abc");
    EXPECT_EQ(e.minValue.getDouble(), 0.0);
    e.minValue.setText(u"-20000"); // read, then held to the range
    EXPECT_EQ(e.minValue.getDouble(), -10000.0);
    e.minValue.setText(u"1,5");
    EXPECT_EQ(e.minValue.getDouble(), 1.5);
    // OnRemoveTag: the last goes, the one before it is selected.
    EXPECT_FALSE(e.removeTag());
    EXPECT_EQ(e.selection(), 21);
    EXPECT_EQ(e.name, u"tanimation");
    // Restore default: the defaults, the selection kept by its name.
    e.select(3);
    e.restoreDefaults();
    EXPECT_EQ(e.tags(), defaultAllTags());
    EXPECT_EQ(e.selection(), 3);
    EXPECT_EQ(AllTagsEdition::restoreQuestion().text, u"Are you sure you want to reset to default?");
    // One definition left cannot go.
    AllTagsEdition one({AllTagsSetting::named(u"x")}, 0);
    EXPECT_EQ(one.removeTag()->text, u"Cannot remove all tags from list");
}

// T6-dialog-list-stale (compatibility-decisions.md): a save puts a rename
// into the dialog's list. Legacy's list kept the old name (only Add, Delete
// and Restore default changed it).
TEST(AllTagsEdition, ARenameByApplyReachesTheList)
{
    AllTagsEdition e(defaultAllTags(), 0);
    e.name = u"soft edges";
    EXPECT_FALSE(e.save());
    EXPECT_EQ(e.tags()[0].name, u"soft edges");
    EXPECT_EQ(e.list()[0], u"soft edges");
    EXPECT_EQ(e.list().size(), 22u);
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
