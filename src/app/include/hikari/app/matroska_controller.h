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
//
// File > Open (GLOBAL_OPEN_SUBS) on an MKV file (HikariSubFrame.cpp:1008-1033
// at 20d647c4): OnMkvSubs on the chosen file instead of OpenFile. A tab with
// subtitles of a file (SubsPath) keeps them: the file loads into a new tab
// (InsertTab), which asks nothing and is not left behind when the load ends
// without subtitles. Otherwise the load goes into the tab as above.

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
        // tab->SubsPath != emptyString: File > Open loads into a new tab.
        std::function<bool()> targetHasSubtitlesPath;
        // `newTab`: File > Open's new tab rather than the editing target.
        std::function<void(application::MatroskaLoaded loaded, bool newTab)> apply;
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
    // Demux::Open and GetSubtitles on the tab's video, or on the file that
    // beginFile() took; false when the menu's check refuses (or a load runs).
    Q_INVOKABLE bool start();
    // File > Open's route: true for a file whose extension is "mkv"
    // (AfterLast('.'), in any case here: Y9-open-mkv-case).
    Q_INVOKABLE static bool opensAsMatroska(const QString &path);
    // File > Open on an MKV file: the next start() reads `path`, into a new
    // tab when the editing target has subtitles of a file (decided now, so a
    // save from the question does not move the load). False while a load
    // runs.
    Q_INVOKABLE bool beginFile(const QString &path);
    // HikariListBox's OK (or a double click): the row's track.
    Q_INVOKABLE void choose(int row);
    // The chooser's Cancel or the progress dialog's (or the question's, which
    // drops a file that beginFile() took).
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
    QString m_file;          // beginFile's MKV file ("" for the tab's video)
    bool m_fileNewTab = false;
};

} // namespace hikari::app
