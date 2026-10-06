#pragma once

// V4 (#183): the Video panel's view commands, as legacy VideoBox and
// RendererVideo had them at 20d647c4.
//
//   - zoom: GLOBAL_VIDEO_ZOOM (the zoom mode, Return in it) and
//     GLOBAL_RESET_VIDEO_ZOOM (Ctrl+Shift+Z in the Video panel), the wheel
//     over the video with the crosshair (or no tool), and the zoom mode's
//     mouse (RendererVideo::ZoomMouseHandle) with its frame drawn over the
//     video (DrawZoom); the geometry is the visual tools' shared VideoView;
//   - VIDEO_ASPECT_RATIO's AspectRatioDialog;
//   - VIDEO_HIDE_PROGRESS_BAR (VIDEO_PROGRESS_BAR; the bar itself is the
//     fullscreen window's, V5);
//   - the volume: VIDEO_VOLUME_PLUS / VIDEO_VOLUME_MINUS, the volume slider
//     and the wheel over the panel (VIDEO_VOLUME, the general player's gain);
//   - VIDEO_PAUSE_ON_CLICK and the video context menu's pointer (the order
//     of VideoBox::OnMouseEvent);
//   - the snapshots VIDEO_SAVE_FRAME_TO_PNG, VIDEO_COPY_FRAME_TO_CLIPBOARD,
//     VIDEO_SAVE_SUBBED_FRAME_TO_PNG and VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD.

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QQuickItem;

namespace hikari::application {
class GeneralPlayerPort;
}

namespace hikari::ui {

class SettingsStore;
class VideoController;
class VisualToolsController;

class VideoViewController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // The zoom mode (legacy m_HasZoom) and whether the view is zoomed
    // (VideoBox::HasZoom: m_ZoomPercent != 1, "Turn off video zoom").
    Q_PROPERTY(bool zoomMode READ zoomMode NOTIFY changed)
    Q_PROPERTY(bool zoomed READ zoomed NOTIFY changed)
    // (int)(m_ZoomPercent * 100), legacy's status bar zoom field.
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY changed)
    // DrawZoom, in the video area's logical coordinates: the zoom frame's
    // corners (m_ZoomRect.x, y to width - 1, height - 1) and the dimmed area
    // (from the corner to the video's right and bottom edges).
    Q_PROPERTY(QRectF zoomFrame READ zoomFrame NOTIFY changed)
    Q_PROPERTY(QRectF zoomBounds READ zoomBounds NOTIFY changed)
    // The zoom mode's cursor near an edge: 0 arrow, 1 west-east, 2 north-south.
    Q_PROPERTY(int zoomCursor READ zoomCursor NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(bool progressBar READ progressBar NOTIFY changed)
    // A paused frame to take a snapshot of (legacy GetState() == Paused).
    Q_PROPERTY(bool canSnapshot READ canSnapshot NOTIFY changed)
    // The last PNG a snapshot wrote.
    Q_PROPERTY(QString lastSnapshot READ lastSnapshot NOTIFY changed)
    Q_PROPERTY(bool pauseOnClick READ pauseOnClick NOTIFY changed)
public:
    VideoViewController(VideoController &video, VisualToolsController &tools, SettingsStore &settings,
                        QObject *parent = nullptr);

    // The player whose output takes the volume (none: tests).
    void setPlayer(application::GeneralPlayerPort *player);

    bool zoomMode() const;
    bool zoomed() const;
    int zoomPercent() const;
    QRectF zoomFrame() const;
    QRectF zoomBounds() const;
    int zoomCursor() const { return m_zoomCursor; }
    int volume() const;
    bool progressBar() const;
    bool canSnapshot() const;
    QString lastSnapshot() const { return m_lastSnapshot; }
    bool pauseOnClick() const;

    // GLOBAL_VIDEO_ZOOM (VideoBox::SetZoom(false), RendererVideo::SetZoom()).
    Q_INVOKABLE bool toggleZoom();
    // GLOBAL_RESET_VIDEO_ZOOM (RendererVideo::ResetZoom).
    Q_INVOKABLE bool resetZoom();
    // A pointer event over the video area (VisualToolsController::pointer's
    // kinds and units), in VideoBox::OnMouseEvent's order: the zoom mode
    // takes everything; the wheel zooms with the crosshair (Ctrl+wheel, the
    // window height, is not the docked panel's); the visual tool; then the
    // right button's release opens the context menu (true) and a left
    // release without Ctrl pauses or plays with VIDEO_PAUSE_ON_CLICK.
    Q_INVOKABLE bool pointer(int kind, qreal x, qreal y, int button, int buttons, int modifiers, int wheelSteps = 0);
    // The wheel over the panel below the video: the volume slider's wheel
    // (VideoBox.cpp:519-534), or the zoom mode's; Ctrl+wheel does nothing
    // there (VideoBox.cpp:508-518, the docked panel is never fullscreen).
    Q_INVOKABLE void panelWheel(int steps, int modifiers = 0);
    // A key the Video panel's bindings left (VideoBox::OnKeyPress): Return
    // in the zoom mode, Ctrl+Shift+Z.
    Q_INVOKABLE bool key(int key, int modifiers);

    // VIDEO_VOLUME_PLUS / VIDEO_VOLUME_MINUS; the slider.
    Q_INVOKABLE bool stepVolume(bool up);
    Q_INVOKABLE void setVolume(int volume);

    // AspectRatioDialog: the slider's value for the current aspect ratio,
    // the label for a value, and a moved slider.
    Q_INVOKABLE int aspectSliderValue() const;
    Q_INVOKABLE QString aspectLabel(int sliderValue) const;
    Q_INVOKABLE void setAspectFromSlider(int sliderValue);

    // VIDEO_HIDE_PROGRESS_BAR (VideoBox::OnHidePB): needs a video.
    Q_INVOKABLE bool toggleProgressBar();

    // RendererVideo::SaveFrame: `id` the action's legacy id (2017..2020).
    Q_INVOKABLE bool snapshot(const QString &action);

    // The context menu's "Recently opened subtitles" and "Recently opened
    // videos" (VideoBox.cpp:946-960): {subtitles: [{path, label}], videos: [...]}.
    Q_INVOKABLE QVariantMap recentFiles() const;
    // The pointer's position in `area` (the menu key opens the menu there).
    Q_INVOKABLE QPointF cursorIn(QQuickItem *area) const;

signals:
    void changed();

private:
    void zoomPointer(int kind, int x, int y, int button, int buttons, int wheelSteps);
    void viewChanged();
    void applyVolume();

    VideoController &m_video;
    VisualToolsController &m_tools;
    SettingsStore &m_settings;
    application::GeneralPlayerPort *m_player = nullptr;
    int m_zoomCursor = 0;
    QString m_lastSnapshot;
};

} // namespace hikari::ui
