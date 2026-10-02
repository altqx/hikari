// H1: the helper-process framework, against a real helper executable.

#include "hikari/backends/helper_host.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <gtest/gtest.h>

#include <optional>

using namespace hikari::backends::helper;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

struct Results {
    std::vector<std::expected<Event, HostError>> events;
    bool resolved() const
    {
        return !events.empty() && (!events.back() || events.back()->kind == Kind::Terminal);
    }
    HelperHost::Handler handler()
    {
        return [this](std::expected<Event, HostError> e) { events.push_back(std::move(e)); };
    }
};

std::unique_ptr<HelperHost> startHelper(QStringList args = {}, HelperHost::Limits limits = {})
{
    auto host = std::make_unique<HelperHost>(QStringLiteral(HIKARI_TEST_HELPER), args, 1, limits);
    host->start();
    return host;
}

} // namespace

class HelperHostTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "helper_host_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }
};

TEST_F(HelperHostTest, HandshakeThenRoundTrip)
{
    auto host = startHelper();
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    EXPECT_EQ(host->helperName(), QStringLiteral("test-helper"));
    EXPECT_GT(host->session(), 0u);
    Results r;
    ASSERT_TRUE(host->request(1, bytesOf("echo:hello"), r.handler()));
    ASSERT_TRUE(waitFor([&] { return r.resolved(); }));
    ASSERT_EQ(r.events.size(), 2u);
    EXPECT_EQ(textOf(r.events[0]->payload), "hello");
    EXPECT_EQ(r.events[1]->outcome, Outcome::Ok);
    EXPECT_EQ(host->outstanding(), 0u);
}

TEST_F(HelperHostTest, IncompatibleHelperIsRefusedBeforeAnyWork)
{
    auto host = startHelper({QStringLiteral("--version"), QStringLiteral("2")});
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Refused; }));
    Results r;
    EXPECT_EQ(host->request(1, bytesOf("echo:x"), r.handler()).error(), HostError::NotReady);
}

TEST_F(HelperHostTest, ProgressThenTerminal)
{
    auto host = startHelper();
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    Results r;
    ASSERT_TRUE(host->request(1, bytesOf("progress"), r.handler()));
    ASSERT_TRUE(waitFor([&] { return r.resolved(); }));
    ASSERT_EQ(r.events.size(), 4u);
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(r.events[i]->kind, Kind::Progress);
}

TEST_F(HelperHostTest, CrashResolvesEveryPendingRequestOnce)
{
    auto host = startHelper();
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    Results crashing, queued;
    ASSERT_TRUE(host->request(1, bytesOf("crash"), crashing.handler()));
    ASSERT_TRUE(host->request(1, bytesOf("echo:never"), queued.handler()));
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Lost; }));
    QCoreApplication::processEvents();
    ASSERT_EQ(crashing.events.size(), 1u);
    EXPECT_EQ(crashing.events[0].error(), HostError::HelperLost);
    ASSERT_EQ(queued.events.size(), 1u);
    EXPECT_EQ(queued.events[0].error(), HostError::HelperLost);
    Results after;
    EXPECT_EQ(host->request(1, bytesOf("echo:x"), after.handler()).error(), HostError::NotReady);
}

TEST_F(HelperHostTest, CancelReachesARunningRequest)
{
    auto host = startHelper();
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    Results r;
    const auto id = host->request(1, bytesOf("wait"), r.handler());
    ASSERT_TRUE(id);
    host->cancel(*id);
    ASSERT_TRUE(waitFor([&] { return r.resolved(); }));
    EXPECT_EQ(r.events.back()->outcome, Outcome::Cancelled);
}

TEST_F(HelperHostTest, DuplicateTerminalIsRejectedAndCounted)
{
    auto host = startHelper();
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    Results r;
    ASSERT_TRUE(host->request(1, bytesOf("duplicate"), r.handler()));
    ASSERT_TRUE(waitFor([&] { return host->rejectedFrames() == 1; }));
    EXPECT_EQ(r.events.size(), 1u); // resolved once
}

TEST_F(HelperHostTest, OutstandingWorkIsBounded)
{
    auto host = startHelper({}, HelperHost::Limits{.maxOutstanding = 2});
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    Results a, b, c;
    const auto first = host->request(1, bytesOf("wait"), a.handler());
    ASSERT_TRUE(first);
    ASSERT_TRUE(host->request(1, bytesOf("echo:queued"), b.handler()));
    EXPECT_EQ(host->request(1, bytesOf("echo:x"), c.handler()).error(), HostError::Busy);
    host->cancel(*first);
    ASSERT_TRUE(waitFor([&] { return a.resolved() && b.resolved(); }));
    EXPECT_TRUE(host->request(1, bytesOf("echo:x"), c.handler()));
}

TEST_F(HelperHostTest, StopEndsTheSessionAndAReplacementGetsANewOne)
{
    auto first = startHelper();
    ASSERT_TRUE(waitFor([&] { return first->state() == HelperHost::State::Ready; }));
    const auto session = first->session();
    Results r;
    ASSERT_TRUE(first->request(1, bytesOf("wait"), r.handler()));
    first->stop();
    ASSERT_EQ(r.events.size(), 1u);
    EXPECT_EQ(r.events[0].error(), HostError::HelperLost);
    auto second = startHelper();
    ASSERT_TRUE(waitFor([&] { return second->state() == HelperHost::State::Ready; }));
    EXPECT_GT(second->session(), session);
}

TEST_F(HelperHostTest, ServiceCallIsAnsweredOnceWithinItsRequest)
{
    auto host = startHelper();
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    Results r;
    std::uint64_t call = 0;
    std::string asked;
    const auto id = host->request(1, bytesOf("service:question"), [&](std::expected<Event, HostError> e) {
        if (e && e->kind == Kind::Service) {
            call = e->call;
            asked = textOf(e->payload);
        }
        r.events.push_back(std::move(e));
    });
    ASSERT_TRUE(id);
    ASSERT_TRUE(waitFor([&] { return call != 0; }));
    EXPECT_EQ(asked, "question");
    EXPECT_FALSE(host->answer(*id, call + 1, Outcome::Ok, bytesOf("wrong call")));
    EXPECT_TRUE(host->answer(*id, call, Outcome::Ok, bytesOf("answer")));
    EXPECT_FALSE(host->answer(*id, call, Outcome::Ok, bytesOf("again"))) << "answered once";
    ASSERT_TRUE(waitFor([&] { return r.resolved(); }));
    ASSERT_EQ(r.events.size(), 3u); // Service, Reply, Terminal
    EXPECT_EQ(textOf(r.events[1]->payload), "answer");
    EXPECT_EQ(r.events[2]->outcome, Outcome::Ok);
    EXPECT_FALSE(host->answer(*id, call, Outcome::Ok, {})) << "the request has ended";
}

TEST_F(HelperHostTest, ServiceFailureReachesTheHelper)
{
    auto host = startHelper();
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    Results r;
    std::optional<std::uint64_t> id;
    id = *host->request(1, bytesOf("service:x"), [&](std::expected<Event, HostError> e) {
        if (e && e->kind == Kind::Service)
            host->answer(*id, e->call, Outcome::Unsupported);
        r.events.push_back(std::move(e));
    });
    ASSERT_TRUE(waitFor([&] { return r.resolved(); }));
    EXPECT_EQ(r.events.back()->outcome, Outcome::Unsupported);
}

TEST_F(HelperHostTest, CancelReleasesAHelperWaitingOnAService)
{
    auto host = startHelper();
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    Results r;
    std::uint64_t call = 0;
    const auto id = host->request(1, bytesOf("service:never answered"), [&](std::expected<Event, HostError> e) {
        if (e && e->kind == Kind::Service)
            call = e->call;
        r.events.push_back(std::move(e));
    });
    ASSERT_TRUE(waitFor([&] { return call != 0; }));
    host->cancel(*id);
    ASSERT_TRUE(waitFor([&] { return r.resolved(); }));
    EXPECT_EQ(r.events.back()->outcome, Outcome::Cancelled);
    EXPECT_FALSE(host->answer(*id, call, Outcome::Ok, {})) << "a late answer is dropped";
    // The helper is still usable.
    Results next;
    ASSERT_TRUE(host->request(1, bytesOf("echo:after"), next.handler()));
    ASSERT_TRUE(waitFor([&] { return next.resolved(); }));
    EXPECT_EQ(textOf(next.events[0]->payload), "after");
}

TEST_F(HelperHostTest, HelperStdoutNeverReachesTheProtocol)
{
    auto host = startHelper();
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    Results noisy, echo;
    ASSERT_TRUE(host->request(1, bytesOf("noisy"), noisy.handler()));
    ASSERT_TRUE(host->request(1, bytesOf("echo:intact"), echo.handler()));
    ASSERT_TRUE(waitFor([&] { return noisy.resolved() && echo.resolved(); }));
    EXPECT_EQ(noisy.events.back()->outcome, Outcome::Ok);
    EXPECT_EQ(textOf(echo.events[0]->payload), "intact");
    EXPECT_EQ(host->rejectedFrames(), 0u);
    ASSERT_TRUE(waitFor([&] { return host->diagnostics().contains("junk on stderr"); }));
    EXPECT_TRUE(host->diagnostics().contains("HKRI junk on stdout")) << host->diagnostics().toStdString();
}

TEST_F(HelperHostTest, DiagnosticsKeepOnlyTheNewestBytes)
{
    auto host = startHelper({}, HelperHost::Limits{.maxDiagnosticBytes = 20});
    ASSERT_TRUE(waitFor([&] { return host->state() == HelperHost::State::Ready; }));
    Results r;
    ASSERT_TRUE(host->request(1, bytesOf("noisy"), r.handler()));
    ASSERT_TRUE(waitFor([&] { return r.resolved() && host->diagnostics().endsWith("junk on stderr\n"); }));
    EXPECT_LE(host->diagnostics().size(), 20);
    EXPECT_GT(host->droppedDiagnosticBytes(), 0u);
}

TEST(HelperProtocol, DecoderHandlesSplitAndMalformedInput)
{
    const auto bytes = encode(Frame{Kind::Reply, 7, 1, 2, 3, bytesOf("payload")});
    Decoder d;
    d.feed(bytes.data(), 5);
    EXPECT_FALSE(d.next());
    d.feed(bytes.data() + 5, bytes.size() - 5);
    const auto frame = d.next();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->code, 7);
    EXPECT_EQ(frame->request, 3u);
    EXPECT_EQ(textOf(frame->payload), "payload");
    Decoder bad;
    const std::string garbage(40, 'x');
    bad.feed(reinterpret_cast<const std::byte *>(garbage.data()), garbage.size());
    EXPECT_FALSE(bad.next());
    EXPECT_TRUE(bad.failed());
}
