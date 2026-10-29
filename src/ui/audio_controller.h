#pragma once

#include "hikari/application/audio_box.h"
#include "hikari/application/audio_display.h"
#include "hikari/application/audio_karaoke.h"
#include "hikari/application/audio_spectrum.h"
#include "hikari/application/audio_timing.h"
#include "hikari/application/audio_playback.h"
#include "hikari/application/karaoke_tap.h"
#include "hikari/application/vocal_onsets.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace hikari::ui {

class SettingsStore;

void deleteOldAudioCache(const std::filesystem::path &folder, const std::filesystem::path &inUse, int limit);

class AudioController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY textsChanged)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    // MUST KILL: the scroll values are in units of 12 columns
    Q_PROPERTY(int scrollPosition READ scrollPosition NOTIFY displayChanged)
    Q_PROPERTY(int scrollPage READ scrollPage NOTIFY displayChanged)
    Q_PROPERTY(int scrollRange READ scrollRange NOTIFY displayChanged)
    Q_PROPERTY(QStringList trackChoices READ trackChoices NOTIFY trackChoicesChanged)
    Q_PROPERTY(int horizontalZoom READ horizontalZoom NOTIFY boxControlsChanged)
    Q_PROPERTY(int verticalZoom READ verticalZoom NOTIFY boxControlsChanged)
    Q_PROPERTY(int volume READ volume NOTIFY boxControlsChanged)
    Q_PROPERTY(bool linked READ linked NOTIFY boxControlsChanged)
    Q_PROPERTY(bool autoScroll READ autoScroll NOTIFY boxControlsChanged)
    Q_PROPERTY(bool spectrumOn READ spectrumOn NOTIFY boxControlsChanged)
    Q_PROPERTY(bool spectrumNonLinear READ spectrumNonLinear NOTIFY boxControlsChanged)
    Q_PROPERTY(double magnification READ magnification NOTIFY boxControlsChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool karaoke READ karaoke NOTIFY boxControlsChanged)
    Q_PROPERTY(bool karaokeSplitMode READ karaokeSplitMode NOTIFY boxControlsChanged)
    Q_PROPERTY(int currentSyllable READ currentSyllable NOTIFY displayChanged)
    Q_PROPERTY(bool syncVideo READ syncVideo NOTIFY boxControlsChanged)
    Q_PROPERTY(int speedPercent READ speedPercent NOTIFY boxControlsChanged)
    Q_PROPERTY(bool tapOn READ tapOn NOTIFY tapChanged)
    Q_PROPERTY(int tapState READ tapState NOTIFY tapChanged)
    Q_PROPERTY(bool tapHold READ tapHold NOTIFY tapChanged)
    Q_PROPERTY(int tapLatency READ tapLatency NOTIFY tapChanged)
    Q_PROPERTY(QVariantList tapSyllables READ tapSyllables NOTIFY tapChanged)
    Q_PROPERTY(int tapLine READ tapLine NOTIFY tapChanged)
    Q_PROPERTY(QString tapNextText READ tapNextText NOTIFY tapChanged)
    Q_PROPERTY(QString tapMessage READ tapMessage NOTIFY tapChanged)
    Q_PROPERTY(bool tapNeedsSplit READ tapNeedsSplit NOTIFY tapChanged)
    Q_PROPERTY(bool onsetSnap READ onsetSnap NOTIFY boxControlsChanged)
    Q_PROPERTY(bool findingOnsets READ findingOnsets NOTIFY findingOnsetsChanged)
    Q_PROPERTY(QVariantList tapSpans READ tapSpans NOTIFY tapSpansChanged)
    Q_PROPERTY(bool calibrating READ calibrating NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationBeat READ calibrationBeat NOTIFY calibrationChanged)
    Q_PROPERTY(QVariantList calibrationDelays READ calibrationDelays NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationLatency READ calibrationLatency NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationSpread READ calibrationSpread NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationCounted READ calibrationCounted NOTIFY calibrationChanged)
    Q_PROPERTY(bool calibrationEnough READ calibrationEnough NOTIFY calibrationChanged)
public:
    enum TapState { TapOff, TapReady, TapTapping, TapStopped };
    Q_ENUM(TapState)
    enum TapSyllable { TapSyllableTapped, TapSyllableHeld, TapSyllableNext, TapSyllableWaiting };
    Q_ENUM(TapSyllable)
    static constexpr int kSpeeds[] = {25, 33, 50, 75, 100};
    explicit AudioController(application::DisplayAudioPort &own, QObject *parent = nullptr);
    ~AudioController() override;

    application::AudioBox &box() { return m_box; }
    const application::AudioView &view() const { return m_view; }
    const application::AudioDisplayOptions &options() const { return m_options; }

    bool hasAudio() const { return m_box.isOpen(); }
    bool loaded() const { return m_box.state() == application::AudioBox::State::Loading || ready(); }
    bool ready() const { return m_box.state() == application::AudioBox::State::Ready; }
    QString status() const;
    QString path() const { return QString::fromStdString(m_box.path()); }
    void retranslate() { emit textsChanged(); }

    Q_INVOKABLE void openAudio(const QString &path);
    void setBeforeOpen(std::function<void()> before) { m_beforeOpen = std::move(before); }
    Q_INVOKABLE void openAudioUrl(const QUrl &url) { openAudio(url.toLocalFile()); }
    void openDummy() { openAudio(QString::fromLatin1(application::AudioBox::kDummyName)); }
    Q_INVOKABLE void closeAudio();
    void openFromVideo(const QString &path, int track, bool videoNewIndex, const QString &handoffIndexFile = {});
    void askTrack(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer);
    void trimCache();
    void setSettings(std::function<application::AudioCacheSettings()> settings);

    QStringList trackChoices() const { return m_trackChoices; }
    Q_INVOKABLE void chooseTrack(int row);
    Q_INVOKABLE void cancelTrackChoice();

    void setLines(std::vector<application::AudioLineSpan> lines, int active, bool reselect);
    void setKeyframes(std::vector<int> keyframesMs);
    void setVideoTime(std::optional<int> pausedMs);
    bool syncVideo() const { return m_syncVideo; }
    Q_INVOKABLE void setSyncVideo(bool on);
    void setPlayingVideoTime(std::optional<int> ms);
    void setFollowAudio(std::function<void(int ms)> showVideoAt) { m_showVideoAt = std::move(showVideoAt); }
    bool cursorIsPlayback() const { return playing() || m_followingVideo; }
    application::AudioMarks marks() const;

    void resize(int width, int height, int timelineHeight, int scrollbarThickness);
    void setCursor(std::optional<float> x);
    void setPointerOver(bool over);
    std::optional<float> cursor() const { return m_cursor; }
    void setFocused(bool focused);
    // rotation is Qt's angleDelta: eighths of a degree, 120 a notch.
    void wheel(int rotation, bool controlOnly = false, bool shift = false, float x = 0);
    Q_INVOKABLE void setScrollPosition(int position);

    void setSettingsStore(SettingsStore *store);
    int horizontalZoom() const { return m_horizontalZoom; }
    int verticalZoom() const { return m_verticalZoom; }
    int volume() const { return m_volume; }
    bool linked() const { return m_linked; }
    bool autoScroll() const { return m_autoScroll; }
    bool spectrumOn() const { return m_spectrumOn; }
    bool spectrumNonLinear() const { return m_spectrumNonLinear; }
    Q_INVOKABLE void setHorizontalZoom(int position);
    Q_INVOKABLE void setVerticalZoom(int position);
    Q_INVOKABLE void setVolume(int position);
    Q_INVOKABLE void setLinked(bool linked);
    double magnification() const;
    Q_INVOKABLE void resetZoom();
    Q_INVOKABLE void setAutoScroll(bool on);
    Q_INVOKABLE void setSpectrumOn(bool on);
    Q_INVOKABLE void setSpectrumNonLinear(bool on);
    // MUST KILL: AUDIO_SCROLL_RIGHT runs scrollLeft and AUDIO_SCROLL_LEFT runs scrollRight
    Q_INVOKABLE void scrollLeft();
    Q_INVOKABLE void scrollRight();
    float playbackVolume() const { return application::playbackVolumeFromSlider(m_volume); }
    std::shared_ptr<const application::AudioImage> spectrumImage();
    const application::AudioSpectrum *spectrum() const { return m_spectrum.get(); }

    std::vector<application::AudioShape> scene(const application::AudioTextWidth &textWidth);
    quint64 revision() const { return m_revision; }
    const application::WaveformColumns &columns();

    void setPlayer(application::AudioPlayerPort *player);
    void setPlaybackSettings(std::function<int()> markPlayTimeMs);
    void setVideoPlayback(std::function<bool()> playing, std::function<void()> pause);
    bool playing() const { return m_player && m_player->playing(); }
    Q_INVOKABLE void playSelection() { play(application::PlayMode::Selection); }
    void followLine(bool moveToEnd) { update(moveToEnd); }
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
    // MUST KILL: playRange's endMs -1 means play to the end
    void playRange(int startMs, int endMs);
    // MUST KILL: an _ALT hotkey arrives as its main symbol (legacy accelerator ids are +10)
    Q_INVOKABLE bool runHotkey(const QString &symbol);
    std::optional<application::PlayRange> lastPlayRange() const { return m_lastRange; }
    const application::AudioPlayback *playback() const { return m_playback.get(); }

    int scrollPosition() const { return m_scrollbar.position; }
    int scrollPage() const { return m_scrollbar.page; }
    int scrollRange() const { return m_scrollbar.range; }

    bool karaoke() const { return m_hasKara; }
    bool karaokeSplitMode() const { return m_karaAuto; }
    Q_INVOKABLE void toggleKaraoke();
    Q_INVOKABLE void setKaraoke(bool on);
    Q_INVOKABLE void toggleKaraokeSplitMode();
    Q_INVOKABLE void splitSyllableAtCursor();
    Q_INVOKABLE void switchLongSyllablesToKf();
    const application::AudioKaraoke &karaokeModel() const { return m_karaoke; }
    int currentSyllable() const { return m_karaoke.current; }
    void setActiveText(std::u16string text) { m_activeText = std::move(text); }
    void setKaraokeMeasure(application::KaraokeMeasure measure) { m_measure = std::move(measure); }
    void setKaraokeClasses(application::KaraokeCharClass classes) { m_classes = std::move(classes); }
    std::pair<int, int> timesSelection(bool rangeEnd = false, bool ignoreKara = false);

    Q_PROPERTY(bool hasMark READ hasMark NOTIFY markChanged)
    Q_PROPERTY(int markMs READ markMs NOTIFY markChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY displayChanged)
    Q_PROPERTY(int selectionStart READ selectionStart NOTIFY displayChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd NOTIFY displayChanged)
    // MUST KILL: lineOutOfView is a magic int (-1 before the view, 1 after, 0 in view or no Line)
    Q_PROPERTY(int lineOutOfView READ lineOutOfView NOTIFY displayChanged)
public:
    struct TimingHooks {
        std::function<bool(const application::AudioCommitRequest &)> commit;
        std::function<void(int key)> setActive;
        std::function<void(int ms)> seekVideo;
        std::function<int(int ms)> fieldTime;
        std::function<bool()> severalSelected;
    };
    void setTimingHooks(TimingHooks hooks) { m_hooks = std::move(hooks); }
    int speedPercent() const { return m_speedPercent; }
    Q_INVOKABLE void setSpeedPercent(int percent);
    Q_INVOKABLE void stepSpeed(int delta);

    bool tapOn() const { return m_tapState != TapOff; }
    int tapState() const { return m_tapState; }
    bool tapHold() const { return m_tap.mode() == application::KaraokeTap::Mode::Hold; }
    int tapLatency() const { return m_tapLatency; }
    QVariantList tapSyllables() const;
    int tapLine() const { return m_active + 1; }
    QString tapNextText() const;
    QString tapMessage() const { return m_tapMessage; }
    bool tapNeedsSplit() const { return m_tapState != TapOff && m_tap.needsSplit(); }
    QVariantList tapSpans() const;
    Q_INVOKABLE void toggleTap();
    Q_INVOKABLE void setTapHold(bool hold);
    Q_INVOKABLE void setTapLatency(int ms);
    Q_INVOKABLE bool tapKey(int key, bool pressed, bool autoRepeat, bool shift);
    // After a focus loss the held key's release never arrives, so a held syllable ends now.
    Q_INVOKABLE void tapFocusLost();
    Q_INVOKABLE void tapStop();
    Q_INVOKABLE void tapResume();
    Q_INVOKABLE void tapRevert();
    Q_INVOKABLE void tapUndo(bool line);
    Q_INVOKABLE void tapAutoSplit();
    void setLineText(std::function<QString(int key)> text) { m_lineText = std::move(text); }
    int tapSongNow() const;

    bool onsetSnap() const { return m_onsetSnap; }
    Q_INVOKABLE void setOnsetSnap(bool on);
    std::span<const int> snapOnsets() const;
    bool onsetsFound() const { return m_onsets && m_onsetsSerial == m_box.serial(); }
    bool findingOnsets() const { return m_onsetCancel != nullptr; }
    int onsetDetections() const { return m_onsetDetections; }

    bool calibrating() const { return m_calibrating; }
    int calibrationBeat() const { return m_calibrationBeat; }
    QVariantList calibrationDelays() const;
    int calibrationLatency() const { return m_calibration.latencyMs; }
    int calibrationSpread() const { return m_calibration.spreadMs; }
    int calibrationCounted() const { return m_calibration.counted; }
    bool calibrationEnough() const { return m_calibration.enough; }
    Q_INVOKABLE void startCalibration();
    Q_INVOKABLE void calibrationPress();
    Q_INVOKABLE void stopCalibration();

    void setTimingSettings(std::function<application::AudioTimingOptions()> settings)
    {
        m_timingSettings = std::move(settings);
    }
    void setKeyframeSnapTimes(std::vector<int> snapMs) { m_keyframeSnap = std::move(snapMs); }
    const std::vector<int> &keyframeSnapTimes() const { return m_keyframeSnap; }
    bool hasMark() const { return m_timing.hasMark(); }
    int markMs() const { return m_timing.markMs(); }
    bool modified() const { return m_needCommit; }
    int selectionStart() const { return m_startMs; }
    int selectionEnd() const { return m_endMs; }
    int lineOutOfView() const;
    const application::AudioTiming &timing() const { return m_timing; }
    bool tapBusy() const { return m_tapState == TapTapping; }
    Q_INVOKABLE void commit();
    Q_INVOKABLE void nextLine();
    Q_INVOKABLE void previousLine();
    Q_INVOKABLE void goToSelection();
    Q_INVOKABLE void leadIn() { addLead(true, false); }
    Q_INVOKABLE void leadOut() { addLead(false, true); }
    void showTime(int ms, bool mark);
    application::AudioMouseResult mouse(const application::AudioMouse &event);
    void lostCapture();
    void setMarkTextHeight(int height) { m_markTextHeight = height; }

signals:
    void markChanged();
    void focusRequested();
    void changed();
    void textsChanged();
    void displayChanged();
    void cursorChanged();
    void opened(const QString &path, bool created);
    void logged(const QString &message, bool debug, bool error);
    void trackChoicesChanged();
    void boxControlsChanged();
    void volumeChanged(float playbackVolume);
    void playingChanged();
    void tapChanged();
    void tapSpansChanged();
    void onsetsChanged();
    void findingOnsetsChanged();
    void calibrationChanged();

private:
    void switchKaraoke();
    void loadBoxControls();
    void loadThemeColours();
    void loadDisplayOptions();
    void applyVerticalZoom(int position, bool fromVolume);
    bool autoScrollSetting() const;
    void ask(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer, bool forBox);
    void boxChanged();
    void newView();
    void reselect();
    void update(bool moveToEnd = false, bool scroll = true);
    void redraw();
    application::AudioTimingOptions timingOptions() const;
    application::AudioSnapContext snapContext() const;
    bool commitChanges(bool nextLine, bool save, bool moveToEnd,
                       application::AudioAdjacent adjacent = application::AudioAdjacent::None,
                       std::optional<std::u16string> karaokeText = std::nullopt, const char *stepName = nullptr,
                       bool activeOnly = false, bool scroll = true);
    void commitKaraoke();
    int committedStart() const;
    std::u16string committedKaraokeText();
    void syncCommittedStart();
    void makeDialogueVisible(bool force = false, bool moveToEnd = false);
    void splitKaraoke();
    void changeLine(int delta);
    void addLead(bool in, bool out);
    void tick();
    void focusAndPlay(application::PlayMode mode);
    void playbackStateChanged();
    void applySpeed();
    void beginTapLine();
    void playForTap(int fromMs);
    void finishTapLine(std::optional<int> carryKey, int carryMs);
    void setTapState(TapState state);
    void stopBoxPlayback();
    void leaveTapMode();
    void tapPlayingEnded(bool atEnd);
    double positionMs() const;
    void calibrationTick();
    void stopFollowingVideo();
    void releasePlayer();
    void findOnsets();
    void stopFindingOnsets();
    std::string onsetKey() const;

    application::AudioBox m_box;
    std::function<void()> m_beforeOpen;
    std::function<application::AudioCacheSettings()> m_settings;
    QStringList m_trackChoices;
    std::function<void(std::optional<int>)> m_trackAnswer;
    bool m_trackForBox = false;
    bool m_trimPending = false;
    application::AudioView m_view;
    application::AudioDisplayOptions m_options;
    application::AudioBox::State m_seenState = application::AudioBox::State::Closed;
    std::vector<application::AudioLineSpan> m_lines;
    int m_active = -1;
    int m_previousActive = 0;
    int m_startMs = 0, m_endMs = 0;
    std::vector<int> m_keyframes;
    std::optional<int> m_videoMs;
    std::optional<float> m_cursor;
    bool m_pointerOver = false;
    bool m_focused = false;
    bool m_fresh = true;
    int m_scrollbarThickness = 0;
    application::AudioView::Scrollbar m_scrollbar;
    quint64 m_revision = 1;
    application::WaveformColumns m_columns;
    struct ColumnsKey {
        std::uint64_t serial = 0;
        bool peaks = false;
        std::int64_t start = -1;
        int w = 0, h = 0, samples = 0;
        float scale = 0;
        bool operator==(const ColumnsKey &) const = default;
    } m_columnsKey;
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
    application::AudioTiming m_timing;
    bool m_needCommit = false;
    std::vector<int> m_keyframeSnap;
    std::function<application::AudioTimingOptions()> m_timingSettings;
    TimingHooks m_hooks;
    int m_markTextHeight = 0;
    application::AudioPlayerPort *m_player = nullptr;
    std::unique_ptr<application::AudioPlayback> m_playback;
    std::function<int()> m_markPlayTime;
    std::function<bool()> m_videoPlaying;
    std::function<void()> m_pauseVideo;
    std::optional<application::PlayRange> m_lastRange;
    QTimer m_playTimer;
    bool m_wasPlaying = false;
    std::uint64_t m_playedSerial = 0;
    bool m_syncVideo = true;
    bool m_followingVideo = false;
    std::function<void(int)> m_showVideoAt;
    application::AudioKaraoke m_karaoke;
    bool m_hasKara = false, m_karaAuto = true;
    int m_lastHorizontalZoom = -1;
    std::u16string m_activeText;
    application::KaraokeMeasure m_measure;
    application::KaraokeCharClass m_classes = application::KaraokeCharClass::ascii();
    int m_speedPercent = 100;
    application::KaraokeTap m_tap;
    TapState m_tapState = TapOff;
    int m_tapLatency = 0;
    QString m_tapMessage;
    // MUST KILL: m_tapPlaying is a hidden argument to playRange letting tap plays through while tapping
    bool m_tapPlaying = false;
    // MUST KILL: m_tapAdvancing is a hidden argument telling beginTapLine its own commit moved to the next Line
    bool m_tapAdvancing = false;
    std::function<QString(int)> m_lineText;
    bool m_calibrating = false;
    int m_calibrationBeat = 0;
    std::vector<double> m_calibrationPresses;
    application::LatencyCalibration m_calibration;
    QTimer m_calibrationTimer;
    int m_speedBeforeCalibration = 100;
    bool m_onsetSnap = false;
    std::map<std::string, std::shared_ptr<const std::vector<int>>> m_onsetCache;
    std::shared_ptr<const std::vector<int>> m_onsets;
    std::uint64_t m_onsetsSerial = 0;
    std::thread m_onsetWorker;
    std::shared_ptr<std::atomic<bool>> m_onsetCancel;
    int m_onsetDetections = 0;
};

}
