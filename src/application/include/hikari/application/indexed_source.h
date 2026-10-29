#pragma once

#include "hikari/application/yuv_bgra.h"
#include "hikari/core/time.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

enum class SourceError {
    NotOpen,
    EndOfStream,     // past the last frame (normal, not a failure)
    InvalidInput,    // unreadable or unsupported file
    Unsupported,     // e.g. no video track
    Cancelled,
    Stale,           // belongs to an older generation, never delivered as current
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
    std::vector<int> keyframes;    // frame indices, ascending
    bool newIndex = true;          // indexed now, not read from an index file
    // Temporary index for the audio box when the index file could not be written, else empty.
    // The port removes it on reopen, end, or once the box has read it.
    std::string handoffIndexFile;
    // Frame 0's encoded size and the sample aspect ratio; 0 when unknown.
    int width = 0, height = 0;
    int sarNum = 0, sarDen = 0;
    // Frame 0's FFMS_CS_* matrix and FFMS_CR_* range.
    int colorSpace = 2, colorRange = 0; // unspecified
};

enum class OpenStage {
    Indexer,  // FFMS_CreateIndexer
    Indexing, // FFMS_DoIndexing2
    Source,   // FFMS_CreateVideoSource
    Convert,  // FFMS_SetOutputFormatV2
    Host,     // the helper itself, or no text
};
struct OpenFailure {
    OpenStage stage = OpenStage::Host;
    std::string message; // FFMS2's error text
};

struct IndexRequest {
    static constexpr int kEveryAudioTrack = -2;
    int audioTrack = kEveryAudioTrack; // -1: no audio track; n: track n alone
    std::string indexFile;             // empty: no index file
};

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

// 8-bit Y'CbCr planes: luma width x height, chroma chromaWidth x chromaHeight; pixel (x, y)
// takes chroma (x / 2, fullHeightChroma ? y : y / 2), no interpolation. Interleaved: plane 1
// holds U, V pairs (NV12), no plane 2. Rows start on 4-byte boundaries.
struct YcbcrPlanes {
    bool interleaved = false;
    bool fullHeightChroma = false; // 4:2:2
    int chromaWidth = 0, chromaHeight = 0;
    std::size_t offset[3] = {};
    int stride[3] = {}; // bytes
    yuv::Matrix matrix;
};

struct IndexedFrame {
    std::uint64_t generation = 0;
    int index = 0;
    std::int64_t pts = 0;
    int width = 0, height = 0, stride = 0;
    std::vector<std::byte> bgra; // owned, top-down
    // A player frame kept as Y'CbCr: `bgra` then holds its planes, not BGRA,
    // and `stride` is 0. Only the presenter reads a player frame's pixels.
    std::optional<YcbcrPlanes> ycbcr;
};

// Interleaved float over a half-open source range; chunks join without seams and total exactly
// the range's duration at the output rate (rounded to a frame).
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

// Thousandths of `total`, -1 without one. FFMS2 reports every packet, so redraw only on change.
constexpr std::int64_t progressPermille(std::int64_t done, std::int64_t total)
{
    if (total <= 0)
        return -1;
    return done <= 0 ? 0 : done >= total ? 1000 : done * 1000 / total;
}

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
    // A port without index files indexes as open() does.
    virtual std::uint64_t openIndexed(const std::string &path, const IndexRequest &request, Progress progress,
                                      Opened done)
    {
        (void)request;
        return open(path, std::move(progress), std::move(done));
    }
    virtual void cancelOpen() = 0;
    // Read in the Opened callback; nullopt when the open did not fail or there is nothing more to say.
    virtual std::optional<OpenFailure> openFailure() const { return std::nullopt; }
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
    // FFMS_CS_* / FFMS_CR_* values for frames decoded after it; Unsupported when the port cannot.
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
    // Pending open, reads and audio resolve Cancelled or go stale; the file is released.
    virtual void close() {}
    virtual std::uint64_t generation() const = 0;
};

}
