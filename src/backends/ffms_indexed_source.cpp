#include "hikari/backends/ffms_indexed_source.h"

#include "hikari/backends/media_protocol.h"

namespace hikari::backends {

using application::SourceError;
using namespace helper;

namespace {

SourceError errorOf(HostError e)
{
    switch (e) {
    case HostError::Busy: return SourceError::Busy;
    case HostError::HelperLost: return SourceError::HelperLost;
    case HostError::NotReady: return SourceError::HelperLost;
    }
    return SourceError::BackendFailure;
}

SourceError errorOf(Outcome o, const std::vector<std::byte> &message)
{
    switch (o) {
    case Outcome::Cancelled: return SourceError::Cancelled;
    case Outcome::Unsupported: return SourceError::Unsupported;
    case Outcome::InvalidInput:
        return textOf(message) == "EOF" ? SourceError::EndOfStream
               : textOf(message) == "not open" ? SourceError::NotOpen
                                               : SourceError::InvalidInput;
    default: return SourceError::BackendFailure;
    }
}

} // namespace

FfmsIndexedSource::FfmsIndexedSource(QString helperProgram, QObject *parent)
    : QObject(parent), m_program(std::move(helperProgram))
{
}

FfmsIndexedSource::~FfmsIndexedSource() = default;

void FfmsIndexedSource::ensureHelper(std::function<void(bool)> ready)
{
    if (m_host && m_host->state() == HelperHost::State::Ready)
        return ready(true);
    // A lost or refused helper is replaced by a new process (a new session).
    if (!m_host || m_host->state() == HelperHost::State::Lost || m_host->state() == HelperHost::State::Refused)
        m_host = std::make_unique<HelperHost>(m_program, QStringList{}, media::kProtocolVersion);
    auto *host = m_host.get();
    auto shared = std::make_shared<std::function<void(bool)>>(std::move(ready));
    connect(host, &HelperHost::ready, this, [shared] { (*shared)(true); }, Qt::SingleShotConnection);
    connect(host, &HelperHost::refused, this, [shared] { (*shared)(false); }, Qt::SingleShotConnection);
    connect(host, &HelperHost::lost, this, [shared, host] {
        if (host->session() == 0)
            (*shared)(false); // ended before the handshake
    }, Qt::SingleShotConnection);
    host->start();
}

std::uint64_t FfmsIndexedSource::open(const std::string &path, Progress progress, Opened done)
{
    const std::uint64_t generation = ++m_generation;
    m_open = false;
    m_audio.reset();
    ensureHelper([this, generation, path, progress = std::move(progress), done = std::move(done)](bool ok) mutable {
        if (!ok)
            return done(std::unexpected(SourceError::MissingDependency));
        if (generation != m_generation)
            return done(std::unexpected(SourceError::Stale));
        auto request = m_host->request(generation,
            Writer().u8(static_cast<std::uint8_t>(media::Command::Open)).str(path).take(),
            [this, generation, progress, done](std::expected<Event, HostError> e) {
                if (!e)
                    return done(std::unexpected(errorOf(e.error())));
                Reader in(e->payload);
                if (e->kind == Kind::Progress) {
                    const auto doneCount = in.i64();
                    const auto total = in.i64();
                    if (progress && generation == m_generation)
                        progress(doneCount, total);
                    return;
                }
                if (e->kind != Kind::Terminal)
                    return;
                m_openRequest.reset();
                if (generation != m_generation)
                    return done(std::unexpected(SourceError::Stale));
                if (e->outcome != Outcome::Ok)
                    return done(std::unexpected(errorOf(e->outcome, e->payload)));
                application::SourceTimeline t;
                t.generation = generation;
                t.track = in.i32();
                t.fpsNumerator = in.i64();
                t.fpsDenominator = in.i64();
                // FFMS2's track time base gives milliseconds (pts * num / den);
                // the port's time base is in seconds.
                t.timeBaseNumerator = in.i64();
                t.timeBaseDenominator = in.i64() * 1000;
                const int count = in.i32();
                t.pts.reserve(static_cast<std::size_t>(std::max(count, 0)));
                for (int i = 0; i < count; ++i)
                    t.pts.push_back(in.i64());
                t.firstAudioTrack = in.i32();
                if (!in.ok())
                    return done(std::unexpected(SourceError::BackendFailure));
                m_open = true;
                done(std::move(t));
            });
        if (!request)
            return done(std::unexpected(errorOf(request.error())));
        m_openRequest = *request;
    });
    return generation;
}

void FfmsIndexedSource::cancelOpen()
{
    if (m_host && m_openRequest)
        m_host->cancel(*m_openRequest);
}

template <typename R>
std::pair<std::uint64_t, std::function<void(R)>> FfmsIndexedSource::track(std::function<void(R)> done)
{
    const std::uint64_t ticket = ++m_nextRead;
    auto slot = std::make_shared<std::function<void(R)>>(std::move(done));
    m_reads[ticket].cancel = [slot] {
        if (auto d = std::exchange(*slot, nullptr))
            d(std::unexpected(SourceError::Cancelled));
    };
    auto finish = [this, ticket, slot](R result) {
        m_reads.erase(ticket);
        if (auto d = std::exchange(*slot, nullptr))
            d(std::move(result));
    };
    return {ticket, finish};
}

void FfmsIndexedSource::frame(int index, FrameReady done)
{
    if (!m_open || !m_host)
        return done(std::unexpected(SourceError::NotOpen));
    const std::uint64_t generation = m_generation;
    auto [ticket, finish] = this->track(std::move(done));
    auto request = m_host->request(generation,
        Writer().u8(static_cast<std::uint8_t>(media::Command::Frame)).i32(index).take(),
        [generation, index, finish, this](std::expected<Event, HostError> e) {
            if (!e)
                return finish(std::unexpected(errorOf(e.error())));
            if (e->kind != Kind::Terminal)
                return;
            if (generation != m_generation)
                return finish(std::unexpected(SourceError::Stale));
            if (e->outcome != Outcome::Ok)
                return finish(std::unexpected(errorOf(e->outcome, e->payload)));
            Reader in(e->payload);
            application::IndexedFrame f;
            f.generation = generation;
            f.index = index;
            f.width = in.i32();
            f.height = in.i32();
            f.stride = in.i32();
            f.pts = in.i64();
            f.bgra = in.bytes();
            // Check the layout before publishing the buffer.
            if (!in.ok() || f.width <= 0 || f.height <= 0 || f.stride < f.width * 4 ||
                f.bgra.size() != static_cast<std::size_t>(f.stride) * static_cast<std::size_t>(f.height))
                return finish(std::unexpected(SourceError::BackendFailure));
            finish(std::move(f));
        });
    if (!request)
        return finish(std::unexpected(errorOf(request.error())));
    m_reads[ticket].request = *request;
}

void FfmsIndexedSource::openAudio(int track, AudioOpened done)
{
    if (!m_open || !m_host)
        return done(std::unexpected(SourceError::NotOpen));
    const std::uint64_t generation = m_generation;
    auto [ticket, finish] = this->track(std::move(done));
    auto request = m_host->request(generation,
        Writer().u8(static_cast<std::uint8_t>(media::Command::OpenAudio)).i32(track).take(),
        [generation, finish, this](std::expected<Event, HostError> e) {
            if (!e)
                return finish(std::unexpected(errorOf(e.error())));
            if (e->kind != Kind::Terminal)
                return;
            if (generation != m_generation)
                return finish(std::unexpected(SourceError::Stale));
            if (e->outcome != Outcome::Ok)
                return finish(std::unexpected(errorOf(e->outcome, e->payload)));
            Reader in(e->payload);
            application::AudioInfo a;
            a.generation = generation;
            a.track = in.i32();
            const int format = in.i32();
            a.sampleRate = in.i32();
            a.bitsPerSample = in.i32();
            a.channels = in.i32();
            a.channelLayout = in.i64();
            a.sampleCount = in.i64();
            a.originMicroseconds = in.i64();
            if (!in.ok() || format < 0 || format > 4 || a.channels <= 0 || a.bitsPerSample % 8 != 0)
                return finish(std::unexpected(SourceError::BackendFailure));
            a.format = static_cast<application::SampleFormat>(format); // FFMS_FMT_* order
            m_audio = a;
            finish(a);
        });
    if (!request)
        return finish(std::unexpected(errorOf(request.error())));
    m_reads[ticket].request = *request;
}

void FfmsIndexedSource::audio(std::int64_t start, std::int64_t count, AudioReady done)
{
    if (!m_audio || !m_host)
        return done(std::unexpected(SourceError::NotOpen));
    const std::uint64_t generation = m_generation;
    auto [ticket, finish] = this->track(std::move(done));
    const application::AudioInfo info = *m_audio;
    auto request = m_host->request(generation,
        Writer().u8(static_cast<std::uint8_t>(media::Command::Audio)).i64(start).i64(count).take(),
        [generation, info, finish, this](std::expected<Event, HostError> e) {
            if (!e)
                return finish(std::unexpected(errorOf(e.error())));
            if (e->kind != Kind::Terminal)
                return;
            if (generation != m_generation)
                return finish(std::unexpected(SourceError::Stale));
            if (e->outcome != Outcome::Ok)
                return finish(std::unexpected(errorOf(e->outcome, e->payload)));
            Reader in(e->payload);
            application::AudioBlock b;
            b.generation = generation;
            b.start = in.i64();
            b.count = in.i64();
            b.format = info.format;
            b.channels = info.channels;
            b.samples = in.bytes();
            const auto expected = static_cast<std::size_t>(b.count) * static_cast<std::size_t>(info.channels) *
                                  static_cast<std::size_t>(info.bitsPerSample / 8);
            if (!in.ok() || b.count < 0 || b.samples.size() != expected)
                return finish(std::unexpected(SourceError::BackendFailure));
            finish(std::move(b));
        });
    if (!request)
        return finish(std::unexpected(errorOf(request.error())));
    m_reads[ticket].request = *request;
}

void FfmsIndexedSource::chapters(const std::string &path, Listed done)
{
    ensureHelper([this, path, done = std::move(done)](bool ok) mutable {
        if (!ok)
            return done(std::unexpected(application::PlayerError::BackendFailure));
        auto request = m_host->request(0,
            Writer().u8(static_cast<std::uint8_t>(media::Command::Chapters)).str(path).take(),
            [done](std::expected<Event, HostError> e) {
                if (!e)
                    return done(std::unexpected(application::PlayerError::BackendFailure));
                if (e->kind != Kind::Terminal)
                    return;
                if (e->outcome != Outcome::Ok)
                    return done(std::unexpected(e->outcome == Outcome::InvalidInput
                                                    ? application::PlayerError::InvalidInput
                                                    : application::PlayerError::ResourceError));
                Reader in(e->payload);
                const std::int32_t n = in.i32();
                std::vector<application::Chapter> list;
                for (std::int32_t i = 0; in.ok() && i < n; ++i) {
                    application::Chapter c;
                    c.startUs = in.i64();
                    c.endUs = in.i64();
                    c.title = in.str();
                    list.push_back(std::move(c));
                }
                if (!in.ok() || n < 0 || !in.atEnd())
                    return done(std::unexpected(application::PlayerError::BackendFailure));
                done(std::move(list));
            });
        if (!request)
            done(std::unexpected(application::PlayerError::BackendFailure));
    });
}

void FfmsIndexedSource::cancelReads()
{
    auto reads = std::move(m_reads);
    m_reads.clear();
    for (auto &[ticket, read] : reads) {
        if (m_host && read.request)
            m_host->cancel(read.request);
        read.cancel();
    }
}

} // namespace hikari::backends
