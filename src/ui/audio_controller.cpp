#include "audio_controller.h"

#include "settings_store.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
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
    m_box.setObserver([this] { boxChanged(); });
    m_box.setLog([this](const std::string &message, application::AudioBox::LogLevel level) {
        emit logged(QString::fromStdString(message), level == application::AudioBox::LogLevel::Debug);
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
    // A2 with A4: the volume slider (and the vertical zoom while linked)
    // sets the player's volume as it moves (legacy OnVolume, OnVerticalZoom,
    // OnVerticalLink: player->SetVolume).
    connect(this, &AudioController::volumeChanged, this, [this](float volume) {
        if (m_player)
            m_player->setVolume(volume);
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
}

QString AudioController::status() const
{
    switch (m_box.state()) {
    case AudioBox::State::Closed:
        return tr("No audio open");
    case AudioBox::State::Opening: {
        // legacy ProgressSink, titled for video
        const auto [done, total] = m_box.indexing();
        const int percent = total > 0 ? static_cast<int>(done * 100 / total) : 0;
        return tr("Indexing video %1%").arg(percent);
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
    m_box.open(path.toStdString());
}

void AudioController::closeAudio()
{
    m_box.close();
    newView();
    emit changed();
    redraw();
}

void AudioController::boxChanged()
{
    if (m_player && m_box.serial() != m_playedSerial)
        releasePlayer();
    const auto state = m_box.state();
    const bool loaded = state == AudioBox::State::Loading || state == AudioBox::State::Ready;
    const bool wasLoaded = m_seenState == AudioBox::State::Loading || m_seenState == AudioBox::State::Ready;
    m_seenState = state;
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
    if (m_options.grabTimesOnSelect)
        std::tie(m_startMs, m_endMs) = application::legacyLineSelection(m_lines, m_active, m_previousActive);
    m_previousActive = m_active;
    update();
}

// Legacy Update: with auto-scroll the Line is made visible.
void AudioController::update(bool moveToEnd)
{
    if (autoScrollSetting())
        m_view.makeVisible(m_startMs, m_endMs, false, moveToEnd, m_scrollbarThickness);
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
    return marks;
}

void AudioController::resize(int width, int height, int timelineHeight, int scrollbarThickness)
{
    m_scrollbarThickness = scrollbarThickness;
    m_view.resize(width, height, timelineHeight, scrollbarThickness);
    redraw();
}

void AudioController::setCursor(std::optional<float> x)
{
    if (playing())
        return; // legacy draws the mouse's cursor only while not playing
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
    m_store = store;
    loadBoxControls();
    emit boxControlsChanged();
    redraw();
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
    }
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
    const auto request =
        application::legacyPlayRequest(mode, m_startMs, m_endMs, mark, m_markPlayTime ? m_markPlayTime() : 1000);
    if (!request)
        return;
    playRange(request->startMs, request->endMs);
}

// Legacy AudioDisplay::Play(start, end, pause = true): a playing video is
// paused before anything else, then nothing plays without the audio's
// provider (here: the box's audio and a player).
void AudioController::playRange(int startMs, int endMs)
{
    if (m_videoPlaying && m_videoPlaying() && m_pauseVideo)
        m_pauseVideo();
    const auto *audio = m_box.audio();
    if (!audio || !m_playback)
        return;
    m_playedSerial = m_box.serial();
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
        {QStringLiteral("AUDIO_SCROLL_LEFT"), [](AudioController &c) { c.scrollRight(); }},
        {QStringLiteral("AUDIO_SCROLL_RIGHT"), [](AudioController &c) { c.scrollLeft(); }},
    };
    const auto found = actions.find(symbol);
    if (found == actions.end())
        return false;
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
    const auto redrawn = m_playback->tick(m_view, m_scrollbarThickness);
    m_cursor = m_playback->cursorPainted() ? std::optional(m_playback->cursorX()) : std::nullopt;
    if (redrawn == application::AudioPlayback::Redraw::Image)
        redraw();
    if (redrawn != application::AudioPlayback::Redraw::None)
        emit cursorChanged();
    if (!m_playback->timerRunning() || !playing())
        m_playTimer.stop();
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
void AudioController::commitChanges(bool nextLine, bool save, bool moveToEnd, application::AudioAdjacent adjacent)
{
    if (!loaded())
        return;
    if (save)
        m_needCommit = false;
    if (m_hooks.commit)
        m_hooks.commit({m_startMs, m_endMs, save, nextLine, m_timing.hold() != 0, adjacent});
    update(moveToEnd);
}

// AudioBox::OnCommit: CommitChanges(true), so the next Line follows whatever
// AUDIO_NEXT_LINE_ON_COMMIT says (legacy never reads it).
void AudioController::commit()
{
    emit focusRequested();
    commitChanges(true, true, false);
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
void AudioController::nextLine()
{
    const bool play = !timingOptions().dontPlayWhenLineChanges;
    if (play)
        emit focusRequested();
    changeLine(1);
    if (play && loaded()) // AudioDisplay::Next: GetTimesSelection, then Play
        playRange(m_startMs, m_endMs);
}

void AudioController::previousLine()
{
    const bool play = !timingOptions().dontPlayWhenLineChanges;
    if (play)
        emit focusRequested();
    changeLine(-1);
    if (play && loaded()) // AudioDisplay::Prev: GetTimesSelection, then Play
        playRange(m_startMs, m_endMs);
}

// AudioBox::OnGoto: MakeDialogueVisible(true).
void AudioController::goToSelection()
{
    emit focusRequested();
    if (!m_view.hasSource())
        return;
    m_view.makeVisible(m_startMs, m_endMs, true, false, m_scrollbarThickness);
    redraw();
}

// Legacy AddLead: the editor's fields take the times (UpdateTimeEditCtrls),
// the selection is modified, and AUDIO_AUTO_COMMIT commits it.
void AudioController::addLead(bool in, bool out)
{
    emit focusRequested();
    if (!loaded())
        return;
    const auto options = timingOptions();
    std::tie(m_startMs, m_endMs) =
        application::legacyAddLead(m_startMs, m_endMs, in, out, options.leadIn, options.leadOut);
    if (m_hooks.commit)
        m_hooks.commit({m_startMs, m_endMs, false, false, m_timing.hold() != 0, application::AudioAdjacent::None});
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

application::AudioMouseResult AudioController::mouse(const application::AudioMouse &event)
{
    if (m_box.state() != AudioBox::State::Ready || !m_view.hasSource())
        return {}; // legacy: nothing without a player and provider
    application::AudioTiming::Selection selection{m_startMs, m_endMs, m_needCommit};
    const bool hadMark = m_timing.hasMark();
    const int mark = m_timing.markMs();
    auto result = m_timing.mouse(event, m_view, selection, snapContext(), timingOptions(), m_scrollbarThickness);
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
    if (result.playSelection) // the middle double click: Play(GetTimesSelection)
        playRange(m_startMs, m_endMs);
    if (result.commit) // legacy Commit(moveToEnd): CommitChanges(false, AUDIO_AUTO_COMMIT, moveToEnd)
        commitChanges(false, timingOptions().autoCommit, *result.commit, result.adjacent);
    else if (result.redraw)
        redraw();
    return result;
}

} // namespace hikari::ui
