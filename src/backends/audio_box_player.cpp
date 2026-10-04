#include "hikari/backends/audio_box_player.h"

#include <algorithm>
#include <chrono>

namespace hikari::backends {

namespace {

double steadySeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

AudioBoxPlayer::AudioBoxPlayer(application::AudioOutputPort &output, Audio audio, QObject *parent)
    : AudioBoxPlayer([&output]() -> application::AudioOutputPort & { return output; }, std::move(audio), parent)
{
}

AudioBoxPlayer::AudioBoxPlayer(std::function<application::AudioOutputPort &()> output, Audio audio, QObject *parent)
    : QObject(parent), m_makeOutput(std::move(output)), m_audio(std::move(audio)), m_now(steadySeconds)
{
    m_timer.setInterval(5);
    connect(&m_timer, &QTimer::timeout, this, &AudioBoxPlayer::pump);
}

AudioBoxPlayer::~AudioBoxPlayer()
{
    if (m_playing)
        m_output->stop();
}

application::AudioOutputPort &AudioBoxPlayer::output()
{
    if (!m_output)
        m_output = &m_makeOutput();
    return *m_output;
}

void AudioBoxPlayer::close()
{
    stop();
    if (m_output && m_srcFormat.sampleRate > 0)
        m_output->close();
    m_srcFormat = m_outFormat = {0, 0};
}

// Legacy DirectSoundPlayer2::OpenStream: the stream follows the provider's
// format (rate, channels); legacy logged a player that could not be made.
bool AudioBoxPlayer::openOutput(const application::DisplayAudio &audio)
{
    const application::OutputFormat want{audio.sampleRate(), audio.channels()};
    const auto status = output().status();
    if (status.open && !status.deviceLost && want == m_srcFormat)
        return true;
    if (status.open)
        output().close();
    const auto opened = output().open(m_device, want);
    if (!opened) {
        m_srcFormat = m_outFormat = {0, 0};
        emit failed(QStringLiteral("Failed creating audio playback device."));
        return false;
    }
    m_srcFormat = want;
    m_outFormat = *opened;
    return true;
}

// Legacy DirectSoundPlayer2Thread::Play and its start event: the buffer is
// stopped and filled from the start again. A range shorter than the wanted
// latency (100 ms of bytes, integer arithmetic) plays once, without looping;
// an empty range loops on silence.
void AudioBoxPlayer::play(std::int64_t start, std::int64_t count)
{
    const auto *audio = m_audio ? m_audio() : nullptr;
    if (!audio || !openOutput(*audio))
        return;
    output().stop();
    m_start = m_next = start;
    m_end = start + count;
    m_written = 0;
    m_pending.clear();
    m_primed = false;
    m_phase = 0;
    m_baseConsumed = output().status().framesConsumed;
    const std::int64_t bytesPerFrame = std::int64_t{m_srcFormat.channels} * 2;
    const std::int64_t wantedBytes = kWantedLatencyMs * (m_srcFormat.sampleRate * bytesPerFrame) / 1000;
    m_oneShot = count > 0 && count * bytesPerFrame < wantedBytes;
    m_rangeFrames = count * m_outFormat.sampleRate / std::max(1, m_srcFormat.sampleRate);
    m_playing = true;
    m_started = false;
    fill();
    if (m_playing)
        m_timer.start();
}

// Legacy Stop: the buffer stops at once.
void AudioBoxPlayer::stop()
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
    m_timer.stop();
    m_pending.clear();
}

// Legacy SetEndFrame: stopped when the frames written already reach the new end.
void AudioBoxPlayer::setEndPosition(std::int64_t end)
{
    m_end = end;
    if (m_playing && m_end <= m_next)
        stop();
}

std::int64_t AudioBoxPlayer::consumed() const
{
    return static_cast<std::int64_t>(m_output->status().framesConsumed - m_baseConsumed);
}

void AudioBoxPlayer::pump()
{
    if (!m_playing)
        return;
    if (output().status().deviceLost) {
        halt();
        emit failed(QStringLiteral("Audio player failed."));
        return;
    }
    // Legacy's single-buffer playback stops once its time has passed.
    if (m_oneShot && m_started && consumed() >= m_rangeFrames) {
        stop();
        return;
    }
    fill();
}

// Legacy FillAndUnlockBuffers: the range's frames from the cache with the
// volume applied (GetPlaybackBuffer), silence from its end on.
void AudioBoxPlayer::fill()
{
    const auto *audio = m_audio ? m_audio() : nullptr;
    const int srcChannels = m_srcFormat.channels;
    const int outChannels = m_outFormat.channels;
    const bool resample = m_srcFormat.sampleRate != m_outFormat.sampleRate;
    const double step = double(m_srcFormat.sampleRate) / double(std::max(1, m_outFormat.sampleRate));
    const auto ahead = static_cast<std::int64_t>(kAheadSeconds * m_outFormat.sampleRate);
    std::vector<float> converted;
    while (m_playing) {
        if (!m_pending.empty()) {
            const std::size_t accepted = output().write(m_pending);
            m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(accepted));
            m_written += static_cast<std::int64_t>(accepted) / outChannels;
            if (!m_pending.empty())
                break;
        }
        if (m_written - consumed() >= ahead)
            break;
        const std::int64_t n = kChunkFrames;
        m_frames.assign(static_cast<std::size_t>(n * srcChannels), 0);
        const std::int64_t inRange = std::clamp<std::int64_t>(m_end - m_next, 0, n);
        if (inRange > 0 && audio) {
            audio->readFrames(m_next, inRange, m_frames.data());
            application::applyLegacyVolume(m_frames.data(), inRange * srcChannels, m_volume);
        }
        m_next += n;
        // to float, each output channel from the same source channel (the
        // last one past the source's)
        converted.clear();
        auto frameAt = [&](std::int64_t i, int c) {
            return m_frames[static_cast<std::size_t>(i * srcChannels + std::min(c, srcChannels - 1))] / 32768.0f;
        };
        if (!resample) {
            converted.reserve(static_cast<std::size_t>(n * outChannels));
            for (std::int64_t i = 0; i < n; i++)
                for (int c = 0; c < outChannels; c++)
                    converted.push_back(frameAt(i, c));
        } else {
            for (std::int64_t i = 0; i < n; i++) {
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
            halt();
            emit failed(QStringLiteral("Audio player failed."));
            return;
        }
        m_started = true;
    }
}

// Legacy GetCurrentPosition: 0 while stopped, the start until the output
// reports.
std::int64_t AudioBoxPlayer::position() const
{
    if (!m_playing)
        return 0;
    const auto clock = m_output->clock();
    if (!m_started || !clock.valid || m_outFormat.sampleRate <= 0)
        return m_start;
    const double rate = m_outFormat.sampleRate;
    double heard = (double(clock.framesConsumed) - double(m_baseConsumed))
                   + (m_now() - clock.monotonicSeconds - (clock.dacTimeSeconds - clock.streamTimeSeconds)) * rate;
    heard = std::clamp(heard, 0.0, double(std::max<std::int64_t>(0, consumed())));
    const auto frames = static_cast<std::int64_t>(heard);
    return m_start + frames * m_srcFormat.sampleRate / m_outFormat.sampleRate;
}

} // namespace hikari::backends
