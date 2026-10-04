#pragma once

// The PortAudio editor output owner (N6, ADR 0010): the only editor-output
// library. PortAudio is initialized once per process; enumeration rescans the
// host when no stream is open so device changes appear. The callback hands
// each buffer to OutputEngine and nothing else.

#include "hikari/application/audio_output.h"

#include <memory>
#include <string>

namespace hikari::backends {

class OutputEngine;

class PortAudioOutput final : public application::AudioOutputPort {
public:
    struct Options {
        double queueSeconds = 0.5;    // ready-queue capacity
        double latencySeconds = -1;   // < 0: the device's default low output latency
        // The host API whose default output device "the default device" is
        // ("" = PortAudio's own default). WASAPI on Windows: it reports an
        // unplugged device as lost, where DirectSound only stalls
        // (audio.outputHostApi; the user's choice, 2026-10-04).
        std::string hostApi = defaultHostApi();
    };
    // "Windows WASAPI" on Windows, "" elsewhere.
    static std::string defaultHostApi();

    PortAudioOutput();
    explicit PortAudioOutput(Options options);
    ~PortAudioOutput() override;

    // False when PortAudio failed to initialize; every call then fails.
    bool available() const;

    std::vector<application::OutputDevice> devices() override;
    std::expected<application::OutputFormat, application::OutputError> open(const std::string &deviceId,
                                                                            application::OutputFormat format) override;
    std::expected<void, application::OutputError> start() override;
    void stop() override;
    void close() override;
    std::size_t write(std::span<const float> interleaved) override;
    application::OutputStatus status() const override;
    application::ClockEstimate clock() const override;
    application::OutputFormat format() const override;

    // The opened device's id and the stream's reported output latency.
    std::string openDeviceId() const;
    double outputLatency() const;
    // Fault injection for device-loss evidence: the next callback aborts the
    // stream exactly as a host does when its device disappears.
    void simulateDeviceLoss();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace hikari::backends
