#pragma once

#include "hikari/application/audio_display.h"
#include "hikari/application/display_audio_port.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

struct AudioCacheSettings {
    bool ram = false;
    int delayMs = 0;
    int cacheFilesLimit = 10;
    std::filesystem::path cacheDir;
    std::filesystem::path indexDir; // empty: no index files
    std::vector<std::string> acceptedStreams;
};

// Several tracks: the first `accepted` language (case-insensitive), else rows to ask.
// One track is taken; none gives neither.
struct AudioTrackChoice {
    std::optional<int> track;
    std::vector<std::string> rows;
    std::vector<int> rowTracks;
};
AudioTrackChoice legacyAudioTrackChoice(const std::vector<AudioTrack> &tracks, const std::vector<std::string> &accepted);
// Splits on ';', skipping empty pieces.
std::vector<std::string> legacyAcceptedStreams(const std::string &value);
// Cache file name HikariSub legacy wrote: <name>_track<n>_<c>ch_<delay>.w64.
std::string legacyAudioCacheName(const std::string &path, int track, int channels, std::int64_t delayFrames);
// Index file name HikariSub legacy wrote: <name>_<track>.ffindex (track -1 without audio).
std::string legacyIndexName(const std::string &path, int track);

class AudioBox {
public:
    enum class State { Closed, Opening, Loading, Ready };
    static constexpr std::int64_t kBlockFrames = 332768;
    static constexpr std::int64_t kRamBlockBytes = 1 << 22;
    static constexpr int kDummyRate = 44100;
    static constexpr std::int64_t kDummySamples = 396'900'000; // 2 h 30 min
    // The dummy audio name older session files store.
    static constexpr char kDummyName[] = "dummy-audio:silence?sr=44100&bd=16&ch=1&ln=396900000";
    static std::string dummyName(std::int64_t samples);
    static bool isDummyName(std::string_view name) { return name.starts_with("dummy"); }
    // The frames of a name's positive "ln=", else kDummySamples.
    static std::int64_t dummySamples(std::string_view name);

    // Shown: the audio did not open; Warning: it opened anyway.
    enum class LogLevel { Shown, Warning, Debug };
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
    // Runs `step` later on the UI event loop; unset, it runs at once.
    void setDefer(std::function<void(std::function<void()>)> defer) { m_defer = std::move(defer); }

    // Unloads first, so a failed open leaves no audio. "dummy..." opens silence; "?dummy" is not audio.
    void open(const std::string &path);
    // `videoNewIndex`: the video indexed it now; `handoffIndexFile`: its index when it could not write one.
    void openFromVideo(const std::string &path, int track, bool videoNewIndex,
                       const std::string &handoffIndexFile = {});
    void close();

    State state() const { return m_state; }
    bool isOpen() const { return m_state != State::Closed; }
    const std::string &path() const { return m_path; }
    bool fromVideo() const { return m_fromVideo; }
    bool choosing() const { return m_choosing; }
    // The last open ended because the chooser was cancelled.
    bool declined() const { return m_declined; }
    // Why the last open ended without audio; nullopt when it did not fail.
    std::optional<SourceError> error() const { return m_error; }
    // (done, total) while Opening.
    std::pair<std::int64_t, std::int64_t> indexing() const { return m_indexing; }
    float progress() const { return m_progress; } // 0..1 while Loading
    const DisplayAudio *audio() const { return m_audio.get(); }
    // Outlives the box's hold on it (the player's fill thread reads it until it stops).
    std::shared_ptr<const DisplayAudio> sharedAudio() const { return m_audio; }
    int track() const { return m_track; }
    std::int64_t delayFrames() const { return m_delay; }
    // Empty with a RAM cache; written as cacheFile().part until complete.
    const std::filesystem::path &cacheFile() const { return m_cacheFile; }
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
    std::string m_videoIndexFile;
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
    bool m_ram = false;
    std::int64_t m_sourceFrame = 0, m_sourceEnd = 0;
    std::int64_t m_decodeEnd = 0; // no frame past it is decoded
    std::int64_t m_written = 0;
    std::int64_t m_silence = 0;
    int m_block = 0, m_blocks = 0;
    float m_progress = 0;
    std::uint64_t m_request = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace hikari::application
