#pragma once

// Y9: GRID_SUBS_FROM_MKV, "Load subtitles from an MKV/OGM file" (legacy
// SubsGrid::OnMkvSubs and Demux::GetSubtitles, SubsGrid.cpp:289, 879-881,
// 1134-1194 and Demux.cpp:66-176 at 20d647c4): enabled when the tab's video
// name ends with ".mkv" or ".ogm"; "Save the file before loading subtitles
// from the MKV?" for a modified Document; the tracks, "The file does not
// contain any subtitle tracks.", "Choose subtitle track" for several, and the
// "Loading subtitles from Matroska." progress with its Cancel. The media
// helper reads the file; the loaded Document replaces the tab's only once it
// is complete, so a cancel, a failure or the helper's loss changes nothing.

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
    // 0 idle, 1 listing the tracks, 2 choosing, 3 reading (ProgressSink shown).
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    // ProgresDialog's "Time elapsed: %s" (SubsTime::raw of the elapsed ms).
    Q_PROPERTY(QString elapsed READ elapsed NOTIFY progressChanged)
public:
    struct Hooks {
        std::function<QString()> videoPath;  // the editing target's tab VideoPath
        std::function<bool()> modified;      // file->IsModified()
        std::function<void(application::MatroskaLoaded loaded)> apply;
        std::function<void(const QString &text)> log; // HikariLog
    };
    MatroskaController(std::unique_ptr<application::MatroskaPort> port, Hooks hooks, QObject *parent = nullptr);
    ~MatroskaController() override;

    bool available() const { return m_available; }
    int state() const { return int(m_load->state()); }
    int progress() const { return m_progress; }
    QString elapsed() const { return m_elapsedText; }
    // The tab or its video changed: the menu's check again.
    void refresh();

    // OnMkvSubs' question: true when the Document has unsaved changes.
    Q_INVOKABLE bool needsSaveQuestion() const;
    // Demux::Open and GetSubtitles on the tab's video; false when the menu's
    // check refuses.
    Q_INVOKABLE bool start();
    // HikariListBox's OK (or a double click): the row's track.
    Q_INVOKABLE void choose(int row);
    // The chooser's Cancel or the progress dialog's.
    Q_INVOKABLE void cancel();

    application::MatroskaPort &port() { return *m_port; }

signals:
    void availableChanged();
    void stateChanged();
    void progressChanged();
    void noTracks();                               // HikariMessageBox
    void chooseTrack(const QStringList &labels);   // HikariListBox "Choose subtitle track"
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
};

} // namespace hikari::app
