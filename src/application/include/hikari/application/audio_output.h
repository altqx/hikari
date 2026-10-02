#pragma once

// Editor audio output port (N6; docs/qt/media.md, ADR 0010). The control
// side opens a device and writes ready interleaved float samples; a real-time
// callback consumes them and fills silence when nothing is ready. Status and
// clock estimates are published without locks. Device identity is scoped to
// its host API, not its display name. All methods belong to one owner thread;
// only the callback runs elsewhere.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace hikari::application {

struct OutputDevice {
    std::string id;      // "<host API>/<device index>/<name>", stable within one enumeration
    std::string name;
    std::string hostApi; // e.g. "ALSA", "Windows WASAPI", "Windows DirectSound"
    bool isDefault = false;
    int maxChannels = 0;
    double defaultSampleRate = 0;
};

struct OutputFormat {
    int sampleRate = 48000;
    int channels = 2; // samples are interleaved 32-bit float

    friend bool operator==(const OutputFormat &, const OutputFormat &) = default;
};

enum class OutputError {
    NoDevice,          // nothing to open (no default output device)
    DeviceUnavailable, // the named device does not exist or refused to open
    InvalidFormat,     // neither the requested rate nor the device's rate fits
    NotOpen,
    DeviceLost,        // the stream ended without a stop; reopen to recover
    BackendFailure,
};

struct OutputStatus {
    bool open = false;
    bool running = false;
    bool deviceLost = false;
    std::uint64_t callbacks = 0;
    std::uint64_t framesConsumed = 0;  // sample frames taken from the ready queue
    std::uint64_t silenceFrames = 0;   // frames filled with silence (nothing ready)
    std::uint64_t underruns = 0;       // the queue ran dry while audio was flowing,
                                       // or the host reported an output underflow
    std::uint64_t discontinuities = 0; // the stream clock jumped (suspend, stall)
};

// Which clock a time belongs to. Stream times are PortAudio's per-stream clock;
// monotonic times are std::chrono::steady_clock seconds.
enum class ClockDomain { StreamTime };

// An estimate, not an observation: PortAudio's DAC time for the first frame of
// the latest callback buffer. A new epoch begins on start, stop, underrun and
// discontinuity; a consumer re-anchors on an epoch change and never
// extrapolates an old one. Invalid while stopped, lost or stalled.
struct ClockEstimate {
    bool valid = false;
    std::uint64_t epoch = 0;
    ClockDomain domain = ClockDomain::StreamTime;
    std::uint64_t framesConsumed = 0; // queued frames before the latest buffer
    std::uint32_t readyFrames = 0;    // queued frames in that buffer; silence follows
    std::uint32_t bufferFrames = 0;
    double dacTimeSeconds = 0;        // when the buffer's first frame should be heard
    bool dacTimeDerived = false;      // the host gave no DAC time; stream time + latency
    double streamTimeSeconds = 0;     // the stream clock at that callback
    double monotonicSeconds = 0;      // steady_clock at that callback, to map domains
    double uncertaintySeconds = 0;    // callback granularity, plus latency when derived
};

class AudioOutputPort {
public:
    virtual ~AudioOutputPort() = default;
    // Re-enumerates when no stream is open, so device changes appear.
    virtual std::vector<OutputDevice> devices() = 0;
    // Empty id: the host's default output device. An id from an earlier
    // enumeration resolves by host API and name when its index has moved.
    // Returns the negotiated format (the device's rate when the requested one
    // is refused); the caller resamples to it.
    virtual std::expected<OutputFormat, OutputError> open(const std::string &deviceId, OutputFormat format) = 0;
    virtual std::expected<void, OutputError> start() = 0;
    virtual void stop() = 0; // discards queued and device-buffered audio
    virtual void close() = 0;
    // Queues ready samples (whole frames); returns the samples accepted.
    virtual std::size_t write(std::span<const float> interleaved) = 0;
    virtual OutputStatus status() const = 0;
    virtual ClockEstimate clock() const = 0;
};

} // namespace hikari::application
