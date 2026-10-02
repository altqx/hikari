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
};

struct IndexedFrame {
    std::uint64_t generation = 0;
    int index = 0;
    std::int64_t pts = 0;
    int width = 0, height = 0, stride = 0;
    std::vector<std::byte> bgra; // owned, top-down
};

class IndexedSourcePort {
public:
    using Progress = std::function<void(std::int64_t done, std::int64_t total)>;
    using Opened = std::function<void(std::expected<SourceTimeline, SourceError>)>;
    using FrameReady = std::function<void(std::expected<IndexedFrame, SourceError>)>;

    virtual ~IndexedSourcePort() = default;
    // Starts indexing; replaces any open source (its pending results go Stale).
    virtual std::uint64_t open(const std::string &path, Progress progress, Opened done) = 0;
    virtual void cancelOpen() = 0;
    virtual void frame(int index, FrameReady done) = 0;
    virtual std::uint64_t generation() const = 0;
};

} // namespace hikari::application
