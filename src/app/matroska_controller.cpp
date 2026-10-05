#include "hikari/app/matroska_controller.h"

#include "hikari/core/ass_save.h"

#include <QFileInfo>

namespace hikari::app {

namespace {

QString elapsedText(qint64 ms)
{
    // _("Time elapsed: %s") with SubsTime::raw() (ProgressDialog.cpp:108-110).
    const auto raw = core::legacy::assTimeText(ms);
    return MatroskaController::tr("Time elapsed: %1")
        .arg(QString::fromUtf8(reinterpret_cast<const char *>(raw.data()), qsizetype(raw.size())));
}

} // namespace

MatroskaController::MatroskaController(std::unique_ptr<application::MatroskaPort> port, Hooks hooks, QObject *parent)
    : QObject(parent), m_port(std::move(port)), m_hooks(std::move(hooks)), m_elapsedText(elapsedText(0))
{
    application::MatroskaSubtitleLoad::Hooks h;
    h.cannotOpen = [this](const std::string &error) {
        // Demux::Open (Demux.cpp:44-47).
        if (m_hooks.log)
            m_hooks.log(tr("Indexing error occurred: %1").arg(QString::fromUtf8(error)));
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
        // ProgresDialog::Progress: shown when more than 10 ms passed.
        const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
        if (m_lastProgress + 10 < now) {
            m_progress = percent;
            m_elapsedText = elapsedText(now);
            emit progressChanged();
        }
        m_lastProgress = now;
    };
    h.loaded = [this](application::MatroskaLoaded loaded) {
        emit stateChanged();
        if (m_hooks.apply)
            m_hooks.apply(std::move(loaded));
        emit finished(true);
    };
    h.ended = [this](application::MatroskaFailure) {
        emit stateChanged();
        emit finished(false);
    };
    m_load = std::make_unique<application::MatroskaSubtitleLoad>(*m_port, std::move(h));
    refresh();
}

MatroskaController::~MatroskaController()
{
    // The helper goes first; whatever it resolves on its way out reaches no
    // hook and no signal.
    blockSignals(true);
    m_hooks = {};
    m_port.reset();
    m_load.reset();
}

void MatroskaController::refresh()
{
    // SubsGrid.cpp:289: tab->VideoName ends with ".mkv" or ".ogm", in any
    // case here (Y9-mkv-case).
    const QString path = m_hooks.videoPath ? m_hooks.videoPath() : QString();
    const bool available = application::subtitlesFromMkvEnabled(QFileInfo(path).fileName().toStdU16String());
    if (available != m_available) {
        m_available = available;
        emit availableChanged();
    }
}

bool MatroskaController::needsSaveQuestion() const
{
    return m_hooks.modified && m_hooks.modified();
}

bool MatroskaController::start()
{
    refresh();
    if (!m_available || m_load->state() != application::MatroskaSubtitleLoad::State::Idle)
        return false;
    // mkvpath = tab->VideoPath (SubsGrid.cpp:1143); FFMS2 gets it as UTF-8.
    m_load->start(m_hooks.videoPath().toStdString());
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
    m_load->cancel();
    emit stateChanged();
}

} // namespace hikari::app
