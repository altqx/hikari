#pragma once

#include "hikari/application/matroska.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace hikari::app {

class MatroskaController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // 0 idle, 1 listing the tracks, 2 choosing, 3 reading.
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString elapsed READ elapsed NOTIFY progressChanged)
    // The MKV's file name while a load runs, else "".
    Q_PROPERTY(QString source READ source NOTIFY stateChanged)
public:
    struct Hooks {
        std::function<QString()> videoPath;
        std::function<bool()> modified;
        std::function<bool()> targetHasSubtitlesPath;
        // `newTab`: into a new tab rather than the editing target.
        std::function<void(application::MatroskaLoaded loaded, bool newTab)> apply;
        std::function<void()> started;
        std::function<void(const QString &text)> log;
    };
    MatroskaController(std::unique_ptr<application::MatroskaPort> port, Hooks hooks, QObject *parent = nullptr);
    ~MatroskaController() override;

    bool available() const { return m_available; }
    int state() const { return int(m_load->state()); }
    int progress() const { return m_progress; }
    QString elapsed() const { return m_elapsedText; }
    QString source() const { return state() == 0 ? QString() : m_source; }
    void refresh();

    Q_INVOKABLE bool needsSaveQuestion() const;
    // Reads the tab's video, or the file beginFile() took; false when unavailable or a load runs.
    Q_INVOKABLE bool start();
    // True for a ".mkv" extension in any case.
    Q_INVOKABLE static bool opensAsMatroska(const QString &path);
    // The next start() reads `path`, into a new tab when the editing target has subtitles of a
    // file (decided now). False while a load runs.
    Q_INVOKABLE bool beginFile(const QString &path);
    Q_INVOKABLE void choose(int row);
    // Also drops a file that beginFile() took.
    Q_INVOKABLE void cancel();

    application::MatroskaPort &port() { return *m_port; }

signals:
    void availableChanged();
    void stateChanged();
    void progressChanged();
    void noTracks();
    void chooseTrack(const QStringList &labels);
    void finished(bool loaded);

private:
    std::unique_ptr<application::MatroskaPort> m_port;
    Hooks m_hooks;
    std::unique_ptr<application::MatroskaSubtitleLoad> m_load;
    bool m_available = false;
    int m_progress = 0;
    QString m_elapsedText;
    QElapsedTimer m_clock;
    qint64 m_lastProgress = 0;
    QString m_file;
    bool m_fileNewTab = false;
    QString m_source;
};

}
