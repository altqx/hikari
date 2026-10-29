#pragma once

#include "hikari/application/indexed_source.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace hikari::application {

// A name or language FFMS2 lacks is absent (has* false), not empty.
struct AudioTrack {
    int index = -1; // the container's track number
    bool hasName = false, hasLanguage = false;
    std::string name, language, codec;
};

struct MediaProbe {
    bool hasVideo = false;
    std::vector<AudioTrack> audio; // in container order
};

enum class AudioStage {
    Indexer,  // FFMS_CreateIndexer
    Indexing, // FFMS_DoIndexing2
    Source,   // FFMS_CreateAudioSource
    Convert,  // FFMS_SetOutputFormatA
    Read,     // FFMS_GetAudio
    Host,     // the helper itself (lost, missing, stale): no FFMS2 text
};

struct AudioFailure {
    SourceError error = SourceError::BackendFailure;
    AudioStage stage = AudioStage::Host;
    std::string message; // FFMS2's error text, when it gave one
    // A failed Read: the block's buffer as FFMS2 left it, interleaved S16.
    std::vector<std::byte> samples;
};

// `frames` frames from `start`, the first `decode` decoded. `fresh` false
// reads into the buffer the source reuses between reads (zeroed when the
// track opens); true into a new zeroed one.
// MUST KILL: zeroes the S16 samples [decode, frames), counting samples, not frames
struct BlockRead {
    std::int64_t start = 0;
    std::int64_t frames = 0;
    std::int64_t decode = 0;
    bool fresh = false;
};

// newIndex: the index was made now, not read from its file.
struct DisplayAudioOpened {
    AudioInfo info;
    bool newIndex = true;
};

class DisplayAudioPort {
public:
    using Progress = IndexedSourcePort::Progress;
    using Probed = std::function<void(std::expected<MediaProbe, AudioFailure>)>;
    using Opened = std::function<void(std::expected<DisplayAudioOpened, AudioFailure>)>;
    using Read = std::function<void(std::expected<AudioBlock, AudioFailure>)>;

    virtual ~DisplayAudioPort() = default;
    // Lists a file's tracks without indexing it or touching what is open.
    virtual void probe(const std::string &path, Probed done) = 0;
    // Reads `indexFile` when it can be used, else indexes `path` for `track`
    // alone (decode errors ignored) and writes it; then opens the track: S16,
    // front left and right when the track has more than one channel, else
    // mono, at the track's rate, sample 0 at the first video frame's time
    // (FFMS_DELAY_FIRST_VIDEO_TRACK). Replaces whatever the helper had open.
    virtual void openDisplayAudio(const std::string &path, int track, const std::string &indexFile, Progress progress,
                                  Opened done) = 0;
    // A block of the box's audio (BlockRead); the answer has `frames` frames.
    virtual void displayAudio(const BlockRead &read, Read done) = 0;
    // Resolves the box's outstanding open and reads as Cancelled now.
    virtual void cancelDisplay() = 0;
    // The open and reads resolve Cancelled, and the port releases the file.
    virtual void closeDisplay() {}
};

}
