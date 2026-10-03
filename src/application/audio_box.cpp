#include "hikari/application/audio_box.h"

#include <algorithm>
#include <cmath>

namespace hikari::application {

namespace {

// wxString::IsSameAs(other, false) for the language codes legacy compares.
bool sameIgnoringCase(const std::string &a, const std::string &b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        return lower(x) == lower(y);
    });
}

// Characters (not bytes) before `at`: wxString positions count characters.
std::size_t charactersBefore(const std::string &text, std::size_t at)
{
    std::size_t n = 0;
    for (std::size_t i = 0; i < at && i < text.size(); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
            ++n;
    return n;
}

} // namespace

AudioTrackChoice legacyAudioTrackChoice(const std::vector<AudioTrack> &tracks, const std::vector<std::string> &accepted)
{
    AudioTrackChoice choice;
    if (tracks.empty())
        return choice;
    if (tracks.size() == 1) {
        choice.track = tracks.front().index;
        return choice;
    }
    const std::size_t enabledSize = accepted.size();
    std::size_t lowestIndex = enabledSize;
    for (const auto &t : tracks) {
        std::string name = t.hasName ? t.name : std::string();
        std::string language = t.hasLanguage ? t.language : std::string();
        if (t.hasLanguage) {
            const auto startBracket = language.rfind('[');
            const auto endBracket = language.rfind(']');
            if (startBracket != std::string::npos && endBracket != std::string::npos) {
                if (name.empty() && charactersBefore(language, startBracket) > 1)
                    name = language.substr(0, startBracket);
                // wxString::Mid with a negative length reads to the end, as substr does
                language = language.substr(startBracket + 1, endBracket - (startBracket + 1));
            }
            if (enabledSize) {
                const auto found = std::find_if(accepted.begin(), accepted.end(),
                                                [&](const std::string &a) { return sameIgnoringCase(a, language); });
                const auto index = static_cast<std::size_t>(found - accepted.begin());
                if (found != accepted.end() && index < lowestIndex) {
                    lowestIndex = index;
                    choice.track = t.index;
                    continue;
                }
            }
        }
        std::string description;
        if (t.hasName)
            description = name;
        if (t.hasLanguage) {
            if (t.hasName)
                description += " [";
            description += language;
            if (t.hasName)
                description += "]";
        }
        if (description.empty())
            description = "Untitled";
        choice.rows.push_back(std::to_string(t.index) + ": " + description + " (" + t.codec + ")");
        choice.rowTracks.push_back(t.index);
    }
    if (lowestIndex < enabledSize) {
        choice.rows.clear();
        choice.rowTracks.clear();
    } else {
        choice.track.reset();
    }
    return choice;
}

std::vector<std::string> legacyAcceptedStreams(const std::string &value)
{
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= value.size()) {
        const auto at = value.find(';', from);
        const auto piece = value.substr(from, at == std::string::npos ? std::string::npos : at - from);
        if (!piece.empty())
            out.push_back(piece);
        if (at == std::string::npos)
            break;
        from = at + 1;
    }
    return out;
}

std::string legacyAudioCacheName(const std::string &path, int track, int channels, std::int64_t delayFrames)
{
    // wxFileName::GetName: the name without folder or extension
    const auto slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (const auto dot = name.rfind('.'); dot != std::string::npos && dot > 0)
        name.resize(dot);
    return name + "_track" + std::to_string(track) + "_" + std::to_string(channels) + "ch_" +
           std::to_string(delayFrames) + ".w64";
}

AudioBox::AudioBox(DisplayAudioPort &own) : m_own(own) {}

AudioBox::~AudioBox()
{
    m_alive.reset(); // answers resolved below reach nothing
    if (m_reader && (m_state == State::Opening || m_state == State::Loading))
        m_reader->cancelDisplay();
}

template <typename F> auto AudioBox::guard(std::uint64_t request, F f)
{
    std::weak_ptr<bool> alive = m_alive;
    return [this, alive, request, f = std::move(f)](auto &&...args) mutable {
        if (alive.expired() || request != m_request)
            return; // the box is gone, or a later open or close replaced this one
        f(std::forward<decltype(args)>(args)...);
    };
}

void AudioBox::notify()
{
    if (m_observer)
        m_observer();
}

void AudioBox::log(const std::string &message, LogLevel level)
{
    if (m_log)
        m_log(message, level);
}

void AudioBox::open(const std::string &path)
{
    close(); // legacy SetFile unloads before it loads
    const std::uint64_t request = ++m_request;
    m_path = path;
    m_error.reset();
    m_declined = false;
    m_fromVideo = false;
    // legacy Provider::Get
    if (path.starts_with("dummy")) {
        m_audio = DisplayAudio::silence(kDummyRate, kDummySamples);
        m_progress = 1.f;
        m_state = State::Ready;
        return notify();
    }
    if (path.starts_with("?dummy"))
        return fail(SourceError::Unsupported); // a dummy video has no audio
    m_reader = &m_own;
    start(request);
}

void AudioBox::openFromVideo(DisplayAudioPort &video, const std::string &path)
{
    close();
    const std::uint64_t request = ++m_request;
    m_path = path;
    m_error.reset();
    m_declined = false;
    m_fromVideo = true;
    m_reader = &video;
    start(request);
}

void AudioBox::start(std::uint64_t request)
{
    m_state = State::Opening;
    m_indexing = {};
    m_progress = 0;
    notify();
    m_reader->probe(m_path, guard(request, [this, request](std::expected<MediaProbe, AudioFailure> probe) {
        if (!probe) {
            // legacy: FFMS_CreateIndexer's failure is a debug message only
            if (probe.error().stage == AudioStage::Indexer)
                log("Indexing error occurred: " + probe.error().message, LogLevel::Debug);
            else if (probe.error().stage == AudioStage::Host)
                log("Cannot open audio " + m_path, LogLevel::Shown);
            return fail(probe.error().error);
        }
        choose(request, *probe);
    }));
}

void AudioBox::choose(std::uint64_t request, const MediaProbe &probe)
{
    if (probe.audio.empty())
        return fail(SourceError::Unsupported); // legacy GetSampleRate() < 0: nothing said
    const auto accepted = m_settings ? m_settings().acceptedStreams : std::vector<std::string>{};
    const auto choice = legacyAudioTrackChoice(probe.audio, accepted);
    if (choice.track)
        return openTrack(request, *choice.track);
    if (!m_chooser)
        return openTrack(request, choice.rowTracks.front()); // no one to ask: the first row, legacy's preselection
    m_choosing = true;
    notify();
    m_chooser(choice.rows, guard(request, [this, request, tracks = choice.rowTracks](std::optional<int> row) {
        m_choosing = false;
        if (!row || *row < 0 || *row >= static_cast<int>(tracks.size())) {
            m_declined = true;
            return fail(SourceError::Cancelled);
        } // legacy: FFMS_CancelIndexing, no audio, nothing said
        openTrack(request, tracks[static_cast<std::size_t>(*row)]);
    }));
}

void AudioBox::openTrack(std::uint64_t request, int track)
{
    m_track = track;
    auto done = guard(request, [this, request](std::expected<AudioInfo, AudioFailure> info) {
        if (!info) {
            const auto &f = info.error();
            switch (f.stage) {
            case AudioStage::Indexer: log("Indexing error occurred: " + f.message, LogLevel::Debug); break;
            case AudioStage::Indexing:
                if (f.error != SourceError::Cancelled) // legacy does not report a cancel
                    log("Indexing error occurred: " + f.message, LogLevel::Shown);
                break;
            case AudioStage::Source: log("An error occurred when creating audio source: " + f.message, LogLevel::Shown); break;
            case AudioStage::Convert: log("An error occurred when converting audio: " + f.message, LogLevel::Shown); break;
            case AudioStage::Read: break;
            case AudioStage::Host:
                if (f.error == SourceError::HelperLost || f.error == SourceError::MissingDependency ||
                    f.error == SourceError::Busy || f.error == SourceError::BackendFailure)
                    log("Cannot open audio " + m_path, LogLevel::Shown);
                break;
            }
            return fail(f.error);
        }
        opened(request, *info);
    });
    if (m_fromVideo)
        return m_reader->openSourceDisplayAudio(track, std::move(done));
    m_reader->openDisplayAudio(m_path, track, guard(request, [this](std::int64_t indexed, std::int64_t total) {
                                   m_indexing = {indexed, total};
                                   notify();
                               }),
                               std::move(done));
}

void AudioBox::opened(std::uint64_t request, const AudioInfo &info)
{
    if (info.format != SampleFormat::S16 || info.channels < 1 || info.channels > 2 || info.sampleRate <= 0) {
        log("Cannot open audio " + m_path, LogLevel::Shown);
        return fail(SourceError::BackendFailure);
    }
    const AudioCacheSettings settings = m_settings ? m_settings() : AudioCacheSettings{};
    m_channels = info.channels;
    // legacy Init: the delay in frames, dropped when it is longer than the audio
    m_delay = std::llround(info.sampleRate * (settings.delayMs / 1000.0));
    if (std::llabs(m_delay) >= info.sampleCount) {
        if (m_delay != 0)
            log("Delay failed, it's longer than audio duration time", LogLevel::Shown);
        m_delay = 0;
    }
    const std::int64_t sampleCount = info.sampleCount - std::max<std::int64_t>(0, -m_delay);
    m_ram = settings.ram;
    std::unique_ptr<AudioStore> store;
    m_cacheFile.clear();
    if (m_ram) {
        store = ramAudioStore(m_channels);
    } else {
        m_cacheFile = settings.cacheDir / legacyAudioCacheName(m_path, m_track, m_channels, m_delay);
        std::string error;
        store = diskAudioStore(m_cacheFile, m_channels, &error);
        if (!store) {
            log("Cannot create the audio cache " + error, LogLevel::Shown);
            m_cacheFile.clear();
            return fail(SourceError::BackendFailure);
        }
    }
    m_audio.emplace(info.sampleRate, sampleCount, std::move(store));
    // a positive delay starts with silence, a negative one skips the start
    m_sourceFrame = std::max<std::int64_t>(0, -m_delay);
    m_sourceEnd = sampleCount + m_sourceFrame;
    m_silence = std::max<std::int64_t>(0, m_delay);
    m_written = 0;
    m_block = 0;
    m_blocks = static_cast<int>(sampleCount * 2 * m_channels / kRamBlockBytes) + 1;
    m_progress = 0; // legacy m_audioProgress = 0
    if (!m_ram && m_silence > 0 && !m_audio->appendSilence(m_silence, m_channels)) {
        log("Cannot write the audio cache " + m_cacheFile.string(), LogLevel::Shown);
        return fail(SourceError::BackendFailure);
    }
    m_state = State::Loading;
    notify();
    readNext(request);
}

void AudioBox::readNext(std::uint64_t request)
{
    if (m_ram) {
        // legacy RAMCache: 4 MiB blocks over the whole audio, the delay's
        // silence counted within them
        if (m_block >= m_blocks) {
            m_audio->finish();
            m_progress = 1.f;
            m_state = State::Ready;
            return notify();
        }
        const std::int64_t frameBytes = 2 * m_channels;
        const std::int64_t end = m_audio->sampleCount() * frameBytes;
        const std::int64_t size = std::min(kRamBlockBytes, end - m_written * frameBytes);
        const std::int64_t frames = size / frameBytes;
        const std::int64_t silent = std::clamp<std::int64_t>(m_silence - m_written, 0, frames);
        if (silent > 0 && !m_audio->appendSilence(silent, m_channels)) {
            log("Not enough memory to cache the audio", LogLevel::Shown);
            return fail(SourceError::BackendFailure);
        }
        m_written += silent;
        if (frames > silent)
            return readSource(request, m_sourceFrame, frames - silent);
        m_progress = m_blocks > 1 ? static_cast<float>(m_block) / static_cast<float>(m_blocks - 1) : 1.f;
        ++m_block;
        notify();
        return readNext(request);
    }
    // legacy DiskCache: blocks of 332768 source frames
    if (m_sourceFrame >= m_sourceEnd) {
        m_audio->finish();
        m_progress = 1.f;
        m_state = State::Ready;
        notify();
        if (m_cached)
            m_cached(m_cacheFile);
        return;
    }
    readSource(request, m_sourceFrame, std::min(kBlockFrames, m_sourceEnd - m_sourceFrame));
}

void AudioBox::readSource(std::uint64_t request, std::int64_t start, std::int64_t count)
{
    m_reader->displayAudio(start, count, guard(request, [this, request, count](std::expected<AudioBlock, AudioFailure> block) {
        bool kept = false;
        if (block) {
            if (block->channels != m_channels || block->count < 0 || block->count > count ||
                block->samples.size() < static_cast<std::size_t>(block->count * m_channels) * 2) {
                log("Cannot open audio " + m_path, LogLevel::Shown);
                return fail(SourceError::BackendFailure);
            }
            // legacy GetAudio: past the end is silence
            kept = m_audio->appendFrames(reinterpret_cast<const std::int16_t *>(block->samples.data()), block->count,
                                         m_channels) &&
                   m_audio->appendSilence(count - block->count, m_channels);
        } else if (block.error().stage == AudioStage::Read || block.error().error == SourceError::EndOfStream) {
            // legacy logged FFMS_GetAudio's error for debugging and kept caching;
            // the block is silence (R3: legacy kept whatever the buffer held)
            log("error audio" + block.error().message, LogLevel::Debug);
            kept = m_audio->appendSilence(count, m_channels);
        } else {
            if (block.error().error == SourceError::HelperLost || block.error().error == SourceError::Busy ||
                block.error().error == SourceError::BackendFailure)
                log("Cannot open audio " + m_path, LogLevel::Shown);
            return fail(block.error().error); // the source is gone (closed, replaced or lost)
        }
        if (!kept) {
            log(m_ram ? std::string("Not enough memory to cache the audio")
                      : "Cannot write the audio cache " + m_cacheFile.string(),
                LogLevel::Shown);
            return fail(SourceError::BackendFailure);
        }
        m_sourceFrame += count;
        if (m_ram) {
            m_written += count;
            m_progress = m_blocks > 1 ? static_cast<float>(m_block) / static_cast<float>(m_blocks - 1) : 1.f;
            ++m_block;
        } else {
            m_progress = static_cast<float>(m_sourceFrame) / static_cast<float>(m_sourceEnd);
        }
        notify();
        readNext(request);
    }));
}

// The failed request has ended: nothing to cancel. The path stays for the message.
void AudioBox::fail(SourceError error)
{
    ++m_request;
    m_state = State::Closed;
    m_audio.reset();
    m_cacheFile.clear();
    m_indexing = {};
    m_progress = 0;
    m_channels = 0;
    m_choosing = false;
    m_reader = nullptr;
    m_error = error;
    notify();
}

void AudioBox::close()
{
    const bool wasOpen = m_state != State::Closed;
    if (m_reader && (m_state == State::Opening || m_state == State::Loading)) {
        ++m_request; // the cancelled answers are dropped
        m_reader->cancelDisplay();
    }
    ++m_request;
    m_state = State::Closed;
    m_audio.reset(); // a complete disk cache keeps its name, an incomplete one is removed
    m_cacheFile.clear();
    m_path.clear();
    m_fromVideo = false;
    m_choosing = false;
    m_indexing = {};
    m_progress = 0;
    m_channels = 0;
    m_track = -1;
    m_reader = nullptr;
    if (wasOpen)
        notify();
}

} // namespace hikari::application
