#pragma once

#include "hikari/application/audio_output.h"

#include <functional>
#include <memory>

namespace hikari::backends {

class OutputEngine;

// No callback thread: every call first renders the buffers the elapsed time is due.
class SimulatedOutput final : public application::AudioOutputPort {
public:
    static constexpr int kBufferFrames = 256;
    static constexpr double kLatencySeconds = 0.02;

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

    void render() const;
    // The next buffer aborts the stream; reopening recovers.
    void simulateDeviceLoss();

private:
    std::unique_ptr<OutputEngine> m_engine;
    mutable bool m_running = false;
    double m_startedAt = 0;
    mutable std::int64_t m_rendered = 0;
    mutable std::vector<float> m_buffer;
};

} // namespace hikari::backends
