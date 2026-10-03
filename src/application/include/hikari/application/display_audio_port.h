#pragma once

// The audio box's source (A1; legacy ProviderFFMS2 as the audio box used it at
// 20d647c4). The media helper probes a file's tracks, opens one audio track
// in legacy's decode format and reads it block by block. Failures carry the
// stage they happened at and FFMS2's own text, so the box can log legacy's
// messages ("Indexing error occurred: %s" and the others).
//
// Two ways to open: a file of its own (indexed for that one track, replacing
// whatever the helper had open: legacy Provider::Get for the box), or a
// second audio source over the video the helper already has open (legacy
// AudioDisplay::SetFile reusing the video's provider), which needs no second
// index. Either way displayAudio() reads it, and cancelDisplay() resolves only
// the box's own outstanding requests, never the video's.

#include "hikari/application/indexed_source.h"

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
};

class DisplayAudioPort {
public:
    using Progress = IndexedSourcePort::Progress;
    using Probed = std::function<void(std::expected<MediaProbe, AudioFailure>)>;
    using Opened = std::function<void(std::expected<AudioInfo, AudioFailure>)>;
    using Read = std::function<void(std::expected<AudioBlock, AudioFailure>)>;

    virtual ~DisplayAudioPort() = default;
    // Lists a file's tracks without indexing it or touching what is open.
    virtual void probe(const std::string &path, Probed done) = 0;
    // Indexes `path` for `track` alone (decode errors ignored) and opens it:
    // S16, front left and right when the track has more than one channel,
    // else mono, at the track's rate, sample 0 at the first video frame's time
    // (FFMS_DELAY_FIRST_VIDEO_TRACK). Replaces whatever the helper had open.
    virtual void openDisplayAudio(const std::string &path, int track, Progress progress, Opened done) = 0;
    // The same over the video the helper has open (its index already holds
    // every audio track); the video stays open.
    virtual void openSourceDisplayAudio(int track, Opened done) = 0;
    // A half-open range of sample frames of the box's audio.
    virtual void displayAudio(std::int64_t start, std::int64_t count, Read done) = 0;
    // Resolves the box's outstanding open and reads as Cancelled now.
    virtual void cancelDisplay() = 0;
};

} // namespace hikari::application
