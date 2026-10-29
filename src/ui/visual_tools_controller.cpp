#include "visual_tools_controller.h"

#include "hikari/application/visual_all_tags.h"

#include "video_controller.h"

#include "hikari/application/resample.h"
#include "hikari/application/visual_crosshair.h"
#include "hikari/application/visual_drawing.h"
#include "hikari/application/visual_transform.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/text_projection.h"

#include <QClipboard>
#include <QColor>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QQuickItem>
#include <QRawFont>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <utility>

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
        // The render: Visuals::Draw sets blockevents for the shown frame.
        if (auto *t = tool(); t && m_view.hasVideo() && t->warnsOutsideLine() && currentWarning() != LineWarning::None)
            block(*t);
        // The shown frame's time moves the warning. V1: while playing, every
        // player frame changes the video and none of the rest of this
        // controller's state, so a frame notifies only when it changes the
        // warning (the status bar and the panel's bindings follow `changed`).
        if (const QString now = warning(); !m_video.session().playing() || now != m_frameWarning) {
            m_frameWarning = now;
            emit changed();
        }
        // T3: a \move's position follows the shown frame (legacy Draw ran
        // DrawVisual on every frame, Visuals.cpp:504-529);
        // T5: so do a \move drawing's points (DrawingAndClip::DrawVisual).
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
                               {QStringLiteral("hotkey"), QString::fromLatin1(f.hotkey.data(), f.hotkey.size())},
                               {QStringLiteral("available"),
                                m_tools[static_cast<std::size_t>(f.family)] != nullptr}});
    return out;
}

QVariantList VisualToolsController::railOrder() const
{
    QVariantList out;
    for (const Family f : application::visual::railOrder())
        out.append(static_cast<int>(f));
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
    // T2: the Line editor's text changed (legacy EditBox::OnEdit committed
    // it and ran SetVisual, so the tool read the Line again).
    std::optional<std::pair<std::u8string, std::u8string>> draft;
    if (s && active)
        if (const auto record = s->draftRecord(); record && record->id == *active)
            draft = std::pair(record->text, record->translation);
    if (draft != m_seenDraft && !m_gesture)
        reset = true;
    m_seenDraft = draft;
    if (id != m_seenSession) {
        // Another editing target: a gesture never moves to it.
        (void)escape();
        m_picker.clear();
        reset = true;
    } else if (revision != m_seenRevision) {
        reset = true;
    }
    if (active != m_seenActive) {
        // Another active Line: the tool reads it again (legacy's SetVisual,
        // which dropped the tool's unsent preview), so an open gesture is
        // cancelled. It never moves to the new Line, and the tool's state no
        // longer belongs to the Lines it targets (T4).
        (void)escape();
        reset = true;
    }
    if (id != m_seenSession || active != m_seenActive)
        ++m_editTarget;
    // PU5: an open "Edit tags" previews Set on the active Line (or the
    // first): read again whenever the tool reads its Line again.
    if (m_tagsEditor && reset)
        m_tagsEditor->lineChanged();
    m_seenSession = id;
    m_seenRevision = revision;
    m_seenActive = active;
    if (reset)
        resetTool();
    emit changed();
}

void VisualToolsController::block(VisualTool &t)
{
    t.blocked(*this);
    if (!hasArrow())
        setPointerShape(PointerShape::Arrow);
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
    refreshOptions();
    updatePreview();
}

namespace {

// libass's ass_strcasecmp: ASCII letters only.
std::string asciiLower(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

} // namespace

void VisualToolsController::setLeasedFonts(const std::vector<std::string> &names)
{
    std::set<std::string> lowered;
    for (const std::string &name : names)
        lowered.insert(asciiLower(name));
    if (lowered == m_leasedFonts)
        return;
    m_leasedFonts = std::move(lowered);
    // The Perspective tool measures only fonts it can see: it reads its Line again.
    if (m_family == Family::Perspective && tool())
        resetTool();
}

bool VisualToolsController::fontLeased(const std::string &name) const
{
    return m_leasedFonts.contains(asciiLower(name));
}

bool VisualToolsController::fallsBackToLeased(const std::string &font, bool bold, bool italic,
                                              std::u16string_view text) const
{
    if (m_leasedFonts.empty() || text.empty())
        return false;
    // The face the measure resolves the name to, alone: QFontMetrics would
    // answer for Qt's fallbacks too.
    QFont f(QString::fromStdString(font));
    f.setBold(bold);
    f.setItalic(italic);
    const QRawFont raw = QRawFont::fromFont(f);
    if (!raw.isValid())
        return true;
    const QString string = QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
    for (const char32_t c : string.toUcs4())
        if (!raw.supportsCharacter(c))
            return true;
    return false;
}

void VisualToolsController::viewChanged()
{
    m_view.refreshToolTransform();
    auto *t = tool();
    if (t && (m_family < Family::RectangleClip || m_family > Family::Drawing))
        t->reset(*this);
    emit geometryChanged();
    emit overlayChanged();
    emit changed();
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
    // T3: the tool's own step is no reason to reset it (legacy ran no
    // SetVisual after a visual edit); refresh() sees the revision as seen.
    if (const auto *t = tool(); result && t && t->keepsStateAfterCommit())
        m_seenRevision = s->revision();
    if (changes && m_edited)
        m_edited(); // the shell refreshes; refresh() follows
    updatePreview();
    emit changed();
    return result;
}

bool VisualToolsController::escape()
{
    m_valueScrub = false;
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
    // The drag is over: the shape it set goes, as on legacy's button
    // release; the next move sets the one over a handle again.
    if (!hasArrow())
        setPointerShape(PointerShape::Arrow);
    // T2-T4: the tool reads its Lines again, as before the gesture (legacy
    // SetCurVisual), so its handles, points or corners go back to where the
    // text puts them.
    if (auto *t = tool(); t && t->family() != Family::Crosshair) {
        resetTool();
    } else {
        emit changed();
        emit overlayChanged();
    }
    updatePreview();
    return true;
}

bool VisualToolsController::escapable() const
{
    if (m_gesture)
        return true;
    const auto *t = tool();
    return t && m_view.hasVideo() && t->hasPending();
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
    refreshOptions();
    updatePreview();
}

std::int64_t VisualToolsController::videoTimeMs() const
{
    // VideoBox::Tell: the reported frame's start (the player's while playing).
    const auto frame = m_video.session().reportedFrame();
    if (!frame)
        return 0;
    const auto start = m_video.session().frameStart(*frame);
    return start ? start->microseconds() / 1000 : 0;
}

const std::vector<ShapePreset> *VisualToolsController::shapePresets() const
{
    // VideoToolbar::GetShapesSettings: LoadSettings while there are none
    // (VisualDrawingShapes.cpp:291-331): the file, read as UTF-8 (BOM
    // dropped; the Windows build's text-mode read made CRLF LF), else
    // legacy's defaults.
    if (m_shapes.empty()) {
        QFile f(m_shapesFile);
        if (!m_shapesFile.isEmpty() && f.open(QIODevice::ReadOnly)) {
            QByteArray bytes = f.readAll();
            if (bytes.startsWith("\xEF\xBB\xBF"))
                bytes.remove(0, 3);
            QString text = QString::fromUtf8(bytes);
#ifdef _WIN32
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
#endif
            m_shapes = parseShapePresets(text.toStdU16String());
        } else {
            m_shapes = defaultShapePresets();
        }
    }
    return &m_shapes;
}

void VisualToolsController::saveShapes() const
{
    // SaveSettings (VisualDrawingShapes.cpp:341-353): UTF-8 with a BOM, the
    // folder made when missing (OpenWrite).
    if (m_shapesFile.isEmpty())
        return;
    QDir().mkpath(QFileInfo(m_shapesFile).absolutePath());
    QFile f(m_shapesFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write("\xEF\xBB\xBF");
    f.write(QString::fromStdU16String(writeShapePresets(m_shapes)).toUtf8());
}

const std::vector<application::visual::AllTagsSetting> *VisualToolsController::allTagsSettings() const
{
    // VideoToolbar::GetTagsSettings: LoadSettings while there are none
    // (VisualAllTagsEdition.cpp:328-417): the file, read as UTF-8 (BOM
    // dropped; the Windows build's text-mode read made CRLF LF), else
    // legacy's defaults, which replace a file of an older version. An empty
    // list (a file with the version alone) is legacy's defaults too, the file
    // left as it is: legacy read past the end of it (T6-tags-version-only).
    if (m_allTags.empty()) {
        QFile f(m_tagsFile);
        QString text;
        if (!m_tagsFile.isEmpty() && f.open(QIODevice::ReadOnly)) {
            QByteArray bytes = f.readAll();
            if (bytes.startsWith("\xEF\xBB\xBF"))
                bytes.remove(0, 3);
            text = QString::fromUtf8(bytes);
#ifdef _WIN32
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
#endif
        }
        bool writeDefaults = false;
        m_allTags = application::visual::parseAllTags(text.toStdU16String(), &writeDefaults);
        if (writeDefaults && !m_tagsFile.isEmpty()) {
            // OpenWrite::FileWrite: UTF-8 with a BOM.
            QFile out(m_tagsFile);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.write("\xEF\xBB\xBF");
                out.write(QString::fromStdU16String(std::u16string(application::visual::defaultAllTagsText())).toUtf8());
            }
        }
        if (m_allTags.empty())
            m_allTags = application::visual::defaultAllTags();
    }
    return &m_allTags;
}

void VisualToolsController::saveAllTags() const
{
    // SaveSettings (VisualAllTagsEdition.cpp:428-446): UTF-8 with a BOM, the
    // folder made when missing (OpenWrite).
    if (m_tagsFile.isEmpty())
        return;
    QDir().mkpath(QFileInfo(m_tagsFile).absolutePath());
    QFile f(m_tagsFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write("\xEF\xBB\xBF");
    f.write(QString::fromStdU16String(application::visual::writeAllTags(m_allTags)).toUtf8());
}

std::string VisualToolsController::editorHotkey(int id) const
{
    return m_editorHotkey ? m_editorHotkey(id) : VisualHost::editorHotkey(id);
}

std::u16string VisualToolsController::activeLineText() const
{
    // OnGetShapeFromLine reads tab->edit->line: the Line as last sent
    // (Dialogue::GetTextNoCopy: its translation when it has one). Legacy's
    // editor sent each edit (EditBox::OnEdit), so the Line editor's pending
    // draft is the Line's text here.
    const auto *s = editingSession();
    const auto active = activeLine();
    if (!s || !active)
        return {};
    if (const auto draft = s->draftRecord(); draft && draft->id == *active)
        return core::toUtf16(draft->translation.empty() ? draft->text : draft->translation);
    for (const auto *line : s->document().lines())
        if (line->id == *active)
            return core::toUtf16(line->translation.empty() ? line->text : line->translation);
    return {};
}

int VisualToolsController::targetCount() const
{
    // HY1: the Lines a change edits: several picked ones less those the
    // Grid hides (AllTagsTool::beginEdit).
    const auto targets = batchTargets();
    const auto shown = application::visual::transform::severalLines(targets, activeLine())
                           ? shownLines()
                           : std::function<bool(core::LineId)>{};
    if (!shown)
        return static_cast<int>(targets.size());
    return static_cast<int>(std::ranges::count_if(targets, shown));
}

bool VisualToolsController::undoStep(std::uint64_t revision)
{
    // HY1: the tool's own last step undone, only while it is the last thing
    // that changed the Document (no gesture, no draft on it); the tool keeps
    // its state through it.
    auto *s = editingSession();
    if (!s || m_gesture || s->revision() != revision || s->draftLine() || !s->canUndo() || !s->undo())
        return false;
    m_seenRevision = s->revision();
    return true;
}

void VisualToolsController::openTagsEditor()
{
    if (m_tagsEditor) {
        // Open already: its window comes forward (or back, gone with the
        // window it opened from).
        emit tagsEditorChanged();
        return;
    }
    auto *allTags = dynamic_cast<application::visual::AllTagsTool *>(tool());
    if (!allTags)
        return;
    AllTagsEditor::Hooks hooks;
    hooks.activeLineText = [this] {
        // The preview's Line: the active one, else the first.
        if (const auto *s = editingSession(); s && !activeLine() && !s->document().lines().empty()) {
            const auto *first = s->document().lines().front();
            return core::toUtf16(first->translation.empty() ? first->text : first->translation);
        }
        return activeLineText();
    };
    hooks.changed = [this](const std::vector<application::visual::AllTagsSetting> &tags,
                           const AllTagsEditor::Renames &renamed) {
        // PU5: each change in "Edit tags" is kept at once (legacy's OK:
        // VideoToolbar::SetTagsSettings, the list's names again, its
        // selection kept by name, and the tool told, ChangeTool). The chosen
        // definition follows a rename, and Hydra takes the change while
        // another family is shown too (the window stays open).
        auto oldNames = application::visual::allTagsNames(*allTagsSettings());
        for (const auto &r : renamed)
            std::ranges::replace(oldNames, r.from, r.to);
        m_allTags = tags;
        if (m_allTags.empty())
            m_allTags = application::visual::defaultAllTags();
        saveAllTags();
        auto *hydra = dynamic_cast<application::visual::AllTagsTool *>(
            m_tools[static_cast<std::size_t>(Family::Hydra)].get());
        if (hydra)
            hydra->definitionsChanged(oldNames, *this, hydra == tool());
        refreshOptions();
        emit changed();
        emit overlayChanged();
    };
    hooks.finished = [this] {
        if (m_tagsEditor)
            m_tagsEditor->deleteLater();
        m_tagsEditor = nullptr;
        emit tagsEditorChanged();
    };
    m_tagsEditor = new AllTagsEditor(*allTagsSettings(), allTags->toggled() & 0xFFFFF, std::move(hooks), this);
    emit tagsEditorChanged();
}

void VisualToolsController::openShapeEditor()
{
    if (m_shapeEditor) {
        // Open already: its window comes forward (or back, gone with the
        // window it opened from).
        emit shapeEditorChanged();
        return;
    }
    int selection = 0;
    for (const QVariant &o : std::as_const(m_options)) {
        const QVariantMap m = o.toMap();
        if (m.value(QStringLiteral("name")).toString() == QStringLiteral("shape"))
            selection = m.value(QStringLiteral("index")).toInt();
    }
    ShapeEditor::Hooks hooks;
    hooks.activeLineText = [this] { return activeLineText(); };
    hooks.defaults = [] { return defaultShapePresets(); };
    hooks.changed = [this](const std::vector<ShapePreset> &presets, const ShapeEditor::Renames &renamed) {
        // PU5: each change in "Edit shapes" is kept at once (legacy's OK:
        // VideoToolbar::SetShapesSettings, the list's names again, its
        // selection kept (or the last preset's), and the tool told,
        // ChangeTool -> SetShape). The chosen preset follows its name when
        // others come or go before it or it is renamed, and the tool reads it
        // again (legacy's SetShape kept the shape it had when the selection
        // was the same), also while another family is shown (the window
        // stays open).
        auto *drawing = dynamic_cast<DrawingTool *>(m_tools[static_cast<std::size_t>(Family::Drawing)].get());
        int selection = drawing ? drawing->shapeSelection() : 0;
        if (selection > 0 && selection <= static_cast<int>(m_shapes.size())) {
            std::u16string chosen = m_shapes[static_cast<std::size_t>(selection - 1)].name;
            for (const auto &r : renamed)
                if (chosen == r.from)
                    chosen = r.to;
            const auto at = std::ranges::find(presets, chosen, &ShapePreset::name);
            if (at != presets.end())
                selection = static_cast<int>(at - presets.begin()) + 1;
        }
        m_shapes = presets;
        saveShapes();
        if (selection > static_cast<int>(m_shapes.size()))
            selection = static_cast<int>(m_shapes.size());
        if (drawing)
            drawing->presetsChanged(selection, *this, drawing == tool());
        refreshOptions();
        emit optionsChanged();
        emit overlayChanged();
    };
    hooks.finished = [this] {
        if (m_shapeEditor)
            m_shapeEditor->deleteLater();
        m_shapeEditor = nullptr;
        emit shapeEditorChanged();
    };
    // The chosen preset: the list's selection less "Choose" (from "Choose"
    // the first). Legacy passed the selection itself, opening on the preset
    // after the one chosen (T5-editor-opens-next).
    m_shapeEditor = new ShapeEditor(*shapePresets(), selection - 1, std::move(hooks), this);
    emit shapeEditorChanged();
}

void VisualToolsController::bell()
{
    // Legacy wxBell; counted (the shell has no audible bell to give).
    ++m_bells;
}

void VisualToolsController::notice(std::u16string_view text)
{
    if (m_log)
        m_log(qs(text));
}

QString VisualToolsController::toolName() const
{
    for (const FamilyInfo &f : application::visual::families())
        if (f.family == m_family)
            return QString::fromLatin1(f.tooltip.data(), f.tooltip.size());
    return {};
}

void VisualToolsController::refreshOptions()
{
    // Its own signal, only when the options change: the row is not rebuilt
    // on every pointer move.
    QVariantList out;
    if (const auto *t = tool())
        out = optionMaps(*t);
    if (out == m_options)
        return;
    m_options = std::move(out);
    emit optionsChanged();
}

QVariantList VisualToolsController::optionMaps(const VisualTool &t) const
{
    QVariantList out;
    for (const ToolOption &o : t.options(*this)) {
        QStringList choices;
        for (const auto &c : o.choices)
            choices.append(qs(c));
        QStringList notes;
        for (const auto &n : o.notes)
            notes.append(qs(n));
        QStringList sectionNames;
        for (const auto &n : o.sectionNames)
            sectionNames.append(qs(n));
        QVariantList sections;
        for (const int section : o.sections)
            sections.append(section);
        const QString kind = o.kind == ToolOption::Kind::Choice   ? QStringLiteral("choice")
                             : o.kind == ToolOption::Kind::Action ? QStringLiteral("action")
                             : o.kind == ToolOption::Kind::Switch ? QStringLiteral("switch")
                                                                  : QStringLiteral("toggle");
        out.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(o.name)},
                               {QStringLiteral("kind"), kind},
                               {QStringLiteral("iconRole"), QString::fromStdString(o.iconRole)},
                               {QStringLiteral("tooltip"), qs(o.tooltip)},
                               {QStringLiteral("checked"), o.checked},
                               {QStringLiteral("enabled"), o.enabled && m_railEnabled},
                               {QStringLiteral("choices"), choices},
                               {QStringLiteral("index"), o.index},
                               {QStringLiteral("listEnds"), o.listEnds},
                               {QStringLiteral("group"), qs(o.group)},
                               {QStringLiteral("searchable"), o.searchable},
                               {QStringLiteral("notes"), notes},
                               {QStringLiteral("sections"), sections},
                               {QStringLiteral("sectionNames"), sectionNames},
                               {QStringLiteral("afterValues"), o.afterValues}});
    }
    return out;
}

bool VisualToolsController::setOption(const QString &name, int value)
{
    // Legacy VideoToolbar's item click: the tool takes its toggles
    // (VideoBox.cpp:179-181, Visuals::ChangeTool); never during a gesture.
    auto *t = tool();
    if (!t || !m_railEnabled || m_gesture)
        return false;
    if (m_family == Family::Drawing && name == QStringLiteral("shape") && shapePresets() &&
        value > static_cast<int>(m_shapes.size())) {
        // The shape list's "Edit" (VectorItem::ShowContols, VideoToolbar.cpp:
        // 516-536): the editor opens; the list keeps its selection.
        openShapeEditor();
        refreshOptions();
        emit optionsChanged(); // the list shows the selection again
        return true;
    }
    if (m_family == Family::Crosshair && name == QStringLiteral("copyPosition")) {
        // VT1: the bar's Copy video position, the shown position in
        // VIDEO_COPY_COORDS' text form.
        const auto *cross = dynamic_cast<const CrosshairTool *>(t);
        if (!cross || cross->copyText().empty())
            return false;
        m_copied = qs(cross->copyText());
        if (auto *clipboard = QGuiApplication::clipboard())
            clipboard->setText(m_copied);
        emit changed();
        return true;
    }
    if (m_family == Family::Hydra && name == QStringLiteral("edit")) {
        // The all-tags row's Edit (AllTagsItem, VideoToolbar.cpp:783-797):
        // the "Tag editing" dialog on the list's selection.
        openTagsEditor();
        return true;
    }
    if (m_family == Family::Hydra && name == QStringLiteral("variant")) {
        // HY1: Set's placing is the tag definition's (AllTagsSettings.txt),
        // as the "Tag editing" dialog's Mode.
        auto *allTags = dynamic_cast<application::visual::AllTagsTool *>(t);
        const auto at = allTags ? static_cast<std::size_t>(allTags->currentTag()) : 0;
        if (allTags && allTags->mode() == application::visual::PasteInsert && allTagsSettings() &&
            at < m_allTags.size() && value >= 0 && value <= 2) {
            // PU5: an open Edit tags takes it as one of its steps (and
            // hands the definitions back), else the window would put the
            // old placing back with its next change.
            if (m_tagsEditor) {
                m_tagsEditor->setPlacingOf(QString::fromStdU16String(m_allTags[at].name), value);
            } else {
                m_allTags[at].mode = static_cast<unsigned char>(value);
                saveAllTags();
            }
        }
    }
    const bool done = t->setOption(name.toStdString(), value, *this);
    emit changed();
    emit overlayChanged();
    refreshOptions();
    return done;
}

bool VisualToolsController::previewDocument(const core::Document &document, core::Document &out) const
{
    // Legacy's dummy render: the edited Lines' texts as the gesture staged
    // them (Visuals::RenderSubs), then the tool's Lines (AppendClipMask).
    const auto *t = tool();
    std::vector<core::LineRecord> extra;
    if (t && m_view.hasVideo())
        extra = t->previewLines(*this);
    const bool staged = m_gesture && m_gesture->hasChanges();
    if (!staged && extra.empty())
        return false;
    // T2: only the Lines the frame shows while a gesture has staged texts,
    // as legacy's GetVisible sent: a long script is not reparsed on every
    // pointer move.
    out = staged ? m_gesture->preview(document, videoTimeMs(), m_video.session().playing()) : document;
    for (auto &line : extra)
        (void)out.appendLine(std::move(line));
    return true;
}

std::vector<std::byte> VisualToolsController::subtitles(const core::Document &document, const Encoder &encode) const
{
    core::Document preview;
    const core::Document &shown = previewDocument(document, preview) ? preview : document;
    return encode ? encode(shown) : core::encodeAss(shown);
}

void VisualToolsController::updatePreview()
{
    // The preview is asked for again only when what it adds changed (each
    // pointer move redraws the overlay, not the subtitles).
    std::u8string key;
    if (m_gesture)
        for (const auto &id : m_gesture->targets())
            for (const bool translation : {false, true})
                if (const auto staged = m_gesture->staged(id, translation))
                    key += *staged + u8'\x1f';
    key += u8'\x1e';
    if (const auto *t = tool(); t && m_view.hasVideo())
        for (const auto &line : t->previewLines(*this))
            key += line.text + u8'\x1f';
    if (key == m_previewKey)
        return;
    m_previewKey = std::move(key);
    const auto *s = editingSession();
    if (!m_preview || !s)
        return;
    core::Document preview;
    if (previewDocument(s->document(), preview))
        m_preview(&preview);
    else
        m_preview(nullptr);
}


application::LegacyTimebase VisualToolsController::timebase() const
{
    return m_video.session().legacyTimebase();
}

void VisualToolsController::log(std::u16string_view text)
{
    if (m_log)
        m_log(qs(text));
}

std::pair<long, long> VisualToolsController::editorSelection() const
{
    return m_editorSelection ? m_editorSelection() : std::pair<long, long>{0, 0};
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
    // The crosshair's hidden pointer is the picture's only, where the cross
    // is drawn (VideoView::onPicture): over the letterbox or pillarbox bars
    // the pointer is the arrow. Legacy hid it over the whole video window,
    // bars included (VideoBox.cpp:607-611), until a8a428e4c.
    if (p.kind == Pointer::Kind::Leave) {
        m_overPicture = false;
        m_lastPointer.reset();
    } else {
        m_overPicture = m_view.onPicture(p.x, p.y);
        m_lastPointer = QPointF(x, y);
    }
    // VideoBox::OnMouseEvent: nothing without a video (GetState() == None).
    auto *t = tool();
    if (!t || !m_view.hasVideo())
        return;
    // Visuals::Draw's blockevents: a tool other than the crosshair takes no
    // events outside its Line's time or on a comment, warning shown or not.
    if (t->warnsOutsideLine() && currentWarning() != LineWarning::None) {
        const PointerShape shape = pointerShape();
        block(*t);
        if (m_gesture && p.kind == Pointer::Kind::Release)
            (void)escape(); // legacy released the mouse capture
        if (pointerShape() != shape)
            emit changed();
        return;
    }
    t->pointer(p, *this);
    emit changed();
}

namespace {

Key keyOf(int key, int modifiers)
{
    Key k;
    k.key = key;
    k.control = (modifiers & Qt::ControlModifier) != 0;
    k.shift = (modifiers & Qt::ShiftModifier) != 0;
    k.alt = (modifiers & Qt::AltModifier) != 0;
    return k;
}

} // namespace

bool VisualToolsController::key(int key, int modifiers, bool release, bool autoRepeat)
{
    auto *t = tool();
    if (!t || !m_view.hasVideo())
        return false;
    // VideoBox::OnKeyPress hands the keys to the tool whether or not its
    // events are blocked (VideoBox.cpp:666-668; blockevents is the pointer's).
    Key k = keyOf(key, modifiers);
    k.release = release;
    k.autoRepeat = autoRepeat;
    return t->key(k, *this);
}

bool VisualToolsController::takesKey(int key, int modifiers) const
{
    const auto *t = tool();
    return t && m_view.hasVideo() && t->takesKey(keyOf(key, modifiers));
}

bool VisualToolsController::runHotkey(const QString &action)
{
    const auto family = application::visual::familyOfHotkey(action.toStdString());
    if (!family)
        return false;
    // VT2: the command toggles as the rail does, so the on tool's key goes
    // back to the crosshair (the crosshair's stays on it).
    selectFamily(static_cast<int>(*family));
    return true;
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
    // VideoBox::OnChangeVisual: the arrow back for the new tool.
    if (!hasArrow())
        setPointerShape(PointerShape::Arrow);
    // Visuals::Get made a new tool for the family (RendererVideo::SetVisual).
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

bool VisualToolsController::editValue(VisualTool &t, const QString &name, const QString &text, ValueEdit edit)
{
    // VT1: a value step the session refuses (a blocked draft, a read-only
    // Document), at the first sample or the release, leaves no gesture: the
    // tool reads its Lines again, its handles and fields back at the text's.
    const auto refusedBefore = std::exchange(m_lastRefusal, std::nullopt);
    const bool done = t.setValue(name.toStdString(), text.toStdU16String(), *this, edit);
    if (!done && m_lastRefusal && !m_gesture && t.family() != Family::Crosshair)
        resetTool();
    if (!m_lastRefusal)
        m_lastRefusal = refusedBefore;
    return done;
}

bool VisualToolsController::setValue(const QString &name, const QString &text)
{
    // VT1: a typed value (Enter) or a scrub's release: one history step.
    auto *t = tool();
    if (!t || !m_railEnabled)
        return false;
    const bool done = editValue(*t, name, text, ValueEdit::Commit);
    // A scrub's release the tool refuses (a value out of its range) leaves
    // nothing staged behind: its gesture is dropped, as by Esc.
    const bool scrubbed = std::exchange(m_valueScrub, false);
    if (!done && scrubbed && m_gesture)
        escape();
    emit changed();
    emit overlayChanged();
    return done;
}

bool VisualToolsController::scrubValue(const QString &name, const QString &text)
{
    // VT1: a scrub's sample, staged on the gesture its first sample opens
    // (shown on the video); the release commits it, Esc drops it.
    auto *t = tool();
    if (!t || !m_railEnabled)
        return false;
    const bool done = editValue(*t, name, text, ValueEdit::Sample);
    m_valueScrub = m_valueScrub || (done && m_gesture);
    emit changed();
    emit overlayChanged();
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
    ++m_editTarget;
    if (auto *t = tool())
        t->targetsChanged(*this);
    refreshOptions();
    emit changed();
    emit overlayChanged();
}

void VisualToolsController::clearBatch()
{
    m_picker.clear();
    ++m_editTarget;
    if (auto *t = tool())
        t->targetsChanged(*this);
    refreshOptions();
    emit changed();
    emit overlayChanged();
}

bool VisualToolsController::markValue(const QString &name)
{
    auto *t = tool();
    if (!t || !t->markValue(name.toStdString(), *this))
        return false;
    emit changed();
    return true;
}

QVariantMap VisualToolsController::lastChange() const
{
    const auto *t = tool();
    const auto last = t ? t->lastChange(*this) : std::nullopt;
    if (!last)
        return {};
    QVariantList values;
    for (const ToolValue &v : last->values)
        values.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(v.name)},
                                  {QStringLiteral("label"), qs(v.label)},
                                  {QStringLiteral("text"), qs(v.text)},
                                  {QStringLiteral("group"), qs(v.group)},
                                  {QStringLiteral("unit"), qs(v.unit)},
                                  {QStringLiteral("step"), v.step},
                                  {QStringLiteral("swatch"), v.swatch ? QColor::fromRgba(v.swatch).name() : QString()}});
    return QVariantMap{{QStringLiteral("tag"), qs(last->tag)},
                       {QStringLiteral("change"), last->change},
                       {QStringLiteral("lines"), last->lines},
                       {QStringLiteral("values"), values}};
}

bool VisualToolsController::editLastChange(const QString &name, const QString &text)
{
    // HY1: the card's value: the last step undone and done again with it,
    // so the history still has one step for it.
    auto *t = tool();
    const auto *s = editingSession();
    if (!t || !s || !m_railEnabled || m_gesture)
        return false;
    const std::uint64_t before = s->revision();
    const bool done = t->editLastChange(name.toStdString(), text.toStdU16String(), *this);
    // A redone change refreshed the shell as it committed; an undo whose
    // change could not be done again has not.
    if (const auto *now = editingSession(); !done && now && now->revision() != before && m_edited)
        m_edited();
    updatePreview();
    emit changed();
    emit overlayChanged();
    return done;
}

void VisualToolsController::dismissLastChange()
{
    if (auto *t = tool()) {
        t->dismissLastChange();
        emit changed();
    }
}

LineWarning VisualToolsController::currentWarning() const
{
    const auto *s = editingSession();
    if (const auto *t = tool(); t && m_view.hasVideo())
        if (const auto own = t->warning(*this))
            return *own;
    const auto active = activeLine();
    const auto frame = m_video.session().reportedFrame();
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

int VisualToolsController::pointerShapeCursor() const
{
    // wx's stock cursors as Qt's: SIZING is the four-way move (IDC_SIZEALL
    // on Windows, legacy's platform).
    if (!tool() || !m_view.hasVideo() || m_view.zoomMode())
        return Qt::ArrowCursor;
    switch (pointerShape()) {
    case PointerShape::SizeWE:
        return Qt::SizeHorCursor;
    case PointerShape::SizeNS:
        return Qt::SizeVerCursor;
    case PointerShape::SizeNWSE:
        return Qt::SizeFDiagCursor;
    case PointerShape::SizeNESW:
        return Qt::SizeBDiagCursor;
    case PointerShape::Sizing:
        return Qt::SizeAllCursor;
    case PointerShape::Arrow:
        break;
    }
    return Qt::ArrowCursor;
}

bool VisualToolsController::hideCursor() const
{
    // V4: the zoom mode shows the pointer (ZoomMouseHandle's cursors).
    return !m_view.zoomMode() && m_overPicture && m_view.hasVideo() && m_railEnabled && m_family == Family::Crosshair && tool();
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
    // V4: the tools are not drawn in the zoom mode (RendererFFMS2.cpp:392,
    // `m_Visual && !m_HasZoom`).
    if (!t || !m_view.hasVideo() || m_view.zoomMode())
        return out;
    // Visuals::Draw draws the warning instead of the tool.
    if (t->warnsOutsideLine() && currentWarning() != LineWarning::None)
        return out;
    const Overlay o = t->overlay(*this);
    const auto L = [this](double device) { return m_view.toLogical(device); };
    // Filled shapes (T2-T4), before the lines, or after them (T4: `above`);
    // a fill or border of 0 is not drawn (T3).
    const auto polygon = [&](const OverlayPolygon &p) {
        QVariantList contours;
        const auto contour = [&](const std::vector<PointF> &c) {
            QVariantList points;
            for (const PointF &pt : c)
                points.append(QPointF(L(pt.x), L(pt.y)));
            contours.append(QVariant(points));
        };
        contour(p.points);
        for (const auto &c : p.more)
            contour(c);
        out.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("polygon")},
                               {QStringLiteral("contours"), contours},
                               {QStringLiteral("fill"), p.fill ? colour(p.fill) : QString()},
                               {QStringLiteral("border"), p.border ? colour(p.border) : QString()}});
    };
    for (const OverlayPolygon &p : o.polygons)
        if (!p.above)
            polygon(p);
    // SV1-hidpi-tool-sizes: a line's width is legacy's pixels, in the
    // window's units (as the borders' and the labels'), at any scale.
    for (const OverlayLine &l : o.lines)
        out.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("line")},
                               {QStringLiteral("x1"), L(l.from.x)}, {QStringLiteral("y1"), L(l.from.y)},
                               {QStringLiteral("x2"), L(l.to.x)}, {QStringLiteral("y2"), L(l.to.y)},
                               {QStringLiteral("width"), l.width}, {QStringLiteral("color"), colour(l.argb)}});
    for (const OverlayCircle &c : o.circles)
        out.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("circle")},
                               {QStringLiteral("x"), L(c.centre.x)}, {QStringLiteral("y"), L(c.centre.y)},
                               {QStringLiteral("radius"), L(c.radius)}, {QStringLiteral("filled"), c.filled},
                               {QStringLiteral("color"), colour(c.argb)}});
    for (const OverlayPolygon &p : o.polygons)
        if (p.above)
            polygon(p);
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
    const auto *t = tool();
    return t ? valueMaps(*t) : QVariantList();
}

QVariantList VisualToolsController::valueMaps(const VisualTool &t) const
{
    // HY1: "marked" only for a tool that marks one of its values.
    QVariantList out;
    const auto values = t.values(*this);
    const bool marks = std::ranges::any_of(values, [](const ToolValue &v) { return v.marked; });
    for (const ToolValue &v : values)
        out.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(v.name)},
                               {QStringLiteral("label"), qs(v.label)},
                               {QStringLiteral("text"), qs(v.text)},
                               {QStringLiteral("editable"), v.editable},
                               {QStringLiteral("group"), qs(v.group)},
                               {QStringLiteral("unit"), qs(v.unit)},
                               {QStringLiteral("step"), v.step},
                               {QStringLiteral("swatch"), v.swatch ? QColor::fromRgba(v.swatch).name() : QString()},
                               {QStringLiteral("marked"), marks ? QVariant(v.marked) : QVariant()}});
    return out;
}

} // namespace hikari::ui
