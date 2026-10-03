#pragma once

// The audio box's audio (A1; legacy AudioDisplay::SetFile, Provider::Get and
// ProviderFFMS2's audio cache at 20d647c4). Opening first unloads what is
// open, as legacy does, so a failed open leaves no audio. A file is indexed
// and its first audio track read through the source port in legacy's decode
// format, block after block like legacy's disk cache (332768 frames), with
// the progress legacy draws; the waveform shows once every block is in.
// A name starting with "dummy" opens blank audio (legacy ProviderDummy):
// 2 h 30 min of 44.1 kHz mono silence, ready at once; "?dummy" (a dummy
// video's name) has no audio. Answers for an earlier open are dropped.

#include "hikari/application/audio_display.h"
#include "hikari/application/indexed_source.h"

#include <functional>
#include <optional>
#include <string>

namespace hikari::application {

class AudioBox {
public:
    enum class State { Closed, Opening, Loading, Ready };
    static constexpr std::int64_t kBlockFrames = 332768;      // legacy DiskCache block
    static constexpr int kDummyRate = 44100;                    // legacy ParseDummyData
    static constexpr std::int64_t kDummySamples = 396'900'000;  // 5 * 30 * 60 * 1000 * 44100 / 1000
    // GLOBAL_OPEN_DUMMY_AUDIO's name.
    static constexpr char kDummyName[] = "dummy-audio:silence?sr=44100&bd=16&ch=1&ln=396900000";

    explicit AudioBox(IndexedSourcePort &source) : m_source(source) {}

    void setObserver(std::function<void()> changed) { m_observer = std::move(changed); }
    void open(const std::string &path);
    void close();

    State state() const { return m_state; }
    // Legacy ABox: open, loading or ready.
    bool isOpen() const { return m_state != State::Closed; }
    const std::string &path() const { return m_path; }
    // Why the last open ended without audio; nullopt when it did not fail.
    std::optional<SourceError> error() const { return m_error; }
    // Indexing (Opening): FFMS2's done and total; decoding (Loading): 0..1.
    std::pair<std::int64_t, std::int64_t> indexing() const { return m_indexing; }
    float progress() const;
    const DisplayAudio *audio() const { return m_audio ? &*m_audio : nullptr; }
    // Changes with every open and close, so a view can tell its audio apart.
    std::uint64_t serial() const { return m_request; }

private:
    void notify();
    void fail(SourceError error);
    void readNext(std::uint64_t request);

    IndexedSourcePort &m_source;
    std::function<void()> m_observer;
    State m_state = State::Closed;
    std::string m_path;
    std::optional<SourceError> m_error;
    std::pair<std::int64_t, std::int64_t> m_indexing;
    std::optional<DisplayAudio> m_audio;
    int m_channels = 0;
    std::uint64_t m_request = 0; // the newest open; older answers are dropped
};

} // namespace hikari::application
