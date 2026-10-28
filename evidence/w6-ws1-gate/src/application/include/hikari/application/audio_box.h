#pragma once

// The audio box's audio (A1; legacy AudioDisplay::SetFile, Provider::Get and
// ProviderFFMS2 at 20d647c4). Opening first unloads what is open, as legacy
// does, so a failed open leaves no audio. The file's tracks are listed; with
// several audio tracks one is taken by ACCEPTED_AUDIO_STREAM's languages or
// chosen by the user ("Choose the track"; cancelling ends the open). The
// track is then opened through the box's own source in legacy's decode
// format, with legacy's index file (Indices/<name>_<track>.ffindex), and
// cached the way legacy caches it: in a disk cache file by default, block
// after block of 332768 frames (DiskCache), or in RAM with AUDIO_RAM_CACHE
// in blocks of 4 MiB (RAMCache), with AUDIO_DELAY's silence or skip and the
// progress legacy draws; the waveform shows once every block is in. A disk
// cache file is read again instead of decoding when the index was read from
// its file and was not made now (legacy DiskCache(newIndex)).
//
// The open video's audio is the video's track (the one the video's open
// chose), opened from the index file the video's open wrote, so nothing is
// indexed twice and the video's helper never waits for the box. When that
// file could not be written, the video's helper handed the index over in a
// temporary file, which the box reads instead (legacy's box shared the
// video's index in memory). Its cache is made again when the video's index
// was new.
//
// A name starting with "dummy" opens blank audio (legacy ProviderDummy):
// 2 h 30 min of 44.1 kHz mono silence, ready at once; "?dummy" (a dummy
// video's name) has no audio. Answers for an earlier open are dropped.
//
// Blocks are read as legacy ProviderFFMS2::GetAudio read them into its
// caches' buffers (BlockRead): frames past legacy's sample count (the audio
// less a negative delay's skipped start) are not decoded, and its "fill
// beyond with zero" clears samples, not frames. A disk cache reads into the
// block buffer the source keeps (DiskCache's reused `data`), a RAM cache into
// a new one. A block FFMS2 fails to decode is cached as legacy cached it: what
// the buffer holds, the frames decoded before the failure followed by the
// previous block's (zeros before the first block), and caching goes on.
//
// Approved departures (R3-hang-crash-loss, extended to A1): in a RAM cache
// the part of a block FFMS2 did not write is zeros (legacy's new block was
// uninitialised memory); a cache that cannot be allocated or written, or a
// media helper that ends, ends the open with a message (legacy crashed, drew
// an empty progress bar forever or cached nothing).

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
    std::filesystem::path indexDir;           // <config>/Indices (empty: no index files)
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
// Legacy ProviderFFMS2::Init's index file name: <name>_<track>.ffindex (track
// -1 without audio).
std::string legacyIndexName(const std::string &path, int track);

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
    // Runs `step` later (the UI's event loop), so reading back a long cache
    // file does not hold the caller; unset, it runs at once.
    void setDefer(std::function<void(std::function<void()>)> defer) { m_defer = std::move(defer); }

    // A file of the box's own (GLOBAL_OPEN_AUDIO, recent audio, Open audio from video).
    void open(const std::string &path);
    // The open video's audio: the track the video's open chose, with the
    // index file it wrote (`videoNewIndex`: the video indexed it now), or
    // `handoffIndexFile` when it handed its index over in a temporary file.
    void openFromVideo(const std::string &path, int track, bool videoNewIndex,
                       const std::string &handoffIndexFile = {});
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
    const DisplayAudio *audio() const { return m_audio.get(); }
    // The same audio, kept alive by its holder after the box lets it go (A4:
    // the player's fill thread reads it until it stops).
    std::shared_ptr<const DisplayAudio> sharedAudio() const { return m_audio; }
    int track() const { return m_track; }
    std::int64_t delayFrames() const { return m_delay; }
    // The disk cache file in use (empty with a RAM cache): written as
    // cacheFile().part until complete, or read back as it is.
    const std::filesystem::path &cacheFile() const { return m_cacheFile; }
    // The cache file was read back instead of decoded.
    bool cacheReused() const { return m_reused; }
    // Changes with every open and close, so a view can tell its audio apart.
    std::uint64_t serial() const { return m_request; }

private:
    void start(std::uint64_t request);
    void choose(std::uint64_t request, const MediaProbe &probe);
    void openTrack(std::uint64_t request, int track);
    void opened(std::uint64_t request, const AudioInfo &info, bool newIndex);
    void scanNext(std::uint64_t request);
    void ready();
    void readNext(std::uint64_t request);
    void readSource(std::uint64_t request, std::int64_t start, std::int64_t count);
    void notify();
    void fail(SourceError error);
    void log(const std::string &message, LogLevel level);
    template <typename F> auto guard(std::uint64_t request, F f);

    DisplayAudioPort &m_own;
    std::function<void()> m_observer;
    Log m_log;
    Chooser m_chooser;
    std::function<AudioCacheSettings()> m_settings;
    std::function<void(std::function<void()>)> m_defer;
    State m_state = State::Closed;
    std::string m_path;
    bool m_fromVideo = false;
    bool m_videoNewIndex = false;
    std::string m_videoIndexFile; // the video's handed-over index, if any
    bool m_reused = false;
    bool m_choosing = false;
    bool m_declined = false;
    std::optional<SourceError> m_error;
    std::pair<std::int64_t, std::int64_t> m_indexing;
    std::shared_ptr<DisplayAudio> m_audio;
    std::filesystem::path m_cacheFile;
    int m_channels = 0;
    int m_track = -1;
    std::int64_t m_delay = 0;
    // the cache plan (legacy DiskCache / RAMCache)
    bool m_ram = false;
    std::int64_t m_sourceFrame = 0, m_sourceEnd = 0; // the source frames to read
    std::int64_t m_decodeEnd = 0;                    // legacy m_numSamples: no frame past it is decoded
    std::int64_t m_written = 0;                      // RAM: frames of the cache filled
    std::int64_t m_silence = 0;                      // RAM: a positive delay's leading frames
    int m_block = 0, m_blocks = 0;                   // RAM: the current block, blocks in all
    float m_progress = 0;
    std::uint64_t m_request = 0; // the newest open; older answers are dropped
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace hikari::application
