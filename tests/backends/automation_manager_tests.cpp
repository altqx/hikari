// L1 (S44-macro-alias, A33-resources): the automation manager with real Lua
// helpers, and macro identity through the registry.
#include "hikari/application/automation_registry.h"
#include "hikari/backends/automation_manager.h"
#include "hikari/backends/process_metrics.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <set>

using namespace hikari;
using application::AliasProblem;
using application::AutomationRegistry;
using application::MacroIdentity;
using application::ScriptStatus;
using backends::AutomationManager;
using backends::LuaScriptHost;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

std::string fixture(const char *name)
{
    return std::string(HIKARI_LUA_FIXTURES) + "/" + name;
}

void writeScript(const QString &path, const std::string &body)
{
    std::ofstream(path.toStdString()) << body;
}

struct Manager : ::testing::Test {
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "automation_manager_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }
    AutomationManager manager{QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_LUA_INCLUDE)};

    ScriptStatus status(const std::string &path)
    {
        for (const auto &s : manager.scripts())
            if (s.path == path)
                return s;
        return {};
    }
    bool settled(const std::string &path)
    {
        return waitFor([&] { return status(path).state != ScriptStatus::State::Loading; });
    }
};

} // namespace

TEST(AutomationRegistry, LegacyAliasesResolveOnlyWhenUnambiguous)
{
    AutomationRegistry registry;
    registry.setScript({"/a/tools.lua", "h1", {"First", "Second"}});
    registry.setScript({"/b/other.lua", "h2", {"Only"}});
    const auto ok = registry.resolve("tools.lua:1");
    ASSERT_TRUE(ok.identity);
    EXPECT_EQ(ok.identity->name, "Second");
    EXPECT_EQ(ok.identity->legacyAlias(), "tools.lua:1");
    EXPECT_EQ(registry.resolve("missing.lua:0").problem, AliasProblem::MissingScript);
    EXPECT_EQ(registry.resolve("tools.lua:2").problem, AliasProblem::OrdinalOutOfRange);
    EXPECT_EQ(registry.resolve("tools.lua").problem, AliasProblem::MalformedAlias);
    EXPECT_EQ(registry.resolve("tools.lua:x").problem, AliasProblem::MalformedAlias);

    registry.setScript({"/c/tools.lua", "h3", {"Elsewhere"}}); // same file name, other folder
    const auto collision = registry.resolve("tools.lua:0");
    EXPECT_EQ(collision.problem, AliasProblem::BasenameCollision);
    EXPECT_EQ(collision.candidates.size(), 2u);
    registry.removeScript("/c/tools.lua");

    // What the binding recorded must still match: a reordered or edited
    // script stays unresolved instead of binding to whatever is there now.
    const MacroIdentity recorded{"/a/tools.lua", "h1", 1, "Second"};
    EXPECT_TRUE(registry.resolve("tools.lua:1", recorded).identity);
    registry.setScript({"/a/tools.lua", "h1", {"Second", "First"}});
    EXPECT_EQ(registry.resolve("tools.lua:1", recorded).problem, AliasProblem::RegistrationChanged);
    registry.setScript({"/a/tools.lua", "h9", {"First", "Second"}});
    EXPECT_EQ(registry.resolve("tools.lua:1", recorded).problem, AliasProblem::RegistrationChanged);
}

TEST_F(Manager, ScriptsKeepLoadOrderAndMacrosRegistrationOrder)
{
    const auto a = fixture("host-fixture.lua");
    const auto b = std::string(HIKARI_EDGEBLUR_SCRIPT);
    manager.load(a);
    manager.load(b);
    ASSERT_TRUE(settled(a) && settled(b));
    const auto scripts = manager.scripts();
    ASSERT_EQ(scripts.size(), 2u);
    EXPECT_EQ(scripts[0].path, a);
    EXPECT_EQ(scripts[1].path, b);
    const auto macros = manager.registry().macros();
    ASSERT_GE(macros.size(), 10u);
    EXPECT_EQ(macros[0].name, "Dialog");
    EXPECT_EQ(macros[2].name, "Count");
    EXPECT_EQ(macros[2].legacyAlias(), "host-fixture.lua:2");
    EXPECT_EQ(macros.back().legacyAlias(), "macro-1-edgeblur.lua:0");
    const auto resolved = manager.registry().resolve("host-fixture.lua:2");
    ASSERT_TRUE(resolved.identity);
    EXPECT_EQ(resolved.identity->name, "Count");
    EXPECT_FALSE(resolved.identity->scriptSha256.empty());
}

TEST_F(Manager, ReloadIsVisibleAndRunsTheTopLevelAgain)
{
    const auto a = fixture("host-fixture.lua");
    manager.load(a);
    ASSERT_TRUE(settled(a));
    QStringList log;
    QObject::connect(manager.host(a), &LuaScriptHost::logged, [&](const QString &t) { log << t; });
    ASSERT_TRUE(manager.run(a, 2)); // Count
    ASSERT_TRUE(waitFor([&] { return !manager.busy() && !log.isEmpty(); }));
    EXPECT_EQ(log.value(0), "runs=1");
    std::vector<ScriptStatus::State> seen;
    manager.setObserver([&] { seen.push_back(status(a).state); });
    ASSERT_TRUE(manager.reload(a));
    EXPECT_EQ(status(a).generation, 2u);
    EXPECT_EQ(status(a).state, ScriptStatus::State::Loading) << "reloading shows";
    ASSERT_TRUE(settled(a));
    EXPECT_EQ(status(a).state, ScriptStatus::State::Ready);
    log.clear();
    ASSERT_TRUE(manager.run(a, 2));
    ASSERT_TRUE(waitFor([&] { return !manager.busy() && !log.isEmpty(); }));
    EXPECT_EQ(log.value(0), "runs=1") << "a fresh state: the top level ran again";
}

TEST_F(Manager, MissingScriptsAreListedWithTheirReason)
{
    const std::string missing = fixture("no-such-script.lua");
    manager.load(missing);
    ASSERT_TRUE(settled(missing));
    EXPECT_EQ(status(missing).state, ScriptStatus::State::LoadFailed);
    EXPECT_FALSE(status(missing).error.empty());
    EXPECT_EQ(manager.registry().resolve("no-such-script.lua:0").problem, AliasProblem::MissingScript);
}

TEST_F(Manager, OneMacroRunsApplicationWide)
{
    const auto a = fixture("host-fixture.lua");
    const auto b = std::string(HIKARI_EDGEBLUR_SCRIPT);
    manager.load(a);
    manager.load(b);
    ASSERT_TRUE(settled(a) && settled(b));
    ASSERT_TRUE(manager.run(a, 4)); // Wait for cancel
    EXPECT_TRUE(manager.busy());
    EXPECT_FALSE(manager.run(b, 0)) << "another script's macro waits its turn";
    EXPECT_FALSE(manager.run(a, 2));
    manager.cancel();
    ASSERT_TRUE(waitFor([&] { return !manager.busy(); }));
    EXPECT_TRUE(manager.run(a, 2));
    ASSERT_TRUE(waitFor([&] { return !manager.busy(); }));
}

TEST_F(Manager, ACrashIsolatesItsScript)
{
    QTemporaryDir dir;
    const QString second = dir.filePath(QStringLiteral("counter.lua"));
    writeScript(second, "n = 0\naegisub.register_macro('Count', '', function() n = n + 1 aegisub.debug.out('n=%d', n) end)\n");
    const auto a = fixture("host-fixture.lua");
    const auto b = second.toStdString();
    manager.load(a);
    manager.load(b);
    ASSERT_TRUE(settled(a) && settled(b));
    QStringList log;
    QObject::connect(manager.host(b), &LuaScriptHost::logged, [&](const QString &t) { log << t; });
    ASSERT_TRUE(manager.run(b, 0));
    ASSERT_TRUE(waitFor([&] { return !manager.busy(); }));
    ASSERT_TRUE(manager.run(a, 5)); // Crash
    ASSERT_TRUE(waitFor([&] { return status(a).state == ScriptStatus::State::Unavailable; }));
    EXPECT_EQ(status(b).state, ScriptStatus::State::Ready);
    ASSERT_TRUE(manager.run(b, 0));
    ASSERT_TRUE(waitFor([&] { return !manager.busy() && log.size() == 2; }));
    EXPECT_EQ(log, (QStringList{"n=1", "n=2"})) << "the other script kept its state";
}

TEST_F(Manager, PerScriptProcessCostIsMeasured)
{
    QTemporaryDir dir;
    std::vector<std::string> paths;
    for (int i = 0; i < 6; ++i) {
        const QString path = dir.filePath(QStringLiteral("script%1.lua").arg(i));
        writeScript(path, "aegisub.register_macro('Macro', '', function() end)\n");
        paths.push_back(path.toStdString());
        manager.load(paths.back());
    }
    std::set<qint64> pids;
    std::int64_t total = 0;
    for (const auto &p : paths) {
        ASSERT_TRUE(settled(p));
        const LuaScriptHost *h = manager.host(p);
        const auto rss = backends::processResidentBytes(h->processId());
        pids.insert(h->processId());
        total += rss;
        EXPECT_GT(rss, 0);
        std::fprintf(stderr, "helper %lld: loaded in %.1f ms, resident %.1f MiB\n", static_cast<long long>(h->processId()),
                     h->loadMs(), rss / 1048576.0);
    }
    EXPECT_EQ(pids.size(), paths.size()) << "one process per script";
    // An observation on an uncalibrated host (A33-resources), not a budget.
    std::fprintf(stderr, "6 loaded scripts: %.1f MiB resident in their helpers\n", total / 1048576.0);
}
