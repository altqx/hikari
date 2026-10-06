#include "video_view_controller.h"

#include "settings_store.h"
#include "video_controller.h"
#include "visual_tools_controller.h"

#include "hikari/application/general_player.h"
#include "hikari/application/video_controls.h"
#include "hikari/application/video_snapshot.h"

#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQuickItem>
#include <QVariantList>

#include <cmath>

namespace hikari::ui {

namespace {

enum PointerKind { Enter = 0, Leave = 1, Move = 2, Press = 3, Release = 4, Wheel = 5, DoubleClick = 6 };

} // namespace

VideoViewController::VideoViewController(VideoController &video, VisualToolsController &tools, SettingsStore &settings,
                                         QObject *parent)
    : QObject(parent), m_video(video), m_tools(tools), m_settings(settings)
{
    connect(&m_video, &VideoController::changed, this, &VideoViewController::changed);
    connect(&m_tools, &VisualToolsController::geometryChanged, this, &VideoViewController::changed);
    connect(&m_settings, &SettingsStore::changed, this, [this](const QString &id) {
        if (id == QLatin1String("video.volume")) {
            applyVolume();
            emit changed();
        } else if (id == QLatin1String("video.progressBar") || id == QLatin1String("video.pauseOnClick")) {
            emit changed();
        }
    });
}

void VideoViewController::setPlayer(application::GeneralPlayerPort *player)
{
    m_player = player;
    applyVolume();
}

void VideoViewController::applyVolume()
{
    // VideoBox::OnVolume / OpenVideo: SetVolume(-(pos * pos)).
    if (m_player)
        m_player->setVolume(application::videoVolumeGain(volume()));
}

bool VideoViewController::zoomMode() const
{
    return m_video.hasVideo() && m_tools.videoView().hasVideo() && m_tools.videoView().zoomMode();
}

bool VideoViewController::zoomed() const
{
    // VideoBox::HasZoom (VideoBox.cpp:1702-1708).
    return m_video.hasVideo() && m_tools.videoView().hasVideo() && m_tools.videoView().zoomPercent() != 1.f;
}

int VideoViewController::zoomPercent() const
{
    // VideoBox::SetScaleAndZoom (VideoBox.cpp:1301-1315).
    return static_cast<int>(m_tools.videoView().zoomPercent() * 100);
}

QRectF VideoViewController::zoomFrame() const
{
    // RendererVideo::DrawZoom (RendererVideo.cpp:831-871): the line through
    // (x, y), (x, height - 1), (width - 1, height - 1), (width - 1, y).
    const auto &view = m_tools.videoView();
    const auto &z = view.zoomRect();
    return QRectF(QPointF(view.toLogical(z.x), view.toLogical(z.y)),
                  QPointF(view.toLogical(z.width - 1), view.toLogical(z.height - 1)));
}

QRectF VideoViewController::zoomBounds() const
{
    // DrawZoom's dimmed fans span 0,0 to the back buffer rectangle's right and bottom.
    const auto &view = m_tools.videoView();
    const auto &r = view.videoRect();
    return QRectF(0, 0, view.toLogical(r.right), view.toLogical(r.bottom));
}

int VideoViewController::volume() const
{
    return m_settings.integer("video.volume");
}

bool VideoViewController::progressBar() const
{
    return m_settings.boolean("video.progressBar");
}

bool VideoViewController::pauseOnClick() const
{
    return m_settings.boolean("video.pauseOnClick");
}

bool VideoViewController::canSnapshot() const
{
    const auto frame = m_video.session().lastFrame();
    return m_video.hasVideo() && !m_video.playing() && frame && frame->index >= 0;
}

void VideoViewController::viewChanged()
{
    m_tools.viewChanged();
    emit changed();
}

bool VideoViewController::toggleZoom()
{
    // RendererVideo::SetZoom(): nothing with state None.
    if (!m_video.hasVideo() || !m_tools.videoView().hasVideo())
        return false;
    m_tools.videoView().toggleZoom(m_settings.integer("video.zoomPercent"));
    m_zoomCursor = 0;
    viewChanged();
    return true;
}

bool VideoViewController::resetZoom()
{
    if (!m_video.hasVideo() || !m_tools.videoView().hasVideo())
        return false;
    m_tools.videoView().resetZoom();
    viewChanged();
    return true;
}

void VideoViewController::zoomPointer(int kind, int x, int y, int button, int buttons, int wheelSteps)
{
    // RendererVideo::ZoomMouseHandle (RendererVideo.cpp:873-1033).
    auto &view = m_tools.videoView();
    const bool leftDown = kind == Press && button == Qt::LeftButton;
    const bool leftHeld = (buttons & Qt::LeftButton) != 0 && kind != Release;
    const bool rotation = kind == Wheel && wheelSteps != 0;
    if (!(leftDown || leftHeld)) {
        // The cursor near an edge; the later test wins, as legacy set them in turn.
        const auto &z = view.zoomRect();
        int cursor = 0;
        if (std::abs(x - z.x) < 5)
            cursor = 1;
        if (std::abs(y - z.y) < 5)
            cursor = 2;
        if (std::abs(x - z.width) < 5)
            cursor = 1;
        if (std::abs(y - z.height) < 5)
            cursor = 2;
        m_zoomCursor = cursor;
    }
    if (leftDown) {
        view.zoomPress(x, y);
    } else if (leftHeld || rotation) {
        if (rotation)
            view.zoomWheel(wheelSteps);
        else
            view.zoomDrag(x, y);
        viewChanged();
        return;
    }
    emit changed();
}

int VideoViewController::pointer(int kind, qreal x, qreal y, int button, int buttons, int modifiers, int wheelSteps)
{
    // VideoBox::OnMouseEvent (VideoBox.cpp:477-622).
    auto &view = m_tools.videoView();
    const bool control = (modifiers & Qt::ControlModifier) != 0;
    if (!m_video.hasVideo() || !view.hasVideo()) {
        m_tools.pointer(kind, x, y, button, buttons, modifiers, wheelSteps); // GetState() == None: nothing
        return NoRequest;
    }
    if (view.zoomMode()) {
        zoomPointer(kind, view.toDevice(x), view.toDevice(y), button, buttons, wheelSteps);
        return NoRequest;
    }
    if (kind == Wheel && wheelSteps != 0) {
        // Ctrl+wheel resized legacy's video window (TabPanel::SetVideoWindowSizes);
        // the docked panel's size is the docking layout's. In fullscreen
        // (`event.ControlDown() && !m_IsFullscreen`) it goes on to the tool.
        if (control && !m_fullscreen)
            return NoRequest;
        if (!control && (m_toolsOff || !m_tools.hasNonDefaultTool())) {
            const float current = view.zoomPercent();
            const float zoom = application::wheelZoomPercent(current, wheelSteps);
            if (current != zoom) {
                view.zoomAt(zoom, view.toDevice(x), view.toDevice(y));
                viewChanged();
            }
        }
    }
    if (!m_toolsOff) {
        m_tools.pointer(kind, x, y, button, buttons, modifiers, wheelSteps);
        // Only the crosshair leaves the other clicks to the video.
        if (m_tools.hasNonDefaultTool())
            return NoRequest;
    }
    if (kind == Release && button == Qt::RightButton)
        return ContextMenuRequest; // ContextMenu(event.GetPosition())
    // V5: LeftDClick with no modifier, SetFullscreen() (VideoBox.cpp:554-569).
    const int held = modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (kind == DoubleClick && button == Qt::LeftButton && held == 0)
        return FullScreenRequest;
    if (pauseOnClick() && kind == Release && button == Qt::LeftButton && !control)
        m_video.togglePlay(); // VideoBox::Pause
    return kind == Move ? MovedOverVideo : NoRequest;
}

void VideoViewController::setFullscreen(bool on)
{
    if (m_fullscreen == on)
        return;
    m_fullscreen = on;
    emit changed();
}

void VideoViewController::panelWheel(int steps, int modifiers)
{
    if (!m_video.hasVideo() || steps == 0)
        return;
    if (zoomMode()) {
        m_tools.videoView().zoomWheel(steps); // the zoom mode takes the wheel anywhere
        viewChanged();
        return;
    }
    // Ctrl+wheel outside fullscreen returned before the volume, doing
    // nothing below the video (VideoBox.cpp:508-518); in fullscreen the
    // panel's wheel is the volume's with or without Ctrl.
    if ((modifiers & Qt::ControlModifier) != 0 && !m_fullscreen)
        return;
    if (const auto v = application::videoVolumeWheelStep(volume(), steps))
        setVolume(*v);
}

bool VideoViewController::key(int key, int modifiers)
{
    // VideoBox::OnKeyPress (VideoBox.cpp:630-669) after `if (!renderer) return`.
    if (!m_video.hasVideo())
        return false;
    if (key == Qt::Key_Return && zoomMode()) {
        toggleZoom();
        return true;
    }
    if (key == Qt::Key_Z && (modifiers & Qt::ControlModifier) && (modifiers & Qt::ShiftModifier)) {
        resetZoom();
        return true;
    }
    return false;
}

bool VideoViewController::stepVolume(bool up)
{
    // VideoBox::OnSPlus / OnSMinus: nothing without a renderer.
    if (!m_video.hasVideo())
        return false;
    const auto v = application::videoVolumeKeyStep(volume(), up);
    if (!v)
        return false;
    setVolume(*v);
    return true;
}

void VideoViewController::setVolume(int volume)
{
    if (volume == this->volume())
        return;
    m_settings.set("video.volume", volume); // applied through the store's change
}

int VideoViewController::aspectSliderValue() const
{
    return application::aspectSliderValue(m_tools.videoView().aspectRatio());
}

QString VideoViewController::aspectLabel(int sliderValue) const
{
    // "Aspect ratio: %5.3f" of 1 / the ratio: the dialog's own ratio when
    // it opens (sliderValue < 0), then the slider's.
    const float ratio = sliderValue < 0 ? m_tools.videoView().aspectRatio() : application::aspectFromSlider(sliderValue);
    return tr("Aspect ratio: %1").arg(QString::fromStdString(application::aspectLabelNumber(ratio)));
}

void VideoViewController::setAspectFromSlider(int sliderValue)
{
    // AspectRatioDialog::OnSlider then VideoBox::SetAspectRatio: nothing
    // without a renderer.
    if (!m_tools.videoView().hasVideo())
        return;
    m_tools.videoView().setAspectRatio(application::aspectFromSlider(sliderValue));
    viewChanged();
}

bool VideoViewController::toggleProgressBar()
{
    if (!m_video.hasVideo())
        return false;
    m_settings.set("video.progressBar", !progressBar());
    return true;
}

bool VideoViewController::snapshot(const QString &action)
{
    // VideoBox::OnAccelerator: only while paused (VideoBox.cpp:1171-1173).
    const bool save = action == QLatin1String("VIDEO_SAVE_FRAME_TO_PNG")
                      || action == QLatin1String("VIDEO_SAVE_SUBBED_FRAME_TO_PNG");
    const bool subbed = action == QLatin1String("VIDEO_SAVE_SUBBED_FRAME_TO_PNG")
                        || action == QLatin1String("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD");
    if (!canSnapshot())
        return false;
    const auto &session = m_video.session();
    const auto frame = session.lastFrame();
    const auto overlay = subbed ? session.lastOverlay() : nullptr;
    const application::SnapshotImage image = application::snapshotImage(*frame, overlay.get());
    const QImage picture = QImage(image.rgb.data(), image.width, image.height, image.width * 3, QImage::Format_RGB888).copy();
    if (!save) {
        if (auto *clipboard = QGuiApplication::clipboard())
            clipboard->setImage(picture);
        return true;
    }
    const std::string video = session.path();
    // HikariPathDir and wxDir::GetAllFiles(dir, filespec, wxDIR_FILES): the
    // folder's files (not hidden ones) matching the pattern, case-sensitive
    // outside Windows, as full paths.
    const QString folder = QFileInfo(QString::fromStdString(video)).absolutePath();
    QDir dir(folder);
    QDir::Filters filters = QDir::Files;
#ifndef _WIN32
    filters |= QDir::CaseSensitive;
#endif
    std::vector<std::string> paths;
    for (const QString &name :
         dir.entryList({QString::fromStdString(application::snapshotPattern(video))}, filters, QDir::Name))
        paths.push_back(QDir::toNativeSeparators(dir.filePath(name)).toStdString());
    // m_Time: the paused frame's time (Timebase::MsAt).
    const int ms = session.legacyTimebase().msAt(frame->index);
    const QString path = QString::fromStdString(application::nextSnapshotPath(
        video, paths, ms, [](const std::string &p) { return QFileInfo::exists(QString::fromStdString(p)); }));
    if (!picture.save(path, "PNG"))
        return false;
    m_lastSnapshot = path;
    emit changed();
    return true;
}

QVariantMap VideoViewController::recentFiles() const
{
    // VideoBox::ContextMenu (VideoBox.cpp:946-960): the first twenty of each
    // list; a missing file skips its own row only (approved departure
    // V4-recent-rows: legacy's `continue` on a missing subtitle file skipped
    // the video row of the same index too, VideoBox.cpp:952-960).
    const auto subtitles = m_settings.settings().list("recent.subtitles");
    const auto videos = m_settings.settings().list("recent.video");
    QVariantList subs, vids;
    auto row = [](const std::string &path) {
        const QString p = QString::fromStdString(path);
        return QVariantMap{{QStringLiteral("path"), p}, {QStringLiteral("label"), QFileInfo(p).fileName()}};
    };
    for (std::size_t i = 0; i < 20; i++) {
        if (i < subtitles.size() && QFileInfo(QString::fromStdString(subtitles[i])).isFile())
            subs << row(subtitles[i]);
        if (i < videos.size() && QFileInfo(QString::fromStdString(videos[i])).isFile())
            vids << row(videos[i]);
    }
    return {{QStringLiteral("subtitles"), subs}, {QStringLiteral("videos"), vids}};
}

QPointF VideoViewController::cursorIn(QQuickItem *area) const
{
    return area ? area->mapFromGlobal(QCursor::pos()) : QPointF();
}

} // namespace hikari::ui
