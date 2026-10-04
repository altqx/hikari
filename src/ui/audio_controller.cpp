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
    if (autoScrollSetting())
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
