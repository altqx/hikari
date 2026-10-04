#include "audio_controller.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

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
    m_view.setSamplesPercent(m_options.horizontalZoom, false);
    m_view.setScale(application::audioScaleFromSlider(m_options.verticalZoom));
    m_previousActive = 0;
    m_fresh = true;
    m_cursor.reset();
    m_scrollbar = {};
    // A4: a new box plays with legacy's defaults and has no mark
    m_markMs.reset();
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
    if (m_options.grabTimesOnSelect)
        std::tie(m_startMs, m_endMs) = application::legacyLineSelection(m_lines, m_active, m_previousActive);
    m_previousActive = m_active;
    update();
}

// Legacy Update: with auto-scroll the Line is made visible.
void AudioController::update()
{
    if (m_options.autoScroll)
        m_view.makeVisible(m_startMs, m_endMs, false, false, m_scrollbarThickness);
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

// Legacy OnMouseEvent's wheel without Shift or Ctrl (those zoom: A2).
void AudioController::wheel(int rotation)
{
    if (!m_view.hasSource())
        return;
    const int step = -rotation * m_view.width() / 360;
    m_view.updatePosition(m_view.position() + step, false, m_scrollbarThickness);
    redraw();
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

void AudioController::setPlaybackSettings(std::function<int()> markPlayTimeMs, std::function<int()> volume)
{
    m_markPlayTime = std::move(markPlayTimeMs);
    m_volume = std::move(volume);
}

void AudioController::setVideoPlayback(std::function<bool()> playing, std::function<void()> pause)
{
    m_videoPlaying = std::move(playing);
    m_pauseVideo = std::move(pause);
}

// Legacy AudioBox's play handlers, then AudioDisplay::Play: a mark play
// without a mark returns first; a playing video is paused; nothing plays
// without the audio's provider.
void AudioController::play(application::PlayMode mode)
{
    if (!hasAudio() || !m_playback)
        return;
    const auto request = application::legacyPlayRequest(mode, m_startMs, m_endMs, m_markMs,
                                                         m_markPlayTime ? m_markPlayTime() : 1000);
    if (!request)
        return;
    if (m_videoPlaying && m_videoPlaying() && m_pauseVideo)
        m_pauseVideo();
    const auto *audio = m_box.audio();
    if (!audio)
        return;
    if (m_volume) // AUDIO_VOLUME, as legacy OpenAudio and the slider set it
        m_player->setVolume(application::playbackVolumeFromSlider(m_volume()));
    m_playedSerial = m_box.serial();
    m_lastRange = m_playback->play(audio->sampleRate(), audio->sampleCount(), request->startMs, request->endMs);
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

bool AudioController::playbackKey(int key, int modifiers, bool inGrid)
{
    if (!hasAudio())
        return false; // no box, no audio hotkeys
    const bool keypad = modifiers & Qt::KeypadModifier;
    if ((modifiers & ~Qt::KeypadModifier) != Qt::NoModifier)
        return false;
    if (keypad) {
        if (key == Qt::Key_0)
            playBeforeMark(); // Num 0
        else if (key == Qt::Key_Period || key == Qt::Key_Comma)
            playAfterMark(); // Num . (the keypad's decimal key)
        else
            return false;
        return true;
    }
    switch (key) {
    case Qt::Key_Down:
    case Qt::Key_Up:
        if (inGrid)
            return false; // the Grid keeps its arrows
        key == Qt::Key_Down ? playSelection() : playLine();
        return true;
    case Qt::Key_S: playSelection(); return true; // AUDIO_PLAY_ALT
    case Qt::Key_R: playLine(); return true;      // AUDIO_PLAY_LINE_ALT
    case Qt::Key_H: stopPlayback(); return true;
    case Qt::Key_Q: play500Before(); return true;
    case Qt::Key_W: play500After(); return true;
    case Qt::Key_E: play500First(); return true;
    case Qt::Key_D: play500Last(); return true;
    case Qt::Key_T: playToEnd(); return true;
    default:
        return false;
    }
}

void AudioController::setPlaybackVolume(int slider)
{
    if (m_player)
        m_player->setVolume(application::playbackVolumeFromSlider(slider));
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
    m_scrollbar = m_view.scrollbar();
    ++m_revision;
    emit displayChanged();
}

} // namespace hikari::ui
