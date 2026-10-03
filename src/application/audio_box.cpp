#include "hikari/application/audio_box.h"

#include <algorithm>

namespace hikari::application {

void AudioBox::notify()
{
    if (m_observer)
        m_observer();
}

void AudioBox::open(const std::string &path)
{
    close(); // legacy SetFile unloads before it loads
    const std::uint64_t request = ++m_request;
    m_path = path;
    m_error.reset();
    // legacy Provider::Get
    if (path.starts_with("dummy")) {
        m_audio = DisplayAudio::silence(kDummyRate, kDummySamples);
        m_state = State::Ready;
        return notify();
    }
    if (path.starts_with("?dummy"))
        return fail(SourceError::Unsupported); // a dummy video has no audio
    m_state = State::Opening;
    m_indexing = {};
    notify();
    m_source.openDisplayAudio(
        path,
        [this, request](std::int64_t done, std::int64_t total) {
            if (request != m_request)
                return;
            m_indexing = {done, total};
            notify();
        },
        [this, request](std::expected<AudioInfo, SourceError> info) {
            if (request != m_request)
                return;
            if (!info)
                return fail(info.error());
            if (info->format != SampleFormat::S16 || info->channels < 1 || info->sampleRate <= 0)
                return fail(SourceError::BackendFailure);
            m_channels = info->channels;
            m_audio.emplace(info->sampleRate, info->sampleCount);
            m_state = State::Loading;
            notify();
            readNext(request);
        });
}

void AudioBox::readNext(std::uint64_t request)
{
    const std::int64_t at = m_audio->decoded();
    if (at >= m_audio->sampleCount()) {
        m_audio->finish();
        m_state = State::Ready;
        return notify();
    }
    m_source.audio(at, std::min(kBlockFrames, m_audio->sampleCount() - at),
                   [this, request](std::expected<AudioBlock, SourceError> block) {
                       if (request != m_request)
                           return;
                       if (!block)
                           return fail(block.error());
                       if (block->count <= 0 || block->channels != m_channels)
                           return fail(SourceError::BackendFailure);
                       m_audio->appendFrames(reinterpret_cast<const std::int16_t *>(block->samples.data()),
                                             block->count, block->channels);
                       notify();
                       readNext(request);
                   });
}

// The failed request has ended: nothing to cancel. The path stays for the message.
void AudioBox::fail(SourceError error)
{
    ++m_request;
    m_state = State::Closed;
    m_audio.reset();
    m_indexing = {};
    m_channels = 0;
    m_error = error;
    notify();
}

void AudioBox::close()
{
    const bool wasOpen = m_state != State::Closed;
    if (m_state == State::Opening)
        m_source.cancelOpen();
    if (m_state == State::Loading)
        m_source.cancelReads();
    ++m_request;
    m_state = State::Closed;
    m_audio.reset();
    m_path.clear();
    m_indexing = {};
    m_channels = 0;
    if (wasOpen)
        notify();
}

float AudioBox::progress() const
{
    if (!m_audio || m_audio->sampleCount() <= 0)
        return m_state == State::Ready ? 1.f : 0.f;
    // legacy DiskCache: the frames read over the frames to read
    return static_cast<float>(m_audio->decoded()) / static_cast<float>(m_audio->sampleCount());
}

} // namespace hikari::application
