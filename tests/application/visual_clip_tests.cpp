// T4: the rectangle and vector clips and the vector point editor T5 shares.
// The clips replay the legacy probe's captures (tools/legacy-capture/
// clip_capture.cpp over inputs/clip-cases.txt; tests/fixtures/
// legacy-observations/local-t4-clip-20261005), which ran the legacy
// VisualClipRect.cpp and VisualClips.cpp unchanged: every committed text,
// the editor's text during a gesture, the corners, points, selection, mode,
// written body, mask, clip scale and coefficients must be legacy's, float
// for float, but where the rewrite's rules differ (listed in kDepartures).

#include "hikari/application/visual_clip.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_vector.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstring>
#include <fstream>
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

const core::LineRecord *line(const EditSession &s, core::LineId id)
{
    for (const auto *l : s.document().lines())
        if (l->id == id)
            return l;
    return nullptr;
}

std::u16string u(std::u8string_view s)
{
    return core::toUtf16(s);
}

// A VisualHost over a VideoView and an EditSession (as T1's tests).
class TestHost : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    BatchPicker picker;
    int changes = 0;
    int bells = 0;
    std::vector<std::u16string> notices;
    VisualTool *tool = nullptr; // reset after a cancel, as the controller does

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
    void cancelGesture() override
    {
        g.reset();
        if (tool)
            tool->reset(*this);
    }
    std::pair<int, int> measureLabel(std::u16string_view text) const override
    {
        return {7 * static_cast<int>(text.size()), 14};
    }
    void toolChanged() override { ++changes; }
    void bell() override { ++bells; }
    void notice(std::u16string_view text) override { notices.emplace_back(text); }
};

// --- The legacy probe's cases ------------------------------------------------

struct Case {
    std::string name;
    int scriptW = 0, scriptH = 0;
    int clientW = 0, clientH = 0, panel = 0;
    int frameW = 0, frameH = 0;
    std::optional<std::tuple<float, int, int>> zoomAt;
    IntRect video;
    std::optional<std::pair<PointF, PointF>> zoom;
    std::vector<std::pair<std::string, std::optional<std::string>>> lines; // text, translation
    std::vector<int> select;
    bool rect = true;
    int mode = 1;
    std::vector<std::string> ops;
};

std::string rest(std::istringstream &in)
{
    std::string s;
    std::getline(in, s);
    if (!s.empty() && s[0] == ' ')
        s.erase(0, 1);
    return s;
}

std::vector<Case> readCases()
{
    std::ifstream in(HIKARI_CLIP_CASES);
    std::vector<Case> out;
    Case c;
    bool toolSeen = false;
    std::string text;
    while (std::getline(in, text)) {
        std::istringstream ls(text);
        std::string key;
        if (!(ls >> key) || key[0] == '#')
            continue;
        if (key == "case") {
            c = Case{};
            toolSeen = false;
            ls >> c.name;
        } else if (key == "script") {
            ls >> c.scriptW >> c.scriptH;
        } else if (key == "client") {
            ls >> c.clientW >> c.clientH >> c.panel;
        } else if (key == "frame") {
            ls >> c.frameW >> c.frameH;
        } else if (key == "zoomat") {
            float pct;
            int x, y;
            ls >> pct >> x >> y;
            c.zoomAt = std::tuple(pct, x, y);
        } else if (key == "video") {
            ls >> c.video.left >> c.video.top >> c.video.right >> c.video.bottom;
        } else if (key == "zoom") {
            PointF move, scale;
            ls >> move.x >> move.y >> scale.x >> scale.y;
            c.zoom = std::pair(move, scale);
        } else if (key == "line") {
            c.lines.emplace_back(rest(ls), std::nullopt);
        } else if (key == "tl") {
            c.lines.back().second = rest(ls);
        } else if (key == "select") {
            int i;
            while (ls >> i)
                c.select.push_back(i);
        } else if (key == "tool") {
            std::string t;
            ls >> t;
            c.rect = t == "rect";
            toolSeen = true;
        } else if (key == "mode" && !toolSeen) {
            ls >> c.mode;
        } else if (key == "end") {
            out.push_back(c);
        } else if (key != "caret") {
            c.ops.push_back(text);
        }
    }
    return out;
}

std::vector<QJsonObject> readObservations()
{
    QFile f(QStringLiteral(HIKARI_CLIP_OBSERVATIONS));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    std::vector<QJsonObject> out;
    for (const QByteArray &l : f.readAll().split('\n'))
        if (!l.trimmed().isEmpty())
            out.push_back(QJsonDocument::fromJson(l).object());
    return out;
}

float fl(const QJsonValue &v)
{
    return static_cast<float>(v.toDouble());
}

std::string str(const QJsonValue &v)
{
    return v.toString().toStdString();
}

// Where the rewrite's rules give another result than the capture, by case
// and dump: what differs and why. Each is checked for its own outcome below.
enum class Departure {
    // Approved T4-clip-read-start: legacy's ClipRect read the clip from the
    // Line editor's caret (FindTag mode 0); the tools read from the start.
    ReadFromStart,
    // Approved T4-zero-rect-preview: a rectangle without width or height
    // left its preview in legacy's editor, unsent; the rewrite drops it.
    ZeroRectangle,
    // Approved T4-wheel-invert-slot: legacy inverted the clip on every reset
    // with the mode on the Invert clip button, recursing until it crashed.
    WheelInvertSlot,
};
const std::map<std::pair<std::string, int>, Departure> kDepartures = {
    {{"rect-second-block-caret", 0}, Departure::ReadFromStart},
    {{"rect-zero-width", 0}, Departure::ZeroRectangle},
    {{"vector-wheel-to-invert", 1}, Departure::WheelInvertSlot},
};

const char *historyName(const std::string &legacy)
{
    // SubsFile.cpp:228-238.
    return legacy == "VISUAL_RECT_CLIP" ? "Visual rectangular clipping tool" : "Visual vector clipping tool";
}

struct Replay {
    const Case &c;
    TestHost host;
    RectangleClipTool rect;
    VectorClipTool vector;
    std::vector<core::LineId> lineIds;
    std::size_t historyStart = 0;
    int pendingMode = 1;

    VisualTool &tool() { return c.rect ? static_cast<VisualTool &>(rect) : vector; }

    explicit Replay(const Case &cs) : c(cs)
    {
        std::string script = "[Script Info]\nScriptType: v4.00+\nPlayResX: " + std::to_string(c.scriptW) +
                             "\nPlayResY: " + std::to_string(c.scriptH) + "\n";
        bool tl = false;
        for (const auto &l : c.lines)
            tl |= l.second.has_value();
        if (tl)
            script += "TLMode: Yes\n";
        script += "\n[Events]\n";
        for (const auto &l : c.lines)
            script += "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,," + l.first + "\n";
        core::Document document = load(script);
        const auto all = [&] {
            std::vector<core::LineId> out;
            for (const auto *l : document.lines())
                out.push_back(l->id);
            return out;
        }();
        for (std::size_t i = 0; i < c.lines.size(); ++i) {
            // The script's text keeps the case's spacing (legacy trims none here).
            document.editLine(all[i], [&](core::LineRecord &r) {
                r.text = std::u8string(c.lines[i].first.begin(), c.lines[i].first.end());
                if (c.lines[i].second)
                    r.translation = std::u8string(c.lines[i].second->begin(), c.lines[i].second->end());
            });
        }
        host.s = std::make_unique<EditSession>(std::move(document));
        lineIds = ids(*host.s);
        historyStart = host.s->historySize();
        std::set<core::LineId> selected;
        std::vector<core::LineId> picked;
        for (const int i : c.select) {
            selected.insert(lineIds[i]);
            picked.push_back(lineIds[i]);
        }
        if (selected.empty())
            selected.insert(lineIds[0]);
        host.s->setSelection({lineIds[0], selected, lineIds[0], std::nullopt});
        if (picked.size() > 1)
            host.picker.pick(picked); // legacy: several Lines selected
        host.v.setClient(c.clientW, c.clientH, c.panel);
        host.v.setScript(c.scriptW, c.scriptH);
        host.v.open({c.frameW, c.frameH, 0, 1});
        if (c.zoomAt)
            host.v.zoomAt(std::get<0>(*c.zoomAt), std::get<1>(*c.zoomAt), std::get<2>(*c.zoomAt));
        host.tool = &tool();
        pendingMode = c.mode;
    }

    void run(const std::string &op, std::istringstream &in)
    {
        std::string flags, f;
        const auto pointer = [&](Pointer::Kind kind, Pointer::Button button) {
            int x, y;
            in >> x >> y;
            while (in >> f)
                flags += f + " ";
            Pointer p;
            p.kind = kind;
            p.x = x;
            p.y = y;
            p.button = button;
            p.leftDown = (kind == Pointer::Kind::Press && button == Pointer::Button::Left) ||
                         flags.find("left") != std::string::npos;
            p.control = flags.find("ctrl") != std::string::npos;
            p.shift = flags.find("shift") != std::string::npos;
            p.alt = flags.find("alt") != std::string::npos;
            tool().pointer(p, host);
        };
        if (op == "setvisual") {
            vector.editor().mode = pendingMode;
            tool().reset(host);
        } else if (op == "mode") {
            in >> pendingMode;
        } else if (op == "down") {
            pointer(Pointer::Kind::Press, Pointer::Button::Left);
        } else if (op == "move") {
            pointer(Pointer::Kind::Move, Pointer::Button::None);
        } else if (op == "up") {
            pointer(Pointer::Kind::Release, Pointer::Button::Left);
        } else if (op == "rdown") {
            pointer(Pointer::Kind::Press, Pointer::Button::Right);
        } else if (op == "rup") {
            pointer(Pointer::Kind::Release, Pointer::Button::Right);
        } else if (op == "mdown") {
            pointer(Pointer::Kind::Press, Pointer::Button::Middle);
        } else if (op == "mup") {
            pointer(Pointer::Kind::Release, Pointer::Button::Middle);
        } else if (op == "wheel") {
            int x, y, steps;
            in >> x >> y >> steps;
            Pointer p;
            p.kind = Pointer::Kind::Wheel;
            p.x = x;
            p.y = y;
            p.wheelSteps = steps;
            tool().pointer(p, host);
        } else if (op == "key") {
            std::string k;
            in >> k;
            while (in >> f)
                flags += f + " ";
            Key key;
            key.key = k == "Delete" ? keys::Delete : k[0];
            key.control = flags.find("ctrl") != std::string::npos;
            key.shift = flags.find("shift") != std::string::npos;
            key.alt = flags.find("alt") != std::string::npos;
            tool().key(key, host);
            key.release = true; // the nudge commits on the key's release
            tool().key(key, host);
        } else if (op == "invert") {
            tool().setOption("invert", 1, host);
        } else if (op == "activate") {
            int i;
            in >> i;
            host.picker.clear();
            host.s->setSelection({lineIds[i], {lineIds[i]}, lineIds[i], std::nullopt});
            tool().reset(host);
        }
    }

    // The active Line's text as legacy's editor showed it: the staged text
    // during a gesture with one target, else the Line's.
    std::u8string editorText() const
    {
        const auto active = *host.s->selection().active;
        const auto *l = line(*host.s, active);
        const bool tl = !l->translation.empty() && host.s->document().scriptInfo(u8"TLMode") == u8"Yes";
        if (host.g && host.g->targets().size() == 1)
            if (const auto staged = host.g->staged(active, tl))
                return *staged;
        return tl ? l->translation : l->text;
    }

    int steps() const
    {
        // The history entries since the case began (each changed a text).
        return static_cast<int>(host.s->historySize() - historyStart);
    }
};

std::u8string u8(const QJsonValue &v)
{
    const QByteArray b = v.toString().toUtf8();
    return std::u8string(b.begin(), b.end());
}

void compareDump(Replay &r, const QJsonObject &o, std::optional<Departure> departure)
{
    const EditSession &s = *r.host.s;
    const bool multi = r.host.g && r.host.g->targets().size() > 1;
    // The committed texts.
    const QJsonArray texts = o[QStringLiteral("texts")].toArray();
    for (int i = 0; i < texts.size(); ++i) {
        const auto *l = line(s, r.lineIds[i]);
        EXPECT_EQ(l->text, u8(texts[i].toArray()[0])) << "line " << i;
        EXPECT_EQ(l->translation, u8(texts[i].toArray()[1])) << "line " << i;
    }
    // Legacy's Line editor during a gesture with one Line (several Lines are
    // only rendered until the release).
    if (!multi && departure != Departure::ZeroRectangle)
        EXPECT_EQ(r.editorText(), u8(o[QStringLiteral("editor")]));
    // One history step per edit that changed a text, named for the tool.
    if (departure != Departure::WheelInvertSlot) {
        EXPECT_EQ(r.steps(), o[QStringLiteral("steps")].toInt());
        const QJsonArray history = o[QStringLiteral("history")].toArray();
        if (r.steps() > 0 && !history.isEmpty())
            EXPECT_EQ(s.history().back().name, historyName(str(history.last())));
    }
    EXPECT_EQ(r.host.bells, o[QStringLiteral("bells")].toInt());
    EXPECT_EQ(static_cast<int>(r.host.notices.size()), o[QStringLiteral("messages")].toArray().size());
    if (r.c.rect) {
        const QJsonArray corners = o[QStringLiteral("corners")].toArray();
        if (departure != Departure::ReadFromStart && departure != Departure::ZeroRectangle) {
            EXPECT_EQ(r.rect.corner(0).x, fl(corners[0]));
            EXPECT_EQ(r.rect.corner(0).y, fl(corners[1]));
            EXPECT_EQ(r.rect.corner(1).x, fl(corners[2]));
            EXPECT_EQ(r.rect.corner(1).y, fl(corners[3]));
            EXPECT_EQ(r.rect.shown(), o[QStringLiteral("show")].toBool());
        }
        EXPECT_EQ(r.rect.inverse(), o[QStringLiteral("inverse")].toBool());
        return;
    }
    const VectorEditor &e = r.vector.editor();
    const QJsonArray points = o[QStringLiteral("points")].toArray();
    ASSERT_EQ(e.points.size(), static_cast<std::size_t>(points.size()));
    for (int i = 0; i < points.size(); ++i) {
        const QJsonArray p = points[i].toArray();
        SCOPED_TRACE("point " + std::to_string(i));
        EXPECT_EQ(e.points[i].x, fl(p[0]));
        EXPECT_EQ(e.points[i].y, fl(p[1]));
        EXPECT_EQ(std::u16string(1, e.points[i].type), p[2].toString().toStdU16String());
        EXPECT_EQ(e.points[i].start, p[3].toInt() == 1);
        EXPECT_EQ(e.points[i].selected, p[4].toInt() == 1);
    }
    EXPECT_EQ(r.vector.body(), o[QStringLiteral("visual")].toString().toStdU16String());
    EXPECT_EQ(r.vector.vectorScale(), o[QStringLiteral("vectorScale")].toInt());
    EXPECT_EQ(e.mode, o[QStringLiteral("mode")].toInt());
    EXPECT_EQ(e.selecting(), o[QStringLiteral("selecting")].toBool());
    const QJsonArray coeff = o[QStringLiteral("coeff")].toArray();
    const VectorFrame frame = r.vector.frame(r.host);
    EXPECT_EQ(frame.coeffW, fl(coeff[0]));
    EXPECT_EQ(frame.coeffH, fl(coeff[1]));
    // The mask: legacy's raw Line ("layer|text\r\n").
    QString mask = o[QStringLiteral("mask")].toString();
    const auto lines = r.vector.previewLines(r.host);
    if (mask.isEmpty()) {
        EXPECT_TRUE(lines.empty());
    } else {
        ASSERT_EQ(lines.size(), 1u);
        mask.chop(2); // the record's own line break
        const QString layer = mask.section(QLatin1Char('|'), 0, 0);
        EXPECT_EQ(lines[0].layer.value, layer.toLongLong());
        EXPECT_EQ(lines[0].text, u8(mask.section(QLatin1Char('|'), 1)));
    }
}

} // namespace

TEST(ClipCapture, ReplaysTheLegacyProbe)
{
    const auto observations = readObservations();
    const auto cases = readCases();
    std::map<std::string, std::vector<QJsonObject>> byCase;
    for (const auto &o : observations)
        byCase[str(o[QStringLiteral("case")])].push_back(o);
    ASSERT_EQ(cases.size(), byCase.size());
    std::set<std::pair<std::string, int>> departuresSeen;
    for (const Case &c : cases) {
        SCOPED_TRACE(c.name);
        ASSERT_TRUE(byCase.contains(c.name));
        Replay r(c);
        // The view the probe ran with.
        EXPECT_EQ(r.host.v.videoRect(), c.video);
        const auto [zoomMove, zoomScale] = c.zoom.value_or(std::pair(PointF{0, 0}, PointF{1, 1}));
        EXPECT_EQ(r.host.v.zoomMove(), zoomMove);
        EXPECT_EQ(r.host.v.zoomScale(), zoomScale);
        int dump = 0;
        for (const std::string &op : c.ops) {
            std::istringstream in(op);
            std::string kind;
            in >> kind;
            if (kind != "dump") {
                r.run(kind, in);
                continue;
            }
            SCOPED_TRACE("dump " + std::to_string(dump));
            const auto key = std::pair(c.name, dump);
            const auto departure = kDepartures.contains(key) ? std::optional(kDepartures.at(key)) : std::nullopt;
            if (departure)
                departuresSeen.insert(key);
            compareDump(r, byCase[c.name].at(dump), departure);
            // Each departure's own outcome.
            if (departure == Departure::ReadFromStart) {
                // The clip in the second block is not read from the start.
                EXPECT_FALSE(r.rect.shown());
            } else if (departure == Departure::ZeroRectangle) {
                // Nothing staged or committed; legacy's editor kept the preview.
                EXPECT_FALSE(r.host.g);
                EXPECT_EQ(r.editorText(), u8"{\\b1}Text");
                EXPECT_EQ(u8(byCase[c.name].at(dump)[QStringLiteral("editor")]), u8"{\\clip(300,300,300,450)\\b1}Text");
            } else if (departure == Departure::WheelInvertSlot) {
                // Legacy inverted on each reset until the recursion; nothing here.
                EXPECT_TRUE(byCase[c.name].at(dump)[QStringLiteral("recursion")].toBool());
                EXPECT_EQ(r.steps(), 0);
            }
            ++dump;
        }
        EXPECT_EQ(static_cast<std::size_t>(dump), byCase[c.name].size());
    }
    EXPECT_EQ(departuresSeen.size(), kDepartures.size());
}

// --- The vector editor's reading and writing --------------------------------

TEST(VectorPoints, ParseLikeGetVectorPoints)
{
    // Visuals::GetVectorPoints: "p" reads as "s", "c" only starts a command,
    // a number without its pair or with a bad second number is dropped.
    // A pending x survives a command letter: "5" pairs with the "1" after "b".
    const auto pts = parseVectorPoints(u"m 0 0 l 10 x 5 b 1 2 3 4 5 6 s 7 8 9 10 11 12 p 13 14 c l 15 16");
    const std::vector<VectorPoint> expected = {
        {0, 0, u'm', true},   {5, 1, u'b', true},    {2, 3, u'b', false},   {4, 5, u'b', false}, {6, 7, u's', true},
        {8, 9, u's', false},  {10, 11, u's', false}, {12, 13, u's', true},  {14, 15, u'l', true}};
    EXPECT_EQ(pts, expected);
}

TEST(VectorPoints, SerializeLikeGetVisual)
{
    std::vector<VectorPoint> pts = {{0.5f, -0.4f, u'm', true},  {100.5f, 0, u'l', true}, {101.5f, 2, u'l', true},
                                    {1, 1, u'b', true},          {2, 2, u'b', false},     {3, 3, u'b', false},
                                    {4, 4, u's', true},          {5, 5, u's', false},     {6, 6, u's', false},
                                    {7, 7, u'm', true}};
    // printf's rounding (half to even), "-0" kept; letters only when they
    // change; the spline closes with "c"; a lone last "m" gets an "l".
    EXPECT_EQ(serializeVectorPoints(pts, "6.0f"), u"m 0 -0 l 100 0 102 2 b 1 1 2 2 3 3 s 4 4 5 5 6 6 c m 7 7 l 7 7");
    EXPECT_EQ(serializeVectorPoints({{1.25f, 2.5f, u'm', true}}, "6.2f", {1, 1}), u"m 2.25 3.5");
}

TEST(VectorPoints, LegacyNumbers)
{
    double d = 0;
    EXPECT_TRUE(legacy::cDouble(u"1e2", d));
    EXPECT_EQ(d, 100);
    EXPECT_TRUE(legacy::cDouble(u" +2.5", d));
    EXPECT_EQ(d, 2.5);
    EXPECT_TRUE(legacy::cDouble(u"0x10", d));
    EXPECT_EQ(d, 16);
    EXPECT_FALSE(legacy::cDouble(u"1,5", d));
    EXPECT_FALSE(legacy::cDouble(u"", d));
    EXPECT_FALSE(legacy::cDouble(u"1e999", d)); // ERANGE
    EXPECT_FALSE(legacy::cDouble(u"--1", d));
    EXPECT_EQ(legacy::atoi(u" -12x"), -12);
    EXPECT_EQ(legacy::atoi(u"10.7"), 10);
    EXPECT_EQ(legacy::atoi(u"99999999999"), 2147483647);
    EXPECT_EQ(legacy::getfloat(-0.4f, "6.0f"), u"-0");
    EXPECT_EQ(legacy::getfloat(2.5f, "6.0f"), u"2");
    EXPECT_EQ(legacy::getfloat(1.5f, "6.2f"), u"1.5");
    EXPECT_EQ(legacy::getfloat(3.0f, "6.2f"), u"3");
    EXPECT_EQ(legacy::toInt(-2.9f), -2);
    EXPECT_EQ(legacy::toInt(3e9f), INT_MIN);
}

TEST(ClipText, TagOrderAndPlaces)
{
    // ClipRect::ChangeVisual: the first clip (any kind) is replaced in place;
    // without one the tag goes first in the first block, or a new block.
    EXPECT_EQ(clip::putRectangle(u"{\\an8\\clip(1,2,3,4)\\bord2}T", 5, 6, 7, 8, false), u"{\\an8\\clip(5,6,7,8)\\bord2}T");
    EXPECT_EQ(clip::putRectangle(u"{\\an8}T", 5, 6, 7, 8, true), u"{\\iclip(5,6,7,8)\\an8}T");
    EXPECT_EQ(clip::putRectangle(u"T", 5, 6, 7, 8, false), u"{\\clip(5,6,7,8)}T");
    EXPECT_EQ(clip::putRectangle(u"", 5, 6, 7, 8, false), u"{\\clip(5,6,7,8)}");
    // The vector clip keeps \clip or \iclip; an empty body removes it.
    EXPECT_EQ(clip::putVector(u"{\\b1\\iclip(m 0 0 l 1 1)}T", u"m 5 5 l 6 6"), u"{\\b1\\iclip(m 5 5 l 6 6)}T");
    EXPECT_EQ(clip::putVector(u"{\\iclip(m 0 0 l 1 1)}T", u""), u"T");
    std::u16string tag;
    EXPECT_EQ(clip::putVector(u"T", u"m 1 1", &tag), u"{\\clip(m 1 1)}T");
    EXPECT_EQ(tag, u"iclip(");
    // Several subpaths and the scale prefix are one body.
    EXPECT_EQ(clip::readVector(u"{\\clip(2,m 0 0 l 1 1 m 5 5 l 6 6)}").body, u"m 0 0 l 1 1 m 5 5 l 6 6");
    EXPECT_EQ(clip::readVector(u"{\\clip(2,m 0 0 l 1 1 m 5 5 l 6 6)}").divisor, 2);
    EXPECT_EQ(clip::readVector(u"{\\clip(4,m 0 0)}").divisor, 8);
}

TEST(ClipText, InvertFollowsTheActiveLinesLastClip)
{
    EXPECT_EQ(clip::invertedName(u"{\\iclip(1,2,3,4)\\clip(m 0 0)}T", false), u"clip");
    EXPECT_EQ(clip::invertedName(u"{\\iclip(1,2,3,4)\\clip(m 0 0)}T", true), u"iclip");
    EXPECT_EQ(clip::invertedName(u"T", true), u"");
    // Every clip of the kind gets the name, whatever it was.
    EXPECT_EQ(clip::renameClips(u"{\\clip(m 0 0)\\iclip(m 1 1)\\clip(1,2,3,4)}", u"iclip", true),
              u"{\\iclip(m 0 0)\\iclip(m 1 1)\\clip(1,2,3,4)}");
    EXPECT_FALSE(clip::renameClips(u"{\\clip(1,2,3,4)}", u"iclip", true));
}

// --- Gestures, the overlay and the mask --------------------------------------

namespace {

constexpr std::string_view kScript = "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
                                     "[Events]\n"
                                     "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\clip(m 0 0 l 300 0 300 300)}first\n"
                                     "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,second\n";

void setUp(TestHost &host, bool reference = false)
{
    host.s = std::make_unique<EditSession>(load(kScript), reference);
    const auto id = ids(*host.s)[0];
    host.s->setSelection({id, {id}, id, std::nullopt});
    host.v.setClient(640, 360, 0);
    host.v.setScript(1920, 1080);
    host.v.open({1280, 720, 0, 1});
}

Pointer at(Pointer::Kind kind, int x, int y, bool left = false, Pointer::Button b = Pointer::Button::None)
{
    Pointer p;
    p.kind = kind;
    p.x = x;
    p.y = y;
    p.leftDown = left;
    p.button = b;
    return p;
}

} // namespace

TEST(ClipGesture, ADragIsOneStepAndEscRestores)
{
    TestHost host;
    setUp(host);
    VectorClipTool tool;
    host.tool = &tool;
    tool.editor().mode = VectorEditor::Drag;
    tool.reset(host);
    const auto id = ids(*host.s)[0];
    const std::size_t steps = host.s->historySize();
    tool.pointer(at(Pointer::Kind::Press, 100, 0, true, Pointer::Button::Left), host);
    for (int i = 1; i <= 10; ++i)
        tool.pointer(at(Pointer::Kind::Move, 100 + i, i, true), host);
    EXPECT_EQ(line(*host.s, id)->text, u8"{\\clip(m 0 0 l 300 0 300 300)}first"); // nothing reaches the Document
    tool.pointer(at(Pointer::Kind::Release, 110, 10, false, Pointer::Button::Left), host);
    EXPECT_EQ(line(*host.s, id)->text, u8"{\\clip(m 0 0 l 330 30 300 300)}first");
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(host.s->history().back().name, "Visual vector clipping tool");
    // Esc during a drag: the Document and the points as before it.
    tool.pointer(at(Pointer::Kind::Press, 110, 10, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 150, 50, true), host);
    ASSERT_TRUE(host.g);
    host.cancelGesture();
    tool.pointer(at(Pointer::Kind::Move, 160, 60, true), host);
    tool.pointer(at(Pointer::Kind::Release, 160, 60, false, Pointer::Button::Left), host);
    EXPECT_EQ(line(*host.s, id)->text, u8"{\\clip(m 0 0 l 330 30 300 300)}first");
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(tool.editor().points[1].x, 330);
}

TEST(ClipGesture, KeyNudgeCommitsOnReleaseOncePerPress)
{
    TestHost host;
    setUp(host);
    RectangleClipTool tool;
    host.tool = &tool;
    const auto id = ids(*host.s)[0];
    ASSERT_TRUE(host.s->editDraftText(id, u8"{\\clip(30,30,300,300)}first"));
    tool.reset(host);
    const std::size_t steps = host.s->historySize();
    Key d;
    d.key = keys::D;
    EXPECT_TRUE(tool.key(d, host));
    // Qt's auto-repeat: a release and a press each time, both auto-repeat.
    for (int i = 0; i < 2; ++i) {
        Key repeat = d;
        repeat.autoRepeat = true;
        repeat.release = true;
        EXPECT_TRUE(tool.key(repeat, host));
        repeat.release = false;
        EXPECT_TRUE(tool.key(repeat, host));
    }
    EXPECT_TRUE(host.g);
    d.release = true;
    EXPECT_TRUE(tool.key(d, host));
    EXPECT_FALSE(host.g);
    // The pending draft first (its own step), then the nudge as one step.
    EXPECT_EQ(line(*host.s, id)->text, u8"{\\clip(30,30,303,300)}first");
    EXPECT_EQ(host.s->historySize(), steps + 2);
}

// Removing the last point in an add mode (1-3). Legacy's RemovePoints ends in
// SetClip(true), whose empty-clip branch sends the edit at once
// (VisualClips.cpp:444-455, edit->Send). Its hover block then returns before
// the MiddleUp (VisualClips.cpp:906-907: psize < 1), so no later release
// commits anything. The removal is its own step and no gesture stays open.
TEST(ClipGesture, RemovingTheLastPointCommitsAtOnce)
{
    for (const int mode : {VectorEditor::Line, VectorEditor::Bezier, VectorEditor::Spline}) {
        SCOPED_TRACE(mode);
        TestHost host;
        setUp(host);
        const auto id = ids(*host.s)[1];
        host.s->setSelection({id, {id}, id, std::nullopt});
        VectorClipTool tool;
        host.tool = &tool;
        tool.editor().mode = static_cast<VectorEditor::Mode>(mode);
        tool.reset(host);
        const std::size_t steps = host.s->historySize();
        tool.pointer(at(Pointer::Kind::Press, 176, 108, true, Pointer::Button::Left), host);
        tool.pointer(at(Pointer::Kind::Release, 176, 108, false, Pointer::Button::Left), host);
        EXPECT_EQ(line(*host.s, id)->text, u8"{\\clip(m 528 324)}second");
        EXPECT_EQ(host.s->historySize(), steps + 1);
        Key all;
        all.key = keys::A;
        all.control = true;
        EXPECT_TRUE(tool.key(all, host));
        tool.pointer(at(Pointer::Kind::Press, 176, 108, false, Pointer::Button::Middle), host);
        EXPECT_FALSE(host.g);
        EXPECT_EQ(line(*host.s, id)->text, u8"second");
        EXPECT_EQ(host.s->historySize(), steps + 2);
        tool.pointer(at(Pointer::Kind::Release, 176, 108, false, Pointer::Button::Middle), host);
        EXPECT_FALSE(host.g);
        EXPECT_EQ(line(*host.s, id)->text, u8"second");
        EXPECT_EQ(host.s->historySize(), steps + 2);
        // The next click starts a new clip on the Document's text.
        tool.pointer(at(Pointer::Kind::Press, 200, 120, true, Pointer::Button::Left), host);
        tool.pointer(at(Pointer::Kind::Release, 200, 120, false, Pointer::Button::Left), host);
        EXPECT_FALSE(host.g);
        EXPECT_EQ(line(*host.s, id)->text, u8"{\\clip(m 600 360)}second");
        EXPECT_EQ(host.s->historySize(), steps + 3);
    }
}

// The same through Delete: the removal is sent at once, and the key's release
// finds nothing left to commit.
TEST(ClipGesture, DeletingEveryPointCommitsAtOnce)
{
    TestHost host;
    setUp(host);
    const auto id = ids(*host.s)[0];
    VectorClipTool tool;
    host.tool = &tool;
    tool.editor().mode = VectorEditor::Line;
    tool.reset(host);
    const std::size_t steps = host.s->historySize();
    Key all;
    all.key = keys::A;
    all.control = true;
    EXPECT_TRUE(tool.key(all, host));
    Key del;
    del.key = keys::Delete;
    EXPECT_TRUE(tool.key(del, host));
    EXPECT_FALSE(host.g);
    EXPECT_EQ(line(*host.s, id)->text, u8"first");
    EXPECT_EQ(host.s->historySize(), steps + 1);
    del.release = true;
    (void)tool.key(del, host);
    EXPECT_FALSE(host.g);
    EXPECT_EQ(host.s->historySize(), steps + 1);
}

TEST(ClipGesture, ProtectedReferenceRefusesWrites)
{
    TestHost host;
    setUp(host, true);
    VectorClipTool tool;
    tool.editor().mode = VectorEditor::Drag;
    tool.reset(host);
    tool.pointer(at(Pointer::Kind::Press, 100, 0, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 110, 10, true), host);
    tool.pointer(at(Pointer::Kind::Release, 110, 10, false, Pointer::Button::Left), host);
    EXPECT_FALSE(host.g);
    EXPECT_EQ(line(*host.s, ids(*host.s)[0])->text, u8"{\\clip(m 0 0 l 300 0 300 300)}first");
    tool.setOption("invert", 1, host);
    EXPECT_EQ(line(*host.s, ids(*host.s)[0])->text, u8"{\\clip(m 0 0 l 300 0 300 300)}first");
}

// --- Approved departures (User, 2026-10-05, wave-5 batch-3 review) ---------

namespace {

// setUp over two Lines with the given texts, the first active.
void setUpLines(TestHost &host, std::string_view first, std::string_view second)
{
    std::string script = "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n[Events]\n";
    script += "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,";
    script += first;
    script += "\nDialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,";
    script += second;
    script += "\n";
    host.s = std::make_unique<EditSession>(load(script), false);
    const auto id = ids(*host.s)[0];
    host.s->setSelection({id, {id}, id, std::nullopt});
    host.v.setClient(640, 360, 0);
    host.v.setScript(1920, 1080);
    host.v.open({1280, 720, 0, 1});
}

} // namespace

// T4-wheel-invert-slot: legacy's ChangeTool inverted the clip on every reset
// while the wheel had left the mode on the Invert clip button (6), and the
// inversion's SetModified reset the tool again, recursing until the stack
// overflowed (VisualClips.cpp:1499-1504; capture vector-wheel-to-invert).
// Here a reset never inverts; only the button does, once.
TEST(ClipDeparture, OnlyTheInvertButtonInverts)
{
    TestHost host;
    setUpLines(host, "{\\clip(m 0 0 l 300 0 300 300)}first", "{\\clip(m 0 0 l 600 0 600 600)}second");
    const auto first = ids(*host.s)[0];
    const auto second = ids(*host.s)[1];
    VectorClipTool tool;
    host.tool = &tool;
    tool.editor().mode = VectorEditor::Drag;
    tool.reset(host);
    Pointer wheel = at(Pointer::Kind::Wheel, 10, 10);
    wheel.wheelSteps = 1;
    tool.pointer(wheel, host); // 0 - 1 wraps to the Invert clip button
    ASSERT_EQ(tool.editor().mode, VectorEditor::InvertSlot);
    const std::size_t steps = host.s->historySize();
    // Resets: the same Line, then another active Line, then back.
    tool.reset(host);
    host.s->setSelection({second, {second}, second, std::nullopt});
    tool.reset(host);
    host.s->setSelection({first, {first}, first, std::nullopt});
    tool.reset(host);
    EXPECT_FALSE(host.g);
    EXPECT_EQ(host.s->historySize(), steps);
    EXPECT_EQ(line(*host.s, first)->text, u8"{\\clip(m 0 0 l 300 0 300 300)}first");
    EXPECT_EQ(line(*host.s, second)->text, u8"{\\clip(m 0 0 l 600 0 600 600)}second");
    // The button inverts once, as one step, and its own reset adds nothing.
    EXPECT_TRUE(tool.setOption("invert", 1, host));
    EXPECT_EQ(line(*host.s, first)->text, u8"{\\iclip(m 0 0 l 300 0 300 300)}first");
    EXPECT_EQ(host.s->historySize(), steps + 1);
    tool.reset(host);
    EXPECT_EQ(line(*host.s, first)->text, u8"{\\iclip(m 0 0 l 300 0 300 300)}first");
    EXPECT_EQ(host.s->historySize(), steps + 1);
}

// T4-bezier-past-end: a Bézier missing its points (a "b" with fewer than
// three points before the end). Legacy's DrawCurve read its controls past
// the last point (VisualClips.cpp:806-808), and RemovePoints set the point two
// after the Bézier's start to "l" past the end (VisualClips.cpp:1398-1410). Here the incomplete Bézier
// is drawn as its points only (no arms, no curve) and its removal touches
// nothing past the end.
TEST(ClipDeparture, AnIncompleteBezierStopsAtTheLastPoint)
{
    // The clip, and where its last point is on the view (3 script units a pixel).
    for (const auto &[clipText, lastAt] : {std::pair("{\\clip(m 0 0 b 300 300 600 600)}first", 200),
                                           std::pair("{\\clip(m 0 0 b 300 300)}first", 100)}) {
        SCOPED_TRACE(clipText);
        TestHost host;
        setUpLines(host, clipText, "second");
        const auto id = ids(*host.s)[0];
        VectorClipTool tool;
        host.tool = &tool;
        tool.editor().mode = VectorEditor::Drag;
        tool.reset(host);
        // Drawn: no arms or curve from controls that are not there, and
        // nothing outside the points' own box (0,0 to 200,200 on the view).
        const Overlay o = tool.overlay(host);
        for (const auto &l : o.lines) {
            EXPECT_NE(l.argb, 0xFF0000FFu); // a Bézier's arms
            for (const PointF p : {l.from, l.to}) {
                EXPECT_GE(p.x, -10.f);
                EXPECT_GE(p.y, -10.f);
                EXPECT_LE(p.x, 210.f);
                EXPECT_LE(p.y, 210.f);
            }
        }
        // Removed: a middle click on the last "b" point takes the Bézier.
        const std::size_t steps = host.s->historySize();
        tool.pointer(at(Pointer::Kind::Press, lastAt, lastAt, false, Pointer::Button::Middle), host);
        tool.pointer(at(Pointer::Kind::Release, lastAt, lastAt, false, Pointer::Button::Middle), host);
        EXPECT_FALSE(host.g);
        ASSERT_EQ(tool.editor().points.size(), 1u);
        EXPECT_EQ(tool.editor().points[0].type, u'm');
        EXPECT_EQ(line(*host.s, id)->text, u8"{\\clip(m 0 0)}first");
        EXPECT_EQ(host.s->historySize(), steps + 1);
    }
}

// T4-zero-rect-preview: a rectangle released with no width or height. Legacy
// wrote nothing on the release and left the rectangle's preview in the Line
// editor, unsent (VisualClipRect.cpp:129-147; capture rect-zero-width). Here
// the gesture is dropped: nothing staged, committed or shown.
TEST(ClipDeparture, AZeroRectangleLeavesNoPreview)
{
    for (const auto &[toX, toY] : {std::pair(100, 150), std::pair(150, 100), std::pair(100, 100)}) {
        SCOPED_TRACE(std::to_string(toX) + "," + std::to_string(toY));
        TestHost host;
        setUpLines(host, "{\\b1}first", "second");
        const auto id = ids(*host.s)[0];
        RectangleClipTool tool;
        host.tool = &tool;
        tool.reset(host);
        const std::size_t steps = host.s->historySize();
        tool.pointer(at(Pointer::Kind::Press, 100, 100, true, Pointer::Button::Left), host);
        tool.pointer(at(Pointer::Kind::Move, toX, toY, true), host);
        tool.pointer(at(Pointer::Kind::Release, toX, toY, false, Pointer::Button::Left), host);
        EXPECT_FALSE(host.g); // nothing staged: the Line editor shows the Line
        EXPECT_FALSE(tool.shown());
        EXPECT_TRUE(tool.overlay(host).polygons.empty());
        EXPECT_EQ(line(*host.s, id)->text, u8"{\\b1}first");
        EXPECT_EQ(host.s->historySize(), steps);
        // The next drag draws a rectangle as one step.
        tool.pointer(at(Pointer::Kind::Press, 10, 10, true, Pointer::Button::Left), host);
        tool.pointer(at(Pointer::Kind::Move, 110, 60, true), host);
        tool.pointer(at(Pointer::Kind::Release, 110, 60, false, Pointer::Button::Left), host);
        EXPECT_FALSE(host.g);
        EXPECT_EQ(line(*host.s, id)->text, u8"{\\clip(30,30,330,180)\\b1}first");
        EXPECT_EQ(host.s->historySize(), steps + 1);
    }
}

// T4-clip-read-start: legacy's ClipRect read the clip with FindTag from the
// Line editor's caret (VisualClipRect.cpp:229), so a caret in the second
// block found the clip there (capture rect-second-block-caret: shown, 30,30
// to 300,300); with the caret at the start it did not
// (rect-second-block-start). The tools do not follow the editor's caret: the
// clip is read from the Line's start, and a drag writes its own clip there.
TEST(ClipDeparture, TheClipIsReadFromTheLineStart)
{
    TestHost host;
    setUpLines(host, "{\\b1}Te{\\clip(30,30,300,300)}xt", "second");
    const auto id = ids(*host.s)[0];
    RectangleClipTool tool;
    host.tool = &tool;
    tool.reset(host);
    EXPECT_FALSE(tool.shown());
    EXPECT_TRUE(tool.overlay(host).polygons.empty());
    tool.pointer(at(Pointer::Kind::Press, 10, 10, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 110, 60, true), host);
    tool.pointer(at(Pointer::Kind::Release, 110, 60, false, Pointer::Button::Left), host);
    EXPECT_EQ(line(*host.s, id)->text, u8"{\\clip(30,30,330,180)\\b1}Te{\\clip(30,30,300,300)}xt");
}

TEST(ClipOverlay, RectangleMaskAndOutline)
{
    // ClipRect::DrawVisual: the outside darkened by two fans filled as one
    // path, the outline one pixel inside the right and bottom edges.
    TestHost host;
    setUp(host);
    const auto id = ids(*host.s)[0];
    ASSERT_TRUE(host.s->editDraftText(id, u8"{\\clip(300,150,900,600)}first"));
    RectangleClipTool tool;
    tool.reset(host);
    const Overlay o = tool.overlay(host);
    ASSERT_EQ(o.polygons.size(), 1u);
    EXPECT_FALSE(o.polygons[0].above);
    EXPECT_EQ(o.polygons[0].fill, 0x88000000u);
    EXPECT_EQ(o.polygons[0].points,
              (std::vector<PointF>{{0, 0}, {640, 0}, {299, 50}, {100, 50}, {100, 199}, {0, 360}}));
    ASSERT_EQ(o.polygons[0].more.size(), 1u);
    ASSERT_EQ(o.lines.size(), 4u);
    EXPECT_EQ(o.lines[0].from, (PointF{100, 50}));
    EXPECT_EQ(o.lines[1].from, (PointF{100, 199}));
    EXPECT_EQ(o.lines[2].from, (PointF{299, 199}));
    EXPECT_EQ(o.lines[0].argb, 0xFFBB0000u);
    // \iclip: the inside.
    ASSERT_TRUE(host.s->editDraftText(id, u8"{\\iclip(300,150,900,600)}first"));
    tool.reset(host);
    const Overlay inv = tool.overlay(host);
    EXPECT_EQ(inv.polygons[0].points, (std::vector<PointF>{{100, 50}, {299, 50}, {299, 199}, {100, 199}}));
    EXPECT_TRUE(inv.polygons[0].more.empty());
}

TEST(ClipOverlay, VectorHandlesAndPath)
{
    // DrawingAndClip::DrawVisual: the path's lines, then a square on each
    // point (the "m"s and the last line's), the closing line back to the "m".
    TestHost host;
    setUp(host);
    VectorClipTool tool;
    tool.reset(host);
    const Overlay o = tool.overlay(host);
    ASSERT_EQ(o.lines.size(), 3u);
    EXPECT_EQ(o.lines[0].from, (PointF{0, 0}));
    EXPECT_EQ(o.lines[0].to, (PointF{100, 0}));
    EXPECT_EQ(o.lines[2].from, (PointF{100, 100})); // closing
    EXPECT_EQ(o.lines[2].to, (PointF{0, 0}));
    int handles = 0;
    for (const auto &p : o.polygons)
        handles += p.above && p.border == 0xFFBB0000u;
    EXPECT_EQ(handles, 3); // DrawLine's previous point, then lastM and g - 1
    // A Bézier: its arms, its flattened curve and circles on its controls.
    ASSERT_TRUE(host.s->editDraftText(ids(*host.s)[0], u8"{\\clip(m 0 0 b 300 0 300 300 0 300)}first"));
    tool.reset(host);
    const Overlay b = tool.overlay(host);
    int arms = 0;
    for (const auto &l : b.lines)
        arms += l.argb == 0xFF0000FFu;
    EXPECT_EQ(arms, 2);
    EXPECT_GT(b.lines.size(), 6u);
}

TEST(ClipMask, PreviewLineAboveEverything)
{
    TestHost host;
    setUp(host);
    VectorClipTool tool;
    tool.reset(host);
    const auto lines = tool.previewLines(host);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].layer.value, 2147483647);
    EXPECT_EQ(lines[0].text, u8"{\\p1\\bord0\\shad0\\fscx100\\fscy100\\frz0\\1c&H000000&\\1a&H77&\\pos(0,0)\\an7\\"
                             u8"iclip(m 0 0 l 300 0 300 300)}m 0 0 l 1920 0 1920 1080 0 1080");
    // The rectangle clip has none (its mask is the overlay's).
    RectangleClipTool rect;
    rect.reset(host);
    EXPECT_TRUE(rect.previewLines(host).empty());
}

TEST(ClipOptions, LegacyToolbarItems)
{
    TestHost host;
    setUp(host);
    VectorClipTool tool;
    tool.reset(host);
    const auto options = tool.options(host);
    ASSERT_EQ(options.size(), 7u);
    const char *roles[] = {"vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point",
                           "vector-delete", "clip-invert"};
    const char16_t *tips[] = {u"Move points", u"Add line", u"Add Bézier curve", u"Add B-spline",
                              u"Add separate point", u"Delete point", u"Invert clip"};
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(options[i].iconRole, roles[i]);
        EXPECT_EQ(options[i].tooltip, tips[i]);
        EXPECT_EQ(options[i].kind, i < 6 ? ToolOption::Kind::Toggle : ToolOption::Kind::Action);
    }
    EXPECT_TRUE(options[1].checked); // VectorItem::toggled = 1
    EXPECT_TRUE(tool.setOption("mode3", 1, host));
    EXPECT_EQ(tool.editor().mode, 3);
    EXPECT_TRUE(tool.options(host)[3].checked);
    // The wheel's way onto the Invert clip button (legacy's seven buttons).
    Pointer wheel;
    wheel.kind = Pointer::Kind::Wheel;
    wheel.wheelSteps = 4;
    tool.pointer(wheel, host);
    EXPECT_EQ(tool.editor().mode, 6);
    EXPECT_TRUE(tool.options(host)[6].checked);
    // Ctrl+wheel never reaches the tool (VideoBox resizes the window).
    wheel.control = true;
    tool.pointer(wheel, host);
    EXPECT_EQ(tool.editor().mode, 6);
    RectangleClipTool rect;
    ASSERT_EQ(rect.options(host).size(), 1u);
    EXPECT_EQ(rect.options(host)[0].iconRole, "clip-invert");
}

TEST(ClipMakeTool, OneLinePerFamily)
{
    EXPECT_EQ(makeVisualTool(Family::RectangleClip)->family(), Family::RectangleClip);
    EXPECT_EQ(makeVisualTool(Family::VectorClip)->family(), Family::VectorClip);
}
