#include "visual_tools_controller.h"

#include "hikari/application/visual_all_tags.h"

#include "command_names.h"
#include "list_files.h"
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

}

VisualToolsController::VisualToolsController(VideoController &video, SettingsStore &settings, SessionProvider session,
                                             QObject *parent)
    : QObject(parent), m_video(video), m_settings(settings), m_session(std::move(session))
{
    for (int i = 0; i < kFamilyCount; ++i)
        m_tools.push_back(makeVisualTool(static_cast<Family>(i)));
    const QString family = m_settings.value(QStringLiteral("program.font")).toString();
    m_labelFont = QGuiApplication::font();
    if (!family.isEmpty())
        m_labelFont.setFamily(family);
    m_labelFont.setPointSizeF(m_settings.integer("program.fontSize") + 4);
    m_labelFont.setBold(true);
    connect(&m_video, &VideoController::changed, this, [this] {
        syncGeometry();
        if (auto *t = tool(); t && m_view.hasVideo() && t->warnsOutsideLine() && currentWarning() != LineWarning::None)
            block(*t);
        if (const QString now = warning(); !m_video.session().playing() || now != m_frameWarning) {
            m_frameWarning = now;
            emit changed();
        }
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
    const auto *s = editingSession();
    if (!s || s->document().format() != core::SubtitleFormat::Ass)
        return nullptr;
    return m_tools[static_cast<std::size_t>(m_family)].get();
}

QVariantList VisualToolsController::families() const
{
    QVariantList out;
    for (const FamilyInfo &f : application::visual::families())
        out.append(QVariantMap{{QStringLiteral("name"), visualToolName(f.tooltip)},
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
    m_view.setClient(m_view.toDevice(width), m_view.toDevice(height + panelHeight), m_view.toDevice(panelHeight));
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
    std::optional<std::pair<std::u8string, std::u8string>> draft;
    if (s && active)
        if (const auto record = s->draftRecord(); record && record->id == *active)
            draft = std::pair(record->text, record->translation);
    if (draft != m_seenDraft && !m_gesture)
        reset = true;
    m_seenDraft = draft;
    if (id != m_seenSession) {
        (void)escape();
        m_picker.clear();
        reset = true;
    } else if (revision != m_seenRevision) {
        reset = true;
    }
    if (active != m_seenActive) {
        (void)escape();
        reset = true;
    }
    if (id != m_seenSession || active != m_seenActive)
        ++m_editTarget;
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
    m_view.refreshToolTransform();
    if (auto *t = tool())
        t->reset(*this);
    emit changed();
    emit overlayChanged();
    refreshOptions();
    updatePreview();
}

namespace {

// Matches libass's ass_strcasecmp, which folds ASCII letters only.
std::string asciiLower(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

}

void VisualToolsController::setLeasedFonts(const std::vector<std::string> &names)
{
    std::set<std::string> lowered;
    for (const std::string &name : names)
        lowered.insert(asciiLower(name));
    if (lowered == m_leasedFonts)
        return;
    m_leasedFonts = std::move(lowered);
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
    // QRawFont, not QFontMetrics: the metrics would answer for Qt's fallback fonts too.
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
    // MUST KILL: relies on RectangleClip..Drawing being contiguous in Family
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
    if (const auto *t = tool(); result && t && t->keepsStateAfterCommit())
        m_seenRevision = s->revision();
    if (changes && m_edited)
        m_edited();
    updatePreview();
    emit changed();
    return result;
}

bool VisualToolsController::escape()
{
    m_valueScrub = false;
    if (!m_gesture) {
        auto *t = tool();
        if (t && m_view.hasVideo() && t->cancelPending(*this)) {
            emit changed();
            emit overlayChanged();
            return true;
        }
        return false;
    }
    m_gesture.reset();
    if (!hasArrow())
        setPointerShape(PointerShape::Arrow);
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
    const auto frame = m_video.session().reportedFrame();
    if (!frame)
        return 0;
    const auto start = m_video.session().frameStart(*frame);
    return start ? start->microseconds() / 1000 : 0;
}

const std::vector<ShapePreset> *VisualToolsController::shapePresets() const
{
    if (m_shapes.empty()) {
        std::optional<toml::array> items;
        if (m_shapesFile)
            items = m_shapesFile->read();
        if (items)
            m_shapes = std::get<std::vector<ShapePreset>>(shapesFromToml(*items));
        else
            m_shapes = defaultShapePresets();
    }
    return &m_shapes;
}

void VisualToolsController::saveShapes() const
{
    if (m_shapesFile)
        m_shapesFile->write(shapesToToml(m_shapes));
}

const std::vector<application::visual::AllTagsSetting> *VisualToolsController::allTagsSettings() const
{
    if (m_allTags.empty()) {
        if (const auto items = m_tagsFile ? m_tagsFile->read() : std::nullopt)
            m_allTags = std::get<std::vector<application::visual::AllTagsSetting>>(tagsFromToml(*items));
        if (m_allTags.empty())
            m_allTags = application::visual::defaultAllTags();
    }
    return &m_allTags;
}

void VisualToolsController::saveAllTags() const
{
    if (m_tagsFile)
        m_tagsFile->write(tagsToToml(m_allTags));
}

std::string VisualToolsController::editorHotkey(int id) const
{
    return m_editorHotkey ? m_editorHotkey(id) : VisualHost::editorHotkey(id);
}

std::u16string VisualToolsController::activeLineText() const
{
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
    auto *s = editingSession();
    if (!s || m_gesture || s->revision() != revision || s->draftLine() || !s->canUndo() || !s->undo())
        return false;
    m_seenRevision = s->revision();
    return true;
}

void VisualToolsController::openTagsEditor()
{
    if (m_tagsEditor) {
        emit tagsEditorChanged();
        return;
    }
    auto *allTags = dynamic_cast<application::visual::AllTagsTool *>(tool());
    if (!allTags)
        return;
    AllTagsEditor::Hooks hooks;
    hooks.activeLineText = [this] {
        if (const auto *s = editingSession(); s && !activeLine() && !s->document().lines().empty()) {
            const auto *first = s->document().lines().front();
            return core::toUtf16(first->translation.empty() ? first->text : first->translation);
        }
        return activeLineText();
    };
    hooks.changed = [this](const std::vector<application::visual::AllTagsSetting> &tags,
                           const AllTagsEditor::Renames &renamed) {
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
    m_shapeEditor = new ShapeEditor(*shapePresets(), selection - 1, std::move(hooks), this);
    emit shapeEditorChanged();
}

void VisualToolsController::bell()
{
    ++m_bells;
}

void VisualToolsController::notice(std::u16string_view text)
{
    if (m_log)
        m_log(visualToolText(text));
}

QString VisualToolsController::toolName() const
{
    for (const FamilyInfo &f : application::visual::families())
        if (f.family == m_family)
            return visualToolName(f.tooltip);
    return {};
}

void VisualToolsController::retranslate()
{
    emit familiesChanged();
    m_options.clear();
    refreshOptions();
    emit changed();
}

void VisualToolsController::refreshOptions()
{
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
        for (std::size_t i = 0; i < o.choices.size(); ++i) {
            const bool named = o.searchable || (o.listEnds && i > 0 && i + 1 < o.choices.size());
            choices.append(named ? qs(o.choices[i]) : visualToolText(o.choices[i]));
        }
        QStringList notes;
        for (const auto &n : o.notes)
            notes.append(qs(n));
        QStringList sectionNames;
        for (const auto &n : o.sectionNames)
            sectionNames.append(visualToolText(n));
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
                               {QStringLiteral("tooltip"), visualToolText(o.tooltip)},
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
    auto *t = tool();
    if (!t || !m_railEnabled || m_gesture)
        return false;
    // MUST KILL: the shape choice's index 0 is "Choose", 1..n the presets and n+1 "Edit"
    if (m_family == Family::Drawing && name == QStringLiteral("shape") && shapePresets() &&
        value > static_cast<int>(m_shapes.size())) {
        openShapeEditor();
        refreshOptions();
        emit optionsChanged();
        return true;
    }
    if (m_family == Family::Crosshair && name == QStringLiteral("copyPosition")) {
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
        openTagsEditor();
        return true;
    }
    if (m_family == Family::Hydra && name == QStringLiteral("variant")) {
        auto *allTags = dynamic_cast<application::visual::AllTagsTool *>(t);
        const auto at = allTags ? static_cast<std::size_t>(allTags->currentTag()) : 0;
        if (allTags && allTags->mode() == application::visual::PasteInsert && allTagsSettings() &&
            at < m_allTags.size() && value >= 0 && value <= 2) {
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
    const auto *t = tool();
    std::vector<core::LineRecord> extra;
    if (t && m_view.hasVideo())
        extra = t->previewLines(*this);
    const bool staged = m_gesture && m_gesture->hasChanges();
    if (!staged && extra.empty())
        return false;
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
        m_log(visualToolText(text));
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
    if (p.kind == Pointer::Kind::Leave) {
        m_overPicture = false;
        m_lastPointer.reset();
    } else {
        m_overPicture = m_view.onPicture(p.x, p.y);
        m_lastPointer = QPointF(x, y);
    }
    auto *t = tool();
    if (!t || !m_view.hasVideo())
        return;
    if (t->warnsOutsideLine() && currentWarning() != LineWarning::None) {
        const PointerShape shape = pointerShape();
        block(*t);
        if (m_gesture && p.kind == Pointer::Kind::Release)
            (void)escape();
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

}

bool VisualToolsController::key(int key, int modifiers, bool release, bool autoRepeat)
{
    auto *t = tool();
    if (!t || !m_view.hasVideo())
        return false;
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
    selectFamily(static_cast<int>(*family));
    return true;
}

void VisualToolsController::selectFamily(int family)
{
    if (!m_railEnabled || family < 0 || family >= kFamilyCount)
        return;
    const Family chosen = static_cast<Family>(family) == m_family ? Family::Crosshair : static_cast<Family>(family);
    if (chosen == m_family)
        return;
    (void)escape();
    m_family = chosen;
    if (!hasArrow())
        setPointerShape(PointerShape::Arrow);
    if (auto *t = tool())
        t->selected(*this);
    resetTool();
}

QString VisualToolsController::copyCoordinates(qreal x, qreal y)
{
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
    auto *t = tool();
    if (!t || !m_railEnabled)
        return false;
    const bool done = editValue(*t, name, text, ValueEdit::Commit);
    const bool scrubbed = std::exchange(m_valueScrub, false);
    if (!done && scrubbed && m_gesture)
        escape();
    emit changed();
    emit overlayChanged();
    return done;
}

bool VisualToolsController::scrubValue(const QString &name, const QString &text)
{
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
                                  {QStringLiteral("label"), visualToolText(v.label)},
                                  {QStringLiteral("text"), qs(v.text)},
                                  {QStringLiteral("group"), qs(v.group)},
                                  {QStringLiteral("unit"), visualToolText(v.unit)},
                                  {QStringLiteral("step"), v.step},
                                  {QStringLiteral("swatch"), v.swatch ? QColor::fromRgba(v.swatch).name() : QString()}});
    return QVariantMap{{QStringLiteral("tag"), qs(last->tag)},
                       {QStringLiteral("change"), last->change},
                       {QStringLiteral("lines"), last->lines},
                       {QStringLiteral("values"), values}};
}

bool VisualToolsController::editLastChange(const QString &name, const QString &text)
{
    auto *t = tool();
    const auto *s = editingSession();
    if (!t || !s || !m_railEnabled || m_gesture)
        return false;
    const std::uint64_t before = s->revision();
    const bool done = t->editLastChange(name.toStdString(), text.toStdU16String(), *this);
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
    const auto *t = tool();
    if (!t || !t->warnsOutsideLine() || !m_view.hasVideo() || m_settings.boolean("video.visualWarningsOff"))
        return {};
    return visualToolText(warningText(currentWarning()));
}

int VisualToolsController::pointerShapeCursor() const
{
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
    if (!t || !m_view.hasVideo() || m_view.zoomMode())
        return out;
    if (t->warnsOutsideLine() && currentWarning() != LineWarning::None)
        return out;
    const Overlay o = t->overlay(*this);
    const auto L = [this](double device) { return m_view.toLogical(device); };
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
    QVariantList out;
    const auto values = t.values(*this);
    const bool marks = std::ranges::any_of(values, [](const ToolValue &v) { return v.marked; });
    for (const ToolValue &v : values)
        out.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(v.name)},
                               {QStringLiteral("label"), visualToolText(v.label)},
                               {QStringLiteral("text"), qs(v.text)},
                               {QStringLiteral("editable"), v.editable},
                               {QStringLiteral("group"), qs(v.group)},
                               {QStringLiteral("unit"), visualToolText(v.unit)},
                               {QStringLiteral("step"), v.step},
                               {QStringLiteral("swatch"), v.swatch ? QColor::fromRgba(v.swatch).name() : QString()},
                               {QStringLiteral("marked"), marks ? QVariant(v.marked) : QVariant()}});
    return out;
}

}
