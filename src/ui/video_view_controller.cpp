#include "video_view_controller.h"

#include "settings_store.h"
#include "video_controller.h"
#include "visual_tools_controller.h"

#include "hikari/application/general_player.h"
#include "hikari/application/video_controls.h"
#include "hikari/application/video_snapshot.h"

#include <QCoreApplication>
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

}

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
        } else if (id == QLatin1String("video.progressBar") || id == QLatin1String("video.pauseOnClick") ||
                   id == QLatin1String("video.renderCost")) {
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
    if (m_player)
        m_player->setVolume(m_muted ? 0.0 : application::videoVolumeGain(volume()));
}

bool VideoViewController::zoomMode() const
{
    return m_video.hasVideo() && m_tools.videoView().hasVideo() && m_tools.videoView().zoomMode();
}

bool VideoViewController::zoomed() const
{
    return m_video.hasVideo() && m_tools.videoView().hasVideo() && m_tools.videoView().zoomPercent() != 1.f;
}

int VideoViewController::zoomPercent() const
{
    return static_cast<int>(m_tools.videoView().zoomPercent() * 100);
}

QRectF VideoViewController::zoomFrame() const
{
    const auto &view = m_tools.videoView();
    const auto &z = view.zoomRect();
    return QRectF(QPointF(view.toLogical(z.x), view.toLogical(z.y)),
                  QPointF(view.toLogical(z.width - 1), view.toLogical(z.height - 1)));
}

QRectF VideoViewController::zoomBounds() const
{
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

bool VideoViewController::renderCost() const
{
    return m_settings.boolean("video.renderCost");
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
    auto &view = m_tools.videoView();
    const bool leftDown = kind == Press && button == Qt::LeftButton;
    const bool leftHeld = (buttons & Qt::LeftButton) != 0 && kind != Release;
    const bool rotation = kind == Wheel && wheelSteps != 0;
    if (!(leftDown || leftHeld)) {
        const auto &z = view.zoomRect();
        // MUST KILL: the later edge test overrides the earlier one
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
    auto &view = m_tools.videoView();
    const bool control = (modifiers & Qt::ControlModifier) != 0;
    if (!m_video.hasVideo() || !view.hasVideo()) {
        m_tools.pointer(kind, x, y, button, buttons, modifiers, wheelSteps);
        return NoRequest;
    }
    if (view.zoomMode()) {
        zoomPointer(kind, view.toDevice(x), view.toDevice(y), button, buttons, wheelSteps);
        return kind == Move && buttons == 0 ? MovedOverVideo : NoRequest;
    }
    if (kind == Wheel && wheelSteps != 0) {
        if (control && !m_fullscreen)
            return NoRequest;
        if (!control && !m_tools.hasNonDefaultTool()) {
            const float current = view.zoomPercent();
            const float zoom = application::wheelZoomPercent(current, wheelSteps);
            if (current != zoom) {
                view.zoomAt(zoom, view.toDevice(x), view.toDevice(y));
                viewChanged();
            }
        }
    }
    m_tools.pointer(kind, x, y, button, buttons, modifiers, wheelSteps);
    if (m_tools.hasNonDefaultTool())
        return kind == Move && buttons == 0 ? MovedOverVideo : NoRequest;
    if (kind == Release && button == Qt::RightButton)
        return ContextMenuRequest;
    const int held = modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (kind == DoubleClick && button == Qt::LeftButton && held == 0)
        return FullScreenRequest;
    if (pauseOnClick() && kind == Release && button == Qt::LeftButton && !control)
        m_video.togglePlay();
    return kind == Move && buttons == 0 ? MovedOverVideo : NoRequest;
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
        m_tools.videoView().zoomWheel(steps);
        viewChanged();
        return;
    }
    if ((modifiers & Qt::ControlModifier) != 0 && !m_fullscreen)
        return;
    volumeWheel(steps);
}

void VideoViewController::volumeWheel(int steps)
{
    if (!m_video.hasVideo() || steps == 0)
        return;
    if (const auto v = application::videoVolumeWheelStep(volume(), steps))
        setVolume(*v);
}

bool VideoViewController::key(int key, int modifiers)
{
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
    if (volume != this->volume())
        m_settings.set("video.volume", volume);
    if (m_muted) {
        m_muted = false;
        applyVolume();
        emit changed();
    }
}

void VideoViewController::toggleMute()
{
    m_muted = !m_muted;
    applyVolume();
    emit changed();
}

int VideoViewController::aspectSliderValue() const
{
    return application::aspectSliderValue(m_tools.videoView().aspectRatio());
}

QString VideoViewController::aspectLabel(int sliderValue) const
{
    const float ratio = sliderValue < 0 ? m_tools.videoView().aspectRatio() : application::aspectFromSlider(sliderValue);
    return QCoreApplication::translate("HikariSub.Video", "Aspect ratio: %1")
               .arg(QString::fromStdString(application::aspectLabelNumber(ratio)));
}

void VideoViewController::setAspectFromSlider(int sliderValue)
{
    if (!m_tools.videoView().hasVideo())
        return;
    m_tools.videoView().setAspectRatio(application::aspectFromSlider(sliderValue));
    viewChanged();
}

double VideoViewController::aspectRatio() const
{
    return m_tools.videoView().aspectRatio();
}

void VideoViewController::setAspectRatio(double ratio)
{
    if (!m_tools.videoView().hasVideo())
        return;
    m_tools.videoView().setAspectRatio(static_cast<float>(ratio));
    viewChanged();
}

void VideoViewController::resetAspectRatio()
{
    setAspectRatio(m_tools.videoView().ownAspectRatio());
}

bool VideoViewController::toggleProgressBar()
{
    if (!m_video.hasVideo())
        return false;
    m_settings.set("video.progressBar", !progressBar());
    return true;
}

bool VideoViewController::toggleRenderCost()
{
    if (!m_video.hasVideo())
        return false;
    m_settings.set("video.renderCost", !renderCost());
    return true;
}

bool VideoViewController::snapshot(const QString &action)
{
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

}
