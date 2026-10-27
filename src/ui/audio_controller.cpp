#include "audio_controller.h"

#include "settings_store.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>

#include <utility>

namespace hikari::ui {

using application::AudioBox;

// Legacy ProviderFFMS2::DeleteOldAudioCache: with more files than the limit
// in the cache folder, the ones read longest ago go (legacy sorted on the
// last access time) until the surplus is gone. The cache file in use, under
// its final name or as it is being written (.part), is never removed: legacy
// on Windows could not remove an open file and went on to the next oldest
// (R5); legacy on Linux unlinked it and lost the cache (R3, data loss).
void deleteOldAudioCache(const std::filesystem::path &folder, const std::filesystem::path &inUse, int limit)
{
    if (limit < 1 || folder.empty())
        return;
    const QDir dir(QString::fromStdU16String(folder.u16string()));
    QString used, usedPart;
    if (!inUse.empty()) {
        used = QFileInfo(QString::fromStdU16String(inUse.u16string())).absoluteFilePath();
        usedPart = used + QStringLiteral(".part");
    }
    const auto entries = dir.entryInfoList(QDir::Files);
    if (entries.size() <= limit)
        return;
    std::multimap<QDateTime, QString> byAccess;
    for (const QFileInfo &entry : entries)
        if (used.isEmpty() || (entry.absoluteFilePath() != used && entry.absoluteFilePath() != usedPart))
            byAccess.emplace(entry.fileTime(QFileDevice::FileAccessTime), entry.absoluteFilePath());
    qsizetype removed = 0;
    const qsizetype surplus = entries.size() - limit;
    for (const auto &[when, path] : byAccess) {
        if (removed >= surplus)
            break;
        if (QFile::remove(path))
            ++removed;
    }
}

AudioController::AudioController(application::DisplayAudioPort &own, QObject *parent)
    : QObject(parent), m_box(own)
{
    connect(this, &AudioController::changed, this, &AudioController::textsChanged);
    m_box.setObserver([this] { boxChanged(); });
    m_box.setLog([this](const std::string &message, application::AudioBox::LogLevel level) {
        using L = application::AudioBox::LogLevel;
        emit logged(QString::fromStdString(message), level == L::Debug, level == L::Shown);
    });
    m_box.setSettings([this] { return m_settings ? m_settings() : application::AudioCacheSettings{}; });
    m_box.setChooser([this](const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer) {
        ask(rows, std::move(answer), true);
    });
    m_box.setDefer([this](std::function<void()> step) { QTimer::singleShot(0, this, std::move(step)); });
    // A4: legacy's playback timer (16 ms on Windows, 17 on Linux)
    m_playTimer.setTimerType(Qt::PreciseTimer);
    m_playTimer.setInterval(16);
    connect(&m_playTimer, &QTimer::timeout, this, &AudioController::tick);
    // KA2: the latency calibration follows its beeps
    m_calibrationTimer.setTimerType(Qt::PreciseTimer);
    m_calibrationTimer.setInterval(16);
    connect(&m_calibrationTimer, &QTimer::timeout, this, &AudioController::calibrationTick);
    // A2 with A4: the volume slider (and the vertical zoom while linked)
    // sets the player's volume as it moves (legacy OnVolume, OnVerticalZoom,
    // OnVerticalLink: player->SetVolume).
    connect(this, &AudioController::volumeChanged, this, [this](float volume) {
        if (m_player)
            m_player->setVolume(volume);
    });
    // K2: the theme's colours, live.
    loadThemeColours();
    theme::onChanged(this, [this] {
        loadThemeColours();
        redraw();
    });
    newView();
}

void AudioController::ask(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer,
                          bool forBox)
{
    if (auto earlier = std::exchange(m_trackAnswer, nullptr))
        earlier(std::nullopt); // a newer question replaces an unanswered one
    m_trackChoices.clear();
    for (const auto &row : rows)
        m_trackChoices << QString::fromStdString(row);
    m_trackAnswer = std::move(answer);
    m_trackForBox = forBox;
    emit trackChoicesChanged();
}

void AudioController::askTrack(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer)
{
    ask(rows, std::move(answer), false);
}

void AudioController::trimCache()
{
    if (m_box.state() == AudioBox::State::Opening) {
        m_trimPending = true; // the cache file in use is known once the track is open
        return;
    }
    m_trimPending = false;
    const auto settings = m_settings ? m_settings() : application::AudioCacheSettings{};
    deleteOldAudioCache(settings.cacheDir, m_box.cacheFile(), settings.cacheFilesLimit);
}

void AudioController::setSettings(std::function<application::AudioCacheSettings()> settings)
{
    m_settings = std::move(settings);
}

void AudioController::openFromVideo(const QString &path, int track, bool videoNewIndex, const QString &handoffIndexFile)
{
    m_box.openFromVideo(path.toStdString(), track, videoNewIndex, handoffIndexFile.toStdString());
}

void AudioController::chooseTrack(int row)
{
    auto answer = std::exchange(m_trackAnswer, nullptr);
    m_trackChoices.clear();
    emit trackChoicesChanged();
    if (answer)
        answer(row);
}

void AudioController::cancelTrackChoice()
{
    auto answer = std::exchange(m_trackAnswer, nullptr);
    m_trackChoices.clear();
    emit trackChoicesChanged();
    if (answer)
        answer(std::nullopt);
}

// A new legacy AudioBox: zoom and scale from the options, the first Line as
// the previous one (line_n), the panel's size kept.
void AudioController::newView()
{
    m_view = application::AudioView{};
    loadBoxControls();
    m_spectrum.reset(); // legacy deletes the display's renderer with it
    m_spectrumImage.reset();
    m_spectrumKey = {};
    m_previousActive = 0;
    m_fresh = true;
    m_cursor.reset();
    m_scrollbar = {};
    // A3: a new display has no mark and holds nothing
    const bool hadMark = m_timing.hasMark();
    m_timing.reset();
    m_needCommit = false;
    if (hadMark)
        emit markChanged();
    // A4: a new box plays with legacy's defaults
    if (m_player)
        m_playback = std::make_unique<application::AudioPlayback>(*m_player);
    // A5: a new display has no syllables until its first Line; a new box
    // has no zoom to go back to
    m_karaoke = {};
    m_lastHorizontalZoom = -1;
    leaveTapMode();
}

// KA2: tap mode belongs to the audio it played; closed or replaced, the next
// audio starts with it off.
void AudioController::leaveTapMode()
{
    if (m_tapState == TapOff)
        return;
    m_tap.revert();
    m_tapMessage.clear();
    setTapState(TapOff);
}

QString AudioController::status() const
{
    switch (m_box.state()) {
    case AudioBox::State::Closed:
        return QCoreApplication::translate("HikariSub.Audio", "No audio open");
    case AudioBox::State::Opening: {
        // legacy ProgressSink, titled for video
        const auto [done, total] = m_box.indexing();
        const int percent = total > 0 ? static_cast<int>(done * 100 / total) : 0;
        return QCoreApplication::translate("HikariSub.Audio", "Indexing video %1%").arg(percent);
    }
    case AudioBox::State::Loading:
    case AudioBox::State::Ready:
        return QFileInfo(path()).fileName();
    }
    return {};
}

void AudioController::openAudio(const QString &path)
{
    if (path.isEmpty())
        return; // legacy: no file, nothing happens
    if (m_beforeOpen)
        m_beforeOpen();
    // In the platform's form, as Open video hands its file on: legacy's
    // wxFileDialog and wxFileName paths were native, and a file dialog's URL
    // gives "/" on Windows too.
    m_box.open(QDir::toNativeSeparators(path).toStdString());
}

void AudioController::closeAudio()
{
    m_box.close();
    newView();
    emit changed();
    redraw();
}

AudioController::~AudioController()
{
    blockSignals(true); // no findingOnsetsChanged to a closing window
    stopFindingOnsets();
}

void AudioController::boxChanged()
{
    if (m_player && m_box.serial() != m_playedSerial)
        releasePlayer();
    const auto state = m_box.state();
    const bool loaded = state == AudioBox::State::Loading || state == AudioBox::State::Ready;
    const bool wasLoaded = m_seenState == AudioBox::State::Loading || m_seenState == AudioBox::State::Ready;
    m_seenState = state;
    if (!loaded && wasLoaded)
        leaveTapMode();
    if (loaded && !wasLoaded) {
        // legacy SetFile: loaded, the image, then the current Line (SetDialogue)
        const auto *audio = m_box.audio();
        m_view.setSource(audio->sampleRate(), audio->sampleCount());
        m_view.updateSamples();
        // AudioBox::SetFile: the player starts at AUDIO_VOLUME's volume (the
        // option as stored, which the slider may show clamped)
        if (m_player)
            m_player->setVolume(
                application::playbackVolumeFromSlider(m_store ? m_store->integer("audio.volume") : m_volume));
        emit opened(path(), std::exchange(m_fresh, false));
        reselect();
    } else if (!loaded && m_box.error()) {
        // a failed SetFile destroys the box; the box logged what legacy logs
        newView();
        if (m_trackForBox && (!m_trackChoices.isEmpty() || m_trackAnswer)) {
            m_trackAnswer = nullptr;
            m_trackChoices.clear();
            emit trackChoicesChanged();
        }
    } else if (!loaded) {
        m_view.clearSource();
    }
    if (m_trimPending && state != AudioBox::State::Opening)
        trimCache();
    findOnsets(); // KA5
    emit changed();
    redraw();
}

void AudioController::setLines(std::vector<application::AudioLineSpan> lines, int active, bool select)
{
    if (!select && active == m_active && lines == m_lines)
        return; // nothing the display shows changed
    m_lines = std::move(lines);
    m_active = active;
    if (select && (m_box.state() == AudioBox::State::Loading || m_box.state() == AudioBox::State::Ready))
        reselect();
    else
        redraw();
}

// Legacy SetDialogue.
void AudioController::reselect()
{
    if (m_active < 0 || m_active >= static_cast<int>(m_lines.size()))
        return redraw();
    m_needCommit = false; // A3: SetDialogue
    m_karaoke.current = 0; // A5
    if (m_options.grabTimesOnSelect)
        std::tie(m_startMs, m_endMs) = application::legacyLineSelection(m_lines, m_active, m_previousActive);
    m_previousActive = m_active;
    if (m_hasKara) // A5: SetDialogue splits the new Line
        splitKaraoke();
    if (m_tapState != TapOff) // KA2: the new Line is the one to tap
        beginTapLine();
    update();
}

// Legacy Update: with auto-scroll the Line is made visible.
void AudioController::update(bool moveToEnd, bool scroll)
{
    if (scroll && autoScrollSetting())
        makeDialogueVisible(false, moveToEnd);
    redraw();
}

// Legacy MakeDialogueVisible: the selection, or in karaoke mode the current
// syllable as far as the next one's end (GetTimesSelection(rangeEnd)).
void AudioController::makeDialogueVisible(bool force, bool moveToEnd)
{
    const auto [start, end] = timesSelection(true);
    if (m_hasKara)
        m_view.makeVisibleKaraoke(start, end, m_scrollbarThickness);
    else
        m_view.makeVisible(start, end, force, moveToEnd, m_scrollbarThickness);
}

std::pair<int, int> AudioController::timesSelection(bool rangeEnd, bool ignoreKara)
{
    if (m_hasKara && m_karaoke.count() && !ignoreKara) {
        m_karaoke.current = std::clamp(m_karaoke.current, 0, m_karaoke.count() - 1);
        return rangeEnd ? m_karaoke.visibleTimes(m_karaoke.current, m_startMs, m_endMs)
                        : m_karaoke.syllableTimes(m_karaoke.current, m_startMs);
    }
    return {m_startMs, m_endMs};
}

// KA1: the English hyphenation Auto split uses: LibreOffice's
// hyph_en_US.dic (BSD-style, src/ui/hyphenation/README_hyph_en_US.txt).
static const application::Hyphenation &karaokeHyphenation()
{
    static const application::Hyphenation patterns = [] {
        QFile file(QStringLiteral(":/hikari/ui/hyphenation/hyph_en_US.dic"));
        if (!file.open(QIODevice::ReadOnly))
            return application::Hyphenation();
        const QByteArray bytes = file.readAll();
        return application::Hyphenation::fromPatterns(std::string_view(bytes.constData(), std::size_t(bytes.size())));
    }();
    return patterns;
}

// Legacy Karaoke::Split over the active Line (the edit box's): its text or
// translation and its own times, with AUDIO_MERGE_EVERY_N_WITH_SYLLABLE read now.
void AudioController::splitKaraoke()
{
    if (m_active < 0 || m_active >= static_cast<int>(m_lines.size())) {
        m_karaoke.clear(); // (legacy has no Line before the first SetDialogue)
        return;
    }
    const auto &line = m_lines[static_cast<std::size_t>(m_active)];
    application::KaraokeSplitRules rules;
    rules.everyN = m_store && m_store->boolean("audio.mergeEveryNWithSyllable");
    rules.doubleConsonantApart = m_store && m_store->boolean("audio.karaokeDoubleConsonantApart"); // KA1
    rules.hyphenation = &karaokeHyphenation();
    m_karaoke.split({m_activeText, line.startMs, line.endMs}, m_karaAuto, rules, m_classes);
}

// KA1: as legacy Commit(false) after a split on the letters:
// CommitChanges(false, AUDIO_AUTO_COMMIT) with the syllables' text, the
// view left where it is; the active Line's alone, so other selected Lines
// keep their texts (Codex)
void AudioController::commitKaraoke()
{
    commitChanges(false, timingOptions().autoCommit, false, application::AudioAdjacent::None,
                  committedKaraokeText(), nullptr, true, false);
}

// KA1: the start as the commit writes it; the \k text is written against it
// so the syllables keep their times (a start of 999 ms is committed as 990).
int AudioController::committedStart() const
{
    return m_hooks.fieldTime ? m_hooks.fieldTime(m_startMs) : m_startMs;
}

// KA1: in Karaoke mode the panel takes the start the commit wrote, so the
// syllables are drawn, played and hit from the start their text is read
// from, as a new reading of the Line would give (Codex)
void AudioController::syncCommittedStart()
{
    if (m_hasKara && loaded())
        m_startMs = committedStart();
}

// The syllables' text for that start: an earlier one lengthens the first
// syllable; a later one (MicroDVD rounds up to a frame) first cuts the
// syllables it passes, as moving the start does, so none is negative.
std::u16string AudioController::committedKaraokeText()
{
    const int start = committedStart();
    if (start > m_startMs)
        m_karaoke.moveStart(m_startMs, start);
    return m_karaoke.text(start);
}

void AudioController::splitSyllableAtCursor()
{
    if (!m_hasKara || !loaded() || !m_karaoke.count() || tapBusy()) // the taps hold the syllables (Codex)
        return;
    auto &k = m_karaoke;
    // the syllables as the committed text reads them (Codex)
    const int lineStart = committedStart();
    bool split = false;
    // the syllable under the waveform cursor
    int under = -1, ms = 0;
    if (m_cursor && m_view.hasSource()) {
        ms = m_view.msAtX(static_cast<std::int64_t>(*m_cursor));
        for (int i = 0; i < k.count() && under < 0; i++) {
            const auto [start, end] = k.syllableTimes(i, lineStart);
            if (ms > start && ms < end)
                under = i;
        }
    }
    if (m_pointerOver && k.hover >= 0 && k.character >= 0 && k.hover < k.count()) {
        // the hovered letter, as a click on it (a letter the pointer has
        // left is no longer hovered, though its mark stays drawn)
        split = k.splitSyllable(k.hover, k.character, lineStart);
        if (split)
            k.current = k.hover;
        // the letters moved under the pointer: none is hovered until it
        // moves again (Codex)
        k.hover = -1;
        k.character = -1;
    } else if (under >= 0) {
        // that syllable, at the cursor's time
        if ((split = k.splitAt(under, ms, lineStart)))
            k.current = under + 1;
    } else {
        // no cursor, or none over a syllable
        const int i = std::clamp(k.current, 0, k.count() - 1);
        const auto [start, end] = k.syllableTimes(i, lineStart);
        if ((split = k.splitAt(i, start + (end - start) / 2, lineStart)))
            k.current = i + 1;
    }
    if (split)
        commitKaraoke();
}

void AudioController::switchLongSyllablesToKf()
{
    if (!m_hasKara || !loaded() || !m_karaoke.count() || tapBusy())
        return;
    const int threshold = m_store ? static_cast<int>(m_store->integer("audio.karaokeKfThreshold")) : 100;
    if (m_karaoke.switchLongToKf(threshold * 10, committedStart()))
        commitKaraoke();
}

// Legacy AudioBox::OnKaraoke.
void AudioController::toggleKaraoke()
{
    emit focusRequested();
    switchKaraoke();
}

void AudioController::setKaraoke(bool on)
{
    if (on != m_hasKara)
        switchKaraoke();
}

void AudioController::switchKaraoke()
{
    if (m_hasKara && m_tapState != TapOff)
        toggleTap(); // KA2: tap mode is Karaoke mode's
    m_hasKara = !m_hasKara;
    const int value = application::legacyKaraokeZoom(m_hasKara, m_horizontalZoom, m_lastHorizontalZoom);
    if (m_hasKara)
        splitKaraoke();
    // SetSamplesPercent, the slider and its setting (A5-karaoke-zoom-option:
    // legacy wrote AUDIO_VERTICAL_ZOOM)
    m_view.setSamplesPercent(value, true, 0.5f, m_scrollbarThickness);
    m_horizontalZoom = std::clamp(value, 0, 100);
    if (m_store) {
        m_store->set("audio.horizontalZoom", value);
        m_store->set("audio.karaoke", m_hasKara);
    }
    if (m_view.hasSource())
        makeDialogueVisible();
    findOnsets(); // KA5: snapping is Karaoke mode's
    emit boxControlsChanged();
    redraw();
}

// Legacy AudioBox::OnSplitMode: the Line split again the other way (the
// current syllable kept).
void AudioController::toggleKaraokeSplitMode()
{
    emit focusRequested();
    m_karaAuto = !m_karaAuto;
    if (m_hasKara)
        splitKaraoke();
    if (m_store)
        m_store->set("audio.karaokeSplitMode", m_karaAuto);
    emit boxControlsChanged();
    redraw();
}

void AudioController::setKeyframes(std::vector<int> keyframesMs)
{
    if (keyframesMs == m_keyframes)
        return;
    m_keyframes = std::move(keyframesMs);
    redraw();
}

void AudioController::setVideoTime(std::optional<int> pausedMs)
{
    if (pausedMs == m_videoMs)
        return;
    m_videoMs = pausedMs;
    redraw();
}

// AV2: the sync toggle. Off, a cursor following the video goes at once.
void AudioController::setSyncVideo(bool on)
{
    m_syncVideo = on;
    if (m_store)
        m_store->set("audio.syncVideo", on);
    if (!on)
        stopFollowingVideo();
    emit boxControlsChanged();
}

// AV2: the playing video's time. The box's own playback keeps its cursor.
void AudioController::setPlayingVideoTime(std::optional<int> ms)
{
    const auto *audio = m_box.audio();
    if (!ms || !m_syncVideo || !ready() || !audio || playing())
        return stopFollowingVideo();
    const auto followed = application::followCursor(
        m_view, static_cast<std::int64_t>(*ms) * audio->sampleRate() / 1000, m_scrollbarThickness);
    m_followingVideo = true;
    if (followed.moved)
        redraw();
    if (followed.x != m_cursor || followed.moved) {
        m_cursor = followed.x;
        emit cursorChanged();
    }
}

void AudioController::stopFollowingVideo()
{
    if (!std::exchange(m_followingVideo, false))
        return;
    m_cursor.reset();
    emit cursorChanged();
}

application::AudioMarks AudioController::marks() const
{
    application::AudioMarks marks;
    marks.startMs = m_startMs;
    marks.endMs = m_endMs;
    marks.inactive = application::legacyInactiveLines(m_options.inactiveLines, m_lines, m_active);
    marks.keyframesMs = m_keyframes;
    marks.videoMs = m_videoMs;
    marks.focused = m_focused;
    // A3
    marks.modified = m_needCommit;
    if (m_timing.hasMark())
        marks.markMs = m_timing.markMs();
    marks.markTextHeight = m_markTextHeight;
    // A5
    if (m_hasKara) {
        auto karaoke = std::make_shared<application::AudioKaraokeMarks>();
        karaoke->times = m_karaoke.times();
        for (int i = 0; i < m_karaoke.count(); i++)
            karaoke->stripped.push_back(m_karaoke.stripped(i));
        karaoke->current = m_karaoke.current;
        karaoke->hover = m_karaoke.hover;
        karaoke->character = m_karaoke.character;
        karaoke->curStartMs = m_startMs;
        marks.karaoke = std::move(karaoke);
    }
    return marks;
}

void AudioController::resize(int width, int height, int timelineHeight, int scrollbarThickness)
{
    m_scrollbarThickness = scrollbarThickness;
    m_view.resize(width, height, timelineHeight, scrollbarThickness);
    redraw();
}

void AudioController::setPointerOver(bool over)
{
    m_pointerOver = over;
}

void AudioController::setCursor(std::optional<float> x)
{
    if (playing() || m_followingVideo)
        return; // legacy draws the mouse's cursor only while not playing (AV2: nor the video)
    if (x == m_cursor)
        return;
    m_cursor = x;
    emit cursorChanged();
}

void AudioController::setFocused(bool focused)
{
    if (focused == m_focused)
        return;
    m_focused = focused;
    redraw();
}

// Legacy OnMouseEvent's wheel.
void AudioController::wheel(int rotation, bool controlOnly, bool shift, float x)
{
    if (!m_view.hasSource())
        return;
    // Zoom or scroll?
    bool zoom = shift;
    if (m_store && m_store->boolean("audio.wheelDefaultToZoom"))
        zoom = !zoom;
    constexpr int kWheelDelta = 120; // wxMouseEvent::GetWheelDelta
    if (controlOnly) {
        // the vertical zoom slider moves by whole notches, within its range
        const int step = rotation / kWheelDelta;
        const int pos = std::clamp(m_verticalZoom + step, 1, 100);
        applyVerticalZoom(pos, false);
        return;
    }
    if (zoom) {
        // the slider keeps its range, the setting takes the value as it is
        const int step = rotation / kWheelDelta;
        const int value = m_horizontalZoom - step;
        m_horizontalZoom = std::clamp(value, 0, 100);
        if (m_store)
            m_store->set("audio.horizontalZoom", value);
        m_view.setSamplesPercent(value, true, x / float(m_view.width()), m_scrollbarThickness);
        emit boxControlsChanged();
        redraw();
        return;
    }
    const int step = -rotation * m_view.width() / 360;
    m_view.updatePosition(m_view.position() + step, false, m_scrollbarThickness);
    redraw();
}

void AudioController::setSettingsStore(SettingsStore *store)
{
    if (m_store)
        disconnect(m_store, nullptr, this, nullptr);
    m_store = store;
    loadBoxControls();
    // Settings > Audio's display options, as they change (legacy applied
    // them when the Options dialog closed: OptionsDialog's ChangeOptions).
    if (m_store)
        connect(m_store, &SettingsStore::changed, this, [this](const QString &id) {
            static const QStringList kDisplay{
                QStringLiteral("audio.drawTimeCursor"),         QStringLiteral("audio.drawSecondaryLines"),
                QStringLiteral("audio.drawSelectionBackground"), QStringLiteral("audio.drawVideoPosition"),
                QStringLiteral("audio.drawKeyframes"),          QStringLiteral("audio.lineBoundariesThickness"),
                QStringLiteral("audio.inactiveLinesDisplayMode")};
            if (!kDisplay.contains(id))
                return;
            loadDisplayOptions();
            redraw();
        });
    emit boxControlsChanged();
    redraw();
}

// K2: the display's colours are the theme's, fixed per theme (Dark keeps
// legacy's dark theme, config.cpp:451-472); the selection marks take the
// accent. A theme change draws the display, and the spectrum, again (legacy
// ChangeColors: AudioDisplay::ChangeOptions, AudioSpectrum::ChangeColours).
void AudioController::loadThemeColours()
{
    theme::applyAudioColours(m_options, theme::current().content.audio);
    if (m_spectrum)
        m_spectrum->setColours(m_options.spectrumBackground, m_options.spectrumEcho, m_options.spectrumInner);
    m_spectrumImage.reset(); // drawn again with the new palette
}

void AudioController::loadDisplayOptions()
{
    if (!m_store)
        return;
    m_options.lineBoundaryWidth = m_store->integer("audio.lineBoundariesThickness");
    m_options.inactiveLines = m_store->integer("audio.inactiveLinesDisplayMode");
    m_options.drawVideoPosition = m_store->boolean("audio.drawVideoPosition");
    m_options.drawSelectionBackground = m_store->boolean("audio.drawSelectionBackground");
    m_options.drawSecondBoundaries = m_store->boolean("audio.drawSecondaryLines");
    m_options.drawKeyframes = m_store->boolean("audio.drawKeyframes");
    m_options.drawTimeCursor = m_store->boolean("audio.drawTimeCursor"); // AU-time-cursor-option
}

// Legacy AudioBox's constructor: the zoom and scale from the settings, the
// sliders within their ranges (HikariSlider), the switches as last left; a
// linked volume takes the vertical zoom's value.
void AudioController::loadBoxControls()
{
    int zoom = 50, vertical = 50, volume = 50;
    bool link = false;
    if (m_store) {
        zoom = m_store->integer("audio.horizontalZoom");
        vertical = m_store->integer("audio.verticalZoom");
        volume = m_store->integer("audio.volume");
        link = m_store->boolean("audio.link");
        m_autoScroll = m_store->boolean("audio.autoScroll");
        m_spectrumOn = m_store->boolean("audio.spectrumOn");
        m_spectrumNonLinear = m_store->boolean("audio.spectrumNonLinearOn");
        // A5: legacy AudioDisplay's constructor
        m_hasKara = m_store->boolean("audio.karaoke");
        m_karaAuto = m_store->boolean("audio.karaokeSplitMode");
        m_syncVideo = m_store->boolean("audio.syncVideo"); // AV2
        // KA2
        m_speedPercent = std::clamp(m_store->integer("audio.speedPercent"), 25, 100);
        m_tap.setMode(m_store->boolean("audio.tapHold") ? application::KaraokeTap::Mode::Hold
                                                         : application::KaraokeTap::Mode::Press);
        m_tapLatency = std::clamp(m_store->integer("audio.tapLatency"), -200, 500); // as setTapLatency
        m_onsetSnap = m_store->boolean("audio.onsetSnap"); // KA5
    }
    loadDisplayOptions();
    m_view.setSamplesPercent(zoom, false);
    m_horizontalZoom = std::clamp(zoom, 0, 100);
    m_view.setScale(application::audioScaleFromSlider(vertical));
    m_verticalZoom = std::clamp(vertical, 1, 100);
    m_volume = std::clamp(volume, 1, 100);
    m_linked = link;
    if (link) {
        m_volume = m_verticalZoom;
        if (m_store)
            m_store->set("audio.volume", m_volume);
    }
}

bool AudioController::autoScrollSetting() const
{
    // legacy Update reads AUDIO_AUTO_SCROLL each time
    return m_store ? m_store->boolean("audio.autoScroll") : m_autoScroll;
}

// Legacy OnHorizontalZoom: the zoom kept around the view's centre.
void AudioController::setHorizontalZoom(int position)
{
    m_horizontalZoom = position;
    m_view.setSamplesPercent(position, true, 0.5f, m_scrollbarThickness);
    if (m_store)
        m_store->set("audio.horizontalZoom", position);
    emit boxControlsChanged();
    redraw();
}

// Legacy OnVerticalZoom (and the Ctrl wheel): the scale, and with the link
// the volume slider and the player.
void AudioController::applyVerticalZoom(int position, bool fromVolume)
{
    m_verticalZoom = position;
    m_view.setScale(application::audioScaleFromSlider(position));
    if (m_linked && !fromVolume) {
        m_volume = position;
        if (m_store)
            m_store->set("audio.volume", position);
        emit volumeChanged(playbackVolume());
    }
    if (m_store)
        m_store->set("audio.verticalZoom", position);
    emit boxControlsChanged();
    redraw();
}

void AudioController::setVerticalZoom(int position)
{
    applyVerticalZoom(position, false);
}

// Legacy OnVolume: the player, and with the link the vertical zoom.
void AudioController::setVolume(int position)
{
    m_volume = position;
    if (m_store)
        m_store->set("audio.volume", position);
    emit volumeChanged(playbackVolume());
    if (m_linked)
        return applyVerticalZoom(position, true);
    emit boxControlsChanged();
}

// Legacy OnVerticalLink: linking moves the volume to the vertical zoom.
void AudioController::setLinked(bool linked)
{
    m_linked = linked;
    const int pos = std::clamp(m_verticalZoom, 1, 100);
    if (linked) {
        m_volume = pos;
        if (m_store)
            m_store->set("audio.volume", pos);
        emit volumeChanged(playbackVolume());
    }
    if (m_store)
        m_store->set("audio.link", linked);
    emit boxControlsChanged();
}

// The view's samples per column go with the cube of the stretch
// (AudioView::updateSamples), so the readout is (50 / position)^3.
double AudioController::magnification() const
{
    const double position = std::clamp(m_horizontalZoom, 1, 100);
    return std::pow(50.0 / position, 3);
}

void AudioController::resetZoom()
{
    setHorizontalZoom(50);
    applyVerticalZoom(50, false);
}

void AudioController::setAutoScroll(bool on)
{
    m_autoScroll = on;
    if (m_store)
        m_store->set("audio.autoScroll", on);
    emit boxControlsChanged();
}

void AudioController::setSpectrumOn(bool on)
{
    m_spectrumOn = on;
    if (m_store)
        m_store->set("audio.spectrumOn", on);
    emit boxControlsChanged();
    redraw();
}

void AudioController::setSpectrumNonLinear(bool on)
{
    m_spectrumNonLinear = on;
    if (m_store)
        m_store->set("audio.spectrumNonLinearOn", on);
    emit boxControlsChanged();
    redraw();
}

void AudioController::scrollLeft()
{
    if (!m_view.hasSource())
        return;
    m_view.updatePosition(m_view.position() - 50, false, m_scrollbarThickness);
    redraw();
}

void AudioController::scrollRight()
{
    if (!m_view.hasSource())
        return;
    m_view.updatePosition(m_view.position() + 50, false, m_scrollbarThickness);
    redraw();
}

// Legacy DrawSpectrum: RenderRange over the view's columns at the
// horizontal zoom; the renderer (and its cache) lasts as long as the audio.
std::shared_ptr<const application::AudioImage> AudioController::spectrumImage()
{
    const auto *audio = m_box.audio();
    const int w = m_view.width(), h = m_view.height();
    if (!audio || w < 1 || h < 1 || m_view.samples() <= 0)
        return nullptr;
    if (!m_spectrum || m_spectrumSerial != m_box.serial()) {
        m_spectrum = std::make_unique<application::AudioSpectrum>();
        m_spectrum->setColours(m_options.spectrumBackground, m_options.spectrumEcho, m_options.spectrumInner);
        m_spectrumSerial = m_box.serial();
        m_spectrumImage.reset();
    }
    const SpectrumKey key{m_box.serial(), m_view.position() * m_view.samples(), w, h, m_view.samples(),
                          m_view.samplesPercent(), m_view.scale(), m_spectrumNonLinear};
    if (m_spectrumImage && key == m_spectrumKey)
        return m_spectrumImage;
    m_spectrumKey = key;
    m_spectrum->setScaling(key.scale);
    m_spectrum->setNonLinear(key.nonLinear);
    auto image = std::make_shared<application::AudioImage>();
    image->width = w;
    image->height = h;
    image->bgra.assign(std::size_t(w) * h * 4, 0);
    for (std::size_t i = 3; i < image->bgra.size(); i += 4)
        image->bgra[i] = 0xFF; // legacy's X8R8G8B8 surface is opaque
    m_spectrum->render(*audio, key.start, (m_view.position() + w) * m_view.samples(), image->bgra.data(), w, w, h,
                       key.percent);
    m_spectrumImage = std::move(image);
    return m_spectrumImage;
}

void AudioController::setScrollPosition(int position)
{
    if (!m_view.hasSource())
        return;
    m_view.setPosition(position * 12);
    redraw();
}

const application::WaveformColumns &AudioController::columns()
{
    const auto *audio = m_box.audio();
    const ColumnsKey key{m_box.serial(), audio && audio->peaks(), m_view.position() * m_view.samples(), m_view.width(),
                         m_view.height(), m_view.samples(), m_view.scale()};
    if (!(key == m_columnsKey)) {
        m_columnsKey = key;
        m_columns = audio ? application::legacyWaveform(*audio, key.start, key.w, key.h, key.samples, key.scale)
                          : application::WaveformColumns{};
    }
    return m_columns;
}

std::vector<application::AudioShape> AudioController::scene(const application::AudioTextWidth &textWidth)
{
    switch (m_box.state()) {
    case AudioBox::State::Closed:
    case AudioBox::State::Opening:
        return {}; // legacy: not loaded, nothing drawn
    case AudioBox::State::Loading:
        return application::audioProgressScene(m_view, m_box.progress(), m_options);
    case AudioBox::State::Ready:
        break;
    }
    if (m_spectrumOn) // legacy DoUpdateImage: the spectrum in place of the waveform
        return application::audioScene(m_view, {}, marks(), m_options, textWidth, spectrumImage());
    return application::audioScene(m_view, columns(), marks(), m_options, textWidth);
}

// A4: the player for the box's audio.
void AudioController::setPlayer(application::AudioPlayerPort *player)
{
    m_playTimer.stop();
    m_player = player;
    m_playback = player ? std::make_unique<application::AudioPlayback>(*player) : nullptr;
    m_playedSerial = m_box.serial();
    applySpeed();
}

void AudioController::setPlaybackSettings(std::function<int()> markPlayTimeMs)
{
    m_markPlayTime = std::move(markPlayTimeMs);
}

void AudioController::setVideoPlayback(std::function<bool()> playing, std::function<void()> pause)
{
    m_videoPlaying = std::move(playing);
    m_pauseVideo = std::move(pause);
}

// Legacy AudioBox's play handlers: a mark play without a mark returns
// first; the others ask AudioDisplay::Play for their range.
void AudioController::play(application::PlayMode mode)
{
    if (!hasAudio())
        return;
    const std::optional<int> mark = m_timing.hasMark() ? std::optional(m_timing.markMs()) : std::nullopt;
    // A5: GetTimesSelection, the current syllable in karaoke mode but for
    // AUDIO_PLAY_LINE (the mark plays do not read it)
    using application::PlayMode;
    const bool marks = mode == PlayMode::BeforeMark || mode == PlayMode::AfterMark;
    const auto [start, end] = marks ? std::pair(m_startMs, m_endMs) : timesSelection(false, mode == PlayMode::Line);
    const auto request =
        application::legacyPlayRequest(mode, start, end, mark, m_markPlayTime ? m_markPlayTime() : 1000);
    if (!request)
        return;
    playRange(request->startMs, request->endMs);
}

// Legacy AudioDisplay::Play(start, end, pause = true): a playing video is
// paused before anything else, then nothing plays without the audio's
// provider (here: the box's audio and a player).
void AudioController::playRange(int startMs, int endMs)
{
    // KA2-tap-to-time: while tapping only tap mode plays (a Line change's
    // play, a play key)
    if (m_tapState == TapTapping && !m_tapPlaying)
        return;
    if (m_videoPlaying && m_videoPlaying() && m_pauseVideo)
        m_pauseVideo();
    const auto *audio = m_box.audio();
    if (!audio || !m_playback)
        return;
    m_playedSerial = m_box.serial();
    stopFollowingVideo(); // AV2: the box's own cursor from now on
    m_lastRange = m_playback->play(audio->sampleRate(), audio->sampleCount(), startMs, endMs);
    m_playTimer.start();
    playbackStateChanged();
}

// Legacy AudioBox::OnStop: AudioDisplay::Stop(true).
void AudioController::stopPlayback()
{
    if (!hasAudio())
        return;
    if (m_videoPlaying && m_videoPlaying()) {
        if (m_pauseVideo)
            m_pauseVideo();
        return;
    }
    stopBoxPlayback();
}

// The box's own playing stopped, whatever the video does (tap mode's stops:
// a video started meanwhile must not keep the tapping's audio going).
void AudioController::stopBoxPlayback()
{
    const auto *audio = m_box.audio();
    if (!audio || !m_playback)
        return;
    m_playedSerial = m_box.serial();
    if (const auto range = m_playback->stop(audio->sampleRate(), audio->sampleCount())) {
        m_lastRange = range; // the remembered position to the last end, again
        m_playTimer.start();
    } else {
        m_playTimer.stop();
        m_cursor.reset();
        emit cursorChanged();
    }
    playbackStateChanged();
}

// Legacy AudioBox::OnAccelerator. AUDIO_PLAY and AUDIO_PLAY_LINE both run
// OnPlaySelection; GetTimesSelection's karaoke flag (A5) is the event's id
// == AUDIO_PLAY_LINE, and an AUDIO_PLAY_LINE_ALT key arrives as that id.
bool AudioController::runHotkey(const QString &symbol)
{
    using application::PlayMode;
    static const std::map<QString, std::function<void(AudioController &)>> actions = {
        {QStringLiteral("AUDIO_PLAY"), [](AudioController &c) { c.focusAndPlay(PlayMode::Selection); }},
        {QStringLiteral("AUDIO_PLAY_LINE"), [](AudioController &c) { c.focusAndPlay(PlayMode::Line); }},
        {QStringLiteral("AUDIO_STOP"), [](AudioController &c) { emit c.focusRequested(); c.stopPlayback(); }},
        {QStringLiteral("AUDIO_NEXT"), [](AudioController &c) { c.nextLine(); }},
        {QStringLiteral("AUDIO_PREVIOUS"), [](AudioController &c) { c.previousLine(); }},
        {QStringLiteral("AUDIO_PLAY_BEFORE_MARK"), [](AudioController &c) { c.focusAndPlay(PlayMode::BeforeMark); }},
        {QStringLiteral("AUDIO_PLAY_AFTER_MARK"), [](AudioController &c) { c.focusAndPlay(PlayMode::AfterMark); }},
        {QStringLiteral("AUDIO_PLAY_500MS_BEFORE"), [](AudioController &c) { c.focusAndPlay(PlayMode::Before500); }},
        {QStringLiteral("AUDIO_PLAY_500MS_AFTER"), [](AudioController &c) { c.focusAndPlay(PlayMode::After500); }},
        {QStringLiteral("AUDIO_PLAY_500MS_FIRST"), [](AudioController &c) { c.focusAndPlay(PlayMode::First500); }},
        {QStringLiteral("AUDIO_PLAY_500MS_LAST"), [](AudioController &c) { c.focusAndPlay(PlayMode::Last500); }},
        {QStringLiteral("AUDIO_PLAY_TO_END"), [](AudioController &c) { c.focusAndPlay(PlayMode::ToEnd); }},
        {QStringLiteral("AUDIO_COMMIT"), [](AudioController &c) { c.commit(); }},
        {QStringLiteral("AUDIO_GOTO"), [](AudioController &c) { c.goToSelection(); }},
        {QStringLiteral("AUDIO_LEAD_IN"), [](AudioController &c) { c.leadIn(); }},
        {QStringLiteral("AUDIO_LEAD_OUT"), [](AudioController &c) { c.leadOut(); }},
        {QStringLiteral("AUDIO_KARAOKE_SPLIT"), [](AudioController &c) { c.splitSyllableAtCursor(); }}, // KA1
        {QStringLiteral("AUDIO_KARAOKE_KF"), [](AudioController &c) { c.switchLongSyllablesToKf(); }},  // KA1
        {QStringLiteral("AUDIO_SCROLL_LEFT"), [](AudioController &c) { c.scrollRight(); }},
        {QStringLiteral("AUDIO_SCROLL_RIGHT"), [](AudioController &c) { c.scrollLeft(); }},
        // KA2
        {QStringLiteral("AUDIO_TAP"), [](AudioController &c) { c.toggleTap(); }},
        {QStringLiteral("AUDIO_SPEED_DOWN"), [](AudioController &c) { c.stepSpeed(-1); }},
        {QStringLiteral("AUDIO_SPEED_UP"), [](AudioController &c) { c.stepSpeed(1); }},
    };
    const auto found = actions.find(symbol);
    if (found == actions.end())
        return false;
    // KA2-tap-to-time: while tapping the other audio hotkeys wait (their
    // buttons too: see tapBusy); Stop stops tapping
    if (tapBusy()) {
        if (symbol == QLatin1String("AUDIO_STOP"))
            tapStop();
        else if (symbol == QLatin1String("AUDIO_TAP") || symbol.startsWith(QLatin1String("AUDIO_SPEED_")))
            found->second(*this);
        return true;
    }
    if (hasAudio())
        found->second(*this);
    return true;
}

// The play buttons' and keys' handlers focus the display first (SetFocus).
void AudioController::focusAndPlay(application::PlayMode mode)
{
    emit focusRequested();
    play(mode);
}

// Legacy UpdateTimer: the cursor follows the player, the view scrolls to
// keep it in sight; while the player is not playing the cursor is cleared
// without a redraw.
void AudioController::tick()
{
    if (!m_playback)
        return m_playTimer.stop();
    // KA2: the playing reached the audio's end when this tick stops it past
    // the end, not when the player stopped without it (a failed output)
    const bool wasPlaying = playing();
    const std::int64_t at = wasPlaying ? m_player->position() : 0;
    const auto redrawn = m_playback->tick(m_view, m_scrollbarThickness);
    m_cursor = m_playback->cursorPainted() ? std::optional(m_playback->cursorX()) : std::nullopt;
    // AV2: the video shows where the box is (legacy GetMSAtSample)
    // inside the played range (not in the silence after its end)
    if (m_syncVideo && m_showVideoAt && playing())
        if (const auto *audio = m_box.audio(); audio && audio->sampleRate() > 0)
            if (const auto at = m_player->position(); at >= m_player->startPosition() && at <= m_player->endPosition())
                m_showVideoAt(static_cast<int>(at * 1000 / audio->sampleRate()));
    if (redrawn == application::AudioPlayback::Redraw::Image)
        redraw();
    if (redrawn != application::AudioPlayback::Redraw::None)
        emit cursorChanged();
    if (!m_playback->timerRunning() || !playing())
        m_playTimer.stop();
    if (m_tapState == TapTapping) {
        if (!playing())
            tapPlayingEnded(wasPlaying && at > m_player->endPosition());
        emit tapSpansChanged();
    }
    playbackStateChanged();
}

void AudioController::playbackStateChanged()
{
    if (playing() == m_wasPlaying)
        return;
    m_wasPlaying = playing();
    emit playingChanged();
}

// Legacy SetFile's unload: a playing player is stopped (Stop: a playing
// video is paused instead), then the player is closed with the audio.
void AudioController::releasePlayer()
{
    if (playing())
        stopPlayback();
    m_playedSerial = m_box.serial();
    m_playTimer.stop();
    m_player->stop();
    m_player->close();
    if (m_cursor && m_playback && m_playback->cursorPainted()) {
        m_playback->hideCursor();
        m_cursor.reset();
        emit cursorChanged();
    }
    playbackStateChanged();
}

// Legacy UpdateImage: the samples per column first, then a new image.
void AudioController::redraw()
{
    if (m_view.hasSource())
        m_view.updateSamples();
    m_timing.drawn(m_view, {m_startMs, m_endMs, m_needCommit}); // A3: DoUpdateImage's selStart, selEnd, selMark
    m_scrollbar = m_view.scrollbar();
    ++m_revision;
    emit displayChanged();
}

// A3: timing.

application::AudioTimingOptions AudioController::timingOptions() const
{
    return m_timingSettings ? m_timingSettings() : application::AudioTimingOptions{};
}

application::AudioSnapContext AudioController::snapContext() const
{
    const auto options = timingOptions();
    application::AudioSnapContext snap;
    snap.view = &m_view;
    snap.snapToKeyframes = options.snapToKeyframes;
    snap.snapToOtherLines = options.snapToOtherLines;
    snap.drawKeyframes = m_options.drawKeyframes;
    snap.inactiveLines = m_options.inactiveLines;
    snap.keyframesMs = m_keyframes;
    snap.keyframeSnapMs = m_keyframeSnap;
    snap.lines = m_lines;
    snap.active = m_active;
    return snap;
}

// Legacy CommitChanges: the times go into the editor's fields and, with
// `save`, to the Line (EditBox::Send); then Update(moveToEnd).
bool AudioController::commitChanges(bool nextLine, bool save, bool moveToEnd, application::AudioAdjacent adjacent,
                                    std::optional<std::u16string> karaokeText, const char *stepName, bool activeOnly,
                                    bool scroll)
{
    if (!loaded())
        return false;
    if (save)
        m_needCommit = false;
    bool committed = true;
    if (m_hooks.commit) {
        application::AudioCommitRequest request{m_startMs, m_endMs, save, nextLine, m_timing.hold() != 0, adjacent,
                                                std::move(karaokeText)};
        request.stepName = stepName;
        request.activeOnly = activeOnly;
        committed = m_hooks.commit(request);
    }
    syncCommittedStart();
    update(moveToEnd, scroll);
    return committed;
}

// AudioBox::OnCommit: CommitChanges(true), to the next Line.
// AU-next-line-on-commit: only while AUDIO_NEXT_LINE_ON_COMMIT is on (legacy
// never read it); off, the Line stays active.
void AudioController::commit()
{
    if (tapBusy())
        return;
    emit focusRequested();
    commitChanges(!m_store || m_store->boolean("audio.nextLineOnCommit"), true, false);
}

// Legacy ChangeLine: nothing past either end; otherwise the shown Line
// `delta` away becomes active (SubsGrid::SetActive, which runs SetDialogue
// again for the same Line).
void AudioController::changeLine(int delta)
{
    const int count = static_cast<int>(m_lines.size());
    if (m_active < 0 || m_active >= count)
        return;
    if ((m_active == 0 && delta < 0) || (m_active == count - 1 && delta > 0))
        return;
    const int before = m_active;
    const int next = application::legacyKeyFromPosition(m_lines, m_active, delta);
    if (m_hooks.setActive)
        m_hooks.setActive(next);
    if (next == before && m_active == before && loaded())
        reselect();
}

// AudioBox::OnNext / OnPrev: the display takes the focus when the new Line
// plays (A4's hook), and the new Line's selection plays.
// AudioDisplay::Next: in karaoke mode the next syllable, past the last the
// next Line's first.
void AudioController::nextLine()
{
    if (tapBusy())
        return;
    const bool play = !timingOptions().dontPlayWhenLineChanges;
    if (play)
        emit focusRequested();
    if (m_hasKara) {
        m_karaoke.current++;
        if (m_karaoke.current >= m_karaoke.count()) {
            m_karaoke.current = 0;
            changeLine(1);
        }
        redraw();
    } else {
        changeLine(1);
    }
    if (play && loaded()) { // GetTimesSelection, then Play
        const auto [start, end] = timesSelection();
        playRange(start, end);
    }
}

// AudioDisplay::Prev: the previous syllable only when it plays (legacy's
// `hasKara && play`), before the first the previous Line's last one;
// otherwise the previous Line.
void AudioController::previousLine()
{
    if (tapBusy())
        return;
    const bool play = !timingOptions().dontPlayWhenLineChanges;
    if (play)
        emit focusRequested();
    if (m_hasKara && play) {
        m_karaoke.current--;
        if (m_karaoke.current < 0) {
            changeLine(-1);
            m_karaoke.current = m_karaoke.count() - 1;
            if (m_view.hasSource())
                makeDialogueVisible();
        }
        redraw();
    } else {
        changeLine(-1);
    }
    if (play && loaded()) {
        const auto [start, end] = timesSelection();
        playRange(start, end);
    }
}

// AU1: the range goToSelection brings into view, as timesSelection(true)
// gives it: in Karaoke the current syllable's, else the Line's.
int AudioController::lineOutOfView() const
{
    if (!m_view.hasSource() || m_view.width() <= 0 || m_active < 0 || m_active >= static_cast<int>(m_lines.size()))
        return 0;
    auto [start, end] = std::pair(m_startMs, m_endMs);
    if (m_hasKara && m_karaoke.count())
        std::tie(start, end) =
            m_karaoke.visibleTimes(std::clamp(m_karaoke.current, 0, m_karaoke.count() - 1), m_startMs, m_endMs);
    if (m_view.xAtMs(end) < 0)
        return -1;
    if (m_view.xAtMs(start) > m_view.width())
        return 1;
    return 0;
}

// AudioBox::OnGoto: MakeDialogueVisible(true).

void AudioController::goToSelection()
{
    emit focusRequested();
    if (!m_view.hasSource())
        return;
    makeDialogueVisible(true, false);
    redraw();
}

// Legacy AddLead: the editor's fields take the times (UpdateTimeEditCtrls),
// the selection is modified, and AUDIO_AUTO_COMMIT commits it.
void AudioController::addLead(bool in, bool out)
{
    if (tapBusy())
        return;
    emit focusRequested();
    if (!loaded())
        return;
    const auto options = timingOptions();
    const int startBefore = m_startMs;
    std::tie(m_startMs, m_endMs) =
        application::legacyAddLead(m_startMs, m_endMs, in, out, options.leadIn, options.leadOut);
    // KA1-lead-in-keeps-syllables: in Karaoke mode the syllables stay at
    // their times and their text goes into the editor with the times (in
    // Line mode the commit keeps the text's syllables in place); with several
    // Lines selected each would take this one's text, so they keep theirs
    // (legacy, as in Line mode; Codex)
    std::optional<std::u16string> karaokeText;
    const bool several = m_hooks.severalSelected && m_hooks.severalSelected();
    // the syllables are read from the start the last commit wrote (Codex)
    const int committedBefore = m_hooks.fieldTime ? m_hooks.fieldTime(startBefore) : startBefore;
    if (m_hasKara && m_karaoke.count() && m_startMs != startBefore && !several) {
        m_karaoke.moveStart(committedBefore, committedStart());
        karaokeText = m_karaoke.text(committedStart());
    } else if (m_hasKara && m_karaoke.count() && m_startMs != startBefore) {
        // the text's \k tags stay, now from the new start: the syllables
        // move with it, as the text will read (Codex)
        const int by = committedStart() - committedBefore;
        for (auto &time : m_karaoke.times())
            time += by;
    }
    if (m_hooks.commit)
        m_hooks.commit({.startMs = m_startMs,
                        .endMs = m_endMs,
                        .save = false,
                        .nextLine = false,
                        .holding = m_timing.hold() != 0,
                        .adjacent = application::AudioAdjacent::None,
                        .karaokeText = std::move(karaokeText),
                        .duration = false}); // UpdateTimeEditCtrls: Start and End
    syncCommittedStart();
    m_needCommit = true;
    if (options.autoCommit)
        commitChanges(false, true, false);
    update();
}

// GLOBAL_SET_AUDIO_FROM_VIDEO / GLOBAL_SET_AUDIO_MARK_FROM_VIDEO: SetMark,
// then ChangePosition(time) (the time in the middle of the view).
void AudioController::showTime(int ms, bool mark)
{
    if (!m_view.hasSource() || m_view.samples() <= 0)
        return;
    if (mark) {
        m_timing.setMark(ms);
        emit markChanged();
    }
    std::int64_t samplepos = m_view.sampleAtMs(ms);
    samplepos = (samplepos / m_view.samples()) - (m_view.width() / 2);
    m_view.updatePosition(samplepos, false, m_scrollbarThickness);
    redraw();
}

// KA1: a start drag cut short keeps the syllables at their times, as its
// release does (Codex); one the right button carried them with commits them
// as its release would, since a later Commit reads the Line's own tags
void AudioController::lostCapture()
{
    application::AudioKaraokeMouse karaoke;
    if (m_hasKara) {
        karaoke.karaoke = &m_karaoke;
        karaoke.fieldTime = m_hooks.fieldTime;
    }
    if (m_timing.lostCapture(m_startMs, karaoke)) // the active Line's alone, as commitKaraoke (Codex)
        commitChanges(false, timingOptions().autoCommit, false, application::AudioAdjacent::None,
                      committedKaraokeText(), nullptr, true);
}

application::AudioMouseResult AudioController::mouse(const application::AudioMouse &event)
{
    if (m_box.state() != AudioBox::State::Ready || !m_view.hasSource())
        return {}; // legacy: nothing without a player and provider
    application::AudioTiming::Selection selection{m_startMs, m_endMs, m_needCommit};
    const bool hadMark = m_timing.hasMark();
    const int mark = m_timing.markMs();
    // A5: karaoke mode's syllables, with AUDIO_KARAOKE_MOVE_ON_CLICK read now
    application::AudioKaraokeMouse karaoke;
    if (m_hasKara) {
        karaoke.karaoke = &m_karaoke;
        karaoke.moveOnClick = m_store && m_store->boolean("audio.karaokeMoveOnClick");
        karaoke.measure = m_measure;
        karaoke.fieldTime = m_hooks.fieldTime;
        karaoke.onsetsMs = snapOnsets(); // KA5
    }
    const auto hover = std::tuple(m_karaoke.hover, m_karaoke.character, m_karaoke.current);
    auto result =
        m_timing.mouse(event, m_view, selection, snapContext(), timingOptions(), m_scrollbarThickness, karaoke);
    if (std::tuple(m_karaoke.hover, m_karaoke.character, m_karaoke.current) != hover)
        result.redraw = true; // the letter's mark and the current syllable are drawn
    m_startMs = selection.startMs;
    m_endMs = selection.endMs;
    m_needCommit = selection.modified;
    if (m_timing.hasMark() != hadMark || m_timing.markMs() != mark)
        emit markChanged();
    if (result.seekVideoMs && m_hooks.seekVideo)
        m_hooks.seekVideo(*result.seekVideoMs);
    // legacy: a boundary moved while not playing to the end moves the
    // player's end (SetEndPosition, which stops a player already past it)
    if (result.playEnd && m_player && m_playback && !m_playback->playingToEnd()) {
        m_player->setEndPosition(*result.playEnd);
        playbackStateChanged();
    }
    if (result.playSelection) { // the middle double click: Play(GetTimesSelection)
        const auto [start, end] = timesSelection();
        playRange(start, end);
    }
    if (result.playMs) // A5: the right click's syllable
        playRange(result.playMs->first, result.playMs->second);
    // legacy Commit(moveToEnd): in karaoke mode the syllables' text into the
    // editor, then CommitChanges(false, AUDIO_AUTO_COMMIT, moveToEnd)
    if (result.commit)
        commitChanges(false, timingOptions().autoCommit, *result.commit, result.adjacent,
                      m_hasKara ? std::optional(committedKaraokeText()) : std::nullopt, nullptr, false,
                      !result.keepView);
    else if (result.redraw)
        redraw();
    return result;
}

// KA2 (#277): slowed playback and tap-to-time.

namespace {

// A syllable's or Line's text as the tap bar shows it: without its override blocks.
QString visibleText(std::u16string_view text)
{
    QString out;
    bool inBlock = false;
    for (const char16_t c : text) {
        if (c == u'{')
            inBlock = true;
        else if (c == u'}' && inBlock)
            inBlock = false;
        else if (!inBlock)
            out += QChar(c);
    }
    return out.simplified();
}

// The calibration's beeps: 1 kHz, 40 ms with 5 ms fades, every
// kCalibrationIntervalMs from 1 s, then 1 s of silence.
constexpr int kBeepRate = 48000;
constexpr int kFirstBeepMs = 1000;

std::vector<double> calibrationBeatsMs()
{
    std::vector<double> beats;
    for (int i = 0; i < application::kCalibrationBeats; i++)
        beats.push_back(kFirstBeepMs + double(i) * application::kCalibrationIntervalMs);
    return beats;
}

std::shared_ptr<application::DisplayAudio> calibrationBeeps()
{
    const auto beats = calibrationBeatsMs();
    const auto frames = static_cast<std::int64_t>((beats.back() + 1000) * kBeepRate / 1000);
    std::vector<std::int16_t> samples(static_cast<std::size_t>(frames), 0);
    const int beep = kBeepRate * 40 / 1000, fade = kBeepRate * 5 / 1000;
    for (const double at : beats) {
        const auto start = static_cast<std::int64_t>(at * kBeepRate / 1000);
        for (int i = 0; i < beep; i++) {
            const double gain = std::min({1.0, double(i) / fade, double(beep - i) / fade});
            samples[static_cast<std::size_t>(start + i)] = static_cast<std::int16_t>(
                12000 * gain * std::sin(2 * 3.14159265358979 * 1000 * double(i) / kBeepRate));
        }
    }
    auto audio = std::make_shared<application::DisplayAudio>(kBeepRate, frames);
    audio->appendFrames(samples.data(), frames, 1);
    audio->finish();
    return audio;
}

} // namespace

void AudioController::applySpeed()
{
    if (m_player)
        m_player->setSpeed(m_speedPercent / 100.0);
}

void AudioController::setSpeedPercent(int percent)
{
    percent = std::clamp(percent, 25, 100);
    if (percent == m_speedPercent)
        return;
    m_speedPercent = percent;
    if (m_store)
        m_store->set("audio.speedPercent", percent);
    applySpeed();
    emit boxControlsChanged();
}

// The preset nearest the speed, `delta` presets on.
void AudioController::stepSpeed(int delta)
{
    const int count = static_cast<int>(std::size(kSpeeds));
    int nearest = 0;
    for (int i = 1; i < count; i++)
        if (std::abs(kSpeeds[i] - m_speedPercent) < std::abs(kSpeeds[nearest] - m_speedPercent))
            nearest = i;
    setSpeedPercent(kSpeeds[std::clamp(nearest + delta, 0, count - 1)]);
}

double AudioController::positionMs() const
{
    const auto *audio = m_box.audio();
    if (!audio || !m_player || audio->sampleRate() <= 0)
        return 0;
    return double(m_player->position()) * 1000.0 / audio->sampleRate();
}

int AudioController::tapSongNow() const
{
    return application::tapSongMs(positionMs(), m_tapLatency, m_speedPercent / 100.0);
}

void AudioController::setTapState(TapState state)
{
    m_tapState = state;
    emit tapChanged();
    emit tapSpansChanged();
}

void AudioController::beginTapLine()
{
    m_tap.begin(application::KaraokeTap::targets(m_karaoke), m_startMs);
    m_tapMessage.clear();
    if (m_tapState == TapTapping && m_tap.needsSplit()) {
        if (playing())
            stopBoxPlayback();
        m_tapState = TapReady;
    } else if (m_tapState == TapTapping && !m_tapAdvancing) {
        // another Line chosen while tapping: its taps come from its own
        // playing, from before it (the playing went on for the old Line),
        // and the keys from the audio panel again once the grid's click has
        // taken the focus
        playForTap(m_tap.resumeMs());
        QMetaObject::invokeMethod(this, [this] { if (m_tapState == TapTapping) emit focusRequested(); },
                                  Qt::QueuedConnection);
    }
    emit tapChanged();
    emit tapSpansChanged();
}

void AudioController::playForTap(int fromMs)
{
    m_tapPlaying = true;
    playRange(fromMs, -1);
    m_tapPlaying = false;
}

void AudioController::toggleTap()
{
    emit focusRequested();
    if (m_tapState != TapOff) {
        m_tap.revert();
        if (playing())
            stopBoxPlayback();
        m_tapMessage.clear();
        setTapState(TapOff);
        return;
    }
    if (!m_hasKara || !hasAudio())
        return;
    beginTapLine();
    if (m_tap.needsSplit())
        return setTapState(TapReady);
    setTapState(TapTapping);
    playForTap(m_tap.resumeMs());
}

void AudioController::setTapHold(bool hold)
{
    // a held syllable is dropped (see KaraokeTap::setMode)
    m_tap.setMode(hold ? application::KaraokeTap::Mode::Hold : application::KaraokeTap::Mode::Press);
    if (m_store)
        m_store->set("audio.tapHold", hold);
    emit tapChanged();
    emit tapSpansChanged();
}

void AudioController::setOnsetSnap(bool on)
{
    if (m_onsetSnap == on)
        return;
    m_onsetSnap = on;
    if (m_store)
        m_store->set("audio.onsetSnap", on);
    findOnsets();
    emit boxControlsChanged();
}

std::span<const int> AudioController::snapOnsets() const
{
    if (!m_onsetSnap || !m_hasKara || !onsetsFound())
        return {};
    return *m_onsets;
}

std::string AudioController::onsetKey() const
{
    const auto *audio = m_box.audio();
    return m_box.path() + '\n' + std::to_string(m_box.track()) + '\n' + std::to_string(m_box.delayFrames()) + '\n' +
           std::to_string(audio ? audio->sampleCount() : 0);
}

// KA5-onset-snap: the open audio's onsets, from this session's cache or found
// by a worker over the loaded audio, while snapping is on in Karaoke mode.
// A worker for audio since closed, or once snapping is off or Karaoke mode
// left, is cancelled; one finished for the same file is kept for when it
// opens again.
void AudioController::findOnsets()
{
    if (m_onsetsSerial != m_box.serial()) {
        stopFindingOnsets();
        m_onsets.reset();
        m_onsetsSerial = m_box.serial();
    }
    if (!m_onsetSnap || !m_hasKara)
        return stopFindingOnsets(); // a detection no longer wanted stops
    if (!ready() || m_onsets || m_onsetCancel)
        return;
    const std::string key = onsetKey();
    if (const auto it = m_onsetCache.find(key); it != m_onsetCache.end()) {
        m_onsets = it->second;
        emit onsetsChanged();
        return;
    }
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_onsetCancel = cancel;
    ++m_onsetDetections;
    emit findingOnsetsChanged();
    m_onsetWorker = std::thread([this, audio = m_box.sharedAudio(), cancel, key, serial = m_onsetsSerial] {
        std::optional<std::vector<int>> found;
        try {
            found = application::detectVocalOnsets(*audio, cancel.get());
        } catch (const std::exception &) {
            found.emplace(); // out of memory: no onsets, rather than the editor ending
        }
        if (!found)
            return;
        auto onsets = std::make_shared<const std::vector<int>>(std::move(*found));
        QMetaObject::invokeMethod(
            this,
            [this, cancel, key, serial, onsets] {
                if (cancel->load())
                    return; // stopFindingOnsets has joined it
                m_onsetWorker.join();
                m_onsetCancel.reset();
                m_onsetCache[key] = onsets;
                const bool current = serial == m_box.serial();
                if (current)
                    m_onsets = onsets;
                emit findingOnsetsChanged();
                if (current)
                    emit onsetsChanged();
            },
            Qt::QueuedConnection);
    });
}

void AudioController::stopFindingOnsets()
{
    if (m_onsetCancel)
        *m_onsetCancel = true;
    if (m_onsetWorker.joinable())
        m_onsetWorker.join();
    if (std::exchange(m_onsetCancel, nullptr))
        emit findingOnsetsChanged();
}

void AudioController::setTapLatency(int ms)
{
    m_tapLatency = std::clamp(ms, -200, 500);
    if (m_store)
        m_store->set("audio.tapLatency", m_tapLatency);
    emit tapChanged();
}

bool AudioController::tapKey(int key, bool pressed, bool autoRepeat, bool shift)
{
    using Event = application::KaraokeTap::Event;
    if (m_tapState == TapOff)
        return false;
    if (key == Qt::Key_Backspace) {
        if (pressed && !autoRepeat)
            tapUndo(shift);
        return true;
    }
    if (key == Qt::Key_Escape) {
        if (pressed && !autoRepeat) {
            if (m_tapState == TapTapping)
                tapStop();
            else
                toggleTap();
        }
        return true;
    }
    if (key != Qt::Key_Space)
        return false;
    if (autoRepeat)
        return true;
    if (m_tapState != TapTapping) {
        if (pressed)
            tapResume();
        return true;
    }
    int songMs = tapSongNow();
    // KA5-onset-snap: a press (a syllable's start, or Press mode's boundary)
    // lands on the nearest onset from the last boundary on; a release stays
    if (pressed)
        songMs =
            application::snapToOnset(snapOnsets(), songMs, application::kOnsetSnapWindowMs, m_tap.lastBoundary());
    const Event event = pressed ? m_tap.press(key, songMs) : m_tap.release(key, songMs);
    if (event == Event::LineDone)
        finishTapLine(pressed ? std::optional(key) : std::nullopt, songMs);
    else if (event != Event::None)
        emit tapChanged();
    emit tapSpansChanged();
    return true;
}

void AudioController::tapFocusLost()
{
    if (m_tapState == TapTapping)
        tapKey(Qt::Key_Space, false, false, false);
}

// The finished Line committed as one step, the next Line made active
// (reselect begins it); Press mode's press that ended the Line starts the
// next one's first syllable.
void AudioController::finishTapLine(std::optional<int> carryKey, int carryMs)
{
    const auto result = m_tap.result();
    std::vector<std::u16string> syllables, tags;
    std::vector<int> times;
    for (const auto &s : result.syllables) {
        syllables.push_back(s.text);
        tags.push_back(s.tag);
        times.push_back(s.endMs);
    }
    m_karaoke.assign(std::move(syllables), std::move(tags), std::move(times));
    m_startMs = result.startMs;
    m_endMs = result.endMs;
    const int before = m_active;
    const bool hasNext = application::legacyKeyFromPosition(m_lines, m_active, 1) != m_active;
    m_tapAdvancing = true;
    // KA1: the text for the start the commit writes, the taps kept
    const bool committed = commitChanges(hasNext, true, false, application::AudioAdjacent::None,
                                         committedKaraokeText(), "Tap timing", true);
    m_tapAdvancing = false;
    if (!committed) {
        // refused (an edit the Line editor shows as not valid, a read-only
        // file): the taps stay in the Line's fields, nothing goes on
        if (playing())
            stopBoxPlayback();
        m_tap.begin(application::KaraokeTap::targets(m_karaoke), m_startMs);
        m_tapMessage = QCoreApplication::translate("HikariSub.Audio",
                                                   "Not applied: the Line cannot take these times as it is.");
        setTapState(TapReady);
        return;
    }
    if (!hasNext || m_active == before) {
        if (playing())
            stopBoxPlayback();
        m_tap.begin(application::KaraokeTap::targets(m_karaoke), m_startMs);
        m_tapMessage = QCoreApplication::translate("HikariSub.Audio", "Every Line is tapped.");
        setTapState(TapReady);
        return;
    }
    if (carryKey && m_tapState == TapTapping)
        m_tap.press(*carryKey, carryMs);
    emit tapChanged();
    emit tapSpansChanged();
}

// The tap playing stopped by itself. At the audio's end a held syllable (the
// last press's in Press mode) ends there, and it may finish the Line; a player
// that stopped before it (a failed output) drops the held syllable as Esc
// does. Then tapping stops.
void AudioController::tapPlayingEnded(bool atEnd)
{
    const auto *audio = m_box.audio();
    if (atEnd && audio && m_player && audio->sampleRate() > 0) {
        const int endMs = static_cast<int>(m_player->endPosition() * 1000 / audio->sampleRate());
        if (m_tap.finish(endMs) == application::KaraokeTap::Event::LineDone)
            finishTapLine(std::nullopt, endMs);
    }
    if (m_tapState != TapTapping)
        return;
    m_tap.stop();
    m_tapMessage = QCoreApplication::translate("HikariSub.Audio", "%1 of %2 tapped").arg(m_tap.tapped()).arg(m_tap.count());
    setTapState(TapStopped);
}

void AudioController::tapStop()
{
    if (m_tapState != TapTapping)
        return;
    m_tap.stop();
    if (playing())
        stopBoxPlayback();
    m_tapMessage = QCoreApplication::translate("HikariSub.Audio", "%1 of %2 tapped").arg(m_tap.tapped()).arg(m_tap.count());
    setTapState(TapStopped);
}

void AudioController::tapResume()
{
    if (m_tapState == TapOff || m_tap.needsSplit())
        return;
    m_tapMessage.clear();
    setTapState(TapTapping);
    playForTap(m_tap.resumeMs());
}

void AudioController::tapRevert()
{
    if (m_tapState == TapOff)
        return;
    m_tap.revert();
    if (playing())
        stopBoxPlayback();
    m_tapMessage.clear();
    setTapState(TapReady);
}

void AudioController::tapUndo(bool line)
{
    if (m_tapState == TapOff)
        return;
    // the note names the earliest syllable taken back
    const int earliest = m_tap.tapped() > 0 ? m_tap.tapped() - 1 : m_tap.holding() ? 0 : -1;
    const QString name = earliest >= 0 ? visibleText(m_tap.syllables()[static_cast<std::size_t>(earliest)].text)
                                       : QString();
    const int from = line ? m_tap.undoLine() : m_tap.undo();
    if (line)
        m_tapMessage = QCoreApplication::translate("HikariSub.Audio", "Undid the Line");
    else
        m_tapMessage = name.isEmpty() ? QString() : QCoreApplication::translate("HikariSub.Audio", "Undid “%1”").arg(name);
    if (m_tapState == TapTapping)
        playForTap(from);
    emit tapChanged();
    emit tapSpansChanged();
}

void AudioController::tapAutoSplit()
{
    if (m_tapState == TapOff)
        return;
    if (!m_karaAuto)
        toggleKaraokeSplitMode();
    else
        splitKaraoke();
    beginTapLine();
    redraw();
}

QVariantList AudioController::tapSyllables() const
{
    QVariantList out;
    const auto states = m_tap.states();
    for (std::size_t i = 0; i < states.size(); i++)
        out.push_back(QVariantMap{{QStringLiteral("text"), visibleText(m_tap.syllables()[i].text)},
                                  {QStringLiteral("state"), static_cast<int>(states[i])}});
    return out;
}

QString AudioController::tapNextText() const
{
    if (!m_lineText || m_active < 0)
        return {};
    const int next = application::legacyKeyFromPosition(m_lines, m_active, 1);
    if (next == m_active)
        return {};
    return visibleText(m_lineText(next).toStdU16String());
}

QVariantList AudioController::tapSpans() const
{
    QVariantList out;
    if (m_tapState == TapOff || !m_view.hasSource())
        return out;
    const auto spans = m_tap.spans(m_tapState == TapTapping ? tapSongNow() : 0);
    for (std::size_t i = 0; i < spans.size(); i++) {
        const bool held = m_tap.holding() && i + 1 == spans.size();
        out.push_back(QVariantMap{{QStringLiteral("x1"), m_view.xAtMs(spans[i].startMs)},
                                  {QStringLiteral("x2"), m_view.xAtMs(spans[i].endMs)},
                                  {QStringLiteral("text"), visibleText(m_tap.syllables()[i].text)},
                                  {QStringLiteral("held"), held}});
    }
    return out;
}

void AudioController::startCalibration()
{
    if (!m_player)
        return;
    if (m_tapState == TapTapping)
        tapStop();
    if (playing())
        stopBoxPlayback();
    m_calibrationPresses.clear();
    m_calibration = {};
    m_calibrationBeat = 0;
    m_calibrating = m_player->playClip(calibrationBeeps());
    if (m_calibrating)
        m_calibrationTimer.start();
    emit calibrationChanged();
}

void AudioController::calibrationPress()
{
    if (!m_calibrating || !m_player)
        return;
    m_calibrationPresses.push_back(double(m_player->position()) * 1000.0 / kBeepRate);
    m_calibration = application::calibrateLatency(calibrationBeatsMs(), m_calibrationPresses);
    emit calibrationChanged();
}

void AudioController::calibrationTick()
{
    if (!m_calibrating)
        return m_calibrationTimer.stop();
    const auto beats = calibrationBeatsMs();
    const double at = m_player ? double(m_player->position()) * 1000.0 / kBeepRate : 0;
    const int heard = static_cast<int>(std::ranges::count_if(beats, [at](double b) { return b <= at; }));
    const bool over = !m_player || !m_player->playing() || at > beats.back() + 900;
    if (heard != m_calibrationBeat || over) {
        m_calibrationBeat = heard;
        if (over)
            stopCalibration();
        emit calibrationChanged();
    }
}

void AudioController::stopCalibration()
{
    if (m_calibrating && m_player && m_player->playing())
        m_player->stop();
    m_calibrating = false;
    m_calibrationTimer.stop();
    emit calibrationChanged();
}

QVariantList AudioController::calibrationDelays() const
{
    QVariantList out;
    const auto beats = calibrationBeatsMs();
    for (const double press : m_calibrationPresses) {
        const auto nearest = std::ranges::min_element(beats, {}, [press](double b) { return std::fabs(press - b); });
        const auto index = nearest - beats.begin();
        const double delay = press - *nearest;
        if (index >= application::kCalibrationPractice && std::fabs(delay) <= application::kCalibrationIntervalMs / 2.0)
            out.push_back(delay);
    }
    return out;
}

} // namespace hikari::ui
