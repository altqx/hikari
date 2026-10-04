#include "hikari/backends/simulated_output.h"

#include "hikari/backends/audio_output_engine.h"

#include <chrono>

namespace hikari::backends {

namespace {

double steadySeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

SimulatedOutput::SimulatedOutput()
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(5);
    QObject::connect(&m_timer, &QTimer::timeout, [this] { render(); });
}

SimulatedOutput::~SimulatedOutput() = default;

std::vector<application::OutputDevice> SimulatedOutput::devices()
{
    return {{"Simulated/0/Simulated output", "Simulated output", "Simulated", true, 2, 48000}};
}

std::expected<application::OutputFormat, application::OutputError> SimulatedOutput::open(const std::string &,
                                                                                        application::OutputFormat format)
{
    if (format.channels < 1 || format.sampleRate < 1)
        return std::unexpected(application::OutputError::InvalidFormat);
    close();
    m_engine = std::make_unique<OutputEngine>(format, kLatencySeconds, 0.5);
    m_buffer.assign(static_cast<std::size_t>(kBufferFrames * format.channels), 0.0f);
    return format;
}

std::expected<void, application::OutputError> SimulatedOutput::start()
{
    if (!m_engine)
        return std::unexpected(application::OutputError::NotOpen);
    if (m_running)
        return {};
    m_engine->started();
    m_running = true;
    m_startedAt = steadySeconds();
    m_rendered = 0;
    m_timer.start();
    render(); // the first buffer, as a device asks for it on start
    return {};
}

void SimulatedOutput::stop()
{
    if (!m_engine)
        return;
    m_timer.stop();
    if (m_running) {
        m_engine->stopRequested();
        m_engine->streamFinished();
    }
    m_running = false;
    m_engine->stopped();
}

void SimulatedOutput::close()
{
    stop();
    m_engine.reset();
}

application::OutputFormat SimulatedOutput::format() const
{
    return m_engine ? m_engine->format() : application::OutputFormat{};
}

std::size_t SimulatedOutput::write(std::span<const float> interleaved)
{
    return m_engine ? m_engine->write(interleaved) : 0;
}

application::OutputStatus SimulatedOutput::status() const
{
    return m_engine ? m_engine->status() : application::OutputStatus{};
}

application::ClockEstimate SimulatedOutput::clock() const
{
    return m_engine ? m_engine->clock(steadySeconds()) : application::ClockEstimate{};
}

// Buffers until the elapsed time is covered, stamped as a device would
// stamp them: stream time from the frames rendered, the DAC time the latency
// after it.
void SimulatedOutput::render()
{
    if (!m_engine || !m_running)
        return;
    const auto &format = m_engine->format();
    const double now = steadySeconds();
    const auto due = static_cast<std::int64_t>((now - m_startedAt) * format.sampleRate) + kBufferFrames;
    while (m_rendered < due) {
        const double stream = double(m_rendered) / format.sampleRate;
        m_engine->render(m_buffer, {stream + kLatencySeconds, stream, m_startedAt + stream, false});
        m_rendered += kBufferFrames;
    }
}

} // namespace hikari::backends
