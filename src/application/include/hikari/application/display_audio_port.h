#pragma once

// The audio box's source (A1; legacy ProviderFFMS2 as the audio box used it at
// 20d647c4). The media helper probes a file's tracks, opens one audio track
// in legacy's decode format and reads it block by block. Failures carry the
// stage they happened at and FFMS2's own text, so the box can log legacy's
// messages ("Indexing error occurred: %s" and the others).
//
// The box has a media helper of its own, so its reads never wait behind the
// video's frames (legacy decoded the cache on a thread of its own). Opening
// uses legacy's index file (Indices/<name>_<track>.ffindex): for the open
// video's audio the video has just written it, or, when it could not, the
// temporary file it handed the index over in, so nothing is indexed twice.
// displayAudio() reads the open track, and cancelDisplay() resolves the box's
// outstanding requests.

#include "hikari/application/indexed_source.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace hikari::application {

// One audio track as FFMS2's indexer lists it (FFMS_GetTrackName,
// FFMS_GetTrackLanguage and FFMS_GetCodecNameI; a name or language FFMS2
// does not have is absent, not empty).
struct AudioTrack {
    int index = -1; // the container's track number
    bool hasName = false, hasLanguage = false;
    std::string name, language, codec;
};

struct MediaProbe {
    bool hasVideo = false;
    std::vector<AudioTrack> audio; // in container order
};

// Where an audio open or read failed (legacy ProviderFFMS2::Init's stages).
enum class AudioStage {
    Indexer,  // FFMS_CreateIndexer (legacy: a debug message only)
    Indexing, // FFMS_DoIndexing2: "Indexing error occurred: %s"
    Source,   // FFMS_CreateAudioSource: "An error occurred when creating audio source: %s"
    Convert,  // FFMS_SetOutputFormatA: "An error occurred when converting audio: %s"
    Read,     // FFMS_GetAudio (legacy: a debug message "error audio" + text)
    Host,     // the helper itself (lost, missing, stale): no FFMS2 text
};

struct AudioFailure {
    SourceError error = SourceError::BackendFailure;
    AudioStage stage = AudioStage::Host;
    std::string message; // FFMS2's error text, when it gave one
    // A failed read (Read): the block's buffer as FFMS2 left it, interleaved
    // S16 for the block's frames (the frames decoded before the failure, then
    // what the buffer held), as legacy cached it.
    std::vector<std::byte> samples;
};

// A block read into a cache's buffer, as legacy ProviderFFMS2::GetAudio
// read it: `frames` frames from `start`, of which the first `decode` are
// decoded (legacy's count after it stopped at its sample count); the S16
// samples [decode, frames) are set to zero first (legacy's "fill beyond"
// counts samples, not frames). `fresh` false reads into the block buffer the
// source keeps from read to read (legacy DiskCache's reused `data`, zeroed
// when the track opens); true into a new zeroed one (legacy RAMCache's new
// block, whose unwritten part was uninitialised memory: R3).
struct BlockRead {
    std::int64_t start = 0;
    std::int64_t frames = 0;
    std::int64_t decode = 0;
    bool fresh = false;
};

// An opened track and whether its index was made now (legacy newIndex: a
// disk cache is reused only with an index read from its file).
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
};

} // namespace hikari::application
