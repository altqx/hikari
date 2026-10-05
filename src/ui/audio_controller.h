#pragma once

// The Audio panel's controller (A1; legacy AudioBox and AudioDisplay). It
// owns the audio box's audio and view, follows the active Line the way
// legacy SetDialogue/Update do, and keeps the marks the application hands it
// (other Lines, keyframes, the paused video's time). The AudioDisplay item
// draws what scene() describes. Display options are legacy's defaults and its
// colours the theme layer's (K2); the box's sliders and switches (A2) come
// from the settings registry.

#include "hikari/application/audio_box.h"
#include "hikari/application/audio_display.h"
#include "hikari/application/audio_karaoke.h"
#include "hikari/application/audio_spectrum.h"
#include "hikari/application/audio_timing.h"
#include "hikari/application/audio_playback.h"

#include <QObject>
#include <QPointer>
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

class SettingsStore;

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
    // A2: the box's sliders and switches (legacy AudioBox's HorizontalZoom,
    // VerticalZoom, VolumeBar, VerticalLink, AutoScroll, SpectrumMode and
    // SpectrumNonLinear), as the box was made from the settings and changed since.
    Q_PROPERTY(int horizontalZoom READ horizontalZoom NOTIFY boxControlsChanged)
    Q_PROPERTY(int verticalZoom READ verticalZoom NOTIFY boxControlsChanged)
    Q_PROPERTY(int volume READ volume NOTIFY boxControlsChanged)
    Q_PROPERTY(bool linked READ linked NOTIFY boxControlsChanged)
    Q_PROPERTY(bool autoScroll READ autoScroll NOTIFY boxControlsChanged)
    Q_PROPERTY(bool spectrumOn READ spectrumOn NOTIFY boxControlsChanged)
    Q_PROPERTY(bool spectrumNonLinear READ spectrumNonLinear NOTIFY boxControlsChanged)
    // A4: the player is playing (legacy player->IsPlaying()).
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    // A5: legacy AudioBox's KaraSwitch (hasKara, AUDIO_KARAOKE) and KaraMode
    // (karaAuto, AUDIO_KARAOKE_SPLIT_MODE).
    Q_PROPERTY(bool karaoke READ karaoke NOTIFY boxControlsChanged)
    Q_PROPERTY(bool karaokeSplitMode READ karaokeSplitMode NOTIFY boxControlsChanged)
    Q_PROPERTY(int currentSyllable READ currentSyllable NOTIFY displayChanged)
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
    // Legacy OnMouseEvent's wheel (`rotation` in eighths of a degree, 120 a
    // notch): Ctrl alone the vertical zoom, Shift (or no Shift with
    // AUDIO_WHEEL_DEFAULT_TO_ZOOM) the horizontal zoom around column `x`,
    // otherwise a scroll by -rotation * w / 360 columns.
    void wheel(int rotation, bool controlOnly = false, bool shift = false, float x = 0);
    Q_INVOKABLE void setScrollPosition(int position); // legacy AudioBox::OnScrollbar

    // A2: the settings the box's controls start from and write
    // (AUDIO_HORIZONTAL_ZOOM, AUDIO_VERTICAL_ZOOM, AUDIO_VOLUME, AUDIO_LINK,
    // AUDIO_AUTO_SCROLL, AUDIO_SPECTRUM_ON, AUDIO_SPECTRUM_NON_LINEAR_ON,
    // AUDIO_WHEEL_DEFAULT_TO_ZOOM); without one, legacy's defaults.
    void setSettingsStore(SettingsStore *store);
    int horizontalZoom() const { return m_horizontalZoom; }
    int verticalZoom() const { return m_verticalZoom; }
    int volume() const { return m_volume; }
    bool linked() const { return m_linked; }
    bool autoScroll() const { return m_autoScroll; }
    bool spectrumOn() const { return m_spectrumOn; }
    bool spectrumNonLinear() const { return m_spectrumNonLinear; }
    // Legacy OnHorizontalZoom, OnVerticalZoom, OnVolume and OnVerticalLink.
    Q_INVOKABLE void setHorizontalZoom(int position);
    Q_INVOKABLE void setVerticalZoom(int position);
    Q_INVOKABLE void setVolume(int position);
    Q_INVOKABLE void setLinked(bool linked);
    // Legacy OnAutoGoto, OnSpectrumMode and OnSpectrumNonLinear.
    Q_INVOKABLE void setAutoScroll(bool on);
    Q_INVOKABLE void setSpectrumOn(bool on);
    Q_INVOKABLE void setSpectrumNonLinear(bool on);
    // Legacy OnScrollSpectrum: AUDIO_SCROLL_RIGHT ("Scroll left", A) moves
    // the view 50 columns back, AUDIO_SCROLL_LEFT ("Scroll right", F) forward.
    Q_INVOKABLE void scrollLeft();
    Q_INVOKABLE void scrollRight();
    // What the player gets for the volume slider (legacy PlaybackVolumeFromSlider).
    float playbackVolume() const { return application::playbackVolumeFromSlider(m_volume); }
    // The spectrum picture over the view (legacy DrawSpectrum), kept until the view changes.
    std::shared_ptr<const application::AudioImage> spectrumImage();
    const application::AudioSpectrum *spectrum() const { return m_spectrum.get(); }

    // What to draw (legacy DoUpdateImage, or DrawProgress while loading),
    // and its revision; the waveform columns are kept until the view changes.
    std::vector<application::AudioShape> scene(const application::AudioTextWidth &textWidth);
    quint64 revision() const { return m_revision; }
    const application::WaveformColumns &columns();

    // A4: playback (legacy AudioBox's play handlers, AudioDisplay::Play,
    // Stop and UpdateTimer). The player plays the box's audio (none: no
    // playback); AUDIO_MARK_PLAY_TIME is read at each play. The player's
    // volume is the volume slider's (A2): AUDIO_VOLUME when the box's audio
    // is set (legacy AudioBox::SetFile), then each move of the slider (or of
    // the vertical zoom while linked).
    void setPlayer(application::AudioPlayerPort *player);
    void setPlaybackSettings(std::function<int()> markPlayTimeMs);
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
    // Legacy AudioDisplay::Play(start, end): a playing video is paused
    // first, then nothing without the audio; -1 as the end plays to the end.
    void playRange(int startMs, int endMs);
    // Legacy AudioBox::OnAccelerator: an AUDIO_HOTKEY action by its symbol
    // (an _ALT id arrives as its main id, as legacy's accelerator gave it
    // id + 10). The installed bindings decide which key sends which action
    // (HotkeysController::actionFor). False for a symbol that is not one.
    Q_INVOKABLE bool runHotkey(const QString &symbol);
    // The last range handed to the player, and legacy's Stop memory.
    std::optional<application::PlayRange> lastPlayRange() const { return m_lastRange; }
    const application::AudioPlayback *playback() const { return m_playback.get(); }

    int scrollPosition() const { return m_scrollbar.position; }
    int scrollPage() const { return m_scrollbar.page; }
    int scrollRange() const { return m_scrollbar.range; }

    // A5: karaoke mode (legacy AudioBox::OnKaraoke and OnSplitMode, and the
    // display's Karaoke). Switching it on splits the active Line into
    // syllables and zooms in by 20 (legacy lastHorizontalZoom); off, the zoom
    // goes back, and AUDIO_HORIZONTAL_ZOOM takes it (A5-karaoke-zoom-option:
    // legacy wrote it into AUDIO_VERTICAL_ZOOM).
    bool karaoke() const { return m_hasKara; }
    bool karaokeSplitMode() const { return m_karaAuto; }
    Q_INVOKABLE void toggleKaraoke();
    Q_INVOKABLE void toggleKaraokeSplitMode();
    const application::AudioKaraoke &karaokeModel() const { return m_karaoke; }
    int currentSyllable() const { return m_karaoke.current; }
    // The active Line's text as legacy's SetDialogue read it (the edit box's
    // Line: its translation when it has one, else its text), set before
    // setLines; the label font's measure (GetTextExtentPixel) and Windows'
    // character classes for the automatic split.
    void setActiveText(std::u16string text) { m_activeText = std::move(text); }
    void setKaraokeMeasure(application::KaraokeMeasure measure) { m_measure = std::move(measure); }
    void setKaraokeClasses(application::KaraokeCharClass classes) { m_classes = std::move(classes); }
    // Legacy GetTimesSelection: the current syllable's times in karaoke mode
    // (with `rangeEnd`, to the next syllable's end), else the selection.
    std::pair<int, int> timesSelection(bool rangeEnd = false, bool ignoreKara = false);

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
        // (Playback is the box's own since A4: Play after Next/Previous
        // unless AUDIO_DONT_PLAY_WHEN_LINE_CHANGES, the middle double click's
        // Play and the player's end while a boundary is dragged.)
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
    // A2: a slider or switch moved; `volume` also when the player's volume changed.
    void boxControlsChanged();
    void volumeChanged(float playbackVolume);
    void playingChanged();

private:
    void loadBoxControls(); // legacy AudioBox's constructor
    void loadThemeColours(); // K2: the theme's display colours (legacy ChangeColors)
    void applyVerticalZoom(int position, bool fromVolume);
    bool autoScrollSetting() const;
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
                       application::AudioAdjacent adjacent = application::AudioAdjacent::None,
                       std::optional<std::u16string> karaokeText = std::nullopt);
    void makeDialogueVisible(bool force = false, bool moveToEnd = false); // legacy MakeDialogueVisible
    void splitKaraoke();                                                   // legacy Karaoke::Split
    void changeLine(int delta);
    void addLead(bool in, bool out);
    void tick();         // legacy UpdateTimer
    void focusAndPlay(application::PlayMode mode);
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
    // A2: the box's controls, and the spectrum with its last picture
    QPointer<SettingsStore> m_store;
    int m_horizontalZoom = 50, m_verticalZoom = 50, m_volume = 50;
    bool m_linked = false, m_autoScroll = true, m_spectrumOn = false, m_spectrumNonLinear = false;
    std::unique_ptr<application::AudioSpectrum> m_spectrum;
    std::uint64_t m_spectrumSerial = 0;
    std::shared_ptr<const application::AudioImage> m_spectrumImage;
    struct SpectrumKey {
        std::uint64_t serial = 0;
        std::int64_t start = -1;
        int w = 0, h = 0, samples = 0, percent = 0;
        float scale = 0;
        bool nonLinear = false;
        bool operator==(const SpectrumKey &) const = default;
    } m_spectrumKey;
    // A3
    application::AudioTiming m_timing;
    bool m_needCommit = false; // legacy NeedCommit
    std::vector<int> m_keyframeSnap;
    std::function<application::AudioTimingOptions()> m_timingSettings;
    TimingHooks m_hooks;
    int m_markTextHeight = 0;
    // A4
    application::AudioPlayerPort *m_player = nullptr;
    std::unique_ptr<application::AudioPlayback> m_playback;
    std::function<int()> m_markPlayTime;
    std::function<bool()> m_videoPlaying;
    std::function<void()> m_pauseVideo;
    std::optional<application::PlayRange> m_lastRange;
    QTimer m_playTimer;
    bool m_wasPlaying = false;
    std::uint64_t m_playedSerial = 0; // the audio the player last played
    // A5
    application::AudioKaraoke m_karaoke;
    bool m_hasKara = false, m_karaAuto = true;
    int m_lastHorizontalZoom = -1; // legacy AudioBox::lastHorizontalZoom
    std::u16string m_activeText;
    application::KaraokeMeasure m_measure;
    application::KaraokeCharClass m_classes = application::KaraokeCharClass::ascii();
};

} // namespace hikari::ui
