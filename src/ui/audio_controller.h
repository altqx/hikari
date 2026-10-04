#pragma once

// The Audio panel's controller (A1; legacy AudioBox and AudioDisplay). It
// owns the audio box's audio and view, follows the active Line the way
// legacy SetDialogue/Update do, and keeps the marks the application hands it
// (other Lines, keyframes, the paused video's time). The AudioDisplay item
// draws what scene() describes. Options are legacy's defaults until the
// settings registry (O1) exists.

#include "hikari/application/audio_box.h"
#include "hikari/application/audio_display.h"
#include "hikari/application/audio_playback.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace hikari::ui {

// Legacy DeleteOldAudioCache over `folder`, `inUse` (and its .part) excepted.
void deleteOldAudioCache(const std::filesystem::path &folder, const std::filesystem::path &inUse, int limit);

class AudioController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    // Legacy ABox: audio is open, loading or shown.
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY changed)
    // The display draws: the decoding progress, then the waveform.
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    // The scrollbar in legacy units (12 columns).
    Q_PROPERTY(int scrollPosition READ scrollPosition NOTIFY displayChanged)
    Q_PROPERTY(int scrollPage READ scrollPage NOTIFY displayChanged)
    Q_PROPERTY(int scrollRange READ scrollRange NOTIFY displayChanged)
    // Legacy "Choose the track" (HikariListBox): its rows while it is asking.
    Q_PROPERTY(QStringList trackChoices READ trackChoices NOTIFY trackChoicesChanged)
    // A4: the player is playing (legacy player->IsPlaying()).
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
public:
    explicit AudioController(application::DisplayAudioPort &own, QObject *parent = nullptr);

    application::AudioBox &box() { return m_box; }
    const application::AudioView &view() const { return m_view; }
    const application::AudioDisplayOptions &options() const { return m_options; }

    bool hasAudio() const { return m_box.isOpen(); }
    bool loaded() const { return m_box.state() == application::AudioBox::State::Loading || ready(); }
    bool ready() const { return m_box.state() == application::AudioBox::State::Ready; }
    QString status() const;
    QString path() const { return QString::fromStdString(m_box.path()); }

    // GLOBAL_OPEN_AUDIO (a file), GLOBAL_OPEN_DUMMY_AUDIO, GLOBAL_CLOSE_AUDIO.
    Q_INVOKABLE void openAudio(const QString &path);
    Q_INVOKABLE void openAudioUrl(const QUrl &url) { openAudio(url.toLocalFile()); }
    Q_INVOKABLE void openDummy() { openAudio(QString::fromLatin1(application::AudioBox::kDummyName)); }
    Q_INVOKABLE void closeAudio();
    // An indexed video's audio (legacy RendererFFMS2::OpenFile's
    // OpenAudioInTab(40000): the video's provider and track): the video's
    // track, from the index file the video's open wrote.
    void openFromVideo(const QString &path, int track, bool videoNewIndex, const QString &handoffIndexFile = {});
    // Asks "Choose the track" for a video's open (legacy asked inside the
    // video's indexing); answers the row, or nothing on Cancel.
    void askTrack(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer);
    // Legacy DeleteOldAudioCache (when files open with a video open): the
    // cache folder trimmed to AUDIO_CACHE_FILES_LIMIT, the cache in use
    // excepted; while the box is still opening, once its cache is known.
    void trimCache();
    // The options a new open reads (AUDIO_RAM_CACHE, AUDIO_DELAY, the cache
    // folder and its AUDIO_CACHE_FILES_LIMIT, ACCEPTED_AUDIO_STREAM).
    void setSettings(std::function<application::AudioCacheSettings()> settings);

    QStringList trackChoices() const { return m_trackChoices; }
    // The chooser's OK (the row) and Cancel.
    Q_INVOKABLE void chooseTrack(int row);
    Q_INVOKABLE void cancelTrackChoice();

    // The editing target's Lines (legacy keys, with their Grid visibility) and
    // the active one; `reselect` when legacy would call SetDialogue (another
    // active Line, or its times committed).
    void setLines(std::vector<application::AudioLineSpan> lines, int active, bool reselect);
    void setKeyframes(std::vector<int> keyframesMs);
    void setVideoTime(std::optional<int> pausedMs);
    application::AudioMarks marks() const;

    // The display item: its size, the mouse cursor, focus and the wheel.
    void resize(int width, int height, int timelineHeight, int scrollbarThickness);
    void setCursor(std::optional<float> x); // the mouse's; not while playing (A4)
    std::optional<float> cursor() const { return m_cursor; }
    void setFocused(bool focused);
    void wheel(int rotation); // legacy: scrolls by -rotation * w / 360 columns
    Q_INVOKABLE void setScrollPosition(int position); // legacy AudioBox::OnScrollbar

    // What to draw (legacy DoUpdateImage, or DrawProgress while loading),
    // and its revision; the waveform columns are kept until the view changes.
    std::vector<application::AudioShape> scene(const application::AudioTextWidth &textWidth);
    quint64 revision() const { return m_revision; }
    const application::WaveformColumns &columns();

    // A4: playback (legacy AudioBox's play handlers, AudioDisplay::Play,
    // Stop and UpdateTimer). The player plays the box's audio (none: no
    // playback); AUDIO_MARK_PLAY_TIME and AUDIO_VOLUME are read at each play.
    void setPlayer(application::AudioPlayerPort *player);
    void setPlaybackSettings(std::function<int()> markPlayTimeMs, std::function<int()> volume);
    // The video's Playing state and its Pause: legacy Play pauses a playing
    // video, and Stop pauses it instead of touching the audio.
    void setVideoPlayback(std::function<bool()> playing, std::function<void()> pause);
    bool playing() const { return m_player && m_player->playing(); }
    // AUDIO_PLAY (and _ALT, the middle double click), AUDIO_PLAY_LINE (and
    // _ALT), AUDIO_PLAY_500MS_BEFORE/AFTER/FIRST/LAST, AUDIO_PLAY_BEFORE_MARK,
    // AUDIO_PLAY_AFTER_MARK, AUDIO_PLAY_TO_END and AUDIO_STOP.
    Q_INVOKABLE void playSelection() { play(application::PlayMode::Selection); }
    Q_INVOKABLE void playLine() { play(application::PlayMode::Line); }
    Q_INVOKABLE void play500Before() { play(application::PlayMode::Before500); }
    Q_INVOKABLE void play500After() { play(application::PlayMode::After500); }
    Q_INVOKABLE void play500First() { play(application::PlayMode::First500); }
    Q_INVOKABLE void play500Last() { play(application::PlayMode::Last500); }
    Q_INVOKABLE void playBeforeMark() { play(application::PlayMode::BeforeMark); }
    Q_INVOKABLE void playAfterMark() { play(application::PlayMode::AfterMark); }
    Q_INVOKABLE void playToEnd() { play(application::PlayMode::ToEnd); }
    Q_INVOKABLE void stopPlayback();
    void play(application::PlayMode mode);
    // Legacy's default AUDIO_HOTKEY keys (Down and S play, Up and R play the
    // line, H stops, Num 0 / Num . play before / after the mark, Q W E D the
    // 500 ms modes, T to the end), run when they match; the Grid takes them
    // while the box exists, without Down and Up (TabPanel::SetAccels).
    // Answers whether the key was one of them.
    Q_INVOKABLE bool playbackKey(int key, int modifiers, bool inGrid);
    // AUDIO_VOLUME moved (legacy SetVolume while playing).
    void setPlaybackVolume(int slider);
    // The ruler's mark (legacy hasMark and curMarkMS; set by the ruler, A3).
    std::optional<int> markMs() const { return m_markMs; }
    void setMarkMs(std::optional<int> ms) { m_markMs = ms; }
    // The last range handed to the player, and legacy's Stop memory.
    std::optional<application::PlayRange> lastPlayRange() const { return m_lastRange; }
    const application::AudioPlayback *playback() const { return m_playback.get(); }

    int scrollPosition() const { return m_scrollbar.position; }
    int scrollPage() const { return m_scrollbar.page; }
    int scrollRange() const { return m_scrollbar.range; }

signals:
    void changed();
    void displayChanged();
    void cursorChanged();
    // A file was opened and is loading (legacy SetRecent(2) after LoadAudio);
    // `created` when there was no box before (legacy then focuses the display).
    void opened(const QString &path, bool created);
    // A message for the log: legacy HikariLog, or HikariLogDebug (`debug`).
    void logged(const QString &message, bool debug);
    void trackChoicesChanged();
    void playingChanged();

private:
    void ask(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer, bool forBox);
    void boxChanged();
    void newView();
    void reselect();     // legacy SetDialogue
    void update();       // legacy Update: follow the Line, then redraw
    void redraw();
    void tick();         // legacy UpdateTimer
    void playbackStateChanged();
    void releasePlayer(); // legacy SetFile's unload: Stop, then the player goes

    application::AudioBox m_box;
    std::function<application::AudioCacheSettings()> m_settings;
    QStringList m_trackChoices;
    std::function<void(std::optional<int>)> m_trackAnswer;
    bool m_trackForBox = false; // the box asked (its failure withdraws the question)
    bool m_trimPending = false;
    application::AudioView m_view;
    application::AudioDisplayOptions m_options;
    application::AudioBox::State m_seenState = application::AudioBox::State::Closed;
    std::vector<application::AudioLineSpan> m_lines;
    int m_active = -1;
    int m_previousActive = 0; // legacy line_n of a new box
    int m_startMs = 0, m_endMs = 0;
    std::vector<int> m_keyframes;
    std::optional<int> m_videoMs;
    std::optional<float> m_cursor;
    bool m_focused = false;
    bool m_fresh = true; // no audio loaded since the view was made
    int m_scrollbarThickness = 0;
    application::AudioView::Scrollbar m_scrollbar;
    quint64 m_revision = 1;
    // the waveform columns and what they were computed for
    application::WaveformColumns m_columns;
    struct ColumnsKey {
        std::uint64_t serial = 0;
        bool peaks = false;
        std::int64_t start = -1;
        int w = 0, h = 0, samples = 0;
        float scale = 0;
        bool operator==(const ColumnsKey &) const = default;
    } m_columnsKey;
    // A4
    application::AudioPlayerPort *m_player = nullptr;
    std::unique_ptr<application::AudioPlayback> m_playback;
    std::function<int()> m_markPlayTime, m_volume;
    std::function<bool()> m_videoPlaying;
    std::function<void()> m_pauseVideo;
    std::optional<int> m_markMs;
    std::optional<application::PlayRange> m_lastRange;
    QTimer m_playTimer;
    bool m_wasPlaying = false;
    std::uint64_t m_playedSerial = 0; // the audio the player last played
};

} // namespace hikari::ui
