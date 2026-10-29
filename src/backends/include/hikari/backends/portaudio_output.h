#pragma once

#include "hikari/application/audio_output.h"

#include <cstdint>
#include <memory>
#include <string>

namespace hikari::backends {

class OutputEngine;

class PortAudioOutput final : public application::AudioOutputPort {
public:
    struct Options {
        double queueSeconds = 0.5;
        double latencySeconds = -1;   // < 0: the device's default low output latency
        // "" is PortAudio's own default. WASAPI reports an unplugged device as lost; DirectSound
        // only stalls.
        std::string hostApi = defaultHostApi();
    };
    // "Windows WASAPI" on Windows, "" elsewhere.
    static std::string defaultHostApi();
    // audio.outputHostApi (0 WASAPI, 1 DirectSound) as a host API; with `windows` false,
    // defaultHostApi().
    static std::string hostApiForSetting(std::int64_t value, bool windows = onWindows);
#ifdef _WIN32
    static constexpr bool onWindows = true;
#else
    static constexpr bool onWindows = false;
#endif

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

    std::string openDeviceId() const;
    double outputLatency() const;
    // The next callback aborts the stream as a host does when its device disappears.
    void simulateDeviceLoss();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace hikari::backends
