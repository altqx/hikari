// H2 listening driver (#125): the real PortAudio editor output (N6) and Line
// audition (I3) under a person's control, for listening on real devices. The
// application does not offer Line audition yet (it arrives with the Audio
// playback card), so the guided session drives the same backend objects here.
//
// Commands arrive one per line on stdin; every answer and event is one JSON
// object per line on stdout:
//   devices                     rescan and list the output devices
//   open <device id>|default    open (or reopen) the output on a device
//   close                       close the output
//   media <path>                open a media file's first audio track
//   play <startMs> <endMs>      audition the half-open range [start, end)
//   stop                        logical stop of the running audition
//   status                      output status and clock estimate
//   gain <dB>                   software gain on everything written (<= 0)
//   simulate-loss               the next callback aborts as a lost device does
//                               (the session's dry run; real loss is unplugging)
//   quit
// A watcher reports device loss, underruns and discontinuities as they occur.
// Nothing plays until a `play` command; there is no built-in test sound.

#include "hikari/backends/ffms_indexed_source.h"
#include "hikari/backends/line_audition.h"
#include "hikari/backends/portaudio_output.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace hikari;
using application::AudioOutputPort;
using application::OutputError;

namespace {

double monotonicSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string quote(const std::string &s)
{
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

std::string number(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6f", v);
    return buf;
}

// One JSON line: the event name, monotonic and wall time, then `fields`
// (already-serialized "key": value pairs).
void emitEvent(const std::string &event, const std::string &fields = {})
{
    std::cout << "{\"event\": " << quote(event) << ", \"t\": " << number(monotonicSeconds()) << ", \"wall\": "
              << quote(QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString())
              << (fields.empty() ? "" : ", ") << fields << "}" << std::endl;
}

const char *errorName(OutputError e)
{
    switch (e) {
    case OutputError::NoDevice: return "NoDevice";
    case OutputError::DeviceUnavailable: return "DeviceUnavailable";
    case OutputError::InvalidFormat: return "InvalidFormat";
    case OutputError::NotOpen: return "NotOpen";
    case OutputError::DeviceLost: return "DeviceLost";
    case OutputError::BackendFailure: return "BackendFailure";
    }
    return "Unknown";
}

// Scales every written sample: a listening safety margin for devices whose
// volume the desktop mixer does not control (raw ALSA hardware devices).
class GainOutput final : public AudioOutputPort {
public:
    explicit GainOutput(backends::PortAudioOutput &inner) : m_inner(inner) {}
    void setGainDb(double db) { m_gain = static_cast<float>(std::pow(10.0, db / 20.0)); }
    double gainDb() const { return 20.0 * std::log10(m_gain); }

    std::vector<application::OutputDevice> devices() override { return m_inner.devices(); }
    std::expected<application::OutputFormat, OutputError> open(const std::string &id,
                                                              application::OutputFormat format) override
    {
        return m_inner.open(id, format);
    }
    std::expected<void, OutputError> start() override { return m_inner.start(); }
    void stop() override { m_inner.stop(); }
    void close() override { m_inner.close(); }
    application::OutputFormat format() const override { return m_inner.format(); }
    std::size_t write(std::span<const float> samples) override
    {
        if (m_gain == 1.0f)
            return m_inner.write(samples);
        m_scaled.assign(samples.begin(), samples.end());
        for (float &s : m_scaled)
            s *= m_gain;
        return m_inner.write(m_scaled);
    }
    application::OutputStatus status() const override { return m_inner.status(); }
    application::ClockEstimate clock() const override { return m_inner.clock(); }

private:
    backends::PortAudioOutput &m_inner;
    float m_gain = 1.0f;
    std::vector<float> m_scaled;
};

std::string statusFields(const application::OutputStatus &s)
{
    std::ostringstream o;
    o << "\"open\": " << (s.open ? "true" : "false") << ", \"running\": " << (s.running ? "true" : "false")
      << ", \"deviceLost\": " << (s.deviceLost ? "true" : "false") << ", \"callbacks\": " << s.callbacks
      << ", \"framesConsumed\": " << s.framesConsumed << ", \"silenceFrames\": " << s.silenceFrames
      << ", \"underruns\": " << s.underruns << ", \"discontinuities\": " << s.discontinuities;
    return o.str();
}

std::string clockFields(const application::ClockEstimate &c)
{
    std::ostringstream o;
    o << "\"clock\": {\"valid\": " << (c.valid ? "true" : "false") << ", \"epoch\": " << c.epoch
      << ", \"dacTime\": " << number(c.dacTimeSeconds) << ", \"dacTimeDerived\": " << (c.dacTimeDerived ? "true" : "false")
      << ", \"streamTime\": " << number(c.streamTimeSeconds) << ", \"monotonic\": " << number(c.monotonicSeconds)
      << ", \"uncertainty\": " << number(c.uncertaintySeconds) << ", \"bufferFrames\": " << c.bufferFrames << "}";
    return o.str();
}

class Driver : public QObject {
public:
    explicit Driver(const QString &helper) : m_source(helper), m_output(m_pa), m_audition(m_source, m_output)
    {
        // One audition for the driver's lifetime: its pending source answers
        // capture it, so it is never replaced while a request is in flight.
        QObject::connect(&m_audition, &backends::LineAudition::finished, this, [](const backends::AuditionReport &r) {
            std::ostringstream o;
            o << "\"sourceStart\": " << r.sourceStart << ", \"sourceCount\": " << r.sourceCount
              << ", \"outputRate\": " << r.outputRate << ", \"outputChannels\": " << r.outputChannels
              << ", \"outputFrames\": " << r.outputFrames << ", \"startedAt\": " << number(r.startedAt)
              << ", \"logicalStopAt\": " << number(r.logicalStopAt)
              << ", \"lastFrameConsumedAt\": " << number(r.lastFrameConsumedAt)
              << ", \"audibleEndAt\": " << number(r.audibleEndAt) << ", \"outputLatency\": " << number(r.outputLatency)
              << ", \"tailAfterLogicalStopMs\": " << number(r.drained ? (r.audibleEndAt - r.logicalStopAt) * 1000 : 0)
              << ", \"underruns\": " << r.underruns << ", \"drained\": " << (r.drained ? "true" : "false");
            emitEvent("finished", o.str());
        });
        QObject::connect(&m_audition, &backends::LineAudition::failed, this,
                         [](const QString &why) { emitEvent("failed", "\"reason\": " + quote(why.toStdString())); });
        m_watch.setInterval(100);
        QObject::connect(&m_watch, &QTimer::timeout, this, [this] { watch(); });
        m_watch.start();
    }

    void command(const std::string &line)
    {
        std::istringstream in(line);
        std::string verb;
        in >> verb;
        std::string rest;
        std::getline(in, rest);
        if (const auto first = rest.find_first_not_of(' '); first != std::string::npos)
            rest = rest.substr(first);
        else
            rest.clear();
        if (verb.empty() || verb.starts_with('#'))
            return;
        if (verb == "devices")
            devices();
        else if (verb == "open")
            open(rest);
        else if (verb == "close") {
            stopAudition();
            m_output.close();
            m_deviceId.clear();
            emitEvent("closed");
        } else if (verb == "media")
            media(rest);
        else if (verb == "play")
            play(rest);
        else if (verb == "stop") {
            if (!stopAudition())
                emitEvent("error", "\"command\": \"stop\", \"error\": \"nothing is playing\"");
        } else if (verb == "status")
            emitEvent("status", statusFields(m_output.status()) + ", " + clockFields(m_output.clock()) +
                                    ", \"device\": " + quote(m_deviceId));
        else if (verb == "gain") {
            const double db = std::atof(rest.c_str());
            if (db > 0) {
                emitEvent("error", "\"command\": \"gain\", \"error\": \"gain above 0 dB is refused\"");
                return;
            }
            m_output.setGainDb(db);
            emitEvent("gain", "\"db\": " + number(m_output.gainDb()));
        } else if (verb == "simulate-loss") {
            m_pa.simulateDeviceLoss();
            emitEvent("loss-simulated", "\"device\": " + quote(m_deviceId));
        } else if (verb == "quit") {
            if (m_quitting)
                return;
            m_quitting = true;
            stopAudition();
            m_output.close();
            emitEvent("bye");
            QCoreApplication::quit();
        } else {
            emitEvent("error", "\"command\": " + quote(verb) + ", \"error\": \"unknown command\"");
        }
    }

private:
    void devices()
    {
        std::ostringstream o;
        o << "\"available\": " << (m_pa.available() ? "true" : "false") << ", \"devices\": [";
        const auto list = m_output.devices();
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto &d = list[i];
            o << (i ? ", " : "") << "{\"id\": " << quote(d.id) << ", \"name\": " << quote(d.name)
              << ", \"hostApi\": " << quote(d.hostApi) << ", \"isDefault\": " << (d.isDefault ? "true" : "false")
              << ", \"maxChannels\": " << d.maxChannels << ", \"defaultSampleRate\": " << number(d.defaultSampleRate)
              << "}";
        }
        o << "]";
        emitEvent("devices", o.str());
    }

    void open(const std::string &id)
    {
        stopAudition();
        const std::string wanted = id == "default" ? std::string() : id;
        const auto format = m_output.open(wanted, {48000, 2});
        if (!format) {
            emitEvent("error", "\"command\": \"open\", \"device\": " + quote(id) + ", \"error\": " +
                                   quote(errorName(format.error())));
            return;
        }
        m_deviceId = m_pa.openDeviceId();
        m_lost = false;
        m_lastUnderruns = m_lastDiscontinuities = 0;
        emitEvent("opened", "\"requested\": " + quote(id) + ", \"device\": " + quote(m_deviceId) +
                                ", \"rate\": " + std::to_string(format->sampleRate) +
                                ", \"channels\": " + std::to_string(format->channels) +
                                ", \"outputLatency\": " + number(m_pa.outputLatency()));
    }

    void media(const std::string &path)
    {
        stopAudition();
        m_rate = 0;
        m_source.open(path, {}, [this, path](std::expected<application::SourceTimeline, application::SourceError> opened) {
            if (!opened || opened->firstAudioTrack < 0) {
                emitEvent("error", "\"command\": \"media\", \"path\": " + quote(path) +
                                       ", \"error\": " + quote(opened ? "no audio track" : "cannot open"));
                return;
            }
            m_source.openAudio(opened->firstAudioTrack,
                               [this, path](std::expected<application::AudioInfo, application::SourceError> audio) {
                                   if (!audio) {
                                       emitEvent("error", "\"command\": \"media\", \"path\": " + quote(path) +
                                                              ", \"error\": \"cannot open the audio track\"");
                                       return;
                                   }
                                   m_rate = audio->sampleRate;
                                   m_frames = audio->sampleCount;
                                   emitEvent("media", "\"path\": " + quote(path) +
                                                          ", \"sampleRate\": " + std::to_string(audio->sampleRate) +
                                                          ", \"channels\": " + std::to_string(audio->channels) +
                                                          ", \"sampleCount\": " + std::to_string(audio->sampleCount) +
                                                          ", \"originUs\": " + std::to_string(audio->originMicroseconds));
                               });
        });
    }

    void play(const std::string &args)
    {
        std::istringstream in(args);
        long long startMs = -1, endMs = -1;
        in >> startMs >> endMs;
        if (m_rate <= 0 || !m_output.status().open || startMs < 0 || endMs <= startMs) {
            emitEvent("error", "\"command\": \"play\", \"error\": " +
                                   quote(m_rate <= 0              ? "no media open"
                                         : !m_output.status().open ? "no output open"
                                                                   : "bad range"));
            return;
        }
        const std::int64_t start = startMs * m_rate / 1000;
        const std::int64_t count = std::min<std::int64_t>(endMs * m_rate / 1000, m_frames) - start;
        stopAudition();
        if (!m_audition.play(start, count)) {
            emitEvent("error", "\"command\": \"play\", \"error\": \"the audition did not start\"");
            return;
        }
        emitEvent("playing", "\"startMs\": " + std::to_string(startMs) + ", \"endMs\": " + std::to_string(endMs) +
                                 ", \"sourceStart\": " + std::to_string(start) + ", \"sourceCount\": " +
                                 std::to_string(count) + ", \"device\": " + quote(m_deviceId));
    }

    bool stopAudition()
    {
        if (!m_audition.active())
            return false;
        m_audition.stop(); // emits finished (not drained)
        return true;
    }

    void watch()
    {
        const auto s = m_output.status();
        if (s.deviceLost && !m_lost)
            emitEvent("device-lost", statusFields(s) + ", \"device\": " + quote(m_deviceId));
        m_lost = s.deviceLost;
        if (s.underruns > m_lastUnderruns || s.discontinuities > m_lastDiscontinuities)
            emitEvent("glitch", statusFields(s));
        m_lastUnderruns = s.underruns;
        m_lastDiscontinuities = s.discontinuities;
    }

    backends::FfmsIndexedSource m_source;
    backends::PortAudioOutput m_pa;
    GainOutput m_output;
    backends::LineAudition m_audition;
    std::string m_deviceId;
    int m_rate = 0;
    std::int64_t m_frames = 0;
    bool m_lost = false;
    bool m_quitting = false;
    std::uint64_t m_lastUnderruns = 0, m_lastDiscontinuities = 0;
    QTimer m_watch;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString helper;
#ifdef HIKARI_MEDIA_HELPER
    helper = QStringLiteral(HIKARI_MEDIA_HELPER);
#endif
    const QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); ++i)
        if (args[i] == QStringLiteral("--helper"))
            helper = args[i + 1];
    if (helper.isEmpty()) {
        std::cerr << "usage: hikari_h2_audition --helper <hikari-media-helper>\n";
        return 2;
    }
    int code = 0;
    {
        Driver driver(helper);
        emitEvent("ready", "\"helper\": " + quote(helper.toStdString()));
        // stdin is read on its own thread; each line runs on the event loop.
        std::thread([&app, &driver] {
            for (std::string line; std::getline(std::cin, line);)
                QMetaObject::invokeMethod(&app, [&driver, line] { driver.command(line); }, Qt::QueuedConnection);
            QMetaObject::invokeMethod(&app, [&driver] { driver.command("quit"); }, Qt::QueuedConnection);
        }).detach();
        code = app.exec();
    }
    std::cout.flush();
    std::_Exit(code); // the stdin reader may still be blocked in a read
}
