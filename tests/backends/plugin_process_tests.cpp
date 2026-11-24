#include "hikari/backends/plugin_process.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include <algorithm>
#include <expected>
#include <fstream>
#include <optional>

using hikari::backends::PluginProcess;
using hikari::backends::pluginProfile;
namespace plugin = hikari::backends::plugin;
namespace sandbox = hikari::backends::sandbox;

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
    bool m_holdReplies = false;
    std::vector<PluginProcess::ServiceReply> m_held;
    std::string m_id = "com.example.test";

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
        auto load = [root, tier, id = m_id] {
            plugin::LoadRequest request;
            request.root = root;
            request.main = "main.lua";
            request.id = id;
            request.version = "1.0.0";
            request.tier = tier;
            request.memoryLimit = 64 << 20;
            request.settings = "{}";
            request.commands = {"greet", "spin", "wait", "crash", "scan"};
            request.exporters = {"csv", "bin", "spin"};
            request.importers = {"csv"};
            request.providers = {"completion"};
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
                if (m_holdReplies)
                    m_held.push_back(reply);
                else if (m_slowReplyMs > 0)
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
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_EQ(finished.at(0).at(0).toString(), QStringLiteral("greet"));
    EXPECT_EQ(p->state(), PluginProcess::State::Ready);
    EXPECT_EQ(p->handledCommands(), std::vector<std::string>{"greet"});
    EXPECT_EQ(m_notices, std::vector<std::string>{"v1"});
    EXPECT_EQ(m_calls, std::vector<std::string>{"api.version {}"});
}

TEST_F(PluginProcessTest, HikariVcsCallsTheGitOperationsForTheDocument)
{
    auto p = process(plugin("git", "hikari.commands.register('greet', function() "
                                   "local refs = hikari.vcs.refs({limit = 5}) "
                                   "local _, shown = hikari.vcs.show('v1', 3) "
                                   "hikari.notify(refs.version .. ' ' .. shown.version) "
                                   "hikari.notify(tostring(pcall(hikari.vcs.show, ''))) end)"));
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_EQ(m_calls, (std::vector<std::string>{R"(vcs.refs {"limit":5})", R"(vcs.show {"document":3,"ref":"v1"})"}));
    EXPECT_EQ(m_notices, (std::vector<std::string>{"1 1", "false"}));
}

TEST_F(PluginProcessTest, HikariPresenceSetsAndClearsTheText)
{
    auto p = process(plugin("tracker", "hikari.commands.register('greet', function() "
                                       "hikari.presence.set_text('Ep 5') "
                                       "hikari.presence.set_text(nil) "
                                       "hikari.notify(tostring(pcall(hikari.presence.set_text, 5))) end)"));
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_EQ(m_calls, (std::vector<std::string>{R"(presence.set_text {"text":"Ep 5"})", R"(presence.set_text {"text":""})"}));
    EXPECT_EQ(m_notices, std::vector<std::string>{"false"});
}

TEST_F(PluginProcessTest, ALongCommandIsStillWorkingAndRunsOnUntilForceStopped)
{
    auto p = process(plugin("spin", "hikari.commands.register('spin', function() while true do end end) "
                                    "hikari.commands.register('greet', function() hikari.notify('back') end)"),
                     {.stillWorkingMs = 300});
    QSignalSpy stopped(p.get(), &PluginProcess::stopped);
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("spin", "{}"));
    ASSERT_TRUE(waitFor([&] { return p->stillWorking(); }));
    EXPECT_EQ(p->work(), QStringLiteral("spin"));
    waitFor([] { return false; }, 700);
    EXPECT_EQ(stopped.count(), 0);
    EXPECT_TRUE(p->busy());
    p->forceStop();
    EXPECT_FALSE(p->stillWorking());
    EXPECT_EQ(p->state(), PluginProcess::State::Stopped);
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_EQ(m_notices, std::vector<std::string>{"back"});
}

TEST_F(PluginProcessTest, TimeHikariSubSpendsAnsweringIsNotCounted)
{
    m_slowReplyMs = 700;
    auto p = process(plugin("wait", "hikari.commands.register('wait', function() hikari.call('api.version') end)"),
                     {.stillWorkingMs = 400});
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    bool stillWorking = false;
    QObject::connect(p.get(), &PluginProcess::workChanged, [&] { stillWorking = stillWorking || p->stillWorking(); });
    ASSERT_TRUE(p->run("wait", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_FALSE(stillWorking);
}

TEST_F(PluginProcessTest, ForceStopEndsARunningCommand)
{
    auto p = process(plugin("spin", "hikari.commands.register('spin', function() while true do end end)"));
    QSignalSpy stopped(p.get(), &PluginProcess::stopped);
    ASSERT_TRUE(p->run("spin", "{}"));
    ASSERT_TRUE(waitFor([&] { return p->busy() && p->state() == PluginProcess::State::Ready; }));
    p->forceStop();
    ASSERT_EQ(stopped.count(), 1);
    EXPECT_EQ(stopped.at(0).at(0).value<PluginProcess::StopReason>(), PluginProcess::StopReason::ForceStopped);
    EXPECT_FALSE(p->busy());
}

TEST_F(PluginProcessTest, AJobReportsProgressAndEndsWhenCancelled)
{
    m_id = "example.chains";
    auto p = process(plugin("job", R"(
        hikari.commands.register('scan', function()
            assert(hikari.job.begin{title = 'Scanning'})
            local step = 0
            while not hikari.job.cancelled() do
                step = step + 1
                hikari.job.progress(step % 2 == 0 and 0.5 or nil, 'step')
                for _ = 1, 200000 do end
            end
            hikari.notify('cancelled after ' .. (step > 0 and 'steps' or 'nothing'))
        end))"),
                     {.stillWorkingMs = 300});
    QSignalSpy began(p.get(), &PluginProcess::jobBegan);
    QSignalSpy progress(p.get(), &PluginProcess::jobProgress);
    QSignalSpy ended(p.get(), &PluginProcess::jobEnded);
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    QSignalSpy stopped(p.get(), &PluginProcess::stopped);
    ASSERT_TRUE(p->run("scan", "{}"));
    ASSERT_TRUE(waitFor([&] { return began.count() == 1; }));
    EXPECT_EQ(began.at(0).at(0).toString(), QStringLiteral("scan"));
    EXPECT_EQ(began.at(0).at(1).toString(), QStringLiteral("Scanning"));
    EXPECT_TRUE(p->inJob());
    waitFor([] { return false; }, 900);
    EXPECT_EQ(stopped.count(), 0);
    ASSERT_GE(progress.count(), 2);
    const std::vector<double> fractions{progress.at(0).at(0).toDouble(), progress.at(1).at(0).toDouble()};
    EXPECT_EQ(fractions, (std::vector<double>{-1.0, 0.5}));
    EXPECT_EQ(progress.at(0).at(1).toString(), QStringLiteral("step"));
    p->cancelJob();
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_EQ(ended.count(), 1);
    EXPECT_FALSE(p->inJob());
    EXPECT_EQ(m_notices, std::vector<std::string>{"cancelled after steps"});
}

TEST_F(PluginProcessTest, OnlyACommandBecomesAJob)
{
    auto p = process(plugin("early", "local ok, err = hikari.job.begin{title = 'x'} hikari.notify(tostring(ok) .. ' ' .. err.message) "
                                     "hikari.commands.register('greet', function() hikari.notify(tostring(hikari.job.progress(1))) end)"));
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    ASSERT_EQ(m_notices.size(), 2u);
    EXPECT_EQ(m_notices[0],
              "nil Only a command, a timer callback, a format call or a click in the plugin's own panel or dialog can become a job.");
    EXPECT_EQ(m_notices[1], "nil");
}

TEST_F(PluginProcessTest, AClickInItsOwnPanelBecomesAJob)
{
    auto p = process(plugin("panel", R"(
        hikari.commands.register('greet', function() end)
        hikari.panels.on('scan', function(e)
            local ok, err = hikari.job.begin{title = 'Scanning'}
            hikari.notify(e.kind .. ' ' .. tostring(ok) .. ' ' .. (err and err.message or ''))
        end))"),
                     {.stillWorkingMs = 300});
    QSignalSpy began(p.get(), &PluginProcess::jobBegan);
    QSignalSpy ended(p.get(), &PluginProcess::jobEnded);
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return p->state() == PluginProcess::State::Ready && !p->busy(); }));
    ASSERT_TRUE(p->deliver("panel:scan", R"({"kind": "click", "widget": "go", "panel": "scan", "values": {}})", {}));
    ASSERT_TRUE(waitFor([&] { return m_notices.size() == 1; }));
    EXPECT_EQ(m_notices[0], "click true ");
    ASSERT_EQ(began.count(), 1);
    EXPECT_EQ(began.at(0).at(1).toString(), QStringLiteral("Scanning"));
    ASSERT_TRUE(waitFor([&] { return ended.count() == 1; }));
    EXPECT_FALSE(p->inJob());
}

TEST_F(PluginProcessTest, OtherEventsDontBecomeAJob)
{
    auto p = process(plugin("events", R"(
        hikari.commands.register('greet', function() end)
        local function try(e)
            local ok, err = hikari.job.begin{title = 'Scanning'}
            hikari.notify(tostring(ok) .. ' ' .. (err and err.message or ''))
        end
        hikari.panels.on('scan', try)
        hikari.events.on('document.changed', try))"));
    QSignalSpy began(p.get(), &PluginProcess::jobBegan);
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return p->state() == PluginProcess::State::Ready && !p->busy(); }));
    ASSERT_TRUE(p->deliver("panel:scan", R"({"kind": "change", "widget": "gap", "value": 3, "panel": "scan", "values": {}})", {}));
    ASSERT_TRUE(p->deliver("panel:scan", R"({"kind": "show", "panel": "scan", "values": {}})", {}));
    ASSERT_TRUE(p->deliver("document.changed", R"({"kind": "click"})", {}));
    ASSERT_TRUE(waitFor([&] { return m_notices.size() == 3; }));
    const std::string no =
        "nil Only a command, a timer callback, a format call or a click in the plugin's own panel or dialog can become a job.";
    EXPECT_EQ(m_notices, (std::vector<std::string>{no, no, no}));
    EXPECT_EQ(began.count(), 0);
}

TEST_F(PluginProcessTest, MarksGoThroughTheHikariApi)
{
    auto p = process(plugin("marks", "hikari.commands.register('greet', function() "
                                     "hikari.marks.set({{line = 2, colour = 'warning', tooltip = 'Changed'}}) "
                                     "hikari.marks.clear({2}) hikari.marks.list() end)"));
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    ASSERT_EQ(m_calls.size(), 3u);
    EXPECT_EQ(m_calls[0], R"(marks.set {"marks":[{"colour":"warning","line":2,"tooltip":"Changed"}]})");
    EXPECT_EQ(m_calls[1], R"(marks.clear {"lines":[2]})");
    EXPECT_EQ(m_calls[2], "marks.list {}");
}

TEST_F(PluginProcessTest, ProblemsGoThroughTheHikariApi)
{
    auto p = process(plugin("problems", "hikari.commands.register('greet', function() "
                                        "hikari.problems.set({{line = 2, severity = 'warning', message = 'Odd'}}, "
                                        "{revision = 4, set = 'grammar'}) "
                                        "hikari.problems.list({severity = 'warning', lines = {2}}) "
                                        "hikari.problems.fix('p1', 0) hikari.problems.clear({set = 'grammar'}) end)"));
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_EQ(m_calls, (std::vector<std::string>{
                           R"(problems.set {"problems":[{"line":2,"message":"Odd","severity":"warning"}],"revision":4,"set":"grammar"})",
                           R"(problems.list {"lines":[2],"severity":"warning"})", R"(problems.fix {"fix":0,"problem":"p1"})",
                           R"(problems.clear {"set":"grammar"})"}));
}

TEST_F(PluginProcessTest, ForceStopEndsAJobThatIgnoresCancel)
{
    m_id = "example.chains";
    auto p = process(plugin("stuck", "hikari.commands.register('scan', function() "
                                     "hikari.job.begin{title = 'Stuck'} while true do end end)"),
                     {.stillWorkingMs = 300});
    QSignalSpy began(p.get(), &PluginProcess::jobBegan);
    QSignalSpy ended(p.get(), &PluginProcess::jobEnded);
    QSignalSpy stopped(p.get(), &PluginProcess::stopped);
    ASSERT_TRUE(p->run("scan", "{}"));
    ASSERT_TRUE(waitFor([&] { return began.count() == 1; }));
    p->cancelJob();
    waitFor([] { return false; }, 700);
    EXPECT_EQ(stopped.count(), 0);
    EXPECT_TRUE(p->inJob());
    p->forceStop();
    ASSERT_EQ(stopped.count(), 1);
    EXPECT_EQ(stopped.at(0).at(0).value<PluginProcess::StopReason>(), PluginProcess::StopReason::ForceStopped);
    EXPECT_EQ(ended.count(), 1);
    EXPECT_FALSE(p->inJob());
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
    ASSERT_TRUE(steady->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    const qint64 steadyPid = steady->processId();
    ASSERT_TRUE(crashing->run("crash", "{}"));
    ASSERT_TRUE(waitFor([&] { return stopped.count() == 1; }));
    EXPECT_EQ(stopped.at(0).at(0).value<PluginProcess::StopReason>(), PluginProcess::StopReason::Crashed);
    ASSERT_TRUE(steady->run("greet", "{}"));
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
    ASSERT_TRUE(p->run("wait", "{}"));
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
    ASSERT_TRUE(p->run("wait", "{}"));
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
    EXPECT_FALSE(p->run("greet", "{}"));
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
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return failed.count() == 1; }));
    EXPECT_TRUE(failed.at(0).at(1).toString().contains(QStringLiteral("main.lua:1: nope")));
    EXPECT_EQ(p->state(), PluginProcess::State::Ready);
}

TEST_F(PluginProcessTest, ThePluginProfileReachesOnlyTheHelperAndThePackage)
{
    const QString helper = QStringLiteral(HIKARI_PLUGIN_HELPER);
    const std::string id(120, 'a');
    const sandbox::Profile profile = pluginProfile(helper, QStringLiteral("/plugins/x"), id);
    EXPECT_LE(profile.name.size(), 64);
    EXPECT_TRUE(QRegularExpression(QStringLiteral("^HikariSub\\.Plugin\\.[0-9a-f]{32}$")).match(profile.name).hasMatch())
        << profile.name.toStdString();
    EXPECT_EQ(profile.name, pluginProfile(helper, QStringLiteral("/elsewhere"), id).name);
    EXPECT_NE(profile.name, pluginProfile(helper, QStringLiteral("/plugins/x"), "com.example.other").name);
    EXPECT_EQ(profile.readExecute, QStringList{QFileInfo(helper).absolutePath()});
    EXPECT_EQ(profile.read, QStringList{QStringLiteral("/plugins/x")});
    EXPECT_TRUE(profile.readWrite.isEmpty());
    EXPECT_EQ(profile.network, sandbox::Network::None);
}

TEST_F(PluginProcessTest, AConfinedHelperLoadsAndRunsItsPlugin)
{
    const std::string root = plugin("confined", "hikari.commands.register('greet', function() "
                                                "hikari.notify(require('words').hello) end)");
    QDir().mkpath(QString::fromStdString(root + "/lua"));
    std::ofstream(root + "/lua/words.lua", std::ios::binary) << "return {hello = 'hi'}";
    auto p = process(root);
    p->setSandbox(pluginProfile(QStringLiteral(HIKARI_PLUGIN_HELPER), QString::fromStdString(root), "com.example.test"));
    EXPECT_TRUE(p->sandboxed());
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; })) << p->diagnostics().toStdString();
    EXPECT_EQ(m_notices, std::vector<std::string>{"hi"});
#ifdef Q_OS_LINUX
    const sandbox::Coverage coverage = sandbox::systemCoverage(sandbox::Network::None);
    if (!coverage.complete())
        GTEST_SKIP() << coverage.missing.toStdString();
    QFile status(QStringLiteral("/proc/%1/status").arg(p->processId()));
    ASSERT_TRUE(status.open(QIODevice::ReadOnly));
    const QString text = QString::fromLatin1(status.readAll());
    EXPECT_TRUE(text.contains(QRegularExpression(QStringLiteral("NoNewPrivs:\\s+1")))) << text.toStdString();
    EXPECT_TRUE(text.contains(QRegularExpression(QStringLiteral("Seccomp:\\s+2")))) << text.toStdString();
#endif
}

TEST_F(PluginProcessTest, ThePluginProfileRefusesWhatItLeavesOut)
{
    const std::string root = plugin("package", "return");
    const QString package = QString::fromStdString(root);
    const QString outside = m_dir.filePath(QStringLiteral("data.txt"));
    QFile secret(outside);
    ASSERT_TRUE(secret.open(QIODevice::WriteOnly));
    secret.write("x");
    secret.close();
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost));
    sandbox::Launcher launcher(pluginProfile(QStringLiteral(HIKARI_SANDBOX_PROBE), package, "com.example.test"));
    if (!launcher.coverage().complete())
        GTEST_SKIP() << launcher.coverage().missing.toStdString();
    const QString main = package + QStringLiteral("/main.lua");
    const QString fresh = package + QStringLiteral("/new.lua");
    QProcess probe;
    probe.setProgram(QStringLiteral(HIKARI_SANDBOX_PROBE));
    probe.setArguments({QStringLiteral("read"), main, QStringLiteral("read"), outside, QStringLiteral("write"), fresh,
                        QStringLiteral("tcp"), QStringLiteral("127.0.0.1"), QString::number(server.serverPort())});
    launcher.prepare(probe);
    probe.start();
    ASSERT_TRUE(probe.waitForFinished(20'000)) << probe.errorString().toStdString();
    const QStringList lines =
        QString::fromLocal8Bit(probe.readAllStandardOutput()).remove(QLatin1Char('\r')).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const std::string report = lines.join(QLatin1Char('\n')).toStdString();
    const auto denied = [&](const QString &action) {
        return std::ranges::any_of(lines, [&](const QString &line) { return line.startsWith(action + QStringLiteral(": denied ")); });
    };
    ASSERT_EQ(lines.size(), 4) << report;
    EXPECT_TRUE(lines.contains(QStringLiteral("read ") + main + QStringLiteral(": ok"))) << report;
    EXPECT_TRUE(denied(QStringLiteral("read ") + outside)) << report;
    EXPECT_TRUE(denied(QStringLiteral("write ") + fresh)) << report;
    EXPECT_TRUE(denied(QStringLiteral("tcp"))) << report;
}

namespace {

struct FormatResult {
    std::optional<std::expected<std::string, QString>> value;
    PluginProcess::FormatDone done()
    {
        return [this](std::expected<std::string, QString> v) { value = std::move(v); };
    }
};

}

TEST_F(PluginProcessTest, AnExporterTurnsTheSnapshotIntoTheFilesBytes)
{
    auto p = process(plugin("formats", R"(
hikari.formats.exporter("csv", function(doc, options)
    local rows = {}
    for _, line in ipairs(doc.lines) do rows[#rows + 1] = line.start .. "," .. line.text end
    return table.concat(rows, "\n") .. "\n" .. options.path
end)
hikari.formats.exporter("bin", function() return "AAH/", "base64" end)
)"));
    FormatResult csv, bin;
    ASSERT_TRUE(p->exportDocument("csv", R"({"lines": [{"start": 0, "text": "Hi"}, {"start": 1500, "text": "Bye"}]})",
                                  R"({"path": "/x/a.csv"})", csv.done()));
    ASSERT_TRUE(p->exportDocument("bin", "{}", "{}", bin.done()));
    ASSERT_TRUE(waitFor([&] { return csv.value && bin.value; }));
    ASSERT_TRUE(csv.value->has_value()) << csv.value->error().toStdString();
    EXPECT_EQ(**csv.value, "0,Hi\n1500,Bye\n/x/a.csv");
    ASSERT_TRUE(bin.value->has_value());
    EXPECT_EQ(**bin.value, std::string("\0\x01\xff", 3));
}

TEST_F(PluginProcessTest, AnImporterGetsTheBytesInBase64AndItsTableComesBackAsJson)
{
    auto p = process(plugin("formats", R"(
hikari.formats.importer("csv", function(data, name)
    return {lines = {{text = data}}, info = {Title = name}}
end)
)"));
    FormatResult read;
    ASSERT_TRUE(p->importFile("csv", std::string("\0\x01\xff", 3), "a.csv", read.done()));
    ASSERT_TRUE(waitFor([&] { return read.value.has_value(); }));
    ASSERT_TRUE(read.value->has_value()) << read.value->error().toStdString();
    EXPECT_EQ(**read.value, R"({"info":{"Title":"a.csv"},"lines":[{"text":"AAH/"}]})");
}

TEST_F(PluginProcessTest, FormatCallsThatGoWrongSayWhy)
{
    auto p = process(plugin("formats", R"(
hikari.formats.exporter("csv", function() return 42 end)
hikari.formats.exporter("bin", function() return "\255" end)
hikari.formats.exporter("spin", function() while true do end end)
)"));
    FormatResult number, notText, missing, spin;
    ASSERT_TRUE(p->exportDocument("csv", "{}", "{}", number.done()));
    ASSERT_TRUE(p->exportDocument("bin", "{}", "{}", notText.done()));
    ASSERT_TRUE(p->importFile("csv", "x", "a.csv", missing.done()));
    ASSERT_TRUE(waitFor([&] { return number.value && notText.value && missing.value; }));
    EXPECT_EQ(number.value->error(), QStringLiteral("an exporter must return the file as a string, not a number"));
    EXPECT_EQ(notText.value->error(), QStringLiteral("the exporter's file isn't UTF-8 text; return it with \"base64\""));
    EXPECT_TRUE(missing.value->error().startsWith(QStringLiteral("format “csv” has no importer")));
    ASSERT_TRUE(p->exportDocument("spin", "{}", "{}", spin.done()));
    waitFor([] { return false; }, 300);
    EXPECT_FALSE(spin.value.has_value());
    p->forceStop();
    ASSERT_TRUE(waitFor([&] { return spin.value.has_value(); }));
    EXPECT_EQ(spin.value->error(), QStringLiteral("the plugin was stopped"));
}

TEST_F(PluginProcessTest, OnlyDeclaredFormatsTakeAHandler)
{
    auto p = process(plugin("formats", "hikari.formats.importer('bin', function() end)"));
    QSignalSpy failed(p.get(), &PluginProcess::loadFailed);
    FormatResult never;
    ASSERT_TRUE(p->exportDocument("csv", "{}", "{}", never.done()));
    ASSERT_TRUE(waitFor([&] { return failed.count() == 1 && never.value; }));
    EXPECT_TRUE(failed.at(0).at(0).toString().contains(QStringLiteral("“bin” isn't a format with import in hikari-plugin.json")));
    EXPECT_FALSE(never.value->has_value());
}

TEST_F(PluginProcessTest, ACompletionProviderAnswersWithItsHandlersTable)
{
    auto p = process(plugin("providers", R"(
hikari.providers.completion(function(r)
    if r.trigger == "x" then return nil end
    return {{label = r.text:sub(1, r.offset), from = 0, to = r.offset}}
end)
)"));
    FormatResult items, none;
    ASSERT_TRUE(p->provide("completion", R"({"text": "\\fad", "offset": 3, "trigger": "\\"})", items.done()));
    ASSERT_TRUE(waitFor([&] { return items.value.has_value(); }));
    ASSERT_TRUE(items.value->has_value()) << items.value->error().toStdString();
    EXPECT_EQ(**items.value, R"([{"from":0,"label":"\\fa","to":3}])");
    ASSERT_TRUE(p->provide("completion", R"({"text": "", "offset": 0, "trigger": "x"})", none.done()));
    ASSERT_TRUE(waitFor([&] { return none.value.has_value(); }));
    ASSERT_TRUE(none.value->has_value());
    EXPECT_EQ(**none.value, "null");
}

TEST_F(PluginProcessTest, ALateAnswerIsDroppedAndThePluginKeepsRunning)
{
    m_slowReplyMs = 400;
    auto p = process(plugin("providers", R"(
hikari.providers.completion(function() hikari.call("api.version") return {} end)
hikari.commands.register("greet", function() hikari.notify("hi") end)
)"),
                     PluginProcess::Limits{.answerMs = 150});
    QSignalSpy stopped(p.get(), &PluginProcess::stopped);
    QSignalSpy finished(p.get(), &PluginProcess::finished);
    FormatResult late;
    ASSERT_TRUE(p->provide("completion", "{}", late.done()));
    ASSERT_TRUE(waitFor([&] { return late.value.has_value(); }));
    EXPECT_EQ(late.value->error(), QStringLiteral("the plugin answered too late"));
    m_slowReplyMs = 0;
    ASSERT_TRUE(p->run("greet", "{}"));
    ASSERT_TRUE(waitFor([&] { return finished.count() == 1; }));
    EXPECT_EQ(stopped.count(), 0);
}

TEST_F(PluginProcessTest, ANewerRequestReplacesAQueuedOne)
{
    m_slowReplyMs = 200;
    auto p = process(plugin("providers", R"(
hikari.providers.completion(function(r) hikari.call("api.version") return {{label = r.text}} end)
)"));
    QSignalSpy loaded(p.get(), &PluginProcess::loaded);
    p->start();
    ASSERT_TRUE(waitFor([&] { return loaded.count() == 1; }));
    FormatResult running, replaced, newest;
    ASSERT_TRUE(p->provide("completion", R"({"text": "a"})", running.done()));
    ASSERT_TRUE(p->provide("completion", R"({"text": "b"})", replaced.done()));
    ASSERT_TRUE(p->provide("completion", R"({"text": "c"})", newest.done()));
    ASSERT_TRUE(waitFor([&] { return running.value && replaced.value && newest.value; }));
    EXPECT_EQ(replaced.value->error(), QStringLiteral("a newer request replaced it"));
    ASSERT_TRUE(running.value->has_value());
    ASSERT_TRUE(newest.value->has_value()) << newest.value->error().toStdString();
    EXPECT_EQ(**newest.value, R"([{"label":"c"}])");
}

TEST_F(PluginProcessTest, EndingTheProcessAnswersWhatWasWaitingEvenOnceItIsGone)
{
    m_holdReplies = true;
    auto p = process(plugin("providers", R"(
hikari.providers.completion(function(r) hikari.call("api.version") return {{label = r.text}} end)
)"));
    QSignalSpy loaded(p.get(), &PluginProcess::loaded);
    p->start();
    ASSERT_TRUE(waitFor([&] { return loaded.count() == 1; }));
    FormatResult running, queued;
    ASSERT_TRUE(p->provide("completion", R"({"text": "a"})", running.done()));
    ASSERT_TRUE(waitFor([&] { return !m_held.empty(); }));
    ASSERT_TRUE(p->exportDocument("csv", "{}", "{}", queued.done()));
    p->end();
    p.reset();
    ASSERT_TRUE(waitFor([&] { return running.value && queued.value; }));
    EXPECT_EQ(running.value->error(), QStringLiteral("the plugin was stopped"));
    EXPECT_EQ(queued.value->error(), QStringLiteral("the plugin was stopped"));
}

TEST_F(PluginProcessTest, OnlyDeclaredProvidersTakeAHandler)
{
    auto p = process(plugin("providers", "hikari.providers.hover(function() end)"));
    QSignalSpy failed(p.get(), &PluginProcess::loadFailed);
    p->start();
    ASSERT_TRUE(waitFor([&] { return failed.count() == 1; }));
    EXPECT_TRUE(failed.at(0).at(0).toString().contains(QStringLiteral("declares no contributes.providers.hover")));
}

TEST_F(PluginProcessTest, TimersNeedTheBackgroundPermission)
{
    auto p = process(plugin("timers", "local id, err = hikari.timer.every(100, function() end) "
                                      "hikari.notify(tostring(id) .. ' ' .. err.code)"));
    QSignalSpy loaded(p.get(), &PluginProcess::loaded);
    p->start();
    ASSERT_TRUE(waitFor([&] { return loaded.count() == 1; }));
    EXPECT_EQ(m_notices, std::vector<std::string>{"nil not_granted"});
    EXPECT_EQ(p->timerCount(), 0u);
}

TEST_F(PluginProcessTest, ABackgroundPluginsTimersFireOnceOrEveryHundredMillisecondsAtLeast)
{
    auto p = process(plugin("timers", R"(
        hikari.timer.after(50, function() hikari.notify('once') end)
        local n, every = 0, nil
        every = hikari.timer.every(10, function()
            n = n + 1
            hikari.notify('tick ' .. n)
            if n == 3 then assert(hikari.timer.cancel(every)) end
        end)
    )"));
    p->setBackground(true);
    QElapsedTimer clock;
    clock.start();
    p->start();
    ASSERT_TRUE(waitFor([&] { return m_notices.size() == 4; }));
    EXPECT_GE(clock.elapsed(), 300);
    waitFor([] { return false; }, 400);
    EXPECT_EQ(m_notices, (std::vector<std::string>{"once", "tick 1", "tick 2", "tick 3"}));
    EXPECT_EQ(p->timerCount(), 0u);
}

TEST_F(PluginProcessTest, AnEveryTimerDueWhileItsCallbackRunsIsSkippedNotQueued)
{
    m_holdReplies = true;
    auto p = process(plugin("timers", "hikari.timer.every(300, function() hikari.call('api.version') end)"));
    p->setBackground(true);
    p->start();
    ASSERT_TRUE(waitFor([&] { return m_held.size() == 1; }));
    EXPECT_TRUE(p->inTimer());
    waitFor([] { return false; }, 700);
    ASSERT_EQ(m_held.size(), 1u);
    m_held.front()(R"({"result": {}})");
    EXPECT_TRUE(waitFor([&] { return !p->busy(); }, 1000));
    ASSERT_TRUE(waitFor([&] { return m_held.size() == 2; }));
    p->forceStop();
}

TEST_F(PluginProcessTest, ATimerCallbackMayBeginAJobAndAStopDropsTheTimers)
{
    auto p = process(plugin("timers", R"(
        hikari.timer.after(10, function() assert(hikari.job.begin{title = 'Syncing'}) end)
        hikari.timer.every(100, function() hikari.notify('tick') end)
    )"));
    p->setBackground(true);
    QSignalSpy began(p.get(), &PluginProcess::jobBegan);
    p->start();
    ASSERT_TRUE(waitFor([&] { return began.count() == 1; }));
    EXPECT_EQ(began.at(0).at(1).toString(), QStringLiteral("Syncing"));
    ASSERT_TRUE(waitFor([&] { return !m_notices.empty(); }));
    EXPECT_EQ(p->timerCount(), 1u);
    p->forceStop();
    EXPECT_EQ(p->timerCount(), 0u);
    const std::size_t heard = m_notices.size();
    waitFor([] { return false; }, 300);
    EXPECT_EQ(m_notices.size(), heard);
}
