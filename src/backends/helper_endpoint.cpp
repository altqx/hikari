#include "hikari/backends/helper_endpoint.h"

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace hikari::backends::helper {

namespace {

std::mutex g_out;

void write(const Frame &frame)
{
    const auto bytes = encode(frame);
    std::lock_guard lock(g_out);
    std::fwrite(bytes.data(), 1, bytes.size(), stdout);
    std::fflush(stdout);
}

struct Inbox {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Frame> frames;
    bool closed = false;
    std::uint64_t cancelRequest = 0; // the request a Cancel named
};

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

int runHelper(const std::string &name, std::uint32_t protocolVersion, const RequestHandler &handler)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
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
            while (auto frame = decoder.next()) {
                std::lock_guard lock(inbox.mutex);
                if (frame->kind == Kind::Cancel)
                    inbox.cancelRequest = frame->request;
                else
                    inbox.frames.push_back(std::move(*frame));
                inbox.ready.notify_one();
            }
            if (decoder.failed())
                break;
        }
        std::lock_guard lock(inbox.mutex);
        inbox.closed = true;
        inbox.ready.notify_one();
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
        }
    }
    // stdin is closed or ignored from here; the reader ends with the process.
    reader.detach();
    return exitCode;
}

} // namespace hikari::backends::helper
