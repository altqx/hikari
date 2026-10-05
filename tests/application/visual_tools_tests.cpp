// T1: the visual tools' transform, crosshair, VIDEO_COPY_COORDS, gestures
// and batch picker. The transform and crosshair replay the legacy probe's
// captures (tools/legacy-capture/visual_capture.cpp over
// inputs/visual-cases.txt; tests/fixtures/legacy-observations/
// local-t1-visual-20261005) and must give legacy's numbers exactly, but for
// the approved departures T1-copy-coords-view and T1-wheel-zoom-stale
// (docs/qt/compatibility-decisions.md), whose captures stay as the old
// evidence.

#include "hikari/application/visual_crosshair.h"
#include "hikari/application/visual_tools.h"
#include "hikari/application/visual_view.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
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

constexpr std::string_view kScript = "[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
                                     "[Events]\n"
                                     "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n"
                                     "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,second\n"
                                     "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,third\n"
                                     "Comment: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,note\n";

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

// A VisualHost over a VideoView and an EditSession, measuring labels as the
// probe's stand-in does (7 pixels a character, 14 high).
class TestHost : public VisualHost {
public:
    VideoView v;
    std::unique_ptr<EditSession> s;
    std::optional<Gesture> g;
    BatchPicker picker;
    int changes = 0;

    const VideoView &view() const override { return v; }
    const EditSession *session() const override { return s.get(); }
    std::optional<core::LineId> activeLine() const override
    {
        return s ? s->selection().active : std::nullopt;
    }
    std::vector<core::LineId> batchTargets() const override { return s ? picker.targets(*s) : std::vector<core::LineId>{}; }
    std::expected<Gesture *, CommandRefusal> beginGesture(std::vector<core::LineId> targets,
                                                          std::string history) override
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
    void cancelGesture() override { g.reset(); }
    std::pair<int, int> measureLabel(std::u16string_view text) const override
    {
        return {7 * static_cast<int>(text.size()), 14};
    }
    void toolChanged() override { ++changes; }
};

// One case of inputs/visual-cases.txt.
struct Case {
    std::string name;
    int clientW = 0, clientH = 0, panel = 0;
    SourceGeometry frame;
    std::optional<float> aspect;
    int scriptW = 0, scriptH = 0;
    double dpr = 1;
    std::vector<std::string> ops;
};

std::vector<Case> readCases()
{
    std::ifstream in(HIKARI_VISUAL_CASES);
    std::vector<Case> out;
    Case c;
    std::string text;
    while (std::getline(in, text)) {
        std::istringstream line(text);
        std::string key;
        if (!(line >> key))
            continue;
        if (key == "case") {
            c = Case{};
            line >> c.name;
        } else if (key == "client") {
            line >> c.clientW >> c.clientH >> c.panel;
        } else if (key == "frame") {
            line >> c.frame.width >> c.frame.height >> c.frame.sarNum >> c.frame.sarDen;
        } else if (key == "aspect") {
            float a;
            line >> a;
            c.aspect = a;
        } else if (key == "script") {
            line >> c.scriptW >> c.scriptH;
        } else if (key == "dpr") {
            line >> c.dpr;
        } else if (key == "zoom" || key == "zoomtoggle" || key == "resetzoom" || key == "drag" || key == "setvisual") {
            c.ops.push_back(text);
        } else if (key == "end") {
            out.push_back(c);
        }
    }
    return out;
}

std::map<std::string, QJsonObject> readObservations()
{
    QFile f(QStringLiteral(HIKARI_VISUAL_OBSERVATIONS));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    std::map<std::string, QJsonObject> out;
    for (const QByteArray &l : f.readAll().split('\n')) {
        if (l.trimmed().isEmpty())
            continue;
        const QJsonObject o = QJsonDocument::fromJson(l).object();
        out[o[QStringLiteral("case")].toString().toStdString()] = o;
    }
    return out;
}

// The probe prints floats with %.9g, which reads back to the same float.
float f(const QJsonValue &v)
{
    return static_cast<float>(v.toDouble());
}

IntRect rect(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    return {a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt()};
}

PointF pair(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    return {f(a[0]), f(a[1])};
}

std::u16string u16(const QJsonValue &v)
{
    return v.toString().toStdU16String();
}

// The entry of a capture array whose "at" is the given point.
QJsonObject entryAt(const QJsonObject &o, const char *key, const QJsonValue &at)
{
    for (const QJsonValue &v : o[QLatin1String(key)].toArray())
        if (v.toObject()[QStringLiteral("at")] == at)
            return v.toObject();
    ADD_FAILURE() << "no " << key << " entry at the point";
    return {};
}

// Approved departure T1-copy-coords-view: VIDEO_COPY_COORDS copies the
// crosshair's script position under the pointer (its label) in legacy's
// "x,y" form.
std::u16string copiedFromLabel(const QJsonValue &label)
{
    return label.toString().replace(QStringLiteral(", "), QStringLiteral(",")).toStdU16String();
}

// Sets the case up through the rewrite's logical coordinates: the probe's
// device pixels divided by the case's device pixel ratio, rounded back.
void setUp(TestHost &host, const Case &c)
{
    host.v.setDevicePixelRatio(c.dpr);
    const auto device = [&](int px) { return host.v.toDevice(px / c.dpr); };
    host.v.setClient(device(c.clientW), device(c.clientH), device(c.panel));
    host.v.setScript(c.scriptW, c.scriptH);
    host.v.open(c.frame);
    if (c.aspect)
        host.v.setAspectRatio(*c.aspect);
    for (const std::string &op : c.ops) {
        std::istringstream in(op);
        std::string kind;
        in >> kind;
        if (kind == "zoom") {
            float percent;
            int x, y;
            in >> percent >> x >> y;
            host.v.zoomAt(percent, x, y);
        } else if (kind == "zoomtoggle") {
            host.v.toggleZoom(0); // VIDEO_ZOOM_PERCENT unset
        } else if (kind == "resetzoom") {
            host.v.resetZoom();
        } else if (kind == "setvisual") {
            host.v.refreshToolTransform();
        } else if (kind == "drag") {
            int x0, y0, x1, y1;
            in >> x0 >> y0 >> x1 >> y1;
            host.v.zoomPress(x0, y0);
            host.v.zoomDrag(x1, y1);
        }
    }
}

std::vector<std::string> legacyTooltips()
{
    // VideoToolbar.cpp:44-54, the eleven active icons' help texts.
    std::ifstream in(std::string(HIKARI_LEGACY_DIR) + "/VideoToolbar.cpp");
    std::vector<std::string> out;
    std::string text;
    const std::regex item(R"re(icons\.push_back\(new itemdata\(PTR_BITMAP_PNG\(L"([^"]+)"\), _\("([^"]+)"\)\)\);)re");
    while (std::getline(in, text) && out.size() < kFamilyCount) {
        std::smatch m;
        if (std::regex_search(text, m, item))
            out.push_back(m[1].str() + "|" + m[2].str());
    }
    return out;
}

} // namespace

TEST(VisualFamilies, AreLegacysElevenRailItems)
{
    const auto legacy = legacyTooltips();
    ASSERT_EQ(legacy.size(), static_cast<std::size_t>(kFamilyCount));
    for (int i = 0; i < kFamilyCount; ++i) {
        const FamilyInfo &info = families()[i];
        EXPECT_EQ(static_cast<int>(info.family), i);
        EXPECT_EQ(std::string(info.icon) + "|" + std::string(info.tooltip), legacy[i]);
    }
    EXPECT_EQ(familyInfo(Family::Crosshair).history, "Visual positioning tool"); // VisualCross.cpp:125
    EXPECT_EQ(familyInfo(Family::Hydra).history, "Visual Hydra tool");
    EXPECT_NE(makeVisualTool(Family::Crosshair), nullptr);
}

TEST(VisualView, SourceAspectFollowsProviderFFMS2)
{
    // ProviderFFMS2.cpp:366-380: 720 * 32/27 = 853 (truncated) by 480 has no
    // common factor up to 10.
    EXPECT_EQ(sourceAspectRatio({720, 480, 32, 27}), 480.f / 853.f);
    EXPECT_EQ(sourceAspectRatio({1280, 720, 0, 1}), 9.f / 16.f);
    EXPECT_EQ(sourceAspectRatio({0, 720, 0, 1}), 0.f);
}

TEST(VisualCapture, ReplaysTheLegacyProbe)
{
    const auto observations = readObservations();
    const auto cases = readCases();
    ASSERT_EQ(cases.size(), observations.size());
    int copyDepartures = 0;
    for (const Case &c : cases) {
        SCOPED_TRACE(c.name);
        ASSERT_TRUE(observations.contains(c.name));
        // Approved departure T1-wheel-zoom-stale: legacy's wheel zoom left
        // the tools' transform unzoomed (the capture zoom-wheel-stale, kept
        // as the old evidence); here the tools take the zoom at once, so the
        // case gives what legacy gave once SetVisual ran: zoom-at-point, the
        // same zoom followed by setvisual.
        const bool staleZoom = c.name == "zoom-wheel-stale";
        const QJsonObject &legacy = observations.at(c.name);
        const QJsonObject &o = staleZoom ? observations.at("zoom-at-point") : legacy;
        if (staleZoom) {
            ASSERT_EQ(legacy[QStringLiteral("zoomOps")], QJsonArray{QStringLiteral("zoom 2.5 200 120")});
            ASSERT_EQ(o[QStringLiteral("zoomOps")], (QJsonArray{QStringLiteral("zoom 2.5 200 120"), QStringLiteral("setvisual")}));
        }
        TestHost host;
        host.s = std::make_unique<EditSession>(load(kScript));
        const core::LineId active = ids(*host.s).front();
        host.s->setSelection({active, {active}, active, std::nullopt});
        setUp(host, c);

        // The view.
        EXPECT_EQ(host.v.frameWidth(), o[QStringLiteral("width")].toInt());
        EXPECT_EQ(host.v.frameHeight(), o[QStringLiteral("height")].toInt());
        EXPECT_EQ(host.v.aspectRatio(), f(o[QStringLiteral("aspect")]));
        EXPECT_EQ(host.v.windowRect(), rect(o[QStringLiteral("window")]));
        EXPECT_EQ(host.v.videoRect(), rect(o[QStringLiteral("videoRect")]));
        EXPECT_EQ(host.v.sourceRect(), rect(o[QStringLiteral("sourceRect")]));
        EXPECT_EQ(host.v.zoomPercent(), f(o[QStringLiteral("zoomPercent")]));
        EXPECT_EQ(host.v.zoomMode(), o[QStringLiteral("hasZoom")].toBool());
        const QJsonArray zr = o[QStringLiteral("zoomRect")].toArray();
        EXPECT_EQ(host.v.zoomRect(), (EdgeRect{f(zr[0]), f(zr[1]), f(zr[2]), f(zr[3])}));
        EXPECT_EQ(host.v.zoomMove(), pair(o[QStringLiteral("zoomMove")]));
        EXPECT_EQ(host.v.zoomScale(), pair(o[QStringLiteral("zoomScale")]));
        EXPECT_EQ((PointF{host.v.coeffW(), host.v.coeffH()}), pair(o[QStringLiteral("coeff")]));

        // The crosshair: SetCurVisual, then a move to each point.
        CrosshairTool cross;
        cross.reset(host);
        for (const QJsonValue &lv : legacy[QStringLiteral("points")].toArray()) {
            const QJsonObject p = entryAt(o, "points", lv.toObject()[QStringLiteral("at")]);
            const int x = p[QStringLiteral("at")].toArray()[0].toInt();
            const int y = p[QStringLiteral("at")].toArray()[1].toInt();
            SCOPED_TRACE(std::to_string(x) + "," + std::to_string(y));
            // The pointer arrives in logical coordinates.
            const int dx = host.v.toDevice(x / c.dpr), dy = host.v.toDevice(y / c.dpr);
            ASSERT_EQ(dx, x);
            ASSERT_EQ(dy, y);
            cross.pointer({Pointer::Kind::Move, dx, dy}, host);
            EXPECT_EQ(host.v.viewToScript({static_cast<float>(x), static_cast<float>(y)}), pair(p[QStringLiteral("out")]));
            EXPECT_EQ(cross.label(), u16(p[QStringLiteral("label")]));
            EXPECT_EQ(cross.onVideo(), p[QStringLiteral("onVideo")].toBool());
            EXPECT_EQ(cross.crossOn(), p[QStringLiteral("shown")].toBool());
            EXPECT_EQ(cross.coefficients(), pair(p[QStringLiteral("crossCoeff")]));
            if (cross.onVideo()) { // off the video legacy keeps (or never set) them
                EXPECT_EQ(cross.labelRect(), rect(p[QStringLiteral("labelRect")]));
                const QJsonArray lines = p[QStringLiteral("lines")].toArray();
                for (int i = 0; i < 4; ++i)
                    EXPECT_EQ(cross.lines()[i], pair(lines[i]));
                EXPECT_FALSE(cross.overlay(host).empty());
            } else {
                EXPECT_TRUE(cross.overlay(host).empty());
            }
            const std::u16string copied = copiedFromLabel(p[QStringLiteral("label")]);
            EXPECT_EQ(copyCoordinatesText(host.v, x, y), copied);
            if (copied != u16(lv.toObject()[QStringLiteral("copy")]))
                ++copyDepartures;
        }
        for (const QJsonValue &lv : legacy[QStringLiteral("scriptPoints")].toArray()) {
            const QJsonObject p = entryAt(o, "scriptPoints", lv.toObject()[QStringLiteral("at")]);
            EXPECT_EQ(host.v.scriptToView(pair(p[QStringLiteral("at")])), pair(p[QStringLiteral("in")]));
        }
        // Ctrl+click and middle click: \pos into the active Line, one step.
        for (const QJsonValue &kv : legacy[QStringLiteral("clicks")].toArray()) {
            const QJsonObject k = kv.toObject();
            // The \pos written where SetVisual had run (T1-wheel-zoom-stale).
            const QJsonObject want = staleZoom ? entryAt(o, "clicks", k[QStringLiteral("at")]) : k;
            const int x = k[QStringLiteral("at")].toArray()[0].toInt();
            const int y = k[QStringLiteral("at")].toArray()[1].toInt();
            const bool tl = k[QStringLiteral("tlMode")].toBool();
            host.s = std::make_unique<EditSession>(load(kScript));
            const core::LineId id = ids(*host.s).front();
            host.s->setSelection({id, {id}, id, std::nullopt});
            const std::u8string text = core::toUtf8(u16(k[QStringLiteral("text")]));
            const std::u8string translation = core::toUtf8(u16(k[QStringLiteral("tl")]));
            ASSERT_TRUE(host.s->run({"set up", host.s->revision(), {id}, [&](core::Document &d) {
                                         if (tl)
                                             d.setScriptInfo(u8"TLMode", u8"Yes");
                                         return d.editLine(id, [&](core::LineRecord &l) {
                                             l.text = text;
                                             l.translation = translation;
                                         });
                                     }}));
            const std::size_t steps = host.s->historySize();
            Pointer press{Pointer::Kind::Press, x, y};
            if (k[QStringLiteral("button")].toString() == QStringLiteral("middle")) {
                press.button = Pointer::Button::Middle;
            } else {
                press.button = Pointer::Button::Left;
                press.control = true;
            }
            cross.pointer({Pointer::Kind::Move, x, y}, host);
            cross.pointer(press, host);
            const QString newText = want[QStringLiteral("newText")].toString().replace(
                want[QStringLiteral("text")].toString(), k[QStringLiteral("text")].toString());
            EXPECT_EQ(line(*host.s, id)->text, core::toUtf8(newText.toStdU16String()));
            EXPECT_EQ(line(*host.s, id)->translation, core::toUtf8(u16(k[QStringLiteral("newTl")])));
            EXPECT_EQ(host.s->historySize(), steps + 1);
            EXPECT_EQ(host.s->history().back().name, "Visual positioning tool");
        }
        if (staleZoom) { // the old evidence: legacy's tools stayed unzoomed
            EXPECT_NE(host.v.zoomScale(), pair(legacy[QStringLiteral("zoomScale")]));
            EXPECT_NE(host.v.zoomMove(), pair(legacy[QStringLiteral("zoomMove")]));
        }
    }
    // The old evidence of T1-copy-coords-view: legacy's text differs from the
    // script position under the pointer with a bar or a zoom.
    EXPECT_GT(copyDepartures, 0);
}

TEST(VisualView, ForwardAndInverseRoundTrip)
{
    // Every case's view, at fractional device pixel ratios too: a script
    // point drawn in the view maps back to itself, and a logical pointer
    // position reaches the device pixel it came from.
    for (const Case &c0 : readCases()) {
        for (const double dpr : {1.0, 1.25, 1.5, 1.75, 2.0}) {
            Case c = c0;
            c.dpr = dpr;
            SCOPED_TRACE(c.name + " @" + std::to_string(dpr));
            TestHost host;
            setUp(host, c);
            for (float sx : {0.f, 0.5f * c.scriptW, 0.25f * c.scriptW + 0.5f, static_cast<float>(c.scriptW - 1)})
                for (float sy : {0.f, 0.5f * c.scriptH, 0.75f * c.scriptH + 0.25f, static_cast<float>(c.scriptH - 1)}) {
                    const PointF view = host.v.scriptToView({sx, sy});
                    const PointF back = host.v.viewToScript(view);
                    EXPECT_NEAR(back.x, sx, 1e-3f * std::max(1.f, std::abs(sx)));
                    EXPECT_NEAR(back.y, sy, 1e-3f * std::max(1.f, std::abs(sy)));
                }
            for (int px : {0, 1, 7, 99, 333, 641, 1023})
                EXPECT_EQ(host.v.toDevice(host.v.toLogical(px)), px);
        }
    }
}

TEST(VisualCrosshair, HiddenWithoutVideoAndAfterLeaveOrRightRelease)
{
    TestHost host;
    host.s = std::make_unique<EditSession>(load(kScript));
    host.v.setClient(640, 400, 40);
    host.v.setScript(1920, 1080);
    CrosshairTool cross;
    cross.reset(host);
    cross.pointer({Pointer::Kind::Enter, 10, 10}, host);
    EXPECT_FALSE(cross.shown() && !cross.overlay(host).empty()); // DrawLines: no video, no lines
    host.v.open({1280, 720, 0, 1});
    cross.reset(host);
    cross.pointer({Pointer::Kind::Enter, 10, 10}, host);
    EXPECT_TRUE(cross.shown());
    cross.pointer({Pointer::Kind::Leave, 10, 10}, host);
    EXPECT_FALSE(cross.shown());
    cross.pointer({Pointer::Kind::Move, 20, 20}, host);
    EXPECT_TRUE(cross.shown());
    cross.pointer({Pointer::Kind::Release, 20, 20, Pointer::Button::Right}, host);
    EXPECT_FALSE(cross.shown());
    // A resize (SetCurVisual) hides it until the next move.
    cross.pointer({Pointer::Kind::Move, 20, 20}, host);
    cross.reset(host);
    EXPECT_FALSE(cross.shown());
}

// A departure by the approved rule "Commands commit the draft first"
// (edit-transactions.md): legacy Cross wrote into tab->edit->line, which
// lacks the typed text, and the following SetModified reloaded the editor
// over it (SubsGridBase.cpp:1118), losing it. Here the draft is its own
// step and \pos goes into the typed text.
TEST(VisualCrosshair, ClickUsesThePendingDraftAndCommitsItFirst)
{
    TestHost host;
    host.s = std::make_unique<EditSession>(load(kScript));
    host.v.setClient(640, 400, 40);
    host.v.setScript(1920, 1080);
    host.v.open({1280, 720, 0, 1});
    const core::LineId id = ids(*host.s)[1];
    host.s->setSelection({id, {id}, id, std::nullopt});
    ASSERT_TRUE(host.s->editDraftText(id, u8"{\\b1}typed"));
    CrosshairTool cross;
    cross.reset(host);
    cross.pointer({Pointer::Kind::Move, 320, 180}, host);
    cross.pointer({Pointer::Kind::Press, 320, 180, Pointer::Button::Middle}, host);
    EXPECT_EQ(line(*host.s, id)->text, u8"{\\pos(961.502,541.504)\\b1}typed");
    const auto history = host.s->history();
    ASSERT_GE(history.size(), 3u);
    EXPECT_EQ(history[history.size() - 2].name, "Edit Line"); // the draft, its own step
    EXPECT_EQ(history.back().name, "Visual positioning tool");
    EXPECT_FALSE(host.s->draftLine());
}

TEST(VisualGesture, OneUndoStepPerGesture)
{
    EditSession session(load(kScript));
    const auto all = ids(session);
    const std::size_t steps = session.historySize();
    auto g = Gesture::begin(session, {all[0], all[2]}, "Visual positioning tool");
    ASSERT_TRUE(g);
    // Many pointer samples stage; nothing reaches the Document meanwhile.
    for (int i = 0; i < 20; ++i) {
        g->stage(all[0], u8"{\\pos(" + std::u8string(1, char8_t(u8'0' + i % 10)) + u8",0)}first");
        g->stage(all[2], u8"{\\pos(1,1)}third");
        EXPECT_EQ(line(session, all[0])->text, u8"first");
    }
    ASSERT_TRUE(g->commit(session));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Visual positioning tool");
    EXPECT_EQ(line(session, all[0])->text, u8"{\\pos(9,0)}first");
    EXPECT_EQ(line(session, all[2])->text, u8"{\\pos(1,1)}third");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(line(session, all[0])->text, u8"first");
    EXPECT_EQ(line(session, all[2])->text, u8"third");
    // Nothing staged: no step.
    auto empty = Gesture::begin(session, {all[0]}, "Visual positioning tool");
    ASSERT_TRUE(empty);
    const std::size_t before = session.historySize();
    EXPECT_TRUE(empty->commit(session));
    EXPECT_EQ(session.historySize(), before);
}

// T2: the video's preview while a gesture is open holds only the Lines the
// frame shows, as legacy's dummy rendering sent (Position::ChangeMultiline on
// SubsGrid::GetVisible, SubsGridBase.cpp:1517-1593, VisualPosition.cpp:
// 470-481): others within 5 ms of the time, or not yet over while playing;
// the targets, staged, while the time is within their own (end included), or
// always while playing.
TEST(VisualGesture, PreviewHoldsOnlyTheShownLines)
{
    EditSession session(load(kScript));
    const auto all = ids(session);
    auto g = Gesture::begin(session, {all[0], all[2]}, "Visual positioning tool");
    ASSERT_TRUE(g);
    g->stage(all[0], u8"{\\pos(1,1)}first");
    g->stage(all[2], u8"{\\pos(3,3)}third");
    auto texts = [&](std::int64_t time, bool playing) {
        std::vector<std::u8string> out;
        for (const auto *l : g->preview(session.document(), time, playing).lines())
            out.push_back(l->text);
        return out;
    };
    using V = std::vector<std::u8string>;
    EXPECT_EQ(texts(1500, false), (V{u8"{\\pos(1,1)}first"}));
    EXPECT_EQ(texts(1994, false), (V{u8"{\\pos(1,1)}first"}));
    EXPECT_EQ(texts(1995, false), (V{u8"{\\pos(1,1)}first", u8"second"}));
    EXPECT_EQ(texts(2000, false), (V{u8"{\\pos(1,1)}first", u8"second"})); // a target's end included
    EXPECT_EQ(texts(2001, false), (V{u8"second"}));
    EXPECT_EQ(texts(3004, false), (V{u8"second", u8"{\\pos(3,3)}third"}));
    EXPECT_EQ(texts(4500, false), (V{u8"note"}));
    EXPECT_EQ(texts(1500, true), (V{u8"{\\pos(1,1)}first", u8"second", u8"{\\pos(3,3)}third", u8"note"}));
    EXPECT_EQ(texts(3500, true), (V{u8"{\\pos(1,1)}first", u8"{\\pos(3,3)}third", u8"note"}));
    // The Document itself keeps every Line.
    EXPECT_EQ(session.document().lines().size(), 4u);
}

TEST(VisualGesture, CancelLeavesThePreGestureDraft)
{
    EditSession session(load(kScript));
    const auto all = ids(session);
    session.setSelection({all[1], {all[1]}, all[1], std::nullopt});
    ASSERT_TRUE(session.editDraftText(all[1], u8"draft text"));
    const std::size_t steps = session.historySize();
    const auto revision = session.revision();
    auto begun = Gesture::begin(session, {all[1]}, "Visual positioning tool");
    ASSERT_TRUE(begun);
    std::optional<Gesture> g(std::move(*begun));
    EXPECT_EQ(g->before(all[1]).text, u8"draft text"); // the draft as the gesture began
    g->stage(all[1], u8"{\\pos(5,5)}draft text");
    g.reset(); // Esc
    EXPECT_EQ(session.draftText(), u8"draft text");
    EXPECT_EQ(session.draftLine(), all[1]);
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_EQ(session.revision(), revision);
    EXPECT_EQ(line(session, all[1])->text, u8"second");
}

TEST(VisualGesture, ProtectedReferenceRefusesWrites)
{
    EditSession reference(load(kScript), true);
    const auto all = ids(reference);
    EXPECT_EQ(Gesture::begin(reference, {all[0]}, "x").error(), CommandRefusal::Protected);
    // The crosshair over a reference: the label and lines work, a click
    // writes nothing.
    TestHost host;
    host.s = std::make_unique<EditSession>(load(kScript), true);
    const core::LineId id = ids(*host.s)[0];
    host.s->setSelection({id, {id}, id, std::nullopt});
    host.v.setClient(640, 400, 40);
    host.v.setScript(1920, 1080);
    host.v.open({1280, 720, 0, 1});
    CrosshairTool cross;
    cross.reset(host);
    cross.pointer({Pointer::Kind::Move, 320, 180}, host);
    EXPECT_TRUE(cross.shown());
    EXPECT_EQ(cross.label(), u"961, 541");
    cross.pointer({Pointer::Kind::Press, 320, 180, Pointer::Button::Middle}, host);
    EXPECT_EQ(line(*host.s, id)->text, u8"first");
    EXPECT_EQ(host.s->historySize(), 1u);
    EXPECT_FALSE(host.g);
}

TEST(VisualGesture, RefusedWhileAMacroHoldsTheDocumentOrTargetsAreGone)
{
    EditSession session(load(kScript));
    const auto all = ids(session);
    session.setReadOnly(true);
    EXPECT_EQ(Gesture::begin(session, {all[0]}, "x").error(), CommandRefusal::ReadOnly);
    session.setReadOnly(false);
    EXPECT_EQ(Gesture::begin(session, {}, "x").error(), CommandRefusal::Invalid);
    EXPECT_EQ(Gesture::begin(session, {core::LineId{999999}}, "x").error(), CommandRefusal::UnknownLine);
}

TEST(VisualGesture, TargetsAreFixedAndNeverRetargeted)
{
    EditSession session(load(kScript));
    const auto all = ids(session);
    session.setSelection({all[0], {all[0]}, all[0], std::nullopt});
    BatchPicker picker;
    picker.pick({all[2], all[0]});
    auto g = Gesture::begin(session, picker.targets(session), "Visual position adjustment tool");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->targets(), (std::vector<core::LineId>{all[0], all[2]})); // Document order
    // The picker, selection and active Line change mid-gesture; a draft
    // starts on another Line.
    picker.pick({all[1]});
    session.setSelection({all[1], {all[1]}, all[1], std::nullopt});
    g->stage(all[0], u8"A");
    g->stage(all[2], u8"C");
    g->stage(all[1], u8"not a target"); // ignored
    ASSERT_TRUE(g->commit(session));
    EXPECT_EQ(line(session, all[0])->text, u8"A");
    EXPECT_EQ(line(session, all[1])->text, u8"second");
    EXPECT_EQ(line(session, all[2])->text, u8"C");

    // A draft on a Line outside the gesture stays a draft on that Line.
    ASSERT_TRUE(session.editDraftText(all[1], u8"pending"));
    auto h = Gesture::begin(session, {all[0]}, "Visual positioning tool");
    ASSERT_TRUE(h);
    h->stage(all[0], u8"AA");
    ASSERT_TRUE(h->commit(session));
    EXPECT_EQ(session.draftLine(), all[1]);
    EXPECT_EQ(session.draftText(), u8"pending");
    EXPECT_EQ(line(session, all[1])->text, u8"second");
}

TEST(VisualGesture, StaleGestureIsRefusedNotRetargeted)
{
    EditSession session(load(kScript));
    const auto all = ids(session);
    auto g = Gesture::begin(session, {all[0]}, "Visual positioning tool");
    ASSERT_TRUE(g);
    g->stage(all[0], u8"late");
    ASSERT_TRUE(session.run({"other", session.revision(), {all[2]}, [&](core::Document &d) {
                                 return d.setLineText(all[2], u8"changed meanwhile");
                             }}));
    const std::size_t steps = session.historySize();
    EXPECT_EQ(g->commit(session).error(), CommandRefusal::StaleRevision);
    EXPECT_EQ(session.historySize(), steps);
    EXPECT_EQ(line(session, all[0])->text, u8"first");
}

TEST(VisualBatchPicker, PickedLinesInDocumentOrderElseTheActiveLine)
{
    EditSession session(load(kScript));
    const auto all = ids(session);
    BatchPicker picker;
    EXPECT_TRUE(picker.targets(session).empty()); // no active Line
    session.setSelection({all[1], {all[1], all[2]}, all[1], std::nullopt});
    EXPECT_EQ(picker.targets(session), (std::vector<core::LineId>{all[1]}));
    picker.pick({all[3], all[0], all[3]});
    EXPECT_EQ(picker.picked(), (std::vector<core::LineId>{all[3], all[0]}));
    EXPECT_EQ(picker.targets(session), (std::vector<core::LineId>{all[0], all[3]}));
    // A picked Line removed meanwhile is left out.
    ASSERT_TRUE(session.run({"delete", session.revision(), {all[0]}, [&](core::Document &d) {
                                 return d.removeLine(all[0]);
                             }}));
    EXPECT_EQ(picker.targets(session), (std::vector<core::LineId>{all[3]}));
    picker.clear();
    EXPECT_EQ(picker.targets(session), (std::vector<core::LineId>{all[1]}));
}

TEST(VisualWarnings, FollowVisualsDraw)
{
    EditSession session(load(kScript));
    const auto *dialogue = session.document().lines()[0]; // 1.00-2.00
    const auto *comment = session.document().lines()[3];  // a comment, 4.00-5.00
    EXPECT_EQ(lineWarning(Family::Position, *dialogue, 1000), LineWarning::None);
    EXPECT_EQ(lineWarning(Family::Position, *dialogue, 1999), LineWarning::None);
    EXPECT_EQ(lineWarning(Family::Position, *dialogue, 2000), LineWarning::NotVisible); // end excluded
    EXPECT_EQ(lineWarning(Family::Position, *dialogue, 999), LineWarning::NotVisible);
    EXPECT_EQ(lineWarning(Family::Position, *comment, 4500), LineWarning::Comment);
    EXPECT_EQ(lineWarning(Family::VectorClip, *comment, 4500), LineWarning::None);
    EXPECT_EQ(lineWarning(Family::Drawing, *comment, 4500), LineWarning::None);
    // Outside its time a comment shows the comment text, for every tool.
    EXPECT_EQ(lineWarning(Family::Drawing, *comment, 100), LineWarning::Comment);
    EXPECT_EQ(warningText(LineWarning::NotVisible), u"Line is not visible on video\nor has zero duration");
    EXPECT_EQ(warningText(LineWarning::Comment), u"Visual editing tools\ndo not work on comments");
    CrosshairTool cross;
    EXPECT_FALSE(cross.warnsOutsideLine()); // Cross overrides Visuals::Draw
}
