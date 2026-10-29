#include "hikari/app/matroska_controller.h"

#include "hikari/core/ass_save.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace hikari::app {

namespace {

QString elapsedText(qint64 ms)
{
    const auto raw = core::legacy::assTimeText(ms);
    return QCoreApplication::translate("HikariSub.Matroska", "Time elapsed: %1")
        .arg(QString::fromUtf8(reinterpret_cast<const char *>(raw.data()), qsizetype(raw.size())));
}

}

MatroskaController::MatroskaController(std::unique_ptr<application::MatroskaPort> port, Hooks hooks, QObject *parent)
    : QObject(parent), m_port(std::move(port)), m_hooks(std::move(hooks)), m_elapsedText(elapsedText(0))
{
    application::MatroskaSubtitleLoad::Hooks h;
    h.cannotOpen = [this](const std::string &error) {
        if (m_hooks.log)
            m_hooks.log(QCoreApplication::translate("HikariSub.Matroska", "Indexing error occurred: %1").arg(QString::fromUtf8(error)));
    };
    h.noTracks = [this] {
        emit stateChanged();
        emit noTracks();
    };
    h.chooseTrack = [this](const std::vector<std::u8string> &labels) {
        QStringList out;
        for (const auto &l : labels)
            out << QString::fromUtf8(reinterpret_cast<const char *>(l.data()), qsizetype(l.size()));
        emit stateChanged();
        emit chooseTrack(out);
    };
    h.reading = [this] {
        m_progress = 0;
        m_clock.start();
        m_lastProgress = 0;
        m_elapsedText = elapsedText(0);
        emit progressChanged();
        emit stateChanged();
    };
    h.progress = [this](int percent) {
        const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
        if (m_lastProgress + 10 < now) {
            m_progress = percent;
            m_elapsedText = elapsedText(now);
            emit progressChanged();
        }
        m_lastProgress = now;
    };
    h.loaded = [this](application::MatroskaLoaded loaded) {
        const bool newTab = !m_file.isEmpty() && m_fileNewTab;
        m_file.clear();
        emit stateChanged();
        if (m_hooks.apply)
            m_hooks.apply(std::move(loaded), newTab);
        emit finished(true);
    };
    h.ended = [this](application::MatroskaFailure) {
        m_file.clear();
        emit stateChanged();
        emit finished(false);
    };
    m_load = std::make_unique<application::MatroskaSubtitleLoad>(*m_port, std::move(h));
    refresh();
}

MatroskaController::~MatroskaController()
{
    // MUST KILL: order matters; hooks and signals are cut, then the port goes before m_load.
    blockSignals(true);
    m_hooks = {};
    m_port.reset();
    m_load.reset();
}

void MatroskaController::refresh()
{
    const QString path = m_hooks.videoPath ? m_hooks.videoPath() : QString();
    const bool available = application::subtitlesFromMkvEnabled(QFileInfo(path).fileName().toStdU16String());
    if (available != m_available) {
        m_available = available;
        emit availableChanged();
    }
}

bool MatroskaController::needsSaveQuestion() const
{
    if (!m_file.isEmpty() && m_fileNewTab)
        return false;
    return m_hooks.modified && m_hooks.modified();
}

bool MatroskaController::opensAsMatroska(const QString &path)
{
    return application::isMkvExtension(QFileInfo(path).fileName().toStdU16String());
}

bool MatroskaController::beginFile(const QString &path)
{
    if (m_load->state() != application::MatroskaSubtitleLoad::State::Idle)
        return false;
    m_file = path;
    m_fileNewTab = m_hooks.targetHasSubtitlesPath && m_hooks.targetHasSubtitlesPath();
    return true;
}

bool MatroskaController::start()
{
    if (m_load->state() != application::MatroskaSubtitleLoad::State::Idle)
        return false;
    QString path = m_file;
    if (path.isEmpty()) {
        refresh();
        if (!m_available)
            return false;
        path = m_hooks.videoPath();
    }
    m_source = QFileInfo(path).fileName();
    if (m_hooks.started)
        m_hooks.started();
    // FFMS2 takes the path as UTF-8.
    m_load->start(path.toStdString());
    emit stateChanged();
    return true;
}

void MatroskaController::choose(int row)
{
    m_load->choose(row);
    emit stateChanged();
}

void MatroskaController::cancel()
{
    m_file.clear();
    m_load->cancel();
    emit stateChanged();
}

}
