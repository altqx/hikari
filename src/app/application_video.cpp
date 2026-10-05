// V3: video sources, tracks and chapters in the shell (legacy HikariSubFrame's
// Video menu, VideoBox's file walk and Remove video, DummyVideo and
// VideoBox::OpenKeyframes at 20d647c4). The stream and chapter lists and
// Unload video live on the VideoController; this file keeps what needs the
// recent lists, the log and the tab's media.

#include "hikari/app/application.h"

#include "hikari/application/keyframe_files.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hikari::app {

namespace {

bool missingLocalFile(const std::string &path)
{
    return application::isMissingLocalFile(
        path, [](const std::string &p) { return QFileInfo(QString::fromStdString(p)).isFile(); },
        [](char drive) {
#ifdef _WIN32
            const wchar_t root[] = {static_cast<wchar_t>(drive), L':', L'\\', 0};
            return ::GetDriveTypeW(root) == DRIVE_REMOTE;
#else
            (void)drive;
            return false;
#endif
        });
}

} // namespace

void Application::trackVideoSources()
{
    m_recentVideo.set(m_settings->settings().list("recent.video"));         // VIDEO_RECENT_FILES
    m_recentKeyframes.set(m_settings->settings().list("recent.keyframes")); // KEYFRAMES_RECENT
    // VideoBox::LoadVideo's SetRecent(1) (VideoBox.cpp:405) after each
    // successful load, a dummy video's text included.
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        const auto &video = m_video->session();
        if (video.state() != application::VideoSession::State::Ready) {
            m_recentVideoSeen.clear();
            return;
        }
        const QString path = QString::fromStdString(video.path());
        if (path != m_recentVideoSeen) {
            m_recentVideoSeen = path;
            rememberRecentVideo(path);
        }
    });
    // V3-unload-video: the tab no longer has a video (legacy VideoPath), so
    // neither a tab switch nor the session brings it back.
    connect(m_video.get(), &ui::VideoController::unloaded, this, [this] {
        m_pendingTabSeek.reset();
        m_tabSeekQueued = false;
        if (const auto target = m_workspace.editingTarget())
            if (const auto it = m_tabMedia.find(target->value); it != m_tabMedia.end() && !it->second.video.isEmpty()) {
                it->second.video.clear();
                it->second.position = 0;
                emit tabsChanged();
            }
    });
}

// A failed open is logged once, with legacy ProviderFFMS2::Init's message for
// the stage it failed at (ProviderFFMS2.cpp:165-357): "Indexing error
// occurred: %s" for indexing (FFMS2's text; nothing when cancelled), "Cannot
// create VideoSource.", "Cannot convert video to RGBA". Where legacy gave no
// message (the indexer could not be made: a debug message only) the panel's
// status is logged, as before.
void Application::logVideoFailure()
{
    const auto &session = m_video->session();
    const bool failed = session.state() == application::VideoSession::State::Failed;
    if (failed && !m_videoFailureLogged) {
        const auto &failure = session.openFailure();
        const QString text = failure ? QString::fromStdString(failure->message) : QString();
        using application::OpenStage;
        if (failure && failure->stage == OpenStage::Indexing)
            m_log->log(tr("Indexing error occurred: %1").arg(text));
        else if (failure && failure->stage == OpenStage::Source)
            m_log->log(tr("Cannot create VideoSource."));
        else if (failure && failure->stage == OpenStage::Convert)
            m_log->log(tr("Cannot convert video to RGBA"));
        else {
            if (failure && failure->stage == OpenStage::Indexer)
                qDebug().noquote() << tr("Indexing error occurred: %1").arg(text); // HikariLogDebug
            m_log->log(m_video->status());
        }
    }
    m_videoFailureLogged = failed;
}

void Application::rememberRecentVideo(const QString &path)
{
    m_recentVideo.add(path.toStdString());
    m_settings->settings().set("recent.video", m_recentVideo.entries());
}

void Application::rememberRecentKeyframes(const QString &path)
{
    m_recentKeyframes.add(QDir::toNativeSeparators(path).toStdString());
    m_settings->settings().set("recent.keyframes", m_recentKeyframes.entries());
}

// HikariSubFrame::AppendRecent (HikariSubFrame.cpp:1549-1591): missing local
// files leave the list (and the list is written), each row "<n> <name>".
QVariantList Application::recentRows(application::RecentFiles &list, const char *setting)
{
    if (list.prune(missingLocalFile))
        m_settings->settings().set(setting, list.entries());
    QVariantList rows;
    int n = 0;
    for (const auto &entry : list.entries()) {
        const QString path = QString::fromStdString(entry);
        rows << QVariantMap{{QStringLiteral("path"), path},
                            {QStringLiteral("label"), QStringLiteral("%1 %2").arg(++n).arg(QFileInfo(path).fileName())}};
    }
    return rows;
}

QVariantList Application::recentVideos()
{
    return recentRows(m_recentVideo, "recent.video");
}

QVariantList Application::recentKeyframes()
{
    return recentRows(m_recentKeyframes, "recent.keyframes");
}

// GLOBAL_OPEN_KEYFRAMES' dialog and OnRecent's keyframes (HikariSubFrame.cpp:
// 1061-1065, 1631-1635): KeyframesPath, OpenKeyframes, SetRecent(3), the
// list taking the file whether or not it held keyframes.
QString Application::openKeyframesFile(const QString &path)
{
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    const QString problem = openKeyframes(QUrl::fromLocalFile(local));
    rememberRecentKeyframes(local);
    return problem;
}

// The video dialog: the subtitles' folder, else the latest recent video's
// (HikariSubFrame.cpp:1039-1042).
QUrl Application::videoDialogFolder() const
{
    QString from;
    const auto target = m_workspace.editingTarget();
    if (const auto destination = target ? m_files->destination(*target) : std::nullopt; destination)
        from = QString::fromStdString(destination->value);
    if (from.isEmpty() && !m_recentVideo.entries().empty())
        from = QString::fromStdString(m_recentVideo.entries().front());
    if (from.isEmpty() || application::isDummyVideo(from.toStdString()))
        return {};
    return QUrl::fromLocalFile(QFileInfo(from).absolutePath());
}

// The keyframes dialog: the video's folder, else the latest recent
// keyframes' (HikariSubFrame.cpp:1055-1058).
QUrl Application::keyframesDialogFolder() const
{
    QString from = QString::fromStdString(m_video->session().path());
    if (application::isDummyVideo(from.toStdString()))
        from.clear(); // HikariPathDir of the dummy's text: no folder
    if (from.isEmpty() && !m_recentKeyframes.entries().empty())
        from = QString::fromStdString(m_recentKeyframes.entries().front());
    if (from.isEmpty())
        return {};
    return QUrl::fromLocalFile(QFileInfo(from).absolutePath());
}

// VideoBox::OpenKeyframes with the audio box loaded and no video
// (VideoBox.cpp:1722-1741): the frames at 23.976 fps go to the box, which
// snaps half a frame earlier (AudioDisplay.cpp:2506-2509). False when it does
// not apply (no audio): the file then waits for a video.
bool Application::keyframesWithoutVideo(const QString &path, QString &problem)
{
    if (!m_audio || !m_audio->hasAudio())
        return false;
    m_pendingKeyframes.clear(); // legacy clears m_KeyframesFileName
    QFile f(path);
    QByteArray bytes;
    if (f.open(QIODevice::ReadOnly))
        bytes = f.readAll();
    const auto frames =
        application::parseKeyframes(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (frames.empty()) {
        problem = tr("Invalid keyframes format");
        return true;
    }
    auto ms = application::keyframesWithoutVideo(frames);
    std::vector<int> snap;
    snap.reserve(ms.size());
    for (const int keyMs : ms)
        snap.push_back(application::keyframeSnapWithoutVideo(keyMs));
    m_audio->setKeyframeSnapTimes(std::move(snap));
    m_audio->setKeyframes(std::move(ms));
    return true;
}

QVariantMap Application::dummyVideoDefaults() const
{
    // DummyVideo's controls (DummyVideo.cpp:23-99)
    return {{QStringLiteral("resolutions"),
             QStringList{QStringLiteral("640x480 (SD fullscreen)"), QStringLiteral("704x480 (SD anamorphic)"),
                         QStringLiteral("640x360 (SD widescreen)"), QStringLiteral("704x396 (SD widescreen)"),
                         QStringLiteral("640x352 (SD widescreen MOD16)"), QStringLiteral("704x400 (SD widescreen MOD16)"),
                         QStringLiteral("1024x576 (SuperPAL widescreen)"), QStringLiteral("1280x720 (HD 720p)"),
                         QStringLiteral("1920x1080 (HD 1080p)"), QStringLiteral("3840x2160 (4K)")}},
            {QStringLiteral("resolution"), 8},
            {QStringLiteral("width"), 1920},
            {QStringLiteral("height"), 1080},
            {QStringLiteral("colour"), QStringLiteral("&HFEA32F&")},
            {QStringLiteral("fpsChoices"), QStringList{QStringLiteral("23.976"), QStringLiteral("24"), QStringLiteral("25"),
                                                       QStringLiteral("29.97"), QStringLiteral("30"), QStringLiteral("60")}},
            {QStringLiteral("fps"), QStringLiteral("23.976")},
            {QStringLiteral("duration"), QStringLiteral("0:25:00.00")},
            {QStringLiteral("frames"), application::dummyVideoDialogFrames()}};
}

// GLOBAL_OPEN_DUMMY_VIDEO's OK (HikariSubFrame.cpp:1069-1075): GetDummyText,
// then the video loads as any other (Notebook::LoadVideo, the audio left as
// it is). {fps (text), durationMs, width, height, red, green, blue, pattern}.
QString Application::openDummyVideo(const QVariantMap &values)
{
    const auto text = application::dummyVideoText(
        values.value(QStringLiteral("fps")).toString().toStdString(), values.value(QStringLiteral("durationMs")).toInt(),
        values.value(QStringLiteral("width")).toInt(), values.value(QStringLiteral("height")).toInt(),
        static_cast<std::uint8_t>(values.value(QStringLiteral("red")).toInt()),
        static_cast<std::uint8_t>(values.value(QStringLiteral("green")).toInt()),
        static_cast<std::uint8_t>(values.value(QStringLiteral("blue")).toInt()),
        values.value(QStringLiteral("pattern")).toBool());
    if (!text)
        return tr("Invalid FPS value.");
    m_video->openVideo(QString::fromStdString(*text));
    return {};
}

// VIDEO_PREVIOUS_FILE / VIDEO_NEXT_FILE (VideoBox::NextFile): the folder of
// the video (legacy VideoPath, kept by a failed open), else of the oldest
// recent video (legacy videorec[size - 1]), listed as wxDir::GetAllFiles
// lists it: files only, hidden ones left out, in the file system's own order.
bool Application::nextVideoFile(bool next)
{
    auto &session = m_video->session();
    QString path = QString::fromStdString(session.path());
    if (path.isEmpty()) {
        if (m_recentVideo.entries().empty())
            return false; // legacy indexes an empty list (V3-next-file-no-recent)
        path = QString::fromStdString(m_recentVideo.entries().back());
    }
    // a folder that cannot be opened (a dummy video's text has none) walks
    // the last listing again (legacy's `files` member)
    std::optional<std::vector<std::string>> files;
    if (!application::isDummyVideo(path.toStdString())) {
        const QDir dir = QFileInfo(path).absoluteDir();
        if (dir.exists() && dir.isReadable()) {
            files.emplace();
            for (const QString &name : dir.entryList(QDir::Files, QDir::Unsorted))
                files->push_back(QDir::toNativeSeparators(dir.absoluteFilePath(name)).toStdString());
        }
    }
    const auto step = m_nextFile.step(std::move(files), path.toStdString(), next);
    if (step.kind == application::NextFileStep::Kind::Open) {
        m_video->openVideo(QString::fromStdString(step.path));
        return true;
    }
    // Seek(0); Pause(false): back to the start, and the play state toggled
    const bool done = session.restartToggled();
    emit m_video->changed();
    return done;
}

} // namespace hikari::app
