#include "hikari/app/application.h"

#include "hikari/application/keyframe_files.h"
#include "hikari/application/legacy_dir.h"

#include <QCoreApplication>
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
    m_recentVideo.set(m_settings->settings().list("recent.video"));
    m_recentKeyframes.set(m_settings->settings().list("recent.keyframes"));
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

void Application::logVideoFailure()
{
    const auto &session = m_video->session();
    const bool failed = session.state() == application::VideoSession::State::Failed;
    if (failed && !m_videoFailureLogged) {
        const auto &failure = session.openFailure();
        const QString text = failure ? QString::fromStdString(failure->message) : QString();
        using application::OpenStage;
        const auto notice = [this](const QString &problem) {
            m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Video", "Open video"), problem);
        };
        if (failure && failure->stage == OpenStage::Indexing)
            notice(QCoreApplication::translate("HikariSub.Video", "Indexing error occurred: %1").arg(text));
        else if (failure && failure->stage == OpenStage::Source)
            notice(QCoreApplication::translate("HikariSub.Video", "Cannot create VideoSource."));
        else if (failure && failure->stage == OpenStage::Convert)
            notice(QCoreApplication::translate("HikariSub.Video", "Cannot convert video to RGBA"));
        else {
            if (failure && failure->stage == OpenStage::Indexer)
                qDebug().noquote() << QCoreApplication::translate("HikariSub.Video", "Indexing error occurred: %1")
                                          .arg(text);
            notice(m_video->status());
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

QString Application::openKeyframesFile(const QString &path)
{
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    const QString problem = openKeyframes(QUrl::fromLocalFile(local));
    rememberRecentKeyframes(local);
    return problem;
}

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

QUrl Application::keyframesDialogFolder() const
{
    QString from = QString::fromStdString(m_video->session().path());
    if (application::isDummyVideo(from.toStdString()))
        from.clear();
    if (from.isEmpty() && !m_recentKeyframes.entries().empty())
        from = QString::fromStdString(m_recentKeyframes.entries().front());
    if (from.isEmpty())
        return {};
    return QUrl::fromLocalFile(QFileInfo(from).absolutePath());
}

bool Application::keyframesWithoutVideo(const QString &path, QString &problem)
{
    if (!m_audio || !m_audio->hasAudio())
        return false;
    m_pendingKeyframes.clear();
    QFile f(path);
    QByteArray bytes;
    if (f.open(QIODevice::ReadOnly))
        bytes = f.readAll();
    const auto frames =
        application::parseKeyframes(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (frames.empty()) {
        problem = QCoreApplication::translate("HikariSub.Video", "Invalid keyframes format");
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
            {QStringLiteral("frames"), *application::dummyVideoFrames("23.976", 1500000)}};
}

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
        return QCoreApplication::translate("HikariSub.Video", "Invalid FPS value.");
    m_video->openVideo(QString::fromStdString(*text));
    return {};
}

bool Application::nextVideoFile(bool next)
{
    const auto step = nextVideoStep(m_nextFile, next);
    if (!step)
        return false;
    if (step->kind == application::NextFileStep::Kind::Open) {
        m_video->openVideo(QString::fromStdString(step->path));
        return true;
    }
    auto &session = m_video->session();
    const bool done = session.restartToggled();
    emit m_video->changed();
    return done;
}

QString Application::nextVideoFileName(bool next) const
{
    return QFileInfo(nextVideoFilePath(next)).fileName();
}

QString Application::nextVideoFilePath(bool next) const
{
    application::NextFileWalker walker = m_nextFile;
    const auto step = nextVideoStep(walker, next);
    if (!step || step->kind != application::NextFileStep::Kind::Open)
        return {};
    return QString::fromStdString(step->path);
}

bool Application::nextVideoFileIfStill(bool next, const QString &expected)
{
    if (nextVideoFilePath(next) != expected)
        return false;
    return nextVideoFile(next);
}

int Application::dummyVideoFrames(const QString &fps, int durationMs) const
{
    const auto frames = application::dummyVideoFrames(fps.toStdString(), durationMs);
    return frames ? *frames : -1;
}

std::optional<application::NextFileStep> Application::nextVideoStep(application::NextFileWalker &walker,
                                                                     bool next) const
{
    QString path = QString::fromStdString(m_video->session().path());
    if (path.isEmpty()) {
        if (m_recentVideo.entries().empty())
            return std::nullopt;
        path = QString::fromStdString(m_recentVideo.entries().back());
    }
    std::optional<std::vector<std::string>> files;
    if (!application::isDummyVideo(path.toStdString())) {
#ifdef _WIN32
        constexpr auto format = application::PathFormat::Dos;
#else
        constexpr auto format = application::PathFormat::Unix;
#endif
        // MUST KILL: entries must spell the folder as path does, or the walker never finds path.
        const QString folder = QString::fromStdString(application::nextFileFolder(path.toStdString(), format));
        if (const auto listed = application::legacy_dir::entries(QFileInfo(folder).filesystemFilePath(), {},
                                                                 application::legacy_dir::Files)) {
            files.emplace();
            const QString prefix = folder.endsWith(QDir::separator()) ? folder : folder + QDir::separator();
            for (const auto &file : *listed)
                files->push_back((prefix + QFileInfo(file).fileName()).toStdString());
        }
    }
    return walker.step(std::move(files), path.toStdString(), next);
}

} // namespace hikari::app
