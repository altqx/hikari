#include "hikari/backends/ai_sidecar_host.h"

#include "fake_host.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <optional>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <fstream>
#include <string>
#endif

using namespace hikari::backends::ai;
using hikari::api::Json;

namespace {

constexpr const char *kBuildId = "0.1.0+test";
constexpr const char *kSentinel = "sk-hikari-sentinel-0f9e8d7c";

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    for (;;) {
        if (done())
            return true;
        if (t.elapsed() >= ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

bool processAlive(qint64 pid)
{
#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
#else
    if (::kill(static_cast<pid_t>(pid), 0) != 0)
        return false;
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    std::getline(stat, line);
    const auto paren = line.rfind(')');
    if (paren == std::string::npos || paren + 2 >= line.size())
        return ::kill(static_cast<pid_t>(pid), 0) == 0;
    const char state = line[paren + 2];
    return state != 'Z' && state != 'X' && state != 'x';
#endif
}

struct Call {
    std::optional<SidecarResult> result;
    int replies = 0;
    SidecarReply reply()
    {
        return [this](SidecarResult r) {
            result = std::move(r);
            ++replies;
        };
    }
    bool done() const { return replies > 0; }
};

class AiSidecarHostTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_sidecar_host_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    AiSidecarHost::Options options(const QString &mode = QStringLiteral("ok"))
    {
        AiSidecarHost::Options o;
        o.program = QStringLiteral(HIKARI_FAKE_AI_SIDECAR);
        o.arguments = {QStringLiteral("--mode"), mode, QStringLiteral("--build-id"), QString::fromLatin1(kBuildId),
                       QStringLiteral("--api-version"), QString::number(hikari::api::kApiVersion)};
        o.workingDirectory = dir.filePath(QStringLiteral("ai/work"));
        o.buildId = kBuildId;
        o.handshakeMs = 5'000;
        o.shutdownMs = 600;
        o.firstBackoffMs = 10;
        return o;
    }

    std::unique_ptr<AiSidecarHost> host(AiSidecarHost::Options o, hikari::api::Api *api = nullptr)
    {
        return std::make_unique<AiSidecarHost>(std::move(o), api, hikari::api::Caller{"sidecar", "AI"});
    }

    static SidecarResult call(AiSidecarHost &h, const std::string &method, Json params = {})
    {
        Call c;
        h.request(method, std::move(params), c.reply());
        EXPECT_TRUE(waitFor([&] { return c.done(); }));
        EXPECT_EQ(c.replies, 1);
        return c.result.value_or(std::unexpected(SidecarFailure{SidecarError::SidecarLost, 0, "no reply", {}}));
    }

    QTemporaryDir dir;
};

TEST_F(AiSidecarHostTest, StartsOnTheFirstRequestAndAnswers)
{
    auto h = host(options());
    EXPECT_EQ(h->state(), AiSidecarHost::State::Idle);
    EXPECT_EQ(h->processId(), 0);
    const auto result = call(*h, "echo", Json::object({{"x", 1}}));
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, Json::object({{"x", 1}}));
    EXPECT_EQ(h->state(), AiSidecarHost::State::Ready);
}

TEST_F(AiSidecarHostTest, NoAnswerToInitializeInTimeIsACrash)
{
    auto o = options(QStringLiteral("silent"));
    o.handshakeMs = 300;
    auto h = host(o);
    QSignalSpy lost(h.get(), &AiSidecarHost::lost);
    const auto result = call(*h, "echo");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().error, SidecarError::SidecarLost);
    EXPECT_EQ(lost.count(), 1);
    EXPECT_EQ(h->state(), AiSidecarHost::State::Idle);
}

TEST_F(AiSidecarHostTest, RefusesASidecarFromAnotherBuildUntilReset)
{
    auto o = options();
    o.buildId = "0.1.0+other";
    auto h = host(o);
    QSignalSpy refused(h.get(), &AiSidecarHost::refused);
    auto result = call(*h, "echo");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().error, SidecarError::Refused);
    EXPECT_EQ(refused.count(), 1);
    EXPECT_TRUE(refused.at(0).at(0).toString().contains(QStringLiteral("build id")));
    result = call(*h, "echo");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().error, SidecarError::Refused);
    EXPECT_EQ(h->processId(), 0);
}

TEST_F(AiSidecarHostTest, RefusesAnotherProtocolVersion)
{
    auto h = host(options(QStringLiteral("old-protocol")));
    QSignalSpy refused(h.get(), &AiSidecarHost::refused);
    const auto result = call(*h, "echo");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().error, SidecarError::Refused);
    ASSERT_EQ(refused.count(), 1);
    EXPECT_TRUE(refused.at(0).at(0).toString().contains(QStringLiteral("protocol")));
}

TEST_F(AiSidecarHostTest, ANonJsonStdoutLineRestartsTheSidecar)
{
    auto h = host(options());
    ASSERT_TRUE(call(*h, "echo"));
    const qint64 first = h->processId();
    const auto broken = call(*h, "garbage");
    ASSERT_FALSE(broken);
    EXPECT_EQ(broken.error().error, SidecarError::SidecarLost);
    EXPECT_TRUE(waitFor([&] { return !processAlive(first); }));
    ASSERT_TRUE(call(*h, "echo"));
    EXPECT_NE(h->processId(), first);
}

TEST_F(AiSidecarHostTest, ACrashMidCallFailsEveryRequestInFlightWithSidecarLost)
{
    auto h = host(options());
    Call hanging;
    h->request("hang", {}, hanging.reply());
    const auto crashed = call(*h, "crash");
    ASSERT_FALSE(crashed);
    EXPECT_EQ(crashed.error().error, SidecarError::SidecarLost);
    ASSERT_TRUE(waitFor([&] { return hanging.done(); }));
    EXPECT_EQ(hanging.replies, 1);
    EXPECT_EQ(hanging.result->error().error, SidecarError::SidecarLost);
    EXPECT_EQ(h->outstanding(), 0u);
}

TEST_F(AiSidecarHostTest, RestartsWaitLongerAfterEachCrash)
{
    auto o = options();
    o.firstBackoffMs = 300;
    auto h = host(o);
    ASSERT_FALSE(call(*h, "crash"));
    QElapsedTimer t;
    t.start();
    ASSERT_TRUE(call(*h, "echo"));
    EXPECT_GE(t.elapsed(), 250);
}

TEST_F(AiSidecarHostTest, ThreeCrashesInAMinuteStopTheRestartsAndKeepTheLog)
{
    auto h = host(options());
    QSignalSpy stopped(h.get(), &AiSidecarHost::stopped);
    for (int i = 0; i < 3; ++i)
        ASSERT_FALSE(call(*h, "crash"));
    EXPECT_EQ(h->state(), AiSidecarHost::State::Stopped);
    ASSERT_EQ(stopped.count(), 1);
    EXPECT_TRUE(stopped.at(0).at(0).toByteArray().contains("crashing on request"));
    const auto after = call(*h, "echo");
    ASSERT_FALSE(after);
    EXPECT_EQ(after.error().error, SidecarError::Stopped);
    EXPECT_EQ(h->processId(), 0);
    h->reset();
    EXPECT_TRUE(call(*h, "echo"));
}

TEST_F(AiSidecarHostTest, ShutdownClosesStdinAndASidecarThatExitsIsNotKilled)
{
    auto o = options();
    o.shutdownMs = 5'000;
    auto h = host(o);
    ASSERT_TRUE(call(*h, "echo"));
    QSignalSpy finished(h.get(), &AiSidecarHost::finished);
    QElapsedTimer t;
    t.start();
    h->shutdown();
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_LT(t.elapsed(), 4'000);
    EXPECT_EQ(h->state(), AiSidecarHost::State::Idle);
}

TEST_F(AiSidecarHostTest, ShutdownKillsASidecarThatIgnoresStdinAndItsChildren)
{
    auto h = host(options(QStringLiteral("ignore-eof")));
    const auto spawned = call(*h, "spawn");
    ASSERT_TRUE(spawned);
    const qint64 child = spawned->find("pid")->asInt();
    ASSERT_GT(child, 0);
    const qint64 sidecar = h->processId();
    EXPECT_TRUE(processAlive(child));
    QSignalSpy finished(h.get(), &AiSidecarHost::finished);
    QElapsedTimer t;
    t.start();
    h->shutdown();
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_GE(t.elapsed(), 550);
    EXPECT_TRUE(waitFor([&] { return !processAlive(sidecar) && !processAlive(child); }, 3'000));
}

TEST_F(AiSidecarHostTest, DestroyingTheHostTakesTheProcessTreeWithIt)
{
    auto h = host(options(QStringLiteral("ignore-eof")));
    const auto spawned = call(*h, "spawn");
    ASSERT_TRUE(spawned);
    const qint64 child = spawned->find("pid")->asInt();
    const qint64 sidecar = h->processId();
    h.reset();
    EXPECT_TRUE(waitFor([&] { return !processAlive(sidecar) && !processAlive(child); }, 3'000));
}

TEST_F(AiSidecarHostTest, TheSidecarGetsACleanedEnvironmentAndAPrivateWorkingDirectory)
{
    auto o = options();
    o.environment.insert(QStringLiteral("ANTHROPIC_BASE_URL"), QStringLiteral("http://evil.example"));
    o.environment.insert(QStringLiteral("ANTHROPIC_API_KEY"), QString::fromLatin1(kSentinel));
    o.environment.insert(QStringLiteral("OPENAI_API_KEY"), QString::fromLatin1(kSentinel));
    o.environment.insert(QStringLiteral("OPENAI_BASE_URL"), QStringLiteral("http://evil.example"));
    o.environment.insert(QStringLiteral("GEMINI_API_KEY"), QString::fromLatin1(kSentinel));
    o.environment.insert(QStringLiteral("GOOGLE_GEMINI_BASE_URL"), QStringLiteral("http://evil.example"));
    o.environment.insert(QStringLiteral("OTEL_EXPORTER_OTLP_ENDPOINT"), QStringLiteral("http://evil.example"));
    o.environment.insert(QStringLiteral("NODE_OPTIONS"), QStringLiteral("--require evil.js"));
    o.environment.insert(QStringLiteral("SOMEVENDOR_BASE_URL"), QStringLiteral("http://evil.example"));
    o.environment.insert(QStringLiteral("SOMEVENDOR_API_KEY"), QString::fromLatin1(kSentinel));
    auto h = host(o);
    const auto result = call(*h, "environment");
    ASSERT_TRUE(result);
    const Json &env = *result->find("environment");
    for (const auto &member : env.asObject()) {
        const QString name = QString::fromStdString(member.key).toUpper();
        EXPECT_FALSE(name.startsWith(QStringLiteral("ANTHROPIC_")) || name.startsWith(QStringLiteral("OPENAI_")) ||
                     name.startsWith(QStringLiteral("GEMINI_")) || name.startsWith(QStringLiteral("GOOGLE_")) ||
                     name.startsWith(QStringLiteral("OTEL_")) || name == QStringLiteral("NODE_OPTIONS") ||
                     name.endsWith(QStringLiteral("_BASE_URL")) || name.endsWith(QStringLiteral("_API_KEY")))
            << member.key;
    }
    EXPECT_TRUE(env.find("PATH") || env.find("Path"));
    const QString cwd = QString::fromStdString(result->find("cwd")->asString());
    EXPECT_EQ(QFileInfo(cwd).canonicalFilePath(), QFileInfo(o.workingDirectory).canonicalFilePath());
#ifndef _WIN32
    EXPECT_EQ(QFileInfo(o.workingDirectory).permissions() & (QFile::ReadGroup | QFile::ReadOther), 0);
#endif
}

TEST_F(AiSidecarHostTest, ASandboxedSidecarAnswersKeepsItsTemporaryFilesInItsWorkFolderAndDiesWithTheHost)
{
    auto o = options(QStringLiteral("ignore-eof"));
    hikari::backends::sandbox::Profile profile;
    profile.name = QStringLiteral("HikariSub.Test.Sidecar");
    profile.readExecute = {QFileInfo(QStringLiteral(HIKARI_FAKE_AI_SIDECAR)).absolutePath()};
    profile.readWrite = {o.workingDirectory};
    o.sandbox = profile;
    auto h = host(o);
    const auto result = call(*h, "environment");
    ASSERT_TRUE(result) << result.error().message << h->diagnostics().toStdString();
    const Json &env = *result->find("environment");
    const QString temporary = QDir::toNativeSeparators(o.workingDirectory + QStringLiteral("/tmp"));
    for (const char *name : {"TMPDIR", "TEMP", "TMP"}) {
        ASSERT_TRUE(env.find(name)) << name;
        EXPECT_EQ(QString::fromStdString(env.find(name)->asString()), temporary) << name;
    }
    EXPECT_TRUE(QFileInfo(temporary).isDir());
    const auto spawned = call(*h, "spawn");
    ASSERT_TRUE(spawned);
    const qint64 child = spawned->find("pid")->asInt();
    const qint64 sidecar = h->processId();
    h.reset();
    EXPECT_TRUE(waitFor([&] { return !processAlive(sidecar) && !processAlive(child); }, 3'000));
}

QStringList g_log;
void collectLog(QtMsgType, const QMessageLogContext &, const QString &message)
{
    g_log << message;
}

TEST_F(AiSidecarHostTest, ASentinelKeyInQtsEnvironmentNeverReachesTheSidecarOrTheLogs)
{
    g_log.clear();
    const auto previous = qInstallMessageHandler(collectLog);
    auto o = options();
    o.environment.insert(QStringLiteral("OPENAI_API_KEY"), QString::fromLatin1(kSentinel));
    o.environment.insert(QStringLiteral("HIKARI_PROBE"), QString::fromLatin1(kSentinel));
    auto h = host(o);
    const auto result = call(*h, "environment");
    ASSERT_TRUE(call(*h, "stderr", Json("diagnostics line\n")));
    ASSERT_FALSE(call(*h, "crash"));
    qInstallMessageHandler(previous);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->dump().find(kSentinel), std::string::npos);
    EXPECT_FALSE(h->diagnostics().contains(kSentinel));
    EXPECT_TRUE(h->diagnostics().contains("crashing on request"));
    EXPECT_FALSE(g_log.join(QLatin1Char('\n')).contains(QString::fromLatin1(kSentinel)));
}

TEST_F(AiSidecarHostTest, ApiCallRoundTripsAnHa1ReadAndAWriteUnderTheSidecarsCaller)
{
    hikari::api::testing::FakeHost fake;
    hikari::api::Api api(fake.host());
    auto h = host(options(), &api);
    const auto read = call(*h, "api", Json::object({{"operation", "documents.get"}}));
    ASSERT_TRUE(read);
    const Json *document = read->find("result");
    ASSERT_TRUE(document) << read->dump();
    EXPECT_EQ(*document->find("lineCount"), Json(3));
    const Json revision = *document->find("revision");

    const auto apply = Json::object({{"operation", "document.apply"},
        {"params", Json::object({{"revision", revision}, {"name", "Fix"},
            {"edits", Json::array({Json::object({{"op", "set_line"}, {"line", 1},
                {"fields", Json::object({{"text", "Hi"}})}})})}})}});
    const auto write = call(*h, "api", apply);
    ASSERT_TRUE(write);
    ASSERT_TRUE(write->find("result")) << write->dump();
    EXPECT_EQ(*write->find("result")->find("changed"), Json(true));
    EXPECT_EQ(fake.target()->document().lines()[0]->text, u8"Hi");

    const auto stale = call(*h, "api", apply);
    ASSERT_TRUE(stale);
    const Json *error = stale->find("error");
    ASSERT_TRUE(error) << stale->dump();
    EXPECT_EQ(*error->find("code"), Json(rpc::kHikariApiError));
    EXPECT_EQ(*error->find("data")->find("code"), Json(hikari::api::errors::kStaleRevision));
}

TEST_F(AiSidecarHostTest, ApiCallWithoutTheApiAnswersUnavailable)
{
    auto h = host(options());
    const auto result = call(*h, "api", Json::object({{"operation", "documents.get"}}));
    ASSERT_TRUE(result);
    EXPECT_EQ(*result->find("error")->find("data")->find("code"), Json(hikari::api::errors::kUnavailable));
}

TEST_F(AiSidecarHostTest, ACancelledRequestGetsExactlyOneReply)
{
    auto h = host(options());
    Call hanging;
    const auto id = h->request("hang", {}, hanging.reply());
    ASSERT_TRUE(waitFor([&] { return h->state() == AiSidecarHost::State::Ready; }));
    h->cancel(id);
    h->cancel(id);
    ASSERT_TRUE(waitFor([&] { return hanging.done(); }));
    ASSERT_TRUE(call(*h, "echo"));
    EXPECT_EQ(hanging.replies, 1);
    EXPECT_EQ(hanging.result->error().error, SidecarError::Cancelled);
}

TEST_F(AiSidecarHostTest, ARequestCancelledBeforeTheSidecarIsReadyIsNeverSent)
{
    auto h = host(options());
    Call queued;
    const auto id = h->request("crash", {}, queued.reply());
    h->cancel(id);
    EXPECT_EQ(queued.replies, 1);
    EXPECT_EQ(queued.result->error().error, SidecarError::Cancelled);
    EXPECT_TRUE(call(*h, "echo"));
    EXPECT_EQ(h->state(), AiSidecarHost::State::Ready);
}

TEST_F(AiSidecarHostTest, RequestsOverTheOutstandingLimitAreBusy)
{
    auto o = options();
    o.maxOutstanding = 2;
    auto h = host(o);
    Call a, b;
    h->request("hang", {}, a.reply());
    h->request("hang", {}, b.reply());
    const auto third = call(*h, "echo");
    ASSERT_FALSE(third);
    EXPECT_EQ(third.error().error, SidecarError::Busy);
}

TEST_F(AiSidecarHostTest, NotificationsReachTheHandler)
{
    auto h = host(options());
    std::vector<std::pair<std::string, Json>> seen;
    h->onNotification([&](const std::string &method, const Json &params) { seen.emplace_back(method, params); });
    ASSERT_TRUE(call(*h, "notify", Json::object({{"text", "hi"}})));
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].first, "session/update");
    EXPECT_EQ(seen[0].second, Json::object({{"text", "hi"}}));
}

}
