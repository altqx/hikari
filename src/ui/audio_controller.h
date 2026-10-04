#pragma once

// The Audio panel's controller (A1; legacy AudioBox and AudioDisplay). It
// owns the audio box's audio and view, follows the active Line the way
// legacy SetDialogue/Update do, and keeps the marks the application hands it
// (other Lines, keyframes, the paused video's time). The AudioDisplay item
// draws what scene() describes. Options are legacy's defaults until the
// settings registry (O1) exists.

#include "hikari/application/audio_box.h"
#include "hikari/application/audio_display.h"
#include "hikari/application/audio_timing.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <functional>
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
    void setCursor(std::optional<float> x);
    std::optional<float> cursor() const { return m_cursor; }
    void setFocused(bool focused);
    void wheel(int rotation); // legacy: scrolls by -rotation * w / 360 columns
    Q_INVOKABLE void setScrollPosition(int position); // legacy AudioBox::OnScrollbar

    // What to draw (legacy DoUpdateImage, or DrawProgress while loading),
    // and its revision; the waveform columns are kept until the view changes.
    std::vector<application::AudioShape> scene(const application::AudioTextWidth &textWidth);
    quint64 revision() const { return m_revision; }
    const application::WaveformColumns &columns();

    int scrollPosition() const { return m_scrollbar.position; }
    int scrollPage() const { return m_scrollbar.page; }
    int scrollRange() const { return m_scrollbar.range; }

    // A3: timing (legacy AudioDisplay's selection and mouse timing,
    // CommitChanges, AddLead, ChangeLine, SetMark and ChangePosition; the
    // AudioBox buttons and hotkeys). What legacy did through the edit box,
    // the grid and the video, the application does through these hooks.
    Q_PROPERTY(bool hasMark READ hasMark NOTIFY markChanged)
    Q_PROPERTY(int markMs READ markMs NOTIFY markChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY displayChanged) // legacy NeedCommit
    Q_PROPERTY(int selectionStart READ selectionStart NOTIFY displayChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd NOTIFY displayChanged)
public:
    struct TimingHooks {
        // CommitChanges' edit-box part: the times into the editor's fields,
        // with `save` EditBox::Send(AUDIO_CHANGE_TIME, nextLine).
        std::function<void(const application::AudioCommitRequest &)> commit;
        // grid->SetActive: the Line at `key` becomes active, selected alone.
        std::function<void(int key)> setActive;
        // tab->video->Seek (Ctrl+left or middle click).
        std::function<void(int ms)> seekVideo;
        // A4: legacy Play after Next/Previous (unless
        // AUDIO_DONT_PLAY_WHEN_LINE_CHANGES) and on a middle double click,
        // and the player's end while a boundary is dragged. Unset: nothing plays.
        std::function<void(int startMs, int endMs)> play;
        std::function<void(std::int64_t sample)> setPlayEnd;
    };
    void setTimingHooks(TimingHooks hooks) { m_hooks = std::move(hooks); }
    // AUDIO_AUTO_COMMIT, the snap options, AUDIO_START_DRAG_SENSITIVITY, the
    // leads and AUDIO_DONT_PLAY_WHEN_LINE_CHANGES, read when used.
    void setTimingSettings(std::function<application::AudioTimingOptions()> settings)
    {
        m_timingSettings = std::move(settings);
    }
    // Each keyframe's snap time (StartTimeFor(FrameAt(keyframe))), in step with setKeyframes.
    void setKeyframeSnapTimes(std::vector<int> snapMs) { m_keyframeSnap = std::move(snapMs); }
    bool hasMark() const { return m_timing.hasMark(); }
    int markMs() const { return m_timing.markMs(); }
    bool modified() const { return m_needCommit; }
    int selectionStart() const { return m_startMs; }
    int selectionEnd() const { return m_endMs; }
    const application::AudioTiming &timing() const { return m_timing; }
    // AUDIO_COMMIT and AUDIO_COMMIT_ALT (AudioBox::OnCommit: CommitChanges(true)).
    Q_INVOKABLE void commit();
    // AUDIO_NEXT(_ALT), AUDIO_PREVIOUS(_ALT) (AudioBox::OnNext/OnPrev).
    Q_INVOKABLE void nextLine();
    Q_INVOKABLE void previousLine();
    // AUDIO_GOTO (MakeDialogueVisible(true)).
    Q_INVOKABLE void goToSelection();
    // AUDIO_LEAD_IN, AUDIO_LEAD_OUT (AddLead).
    Q_INVOKABLE void leadIn() { addLead(true, false); }
    Q_INVOKABLE void leadOut() { addLead(false, true); }
    // GLOBAL_SET_AUDIO_FROM_VIDEO and GLOBAL_SET_AUDIO_MARK_FROM_VIDEO: the
    // view centred on the video's time (ChangePosition), the mark there too.
    void showTime(int ms, bool mark);
    // The display's mouse (OnMouseEvent's timing) and lost capture.
    application::AudioMouseResult mouse(const application::AudioMouse &event);
    void lostCapture() { m_timing.lostCapture(); }
    // The label font's text height, which places the mark's time.
    void setMarkTextHeight(int height) { m_markTextHeight = height; }

signals:
    // A3: the mark was set or moved; the display wants the focus (SetFocus).
    void markChanged();
    void focusRequested();
    void changed();
    void displayChanged();
    void cursorChanged();
    // A file was opened and is loading (legacy SetRecent(2) after LoadAudio);
    // `created` when there was no box before (legacy then focuses the display).
    void opened(const QString &path, bool created);
    // A message for the log: legacy HikariLog, or HikariLogDebug (`debug`).
    void logged(const QString &message, bool debug);
    void trackChoicesChanged();

private:
    void ask(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer, bool forBox);
    void boxChanged();
    void newView();
    void reselect();     // legacy SetDialogue
    void update(bool moveToEnd = false); // legacy Update: follow the Line, then redraw
    void redraw();
    // A3
    application::AudioTimingOptions timingOptions() const;
    application::AudioSnapContext snapContext() const;
    void commitChanges(bool nextLine, bool save, bool moveToEnd,
                       application::AudioAdjacent adjacent = application::AudioAdjacent::None);
    void changeLine(int delta);
    void addLead(bool in, bool out);

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
    // A3
    application::AudioTiming m_timing;
    bool m_needCommit = false; // legacy NeedCommit
    std::vector<int> m_keyframeSnap;
    std::function<application::AudioTimingOptions()> m_timingSettings;
    TimingHooks m_hooks;
    int m_markTextHeight = 0;
};

} // namespace hikari::ui
