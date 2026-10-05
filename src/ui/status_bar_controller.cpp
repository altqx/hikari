#include "status_bar_controller.h"

#include "video_controller.h"
#include "visual_tools_controller.h"

#include "hikari/application/grid_split.h"
#include "hikari/core/document.h"
#include "hikari/core/srt.h"

#include <QFileInfo>

namespace hikari::ui {

namespace {

QString qs(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

QString resolutionText(int width, int height)
{
    // wxString::Format(L"%d x %d") and `<< w << L" x " << h` alike.
    return QStringLiteral("%1 x %2").arg(width).arg(height);
}

} // namespace

StatusBarController::Fields StatusBarController::fieldsFor(const Inputs &in)
{
    Fields out;
    auto &t = out.text;
    if (in.video) {
        if (in.frameWidth > 0) {
            // VideoBox::SetScaleAndZoom (VideoBox.cpp:1301-1315): the box's
            // width over the frame's, then m_ZoomPercent, both truncated.
            t[VideoScale] = QString::number(static_cast<int>((static_cast<float>(in.clientWidth) /
                                                              static_cast<float>(in.frameWidth)) * 100)) +
                            QLatin1Char('%');
            t[VideoZoom] = QString::number(static_cast<int>(in.zoomPercent * 100)) + QLatin1Char('%');
            // SetVideoResolution (HikariSubFrame.cpp:1477-1480).
            t[VideoResolution] = resolutionText(in.frameWidth, in.frameHeight);
        }
        // VideoBox::OpenVideo (VideoBox.cpp:395-404): the duration as
        // SubsTime::raw(SRT), getfloat(m_FPS) + " FPS", "x : y" and the file name.
        t[VideoDuration] = qs(core::legacy::srtTimeText(in.durationMs));
        t[FramesPerSecond] = qs(application::legacy::floatText(in.fps)) + QStringLiteral(" FPS");
        t[AspectRatio] = QStringLiteral("%1 : %2").arg(in.aspect.width).arg(in.aspect.height);
        t[VideoName] = QFileInfo(in.videoPath).fileName();
    }
    // SetSubsResolution (HikariSubFrame.cpp:1439-1450): ASS only.
    if (in.document && in.ass)
        t[SubtitlesResolution] = resolutionText(in.script.width, in.script.height);
    return out;
}

StatusBarController::StatusBarController(VideoController &video, VisualToolsController &tools, DocumentProvider document,
                                         QObject *parent)
    : QObject(parent), m_video(video), m_tools(tools), m_document(std::move(document))
{
    connect(&m_video, &VideoController::changed, this, &StatusBarController::refresh);
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
        // RendererFFMS2::GetDuration: FFMS_VideoProperties::LastTime, the
        // last frame's time, in whole milliseconds.
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
    // Fields 5 and 7's colour follows legacy's two setters rather than the
    // state. SetSubsResolution (HikariSubFrame.cpp:1439-1473: another
    // Document shown or loaded, its format or resolution changed) warns for
    // an ASS Document whose resolution is not the open video's.
    // SetVideoResolution (HikariSubFrame.cpp:1475-1497: a video opened)
    // warns whenever the video's resolution is not GetASSRes's, whatever the
    // Document's format, until the next SetSubsResolution. Anything else
    // keeps the colour. The rewrite's "Unload video" (no legacy counterpart)
    // counts as SetSubsResolution, as legacy's tab change to a tab without
    // video did (OnPageChanged).
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
    // HikariSubFrame.cpp:153-154.
    return {QString(),           tr("Video scale"),       tr("Video zoom"),
            tr("Video duration"), tr("Frames per second"), tr("Video resolution"),
            tr("Video aspect ratio"), tr("Subtitles resolution"), tr("Video file name")};
}

} // namespace hikari::ui
