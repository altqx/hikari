#include "visual_tools_controller.h"

#include "video_controller.h"

#include "hikari/application/resample.h"
#include "hikari/application/visual_crosshair.h"

#include <QClipboard>
#include <QColor>
#include <QCursor>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QQuickItem>
#include <QStringList>
#include <QVariantMap>

#include <cmath>

namespace hikari::ui {

using namespace application::visual;

namespace {

QString qs(std::u16string_view text)
{
    return QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
}

QString colour(std::uint32_t argb)
{
    return QColor::fromRgba(argb).name(QColor::HexArgb);
}

} // namespace

VisualToolsController::VisualToolsController(VideoController &video, SettingsStore &settings, SessionProvider session,
                                             QObject *parent)
    : QObject(parent), m_video(video), m_settings(settings), m_session(std::move(session))
{
    for (int i = 0; i < kFamilyCount; ++i)
        m_tools.push_back(makeVisualTool(static_cast<Family>(i)));
    // The crosshair's label: the program font + 4 (config::GetFont(4),
    // VisualCross.cpp:179-185), bold as the D3DX font Cross::SizeChanged makes.
    const QString family = m_settings.value(QStringLiteral("program.font")).toString();
    m_labelFont = QGuiApplication::font();
    if (!family.isEmpty())
        m_labelFont.setFamily(family);
    m_labelFont.setPointSizeF(m_settings.integer("program.fontSize") + 4);
    m_labelFont.setBold(true);
    connect(&m_video, &VideoController::changed, this, [this] {
        syncGeometry();
        emit changed(); // the shown frame's time moves the warnings
        // T3: a \move's position follows the shown frame (legacy Draw ran
        // DrawVisual on every frame, Visuals.cpp:504-529).
        if (const auto time = videoTimeMs(); time != m_seenTime) {
            m_seenTime = time;
            emit overlayChanged();
        }
    });
    connect(&m_settings, &SettingsStore::changed, this, [this](const QString &id) {
        if (id == QStringLiteral("video.visualWarningsOff"))
            emit changed();
    });
}

VisualToolsController::~VisualToolsController() = default;

void VisualToolsController::setTool(Family family, std::unique_ptr<VisualTool> tool)
{
    m_tools[static_cast<std::size_t>(family)] = std::move(tool);
    if (family == m_family)
        resetTool();
}

VisualTool *VisualToolsController::tool() const
{
    // Legacy's renderer holds a tool (m_Visual) exactly while its tab's
    // Document is ASS: made when the renderer opens unless the rail is
    // disabled (RendererVideo.cpp:113-114, VideoBox.cpp:315-321), made when an
    // ASS Document arrives with the video already open (VideoBox::
    // DisableVisuals, VideoBox.cpp:1748-1758, from Notebook.cpp:1145,
    // SubsGridBase.cpp:981, SubsGrid.cpp:1184, HikariSubFrame.cpp:1169) and
    // deleted for any other format (RemoveVisual(false, true),
    // RendererVideo.cpp:1133-1146). An edit never makes it while the
    // crosshair is the tool (SubsGridBase.cpp:1168, EditBox.cpp:485, 979,
    // SubsGridWindow.cpp:1399 run SetVisual only above CROSS).
    const auto *s = editingSession();
    if (!s || s->document().format() != core::SubtitleFormat::Ass)
        return nullptr;
    return m_tools[static_cast<std::size_t>(m_family)].get();
}

QVariantList VisualToolsController::families() const
{
    QVariantList out;
    for (const FamilyInfo &f : application::visual::families())
        out.append(QVariantMap{{QStringLiteral("name"), QString::fromLatin1(f.tooltip.data(), f.tooltip.size())},
                               {QStringLiteral("icon"), QString::fromLatin1(f.icon.data(), f.icon.size())},
                               {QStringLiteral("available"),
                                m_tools[static_cast<std::size_t>(f.family)] != nullptr}});
    return out;
}

void VisualToolsController::setViewport(qreal width, qreal height, qreal panelHeight, qreal devicePixelRatio)
{
    m_logicalWidth = width;
    m_logicalHeight = height;
    m_logicalPanel = panelHeight;
    m_view.setDevicePixelRatio(devicePixelRatio);
    // The video window's client: the video area and the panel below it.
    m_view.setClient(m_view.toDevice(width), m_view.toDevice(height + panelHeight), m_view.toDevice(panelHeight));
    // RendererVideo::UpdateVideoWindow: the tool takes the new size (SetCurVisual).
    if (auto *t = tool())
        t->reset(*this);
    emit geometryChanged();
    emit overlayChanged();
}

void VisualToolsController::syncGeometry()
{
    const SourceGeometry geometry = m_video.session().sourceGeometry();
    if (geometry == m_geometry && m_view.hasVideo() == geometry.valid())
        return;
    m_geometry = geometry;
    if (geometry.valid()) {
        m_view.open(geometry);
    } else {
        m_view.close();
    }
    resetTool();
    emit geometryChanged();
}

void VisualToolsController::refresh()
{
    auto *s = editingSession();
    if (s) {
        const auto res = application::scriptResolution(s->document());
        m_view.setScript(res.width, res.height);
    }
    const bool ass = s && s->document().format() == core::SubtitleFormat::Ass;
    bool reset = false;
    if (ass != m_railEnabled) {
        // VideoToolbar::DisableVisuals: the rail's toggle goes back to the crosshair.
        m_railEnabled = ass;
        if (!ass && m_family != Family::Crosshair) {
            (void)escape();
            m_family = Family::Crosshair;
        }
        reset = true;
    }
    const void *id = s;
    const std::uint64_t revision = s ? s->revision() : 0;
    const auto active = s ? s->selection().active : std::nullopt;
    if (id != m_seenSession) {
        // Another editing target: a gesture never moves to it.
        (void)escape();
        m_picker.clear();
        reset = true;
    } else if (revision != m_seenRevision) {
        reset = true;
    }
    if (active != m_seenActive)
        reset = true;
    m_seenSession = id;
    m_seenRevision = revision;
    m_seenActive = active;
    if (reset)
        resetTool();
    emit changed();
}

void VisualToolsController::resetTool()
{
    // RendererVideo::SetVisual: the tools' transform takes the current zoom,
    // then the tool's SetCurVisual.
    m_view.refreshToolTransform();
    if (auto *t = tool())
        t->reset(*this);
    emit changed();
    emit overlayChanged();
    emit optionsChanged(); // the family, the format or the Line changed
}

std::optional<core::LineId> VisualToolsController::activeLine() const
{
    const auto *s = editingSession();
    return s ? s->selection().active : std::nullopt;
}

std::vector<core::LineId> VisualToolsController::batchTargets() const
{
    const auto *s = editingSession();
    return s ? m_picker.targets(*s) : std::vector<core::LineId>{};
}

std::expected<Gesture *, application::CommandRefusal> VisualToolsController::beginGesture(std::vector<core::LineId> targets,
                                                                                          std::string history)
{
    const auto *s = editingSession();
    if (!s || m_gesture)
        return std::unexpected(application::CommandRefusal::Invalid);
    auto begun = Gesture::begin(*s, std::move(targets), std::move(history));
    if (!begun) {
        m_lastRefusal = begun.error();
        return std::unexpected(begun.error());
    }
    m_gesture.emplace(std::move(*begun));
    emit changed();
    return &*m_gesture;
}

std::expected<void, application::CommandRefusal> VisualToolsController::commitGesture()
{
    auto *s = editingSession();
    if (!m_gesture || !s)
        return std::unexpected(application::CommandRefusal::Invalid);
    const bool changes = m_gesture->hasChanges();
    auto result = m_gesture->commit(*s);
    m_gesture.reset();
    if (!result)
        m_lastRefusal = result.error();
    if (changes && m_edited)
        m_edited(); // the shell refreshes; refresh() follows
    emit changed();
    return result;
}

bool VisualToolsController::escape()
{
    if (!m_gesture) {
        // T3: with no gesture open Esc drops a tool's pending step (the
        // first of RotationZ's two points, the card's evidence on #178).
        auto *t = tool();
        if (t && m_view.hasVideo() && t->cancelPending(*this)) {
            emit changed();
            emit overlayChanged();
            return true;
        }
        return false;
    }
    m_gesture.reset();
    // T3: the tool reads the unchanged text again (legacy SetCurVisual), so
    // its handles go back to where the text puts them.
    if (auto *t = tool(); t && t->family() != Family::Crosshair)
        t->reset(*this);
    emit changed();
    emit overlayChanged();
    return true;
}

bool VisualToolsController::escapable() const
{
    if (m_gesture)
        return true;
    const auto *t = tool();
    return t && m_view.hasVideo() && t->hasPending();
}

QVariantList VisualToolsController::options() const
{
    QVariantList out;
    const auto *t = tool();
    if (!t)
        return out;
    for (const ToolOption &o : t->options(*this)) {
        QStringList choices;
        for (const auto &c : o.choices)
            choices.append(qs(c));
        out.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(o.name)},
                               {QStringLiteral("kind"), o.kind == ToolOption::Kind::Choice ? QStringLiteral("choice")
                                                                                         : QStringLiteral("toggle")},
                               {QStringLiteral("iconRole"), QString::fromStdString(o.iconRole)},
                               {QStringLiteral("tooltip"), qs(o.tooltip)},
                               {QStringLiteral("checked"), o.checked},
                               {QStringLiteral("enabled"), o.enabled},
                               {QStringLiteral("choices"), choices},
                               {QStringLiteral("index"), o.index}});
    }
    return out;
}

bool VisualToolsController::setOption(const QString &name, int value)
{
    // Legacy VideoToolbar's item click: the tool takes its toggles
    // (VideoBox.cpp:179-181, Visuals::ChangeTool); never during a gesture.
    auto *t = tool();
    if (!t || m_gesture)
        return false;
    const bool done = t->setOption(name.toStdString(), value, *this);
    emit changed();
    emit overlayChanged();
    emit optionsChanged();
    return done;
}

std::int64_t VisualToolsController::videoTimeMs() const
{
    // VideoBox::Tell: the shown frame's start.
    const auto frame = m_video.session().shownFrame();
    if (!frame)
        return 0;
    const auto start = m_video.session().frameStart(*frame);
    return start ? start->microseconds() / 1000 : 0;
}

application::LegacyTimebase VisualToolsController::timebase() const
{
    return m_video.session().legacyTimebase();
}

std::pair<long, long> VisualToolsController::editorSelection() const
{
    return m_editorSelection ? m_editorSelection() : std::pair<long, long>{0, 0};
}

std::pair<int, int> VisualToolsController::measureLabel(std::u16string_view text) const
{
    // In device pixels, as legacy measured in its window.
    const QFontMetricsF metrics(m_labelFont);
    const double dpr = m_view.devicePixelRatio();
    return {static_cast<int>(std::lround(metrics.horizontalAdvance(qs(text)) * dpr)),
            static_cast<int>(std::lround(metrics.height() * dpr))};
}

QString VisualToolsController::labelFamily() const
{
    return QFontInfo(m_labelFont).family();
}

int VisualToolsController::labelPixelSize() const
{
    return QFontInfo(m_labelFont).pixelSize();
}

void VisualToolsController::toolChanged()
{
    emit overlayChanged();
    emit changed();
}

void VisualToolsController::pointer(int kind, qreal x, qreal y, int button, int buttons, int modifiers, int wheelSteps)
{
    Pointer p;
    p.kind = static_cast<Pointer::Kind>(kind);
    p.x = m_view.toDevice(x);
    p.y = m_view.toDevice(y);
    switch (button) {
    case Qt::LeftButton:
        p.button = Pointer::Button::Left;
        break;
    case Qt::MiddleButton:
        p.button = Pointer::Button::Middle;
        break;
    case Qt::RightButton:
        p.button = Pointer::Button::Right;
        break;
    default:
        break;
    }
    p.leftDown = (buttons & Qt::LeftButton) != 0;
    p.rightDown = (buttons & Qt::RightButton) != 0;
    p.middleDown = (buttons & Qt::MiddleButton) != 0;
    p.control = (modifiers & Qt::ControlModifier) != 0;
    p.shift = (modifiers & Qt::ShiftModifier) != 0;
    p.alt = (modifiers & Qt::AltModifier) != 0;
    p.wheelSteps = wheelSteps;
    if (p.kind == Pointer::Kind::Leave) {
        m_overVideo = false;
        m_lastPointer.reset();
    } else {
        m_overVideo = true;
        m_lastPointer = QPointF(x, y);
    }
    // VideoBox::OnMouseEvent: nothing without a video (GetState() == None).
    auto *t = tool();
    if (!t || !m_view.hasVideo())
        return;
    // Visuals::Draw's blockevents: a tool other than the crosshair takes no
    // events outside its Line's time or on a comment, warning shown or not.
    if (t->warnsOutsideLine() && currentWarning() != LineWarning::None) {
        if (m_gesture && p.kind == Pointer::Kind::Release)
            (void)escape(); // legacy released the mouse capture
        return;
    }
    t->pointer(p, *this);
    emit changed();
}

bool VisualToolsController::key(int key, int modifiers, bool release, bool autoRepeat)
{
    auto *t = tool();
    if (!t || !m_view.hasVideo())
        return false;
    if (t->warnsOutsideLine() && currentWarning() != LineWarning::None)
        return false;
    Key k;
    k.key = key;
    k.release = release;
    k.autoRepeat = autoRepeat;
    k.control = (modifiers & Qt::ControlModifier) != 0;
    k.shift = (modifiers & Qt::ShiftModifier) != 0;
    k.alt = (modifiers & Qt::AltModifier) != 0;
    return t->key(k, *this);
}

void VisualToolsController::selectFamily(int family)
{
    // VideoBox::OnChangeVisual after VideoToolbar's click: the toggled
    // family again returns to the crosshair; a gesture never moves to
    // another tool.
    if (!m_railEnabled || family < 0 || family >= kFamilyCount)
        return;
    const Family chosen = static_cast<Family>(family) == m_family ? Family::Crosshair : static_cast<Family>(family);
    if (chosen == m_family)
        return;
    (void)escape();
    m_family = chosen;
    // RendererVideo::SetVisual made a new tool for the family (Visuals::Get).
    if (auto *t = tool())
        t->selected(*this);
    resetTool();
}

QString VisualToolsController::copyCoordinates(qreal x, qreal y)
{
    // VideoBox::OnCopyCoords: needs a renderer (VideoBox.cpp:1151 runs it in
    // OnAccelerator, which a video without one never reaches).
    if (!m_view.hasVideo())
        return {};
    m_copied = qs(copyCoordinatesText(m_view, m_view.toDevice(x), m_view.toDevice(y)));
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(m_copied);
    emit changed();
    return m_copied;
}

QString VisualToolsController::copyCoordinatesAtCursor(QQuickItem *area)
{
    if (m_lastPointer)
        return copyCoordinates(m_lastPointer->x(), m_lastPointer->y());
    if (!area)
        return {};
    const QPointF at = area->mapFromGlobal(QCursor::pos());
    return copyCoordinates(at.x(), at.y());
}

bool VisualToolsController::setValue(const QString &name, const QString &text)
{
    auto *t = tool();
    if (!t)
        return false;
    const bool done = t->setValue(name.toStdString(), text.toStdU16String(), *this);
    emit changed();
    return done;
}

void VisualToolsController::pickBatch()
{
    const auto *s = editingSession();
    if (!s)
        return;
    std::vector<core::LineId> picked;
    for (const auto *line : s->document().lines())
        if (s->selection().selected.contains(line->id))
            picked.push_back(line->id);
    m_picker.pick(std::move(picked));
    emit changed();
}

void VisualToolsController::clearBatch()
{
    m_picker.clear();
    emit changed();
}

LineWarning VisualToolsController::currentWarning() const
{
    const auto *s = editingSession();
    const auto active = activeLine();
    const auto frame = m_video.session().shownFrame();
    if (!s || !active || !frame)
        return LineWarning::None;
    const auto start = m_video.session().frameStart(*frame);
    if (!start)
        return LineWarning::None;
    const auto draft = s->draftRecord();
    for (const auto *line : s->document().lines())
        if (line->id == *active)
            return lineWarning(m_family, (draft && draft->id == *active) ? *draft : *line,
                               start->microseconds() / 1000);
    return LineWarning::None;
}

QString VisualToolsController::warning() const
{
    // Visuals::DrawWarning: none with VIDEO_VISUAL_WARNINGS_OFF.
    const auto *t = tool();
    if (!t || !t->warnsOutsideLine() || !m_view.hasVideo() || m_settings.boolean("video.visualWarningsOff"))
        return {};
    return qs(warningText(currentWarning()));
}

bool VisualToolsController::hideCursor() const
{
    return m_overVideo && m_view.hasVideo() && m_railEnabled && m_family == Family::Crosshair && tool();
}

QRectF VisualToolsController::videoRect() const
{
    if (!m_view.hasVideo())
        return {};
    const IntRect r = m_view.videoRect();
    return {m_view.toLogical(r.left), m_view.toLogical(r.top), m_view.toLogical(r.width()), m_view.toLogical(r.height())};
}

QRectF VisualToolsController::sourceRect() const
{
    if (!m_view.hasVideo())
        return {};
    const IntRect r = m_view.sourceRect();
    return QRectF(r.left, r.top, r.width(), r.height());
}

QVariantList VisualToolsController::overlay() const
{
    QVariantList out;
    const auto *t = tool();
    if (!t || !m_view.hasVideo())
        return out;
    const Overlay o = t->overlay(*this);
    const auto L = [this](double device) { return m_view.toLogical(device); };
    // Drawn first (T2, T3): filled shapes with a one-pixel border.
    for (const OverlayPolygon &poly : o.polygons) {
        QVariantList points;
        for (const PointF &pt : poly.points)
            points.append(QVariantList{L(pt.x), L(pt.y)});
        out.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("polygon")},
                               {QStringLiteral("points"), points},
                               {QStringLiteral("fill"), poly.fill ? colour(poly.fill) : QString()},
                               {QStringLiteral("border"), poly.border ? colour(poly.border) : QString()}});
    }
    for (const OverlayLine &l : o.lines)
        out.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("line")},
                               {QStringLiteral("x1"), L(l.from.x)}, {QStringLiteral("y1"), L(l.from.y)},
                               {QStringLiteral("x2"), L(l.to.x)}, {QStringLiteral("y2"), L(l.to.y)},
                               {QStringLiteral("width"), L(l.width)}, {QStringLiteral("color"), colour(l.argb)}});
    for (const OverlayCircle &c : o.circles)
        out.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("circle")},
                               {QStringLiteral("x"), L(c.centre.x)}, {QStringLiteral("y"), L(c.centre.y)},
                               {QStringLiteral("radius"), L(c.radius)}, {QStringLiteral("filled"), c.filled},
                               {QStringLiteral("color"), colour(c.argb)}});
    for (const OverlayText &x : o.texts)
        out.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("x"), L(x.rect.left)}, {QStringLiteral("y"), L(x.rect.top)},
                               {QStringLiteral("text"), qs(x.text)}, {QStringLiteral("outline"), x.outline},
                               {QStringLiteral("pixelSize"), x.pixelSize > 0 ? L(x.pixelSize) : labelPixelSize()},
                               {QStringLiteral("color"), colour(x.argb)}});
    return out;
}

QVariantList VisualToolsController::values() const
{
    QVariantList out;
    const auto *t = tool();
    if (!t)
        return out;
    for (const ToolValue &v : t->values(*this))
        out.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(v.name)},
                               {QStringLiteral("label"), qs(v.label)},
                               {QStringLiteral("text"), qs(v.text)},
                               {QStringLiteral("editable"), v.editable}});
    return out;
}

} // namespace hikari::ui
