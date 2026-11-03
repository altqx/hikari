#include "hikari/backends/plugin_process.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include <fstream>

using hikari::backends::PluginProcess;
namespace plugin = hikari::backends::plugin;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

}

class PluginProcessTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "plugin_process_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    QTemporaryDir m_dir;
    std::vector<std::string> m_notices;
    std::vector<std::string> m_calls;
    int m_slowReplyMs = 0;

    std::string plugin(const std::string &name, const std::string &main)
    {
        const std::string root = m_dir.filePath(QString::fromStdString(name)).toStdString();
        QDir().mkpath(QString::fromStdString(root));
        std::ofstream(root + "/main.lua", std::ios::binary) << main;
        return root;
    }

    std::unique_ptr<PluginProcess> process(const std::string &root, PluginProcess::Limits limits = {},
                                           plugin::Tier tier = plugin::Tier::Restricted)
    {
        auto load = [root, tier] {
            plugin::LoadRequest request;
            request.root = root;
            request.main = "main.lua";
            request.id = "com.example.test";
            request.version = "1.0.0";
            request.tier = tier;
            request.memoryLimit = 64 << 20;
            request.settings = "{}";
            request.commands = {"greet", "spin", "wait", "crash"};
            return request;
        };
        auto p = std::make_unique<PluginProcess>(QStringLiteral(HIKARI_PLUGIN_HELPER), load, limits);
        p->setServiceHandler([this](const PluginProcess::ServiceRequest &request, PluginProcess::ServiceReply reply) {
            if (request.service == plugin::Service::Notify) {
                m_notices.push_back(request.text);
                reply({});
            } else if (request.service == plugin::Service::Call) {
                m_calls.push_back(request.name + " " + request.json);
                const std::string answer = R"({"result": {"version": 1}})";
                if (m_slowReplyMs > 0)
                    QTimer::singleShot(m_slowReplyMs, [reply, answer] { reply(answer); });
                else
                    reply(answer);
            } else {
                reply({});
            }
        });
        return p;
    }
};

TEST_F(PluginProcessTest, ACommandRunsInTheHelperAndCallsHikariSub)
{
    auto p = process(plugin("hello", "hikari.commands.register('greet', function() "
                                     "hikari.notify('v' .. hikari.call('api.version').version) end)"));
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("greet"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_EQ(finished.at(0).at(0).toString(), QStringLiteral("greet"));
    EXPECT_EQ(p->state(), PluginProcess::State::Ready);
    EXPECT_EQ(p->handledCommands(), std::vector<std::string>{"greet"});
    EXPECT_EQ(m_notices, std::vector<std::string>{"v1"});
    EXPECT_EQ(m_calls, std::vector<std::string>{"api.version {}"});
}

TEST_F(PluginProcessTest, TheWatchdogStopsARunawayCommandAndTheNextRunStartsAgain)
{
    auto p = process(plugin("spin", "hikari.commands.register('spin', function() while true do end end) "
                                    "hikari.commands.register('greet', function() hikari.notify('back') end)"),
                     {.timeLimitMs = 300});
    QSignalSpy stopped(p.get(), &PluginProcess::stopped);
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("spin"));
    ASSERT_TRUE(waitFor([&] { return stopped.count() == 1; }));
    EXPECT_EQ(stopped.at(0).at(0).value<PluginProcess::StopReason>(), PluginProcess::StopReason::TimeLimit);
    EXPECT_EQ(p->state(), PluginProcess::State::Stopped);
    ASSERT_TRUE(p->run("greet"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_EQ(m_notices, std::vector<std::string>{"back"});
}

TEST_F(PluginProcessTest, TimeHikariSubSpendsAnsweringIsNotCounted)
{
    m_slowReplyMs = 700;
    auto p = process(plugin("wait", "hikari.commands.register('wait', function() hikari.call('api.version') end)"),
                     {.timeLimitMs = 400});
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    QSignalSpy stopped(p.get(), &PluginProcess::stopped);
    ASSERT_TRUE(p->run("wait"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1 || stopped.count() == 1; }));
    EXPECT_EQ(finished.count(), 1);
    EXPECT_EQ(stopped.count(), 0);
}

TEST_F(PluginProcessTest, ForceStopEndsARunningCommand)
{
    auto p = process(plugin("spin", "hikari.commands.register('spin', function() while true do end end)"));
    QSignalSpy stopped(p.get(), &PluginProcess::stopped);
    ASSERT_TRUE(p->run("spin"));
    ASSERT_TRUE(waitFor([&] { return p->busy() && p->state() == PluginProcess::State::Ready; }));
    p->forceStop();
    ASSERT_EQ(stopped.count(), 1);
    EXPECT_EQ(stopped.at(0).at(0).value<PluginProcess::StopReason>(), PluginProcess::StopReason::ForceStopped);
    EXPECT_FALSE(p->busy());
}

TEST_F(PluginProcessTest, ACrashTakesDownOnlyItsHelper)
{
    auto crashing = process(plugin("crash", "hikari.commands.register('crash', function() "
                                            "local ffi = require 'ffi'; ffi.cast('int *', 0)[0] = 1 end)"),
                            {}, plugin::Tier::Unrestricted);
    auto steady = process(plugin("steady", "hikari.commands.register('greet', function() hikari.notify('fine') end)"));
    QSignalSpy stopped(crashing.get(), &PluginProcess::stopped);
    QSignalSpy finished(steady.get(), &PluginProcess::finished);
    ASSERT_TRUE(steady->run("greet"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    const qint64 steadyPid = steady->processId();
    ASSERT_TRUE(crashing->run("crash"));
    ASSERT_TRUE(waitFor([&] { return stopped.count() == 1; }));
    EXPECT_EQ(stopped.at(0).at(0).value<PluginProcess::StopReason>(), PluginProcess::StopReason::Crashed);
    ASSERT_TRUE(steady->run("greet"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 2; }));
    EXPECT_EQ(steady->processId(), steadyPid);
    EXPECT_EQ(m_notices, (std::vector<std::string>{"fine", "fine"}));
}

TEST_F(PluginProcessTest, EventsWaitBehindACommandAndTheNewestOfANameWins)
{
    m_slowReplyMs = 300;
    auto p = process(plugin("events", "hikari.events.on('document.changed', function(e) hikari.notify('seen ' .. e.n) end) "
                                      "hikari.commands.register('wait', function() hikari.call('api.version') end)"));
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("wait"));
    ASSERT_TRUE(waitFor([&] { return p->busy() && p->state() == PluginProcess::State::Ready; }));
    for (int n = 1; n <= 3; ++n)
        ASSERT_TRUE(p->push("document.changed", "{\"n\": " + std::to_string(n) + "}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1 && !p->busy(); }));
    ASSERT_TRUE(waitFor([&] { return m_notices.size() == 1; }));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    EXPECT_EQ(m_notices, std::vector<std::string>{"seen 3"});
}

TEST_F(PluginProcessTest, DeliveredEventsMergeOnlyUnderTheSameKey)
{
    m_slowReplyMs = 300;
    auto p = process(plugin("keys", "hikari.events.on('document.changed', function(e) hikari.notify(e.n) end) "
                                    "hikari.commands.register('wait', function() hikari.call('api.version') end)"));
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("wait"));
    ASSERT_TRUE(waitFor([&] { return p->busy() && p->state() == PluginProcess::State::Ready; }));
    ASSERT_TRUE(p->deliver("document.changed", R"({"n": "click a"})", {}));
    ASSERT_TRUE(p->deliver("document.changed", R"({"n": "click b"})", {}));
    ASSERT_TRUE(p->deliver("document.changed", R"({"n": "move 1"})", "move"));
    ASSERT_TRUE(p->deliver("document.changed", R"({"n": "move 2"})", "move"));
    ASSERT_TRUE(waitFor([&] { return m_notices.size() == 3; }));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    EXPECT_EQ(m_notices, (std::vector<std::string>{"click a", "click b", "move 2"}));
}

TEST_F(PluginProcessTest, ALoadErrorIsReportedAndStopsFurtherRuns)
{
    auto p = process(plugin("broken", "this is not lua"));
    QSignalSpy failed(p.get(), &PluginProcess::loadFailed);
    p->start();
    ASSERT_TRUE(waitFor([&] { return failed.count() == 1; }));
    EXPECT_TRUE(p->loadError().startsWith(QStringLiteral("main.lua:1:"))) << p->loadError().toStdString();
    EXPECT_EQ(p->state(), PluginProcess::State::LoadFailed);
    EXPECT_FALSE(p->run("greet"));
}

TEST_F(PluginProcessTest, StartTriesAgainAfterAFailedLoad)
{
    const std::string root = plugin("fixed", "this is not lua");
    auto p = process(root);
    QSignalSpy failed(p.get(), &PluginProcess::loadFailed);
    QSignalSpy loaded(p.get(), &PluginProcess::loaded);
    p->start();
    ASSERT_TRUE(waitFor([&] { return failed.count() == 1; }));
    plugin("fixed", "hikari.notify('back')");
    p->start();
    ASSERT_TRUE(waitFor([&] { return loaded.count() == 1; }));
    EXPECT_EQ(p->state(), PluginProcess::State::Ready);
    EXPECT_EQ(m_notices, std::vector<std::string>{"back"});
}

TEST_F(PluginProcessTest, AnErrorInACommandIsReportedAndThePluginKeepsRunning)
{
    auto p = process(plugin("err", "hikari.commands.register('greet', function() error('nope') end)"));
    QSignalSpy failed(p.get(), &PluginProcess::commandFailed);
    ASSERT_TRUE(p->run("greet"));
    ASSERT_TRUE(waitFor([&] { return failed.count() == 1; }));
    EXPECT_TRUE(failed.at(0).at(1).toString().contains(QStringLiteral("main.lua:1: nope")));
    EXPECT_EQ(p->state(), PluginProcess::State::Ready);
}
