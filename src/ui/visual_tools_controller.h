#pragma once

// T1: the Video panel's visual tools (docs/qt/visual-tools.md). The tool
// rail (accepted layout A on #55) lists legacy VideoToolbar's eleven
// families; the active tool gets the pointer over the video and the panel's
// keys, and draws through the overlay. This controller is the tools'
// VisualHost: it keeps the shared view transform (visual::VideoView) in step
// with the panel's size, the video's geometry and the script resolution,
// owns the one open gesture (Esc cancels it), the batch picker, the
// warnings (VIDEO_VISUAL_WARNINGS_OFF) and VIDEO_COPY_COORDS.

#include "hikari/application/shape_presets.h"
#include "hikari/application/visual_tools.h"
#include "automation_services_qt.h"
#include "settings_store.h"
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

class QQuickItem;

namespace hikari::ui {

class VideoController;

class VisualToolsController : public QObject, public application::visual::VisualHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // The rail: name, tooltip, icon and whether the family has its tool yet.
    Q_PROPERTY(QVariantList families READ families CONSTANT)
    Q_PROPERTY(int activeFamily READ activeFamily NOTIFY changed)
    // Legacy VideoToolbar::DisableVisuals(form != ASS): the rail is disabled
    // for a Document that is not ASS.
    Q_PROPERTY(bool railEnabled READ railEnabled NOTIFY changed)
    // What the active tool draws, in the video area's logical coordinates.
    Q_PROPERTY(QVariantList overlay READ overlay NOTIFY overlayChanged)
    Q_PROPERTY(QString labelFamily READ labelFamily CONSTANT)
    Q_PROPERTY(int labelPixelSize READ labelPixelSize NOTIFY geometryChanged)
    // VideoBox hides the pointer over the video while the crosshair is the
    // tool (VideoBox.cpp:607-611).
    Q_PROPERTY(bool hideCursor READ hideCursor NOTIFY changed)
    // The tool's Line warning (empty for none or with VIDEO_VISUAL_WARNINGS_OFF).
    Q_PROPERTY(QString warning READ warning NOTIFY changed)
    // The presenter draws the frame here (legacy m_BackBufferRect), from the
    // frame's sourceRect (m_MainStreamRect, in frame pixels).
    Q_PROPERTY(QRectF videoRect READ videoRect NOTIFY geometryChanged)
    Q_PROPERTY(QRectF sourceRect READ sourceRect NOTIFY geometryChanged)
    // The tool's numeric values (shown below the canvas): name, label, text, editable.
    Q_PROPERTY(QVariantList values READ values NOTIFY changed)
    // T3: Esc has something to drop: an open gesture or a tool's pending step.
    Q_PROPERTY(bool escapable READ escapable NOTIFY changed)
    // The batch picker.
    Q_PROPERTY(int batchCount READ batchCount NOTIFY changed)
    Q_PROPERTY(bool gestureActive READ gestureActive NOTIFY changed)
    // The last text VIDEO_COPY_COORDS put on the clipboard.
    Q_PROPERTY(QString copied READ copied NOTIFY changed)
    // T2-T4: the family's own options (legacy VideoToolbar's second row):
    // name, kind ("toggle", "choice" or "action"), iconRole, tooltip, checked,
    // enabled, choices, index. T4: its own signal, so the row is rebuilt only
    // when it changes. And the notice a tool gave, which the shell shows as
    // legacy's modal "Warning" message box until OK.
    Q_PROPERTY(QVariantList options READ options NOTIFY optionsChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    // T5: the "Vector shape editing" dialog while the drawing's shape list's
    // "Edit" has it open (null otherwise).
    Q_PROPERTY(hikari::ui::ShapeEditor *shapeEditor READ shapeEditor NOTIFY shapeEditorChanged)
public:
    using SessionProvider = std::function<application::EditSession *()>;
    VisualToolsController(VideoController &video, SettingsStore &settings, SessionProvider session,
                          QObject *parent = nullptr);
    ~VisualToolsController() override;

    // Called after a gesture changed the Document (the shell refreshes).
    void setEdited(std::function<void()> edited) { m_edited = std::move(edited); }
    // T2, T4: the video shows the open gesture's staged texts (legacy's dummy
    // rendering, Visuals::RenderSubs) and the tool's own Lines (T4: the
    // vector clip's mask, Visuals::AppendClipMask): called with the Document
    // as it would be, or null when there is nothing to add (the committed
    // Document again).
    void setPreview(std::function<void(const core::Document *)> preview) { m_preview = std::move(preview); }
    // T2: HikariLog and the Grid's "Ignore filtering in some actions".
    void setLog(std::function<void(const QString &)> log) { m_log = std::move(log); }
    void setIgnoreFiltered(std::function<bool()> ignore) { m_ignoreFiltered = std::move(ignore); }
    // T3: the Line editor's selection in the edited text (FindTag's mode 0).
    void setEditorSelection(std::function<std::pair<long, long>()> selection)
    {
        m_editorSelection = std::move(selection);
    }
    // T3: puts the Line editor's caret where a tool's edit left it.
    void setEditorSelectionPlacer(std::function<void(long, long)> place) { m_placeSelection = std::move(place); }
    // The editing target, its content, active Line or format changed.
    void refresh();
    // T5: the drawing's shape presets' file (legacy Config/ShapesSettings.txt
    // beside the settings). Without one the presets are kept in memory.
    void setShapesFile(const QString &path) { m_shapesFile = path; }
    const QString &shapesFile() const { return m_shapesFile; }
    ShapeEditor *shapeEditor() const { return m_shapeEditor; }
    // Replaces a family's tool (tests; T2-T6 use makeVisualTool).
    void setTool(application::visual::Family family, std::unique_ptr<application::visual::VisualTool> tool);
    application::visual::VisualTool *tool() const;
    application::visual::BatchPicker &batchPicker() { return m_picker; }

    QVariantList families() const;
    int activeFamily() const { return static_cast<int>(m_family); }
    bool railEnabled() const { return m_railEnabled; }
    QVariantList overlay() const;
    QString labelFamily() const; // the family the label font resolves to
    int labelPixelSize() const;
    bool hideCursor() const;
    QString warning() const;
    QRectF videoRect() const;
    QRectF sourceRect() const;
    QVariantList values() const;
    bool escapable() const;
    int batchCount() const { return static_cast<int>(m_picker.picked().size()); }
    bool gestureActive() const { return m_gesture.has_value(); }
    QString copied() const { return m_copied; }
    QVariantList options() const { return m_options; }
    QString notice() const { return m_notice; }
    int bells() const { return m_bells; }

    // The video area's logical size (the presenter's), the panel below it
    // and the window's device pixel ratio.
    Q_INVOKABLE void setViewport(qreal width, qreal height, qreal panelHeight, qreal devicePixelRatio);
    // A pointer event over the video area, in its logical coordinates:
    // kind 0 enter, 1 leave, 2 move, 3 press, 4 release, 5 wheel, 6 the
    // second press of a double click (after its press); button a
    // Qt::MouseButton; modifiers Qt::KeyboardModifiers.
    Q_INVOKABLE void pointer(int kind, qreal x, qreal y, int button, int buttons, int modifiers, int wheelSteps = 0);
    // A key while the Video panel has focus: true when the tool used it.
    Q_INVOKABLE bool key(int key, int modifiers, bool release, bool autoRepeat);
    // Esc: cancels the open gesture, if any (true then).
    Q_INVOKABLE bool escape();
    // A rail button: legacy's toggle (the toggled family again goes back to
    // the crosshair, VideoToolbar.cpp:209-210).
    Q_INVOKABLE void selectFamily(int family);
    // VIDEO_COPY_COORDS at a logical position in the video area, or at the
    // pointer (legacy wxGetMousePosition, VideoBox.cpp:1151): the last
    // pointer event's position while the pointer is over the video, else
    // the cursor's.
    Q_INVOKABLE QString copyCoordinates(qreal x, qreal y);
    Q_INVOKABLE QString copyCoordinatesAtCursor(QQuickItem *area);
    Q_INVOKABLE bool setValue(const QString &name, const QString &text);
    // The batch picker: the Grid's selected Lines, or nothing (the active Line).
    Q_INVOKABLE void pickBatch();
    Q_INVOKABLE void clearBatch();
    // A toggle (0/1), a choice's index or an action (1) of the family's options.
    Q_INVOKABLE bool setOption(const QString &name, int value);
    Q_INVOKABLE void dismissNotice();

    // T4: the subtitles the video renders for a Document: as the preview
    // gives it (the staged texts and the tool's Lines added), encoded by
    // `encode` (E5: the renderer's options), else as saved.
    using Encoder = std::function<std::vector<std::byte>(const core::Document &)>;
    std::vector<std::byte> subtitles(const core::Document &document, const Encoder &encode = {}) const;

    // VisualHost.
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

    // The shared view (tests and V4's zoom commands).
    application::visual::VideoView &videoView() { return m_view; }
    const application::visual::VideoView &videoView() const { return m_view; }
    // V4: legacy RendererVideo::HasVisual(true): a tool other than the
    // crosshair, which keeps the wheel and the clicks to itself.
    bool hasNonDefaultTool() const { return tool() && m_family != application::visual::Family::Crosshair; }
    // V4: the zoom or the aspect ratio changed the view (RendererVideo::Zoom,
    // ResetZoom, UpdateVideoWindow): the tools take it, and a tool other
    // than the clips and the drawing starts again (SetCurVisual,
    // RendererVideo.cpp:785-788 and 806-811).
    void viewChanged();
    std::optional<application::CommandRefusal> lastRefusal() const { return m_lastRefusal; }

signals:
    void changed();
    void overlayChanged();
    void geometryChanged();
    void optionsChanged();
    void shapeEditorChanged();

private:
    application::EditSession *editingSession() const { return m_session ? m_session() : nullptr; }
    void syncGeometry();
    void resetTool();
    application::visual::LineWarning currentWarning() const;
    void updatePreview();
    void refreshOptions();
    void openShapeEditor();
    void saveShapes() const;

    VideoController &m_video;
    SettingsStore &m_settings;
    SessionProvider m_session;
    std::function<void()> m_edited;
    std::function<void(const core::Document *)> m_preview;
    std::function<void(const QString &)> m_log;
    std::function<bool()> m_ignoreFiltered;
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
    bool m_overVideo = false;
    std::optional<QPointF> m_lastPointer; // the last pointer event's position, until it leaves
    QFont m_labelFont;
    QString m_copied;
    qreal m_logicalWidth = 0, m_logicalHeight = 0, m_logicalPanel = 0;
    std::uint64_t m_seenRevision = 0;
    const void *m_seenSession = nullptr;
    std::optional<core::LineId> m_seenActive;
    std::optional<std::pair<std::u8string, std::u8string>> m_seenDraft; // the active Line's draft text, translation
    std::int64_t m_seenTime = -1;
    QString m_notice;
    QVariantList m_options; // the tool's buttons as last shown
    int m_bells = 0;
    std::u8string m_previewKey; // what the preview showed last
    QString m_shapesFile;
    mutable std::vector<application::visual::ShapePreset> m_shapes; // VideoToolbar::shapes
    QPointer<ShapeEditor> m_shapeEditor;
};

} // namespace hikari::ui
