#pragma once

// General (unindexed) playback port (N5; docs/qt/backends.md GeneralPlayer,
// docs/qt/media.md). While general playback is active its player owns audio
// output and A/V scheduling; editor PortAudio output is stopped. A time seek
// is acknowledged only by a delivered video frame, never by a position
// notification, and its result reports both positions and the accuracy.
// Chapters come from a companion metadata port.

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

enum class PlayerError {
    NotOpen,
    InvalidInput,
    Unsupported,  // e.g. seeking a stream that is not seekable
    Stale,        // a newer open or seek replaced the request
    ResourceError,
    FormatError,
    AccessDenied,
    BackendFailure,
};

enum class PlaybackState { Stopped, Playing, Paused };
enum class MediaStatus { NoMedia, Loading, Loaded, Buffering, Buffered, EndOfMedia, Invalid };

struct PlayerTrack {
    std::string language; // ISO 639 code when the container names one, else empty
    std::string title;
    std::string codec;
};

struct MediaDescription {
    std::uint64_t generation = 0;
    std::optional<std::int64_t> durationUs; // empty: the container does not say
    bool seekable = false;
    bool audioOutput = false; // false: no output device, playback is silent
    std::vector<PlayerTrack> videoTracks, audioTracks, subtitleTracks;
    int activeVideo = -1, activeAudio = -1, activeSubtitle = -1; // -1: none (subtitles suppressed)
};

// The first video frame delivered after a time seek. Accuracy is whatever the
// player achieved; general playback promises no exact frame.
struct SeekResult {
    std::uint64_t generation = 0;
    std::int64_t requestedUs = 0;
    std::int64_t deliveredStartUs = 0; // the delivered frame's presentation interval
    std::optional<std::int64_t> deliveredEndUs;
    std::int64_t errorUs() const { return deliveredStartUs - requestedUs; }
};

// From the latest delivered frame: its start time and when it arrived.
// An estimate with its uncertainty; a seek, stop or rate change starts a new
// epoch, and the estimate is invalid unless playing.
struct PlayerClock {
    bool valid = false;
    std::uint64_t epoch = 0;
    std::int64_t mediaUs = 0;
    double monotonicSeconds = 0; // steady_clock when that frame arrived
    double rate = 1.0;
    std::int64_t uncertaintyUs = 0; // the frame's duration when known
};

class GeneralPlayerPort {
public:
    using Opened = std::function<void(std::expected<MediaDescription, PlayerError>)>;
    using Seeked = std::function<void(std::expected<SeekResult, PlayerError>)>;

    virtual ~GeneralPlayerPort() = default;
    // Replaces any open media; pending results go Stale.
    virtual void open(const std::string &path, Opened done) = 0;
    virtual void seek(std::int64_t us, Seeked done) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    // -1 switches the track type off (for subtitles: suppression).
    virtual bool selectAudioTrack(int index) = 0;
    virtual bool selectSubtitleTrack(int index) = 0;
    virtual PlaybackState playbackState() const = 0;
    virtual MediaStatus mediaStatus() const = 0;
    virtual double bufferProgress() const = 0;
    virtual PlayerClock clock() const = 0;
    virtual MediaDescription description() const = 0;
    virtual std::uint64_t generation() const = 0;
};

struct Chapter {
    std::int64_t startUs = 0;
    std::int64_t endUs = 0;
    std::string title;
};

class ChapterPort {
public:
    using Listed = std::function<void(std::expected<std::vector<Chapter>, PlayerError>)>;
    virtual ~ChapterPort() = default;
    virtual void chapters(const std::string &path, Listed done) = 0;
};

} // namespace hikari::application
