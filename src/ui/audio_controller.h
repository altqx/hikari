#pragma once

// The Audio panel's controller (A1; legacy AudioBox and AudioDisplay). It
// owns the audio box's audio and view, follows the active Line the way
// legacy SetDialogue/Update do, and keeps the marks the application hands it
// (other Lines, keyframes, the paused video's time). The AudioDisplay item
// draws what scene() describes. Display options and colours are legacy's
// defaults; the box's sliders and switches (A2) come from the settings registry.

#include "hikari/application/audio_box.h"
#include "hikari/application/audio_display.h"
#include "hikari/application/audio_spectrum.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <functional>
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
    // A2: a slider or switch moved; `volume` also when the player's volume changed.
    void boxControlsChanged();
    void volumeChanged(float playbackVolume);

private:
    void loadBoxControls(); // legacy AudioBox's constructor
    void applyVerticalZoom(int position, bool fromVolume);
    bool autoScrollSetting() const;
    void ask(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer, bool forBox);
    void boxChanged();
    void newView();
    void reselect();     // legacy SetDialogue
    void update();       // legacy Update: follow the Line, then redraw
    void redraw();

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
};

} // namespace hikari::ui
