#include "hikari/backends/portaudio_output.h"

#include "hikari/backends/audio_output_engine.h"

#include <portaudio.h>

#include <chrono>
#include <mutex>

namespace hikari::backends {

using application::ClockEstimate;
using application::OutputDevice;
using application::OutputError;
using application::OutputFormat;
using application::OutputStatus;

namespace {

// PortAudio's enumeration and stream creation are not thread-safe, and a
// rescan (terminate + initialize) must wait until no stream is open anywhere
// in the process.
struct Library {
    std::mutex mutex;
    int users = 0;
    int openStreams = 0;
    bool initialized = false;
};

Library &library()
{
    static Library lib;
    return lib;
}

double monotonicSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string deviceId(PaDeviceIndex index, const PaDeviceInfo &info)
{
    const PaHostApiInfo *api = Pa_GetHostApiInfo(info.hostApi);
    return std::string(api ? api->name : "?") + "/" + std::to_string(index) + "/" + info.name;
}

// The default output device of the named host API, else PortAudio's.
PaDeviceIndex defaultOutputOf(const std::string &hostApi)
{
    if (!hostApi.empty())
        for (PaHostApiIndex i = 0, n = Pa_GetHostApiCount(); i < n; ++i)
            if (const PaHostApiInfo *api = Pa_GetHostApiInfo(i);
                api && hostApi == api->name && api->defaultOutputDevice != paNoDevice)
                return api->defaultOutputDevice;
    return Pa_GetDefaultOutputDevice();
}

std::vector<OutputDevice> enumerate(const std::string &hostApi)
{
    std::vector<OutputDevice> out;
    const PaDeviceIndex defaultOutput = defaultOutputOf(hostApi);
    const PaDeviceIndex count = Pa_GetDeviceCount();
    for (PaDeviceIndex i = 0; i < count; ++i) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        if (!info || info->maxOutputChannels <= 0)
            continue;
        const PaHostApiInfo *api = Pa_GetHostApiInfo(info->hostApi);
        out.push_back({deviceId(i, *info), info->name, api ? api->name : "?", i == defaultOutput,
                       info->maxOutputChannels, info->defaultSampleRate});
    }
    return out;
}

// Resolves an id from any enumeration: the exact id, or the same host API and
// name at a moved index.
std::expected<PaDeviceIndex, OutputError> resolve(const std::string &id, const std::string &hostApi)
{
    if (id.empty()) {
        const PaDeviceIndex index = defaultOutputOf(hostApi);
        if (index == paNoDevice)
            return std::unexpected(OutputError::NoDevice);
        return index;
    }
    const auto first = id.find('/');
    const auto second = first == std::string::npos ? first : id.find('/', first + 1);
    if (second == std::string::npos)
        return std::unexpected(OutputError::DeviceUnavailable);
    const std::string api = id.substr(0, first), name = id.substr(second + 1);
    PaDeviceIndex byName = paNoDevice;
    for (const OutputDevice &device : enumerate(hostApi)) {
        const auto index = static_cast<PaDeviceIndex>(std::stoi(device.id.substr(device.hostApi.size() + 1)));
        if (device.id == id)
            return index;
        if (byName == paNoDevice && device.hostApi == api && device.name == name)
            byName = index;
    }
    if (byName == paNoDevice)
        return std::unexpected(OutputError::DeviceUnavailable);
    return byName;
}

int callback(const void *, void *output, unsigned long frames, const PaStreamCallbackTimeInfo *time,
             PaStreamCallbackFlags flags, void *user)
{
    auto *engine = static_cast<OutputEngine *>(user);
    const auto channels = static_cast<std::size_t>(engine->format().channels);
    const CallbackTiming timing{time ? time->outputBufferDacTime : 0, time ? time->currentTime : 0,
                                monotonicSeconds(), (flags & paOutputUnderflow) != 0};
    return engine->render({static_cast<float *>(output), frames * channels}, timing) ? paContinue : paAbort;
}

void finished(void *user)
{
    static_cast<OutputEngine *>(user)->streamFinished();
}

} // namespace

struct PortAudioOutput::Impl {
    Options options;
    bool available = false;
    PaStream *stream = nullptr;
    std::unique_ptr<OutputEngine> engine;
    std::string deviceId;
    double latency = 0;
};

std::string PortAudioOutput::defaultHostApi()
{
#ifdef _WIN32
    return "Windows WASAPI";
#else
    return {};
#endif
}

std::string PortAudioOutput::hostApiForSetting(std::int64_t value, bool windows)
{
    if (!windows)
        return defaultHostApi();
    return value == 1 ? "Windows DirectSound" : "Windows WASAPI";
}

PortAudioOutput::PortAudioOutput() : PortAudioOutput(Options{}) {}

PortAudioOutput::PortAudioOutput(Options options) : d(std::make_unique<Impl>())
{
    d->options = options;
    Library &lib = library();
    std::scoped_lock lock(lib.mutex);
    if (lib.users++ == 0)
        lib.initialized = Pa_Initialize() == paNoError;
    d->available = lib.initialized;
}

PortAudioOutput::~PortAudioOutput()
{
    close();
    Library &lib = library();
    std::scoped_lock lock(lib.mutex);
    if (--lib.users == 0 && lib.initialized) {
        Pa_Terminate();
        lib.initialized = false;
    }
}

bool PortAudioOutput::available() const
{
    return d->available;
}

std::vector<OutputDevice> PortAudioOutput::devices()
{
    Library &lib = library();
    std::scoped_lock lock(lib.mutex);
    if (!lib.initialized)
        return {};
    // PortAudio enumerates once per initialization; rescan while idle so
    // added and removed devices appear.
    if (lib.openStreams == 0) {
        Pa_Terminate();
        lib.initialized = Pa_Initialize() == paNoError;
        if (!lib.initialized)
            return {};
    }
    return enumerate(d->options.hostApi);
}

std::expected<OutputFormat, OutputError> PortAudioOutput::open(const std::string &id, OutputFormat format)
{
    close();
    Library &lib = library();
    std::scoped_lock lock(lib.mutex);
    if (!lib.initialized)
        return std::unexpected(OutputError::BackendFailure);
    if (format.channels < 1 || format.sampleRate < 1)
        return std::unexpected(OutputError::InvalidFormat);
    const auto index = resolve(id, d->options.hostApi);
    if (!index)
        return std::unexpected(index.error());
    const PaDeviceInfo *info = Pa_GetDeviceInfo(*index);
    if (!info)
        return std::unexpected(OutputError::DeviceUnavailable);
    if (format.channels > info->maxOutputChannels)
        return std::unexpected(OutputError::InvalidFormat);

    PaStreamParameters params{};
    params.device = *index;
    params.channelCount = format.channels;
    params.sampleFormat = paFloat32;
    params.suggestedLatency = d->options.latencySeconds >= 0 ? d->options.latencySeconds
                                                             : info->defaultLowOutputLatency;
    if (Pa_IsFormatSupported(nullptr, &params, format.sampleRate) != paFormatIsSupported) {
        const int deviceRate = static_cast<int>(info->defaultSampleRate);
        if (deviceRate == format.sampleRate
            || Pa_IsFormatSupported(nullptr, &params, deviceRate) != paFormatIsSupported)
            return std::unexpected(OutputError::InvalidFormat);
        format.sampleRate = deviceRate;
    }

    // The engine must exist before the stream: it is the callback's state.
    auto engine = std::make_unique<OutputEngine>(format, params.suggestedLatency, d->options.queueSeconds);
    PaStream *stream = nullptr;
    const PaError err = Pa_OpenStream(&stream, nullptr, &params, format.sampleRate, paFramesPerBufferUnspecified,
                                      paNoFlag, &callback, engine.get());
    if (err != paNoError)
        return std::unexpected(err == paInvalidDevice || err == paDeviceUnavailable ? OutputError::DeviceUnavailable
                                                                                   : OutputError::BackendFailure);
    if (Pa_SetStreamFinishedCallback(stream, &finished) != paNoError) {
        Pa_CloseStream(stream);
        return std::unexpected(OutputError::BackendFailure);
    }
    const PaStreamInfo *streamInfo = Pa_GetStreamInfo(stream);
    d->latency = streamInfo ? streamInfo->outputLatency : params.suggestedLatency;
    d->stream = stream;
    d->engine = std::move(engine);
    d->deviceId = deviceId(*index, *info);
    ++lib.openStreams;
    return format;
}

std::expected<void, OutputError> PortAudioOutput::start()
{
    if (!d->stream)
        return std::unexpected(OutputError::NotOpen);
    if (d->engine->status().deviceLost)
        return std::unexpected(OutputError::DeviceLost);
    if (Pa_IsStreamActive(d->stream) == 1)
        return {};
    d->engine->started();
    if (Pa_StartStream(d->stream) != paNoError) {
        d->engine->stopped();
        return std::unexpected(OutputError::BackendFailure);
    }
    return {};
}

void PortAudioOutput::stop()
{
    if (!d->stream)
        return;
    d->engine->stopRequested();
    if (Pa_IsStreamStopped(d->stream) == 0)
        Pa_AbortStream(d->stream);
    d->engine->stopped();
}

void PortAudioOutput::close()
{
    if (!d->stream)
        return;
    stop();
    Library &lib = library();
    std::scoped_lock lock(lib.mutex);
    Pa_CloseStream(d->stream);
    --lib.openStreams;
    d->stream = nullptr;
    d->engine.reset();
    d->deviceId.clear();
    d->latency = 0;
}

std::size_t PortAudioOutput::write(std::span<const float> interleaved)
{
    return d->engine ? d->engine->write(interleaved) : 0;
}

OutputStatus PortAudioOutput::status() const
{
    if (!d->engine)
        return {};
    // Some host APIs end the stream on device loss without the finished
    // callback: a stream that is no longer active although nobody stopped it
    // was lost.
    if (const auto s = d->engine->status(); s.running && !s.deviceLost && Pa_IsStreamActive(d->stream) == 0)
        d->engine->streamFinished();
    return d->engine->status();
}

ClockEstimate PortAudioOutput::clock() const
{
    return d->engine ? d->engine->clock(monotonicSeconds()) : ClockEstimate{};
}

OutputFormat PortAudioOutput::format() const
{
    return d->engine ? d->engine->format() : OutputFormat{};
}

std::string PortAudioOutput::openDeviceId() const
{
    return d->deviceId;
}

double PortAudioOutput::outputLatency() const
{
    return d->latency;
}

void PortAudioOutput::simulateDeviceLoss()
{
    if (d->engine)
        d->engine->injectLoss();
}

} // namespace hikari::backends
