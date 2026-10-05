// T2: the Position and Move families. The replay runs the legacy probe's
// cases (tools/legacy-capture/position_capture.cpp over
// inputs/position-cases.txt; tests/fixtures/legacy-observations/
// local-t2-position-20261005) through visual::PositionTool and
// visual::MoveTool and must give legacy's texts, history, log, positions and
// handles exactly. A key in a case is a press and its release here: legacy
// committed on the press, the transaction rule (#55) on the release, so the
// texts and steps after each key agree. The other tests cover what the
// probe does not: the gesture transaction (Esc, one step per held nudge),
// Qt's double click order, the rail's options and the numeric values.

#include "hikari/application/automation_services.h"
#include "hikari/application/visual_position.h"
#include "hikari/application/visual_script.h"
#include "hikari/application/visual_tools.h"
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
#include <cstdlib>
#include <cstring>
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

std::u16string u16(const QJsonValue &v)
{
    return v.toString().toStdU16String();
}

std::u16string u16(std::string_view s)
{
    return std::u16string(s.begin(), s.end());
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

// The probe's GetLineTextExtents stand-in (position_capture.cpp), the same
// float and double steps: half the size a character, the height 1.25 times
// the size, descent 0.25, external leading 0.125, scaled by ScaleX/ScaleY.
class ProbeMeasure : public TextMeasurePort {
public:
    std::optional<TextExtents> measure(const std::vector<std::string> &style, const std::string &text) override
    {
        auto number = [](const std::string &s) {
            char *end = nullptr;
            const double v = std::strtod(s.c_str(), &end);
            return (end && *end == '\0' && !s.empty()) ? v : static_cast<double>(std::atoi(s.c_str()));
        };
        const float fontsize = static_cast<float>(number(style[2]) * 64.f);
        const float spacing = static_cast<float>(std::atof(style[13].c_str()) * 64.f);
        const std::size_t len = core::toUtf16(std::u8string(text.begin(), text.end())).size();
        if (!len)
            return TextExtents{};
        float fwidth = 0;
        if (spacing != 0) {
            for (std::size_t i = 0; i < len; i++)
                fwidth += fontsize * 0.5f + spacing;
        } else {
            fwidth = fontsize * 0.5f * static_cast<float>(len);
        }
        if (style[7] != "0")
            fwidth *= 1.25f;
        const float fheight = fontsize * 1.25f;
        const float fdescent = fontsize * 0.25f;
        const float fextlead = fontsize * 0.125f;
        const float scalex = static_cast<float>(std::atof(style[11].c_str()) / 100.f);
        const float scaley = static_cast<float>(std::atof(style[12].c_str()) / 100.f);
        return TextExtents{scalex * (fwidth / 64.f), scaley * (fheight / 64.f), scaley * (fdescent / 64.f),
                           scaley * (fextlead / 64.f)};
    }
};

// A VisualHost over a VideoView and an EditSession. As the controller does
// (the shell refreshes after an edit, VisualToolsController::refresh), a
// commit sets the tool again, as legacy's SetModified did (ShowEditOnVideo:
// SetVisual).
class TestHost : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    BatchPicker picker;
    VisualTool *tool = nullptr;
    int time = 0;
    LegacyTimebase tb;
    ProbeMeasure probeMeasure;
    std::vector<std::u16string> logs;
    int commits = 0;

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
        const bool changed = g->hasChanges();
        g.reset();
        ++commits;
        if (changed && tool)
            tool->reset(*this);
        return r;
    }
    void cancelGesture() override { g.reset(); }
    std::pair<int, int> measureLabel(std::u16string_view text) const override
    {
        return {7 * static_cast<int>(text.size()), 14};
    }
    void toolChanged() override {}
    std::int64_t videoTimeMs() const override { return time; }
    LegacyTimebase timebase() const override { return tb; }
    TextMeasurePort *textMeasure() const override { return const_cast<ProbeMeasure *>(&probeMeasure); }
    void log(std::u16string_view text) override { logs.emplace_back(text); }
    // Esc in the panel (VisualToolsController::escape).
    void escape()
    {
        g.reset();
        if (tool)
            tool->reset(*this);
    }
    // The events the host lets through (Visuals::Draw's blockevents).
    bool blocked() const
    {
        if (!tool)
            return true;
        if (const auto own = tool->warning(*this))
            return *own != LineWarning::None;
        const auto active = activeLine();
        if (!active)
            return false;
        const auto draft = s->draftRecord();
        const core::LineRecord *line = (draft && draft->id == *active) ? &*draft : lineOf(*s, *active);
        return lineWarning(tool->family(), *line, time) != LineWarning::None;
    }
};

std::string assTime(int ms)
{
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d:%02d.%02d", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, (ms % 1000) / 10);
    return b;
}

// One case of inputs/position-cases.txt.
struct ProbeCase {
    std::string name;
    int clientW = 0, clientH = 0, panel = 0;
    SourceGeometry frame;
    std::vector<std::string> zoomOps;
    IntRect view;
    PointF zoomMove, zoomScale;
    int scriptW = 0, scriptH = 0;
    std::vector<int> timecodes;
    int time = 0;
    std::vector<std::string> styles;
    struct Line {
        int start = 0, end = 0, ml = 0, mr = 0, mv = 0;
        bool comment = false;
        std::string style, text, translation;
        bool hasTranslation = false;
    };
    std::vector<Line> lines;
    int active = 0;
    std::vector<int> select;
    std::vector<std::string> steps; // tool, option, mouse, key, time, dump
};

std::vector<ProbeCase> readCases()
{
    std::ifstream in(HIKARI_POSITION_CASES);
    std::vector<ProbeCase> out;
    std::string text;
    ProbeCase c;
    while (std::getline(in, text)) {
        if (text.empty() || text[0] == '#')
            continue;
        std::istringstream line(text);
        std::string op;
        line >> op;
        if (op == "case") {
            c = ProbeCase{};
            line >> c.name;
        } else if (op == "client") {
            line >> c.clientW >> c.clientH >> c.panel;
        } else if (op == "frame") {
            line >> c.frame.width >> c.frame.height >> c.frame.sarNum >> c.frame.sarDen;
        } else if (op == "zoomat") {
            c.zoomOps.push_back(text);
        } else if (op == "view") {
            line >> c.view.left >> c.view.top >> c.view.right >> c.view.bottom >> c.zoomMove.x >> c.zoomMove.y >>
                c.zoomScale.x >> c.zoomScale.y;
        } else if (op == "script") {
            line >> c.scriptW >> c.scriptH;
        } else if (op == "timecodes") {
            int ms;
            while (line >> ms)
                c.timecodes.push_back(ms);
        } else if (op == "time" && c.steps.empty()) {
            line >> c.time;
        } else if (op == "style") {
            std::string rest;
            std::getline(line >> std::ws, rest);
            c.styles.push_back(rest);
        } else if (op == "line") {
            ProbeCase::Line l;
            int comment = 0;
            line >> l.start >> l.end >> l.style >> l.ml >> l.mr >> l.mv >> comment;
            l.comment = comment != 0;
            std::string rest;
            std::getline(line >> std::ws, rest);
            const auto bar = rest.find('|');
            l.text = rest.substr(0, bar);
            if (bar != std::string::npos) {
                l.translation = rest.substr(bar + 1);
                l.hasTranslation = true;
            }
            c.lines.push_back(l);
        } else if (op == "active") {
            line >> c.active;
        } else if (op == "select") {
            int row;
            while (line >> row)
                c.select.push_back(row);
        } else if (op == "tlmode") {
        } else if (op == "end") {
            out.push_back(c);
        } else {
            c.steps.push_back(text);
        }
    }
    return out;
}

std::vector<QJsonObject> readObservations()
{
    QFile file(QStringLiteral(HIKARI_POSITION_OBSERVATIONS));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    std::vector<QJsonObject> out;
    for (const QByteArray &line : file.readAll().split('\n'))
        if (!line.trimmed().isEmpty())
            out.push_back(QJsonDocument::fromJson(line).object());
    return out;
}

std::unique_ptr<EditSession> sessionFor(const ProbeCase &c)
{
    std::string script = "[Script Info]\nScriptType: v4.00+\nPlayResX: " + std::to_string(c.scriptW) +
                         "\nPlayResY: " + std::to_string(c.scriptH) +
                         "\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                         "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, "
                         "Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n";
    for (const auto &s : c.styles)
        script += "Style: " + s + "\n";
    script += "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    for (const auto &l : c.lines)
        script += std::string(l.comment ? "Comment: " : "Dialogue: ") + "0," + assTime(l.start) + "," +
                  assTime(l.end) + "," + l.style + ",," + std::to_string(l.ml) + "," + std::to_string(l.mr) + "," +
                  std::to_string(l.mv) + ",," + l.text + "\n";
    core::Document document = load(script);
    // Times in whole ms (legacy Start.mstime), beyond ASS's centiseconds,
    // and the translations of a TLMode Document.
    std::size_t i = 0;
    for (const auto *line : document.lines()) {
        const auto &l = c.lines[i++];
        (void)document.editLine(line->id, [&](core::LineRecord &r) {
            r.start.value = core::DocumentTime(static_cast<std::int64_t>(l.start) * 1000);
            r.end.value = core::DocumentTime(static_cast<std::int64_t>(l.end) * 1000);
            r.text = std::u8string(l.text.begin(), l.text.end());
            if (l.hasTranslation)
                r.translation = std::u8string(l.translation.begin(), l.translation.end());
        });
    }
    return std::make_unique<EditSession>(std::move(document));
}

LegacyTimebase timebaseOf(const std::vector<int> &timecodes)
{
    // Timebase::FromTimecodes(timecodes, 0): the frame rate from the span.
    const float fps = timecodes.size() > 1 && timecodes.back() > timecodes.front()
                          ? 1000.f * (timecodes.size() - 1) / (timecodes.back() - timecodes.front())
                          : 0.f;
    return LegacyTimebase(timecodes, fps);
}

struct Draw {
    std::string kind;
    float x = 0, y = 0, x2 = 0, y2 = 0, size = 0;
    std::uint32_t color = 0;
    auto operator<=>(const Draw &) const = default;
};
[[maybe_unused]] void PrintTo(const Draw &d, std::ostream *os)
{
    *os << d.kind << "(" << d.x << "," << d.y << " " << d.x2 << "," << d.y2 << " size " << d.size << " colour "
        << d.color << ")";
}

// The overlay as the probe records legacy's drawing: squares and arrow
// heads (by their tip and where the line ends), circles, crosses.
std::vector<Draw> draws(const Overlay &o)
{
    std::vector<Draw> out;
    for (const auto &p : o.polygons) {
        // A square by its corners (DrawRect: pos - size to pos + size).
        if (p.points.size() == 4)
            out.push_back({p.fill == kHandleSelectedFill ? "rectsel" : "rect", p.points[0].x, p.points[0].y,
                           p.points[2].x, p.points[2].y, 0, 0});
    }
    for (const auto &c : o.circles)
        if (c.filled)
            out.push_back({c.argb == kHandleSelectedFill ? "circlesel" : "circle", c.centre.x, c.centre.y, 0, 0,
                           c.radius, 0});
    for (std::size_t i = 0; i + 1 < o.lines.size(); i++) {
        const auto &h = o.lines[i];
        const auto &v = o.lines[i + 1];
        // DrawCross: x - 15 to x + 15 at y, then y - 15 to y + 15 at x.
        if (h.from.y == h.to.y && v.from.x == v.to.x && h.from.x == v.from.x - 15.0f && h.to.x == v.from.x + 15.0f &&
            v.from.y == h.from.y - 15.0f && v.to.y == h.from.y + 15.0f && h.argb == v.argb) {
            out.push_back({"cross", v.from.x, h.from.y, 0, 0, 0, h.argb});
            ++i;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Draw> legacyDraws(const QJsonArray &a)
{
    std::vector<Draw> out;
    for (const auto &v : a) {
        const QJsonArray d = v.toArray();
        const std::string kind = d[0].toString().toStdString();
        if (kind == "rect" || kind == "rectsel") {
            const float x = f(d[1]), y = f(d[2]), size = f(d[5]);
            out.push_back({kind, x - size, y - size, x + size, y + size, 0, 0});
        } else if (kind == "circle" || kind == "circlesel") {
            out.push_back({kind, f(d[1]), f(d[2]), 0, 0, f(d[5]), 0});
        }
        else if (kind == "cross")
            out.push_back({kind, f(d[1]), f(d[2]), 0, 0, 0, static_cast<std::uint32_t>(d[6].toDouble())});
    }
    std::sort(out.begin(), out.end());
    return out;
}

Pointer pointerOf(const std::string &step)
{
    std::istringstream in(step);
    std::string op, type;
    Pointer p;
    in >> op >> type >> p.x >> p.y;
    std::string w;
    while (in >> w) {
        if (w == "left")
            p.leftDown = true;
        else if (w == "right")
            p.rightDown = true;
        else if (w == "shift")
            p.shift = true;
        else if (w == "ctrl")
            p.control = true;
        else if (w == "alt")
            p.alt = true;
    }
    using K = Pointer::Kind;
    using B = Pointer::Button;
    if (type == "ldown")
        p.kind = K::Press, p.button = B::Left;
    else if (type == "lup")
        p.kind = K::Release, p.button = B::Left;
    else if (type == "rdown")
        p.kind = K::Press, p.button = B::Right;
    else if (type == "rup")
        p.kind = K::Release, p.button = B::Right;
    else if (type == "mdown")
        p.kind = K::Press, p.button = B::Middle;
    else if (type == "mup")
        p.kind = K::Release, p.button = B::Middle;
    else if (type == "ldclick")
        p.kind = K::DoubleClick, p.button = B::Left; // wxEVT_LEFT_DCLICK in place of the second press
    else
        p.kind = K::Move;
    return p;
}

void setUpView(TestHost &host, const ProbeCase &c)
{
    host.v.setClient(c.clientW, c.clientH, c.panel);
    host.v.setScript(c.scriptW, c.scriptH);
    host.v.open(c.frame);
    for (const auto &op : c.zoomOps) {
        std::istringstream in(op);
        std::string kind;
        float percent;
        int x, y;
        in >> kind >> percent >> x >> y;
        host.v.zoomAt(percent, x, y);
    }
}

} // namespace

TEST(VisualPositionCapture, ReplaysTheLegacyProbe)
{
    const auto cases = readCases();
    const auto observations = readObservations();
    ASSERT_GE(cases.size(), 30u);
    std::size_t next = 0;
    for (const ProbeCase &c : cases) {
        SCOPED_TRACE(c.name);
        TestHost host;
        host.s = sessionFor(c);
        host.tb = timebaseOf(c.timecodes);
        host.time = c.time;
        setUpView(host, c);
        // The probe's window (SizeChanged's rectangle and SetZoom).
        ASSERT_EQ(host.v.videoRect(), c.view);
        ASSERT_EQ(host.v.zoomMove(), c.zoomMove);
        ASSERT_EQ(host.v.zoomScale(), c.zoomScale);
        const auto rows = ids(*host.s);
        ASSERT_EQ(rows.size(), c.lines.size());
        const core::LineId active = rows[static_cast<std::size_t>(c.active)];
        host.s->setSelection({active, {active}, active, std::nullopt});
        if (!c.select.empty()) {
            std::vector<core::LineId> picked;
            for (int r : c.select)
                picked.push_back(rows[static_cast<std::size_t>(r)]);
            host.picker.pick(picked);
        }
        auto rowOf = [&](core::LineId id) {
            return static_cast<int>(std::find(rows.begin(), rows.end(), id) - rows.begin());
        };
        std::unique_ptr<VisualTool> tool;
        int dump = 0;
        for (const std::string &step : c.steps) {
            SCOPED_TRACE(step);
            std::istringstream in(step);
            std::string op;
            in >> op;
            if (op == "tool") {
                std::string family;
                int value = 0;
                in >> family >> value;
                tool = makeVisualTool(family == "move" ? Family::Move : Family::Position);
                host.tool = tool.get();
                tool->selected(host);
                if (auto *p = dynamic_cast<PositionTool *>(tool.get()))
                    p->applyToolValue(value, host); // the toolbar's state before SetVisual
                else
                    tool->setOption("twoPoints", value != -1 ? 1 : 0, host);
                tool->reset(host);
            } else if (op == "option") {
                int value = 0;
                in >> value;
                if (auto *p = dynamic_cast<PositionTool *>(tool.get()))
                    p->applyToolValue(value, host);
                else
                    tool->setOption("twoPoints", value != -1 ? 1 : 0, host);
            } else if (op == "time") {
                in >> host.time;
            } else if (op == "activate") {
                int row = 0;
                in >> row;
                const core::LineId line = rows[static_cast<std::size_t>(row)];
                host.s->setSelection({line, {line}, line, std::nullopt});
                host.picker.clear();
                tool->reset(host);
            } else if (op == "mouse") {
                if (!host.blocked())
                    tool->pointer(pointerOf(step), host);
            } else if (op == "key") {
                std::string k;
                in >> k;
                Key key;
                key.key = k[0];
                std::string w;
                while (in >> w) {
                    key.shift |= w == "shift";
                    key.control |= w == "ctrl";
                    key.alt |= w == "alt";
                }
                // VideoBox::OnKeyPress hands every key to the tool, blocked
                // or not (VideoBox.cpp:666-668).
                (void)tool->key(key, host);
                key.release = true;
                (void)tool->key(key, host);
            } else if (op == "dump") {
                ASSERT_LT(next, observations.size());
                const QJsonObject &o = observations[next++];
                ASSERT_EQ(o[QStringLiteral("case")].toString().toStdString(), c.name);
                ASSERT_EQ(o[QStringLiteral("dump")].toInt(), dump++);
                // The Lines.
                const QJsonArray lines = o[QStringLiteral("lines")].toArray();
                for (std::size_t i = 0; i < rows.size(); i++) {
                    const auto *l = lineOf(*host.s, rows[i]);
                    EXPECT_EQ(core::toUtf16(l->text), u16(lines[static_cast<int>(i)].toArray()[0])) << "row " << i;
                    EXPECT_EQ(core::toUtf16(l->translation), u16(lines[static_cast<int>(i)].toArray()[1])) << "row " << i;
                }
                // One history step where legacy recorded one.
                std::vector<std::string> history;
                const auto steps = host.s->history();
                for (std::size_t i = 1; i < steps.size(); i++)
                    history.push_back(steps[i].name);
                std::vector<std::string> legacyHistory;
                for (const auto &h : o[QStringLiteral("history")].toArray())
                    legacyHistory.push_back(h.toString().toStdString());
                EXPECT_EQ(history, legacyHistory);
                std::vector<std::u16string> legacyLog;
                for (const auto &l : o[QStringLiteral("log")].toArray())
                    legacyLog.push_back(u16(l));
                EXPECT_EQ(host.logs, legacyLog);
                EXPECT_EQ((PointF{host.v.coeffW(), host.v.coeffH()}), pair(o[QStringLiteral("coeff")]));
                if (auto *p = dynamic_cast<PositionTool *>(tool.get())) {
                    const QJsonArray data = o[QStringLiteral("data")].toArray();
                    ASSERT_EQ(p->data().size(), static_cast<std::size_t>(data.size()));
                    for (std::size_t i = 0; i < p->data().size(); i++) {
                        const auto &d = p->data()[i];
                        const QJsonObject l = data[static_cast<int>(i)].toObject();
                        SCOPED_TRACE("data " + std::to_string(i));
                        EXPECT_EQ(rowOf(d.line), l[QStringLiteral("row")].toInt());
                        EXPECT_EQ(d.pos, pair(l[QStringLiteral("pos")]));
                        EXPECT_EQ(d.lastpos, pair(l[QStringLiteral("lastpos")]));
                        const QJsonArray tp = l[QStringLiteral("textPos")].toArray();
                        EXPECT_EQ(static_cast<int>(d.textStart), tp[0].toInt());
                        EXPECT_EQ(static_cast<int>(d.textLength), tp[1].toInt());
                        EXPECT_EQ(d.putInBracket, l[QStringLiteral("bracket")].toBool());
                        EXPECT_EQ(d.move.has_value(), l.contains(QStringLiteral("move")));
                        if (d.move && l.contains(QStringLiteral("move"))) {
                            const QJsonArray m = l[QStringLiteral("move")].toArray();
                            for (int k = 0; k < 4; k++)
                                EXPECT_EQ(static_cast<float>((*d.move)[static_cast<std::size_t>(k)]), f(m[k])) << k;
                        }
                    }
                    const QJsonArray rect = o[QStringLiteral("rect")].toArray();
                    EXPECT_EQ(p->rectangle()[0], pair(rect[0]));
                    EXPECT_EQ(p->rectangle()[1], pair(rect[1]));
                    EXPECT_EQ(p->rectangleVisible(), o[QStringLiteral("rectVisible")].toBool());
                    EXPECT_EQ(p->alignment(), o[QStringLiteral("alignment")].toInt());
                    EXPECT_EQ(p->lineAlignment(), o[QStringLiteral("curLineAlignment")].toInt());
                } else if (auto *m = dynamic_cast<MoveTool *>(tool.get())) {
                    EXPECT_EQ(m->from(), pair(o[QStringLiteral("from")]));
                    EXPECT_EQ(m->to(), pair(o[QStringLiteral("to")]));
                    EXPECT_EQ(m->moveStart(), o[QStringLiteral("moveStart")].toInt());
                    EXPECT_EQ(m->moveEnd(), o[QStringLiteral("moveEnd")].toInt());
                    const QJsonArray two = o[QStringLiteral("twoPoints")].toArray();
                    EXPECT_EQ(m->lineToMoveStart(), pair(two[0]));
                    EXPECT_EQ(m->lineToMoveEnd(), pair(two[1]));
                }
                // The handles, and the warning in their place when blocked.
                const bool blocked = host.blocked();
                EXPECT_EQ(blocked, o[QStringLiteral("blocked")].toBool());
                if (!blocked)
                    EXPECT_EQ(draws(tool->overlay(host)), legacyDraws(o[QStringLiteral("draws")].toArray()));
            }
        }
    }
    EXPECT_EQ(next, observations.size());
}

// ----------------------------------------------------------- unit checks

namespace {

constexpr std::string_view kScript =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
    "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
    "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,20,20,20,1\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\pos(300,300)}x\n"
    "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\move(30,60,90,120,0,2000)}y\n"
    "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,plain\n";

std::vector<int> cfr25()
{
    std::vector<int> out;
    for (int i = 0; i <= 250; i++)
        out.push_back(i * 40);
    return out;
}

// 1920x1080 in a 640x360 rectangle: three script pixels a view pixel.
void standard(TestHost &host, std::size_t activeRow = 0)
{
    host.s = std::make_unique<EditSession>(load(kScript));
    host.tb = timebaseOf(cfr25());
    host.time = 1500;
    host.v.setClient(640, 400, 40);
    host.v.setScript(1920, 1080);
    host.v.open({1280, 720, 0, 1});
    const core::LineId active = ids(*host.s)[activeRow];
    host.s->setSelection({active, {active}, active, std::nullopt});
}

std::u8string text(const TestHost &host, std::size_t row)
{
    return lineOf(*host.s, ids(*host.s)[row])->text;
}

Pointer at(Pointer::Kind kind, int x, int y, Pointer::Button button = Pointer::Button::None, bool left = false)
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

TEST(VisualPositionTags, PositionAndMoveAreReadAsLegacyGetPosition)
{
    TestHost host;
    standard(host);
    const ScriptState state = scriptState(host);
    auto read = [&](std::u8string_view t) {
        core::LineRecord line = *lineOf(*host.s, ids(*host.s)[2]);
        line.text = std::u8string(t);
        return linePosition(state, line, true);
    };
    // \pos: its values and the whole tag's place.
    auto p = read(u8"{\\an7}{\\pos(12.5,-3)}x");
    EXPECT_EQ(p.pos, (PointF{12.5f, -3.f}));
    EXPECT_EQ(p.textStart, 7u);
    EXPECT_EQ(p.textLength, 13u);
    EXPECT_FALSE(p.putInBracket);
    EXPECT_FALSE(p.move);
    // \move with and without times: the end and the times made absolute
    // (the Line's own without them).
    p = read(u8"{\\move(1,2,3,4)}x");
    ASSERT_TRUE(p.move);
    EXPECT_EQ(p.move->values, (std::array<double, 4>{3, 4, 1000, 3000}));
    EXPECT_EQ(p.move->count, 2);
    p = read(u8"{\\move(1,2,3,4,100,900)}x");
    ASSERT_TRUE(p.move);
    EXPECT_EQ(p.move->values, (std::array<double, 4>{3, 4, 1100, 1900}));
    EXPECT_EQ(p.move->count, 4);
    // Malformed: the tag stays where it is and a new one goes first in the
    // first block, at the default position (bottom centre, MarginV 20).
    p = read(u8"{\\pos(abc,2)}x");
    EXPECT_EQ(p.pos, (PointF{960, 1060}));
    EXPECT_EQ(p.textStart, 1u);
    EXPECT_EQ(p.textLength, 0u);
    EXPECT_FALSE(p.putInBracket);
    // No block: a new one.
    p = read(u8"x");
    EXPECT_TRUE(p.putInBracket);
    // Several: the first one.
    p = read(u8"{\\pos(1,2)\\pos(3,4)}x");
    EXPECT_EQ(p.pos, (PointF{1, 2}));
}

TEST(VisualPositionTags, MoveTimesFollowTheVideosFrames)
{
    // GetMoveTimes: from the first frame at or after the start to the last
    // frame before the end, relative to the start truncated to 10 ms.
    TestHost host;
    standard(host);
    core::LineRecord line = *lineOf(*host.s, ids(*host.s)[0]);
    line.start.value = core::DocumentTime(1005000);
    line.end.value = core::DocumentTime(2995000);
    EXPECT_EQ(moveTimes(scriptState(host), line), (std::pair{0, 1960})); // CFR 25: frames 25 (1000) to 74 (2960)
    host.tb = LegacyTimebase({0, 33, 83, 125, 166, 199, 249, 291, 332, 365, 415, 457, 498}, 0.f);
    line.start.value = core::DocumentTime(100000);
    line.end.value = core::DocumentTime(400000);
    // VFR: the frame at or after 100 starts at 125, the one before 400 at 365.
    EXPECT_EQ(moveTimes(scriptState(host), line), (std::pair{25, 265}));
}

TEST(VisualPositionTags, CalcMovePositionKeepsLegacysSteps)
{
    const double table[4] = {300, 400, 1000, 2000};
    EXPECT_EQ(calcMovePosition({100, 200}, table, 500), (PointF{100, 200}));
    EXPECT_EQ(calcMovePosition({100, 200}, table, 2500), (PointF{300, 400}));
    EXPECT_EQ(calcMovePosition({100, 200}, table, 1500), (PointF{200, 300}));
}

TEST(VisualPositionGesture, DragStagesAndCommitsOnReleaseAndEscCancels)
{
    TestHost host;
    standard(host);
    PositionTool tool;
    host.tool = &tool;
    tool.selected(host);
    tool.reset(host);
    const auto before = host.s->history().size();
    tool.pointer(at(Pointer::Kind::Press, 100, 100, Pointer::Button::Left, true), host);
    tool.pointer(at(Pointer::Kind::Move, 110, 105, Pointer::Button::None, true), host);
    ASSERT_TRUE(host.g);
    // Staged, not committed.
    EXPECT_EQ(text(host, 0), u8"{\\pos(300,300)}x");
    EXPECT_EQ(*host.g->staged(ids(*host.s)[0]), u8"{\\pos(330,315)}x");
    // Esc drops it; the rest of the drag writes nothing.
    host.escape();
    tool.pointer(at(Pointer::Kind::Move, 130, 105, Pointer::Button::None, true), host);
    tool.pointer(at(Pointer::Kind::Release, 130, 105, Pointer::Button::Left), host);
    EXPECT_EQ(text(host, 0), u8"{\\pos(300,300)}x");
    EXPECT_EQ(host.s->history().size(), before);
    EXPECT_FALSE(host.g);
    // The next drag works again: one step named as legacy's.
    tool.pointer(at(Pointer::Kind::Press, 100, 100, Pointer::Button::Left, true), host);
    tool.pointer(at(Pointer::Kind::Move, 101, 102, Pointer::Button::None, true), host);
    tool.pointer(at(Pointer::Kind::Release, 101, 102, Pointer::Button::Left), host);
    EXPECT_EQ(text(host, 0), u8"{\\pos(303,306)}x");
    ASSERT_EQ(host.s->history().size(), before + 1);
    EXPECT_EQ(host.s->history().back().name, "Visual positioning tool");
}

TEST(VisualPositionGesture, AHeldNudgeIsOneStepCommittedOnRelease)
{
    // Legacy committed every key press (VisualPosition.cpp:554); the
    // transaction rule (#55) commits a nudge on its key's release, so the
    // presses of a held key make one step.
    TestHost host;
    standard(host);
    PositionTool tool;
    host.tool = &tool;
    tool.selected(host);
    tool.reset(host);
    const auto before = host.s->history().size();
    Key key;
    key.key = 'D';
    EXPECT_TRUE(tool.key(key, host));
    key.autoRepeat = true;
    EXPECT_TRUE(tool.key(key, host));
    key.release = true;
    EXPECT_TRUE(tool.key(key, host)); // an auto-repeat release: still held
    key.release = false;
    EXPECT_TRUE(tool.key(key, host));
    EXPECT_EQ(text(host, 0), u8"{\\pos(300,300)}x");
    key.release = true;
    key.autoRepeat = false;
    EXPECT_TRUE(tool.key(key, host));
    EXPECT_EQ(text(host, 0), u8"{\\pos(303,300)}x");
    EXPECT_EQ(host.s->history().size(), before + 1);
    // Alt alone leaves the key to others; Ctrl+Alt nudges.
    Key alt;
    alt.key = 'A';
    alt.alt = true;
    EXPECT_FALSE(tool.key(alt, host));
}

TEST(VisualPositionGesture, QtsDoubleClickOrderGivesLegacysResult)
{
    // Qt delivers the second press before the double click; legacy got the
    // double click in its place. The Line goes to the point either way and
    // the release commits it.
    TestHost host;
    standard(host);
    PositionTool tool;
    host.tool = &tool;
    tool.selected(host);
    tool.reset(host);
    tool.pointer(at(Pointer::Kind::Press, 200, 100, Pointer::Button::Left, true), host);
    tool.pointer(at(Pointer::Kind::DoubleClick, 200, 100, Pointer::Button::Left, true), host);
    tool.pointer(at(Pointer::Kind::Release, 200, 100, Pointer::Button::Left), host);
    EXPECT_EQ(text(host, 0), u8"{\\pos(600,300)}x");
}

TEST(VisualPositionOptions, RectangleGreysXAndYAndOneStaysOn)
{
    TestHost host;
    standard(host);
    PositionTool tool;
    host.tool = &tool;
    tool.selected(host);
    tool.reset(host);
    auto option = [&](const std::string &name) {
        for (const auto &o : tool.options(host))
            if (o.name == name)
                return o;
        return ToolOption{};
    };
    EXPECT_EQ(tool.toolValue(), 1 | 64 | 128);
    EXPECT_FALSE(option("x").enabled);
    EXPECT_EQ(option("byRectangle").iconRole, "frame-to-scale");
    EXPECT_EQ(option("x").iconRole, "scale-x");
    EXPECT_EQ(option("y").iconRole, "scale-y");
    EXPECT_EQ(option("alignment").choices.size(), 21u);
    EXPECT_EQ(option("alignment").tooltip, u"Text placing works similar like in styles");
    EXPECT_FALSE(tool.setOption("x", 0, host)); // greyed
    EXPECT_TRUE(tool.setOption("byRectangle", 1, host));
    EXPECT_TRUE(option("x").enabled);
    EXPECT_TRUE(tool.setOption("x", 0, host));
    EXPECT_TRUE(tool.setOption("y", 0, host)); // the other comes back on
    EXPECT_FALSE(option("y").checked);
    EXPECT_TRUE(option("x").checked);
    EXPECT_EQ(tool.toolValue(), 1 | 32 | 64);
    MoveTool move;
    ASSERT_EQ(move.options(host).size(), 1u);
    EXPECT_EQ(move.options(host)[0].iconRole, "two-points");
}

TEST(VisualPositionValues, TypedPointWritesTheActiveLineAndMovesTheOthers)
{
    TestHost host;
    standard(host);
    host.picker.pick({ids(*host.s)[0], ids(*host.s)[2]});
    PositionTool tool;
    host.tool = &tool;
    tool.selected(host);
    tool.reset(host);
    auto values = tool.values(host);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0].text, u"300");
    EXPECT_EQ(values[1].text, u"300");
    EXPECT_FALSE(tool.setValue("x", u"abc", host));
    EXPECT_TRUE(tool.setValue("x", u"360", host));
    EXPECT_EQ(text(host, 0), u8"{\\pos(360,300)}x");
    EXPECT_EQ(text(host, 2), u8"{\\pos(1020,1060)}plain"); // moved by the same 60
    EXPECT_TRUE(tool.setValue("y", u"12.5", host));
    EXPECT_EQ(text(host, 0), u8"{\\pos(360,12.5)}x");
}

TEST(VisualMoveValues, TypedPointsWriteTheMove)
{
    TestHost host;
    standard(host, 1);
    MoveTool tool;
    host.tool = &tool;
    tool.selected(host);
    tool.reset(host);
    const auto values = tool.values(host);
    ASSERT_EQ(values.size(), 6u);
    EXPECT_EQ(values[0].text, u"30");
    EXPECT_EQ(values[2].text, u"90");
    EXPECT_EQ(values[4].text, u"0");
    EXPECT_EQ(values[5].text, u"2000");
    EXPECT_FALSE(values[4].editable);
    EXPECT_TRUE(tool.setValue("x2", u"600", host));
    EXPECT_EQ(text(host, 1), u8"{\\move(30,60,600,120,0,2000)}y");
}

TEST(VisualPositionWarning, OnlyWhileNoneOfItsLinesIsVisible)
{
    TestHost host;
    standard(host);
    PositionTool tool;
    host.tool = &tool;
    tool.selected(host);
    tool.reset(host);
    EXPECT_EQ(tool.warning(host), LineWarning::None);
    host.time = 3500;
    EXPECT_EQ(tool.warning(host), LineWarning::NotVisible);
    EXPECT_TRUE(tool.overlay(host).polygons.empty());
}
