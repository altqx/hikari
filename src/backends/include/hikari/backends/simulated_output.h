#pragma once

// An editor output without a device (A4): the real OutputEngine rendered at
// the device's pace into nothing, so playback, its clock and the cursor run
// where no sound may be made (tests, playbackAudio off). Callbacks come from
// the owner thread's event loop; each one renders the buffers the elapsed
// time is due.

#include "hikari/application/audio_output.h"

#include <QTimer>

#include <functional>
#include <memory>

namespace hikari::backends {

class OutputEngine;

class SimulatedOutput final : public application::AudioOutputPort {
public:
    static constexpr int kBufferFrames = 256;
    static constexpr double kLatencySeconds = 0.02; // the reported DAC latency

    SimulatedOutput();
    ~SimulatedOutput() override;

    std::vector<application::OutputDevice> devices() override;
    // Any format is taken as asked.
    std::expected<application::OutputFormat, application::OutputError> open(const std::string &deviceId,
                                                                            application::OutputFormat format) override;
    std::expected<void, application::OutputError> start() override;
    void stop() override;
    void close() override;
    application::OutputFormat format() const override;
    std::size_t write(std::span<const float> interleaved) override;
    application::OutputStatus status() const override;
    application::ClockEstimate clock() const override;

    // Renders the buffers due by now (the timer's step).
    void render();

private:
    std::unique_ptr<OutputEngine> m_engine;
    QTimer m_timer;
    bool m_running = false;
    double m_startedAt = 0;      // steady seconds at start
    std::int64_t m_rendered = 0; // frames rendered since start
    std::vector<float> m_buffer;
};

} // namespace hikari::backends
