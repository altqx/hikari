#pragma once

// IndexedSource port (N1; docs/qt/backends.md). Opening indexes a source and
// returns its video track's timeline; a frame request returns the exact
// indexed frame as an owned BGRA buffer, or EOF/error. Every result names the
// source generation it belongs to; a result from an older generation (the
// source was reopened or replaced) is reported as Stale, never delivered as
// the current source's frame.

#include "hikari/core/time.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace hikari::application {

enum class SourceError {
    NotOpen,
    EndOfStream,     // past the last frame (normal, not a failure)
    InvalidInput,    // unreadable or unsupported file
    Unsupported,     // e.g. no video track
    Cancelled,
    Stale,           // belongs to an older generation
    Busy,
    HelperLost,      // the media helper ended; reopen to continue
    MissingDependency,
    BackendFailure,
};

struct SourceTimeline {
    std::uint64_t generation = 0;
    int track = 0;
    std::int64_t fpsNumerator = 0, fpsDenominator = 1; // nominal rate
    // Seconds per PTS unit: pts * timeBaseNumerator / timeBaseDenominator seconds.
    std::int64_t timeBaseNumerator = 1, timeBaseDenominator = 1000;
    std::vector<std::int64_t> pts; // per frame, in the time base (rational PTS)
    int firstAudioTrack = -1;      // -1: the source has no audio
    std::vector<int> audioTracks;  // every audio track, in container order
    std::vector<int> keyframes;    // frame indices of keyframes, ascending (V2)
    bool newIndex = true;          // A1: indexed now, not read from an index file
    // A1: when the open's index file could not be written, the new index in a
    // temporary file for the audio box to read (legacy's box shared the
    // video's index); empty otherwise. The port removes it when the source is
    // opened again or ends, or sooner when told the box has read it.
    std::string handoffIndexFile;
    // T1: frame 0's encoded size and the track's sample aspect ratio
    // (FFMS_Frame EncodedWidth/EncodedHeight, FFMS_VideoProperties SARNum/
    // SARDen; 0 when unknown), as legacy ProviderFFMS2::Init read them for
    // the video's aspect (ProviderFFMS2.cpp:362-366).
    int width = 0, height = 0;
    int sarNum = 0, sarDen = 0;
    // V4: frame 0's Y'CbCr matrix and range (FFMS_Frame ColorSpace and
    // ColorRange, FFMS_CS_* / FFMS_CR_*), as legacy ProviderFFMS2::Init read
    // them for the Script properties matrix (ProviderFFMS2.cpp:368-369).
    int colorSpace = 2, colorRange = 0; // unspecified
};

// How an open indexes (A1; legacy ProviderFFMS2::Init). Legacy indexed the
// video with the one audio track it chose and kept the index in
// <settings folder>/Indices/<name>_<track>.ffindex, read again on the next
// open of the same file and track while it is not older than the file.
struct IndexRequest {
    static constexpr int kEveryAudioTrack = -2;
    int audioTrack = kEveryAudioTrack; // -1: no audio track; n: track n alone
    std::string indexFile;             // empty: no index file
};

// AudioDecode (N2): immutable PCM for a half-open source sample range.
enum class SampleFormat { U8, S16, S32, Float, Double };

struct AudioInfo {
    std::uint64_t generation = 0;
    int track = -1;
    SampleFormat format = SampleFormat::S16;
    int sampleRate = 0;
    int bitsPerSample = 0;
    int channels = 0;
    std::int64_t channelLayout = 0; // FFmpeg channel mask
    std::int64_t sampleCount = 0;   // sample frames (one sample on every channel)
    // Source time of sample 0, in microseconds; samples are not shifted or
    // padded to time zero.
    std::int64_t originMicroseconds = 0;
};

struct AudioBlock {
    std::uint64_t generation = 0;
    std::int64_t start = 0;       // first sample frame
    std::int64_t count = 0;       // sample frames (shortened at the end of the source)
    SampleFormat format = SampleFormat::S16;
    int channels = 0;
    std::vector<std::byte> samples; // interleaved
};

struct IndexedFrame {
    std::uint64_t generation = 0;
    int index = 0;
    std::int64_t pts = 0;
    int width = 0, height = 0, stride = 0;
    std::vector<std::byte> bgra; // owned, top-down
};

// A resampled stream over a half-open source range (I3): interleaved float at
// the requested rate and channel count. Chunks continue one resampler, so
// they join without seams, and the stream yields exactly the range's duration
// at the output rate (rounded to a frame).
struct PcmStream {
    std::uint64_t generation = 0;
    std::int64_t start = 0, count = 0; // the source range (shortened at the end)
    std::int64_t totalFrames = 0;      // output frames the stream will yield
};
struct PcmChunk {
    std::uint64_t generation = 0;
    std::int64_t frames = 0;
    std::vector<float> samples; // frames * channels
    bool end = false;
};

class IndexedSourcePort {
public:
    using Progress = std::function<void(std::int64_t done, std::int64_t total)>;
    using Opened = std::function<void(std::expected<SourceTimeline, SourceError>)>;
    using FrameReady = std::function<void(std::expected<IndexedFrame, SourceError>)>;
    using AudioOpened = std::function<void(std::expected<AudioInfo, SourceError>)>;
    using AudioReady = std::function<void(std::expected<AudioBlock, SourceError>)>;

    virtual ~IndexedSourcePort() = default;
    // Starts indexing; replaces any open source (its pending results go Stale).
    virtual std::uint64_t open(const std::string &path, Progress progress, Opened done) = 0;
    // The same with legacy's track choice and index file; a port without
    // index files indexes as open() does.
    virtual std::uint64_t openIndexed(const std::string &path, const IndexRequest &request, Progress progress,
                                      Opened done)
    {
        (void)request;
        return open(path, std::move(progress), std::move(done));
    }
    virtual void cancelOpen() = 0;
    virtual void frame(int index, FrameReady done) = 0;
    // Opens an audio track of the open source (its timeline's firstAudioTrack).
    virtual void openAudio(int track, AudioOpened done) = 0;
    // A half-open range [start, start + count) of sample frames.
    virtual void audio(std::int64_t start, std::int64_t count, AudioReady done) = 0;
    using PcmBegun = std::function<void(std::expected<PcmStream, SourceError>)>;
    using PcmReady = std::function<void(std::expected<PcmChunk, SourceError>)>;
    // Starts a resampled stream over the open audio track (replacing any other).
    virtual void beginPcm(std::int64_t start, std::int64_t count, int outRate, int outChannels, PcmBegun done) = 0;
    virtual void nextPcm(std::int64_t maxFrames, PcmReady done) = 0;
    // V4: the converter's input matrix and range (FFMS_SetInputFormatV with
    // FFMS_CS_* / FFMS_CR_* values, the pixel format kept); frames decoded
    // after it use them. A port that cannot convert otherwise reports
    // Unsupported.
    using MatrixSet = std::function<void(std::expected<void, SourceError>)>;
    virtual void setInputMatrix(int colorSpace, int colorRange, MatrixSet done)
    {
        (void)colorSpace;
        (void)colorRange;
        done(std::unexpected(SourceError::Unsupported));
    }
    // Resolves every outstanding frame and audio request as Cancelled now;
    // the helper's late results are dropped.
    virtual void cancelReads() = 0;
    virtual std::uint64_t generation() const = 0;
};

} // namespace hikari::application
