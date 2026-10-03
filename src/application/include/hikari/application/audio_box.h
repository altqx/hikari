#pragma once

// The audio box's audio (A1; legacy AudioDisplay::SetFile, Provider::Get and
// ProviderFFMS2 at 20d647c4). Opening first unloads what is open, as legacy
// does, so a failed open leaves no audio. The file's tracks are listed; with
// several audio tracks one is taken by ACCEPTED_AUDIO_STREAM's languages or
// chosen by the user ("Choose the track"; cancelling ends the open). The
// track is then opened through the source in legacy's decode format and
// cached the way legacy caches it: in a disk cache file by default, block
// after block of 332768 frames (DiskCache), or in RAM with AUDIO_RAM_CACHE
// in blocks of 4 MiB (RAMCache), with AUDIO_DELAY's silence or skip and the
// progress legacy draws; the waveform shows once every block is in.
//
// A file of the box's own is indexed through `own`; the open video's audio is
// read through the video's source (legacy reused the video's provider), so
// a file is never indexed twice. Once cached, the audio needs neither.
//
// A name starting with "dummy" opens blank audio (legacy ProviderDummy):
// 2 h 30 min of 44.1 kHz mono silence, ready at once; "?dummy" (a dummy
// video's name) has no audio. Answers for an earlier open are dropped.
//
// Approved departures (R3-hang-crash-loss): a block FFMS2 fails to decode is
// silence (legacy cached whatever the buffer held) and caching goes on; a
// cache that cannot be allocated or written, or a media helper that ends,
// ends the open with a message (legacy crashed or read garbage).

#include "hikari/application/audio_display.h"
#include "hikari/application/display_audio_port.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

// The options legacy read when a provider opened (interim INI keys until the
// settings registry).
struct AudioCacheSettings {
    bool ram = false;                         // AUDIO_RAM_CACHE (default false)
    int delayMs = 0;                          // AUDIO_DELAY
    int cacheFilesLimit = 10;                 // AUDIO_CACHE_FILES_LIMIT
    std::filesystem::path cacheDir;           // <config>/AudioCache
    std::vector<std::string> acceptedStreams; // ACCEPTED_AUDIO_STREAM, split on ';'
};

// Legacy ProviderFFMS2::Init's track choice for several audio tracks: the
// track whose language (the text in its last [...] when there is one) comes
// first in `accepted` (case-insensitively), else the chooser's rows
// ("<track>: <name> [<language>] (<codec>)", "Untitled" without either).
// One track is taken as it is; none gives neither.
struct AudioTrackChoice {
    std::optional<int> track;
    std::vector<std::string> rows; // the chooser's, when it must ask
    std::vector<int> rowTracks;    // each row's track
};
AudioTrackChoice legacyAudioTrackChoice(const std::vector<AudioTrack> &tracks, const std::vector<std::string> &accepted);
// ACCEPTED_AUDIO_STREAM's value split as legacy GetTableFromString(";") does
// (empty pieces skipped).
std::vector<std::string> legacyAcceptedStreams(const std::string &value);
// Legacy AudioLoad's cache file name: <name>_track<n>_<c>ch_<delay>.w64.
std::string legacyAudioCacheName(const std::string &path, int track, int channels, std::int64_t delayFrames);

class AudioBox {
public:
    enum class State { Closed, Opening, Loading, Ready };
    static constexpr std::int64_t kBlockFrames = 332768;        // legacy DiskCache block
    static constexpr std::int64_t kRamBlockBytes = 1 << 22;     // legacy RAMCache block
    static constexpr int kDummyRate = 44100;                    // legacy ParseDummyData
    static constexpr std::int64_t kDummySamples = 396'900'000;  // 5 * 30 * 60 * 1000 * 44100 / 1000
    // GLOBAL_OPEN_DUMMY_AUDIO's name.
    static constexpr char kDummyName[] = "dummy-audio:silence?sr=44100&bd=16&ch=1&ln=396900000";

    // HikariLog, or HikariLogDebug (shown only in legacy debug builds).
    enum class LogLevel { Shown, Debug };
    using Log = std::function<void(const std::string &message, LogLevel level)>;
    // Shows the chooser's rows; answers the chosen row, or nothing on Cancel.
    using Chooser = std::function<void(const std::vector<std::string> &rows, std::function<void(std::optional<int>)> answer)>;

    explicit AudioBox(DisplayAudioPort &own);
    ~AudioBox();
    AudioBox(const AudioBox &) = delete;
    AudioBox &operator=(const AudioBox &) = delete;

    void setObserver(std::function<void()> changed) { m_observer = std::move(changed); }
    void setLog(Log log) { m_log = std::move(log); }
    void setChooser(Chooser chooser) { m_chooser = std::move(chooser); }
    void setSettings(std::function<AudioCacheSettings()> settings) { m_settings = std::move(settings); }
    // A disk cache was filled (legacy then trims old caches, DeleteOldAudioCache).
    void setCached(std::function<void(const std::filesystem::path &)> cached) { m_cached = std::move(cached); }

    // A file of the box's own (GLOBAL_OPEN_AUDIO, recent audio, Open audio from video).
    void open(const std::string &path);
    // The open video's audio, read through the video's source.
    void openFromVideo(DisplayAudioPort &video, const std::string &path);
    void close();

    State state() const { return m_state; }
    // Legacy ABox: open, loading or ready.
    bool isOpen() const { return m_state != State::Closed; }
    const std::string &path() const { return m_path; }
    bool fromVideo() const { return m_fromVideo; }
    // The chooser is waiting for an answer.
    bool choosing() const { return m_choosing; }
    // The last open ended because the chooser was cancelled.
    bool declined() const { return m_declined; }
    // Why the last open ended without audio; nullopt when it did not fail.
    std::optional<SourceError> error() const { return m_error; }
    // Indexing (Opening): FFMS2's done and total.
    std::pair<std::int64_t, std::int64_t> indexing() const { return m_indexing; }
    float progress() const { return m_progress; } // decoding (Loading): legacy m_audioProgress
    const DisplayAudio *audio() const { return m_audio ? &*m_audio : nullptr; }
    int track() const { return m_track; }
    std::int64_t delayFrames() const { return m_delay; }
    // The disk cache file being filled (empty with a RAM cache).
    const std::filesystem::path &cacheFile() const { return m_cacheFile; }
    // Changes with every open and close, so a view can tell its audio apart.
    std::uint64_t serial() const { return m_request; }

private:
    void start(std::uint64_t request);
    void choose(std::uint64_t request, const MediaProbe &probe);
    void openTrack(std::uint64_t request, int track);
    void opened(std::uint64_t request, const AudioInfo &info);
    void readNext(std::uint64_t request);
    void readSource(std::uint64_t request, std::int64_t start, std::int64_t count);
    void notify();
    void fail(SourceError error);
    void log(const std::string &message, LogLevel level);
    template <typename F> auto guard(std::uint64_t request, F f);

    DisplayAudioPort &m_own;
    DisplayAudioPort *m_reader = nullptr; // the source of the open in progress
    std::function<void()> m_observer;
    Log m_log;
    Chooser m_chooser;
    std::function<AudioCacheSettings()> m_settings;
    std::function<void(const std::filesystem::path &)> m_cached;
    State m_state = State::Closed;
    std::string m_path;
    bool m_fromVideo = false;
    bool m_choosing = false;
    bool m_declined = false;
    std::optional<SourceError> m_error;
    std::pair<std::int64_t, std::int64_t> m_indexing;
    std::optional<DisplayAudio> m_audio;
    std::filesystem::path m_cacheFile;
    int m_channels = 0;
    int m_track = -1;
    std::int64_t m_delay = 0;
    // the cache plan (legacy DiskCache / RAMCache)
    bool m_ram = false;
    std::int64_t m_sourceFrame = 0, m_sourceEnd = 0; // the source frames to read
    std::int64_t m_written = 0;                      // RAM: frames of the cache filled
    std::int64_t m_silence = 0;                      // RAM: a positive delay's leading frames
    int m_block = 0, m_blocks = 0;                   // RAM: the current block, blocks in all
    float m_progress = 0;
    std::uint64_t m_request = 0; // the newest open; older answers are dropped
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace hikari::application
