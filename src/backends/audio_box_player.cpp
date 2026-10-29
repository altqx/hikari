#include "hikari/backends/audio_box_player.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

#include <QMetaObject>
#include <QTimer>

#include <soundtouch/SoundTouch.h>

namespace hikari::backends {

namespace {

double steadySeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

AudioBoxPlayer::SharedAudio shareRaw(AudioBoxPlayer::Audio audio)
{
    return [audio = std::move(audio)]() -> std::shared_ptr<const application::DisplayAudio> {
        const auto *raw = audio ? audio() : nullptr;
        return raw ? std::shared_ptr<const application::DisplayAudio>(raw, [](const application::DisplayAudio *) {})
                   : nullptr;
    };
}

AudioBoxPlayer::AudioBoxPlayer(application::AudioOutputPort &output, Audio audio, QObject *parent)
    : AudioBoxPlayer([&output]() -> application::AudioOutputPort & { return output; }, std::move(audio), parent)
{
}

AudioBoxPlayer::AudioBoxPlayer(std::function<application::AudioOutputPort &()> output, Audio audio, QObject *parent)
    : AudioBoxPlayer(std::move(output), shareRaw(std::move(audio)), parent)
{
}

AudioBoxPlayer::AudioBoxPlayer(std::function<application::AudioOutputPort &()> output, SharedAudio audio,
                               QObject *parent)
    : QObject(parent), m_makeOutput(std::move(output)), m_audio(std::move(audio)), m_now(steadySeconds)
{
}

AudioBoxPlayer::~AudioBoxPlayer()
{
    {
        const std::scoped_lock lock(m_lock);
        m_quit = true;
        if (m_playing && m_output)
            m_output->stop();
        m_playing = false;
        m_playingAudio.reset();
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void AudioBoxPlayer::setPumping(Pumping pumping)
{
    const std::scoped_lock lock(m_lock);
    m_pumping = pumping;
}

void AudioBoxPlayer::fillThread()
{
    std::unique_lock lock(m_lock);
    while (!m_quit) {
        if (m_playing && m_pumping == Pumping::Thread) {
            pumpLocked();
            m_wake.wait_for(lock, std::chrono::milliseconds(5));
        } else {
            m_wake.wait(lock);
        }
    }
}

void AudioBoxPlayer::fail(const QString &message)
{
    if (std::this_thread::get_id() == m_threadId)
        QMetaObject::invokeMethod(this, [this, message] { emit failed(message); }, Qt::QueuedConnection);
    else
        emit failed(message);
}

application::AudioOutputPort &AudioBoxPlayer::output()
{
    if (!m_output)
        m_output = &m_makeOutput();
    return *m_output;
}

void AudioBoxPlayer::close()
{
    const std::scoped_lock lock(m_lock);
    stopLocked();
    if (m_output && m_srcFormat.sampleRate > 0)
        m_output->close();
    m_srcFormat = m_outFormat = {0, 0};
}

bool AudioBoxPlayer::openOutput(const application::DisplayAudio &audio)
{
    const application::OutputFormat want{audio.sampleRate(), audio.channels()};
    const auto status = output().status();
    if (status.open && !status.deviceLost && want == m_srcFormat && !m_rescan)
        return true;
    const auto opened = openDevice(want);
    if (!opened) {
        m_srcFormat = m_outFormat = {0, 0};
        fail(QStringLiteral("Failed creating audio playback device."));
        return false;
    }
    m_srcFormat = want;
    m_outFormat = *opened;
    return true;
}

// PortAudio enumerates devices once per initialization, so a device change needs a rescan.
std::expected<application::OutputFormat, application::OutputError>
AudioBoxPlayer::openDevice(application::OutputFormat want)
{
    auto &out = output();
    if (out.status().open)
        out.close();
    std::string id;
    if (m_rescan || !m_deviceName.empty()) {
        const auto devices = out.devices();
        m_rescan = false;
        if (!m_deviceName.empty()) {
            std::string hostApi;
            if (const auto d = std::ranges::find(devices, true, &application::OutputDevice::isDefault); d != devices.end())
                hostApi = d->hostApi;
            for (const auto &device : devices)
                if (device.name == m_deviceName && (hostApi.empty() || device.hostApi == hostApi)) {
                    id = device.id;
                    break;
                }
        }
    }
    auto opened = out.open(id, want);
    if (!opened && !id.empty())
        opened = out.open({}, want);
    return opened;
}

void AudioBoxPlayer::play(std::int64_t start, std::int64_t count)
{
    playAudio(nullptr, start, count);
}

bool AudioBoxPlayer::playClip(std::shared_ptr<const application::DisplayAudio> clip)
{
    if (!clip)
        return false;
    const std::int64_t count = clip->sampleCount();
    playAudio(std::move(clip), 0, count);
    return playing();
}

void AudioBoxPlayer::playAudio(std::shared_ptr<const application::DisplayAudio> clip, std::int64_t start,
                               std::int64_t count)
{
    {
        const std::scoped_lock lock(m_lock);
        const bool clipping = clip != nullptr;
        auto audio = clipping ? std::move(clip) : m_audio ? m_audio() : nullptr;
        if (!audio || !openOutput(*audio))
            return;
        m_clipping = clipping;
        output().stop();
        m_playingAudio = std::move(audio);
        m_start = m_base = m_next = start;
        m_waiting = false;
        m_attempts = 0;
        m_end = start + count;
        m_written = 0;
        m_pending.clear();
        m_primed = false;
        m_phase = 0;
        m_baseConsumed = output().status().framesConsumed;
        const std::int64_t bytesPerFrame = std::int64_t{m_srcFormat.channels} * 2;
        const std::int64_t wantedBytes = kWantedLatencyMs * (m_srcFormat.sampleRate * bytesPerFrame) / 1000;
        m_oneShot = count > 0 && count * bytesPerFrame < wantedBytes;
        m_rangeFrames = static_cast<std::int64_t>(
            double(count * m_outFormat.sampleRate / std::max(1, m_srcFormat.sampleRate)) / playSpeed());
        resetStretch();
        m_playing = true;
        m_started = false;
        fill();
        if (m_pumping == Pumping::Thread && !m_thread.joinable()) {
            m_thread = std::thread([this] { fillThread(); });
            m_threadId = m_thread.get_id();
        }
    }
    m_wake.notify_all();
}

bool AudioBoxPlayer::playingClip(const application::DisplayAudio *clip) const
{
    const std::scoped_lock lock(m_lock);
    return m_playing && m_clipping && m_playingAudio.get() == clip;
}

void AudioBoxPlayer::stop()
{
    const std::scoped_lock lock(m_lock);
    stopLocked();
}

void AudioBoxPlayer::stopLocked()
{
    if (!m_playing)
        return;
    output().stop();
    halt();
}

void AudioBoxPlayer::halt()
{
    m_playing = false;
    m_started = false;
    m_waiting = false;
    m_pending.clear();
    m_playingAudio.reset();
    if (m_reopen)
        releaseOutput();
}

void AudioBoxPlayer::setOutputDevice(std::string name)
{
    const std::scoped_lock lock(m_lock);
    m_deviceName = std::move(name);
}

void AudioBoxPlayer::switchOutput()
{
    const std::scoped_lock lock(m_lock);
    m_rescan = true;
    if (m_playing) {
        m_attempts = 0;
        resumeLocked(m_waiting ? m_base : heardLocked());
        return;
    }
    if (m_output && m_srcFormat.sampleRate > 0)
        m_output->close();
    m_srcFormat = m_outFormat = {0, 0};
}

void AudioBoxPlayer::resumeLocked(std::int64_t frame)
{
    const application::OutputFormat want = m_srcFormat;
    if (m_output)
        m_output->stop();
    if (m_reopen)
        releaseOutput();
    m_rescan = true;
    m_base = m_next = frame;
    m_written = 0;
    m_pending.clear();
    m_primed = false;
    m_phase = 0;
    m_started = false;
    const auto opened = openDevice(want);
    m_srcFormat = want;
    if (!opened) {
        m_waiting = true;
        if (++m_attempts >= m_maxAttempts) {
            halt();
            m_srcFormat = m_outFormat = {0, 0};
            fail(QStringLiteral("Audio player failed."));
            return;
        }
        if (!std::exchange(m_retryQueued, true))
            QTimer::singleShot(m_retryMs, this, [this] { retry(); });
        return;
    }
    m_outFormat = *opened;
    m_waiting = false;
    m_baseConsumed = output().status().framesConsumed;
    m_rangeFrames = static_cast<std::int64_t>(
        double(std::max<std::int64_t>(0, m_end - frame) * m_outFormat.sampleRate / std::max(1, m_srcFormat.sampleRate))
        / playSpeed());
    resetStretch();
    fill();
}

void AudioBoxPlayer::restartLocked(std::int64_t frame)
{
    output().stop();
    m_base = m_next = frame;
    m_written = 0;
    m_pending.clear();
    m_primed = false;
    m_phase = 0;
    m_started = false;
    m_baseConsumed = output().status().framesConsumed;
    m_rangeFrames = static_cast<std::int64_t>(
        double(std::max<std::int64_t>(0, m_end - frame) * m_outFormat.sampleRate / std::max(1, m_srcFormat.sampleRate))
        / playSpeed());
    resetStretch();
    fill();
}

void AudioBoxPlayer::resetStretch()
{
    if (playSpeed() >= 1.0 || m_srcFormat.sampleRate <= 0 || m_outFormat.channels <= 0) {
        m_stretch.reset();
        return;
    }
    if (!m_stretch)
        m_stretch = std::make_unique<soundtouch::SoundTouch>();
    m_stretch->clear();
    m_stretch->setSampleRate(static_cast<unsigned>(m_srcFormat.sampleRate));
    m_stretch->setChannels(static_cast<unsigned>(m_outFormat.channels));
    m_stretch->setTempo(playSpeed());
    m_stretch->setSetting(SETTING_SEQUENCE_MS, kStretchSequenceMs);
    m_stretch->setSetting(SETTING_SEEKWINDOW_MS, kStretchSeekWindowMs);
    m_stretch->setSetting(SETTING_OVERLAP_MS, kStretchOverlapMs);
}

void AudioBoxPlayer::stretch(std::vector<float> &frames, int channels)
{
    m_stretch->putSamples(frames.data(), static_cast<unsigned>(frames.size() / static_cast<std::size_t>(channels)));
    frames.resize(std::size_t{m_stretch->numSamples()} * static_cast<std::size_t>(channels));
    const unsigned got = frames.empty() ? 0 : m_stretch->receiveSamples(frames.data(), m_stretch->numSamples());
    frames.resize(std::size_t{got} * static_cast<std::size_t>(channels));
}

void AudioBoxPlayer::setSpeed(double speed)
{
    const std::scoped_lock lock(m_lock);
    speed = std::clamp(speed, 0.25, 1.0);
    if (speed == m_speed)
        return;
    if (!m_playing || m_waiting || !m_started || m_clipping) {
        const bool restart = m_playing && !m_waiting && !m_clipping;
        m_speed = speed;
        if (restart)
            restartLocked(m_base);
        return;
    }
    const std::int64_t frame = heardLocked();
    m_speed = speed;
    restartLocked(frame);
}

double AudioBoxPlayer::speed() const
{
    const std::scoped_lock lock(m_lock);
    return m_speed;
}

// Measured at 48 kHz with the kStretch* settings: the mean of 20 onsets per speed.
double AudioBoxPlayer::stretchLeadMs(double speed)
{
    static constexpr std::array<std::pair<double, double>, 5> kLead{
        {{0.25, 27.9}, {1.0 / 3.0, 22.7}, {0.5, 15.1}, {0.75, 6.8}, {1.0, 0.0}}};
    speed = std::clamp(speed, kLead.front().first, 1.0);
    for (std::size_t i = 1; i < kLead.size(); i++) {
        const auto [s0, l0] = kLead[i - 1];
        const auto [s1, l1] = kLead[i];
        if (speed <= s1)
            return l0 + (l1 - l0) * (speed - s0) / (s1 - s0);
    }
    return 0;
}

void AudioBoxPlayer::lostLocked()
{
    m_base = heardLocked();
    m_waiting = true;
    m_started = false;
    m_pending.clear();
    if (!std::exchange(m_retryQueued, true))
        QMetaObject::invokeMethod(this, [this] { retry(); }, Qt::QueuedConnection);
}

void AudioBoxPlayer::retry()
{
    const std::scoped_lock lock(m_lock);
    m_retryQueued = false;
    if (m_playing && m_waiting)
        resumeLocked(m_base);
}

void AudioBoxPlayer::setRecovery(int attempts, int intervalMs)
{
    const std::scoped_lock lock(m_lock);
    m_maxAttempts = attempts;
    m_retryMs = intervalMs;
}

void AudioBoxPlayer::reopenOutput()
{
    const std::scoped_lock lock(m_lock);
    if (m_playing)
        m_reopen = true;
    else
        releaseOutput();
}

void AudioBoxPlayer::releaseOutput()
{
    m_reopen = false;
    if (m_output && m_srcFormat.sampleRate > 0)
        m_output->close();
    m_output = nullptr;
    m_srcFormat = m_outFormat = {0, 0};
}

void AudioBoxPlayer::setEndPosition(std::int64_t end)
{
    const std::scoped_lock lock(m_lock);
    m_end = end;
    if (m_playing && m_end <= m_next)
        stopLocked();
}

std::int64_t AudioBoxPlayer::startPosition() const
{
    const std::scoped_lock lock(m_lock);
    return m_start;
}

std::int64_t AudioBoxPlayer::endPosition() const
{
    const std::scoped_lock lock(m_lock);
    return m_end;
}

void AudioBoxPlayer::setVolume(double volume)
{
    const std::scoped_lock lock(m_lock);
    m_volume = volume;
}

double AudioBoxPlayer::volume() const
{
    const std::scoped_lock lock(m_lock);
    return m_volume;
}

application::OutputFormat AudioBoxPlayer::outputFormat() const
{
    const std::scoped_lock lock(m_lock);
    return m_outFormat;
}

application::OutputFormat AudioBoxPlayer::sourceFormat() const
{
    const std::scoped_lock lock(m_lock);
    return m_srcFormat;
}

std::int64_t AudioBoxPlayer::nextFrame() const
{
    const std::scoped_lock lock(m_lock);
    return m_next;
}

std::int64_t AudioBoxPlayer::consumed() const
{
    return static_cast<std::int64_t>(m_output->status().framesConsumed - m_baseConsumed);
}

void AudioBoxPlayer::pump()
{
    const std::scoped_lock lock(m_lock);
    pumpLocked();
}

void AudioBoxPlayer::pumpLocked()
{
    if (!m_playing || m_waiting)
        return;
    if (output().status().deviceLost) {
        lostLocked();
        return;
    }
    if (m_attempts > 0 && m_started && consumed() > 0)
        m_attempts = 0;
    if (m_oneShot && m_started && consumed() >= m_rangeFrames) {
        stopLocked();
        return;
    }
    fill();
}

void AudioBoxPlayer::fill()
{
    const auto *audio = m_playingAudio.get();
    const int srcChannels = m_srcFormat.channels;
    const int outChannels = m_outFormat.channels;
    const bool resample = m_srcFormat.sampleRate != m_outFormat.sampleRate;
    const double step = double(m_srcFormat.sampleRate) / double(std::max(1, m_outFormat.sampleRate));
    const auto ahead = static_cast<std::int64_t>(kAheadSeconds * m_outFormat.sampleRate);
    auto &converted = m_converted;
    // Bounded per call: the null PCM has no clock and would keep this loop holding the lock.
    const std::int64_t writtenBefore = m_written;
    while (m_playing) {
        if (!m_pending.empty()) {
            const std::size_t accepted = output().write(m_pending);
            m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(accepted));
            m_written += static_cast<std::int64_t>(accepted) / outChannels;
            if (!m_pending.empty())
                break;
        }
        if (m_written - consumed() >= ahead || m_written - writtenBefore >= ahead)
            break;
        const std::int64_t n = kChunkFrames;
        m_frames.assign(static_cast<std::size_t>(n * srcChannels), 0);
        const std::int64_t inRange = std::clamp<std::int64_t>(m_end - m_next, 0, n);
        if (inRange > 0 && audio) {
            audio->readFrames(m_next, inRange, m_frames.data());
            application::applyLegacyVolume(m_frames.data(), inRange * srcChannels, m_volume);
        }
        m_next += n;
        auto &source = m_source;
        source.clear();
        source.reserve(static_cast<std::size_t>(n * outChannels));
        for (std::int64_t i = 0; i < n; i++)
            for (int c = 0; c < outChannels; c++)
                source.push_back(m_frames[static_cast<std::size_t>(i * srcChannels + std::min(c, srcChannels - 1))]
                                 / 32768.0f);
        if (m_stretch)
            stretch(source, outChannels);
        const auto frames = static_cast<std::int64_t>(source.size()) / outChannels;
        auto frameAt = [&](std::int64_t i, int c) { return source[static_cast<std::size_t>(i * outChannels + c)]; };
        converted.clear();
        if (!resample) {
            std::swap(converted, source);
        } else {
            for (std::int64_t i = 0; i < frames; i++) {
                if (!m_primed) {
                    m_previous.resize(static_cast<std::size_t>(outChannels));
                    for (int c = 0; c < outChannels; c++)
                        m_previous[static_cast<std::size_t>(c)] = frameAt(i, c);
                    m_primed = true;
                    continue;
                }
                while (m_phase < 1.0) {
                    for (int c = 0; c < outChannels; c++) {
                        const float a = m_previous[static_cast<std::size_t>(c)];
                        converted.push_back(a + (frameAt(i, c) - a) * static_cast<float>(m_phase));
                    }
                    m_phase += step;
                }
                m_phase -= 1.0;
                for (int c = 0; c < outChannels; c++)
                    m_previous[static_cast<std::size_t>(c)] = frameAt(i, c);
            }
        }
        m_pending.insert(m_pending.end(), converted.begin(), converted.end());
    }
    if (m_playing && !m_started && m_written > 0) {
        if (!output().start()) {
            if (++m_attempts >= m_maxAttempts) {
                halt();
                fail(QStringLiteral("Audio player failed."));
                return;
            }
            lostLocked();
            return;
        }
        m_started = true;
    }
}

std::int64_t AudioBoxPlayer::clockFrames(const application::ClockEstimate &clock) const
{
    const double rate = m_outFormat.sampleRate;
    double heard = (double(clock.framesConsumed) - double(m_baseConsumed))
                   + (m_now() - clock.monotonicSeconds - (clock.dacTimeSeconds - clock.streamTimeSeconds)) * rate;
    heard = std::clamp(heard, 0.0, double(std::max<std::int64_t>(0, consumed())));
    return static_cast<std::int64_t>(heard);
}

std::int64_t AudioBoxPlayer::position() const
{
    const std::scoped_lock lock(m_lock);
    if (!m_playing)
        return 0;
    if (!m_started || !m_output || m_outFormat.sampleRate <= 0)
        return m_base;
    const auto clock = m_output->clock();
    if (!clock.valid)
        return m_base;
    return m_base + sourceFrames(clockFrames(clock));
}

std::int64_t AudioBoxPlayer::sourceFrames(std::int64_t outputFrames) const
{
    if (!m_stretch)
        return outputFrames * m_srcFormat.sampleRate / m_outFormat.sampleRate;
    const double heard = double(outputFrames) * m_srcFormat.sampleRate / m_outFormat.sampleRate * m_speed;
    const double lead = stretchLeadMs(m_speed) * m_srcFormat.sampleRate / 1000.0;
    return static_cast<std::int64_t>(heard + std::min(heard, lead));
}

std::int64_t AudioBoxPlayer::heardLocked() const
{
    if (!m_started || !m_output || m_outFormat.sampleRate <= 0 || m_srcFormat.sampleRate <= 0)
        return m_base;
    const auto clock = m_output->clock();
    const std::int64_t frames = clock.valid ? clockFrames(clock) : std::max<std::int64_t>(0, consumed());
    return m_base + sourceFrames(frames);
}

}
