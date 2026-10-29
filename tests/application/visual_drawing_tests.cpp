// T5: the drawing tool and its shape presets. The tool replays the legacy
// probe's captures (legacy_drawing_capture over
// tests/fixtures/legacy-cases/inputs/drawing-cases.txt; tests/fixtures/
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

#include "legacy_visual_keys.h"

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
            Key key = legacy_keys::legacyKey(k, flags.find("shift") != std::string::npos,
                                         flags.find("ctrl") != std::string::npos,
                                         flags.find("alt") != std::string::npos, tool.family());
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

// Approved departures (docs/compatibility-decisions.md) where a dump's
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

// --- The "Edit shapes" window (PU5) -----------------------------------------

TEST(ShapesEdition, OpensOnThePresetsIndex)
{
    // A preset's index (the controller gives the shape list's selection
    // less its "Choose", T5-editor-opens-next); out of range the first.
    EXPECT_EQ(ShapesEdition(defaultShapePresets(), 2).current().name, u"rounded square 1");
    EXPECT_EQ(ShapesEdition(defaultShapePresets(), 5).current().name, u"rectangle");
    EXPECT_EQ(ShapesEdition(defaultShapePresets(), -1).selection(), 0);
    const ShapesEdition empty({}, 0);
    EXPECT_TRUE(empty.current().name.empty());
    EXPECT_FALSE(empty.canUndo());
}

TEST(ShapesEdition, EachChangeIsKeptAndUndoneInSteps)
{
    ShapesEdition e(defaultShapePresets(), 2);
    // Typing a name is one step however many keys it takes.
    EXPECT_EQ(e.setName(u"r"), ShapesEdition::NameCheck::Ok);
    EXPECT_EQ(e.setName(u"ro"), ShapesEdition::NameCheck::Ok);
    EXPECT_EQ(e.setName(u"round"), ShapesEdition::NameCheck::Ok);
    EXPECT_EQ(e.presets()[2].name, u"round"); // kept at once
    EXPECT_EQ(e.undoStep(), ShapesEdition::Rename);
    e.endTyping();
    EXPECT_TRUE(e.setShape(u"m 0 0 l 10 0\n10 10"));
    EXPECT_EQ(e.current().shape, u"m 0 0 l 10 0 10 10"); // a line break is a space in the file's one line
    e.setMode(ShapePreset::OnlyScaleX);
    EXPECT_EQ(e.current().mode, ShapePreset::OnlyScaleX);
    EXPECT_EQ(e.undoStep(), ShapesEdition::ChangeScaling);

    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.current().mode, ShapePreset::BothScaleX);
    EXPECT_EQ(e.redoStep(), ShapesEdition::ChangeScaling);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.current().shape, defaultShapePresets()[2].shape);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.current().name, u"rounded square 1");
    EXPECT_FALSE(e.canUndo());
    EXPECT_TRUE(e.redo());
    EXPECT_EQ(e.current().name, u"round");
    // A change after an undo drops what Redo had.
    e.setMode(ShapePreset::SeparateScaleXY);
    EXPECT_FALSE(e.canRedo());
    // Another preset chosen ends the typing: its name is a step of its own.
    e.select(0);
    EXPECT_EQ(e.setName(u"square"), ShapesEdition::NameCheck::Ok);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.presets()[0].name, u"rectangle");
    EXPECT_EQ(e.presets()[2].name, u"round");
}

TEST(ShapesEdition, AValueTheToolCannotUseIsRefusedAndTheLastKept)
{
    ShapesEdition e(defaultShapePresets(), 1);
    std::u16string taken;
    EXPECT_EQ(e.setName(u""), ShapesEdition::NameCheck::Empty);
    EXPECT_EQ(e.setName(u"  "), ShapesEdition::NameCheck::Empty);
    EXPECT_EQ(e.setName(u"a;b"), ShapesEdition::NameCheck::Separator); // the file's separator
    EXPECT_EQ(e.setName(u"RECTANGLE", &taken), ShapesEdition::NameCheck::Taken); // ignoring case
    EXPECT_EQ(taken, u"rectangle");
    EXPECT_EQ(e.setName(u"Circle"), ShapesEdition::NameCheck::Ok); // its own name, another case
    EXPECT_FALSE(e.setShape(u""));
    EXPECT_FALSE(e.setShape(u" \n "));
    EXPECT_FALSE(e.setShape(u"m 0 0; l 1 1"));
    EXPECT_EQ(e.current().shape, defaultShapePresets()[1].shape);
    e.setMode(7);
    EXPECT_EQ(e.current().mode, ShapePreset::BothScaleX);
    EXPECT_EQ(e.undoStep(), ShapesEdition::Rename); // only the rename was a step
}

TEST(ShapesEdition, NewDuplicateAndDelete)
{
    ShapesEdition e(defaultShapePresets(), 0);
    // New shape: from the active Line's drawing, else a square.
    e.addShape(u"{\\p1}m 0 0 l 5 5{\\p0}");
    EXPECT_EQ(e.selection(), 5);
    EXPECT_EQ(e.current().name, u"New shape");
    EXPECT_EQ(e.current().shape, u"m 0 0 l 5 5");
    e.addShape(u"no drawing");
    EXPECT_EQ(e.current().name, u"New shape 2");
    EXPECT_EQ(e.current().shape, u"m 0 0 l 100 0 100 100 0 100");
    EXPECT_EQ(e.undoStep(), ShapesEdition::NewShape);
    // Duplicate: after it, "copy", cut to the 20 characters.
    e.select(2);
    e.duplicate();
    EXPECT_EQ(e.selection(), 3);
    EXPECT_EQ(e.current().name, u"rounded square copy"); // its blank end dropped
    EXPECT_EQ(e.current().shape, e.presets()[2].shape);
    e.select(2);
    e.duplicate();
    EXPECT_EQ(e.current().name, u"rounded squar copy 2");
    // Delete: no question, the next one selected; never the last one.
    EXPECT_TRUE(e.remove());
    EXPECT_EQ(e.current().name, u"rounded square copy");
    EXPECT_EQ(e.undoStep(), ShapesEdition::DeleteShape);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.current().name, u"rounded squar copy 2");
    ShapesEdition one({{u"only", u"m 0 0", 0, 0}}, 0);
    EXPECT_FALSE(one.canRemove());
    EXPECT_FALSE(one.remove());
    EXPECT_EQ(one.presets().size(), 1u);
}

TEST(ShapesEdition, ATakenNameIsReplacedInOneStep)
{
    ShapesEdition e(defaultShapePresets(), 3);
    std::u16string taken;
    EXPECT_EQ(e.setName(u"Circle", &taken), ShapesEdition::NameCheck::Taken);
    e.replaceNamed(u"Circle");
    ASSERT_EQ(e.presets().size(), 4u);
    EXPECT_EQ(e.selection(), 2); // the removed one was before it
    EXPECT_EQ(e.current().name, u"Circle");
    EXPECT_EQ(e.current().shape, defaultShapePresets()[3].shape);
    EXPECT_EQ(e.undoStep(), ShapesEdition::ReplaceShape);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.presets(), defaultShapePresets());
    EXPECT_EQ(e.selection(), 3);
}

TEST(ShapesEdition, RenamesFollowUndoRedoAndUndoAll)
{
    // The toolbar's chosen preset follows the history's renames (Codex on
    // #349): a name goes through each in turn.
    const auto through = [](std::u16string name, const Renames &renames) {
        for (const Renamed &r : renames)
            if (name == r.from)
                name = r.to;
        return name;
    };
    const auto opened = defaultShapePresets();
    ShapesEdition e(opened, 3);
    e.replaceNamed(u"Circle");
    EXPECT_EQ(through(opened[3].name, e.renamed()), u"Circle");
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(through(u"Circle", e.renamed()), opened[3].name);
    ASSERT_TRUE(e.redo());
    EXPECT_EQ(through(opened[3].name, e.renamed()), u"Circle");
    // Undo all after the Replace and another change to the same preset.
    ASSERT_TRUE(e.setShape(u"m 0 0 l 9 9"));
    EXPECT_TRUE(e.renamed().empty());
    ASSERT_TRUE(e.undoAll());
    EXPECT_EQ(through(u"Circle", e.renamed()), opened[3].name);
    // Typing a name is one step: its Undo goes back through every key.
    ASSERT_TRUE(e.undo());
    e.select(1);
    const std::u16string before = e.current().name;
    ASSERT_EQ(e.setName(u"r"), ShapesEdition::NameCheck::Ok);
    ASSERT_EQ(e.setName(u"ring"), ShapesEdition::NameCheck::Ok);
    EXPECT_EQ(through(u"r", e.renamed()), u"ring");
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(through(u"ring", e.renamed()), before);
    // Other steps rename nothing.
    ASSERT_TRUE(e.remove());
    EXPECT_TRUE(e.renamed().empty());
    // An action that changes nothing is no step: the revision the editor
    // hands changes on stays, so the last renames are not handed on again.
    ShapesEdition same(opened, 3);
    same.replaceNamed(u"Circle");
    ASSERT_TRUE(same.undo());
    const auto revision = same.revision();
    same.restoreDefaults(opened);
    EXPECT_EQ(same.revision(), revision);
    // Delete selects the next one, the last when it was the last (Codex on
    // #349: worked out before the list moves).
    same.select(static_cast<int>(opened.size()) - 1);
    ASSERT_TRUE(same.remove());
    EXPECT_EQ(same.selection(), static_cast<int>(opened.size()) - 2);
}

TEST(ShapesEdition, RedoBringsBackTheStepsOwnSelection)
{
    // Another preset chosen after a step does not change what Redo selects
    // (Codex on #349).
    ShapesEdition e(defaultShapePresets(), 2);
    e.duplicate();
    ASSERT_EQ(e.selection(), 3);
    e.select(0);
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.selection(), 2);
    e.select(4);
    ASSERT_TRUE(e.redo());
    EXPECT_EQ(e.selection(), 3);
    EXPECT_EQ(e.current().name, u"rounded square copy");
    e.select(1);
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.selection(), 2);
}

TEST(ShapesEdition, TakeFromTheActiveLineReplacesTheDrawing)
{
    ShapesEdition e(defaultShapePresets(), 0);
    EXPECT_FALSE(e.takeFromLine(u"no drawing"));
    EXPECT_FALSE(e.canUndo());
    // The last drawing in the Line (legacy's loop left the field with it).
    EXPECT_TRUE(e.takeFromLine(u"{\\p1}m 0 0 l 1 1{\\p0}x{\\p2}m 5 5 l 6 6"));
    EXPECT_EQ(e.presets().size(), 5u);
    EXPECT_EQ(e.current().name, u"rectangle");
    EXPECT_EQ(e.current().shape, u"m 5 5 l 6 6");
    EXPECT_EQ(e.undoStep(), ShapesEdition::TakeFromLine);
}

TEST(ShapesEdition, RestoreDefaultsAndUndoAllAreOneStep)
{
    std::vector<ShapePreset> mine = defaultShapePresets();
    mine.insert(mine.begin(), {u"star", u"m 0 0 l 1 1", 1, 0});
    ShapesEdition e(mine, 2); // circle
    e.setName(u"ring");
    e.endTyping();
    e.select(0);
    e.setMode(ShapePreset::OnlyScaleX);
    e.select(3); // rounded square 1
    // Restore: legacy's five, the selected name's place where the list has
    // it, else where it is, else the first.
    e.restoreDefaults(defaultShapePresets());
    EXPECT_EQ(e.presets(), defaultShapePresets());
    EXPECT_EQ(e.current().name, u"rounded square 1");
    EXPECT_EQ(e.undoStep(), ShapesEdition::RestoreDefaults);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.presets()[0].name, u"star");
    // Undo all changes: the presets the window opened with, one step.
    EXPECT_TRUE(e.undoAll());
    EXPECT_EQ(e.presets(), mine);
    EXPECT_EQ(e.undoStep(), ShapesEdition::UndoAll);
    EXPECT_FALSE(e.undoAll()); // nothing differs
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.presets()[2].name, u"ring");
    // Defaults the same as the presets: no step.
    ShapesEdition same(defaultShapePresets(), 0);
    same.restoreDefaults(defaultShapePresets());
    EXPECT_FALSE(same.canUndo());
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

// PU5: "Edit shapes" keeps each change at once, so the tool reads the
// chosen preset again even at the place chosen before (legacy's SetShape
// kept the one it had; its edits came only at OK).
TEST(DrawingOptions, ChangedPresetsAreReadAgain)
{
    TestHost host;
    setUp(host);
    DrawingTool tool;
    tool.reset(host);
    ASSERT_TRUE(tool.setOption("shape", 1, host)); // rectangle
    const std::size_t rectangle = tool.shapePoints().size();
    ASSERT_NE(rectangle, 3u);
    host.presets[0].shape = u"m 0 0 l 100 0 0 100";
    tool.setOption("shape", 1, host); // legacy's SetShape: the same place, nothing read
    EXPECT_EQ(tool.shapePoints().size(), rectangle);
    const int changes = host.changes;
    tool.presetsChanged(1, host);
    EXPECT_EQ(tool.shape(), 0);
    EXPECT_EQ(tool.shapeSelection(), 1);
    EXPECT_EQ(tool.shapePoints().size(), 3u);
    EXPECT_EQ(host.changes, changes + 1);
    // Free drawing stays so.
    tool.setOption("shape", 0, host);
    tool.presetsChanged(0, host);
    EXPECT_EQ(tool.shape(), -1);
    // A hidden tool (another family shown) takes them too, quietly; it
    // reads its Line when it comes back (Codex on #349).
    ASSERT_TRUE(tool.setOption("shape", 1, host));
    host.presets[0].shape = u"m 0 0 l 100 0 100 100 0 100 0 50";
    const int hidden = host.changes;
    tool.presetsChanged(1, host, false);
    EXPECT_EQ(tool.shapePoints().size(), 5u);
    EXPECT_EQ(host.changes, hidden);
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

// The pointer's shapes (VideoBox::SetCursor from Shapes::OnMouseEvent,
// VisualDrawingShapes.cpp:384-398; the user's 2026-10-13 decision to port
// them). The drawing's points set none (DrawingAndClip never calls it).
TEST(DrawingPointer, TheShapesRectangleEdgesAndCornersShowTheirResizeShapes)
{
    TestHost host;
    setUp(host);
    DrawingTool tool;
    host.tool = &tool;
    const auto rows = ids(*host.s);
    host.s->setSelection({rows[1], {rows[1]}, rows[1], std::nullopt});
    tool.reset(host);
    ASSERT_TRUE(tool.setOption("shape", 1, host)); // rectangle
    tool.pointer(at(Pointer::Kind::Move, 100, 100), host);
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow); // nothing drawn yet
    tool.pointer(at(Pointer::Kind::Press, 100, 100, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 200, 150, true), host);
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow); // a new rectangle's drag sets none
    tool.pointer(at(Pointer::Kind::Release, 200, 150, false, Pointer::Button::Left), host);
    ASSERT_TRUE(tool.rectangleVisible());
    EXPECT_EQ(host.pointerShape(), PointerShape::SizeNWSE); // released on its corner
    const struct {
        int x, y;
        PointerShape shape;
    } hovers[] = {
        {100, 125, PointerShape::SizeWE},   {200, 125, PointerShape::SizeWE},   {150, 100, PointerShape::SizeNS},
        {150, 150, PointerShape::SizeNS},   {100, 100, PointerShape::SizeNWSE}, {200, 150, PointerShape::SizeNWSE},
        {200, 100, PointerShape::SizeNESW}, {100, 150, PointerShape::SizeNESW}, {150, 125, PointerShape::Arrow},
        {400, 300, PointerShape::Arrow},
    };
    for (const auto &h : hovers) {
        tool.pointer(at(Pointer::Kind::Move, h.x, h.y), host);
        EXPECT_EQ(host.pointerShape(), h.shape) << h.x << "," << h.y;
        tool.pointer(at(Pointer::Kind::Move, 150, 125), host);
        EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    }
    // The right edge's drag keeps west-east.
    tool.pointer(at(Pointer::Kind::Move, 200, 125), host);
    tool.pointer(at(Pointer::Kind::Press, 200, 125, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 260, 40, true), host);
    EXPECT_EQ(host.pointerShape(), PointerShape::SizeWE);
    tool.pointer(at(Pointer::Kind::Release, 260, 40, false, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 150, 125), host);
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
}

TEST(DrawingPointer, TheDrawingsPointsSetNoShape)
{
    TestHost host;
    setUp(host);
    DrawingTool tool;
    host.tool = &tool;
    tool.editor().mode = VectorEditor::Drag;
    tool.reset(host);
    tool.pointer(at(Pointer::Kind::Move, 150, 50), host);
    tool.pointer(at(Pointer::Kind::Press, 150, 50, true, Pointer::Button::Left), host);
    tool.pointer(at(Pointer::Kind::Move, 160, 60, true), host);
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
    tool.pointer(at(Pointer::Kind::Release, 160, 60, false, Pointer::Button::Left), host);
    EXPECT_EQ(host.pointerShape(), PointerShape::Arrow);
}
