#include "hikari/backends/ffms_matroska.h"

#include "hikari/backends/media_protocol.h"

#include <utility>

namespace hikari::backends {

using application::MatroskaError;
using application::MatroskaFailure;
using namespace helper;

namespace {

MatroskaError errorOf(Outcome outcome, const std::vector<std::byte> &payload)
{
    switch (outcome) {
    case Outcome::Cancelled: return {MatroskaFailure::Cancelled, {}};
    case Outcome::Failed: return {MatroskaFailure::CannotOpen, textOf(payload)};
    default: return {MatroskaFailure::Failed, textOf(payload)};
    }
}

MatroskaError lost()
{
    return {MatroskaFailure::HelperLost, {}};
}

// A callback that runs at most once, whoever calls it first: the helper's
// answer, a loss, or cancel().
template <typename R> std::function<void(R)> once(std::function<void(R)> done)
{
    auto shared = std::make_shared<std::function<void(R)>>(std::move(done));
    return [shared](R r) {
        if (!*shared)
            return;
        auto f = std::exchange(*shared, nullptr);
        f(std::move(r));
    };
}

} // namespace

FfmsMatroska::FfmsMatroska(QString helperProgram, QObject *parent) : QObject(parent), m_program(std::move(helperProgram))
{
}

FfmsMatroska::~FfmsMatroska() = default;

void FfmsMatroska::ensureHelper(std::function<void(bool)> ready)
{
    if (m_host && m_host->state() == HelperHost::State::Ready)
        return ready(true);
    auto *host = m_host.get();
    if (!host || host->state() == HelperHost::State::Lost || host->state() == HelperHost::State::Refused) {
        m_host = std::make_unique<HelperHost>(m_program, QStringList{}, media::kProtocolVersion);
        host = m_host.get();
    }
    auto shared = std::make_shared<std::function<void(bool)>>(std::move(ready));
    connect(host, &HelperHost::ready, this, [shared] { (*shared)(true); }, Qt::SingleShotConnection);
    connect(host, &HelperHost::refused, this, [shared] { (*shared)(false); }, Qt::SingleShotConnection);
    connect(host, &HelperHost::lost, this, [shared, host] {
        if (host->session() == 0)
            (*shared)(false); // ended before the handshake
    }, Qt::SingleShotConnection);
    if (host->state() == HelperHost::State::Idle)
        host->start();
}

std::shared_ptr<bool> FfmsMatroska::begin(std::function<void()> resolveCancelled)
{
    auto cancelled = std::make_shared<bool>(false);
    m_cancel = [cancelled, resolveCancelled = std::move(resolveCancelled)] {
        *cancelled = true;
        resolveCancelled();
    };
    return cancelled;
}

void FfmsMatroska::send(std::shared_ptr<bool> cancelled, std::vector<std::byte> payload, Handler handler,
                        std::function<void(MatroskaError)> failed)
{
    ensureHelper([this, cancelled = std::move(cancelled), payload = std::move(payload), handler = std::move(handler),
                  failed = std::move(failed)](bool ok) mutable {
        if (*cancelled)
            return; // cancelled while the helper was starting: nothing is sent
        if (!ok)
            return failed(lost());
        auto id = std::make_shared<std::uint64_t>(0);
        auto request = m_host->request(0, std::move(payload), [this, handler, id](std::expected<Event, HostError> e) {
            if ((!e || e->kind == Kind::Terminal) && m_request == *id)
                m_request.reset();
            handler(std::move(e));
        });
        if (!request)
            return failed(lost());
        *id = *request;
        m_request = *request;
    });
}

void FfmsMatroska::cancel()
{
    if (m_request && m_host)
        m_host->cancel(*m_request);
    m_request.reset();
    if (auto cancelled = std::exchange(m_cancel, nullptr))
        cancelled();
}

void FfmsMatroska::subtitleTracks(const std::string &path, Tracks done)
{
    auto finish = once(std::move(done));
    auto cancelled = begin([finish] { finish(std::unexpected(MatroskaError{MatroskaFailure::Cancelled, {}})); });
    send(std::move(cancelled), Writer().u8(static_cast<std::uint8_t>(media::Command::SubtitleTracks)).str(path).take(),
         [finish](std::expected<Event, HostError> e) {
             if (!e)
                 return finish(std::unexpected(lost()));
             if (e->kind != Kind::Terminal)
                 return;
             if (e->outcome != Outcome::Ok)
                 return finish(std::unexpected(errorOf(e->outcome, e->payload)));
             Reader in(e->payload);
             const std::int32_t n = in.i32();
             std::vector<application::MatroskaTrack> tracks;
             for (std::int32_t i = 0; in.ok() && i < n; ++i) {
                 application::MatroskaTrack t;
                 t.track = in.i32();
                 t.name = in.str();
                 t.language = in.str();
                 t.codec = in.str();
                 tracks.push_back(std::move(t));
             }
             if (!in.ok() || n < 0 || !in.atEnd())
                 return finish(std::unexpected(MatroskaError{MatroskaFailure::Failed, "malformed reply"}));
             finish(std::move(tracks));
         },
         [finish](MatroskaError error) { finish(std::unexpected(std::move(error))); });
}

void FfmsMatroska::subtitles(const std::string &path, int track, Progress progress, Read done)
{
    auto finish = once(std::move(done));
    auto cancelled = begin([finish] { finish(std::unexpected(MatroskaError{MatroskaFailure::Cancelled, {}})); });
    send(std::move(cancelled), Writer().u8(static_cast<std::uint8_t>(media::Command::Subtitles)).str(path).i32(track).take(),
         [finish, progress = std::move(progress)](std::expected<Event, HostError> e) {
             if (!e)
                 return finish(std::unexpected(lost()));
             if (e->kind == Kind::Progress) {
                 Reader in(e->payload);
                 const std::int64_t start = in.i64(), total = in.i64();
                 if (in.ok() && progress)
                     progress(start, total);
                 return;
             }
             if (e->kind != Kind::Terminal)
                 return;
             if (e->outcome != Outcome::Ok)
                 return finish(std::unexpected(errorOf(e->outcome, e->payload)));
             Reader in(e->payload);
             application::MatroskaSubtitles read;
             read.codecPrivate = in.str();
             const std::int32_t n = in.i32();
             for (std::int32_t i = 0; in.ok() && i < n; ++i) {
                 application::MatroskaPacket p;
                 p.start = in.i64();
                 p.duration = in.i64();
                 p.line = in.str();
                 read.packets.push_back(std::move(p));
             }
             if (!in.ok() || n < 0 || !in.atEnd())
                 return finish(std::unexpected(MatroskaError{MatroskaFailure::Failed, "malformed reply"}));
             finish(std::move(read));
         },
         [finish](MatroskaError error) { finish(std::unexpected(std::move(error))); });
}

void FfmsMatroska::attachments(const std::string &path, Attachments done)
{
    auto finish = once(std::move(done));
    auto cancelled = begin([finish] { finish(std::unexpected(MatroskaError{MatroskaFailure::Cancelled, {}})); });
    auto list = std::make_shared<std::vector<application::MatroskaAttachment>>();
    auto malformed = std::make_shared<bool>(false);
    send(std::move(cancelled), Writer().u8(static_cast<std::uint8_t>(media::Command::Attachments)).str(path).take(),
         [finish, list, malformed](std::expected<Event, HostError> e) {
             if (!e)
                 return finish(std::unexpected(lost()));
             if (e->kind == Kind::Reply) {
                 Reader in(e->payload);
                 application::MatroskaAttachment a;
                 a.track = in.i32();
                 a.hasFilename = in.u8() != 0;
                 a.filename = in.str();
                 a.mimetype = in.str();
                 a.data = std::make_shared<const std::vector<std::byte>>(in.bytes());
                 if (!in.ok() || !in.atEnd())
                     *malformed = true;
                 list->push_back(std::move(a));
                 return;
             }
             if (e->kind != Kind::Terminal)
                 return;
             if (e->outcome != Outcome::Ok)
                 return finish(std::unexpected(errorOf(e->outcome, e->payload)));
             Reader in(e->payload);
             const std::int32_t n = in.i32();
             if (*malformed || !in.ok() || !in.atEnd() || n != std::int32_t(list->size()))
                 return finish(std::unexpected(MatroskaError{MatroskaFailure::Failed, "malformed reply"}));
             finish(std::move(*list));
         },
         [finish](MatroskaError error) { finish(std::unexpected(std::move(error))); });
}

} // namespace hikari::backends
