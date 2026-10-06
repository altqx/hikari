// T5: the drawing tool and its shape presets. The tool replays the legacy
// probe's captures (tools/legacy-capture/clip_capture.cpp built as
// legacy_drawing_capture over inputs/drawing-cases.txt; tests/fixtures/
// legacy-observations/local-t5-drawing-20261005), which ran the legacy
// VisualClips.cpp and VisualDrawingShapes.cpp's Shapes unchanged: every
// committed text, the editor's text during a gesture, the points, the
// drawing's position, scale, alignment and rotation, the shape rectangle,
// its scale and the presets read and written must be legacy's, float for
// float. No case differs but those of the approved departure
// T5-change-scale-garble (departureFor), which keep legacy's text as the old
// evidence.

#include "hikari/application/shape_presets.h"
#include "hikari/application/visual_drawing.h"
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

// A VisualHost over a VideoView and an EditSession (as T1's and T4's
// tests), with the video's time and the presets VideoToolbar keeps
// (loaded from the case's ShapesSettings.txt, else legacy's defaults, at
// the first use; reloaded while empty, as GetShapesSettings).
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
    std::int64_t timeMs = 1500;
    std::optional<std::u16string> shapesFile;
    mutable std::vector<ShapePreset> presets;

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
    std::int64_t videoTimeMs() const override { return timeMs; }
    const std::vector<ShapePreset> *shapePresets() const override
    {
        if (presets.empty())
            presets = shapesFile ? parseShapePresets(*shapesFile) : defaultShapePresets();
        return &presets;
    }
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
    struct Line {
        std::string text;
        std::optional<std::string> translation;
        int marginL = 0, marginR = 0, marginV = 0;
    };
    std::vector<Line> lines;
    std::map<std::string, std::string> style; // the probe's Styles stand-in
    std::vector<int> select;
    int mode = 1;
    std::optional<std::string> shapesFile;
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
    std::ifstream in(HIKARI_DRAWING_CASES);
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
            c.lines.push_back({rest(ls), std::nullopt});
        } else if (key == "tl") {
            c.lines.back().translation = rest(ls);
        } else if (key == "margins") {
            ls >> c.lines.back().marginL >> c.lines.back().marginR >> c.lines.back().marginV;
        } else if (key == "style") {
            std::string field;
            ls >> field;
            c.style[field] = rest(ls);
        } else if (key == "shapesfile") {
            c.shapesFile = c.shapesFile.value_or("") + rest(ls) + "\n";
        } else if (key == "select") {
            int i;
            while (ls >> i)
                c.select.push_back(i);
        } else if (key == "tool") {
            toolSeen = true;
        } else if (key == "mode" && !toolSeen) {
            ls >> c.mode;
        } else if (key == "end") {
            out.push_back(c);
        } else {
            c.ops.push_back(text);
        }
    }
    return out;
}

std::vector<QJsonObject> readObservations()
{
    QFile f(QStringLiteral(HIKARI_DRAWING_OBSERVATIONS));
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

std::u8string u8(const QJsonValue &v)
{
    const QByteArray b = v.toString().toUtf8();
    return std::u8string(b.begin(), b.end());
}

struct Replay {
    const Case &c;
    TestHost host;
    DrawingTool tool;
    std::vector<core::LineId> lineIds;
    std::size_t historyStart = 0;
    int pendingMode = 1;
    std::u16string savedFile;

    explicit Replay(const Case &cs) : c(cs)
    {
        const auto field = [&](const char *name, const char *fallback) {
            return c.style.contains(name) ? c.style.at(name) : std::string(fallback);
        };
        // The probe's Styles stand-in: alignment 2, margins 0, angle 0, scale 100.
        std::string script = "[Script Info]\nScriptType: v4.00+\nPlayResX: " + std::to_string(c.scriptW) +
                             "\nPlayResY: " + std::to_string(c.scriptH) + "\n";
        bool tl = false;
        for (const auto &l : c.lines)
            tl |= l.translation.has_value();
        if (tl)
            script += "TLMode: Yes\n";
        script += "\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                  "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, "
                  "Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                  "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0," +
                  field("ScaleX", "100") + "," + field("ScaleY", "100") + ",0," + field("Angle", "0") + ",1,2,2," +
                  field("Alignment", "2") + "," + field("MarginL", "0") + "," + field("MarginR", "0") + "," +
                  field("MarginV", "0") + ",1\n";
        script += "\n[Events]\n";
        for (const auto &l : c.lines)
            script += "Dialogue: 0,0:00:01.00,0:00:02.00,Default,," + std::to_string(l.marginL) + "," +
                      std::to_string(l.marginR) + "," + std::to_string(l.marginV) + ",,x\n";
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
                r.text = std::u8string(c.lines[i].text.begin(), c.lines[i].text.end());
                if (c.lines[i].translation)
                    r.translation = std::u8string(c.lines[i].translation->begin(), c.lines[i].translation->end());
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
        if (c.shapesFile)
            host.shapesFile = QString::fromStdString(*c.shapesFile).toStdU16String();
        host.tool = &tool;
        pendingMode = c.mode;
    }

    // Legacy rendered after every event (Draw -> DrawVisual).
    void render() { (void)tool.overlay(host); }

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
            tool.pointer(p, host);
            render();
        };
        if (op == "setvisual") {
            tool.editor().mode = pendingMode;
            tool.reset(host);
            render();
        } else if (op == "mode") {
            in >> pendingMode;
        } else if (op == "shape") {
            int n;
            in >> n;
            tool.setOption("shape", n, host);
            render();
        } else if (op == "time") {
            in >> host.timeMs;
            render();
        } else if (op == "down" || op == "dclick") {
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
            tool.pointer(p, host);
            render();
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
            tool.key(key, host);
            key.release = true; // the nudge commits on the key's release
            tool.key(key, host);
            render();
        } else if (op == "activate") {
            int i;
            in >> i;
            host.picker.clear();
            host.s->setSelection({lineIds[i], {lineIds[i]}, lineIds[i], std::nullopt});
            tool.reset(host);
            render();
        } else if (op == "savepresets") {
            savedFile = u"﻿" + writeShapePresets(*host.shapePresets());
        }
    }

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

    int steps() const { return static_cast<int>(host.s->historySize() - historyStart); }
};

// Approved departures (docs/qt/compatibility-decisions.md) where a dump's
// legacy text is kept as the old evidence and the rewrite expects another.
struct Departure {
    std::string caseName;
    int dump = 0;
    std::u8string text;   // the first Line's committed text
    std::u8string editor; // the Line editor's text
};

const Departure *departureFor(const std::string &caseName, int dump)
{
    // T5-change-scale-garble: the drawing's place moves by what \fscx and
    // \fscy added, so the next sample replaces the drawing, not the tags
    // (legacy: "{\fscy12m 1 1 l 301 ...", "{\fscx9300\fscy9300\an7\pos(m 1 1 ...").
    static const std::vector<Departure> list{
        {"shapes-change-scale-new-tags", 1, u8"{\\an7\\pos(300,300)}Text",
         u8"{\\fscy120\\fscx150\\p1\\an7\\pos(300,300)}m 1 1 l 301 1 301 181 1 181{\\p0}{Text}"},
        {"shapes-change-scale-new-tags", 2,
         u8"{\\fscy120\\fscx150\\p1\\an7\\pos(300,300)}m 1 1 l 301 1 301 181 1 181{\\p0}{Text}",
         u8"{\\fscy120\\fscx150\\p1\\an7\\pos(300,300)}m 1 1 l 301 1 301 181 1 181{\\p0}{Text}"},
        {"shapes-change-scale-longer-tags", 1,
         u8"{\\fscx9300\\fscy9300\\an7\\pos(300,300)\\p1}m 1 1 l 30001 1 30001 30001 1 30001{\\p0}{After}",
         u8"{\\fscx9300\\fscy9300\\an7\\pos(300,300)\\p1}m 1 1 l 30001 1 30001 30001 1 30001{\\p0}{After}"},
    };
    for (const auto &d : list)
        if (d.caseName == caseName && d.dump == dump)
            return &d;
    return nullptr;
}

void compareDump(Replay &r, const QJsonObject &o, const Departure *departure)
{
    const EditSession &s = *r.host.s;
    const bool multi = r.host.g && r.host.g->targets().size() > 1;
    const QJsonArray texts = o[QStringLiteral("texts")].toArray();
    for (int i = 0; i < texts.size(); ++i) {
        const auto *l = line(s, r.lineIds[i]);
        if (departure && i == 0) {
            EXPECT_EQ(l->text, departure->text) << "line " << i;
        } else {
            EXPECT_EQ(l->text, u8(texts[i].toArray()[0])) << "line " << i;
        }
        EXPECT_EQ(l->translation, u8(texts[i].toArray()[1])) << "line " << i;
    }
    if (departure) {
        EXPECT_NE(departure->editor, u8(o[QStringLiteral("editor")])) << "the old evidence";
        EXPECT_EQ(r.editorText(), departure->editor);
    } else if (!multi) {
        EXPECT_EQ(r.editorText(), u8(o[QStringLiteral("editor")]));
    }
    EXPECT_EQ(r.steps(), o[QStringLiteral("steps")].toInt());
    const QJsonArray history = o[QStringLiteral("history")].toArray();
    if (r.steps() > 0 && !history.isEmpty()) {
        EXPECT_EQ(str(history.last()), "VISUAL_DRAWING");
        EXPECT_EQ(s.history().back().name, "Visual vector drawing tool"); // SubsFile.cpp:236
    }
    EXPECT_EQ(r.host.bells, o[QStringLiteral("bells")].toInt());
    EXPECT_EQ(static_cast<int>(r.host.notices.size()), o[QStringLiteral("messages")].toArray().size());

    const VectorEditor &e = r.tool.editor();
    const QJsonArray points = o[QStringLiteral("points")].toArray();
    EXPECT_EQ(e.points.size(), static_cast<std::size_t>(points.size()));
    for (int i = 0; i < points.size() && i < static_cast<int>(e.points.size()); ++i) {
        const QJsonArray p = points[i].toArray();
        SCOPED_TRACE("point " + std::to_string(i));
        EXPECT_EQ(e.points[i].x, fl(p[0]));
        EXPECT_EQ(e.points[i].y, fl(p[1]));
        EXPECT_EQ(std::u16string(1, e.points[i].type), p[2].toString().toStdU16String());
        EXPECT_EQ(e.points[i].start, p[3].toInt() == 1);
        EXPECT_EQ(e.points[i].selected, p[4].toInt() == 1);
    }
    EXPECT_EQ(r.tool.body(), o[QStringLiteral("visual")].toString().toStdU16String());
    EXPECT_EQ(r.tool.vectorScale(), o[QStringLiteral("vectorScale")].toInt());
    EXPECT_EQ(e.mode, o[QStringLiteral("mode")].toInt());
    EXPECT_EQ(e.selecting(), o[QStringLiteral("selecting")].toBool());
    const QJsonArray coeff = o[QStringLiteral("coeff")].toArray();
    const VectorFrame frame = r.tool.frame(r.host);
    EXPECT_EQ(frame.coeffW, fl(coeff[0]));
    EXPECT_EQ(frame.coeffH, fl(coeff[1]));

    const QJsonObject d = o[QStringLiteral("drawing")].toObject();
    EXPECT_EQ(r.tool.position().x, fl(d[QStringLiteral("x")]));
    EXPECT_EQ(r.tool.position().y, fl(d[QStringLiteral("y")]));
    EXPECT_EQ(r.tool.scale().x, fl(d[QStringLiteral("scale")].toArray()[0]));
    EXPECT_EQ(r.tool.scale().y, fl(d[QStringLiteral("scale")].toArray()[1]));
    EXPECT_EQ(r.tool.alignment(), d[QStringLiteral("an")].toInt());
    EXPECT_EQ(r.tool.frz(), fl(d[QStringLiteral("frz")]));
    EXPECT_EQ(r.tool.org().x, fl(d[QStringLiteral("org")].toArray()[0]));
    EXPECT_EQ(r.tool.org().y, fl(d[QStringLiteral("org")].toArray()[1]));
    const QJsonArray move = d[QStringLiteral("move")].toArray();
    const auto &mv = r.tool.moveValues();
    EXPECT_EQ(mv[6], move[6].toDouble());
    // Only the values GetPosnScale read (tbl[6] of them): legacy's
    // moveValues is never initialised (Visuals.h:160, Visuals::Visuals), so
    // the others are what the probe's previous case left in that memory.
    for (int i = 0; i < 4 && i < move[6].toInt(); ++i)
        EXPECT_EQ(mv[static_cast<std::size_t>(i)], move[i].toDouble()) << "move " << i;
    // The \move times are read only for a \move (GetMoveTimes, which the
    // probe stubs, gives the others; CalcMovePos never reads them).
    if (mv[6] > 2) {
        EXPECT_EQ(mv[4], move[4].toDouble());
        EXPECT_EQ(mv[5], move[5].toDouble());
    }
    EXPECT_EQ(r.tool.shapeSelection(), d[QStringLiteral("shapeSelection")].toInt());
    EXPECT_EQ(r.tool.shape(), d[QStringLiteral("shape")].toInt());
    const QJsonArray rect = d[QStringLiteral("rect")].toArray();
    EXPECT_EQ(r.tool.rectangle(0).x, fl(rect[0]));
    EXPECT_EQ(r.tool.rectangle(0).y, fl(rect[1]));
    EXPECT_EQ(r.tool.rectangle(1).x, fl(rect[2]));
    EXPECT_EQ(r.tool.rectangle(1).y, fl(rect[3]));
    EXPECT_EQ(r.tool.rectangleVisible(), d[QStringLiteral("rectVisible")].toBool());
    EXPECT_EQ(r.tool.shapeScale().x, fl(d[QStringLiteral("shapeScale")].toArray()[0]));
    EXPECT_EQ(r.tool.shapeScale().y, fl(d[QStringLiteral("shapeScale")].toArray()[1]));
    EXPECT_EQ(r.tool.shapeSize().x, fl(d[QStringLiteral("shapeSize")].toArray()[0]));
    EXPECT_EQ(r.tool.shapeSize().y, fl(d[QStringLiteral("shapeSize")].toArray()[1]));
    EXPECT_EQ(r.tool.shapeGrabbed(), d[QStringLiteral("shapeGrabbed")].toInt());
    const QJsonArray shapePoints = d[QStringLiteral("shapePoints")].toArray();
    ASSERT_EQ(r.tool.shapePoints().size(), static_cast<std::size_t>(shapePoints.size()));
    for (int i = 0; i < shapePoints.size(); ++i) {
        const QJsonArray p = shapePoints[i].toArray();
        EXPECT_EQ(r.tool.shapePoints()[i].x, fl(p[0]));
        EXPECT_EQ(r.tool.shapePoints()[i].y, fl(p[1]));
        EXPECT_EQ(std::u16string(1, r.tool.shapePoints()[i].type), p[2].toString().toStdU16String());
    }

    // The presets VideoToolbar holds, and the file SaveSettings wrote.
    const QJsonArray presets = o[QStringLiteral("presets")].toArray();
    const auto &held = r.host.presets;
    ASSERT_EQ(held.size(), static_cast<std::size_t>(presets.size()));
    for (int i = 0; i < presets.size(); ++i) {
        const QJsonArray p = presets[i].toArray();
        EXPECT_EQ(held[i].name, p[0].toString().toStdU16String());
        EXPECT_EQ(held[i].shape, p[1].toString().toStdU16String());
        EXPECT_EQ(held[i].mode, p[2].toInt());
        EXPECT_EQ(held[i].scalingMode, p[3].toInt());
    }
    if (o.contains(QStringLiteral("file")) && !r.savedFile.empty())
        EXPECT_EQ(r.savedFile, o[QStringLiteral("file")].toString().toStdU16String());
}

} // namespace

TEST(DrawingCapture, ReplaysTheLegacyProbe)
{
    const auto observations = readObservations();
    const auto cases = readCases();
    std::map<std::string, std::vector<QJsonObject>> byCase;
    for (const auto &o : observations)
        byCase[str(o[QStringLiteral("case")])].push_back(o);
    ASSERT_EQ(cases.size(), byCase.size());
    for (const Case &c : cases) {
        SCOPED_TRACE(c.name);
        ASSERT_TRUE(byCase.contains(c.name));
        Replay r(c);
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
            compareDump(r, byCase[c.name].at(dump), departureFor(c.name, dump));
            ++dump;
        }
        EXPECT_EQ(static_cast<std::size_t>(dump), byCase[c.name].size());
    }
}

// --- The presets: legacy's defaults, its reader and writer -------------------

TEST(ShapePresets, LegacyDefaults)
{
    // LoadSettings without the file (VisualDrawingShapes.cpp:299-303).
    const auto presets = defaultShapePresets();
    ASSERT_EQ(presets.size(), 5u);
    EXPECT_EQ(presets[0], (ShapePreset{u"rectangle", u"m 0 0 l 100 0 100 100 0 100", 0, 0}));
    EXPECT_EQ(presets[1].name, u"circle");
    EXPECT_EQ(presets[1].mode, ShapePreset::BothScaleX);
    EXPECT_EQ(presets[2].name, u"rounded square 1");
    EXPECT_EQ(presets[3].name, u"rounded square 2");
    EXPECT_EQ(presets[4].name, u"rounded square 3");
    for (const auto &p : presets)
        EXPECT_EQ(p.scalingMode, ShapePreset::ChangePoints);
    EXPECT_EQ(writeShapePresets(presets), defaultShapePresetsText());
    // The toolbar's list: "Choose", the names, "Edit".
    const auto list = shapeListChoices(presets);
    ASSERT_EQ(list.size(), 7u);
    EXPECT_EQ(list.front(), u"Choose");
    EXPECT_EQ(list[1], u"rectangle");
    EXPECT_EQ(list.back(), u"Edit");
}

TEST(ShapePresets, ReadAndWriteLikeLegacy)
{
    // wxStringTokenizer's STRTOK: empty lines and runs of ";" are skipped;
    // the modes read as whole numbers (blanks before them only), wrapping
    // at 256 as legacy's unsigned char.
    const auto presets = parseShapePresets(u"Shape: a; m 0 0 l 1 1; 1; 0\n\n"
                                           u"Shape: b;;m 0 0;;2;;1\n"
                                           u"Shape: spaced ;  m 1 1 ;  2 ;  1\n" // "2 " does not read
                                           u"Shape: c; m 0 0; x; 0\n"
                                           u"Shape: d; m 0 0\n"
                                           u"Not: e; m 0 0; 0; 0\n"
                                           u"Shape: f; m 0 0; 7; 300\n"
                                           u"Shape: g; m 0 0; 1; 0\r\n"); // the Linux build kept "\r"
    ASSERT_EQ(presets.size(), 3u);
    EXPECT_EQ(presets[0], (ShapePreset{u"a", u"m 0 0 l 1 1", 1, 0}));
    EXPECT_EQ(presets[1], (ShapePreset{u"b", u"m 0 0", 2, 1}));
    EXPECT_EQ(presets[2], (ShapePreset{u"f", u"m 0 0", 7, 44}));
    EXPECT_EQ(writeShapePresets(presets), u"Shape: a; m 0 0 l 1 1; 1; 0\nShape: b; m 0 0; 2; 1\nShape: f; m 0 0; 7; 44\n");
    EXPECT_TRUE(parseShapePresets(u"").empty());
}

// --- The "Vector shape editing" dialog ---------------------------------------

TEST(ShapesEdition, OpensOnThePresetsIndex)
{
    // A preset's index (the controller gives the shape list's selection
    // less its "Choose", T5-editor-opens-next); out of range the first.
    EXPECT_EQ(ShapesEdition(defaultShapePresets(), 2).current().name, u"rounded square 1");
    EXPECT_EQ(ShapesEdition(defaultShapePresets(), 5).current().name, u"rectangle");
    EXPECT_EQ(ShapesEdition(defaultShapePresets(), -1).selection(), 0);
    // Legacy indexed an empty list (a crash); here the fields stay empty.
    const ShapesEdition empty({}, 0);
    EXPECT_EQ(empty.selection(), -1);
    EXPECT_TRUE(empty.name.empty());
}

TEST(ShapesEdition, AddAndDelete)
{
    ShapesEdition e(defaultShapePresets(), 0);
    EXPECT_EQ(e.addShape(u"")->text, u"Enter a name for the new shape.");
    EXPECT_EQ(e.addShape(u"CIRCLE")->text, u"New shape name already exists, enter another name."); // ignoring case
    EXPECT_FALSE(e.addShape(u"star"));
    EXPECT_EQ(e.presets().size(), 6u);
    EXPECT_EQ(e.selection(), 5);
    EXPECT_EQ(e.name, u"star");
    EXPECT_TRUE(e.shape.empty());
    EXPECT_EQ(e.mode, 0);
    // Delete: the selection stays at its place, or the new last one.
    EXPECT_FALSE(e.removeShape());
    EXPECT_EQ(e.presets().size(), 5u);
    EXPECT_EQ(e.selection(), 4);
    EXPECT_EQ(e.name, u"rounded square 3");
    e.select(1);
    EXPECT_FALSE(e.removeShape());
    EXPECT_EQ(e.selection(), 1);
    EXPECT_EQ(e.name, u"rounded square 1");
    ShapesEdition one({{u"only", u"m 0 0", 0, 0}}, 0);
    EXPECT_EQ(one.removeShape()->text, u"Cannot remove all shapes from the list");
}

TEST(ShapesEdition, ApplyKeepsAndOkGivesBack)
{
    ShapesEdition e(defaultShapePresets(), 0);
    e.shape = u"m 0 0 l 50 0 50 50";
    e.name = u"square";
    EXPECT_TRUE(e.modified());
    EXPECT_TRUE(e.save().saved());
    EXPECT_FALSE(e.modified());
    EXPECT_EQ(e.presets()[0], (ShapePreset{u"square", u"m 0 0 l 50 0 50 50", 0, 0}));
    // An empty shape is refused; an empty name becomes "Untitled".
    e.shape.clear();
    EXPECT_EQ(e.save().error->text, u"Field \"shape\" cannot be empty.");
    e.shape = u"m 0 0 l 1 1";
    e.name.clear();
    EXPECT_TRUE(e.save().saved());
    EXPECT_EQ(e.name, u"Untitled");
    EXPECT_EQ(e.presets()[0].name, u"Untitled");
}

TEST(ShapesEdition, ARenameByApplyReachesTheList)
{
    // T5-dialog-list-stale (compatibility-decisions.md): legacy's list never
    // took a rename (only Add, Delete and Restore default changed it).
    ShapesEdition e(defaultShapePresets(), 0);
    e.name = u"square";
    EXPECT_TRUE(e.save().saved());
    EXPECT_EQ(e.list()[0], u"square");
    EXPECT_EQ(e.list().size(), 5u);
}

TEST(ShapesEdition, TheSaveChangesQuestionNamesTheShape)
{
    // T5-save-question-text (compatibility-decisions.md): legacy's question
    // showed the shape's drawing text.
    ShapesEdition e(defaultShapePresets(), 0);
    e.shape = u"m 0 0 l 50 0 50 50";
    EXPECT_TRUE(e.modified());
    EXPECT_EQ(e.saveChangesQuestion().text, u"Save changes to shape \"rectangle\"?");
    EXPECT_EQ(e.saveChangesQuestion().title, u"Confirmation");
}

TEST(ShapesEdition, SavingUnderAnExistingNameAsksReplaceOrRename)
{
    // Accepted on #55 (surface-decision-routing.md): legacy kept both.
    ShapesEdition e(defaultShapePresets(), 0);
    e.name = u"Circle";
    e.shape = u"m 0 0 l 9 9";
    const auto clash = e.save();
    ASSERT_TRUE(clash.clash);
    EXPECT_EQ(*clash.clash, u"circle");
    EXPECT_EQ(e.presets()[0].name, u"rectangle"); // nothing kept yet
    EXPECT_TRUE(e.modified());                    // the edit is still unsaved
    // Rename: the user changes the name and saves again.
    e.name = u"ring";
    EXPECT_TRUE(e.save().saved());
    EXPECT_EQ(e.presets()[0].name, u"ring");
    EXPECT_EQ(e.presets().size(), 5u);
    // Replace: the other preset goes, the edit takes its name.
    e.select(4);
    e.name = u"RING";
    ASSERT_TRUE(e.save().clash);
    int pending = 2;
    EXPECT_EQ(e.replaceClash(&pending), 3);
    EXPECT_EQ(pending, 1);
    ASSERT_EQ(e.presets().size(), 4u);
    EXPECT_EQ(e.presets()[3].name, u"RING");
    EXPECT_EQ(e.list()[3], u"RING");
    EXPECT_EQ(e.presets()[0].name, u"circle");
}

TEST(ShapesEdition, GetShapeFromActiveLine)
{
    // OnGetShapeFromLine: an empty shape takes the drawing; otherwise each
    // drawing is a new preset "Untitled" (the mode fields as they were).
    ShapesEdition e(defaultShapePresets(), 0);
    e.mode = 2;
    EXPECT_FALSE(e.getShapeFromLine(u"{\\pos(1,1)}Text"));
    EXPECT_TRUE(e.getShapeFromLine(u"{\\p1}m 0 0 l 5 5{\\p0}text{\\p2}m 1 1 l 2 2"));
    ASSERT_EQ(e.presets().size(), 7u);
    EXPECT_EQ(e.presets()[5], (ShapePreset{u"Untitled", u"m 0 0 l 5 5", 0, 0}));
    EXPECT_EQ(e.presets()[6], (ShapePreset{u"Untitled", u"m 1 1 l 2 2", 0, 0}));
    EXPECT_EQ(e.selection(), 6);
    EXPECT_EQ(e.name, u"Untitled");
    EXPECT_EQ(e.shape, u"m 1 1 l 2 2");
    EXPECT_EQ(e.mode, 2);
    ShapesEdition blank(defaultShapePresets(), 0);
    EXPECT_FALSE(blank.addShape(u"new"));
    EXPECT_TRUE(blank.getShapeFromLine(u"{\\p1}m 0 0 l 3 3"));
    EXPECT_EQ(blank.presets().size(), 6u);
    EXPECT_EQ(blank.shape, u"m 0 0 l 3 3");
    EXPECT_FALSE(blank.modified()); // legacy set currentShape too: unsaved, yet not "modified"
    EXPECT_TRUE(blank.presets()[5].shape.empty());
}

TEST(ShapesEdition, RestoreDefaultKeepsTheSelectedNamesPlace)
{
    std::vector<ShapePreset> mine = {{u"star", u"m 0 0", 0, 0}, {u"circle", u"m 1 1", 1, 0}};
    ShapesEdition e(mine, 1);
    e.restoreDefaults(defaultShapePresets());
    EXPECT_EQ(e.presets().size(), 5u);
    EXPECT_EQ(e.selection(), 1); // "circle" is listed: its place
    EXPECT_EQ(e.name, u"circle");
    ShapesEdition f(mine, 0);
    f.restoreDefaults(defaultShapePresets());
    EXPECT_EQ(f.selection(), 0); // "star" is not: the first
    EXPECT_EQ(ShapesEdition::restoreQuestion().text, u"Are you sure you want to reset to default?");
}

// --- The tool: gestures, options, the overlay --------------------------------

namespace {

constexpr std::string_view kScript = "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
                                     "[Events]\n"
                                     "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an7\\pos(150,150)\\p1}m 0 0 l 300 0 300 300\n"
                                     "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an7\\pos(300,300)}\n";

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

TEST(DrawingGesture, ADragIsOneStepAndEscRestores)
{
    TestHost host;
    setUp(host);
    DrawingTool tool;
    host.tool = &tool;
    tool.editor().mode = VectorEditor::Drag;
    tool.reset(host);
    const auto id = ids(*host.s)[0];
    const std::size_t steps = host.s->historySize();
    tool.pointer(at(Pointer::Kind::Press, 150, 50, true, Pointer::Button::Left), host);
    for (int i = 1; i <= 10; ++i)
        tool.pointer(at(Pointer::Kind::Move, 150 + i, 50 + i, true), host);
    EXPECT_EQ(line(*host.s, id)->text, u8"{\\an7\\pos(150,150)\\p1}m 0 0 l 300 0 300 300"); // nothing reaches the Document
    ASSERT_TRUE(host.g);
    EXPECT_EQ(host.g->staged(id), std::optional<std::u8string>(u8"{\\an7\\pos(150,150)\\p1}m 0 0 l 330 30 300 300{\\p0}"));
    tool.pointer(at(Pointer::Kind::Release, 160, 60, false, Pointer::Button::Left), host);
    EXPECT_EQ(line(*host.s, id)->text, u8"{\\an7\\pos(150,150)\\p1}m 0 0 l 330 30 300 300{\\p0}");
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(host.s->history().back().name, "Visual vector drawing tool");
    // Esc during a drag: the Document and the points as before it.
    tool.pointer(at(Pointer::Kind::Press, 160, 60, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 200, 100, true), host);
    ASSERT_TRUE(host.g);
    host.cancelGesture();
    tool.pointer(at(Pointer::Kind::Move, 210, 110, true), host);
    tool.pointer(at(Pointer::Kind::Release, 210, 110, false, Pointer::Button::Left), host);
    EXPECT_EQ(line(*host.s, id)->text, u8"{\\an7\\pos(150,150)\\p1}m 0 0 l 330 30 300 300{\\p0}");
    EXPECT_EQ(host.s->historySize(), steps + 1);
    EXPECT_EQ(tool.editor().points[1].x, 330);
}

TEST(DrawingGesture, AShapeDragIsOneStepAndAProtectedSessionRefusesIt)
{
    TestHost host;
    setUp(host);
    DrawingTool tool;
    host.tool = &tool;
    const auto ids_ = ids(*host.s);
    host.s->setSelection({ids_[1], {ids_[1]}, ids_[1], std::nullopt});
    tool.reset(host);
    EXPECT_TRUE(tool.setOption("shape", 1, host)); // rectangle
    EXPECT_EQ(tool.shape(), 0);
    const std::size_t steps = host.s->historySize();
    tool.pointer(at(Pointer::Kind::Press, 100, 100, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 200, 150, true), host);
    tool.pointer(at(Pointer::Kind::Release, 200, 150, false, Pointer::Button::Left), host);
    EXPECT_EQ(line(*host.s, ids_[1])->text, u8"{\\p1\\an7\\pos(300,300)}m 1 1 l 301 1 301 151 1 151{\\p0}");
    EXPECT_EQ(host.s->historySize(), steps + 1);
    // The rectangle drawn over the video (Shapes::DrawVisual).
    const Overlay o = tool.overlay(host);
    ASSERT_EQ(o.lines.size(), 4u);
    EXPECT_EQ(o.lines[0].from, (PointF{100, 100}));
    EXPECT_EQ(o.lines[1].from, (PointF{200, 100}));
    EXPECT_EQ(o.lines[2].from, (PointF{200, 150}));
    EXPECT_EQ(o.lines[0].argb, 0xFFBB0000u);
    // A rectangle released without width or height writes nothing and
    // leaves no preview.
    tool.pointer(at(Pointer::Kind::Press, 400, 300, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 400, 320, true), host);
    EXPECT_TRUE(host.g);
    tool.pointer(at(Pointer::Kind::Release, 400, 320, false, Pointer::Button::Left), host);
    EXPECT_FALSE(host.g);
    EXPECT_FALSE(tool.rectangleVisible());
    EXPECT_EQ(host.s->historySize(), steps + 1);

    TestHost ref;
    setUp(ref, true);
    DrawingTool refTool;
    ref.tool = &refTool;
    refTool.reset(ref);
    refTool.setOption("shape", 1, ref);
    const std::size_t refSteps = ref.s->historySize();
    refTool.pointer(at(Pointer::Kind::Press, 100, 100, true, Pointer::Button::Left), ref);
    refTool.pointer(at(Pointer::Kind::Move, 200, 150, true), ref);
    refTool.pointer(at(Pointer::Kind::Release, 200, 150, false, Pointer::Button::Left), ref);
    EXPECT_EQ(ref.s->historySize(), refSteps);
    EXPECT_FALSE(ref.g);
}

TEST(DrawingOptions, SixModesAndTheShapeList)
{
    TestHost host;
    setUp(host);
    DrawingTool tool;
    tool.reset(host);
    auto options = tool.options(host);
    ASSERT_EQ(options.size(), 7u);
    const char *roles[] = {"vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete"};
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(options[i].iconRole, roles[i]);
        EXPECT_TRUE(options[i].enabled);
        EXPECT_EQ(options[i].checked, i == 1); // Add line (VectorItem::toggled = 1)
    }
    EXPECT_EQ(options[6].kind, ToolOption::Kind::Choice);
    EXPECT_EQ(options[6].iconRole, "shape-presets"); // the row shows it as an icon with its menu
    EXPECT_EQ(options[6].choices.size(), 7u);
    EXPECT_EQ(options[6].index, 0);
    // A shape chosen: the modes take no click and show none pushed
    // (VectorItem::OnMouseEvent / OnPaint with shapeListSelection).
    EXPECT_TRUE(tool.setOption("shape", 2, host));
    options = tool.options(host);
    for (int i = 0; i < 6; ++i) {
        EXPECT_FALSE(options[i].enabled);
        EXPECT_FALSE(options[i].checked);
    }
    EXPECT_EQ(options[6].index, 2);
    EXPECT_FALSE(tool.setOption("mode0", 1, host));
    // The wheel cycles the six modes (no Invert clip button).
    tool.setOption("shape", 0, host);
    tool.editor().mode = 0;
    Pointer wheel;
    wheel.kind = Pointer::Kind::Wheel;
    wheel.wheelSteps = 1;
    tool.pointer(wheel, host);
    EXPECT_EQ(tool.editor().mode, 5);
}

TEST(DrawingTool, MoveFollowsTheVideosTime)
{
    TestHost host;
    host.s = std::make_unique<EditSession>(
        load("[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n[Events]\n"
             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an7\\move(300,300,900,600)\\p1}m 0 0 l 100 0\n"));
    const auto id = ids(*host.s)[0];
    host.s->setSelection({id, {id}, id, std::nullopt});
    host.v.setClient(640, 360, 0);
    host.v.setScript(1920, 1080);
    host.v.open({1280, 720, 0, 1});
    DrawingTool tool;
    host.timeMs = 1000;
    tool.reset(host);
    EXPECT_EQ(tool.position(), (PointF{300, 300}));
    host.timeMs = 1500;
    (void)tool.overlay(host);
    EXPECT_EQ(tool.position(), (PointF{600, 450}));
    host.timeMs = 2500;
    (void)tool.overlay(host);
    EXPECT_EQ(tool.position(), (PointF{900, 600}));
}

TEST(DrawingDeparture, ChangingScaleKeepsTheDrawingInPlace)
{
    // T5-change-scale-garble (compatibility-decisions.md): when "Changing
    // scale" adds \fscx / \fscy, legacy's Shapes::SetScale diff moved the
    // drawing's place the wrong way and the next sample wrote the drawing
    // into the tags ("{\fscy12m 1 1 l 301 1 301 181 1 1810)}m 1 1 ...").
    TestHost host;
    host.s = std::make_unique<EditSession>(
        load("[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n[Events]\n"
             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an7\\pos(300,300)}Text\n"));
    const auto id = ids(*host.s)[0];
    host.s->setSelection({id, {id}, id, std::nullopt});
    host.v.setClient(640, 360, 0);
    host.v.setScript(1920, 1080);
    host.v.open({1280, 720, 0, 1});
    host.shapesFile = u"Shape: scaled; m 0 0 l 100 0 100 100 0 100; 0; 1\n";
    DrawingTool tool;
    host.tool = &tool;
    tool.reset(host);
    ASSERT_TRUE(tool.setOption("shape", 1, host));
    const std::size_t steps = host.s->historySize();
    tool.pointer(at(Pointer::Kind::Press, 100, 100, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 150, 140, true), host);
    ASSERT_TRUE(host.g);
    EXPECT_EQ(*host.g->staged(id, false),
              u8"{\\fscy120\\fscx150\\p1\\an7\\pos(300,300)}m 1 1 l 151 1 151 121 1 121{\\p0}{Text}");
    tool.pointer(at(Pointer::Kind::Move, 200, 160, true), host);
    tool.pointer(at(Pointer::Kind::Release, 200, 160, false, Pointer::Button::Left), host);
    EXPECT_EQ(line(*host.s, id)->text,
              u8"{\\fscy120\\fscx150\\p1\\an7\\pos(300,300)}m 1 1 l 301 1 301 181 1 181{\\p0}{Text}");
    EXPECT_EQ(host.s->historySize(), steps + 1);
}
