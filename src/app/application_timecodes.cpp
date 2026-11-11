#include "hikari/app/application.h"

#include "hikari/application/media_association.h"
#include "hikari/application/timecodes_file.h"
#include "hikari/application/video_sources.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>

namespace hikari::app {

namespace {

const char *problemText(application::TimecodesProblem problem)
{
    using application::TimecodesProblem;
    switch (problem) {
    case TimecodesProblem::UnknownFormat: return QT_TRANSLATE_NOOP("HikariSub.Video", "Unknown timecodes format");
    case TimecodesProblem::MalformedLine: return QT_TRANSLATE_NOOP("HikariSub.Video", "Malformed timecodes line");
    case TimecodesProblem::TooFewTimes: return QT_TRANSLATE_NOOP("HikariSub.Video", "Must have at least two timecodes to do anything useful");
    case TimecodesProblem::TooManyTimes: return QT_TRANSLATE_NOOP("HikariSub.Video", "Timecode file exceeds the 10000000 entry limit");
    case TimecodesProblem::OutOfOrder: return QT_TRANSLATE_NOOP("HikariSub.Video", "Timecodes are out of order");
    case TimecodesProblem::AllIdentical: return QT_TRANSLATE_NOOP("HikariSub.Video", "Timecodes are all identical");
    case TimecodesProblem::NegativeFrame: return QT_TRANSLATE_NOOP("HikariSub.Video", "Cannot specify frame rate for negative frames");
    case TimecodesProblem::EndBeforeStart: return QT_TRANSLATE_NOOP("HikariSub.Video", "End frame must be greater than or equal to start frame");
    case TimecodesProblem::RangeTooLarge: return QT_TRANSLATE_NOOP("HikariSub.Video", "V1 timecode range exceeds the 10000000 frame limit");
    case TimecodesProblem::FpsNotPositive: return QT_TRANSLATE_NOOP("HikariSub.Video", "FPS must be greater than zero");
    case TimecodesProblem::FpsAbove1000: return QT_TRANSLATE_NOOP("HikariSub.Video", "FPS must not be greater than 1000");
    case TimecodesProblem::Overlap: return QT_TRANSLATE_NOOP("HikariSub.Video", "Override ranges must not overlap");
    case TimecodesProblem::TimeOutOfRange: return QT_TRANSLATE_NOOP("HikariSub.Video", "Timecode exceeds the supported timestamp range");
    }
    return "";
}

QString errorText(const application::TimecodesError &error)
{
    const QString problem = QCoreApplication::translate("HikariSub.Video", problemText(error.problem));
    const QString located =
        error.line > 0 ? QCoreApplication::translate("HikariSub.Video", "Line %1: %2").arg(error.line).arg(problem) : problem;
    return QCoreApplication::translate("HikariSub.Video", "Failed to parse timecodes file: %1").arg(located);
}

QString fileSystemPath(const QString &path)
{
    return QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
}

}

std::expected<application::Timecodes, QString> Application::readTimecodes(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(
            QCoreApplication::translate("HikariSub.Video", "Could not open the timecodes file %1").arg(QDir::toNativeSeparators(path)));
    const QByteArray bytes = file.readAll();
    const auto parsed =
        application::parseTimecodes(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!parsed)
        return std::unexpected(errorText(parsed.error()));
    return *parsed;
}

void Application::loadTabTimecodes(TabMedia &media, const QString &path, const application::Timecodes &timecodes)
{
    media.timecodes = QDir::toNativeSeparators(path);
    media.frameTimes = application::AegisubFrameTimes::fromTimecodes(timecodes);
    media.timecodesDropped = false;
    media.timecodesListFrames = timecodes.format != application::TimecodesFormat::V1;
}

void Application::warnShortTimecodes(TabMedia &media)
{
    const auto &video = activeMedia().video->session();
    if (!media.timecodesListFrames || !media.frameTimes || video.state() != application::VideoSession::State::Ready ||
        video.frameCount() <= 0)
        return;
    media.timecodesListFrames = false;
    const auto held = media.frameTimes->heldFrames();
    if (held >= video.frameCount())
        return;
    m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Video", "Open timecodes file"),
                  QCoreApplication::translate("HikariSub.Video", "The timecodes file ends before the video does (frame "
                                                                 "%1 of %2). Later frames continue at its average frame rate.")
                      .arg(held)
                      .arg(video.frameCount()));
}

void Application::showTabTimecodes(std::optional<application::DocumentId> document)
{
    const auto it = document ? m_tabMedia.find(document->value) : m_tabMedia.end();
    auto times = it != m_tabMedia.end() ? it->second.frameTimes : std::nullopt;
    auto &video = activeMedia().video->session();
    if (times || video.replacedFrameTimes())
        video.setFrameTimes(std::move(times));
    emit timecodesChanged();
}

QString Application::openTimecodesFile(const QString &path)
{
    const QString local = fileSystemPath(path);
    auto times = readTimecodes(local);
    if (!times) {
        if (m_recentTimecodes.remove(QDir::toNativeSeparators(local).toStdString()))
            m_settings->settings().set("recent.timecodes", m_recentTimecodes.entries());
        return times.error();
    }
    if (home())
        newDocument();
    const auto target = m_workspace.editingTarget();
    if (!target)
        return {};
    TabMedia &media = m_tabMedia[target->value];
    loadTabTimecodes(media, local, *times);
    rememberRecentTimecodes(local);
    showTabTimecodes(target);
    warnShortTimecodes(media);
    refreshViews();
    return {};
}

QString Application::saveTimecodesFile(const QString &path)
{
    const auto &video = activeMedia().video->session();
    const auto times = video.aegisubFrameTimes();
    if (!times)
        return QCoreApplication::translate("HikariSub.Video", "There are no frame times to save");
    const QString local = fileSystemPath(path);
    const int frameCount = video.state() == application::VideoSession::State::Ready ? video.frameCount() : -1;
    const std::string text = application::timecodesText(*times, frameCount);
    QSaveFile file(local);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size()) ||
        !file.commit())
        return QCoreApplication::translate("HikariSub.Video", "Could not save the timecodes file %1").arg(QDir::toNativeSeparators(local));
    rememberRecentTimecodes(local);
    return {};
}

void Application::closeTimecodes()
{
    const auto target = m_workspace.editingTarget();
    const auto it = target ? m_tabMedia.find(target->value) : m_tabMedia.end();
    if (it == m_tabMedia.end() || it->second.timecodes.isEmpty())
        return;
    it->second.timecodes.clear();
    it->second.frameTimes.reset();
    it->second.timecodesDropped = true;
    showTabTimecodes(target);
    refreshViews();
}

std::vector<application::ProjectGarbageEdit> Application::timecodesGarbage(application::DocumentId document,
                                                                          const application::DestinationKey &destination) const
{
    const auto it = m_tabMedia.find(document.value);
    if (it == m_tabMedia.end())
        return {};
    const TabMedia &media = it->second;
    if (media.timecodes.isEmpty()) {
        if (!media.timecodesDropped)
            return {};
        return {{std::u8string(application::kTimecodesFileKey), std::nullopt}};
    }
    QString path = QDir::fromNativeSeparators(media.timecodes);
    if (!destination.value.empty()) {
        const QString relative = QFileInfo(QString::fromStdString(destination.value)).dir().relativeFilePath(path);
        if (!QDir::isAbsolutePath(relative))
            path = relative;
    }
    const QByteArray utf8 = path.toUtf8();
    return {{std::u8string(application::kTimecodesFileKey),
             std::u8string(reinterpret_cast<const char8_t *>(utf8.constData()), static_cast<std::size_t>(utf8.size()))}};
}

QString Application::tabTimecodes(std::optional<application::DocumentId> document) const
{
    const auto it = document ? m_tabMedia.find(document->value) : m_tabMedia.end();
    return it == m_tabMedia.end() ? QString() : it->second.timecodes;
}

QString Application::timecodesFile() const
{
    return tabTimecodes(m_workspace.editingTarget());
}

void Application::rememberRecentTimecodes(const QString &path)
{
    m_recentTimecodes.add(QDir::toNativeSeparators(path).toStdString());
    m_settings->settings().set("recent.timecodes", m_recentTimecodes.entries());
}

QVariantList Application::recentTimecodes()
{
    return recentRows(m_recentTimecodes, "recent.timecodes");
}

QUrl Application::timecodesDialogFolder() const
{
    QString from = timecodesFile();
    if (from.isEmpty())
        from = QString::fromStdString(activeMedia().video->session().path());
    if (application::isDummyVideo(from.toStdString()))
        from.clear();
    if (from.isEmpty() && !m_recentTimecodes.entries().empty())
        from = QString::fromStdString(m_recentTimecodes.entries().front());
    if (from.isEmpty())
        return {};
    return QUrl::fromLocalFile(QFileInfo(from).absolutePath());
}

}
