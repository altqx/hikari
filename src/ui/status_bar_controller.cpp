#include "status_bar_controller.h"

#include "video_controller.h"
#include "visual_tools_controller.h"

#include "hikari/application/grid_split.h"
#include "hikari/core/document.h"
#include "hikari/core/srt.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace hikari::ui {

namespace {

QString qs(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

QString resolutionText(int width, int height)
{
    return QStringLiteral("%1 x %2").arg(width).arg(height);
}

}

StatusBarController::Fields StatusBarController::fieldsFor(const Inputs &in)
{
    Fields out;
    auto &t = out.text;
    if (in.video) {
        if (in.frameWidth > 0) {
            t[VideoScale] = QString::number(static_cast<int>((static_cast<float>(in.clientWidth) /
                                                              static_cast<float>(in.frameWidth)) * 100)) +
                            QLatin1Char('%');
            t[VideoZoom] = QString::number(static_cast<int>(in.zoomPercent * 100)) + QLatin1Char('%');
            t[VideoResolution] = resolutionText(in.frameWidth, in.frameHeight);
        }
        t[VideoDuration] = qs(core::legacy::srtTimeText(in.durationMs));
        t[FramesPerSecond] = qs(application::legacy::floatText(in.fps)) + QStringLiteral(" FPS");
        t[AspectRatio] = QStringLiteral("%1 : %2").arg(in.aspect.width).arg(in.aspect.height);
        t[VideoName] = QFileInfo(in.videoPath).fileName();
    }
    if (in.document && in.ass)
        t[SubtitlesResolution] = resolutionText(in.script.width, in.script.height);
    return out;
}

StatusBarController::StatusBarController(VideoController &video, VisualToolsController &tools, DocumentProvider document,
                                         QObject *parent)
    : QObject(parent), m_video(video), m_tools(tools), m_document(std::move(document))
{
    connect(&m_video, &VideoController::textsChanged, this, &StatusBarController::refresh);
    connect(&m_tools, &VisualToolsController::geometryChanged, this, &StatusBarController::refresh);
    connect(&m_tools, &VisualToolsController::changed, this, &StatusBarController::refresh);
    refresh();
}

StatusBarController::Inputs StatusBarController::inputs() const
{
    Inputs in;
    const auto &session = m_video.session();
    in.video = m_video.hasVideo();
    if (in.video) {
        const auto &view = m_tools.videoView();
        if (view.hasVideo()) {
            in.clientWidth = view.clientWidth();
            in.frameWidth = view.frameWidth();
            in.frameHeight = view.frameHeight();
            in.zoomPercent = view.zoomPercent();
        }
        if (const auto last = session.frameStart(session.frameCount() - 1))
            in.durationMs = last->microseconds() / 1000;
        in.fps = session.legacyFps();
        in.aspect = application::visual::sourceAspect(session.sourceGeometry());
        in.videoPath = QString::fromStdString(session.path());
    }
    if (const core::Document *document = m_document ? m_document() : nullptr) {
        in.document = true;
        in.documentKey = document;
        in.ass = document->format() == core::SubtitleFormat::Ass;
        in.script = application::scriptResolution(*document);
    }
    return in;
}

bool StatusBarController::nextMismatch(const Inputs &last, const Inputs &in, bool mismatch)
{
    const bool subsEvent = in.documentKey != last.documentKey || in.document != last.document ||
                           in.ass != last.ass || in.script != last.script || (!in.video && last.video);
    const bool videoEvent = in.video && in.frameWidth > 0 &&
                            (!last.video || last.frameWidth <= 0 || in.videoPath != last.videoPath ||
                             in.frameWidth != last.frameWidth || in.frameHeight != last.frameHeight);
    const bool differs = in.frameWidth != in.script.width || in.frameHeight != in.script.height;
    if (subsEvent)
        mismatch = in.document && in.ass && in.video && in.frameWidth > 0 && differs;
    if (videoEvent)
        mismatch = in.document && differs;
    return mismatch;
}

void StatusBarController::refresh()
{
    const Inputs in = inputs();
    Fields next = fieldsFor(in);
    next.mismatch = nextMismatch(m_last, in, m_fields.mismatch);
    m_last = in;
    if (next.text == m_fields.text && next.mismatch == m_fields.mismatch)
        return;
    m_fields = std::move(next);
    emit changed();
}

QStringList StatusBarController::fields() const
{
    return QStringList(m_fields.text.begin(), m_fields.text.end());
}

QStringList StatusBarController::tooltips()
{
    return {QString(),
            QCoreApplication::translate("HikariSub.StatusBar", "Video scale"),
            QCoreApplication::translate("HikariSub.StatusBar", "Video zoom"),
            QCoreApplication::translate("HikariSub.StatusBar", "Video duration"),
            QCoreApplication::translate("HikariSub.StatusBar", "Frames per second"),
            QCoreApplication::translate("HikariSub.StatusBar", "Video resolution"),
            QCoreApplication::translate("HikariSub.StatusBar", "Video aspect ratio"),
            QCoreApplication::translate("HikariSub.StatusBar", "Subtitles resolution"),
            QCoreApplication::translate("HikariSub.StatusBar", "Video file name")};
}

}
