#pragma once

#include "hikari/application/all_tags.h"
#include "hikari/application/shape_presets.h"
#include "hikari/application/visual_tools.h"
#include "automation_services_qt.h"
#include "settings_store.h"
#include "all_tags_editor.h"
#include "shape_editor.h"

#include <QFont>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <optional>
#include <set>

class QQuickItem;

namespace hikari::ui {

class ListFile;
class VideoController;

class VisualToolsController : public QObject, public application::visual::VisualHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(QVariantList families READ families CONSTANT)
    Q_PROPERTY(QVariantList railOrder READ railOrder CONSTANT)
    Q_PROPERTY(int activeFamily READ activeFamily NOTIFY changed)
    Q_PROPERTY(bool railEnabled READ railEnabled NOTIFY changed)
    Q_PROPERTY(QVariantList overlay READ overlay NOTIFY overlayChanged)
    Q_PROPERTY(QString labelFamily READ labelFamily CONSTANT)
    Q_PROPERTY(int labelPixelSize READ labelPixelSize NOTIFY geometryChanged)
    Q_PROPERTY(bool hideCursor READ hideCursor NOTIFY changed)
    Q_PROPERTY(int pointerShape READ pointerShapeCursor NOTIFY changed)
    Q_PROPERTY(QString warning READ warning NOTIFY changed)
    Q_PROPERTY(QRectF videoRect READ videoRect NOTIFY geometryChanged)
    Q_PROPERTY(QRectF sourceRect READ sourceRect NOTIFY geometryChanged)
    Q_PROPERTY(QVariantList values READ values NOTIFY changed)
    Q_PROPERTY(bool escapable READ escapable NOTIFY changed)
    Q_PROPERTY(int batchCount READ batchCount NOTIFY changed)
    Q_PROPERTY(int targetCount READ targetCount NOTIFY changed)
    Q_PROPERTY(QVariantMap lastChange READ lastChange NOTIFY changed)
    Q_PROPERTY(bool gestureActive READ gestureActive NOTIFY changed)
    // MUST KILL: editTarget is a change counter, not a target
    Q_PROPERTY(int editTarget READ editTarget NOTIFY changed)
    Q_PROPERTY(QString copied READ copied NOTIFY changed)
    Q_PROPERTY(QVariantList options READ options NOTIFY optionsChanged)
    Q_PROPERTY(hikari::ui::ShapeEditor *shapeEditor READ shapeEditor NOTIFY shapeEditorChanged)
    Q_PROPERTY(hikari::ui::AllTagsEditor *tagsEditor READ tagsEditor NOTIFY tagsEditorChanged)
public:
    using SessionProvider = std::function<application::EditSession *()>;
    VisualToolsController(VideoController &video, SettingsStore &settings, SessionProvider session,
                          QObject *parent = nullptr);
    ~VisualToolsController() override;

    void setEdited(std::function<void()> edited) { m_edited = std::move(edited); }
    void setPreview(std::function<void(const core::Document *)> preview) { m_preview = std::move(preview); }
    void setLog(std::function<void(const QString &)> log) { m_log = std::move(log); }
    void setIgnoreFiltered(std::function<bool()> ignore) { m_ignoreFiltered = std::move(ignore); }
    void setLeasedFonts(const std::vector<std::string> &names);
    void setEditorSelection(std::function<std::pair<long, long>()> selection)
    {
        m_editorSelection = std::move(selection);
    }
    void setEditorSelectionPlacer(std::function<void(long, long)> place) { m_placeSelection = std::move(place); }
    void refresh();
    void setShapesFile(std::shared_ptr<ListFile> file) { m_shapesFile = std::move(file); }
    const std::shared_ptr<ListFile> &shapesFile() const { return m_shapesFile; }
    ShapeEditor *shapeEditor() const { return m_shapeEditor; }
    void setAllTagsFile(std::shared_ptr<ListFile> file) { m_tagsFile = std::move(file); }
    const std::shared_ptr<ListFile> &allTagsFile() const { return m_tagsFile; }
    AllTagsEditor *tagsEditor() const { return m_tagsEditor; }
    void setShownLines(std::function<std::function<bool(core::LineId)>()> shown) { m_shownLines = std::move(shown); }
    void setEditorHotkey(std::function<std::string(int)> accel) { m_editorHotkey = std::move(accel); }
    void setTool(application::visual::Family family, std::unique_ptr<application::visual::VisualTool> tool);
    application::visual::VisualTool *tool() const;
    application::visual::BatchPicker &batchPicker() { return m_picker; }

    QVariantList families() const;
    QVariantList railOrder() const;
    int activeFamily() const { return static_cast<int>(m_family); }
    bool railEnabled() const { return m_railEnabled; }
    QVariantList overlay() const;
    QString labelFamily() const;
    int labelPixelSize() const;
    bool hideCursor() const;
    int pointerShapeCursor() const;
    QString warning() const;
    QRectF videoRect() const;
    QRectF sourceRect() const;
    QVariantList values() const;
    bool escapable() const;
    int batchCount() const { return static_cast<int>(m_picker.picked().size()); }
    int targetCount() const;
    QVariantMap lastChange() const;
    bool gestureActive() const { return m_gesture.has_value(); }
    int editTarget() const { return m_editTarget; }
    QString copied() const { return m_copied; }
    QVariantList options() const { return m_options; }
    QString toolName() const;
    int bells() const { return m_bells; }

    Q_INVOKABLE void setViewport(qreal width, qreal height, qreal panelHeight, qreal devicePixelRatio);
    // MUST KILL: kind is a magic int (0 enter, 1 leave, 2 move, 3 press, 4 release, 5 wheel, 6 double-click press)
    Q_INVOKABLE void pointer(int kind, qreal x, qreal y, int button, int buttons, int modifiers, int wheelSteps = 0);
    Q_INVOKABLE bool key(int key, int modifiers, bool release, bool autoRepeat);
    Q_INVOKABLE bool takesKey(int key, int modifiers) const;
    Q_INVOKABLE bool escape();
    Q_INVOKABLE void selectFamily(int family);
    Q_INVOKABLE bool runHotkey(const QString &action);
    Q_INVOKABLE QString copyCoordinates(qreal x, qreal y);
    Q_INVOKABLE QString copyCoordinatesAtCursor(QQuickItem *area);
    Q_INVOKABLE bool setValue(const QString &name, const QString &text);
    Q_INVOKABLE bool scrubValue(const QString &name, const QString &text);
    Q_INVOKABLE void pickBatch();
    Q_INVOKABLE void clearBatch();
    Q_INVOKABLE bool markValue(const QString &name);
    Q_INVOKABLE bool editLastChange(const QString &name, const QString &text);
    Q_INVOKABLE void dismissLastChange();
    // MUST KILL: value is a toggle's 0/1, a choice's index, or 1 for an action
    Q_INVOKABLE bool setOption(const QString &name, int value);

    using Encoder = std::function<std::vector<std::byte>(const core::Document &)>;
    std::vector<std::byte> subtitles(const core::Document &document, const Encoder &encode = {}) const;

    const application::visual::VideoView &view() const override { return m_view; }
    const application::EditSession *session() const override { return m_session ? m_session() : nullptr; }
    std::optional<core::LineId> activeLine() const override;
    std::vector<core::LineId> batchTargets() const override;
    std::expected<application::visual::Gesture *, application::CommandRefusal>
    beginGesture(std::vector<core::LineId> targets, std::string history) override;
    application::visual::Gesture *gesture() override { return m_gesture ? &*m_gesture : nullptr; }
    std::expected<void, application::CommandRefusal> commitGesture() override;
    void cancelGesture() override { (void)escape(); }
    std::pair<int, int> measureLabel(std::u16string_view text) const override;
    void toolChanged() override;
    std::int64_t videoTimeMs() const override;
    application::LegacyTimebase timebase() const override;
    application::TextMeasurePort *textMeasure() const override { return &m_measure; }
    bool ignoreFiltered() const override { return m_ignoreFiltered && m_ignoreFiltered(); }
    bool fontLeased(const std::string &name) const override;
    bool fallsBackToLeased(const std::string &font, bool bold, bool italic, std::u16string_view text) const override;
    void log(std::u16string_view text) override;
    std::pair<long, long> editorSelection() const override;
    void setEditorSelection(long from, long to) override
    {
        if (m_placeSelection)
            m_placeSelection(from, to);
    }
    void bell() override;
    void notice(std::u16string_view text) override;
    const std::vector<application::visual::ShapePreset> *shapePresets() const override;
    std::function<bool(core::LineId)> shownLines() const override
    {
        return m_shownLines ? m_shownLines() : std::function<bool(core::LineId)>{};
    }
    const std::vector<application::visual::AllTagsSetting> *allTagsSettings() const override;
    std::string editorHotkey(int id) const override;
    bool undoStep(std::uint64_t revision) override;

    application::visual::VideoView &videoView() { return m_view; }
    const application::visual::VideoView &videoView() const { return m_view; }
    bool hasNonDefaultTool() const { return tool() && m_family != application::visual::Family::Crosshair; }
    void viewChanged();
    std::optional<application::CommandRefusal> lastRefusal() const { return m_lastRefusal; }

signals:
    void changed();
    void overlayChanged();
    void geometryChanged();
    void optionsChanged();
    void shapeEditorChanged();
    void tagsEditorChanged();

private:
    application::EditSession *editingSession() const { return m_session ? m_session() : nullptr; }
    void syncGeometry();
    void resetTool();
    void block(application::visual::VisualTool &tool);
    QVariantList optionMaps(const application::visual::VisualTool &t) const;
    QVariantList valueMaps(const application::visual::VisualTool &t) const;
    bool editValue(application::visual::VisualTool &t, const QString &name, const QString &text,
                   application::visual::ValueEdit edit);
    application::visual::LineWarning currentWarning() const;
    void updatePreview();
    void refreshOptions();
    void openShapeEditor();
    void saveShapes() const;
    void openTagsEditor();
    void saveAllTags() const;
    std::u16string activeLineText() const;

    VideoController &m_video;
    SettingsStore &m_settings;
    SessionProvider m_session;
    std::function<void()> m_edited;
    std::function<void(const core::Document *)> m_preview;
    std::function<void(const QString &)> m_log;
    std::function<bool()> m_ignoreFiltered;
    std::set<std::string> m_leasedFonts;
    std::function<std::pair<long, long>()> m_editorSelection;
    std::function<void(long, long)> m_placeSelection;
    mutable QtTextMeasurePort m_measure;
    bool previewDocument(const core::Document &document, core::Document &out) const;
    application::visual::VideoView m_view;
    application::visual::SourceGeometry m_geometry;
    std::vector<std::unique_ptr<application::visual::VisualTool>> m_tools;
    application::visual::Family m_family = application::visual::Family::Crosshair;
    std::optional<application::visual::Gesture> m_gesture;
    application::visual::BatchPicker m_picker;
    std::optional<application::CommandRefusal> m_lastRefusal;
    bool m_railEnabled = true;
    bool m_overPicture = false;
    std::optional<QPointF> m_lastPointer;
    QFont m_labelFont;
    QString m_copied;
    bool m_valueScrub = false;
    qreal m_logicalWidth = 0, m_logicalHeight = 0, m_logicalPanel = 0;
    // MUST KILL: commitGesture and undoStep also set m_seenRevision so refresh() keeps the tool's state
    std::uint64_t m_seenRevision = 0;
    const void *m_seenSession = nullptr;
    std::optional<core::LineId> m_seenActive;
    int m_editTarget = 0;
    std::optional<std::pair<std::u8string, std::u8string>> m_seenDraft;
    std::int64_t m_seenTime = -1;
    QString m_frameWarning;
    QVariantList m_options;
    int m_bells = 0;
    std::u8string m_previewKey;
    std::shared_ptr<ListFile> m_shapesFile;
    mutable std::vector<application::visual::ShapePreset> m_shapes;
    QPointer<ShapeEditor> m_shapeEditor;
    std::shared_ptr<ListFile> m_tagsFile;
    mutable std::vector<application::visual::AllTagsSetting> m_allTags;
    QPointer<AllTagsEditor> m_tagsEditor;
    std::function<std::function<bool(core::LineId)>()> m_shownLines;
    std::function<std::string(int)> m_editorHotkey;
};

}
