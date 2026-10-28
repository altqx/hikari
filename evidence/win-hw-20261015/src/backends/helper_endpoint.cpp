#include "hikari/backends/helper_endpoint.h"

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace hikari::backends::helper {

namespace {

std::mutex g_out;
int g_protocolFd = 1;

void writeAll(const std::vector<std::byte> &bytes)
{
    std::size_t done = 0;
    while (done < bytes.size()) {
#ifdef _WIN32
        const int n = ::_write(g_protocolFd, bytes.data() + done, static_cast<unsigned>(bytes.size() - done));
#else
        const auto n = ::write(g_protocolFd, bytes.data() + done, bytes.size() - done);
#endif
        if (n <= 0)
            return; // the host is gone; the reader sees stdin close
        done += static_cast<std::size_t>(n);
    }
}

void write(const Frame &frame)
{
    const auto bytes = encode(frame);
    std::lock_guard lock(g_out);
    writeAll(bytes);
}

// Moves the protocol to a private descriptor and sends fd 1 to stderr.
void protectProtocolChannel()
{
    std::fflush(stdout);
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    g_protocolFd = ::_dup(1);
    _setmode(g_protocolFd, _O_BINARY);
    ::_dup2(2, 1);
    SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE));
#else
    g_protocolFd = ::dup(1);
    ::dup2(2, 1);
#endif
}

struct Inbox {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Frame> frames;
    std::map<std::uint64_t, Frame> serviceReplies; // by call ID
    bool closed = false;
    std::uint64_t cancelRequest = 0; // the request a Cancel named
};

std::uint64_t callIdOf(const Frame &frame)
{
    Reader r(frame.payload);
    const auto id = static_cast<std::uint64_t>(r.i64());
    return r.ok() ? id : 0;
}

class StdResponder : public Responder {
public:
    StdResponder(const Frame &request, std::uint64_t session, Inbox &inbox)
        : m_request(request), m_session(session), m_inbox(inbox)
    {
    }
    void progress(std::vector<std::byte> payload) override { send(Kind::Progress, 0, std::move(payload)); }
    void reply(std::vector<std::byte> payload) override { send(Kind::Reply, 0, std::move(payload)); }
    void terminal(Outcome outcome, std::vector<std::byte> payload) override
    {
        if (m_ended)
            return;
        m_ended = true;
        send(Kind::Terminal, static_cast<std::uint16_t>(outcome), std::move(payload));
    }
    bool cancelled() const override
    {
        std::lock_guard lock(m_inbox.mutex);
        return m_inbox.cancelRequest == m_request.request;
    }
    std::expected<std::vector<std::byte>, Outcome> call(std::vector<std::byte> payload) override
    {
        if (m_ended)
            return std::unexpected(Outcome::Failed);
        static std::uint64_t s_nextCall = 1;
        const std::uint64_t id = s_nextCall++;
        Writer w;
        w.i64(static_cast<std::int64_t>(id));
        auto body = w.take();
        body.insert(body.end(), payload.begin(), payload.end());
        send(Kind::Service, 0, std::move(body));

        std::unique_lock lock(m_inbox.mutex);
        m_inbox.ready.wait(lock, [&] {
            return m_inbox.serviceReplies.contains(id) || m_inbox.closed ||
                   m_inbox.cancelRequest == m_request.request;
        });
        const auto it = m_inbox.serviceReplies.find(id);
        if (it == m_inbox.serviceReplies.end())
            return std::unexpected(Outcome::Cancelled);
        Frame reply = std::move(it->second);
        m_inbox.serviceReplies.erase(it);
        const auto outcome = static_cast<Outcome>(reply.code);
        if (outcome != Outcome::Ok)
            return std::unexpected(outcome);
        reply.payload.erase(reply.payload.begin(), reply.payload.begin() + 8);
        return std::move(reply.payload);
    }
    bool ended() const { return m_ended; }

private:
    void send(Kind kind, std::uint16_t code, std::vector<std::byte> payload)
    {
        if (m_ended && kind != Kind::Terminal)
            return;
        write(Frame{kind, code, m_session, m_request.run, m_request.request, std::move(payload)});
    }
    const Frame &m_request;
    std::uint64_t m_session;
    Inbox &m_inbox;
    bool m_ended = false;
};

} // namespace

void sendUncheckedFrame(const Frame &frame)
{
    write(frame);
}

int runHelper(const std::string &name, std::uint32_t protocolVersion, const RequestHandler &handler)
{
    protectProtocolChannel();
    write(Frame{Kind::Hello, static_cast<std::uint16_t>(protocolVersion), 0, 0, 0, bytesOf(name)});

    Inbox inbox;
    std::thread reader([&inbox] {
        Decoder decoder;
        std::byte buffer[1 << 16];
        for (;;) {
            // read() returns what has arrived; fread() would wait to fill the buffer.
#ifdef _WIN32
            const int n = ::_read(0, buffer, static_cast<unsigned>(sizeof buffer));
#else
            const auto n = ::read(0, buffer, sizeof buffer);
#endif
            if (n <= 0)
                break;
            decoder.feed(buffer, static_cast<std::size_t>(n));
            // Every frame of a read is taken in before the handler wakes, so
            // a Cancel that arrived with its request is seen from the start.
            std::lock_guard lock(inbox.mutex);
            bool arrived = false;
            while (auto frame = decoder.next()) {
                arrived = true;
                if (frame->kind == Kind::Cancel) {
                    inbox.cancelRequest = frame->request;
                } else if (frame->kind == Kind::ServiceReply) {
                    if (frame->payload.size() >= 8)
                        inbox.serviceReplies[callIdOf(*frame)] = std::move(*frame);
                } else {
                    inbox.frames.push_back(std::move(*frame));
                }
            }
            if (arrived)
                inbox.ready.notify_all();
            if (decoder.failed())
                break;
        }
        std::lock_guard lock(inbox.mutex);
        inbox.closed = true;
        inbox.ready.notify_all();
    });

    auto take = [&inbox]() -> std::optional<Frame> {
        std::unique_lock lock(inbox.mutex);
        inbox.ready.wait(lock, [&] { return !inbox.frames.empty() || inbox.closed; });
        if (inbox.frames.empty())
            return std::nullopt;
        Frame f = std::move(inbox.frames.front());
        inbox.frames.pop_front();
        return f;
    };

    int exitCode = 0;
    std::uint64_t session = 0;
    if (auto first = take(); !first || first->kind != Kind::Welcome) {
        exitCode = 3; // refused, or the host went away before welcoming us
    } else {
        session = first->session;
        while (auto frame = take()) {
            if (frame->kind != Kind::Request || frame->session != session)
                continue;
            StdResponder responder(*frame, session, inbox);
            handler(*frame, responder);
            if (!responder.ended())
                responder.terminal(Outcome::Failed, bytesOf("handler returned without a result"));
            std::lock_guard lock(inbox.mutex);
            inbox.serviceReplies.clear(); // answers that arrived after their call gave up
        }
    }
    // stdin is closed or ignored from here; the reader ends with the process.
    reader.detach();
    return exitCode;
}

} // namespace hikari::backends::helper
