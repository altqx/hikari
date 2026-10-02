// N8: the isolated Lua helper against real scripts: the unchanged bundled
// edgeblur script and authored fixtures. Dialogs are answered in code here;
// the QML rendering has its own test.
#include "hikari/backends/lua_protocol.h"
#include "hikari/backends/lua_script_host.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <gtest/gtest.h>

#include <functional>
#include <optional>

using hikari::application::DialogRequest;
using hikari::application::DialogResult;
using hikari::backends::LuaScriptHost;
using namespace hikari::backends;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

QString fixture(const char *name)
{
    return QStringLiteral(HIKARI_LUA_FIXTURES "/") + QString::fromUtf8(name);
}

struct RunRecord {
    std::optional<LuaScriptHost::RunOutcome> outcome;
    QString message;
    QStringList log;
    QList<double> progress;
    int withdrawn = 0;
};

class LuaHelper : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "lua_helper_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    std::unique_ptr<LuaScriptHost> load(const QString &script)
    {
        auto host = std::make_unique<LuaScriptHost>(QStringLiteral(HIKARI_LUA_HELPER), script,
                                                    QStringLiteral(HIKARI_LUA_INCLUDE));
        QObject::connect(host.get(), &LuaScriptHost::logged, [this](const QString &t) { run.log << t; });
        QObject::connect(host.get(), &LuaScriptHost::progressChanged, [this](double p) { run.progress << p; });
        QObject::connect(host.get(), &LuaScriptHost::dialogWithdrawn, [this] { ++run.withdrawn; });
        QObject::connect(host.get(), &LuaScriptHost::finished,
                         [this](LuaScriptHost::RunOutcome o, const QString &m) {
                             run.outcome = o;
                             run.message = m;
                         });
        host->load();
        waitFor([&] { return host->state() != LuaScriptHost::State::Loading; });
        return host;
    }

    int macro(const LuaScriptHost &host, const std::string &name)
    {
        for (std::size_t i = 0; i < host.info().macros.size(); ++i)
            if (host.info().macros[i].name == name)
                return static_cast<int>(i);
        ADD_FAILURE() << "no macro " << name;
        return -1;
    }

    bool runToEnd(LuaScriptHost &host, const std::string &name)
    {
        run = {};
        if (!host.run(macro(host, name)))
            return false;
        return waitFor([&] { return run.outcome.has_value(); });
    }

    RunRecord run;
};

TEST_F(LuaHelper, LoadsTheUnchangedBundledEdgeblurScript)
{
    auto host = load(QStringLiteral(HIKARI_EDGEBLUR_SCRIPT));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    const auto &info = host->info();
    EXPECT_EQ(info.name, "Add edgeblur");
    EXPECT_EQ(info.description, "A demo macro showing how to do simple line modification in Automation 4");
    EXPECT_EQ(info.author, "Niels Martin Hansen");
    EXPECT_EQ(info.version, "1");
    ASSERT_EQ(info.macros.size(), 1u);
    EXPECT_EQ(info.macros[0].name, "Add edgeblur");
    EXPECT_EQ(info.macros[0].description, "") << "legacy reads the description from the stack top";
    EXPECT_FALSE(info.macros[0].hasValidate);
}

TEST_F(LuaHelper, IncompatibleProtocolIsRefusedBeforeAnyScriptRuns)
{
    helper::HelperHost host(QStringLiteral(HIKARI_LUA_HELPER), {}, lua::kProtocolVersion + 1);
    std::optional<quint32> helperVersion;
    QObject::connect(&host, &helper::HelperHost::refused, [&](quint32 v) { helperVersion = v; });
    host.start();
    ASSERT_TRUE(waitFor([&] { return helperVersion.has_value(); }));
    EXPECT_EQ(*helperVersion, lua::kProtocolVersion);
    EXPECT_EQ(host.state(), helper::HelperHost::State::Refused);
}

TEST_F(LuaHelper, ScriptGlobalsAndMetadataUseLegacyCoercions)
{
    auto host = load(fixture("host-fixture.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    EXPECT_EQ(host->info().name, "Host fixture");
    EXPECT_EQ(host->info().description, "42");
    EXPECT_EQ(host->info().version, "");
    EXPECT_EQ(host->info().macros.size(), 9u);
}

TEST_F(LuaHelper, DialogRoundTripPreservesCoercionsFalseAndNil)
{
    auto host = load(fixture("host-fixture.lua"));
    std::optional<DialogRequest> asked;
    host->setDialogHandler([&](const DialogRequest &request, LuaScriptHost::DialogReply reply) {
        asked = request;
        DialogResult result;
        result.pressed = 1; // "Stop": the ID table is dropped, so it is not Cancel
        result.values = {std::monostate{}, std::string("typed"), 99, 2.5, false, std::string("a")};
        reply(result);
    });
    ASSERT_TRUE(runToEnd(*host, "Dialog"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();

    ASSERT_TRUE(asked);
    ASSERT_EQ(asked->controls.size(), 6u);
    const auto &c = asked->controls;
    EXPECT_EQ(c[0].kind, "label");
    EXPECT_EQ(c[0].label, "Hello");
    EXPECT_EQ(c[1].kind, "edit") << "class is lowercased";
    EXPECT_EQ(c[1].text, "t") << "'text' overrides 'value'";
    EXPECT_EQ(c[1].x, 1) << "a numeric string is a number";
    EXPECT_EQ(c[1].width, 2);
    EXPECT_EQ(c[1].height, 1);
    EXPECT_EQ(c[2].name, "7") << "a number is a string";
    EXPECT_EQ(c[2].intValue, 12);
    EXPECT_EQ(c[2].intMin, INT_MIN) << "min >= max resets the range";
    EXPECT_EQ(c[2].intMax, INT_MAX);
    EXPECT_DOUBLE_EQ(c[3].number, 1.5);
    EXPECT_DOUBLE_EQ(c[3].step, 0.25);
    EXPECT_TRUE(c[4].checked);
    EXPECT_EQ(c[5].items, (std::vector<std::string>{"a", "3", "b"})) << "non-strings are skipped";
    EXPECT_EQ(c[5].text, "b");
    EXPECT_EQ(asked->buttons, (std::vector<std::string>{"Go", "Stop"}));

    ASSERT_EQ(run.log.size(), 1);
    EXPECT_EQ(run.log[0].toStdString(),
              "button=string:Stop 7=number:99 f=number:2.5 flag=boolean:false pick=string:a text=string:typed")
        << "false survives; the label's nil leaves no key";
}

TEST_F(LuaHelper, DefaultButtonsReturnEmptyStringOrFalse)
{
    auto host = load(fixture("host-fixture.lua"));
    int pressed = 0;
    host->setDialogHandler([&](const DialogRequest &request, LuaScriptHost::DialogReply reply) {
        EXPECT_TRUE(request.buttons.empty());
        auto result = hikari::application::initialDialogResult(request);
        result.pressed = pressed;
        reply(result);
    });
    ASSERT_TRUE(runToEnd(*host, "Default buttons"));
    EXPECT_EQ(run.log.value(0).toStdString(), "button=string: e=string:x") << "OK returns its empty label";
    pressed = 1;
    ASSERT_TRUE(runToEnd(*host, "Default buttons"));
    EXPECT_EQ(run.log.value(0).toStdString(), "button=boolean:false e=string:x") << "Cancel returns false";
    pressed = -1;
    ASSERT_TRUE(runToEnd(*host, "Default buttons"));
    EXPECT_EQ(run.log.value(0).toStdString(), "button=boolean:false e=string:x") << "closed returns false";
}

TEST_F(LuaHelper, MalformedDialogResultFailsTheRun)
{
    auto host = load(fixture("host-fixture.lua"));
    host->setDialogHandler([&](const DialogRequest &, LuaScriptHost::DialogReply reply) {
        DialogResult wrong;
        wrong.pressed = 0;
        wrong.values = {42}; // an integer for an edit control
        reply(wrong);
    });
    ASSERT_TRUE(runToEnd(*host, "Default buttons"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Failed);
    EXPECT_TRUE(run.message.contains("malformed dialog result")) << run.message.toStdString();
}

TEST_F(LuaHelper, UnknownControlClassIsAScriptError)
{
    auto host = load(fixture("host-fixture.lua"));
    ASSERT_TRUE(runToEnd(*host, "Bad dialog"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Failed);
    EXPECT_TRUE(run.message.contains("bad control table entry")) << run.message.toStdString();
}

TEST_F(LuaHelper, ScriptStateIsKeptBetweenInvocations)
{
    auto host = load(fixture("host-fixture.lua"));
    ASSERT_TRUE(runToEnd(*host, "Count"));
    EXPECT_EQ(run.log, (QStringList{"runs=1", "level one"})) << "level 5 is above trace level 3";
    ASSERT_TRUE(runToEnd(*host, "Count"));
    EXPECT_EQ(run.log.value(0), "runs=2");
}

TEST_F(LuaHelper, CrashMarksTheScriptUnavailableWithoutRerun)
{
    auto host = load(fixture("host-fixture.lua"));
    ASSERT_TRUE(runToEnd(*host, "Count"));
    std::optional<QString> unavailable;
    QObject::connect(host.get(), &LuaScriptHost::unavailable, [&](const QString &r) { unavailable = r; });
    ASSERT_TRUE(runToEnd(*host, "Crash"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::HelperLost);
    ASSERT_TRUE(waitFor([&] { return unavailable.has_value(); }));
    EXPECT_EQ(host->state(), LuaScriptHost::State::Unavailable);
    const auto session = host->session();
    run = {};
    EXPECT_FALSE(host->run(0)) << "nothing runs until an explicit restart";

    host->restart();
    ASSERT_TRUE(waitFor([&] { return host->state() == LuaScriptHost::State::Ready; }));
    EXPECT_GT(host->session(), session) << "a new helper generation";
    ASSERT_TRUE(runToEnd(*host, "Count"));
    EXPECT_EQ(run.log.value(0), "runs=1") << "top-level code ran again; the old state is gone";
}

TEST_F(LuaHelper, ScriptCancelEndsTheRunCancelled)
{
    auto host = load(fixture("host-fixture.lua"));
    ASSERT_TRUE(runToEnd(*host, "Cancel self"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Cancelled);
}

TEST_F(LuaHelper, HostCancelIsLatchedEvenWhenTheScriptReturns)
{
    auto host = load(fixture("host-fixture.lua"));
    run = {};
    ASSERT_TRUE(host->run(macro(*host, "Wait for cancel")));
    host->cancel();
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Cancelled);
    EXPECT_EQ(run.log, QStringList{"saw cancel"});
}

TEST_F(LuaHelper, CancelWhileADialogWaitsWithdrawsItAndDropsTheLateAnswer)
{
    auto host = load(fixture("host-fixture.lua"));
    std::optional<LuaScriptHost::DialogReply> pending;
    DialogRequest request;
    host->setDialogHandler([&](const DialogRequest &r, LuaScriptHost::DialogReply reply) {
        request = r;
        pending = std::move(reply);
    });
    run = {};
    ASSERT_TRUE(host->run(macro(*host, "Default buttons")));
    ASSERT_TRUE(waitFor([&] { return pending.has_value(); }));
    host->cancel();
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Cancelled);
    EXPECT_EQ(run.withdrawn, 1);
    (*pending)(hikari::application::initialDialogResult(request)); // too late: dropped
    ASSERT_TRUE(runToEnd(*host, "Count"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << "the helper is still usable";
}

TEST_F(LuaHelper, ScriptOutputGoesToDiagnosticsNotTheProtocol)
{
    auto host = load(fixture("host-fixture.lua"));
    ASSERT_TRUE(runToEnd(*host, "Noisy"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, QStringList{"still framed"});
    EXPECT_EQ(run.progress, (QList<double>{10, 50})) << "legacy reports only increases";
    ASSERT_TRUE(waitFor([&] { return host->diagnostics().contains("on stderr"); }));
    EXPECT_TRUE(host->diagnostics().contains("print on stdout"));
    EXPECT_TRUE(host->diagnostics().contains("io.write on stdout"));
}

TEST_F(LuaHelper, RuntimeErrorCarriesATraceback)
{
    auto host = load(fixture("host-fixture.lua"));
    ASSERT_TRUE(runToEnd(*host, "Error"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Failed);
    EXPECT_TRUE(run.message.contains("script failure")) << run.message.toStdString();
    EXPECT_TRUE(run.message.contains("stack traceback")) << run.message.toStdString();
}

TEST_F(LuaHelper, LoadFailuresAreReportedWithTheirReasons)
{
    auto syntax = load(fixture("syntax-error.lua"));
    EXPECT_EQ(syntax->state(), LuaScriptHost::State::LoadFailed);
    // Legacy names the chunk by its bare path, so Lua shows a truncated
    // [string "..."] prefix rather than the file name.
    EXPECT_TRUE(syntax->lastError().startsWith("[string \"")) << syntax->lastError().toStdString();
    EXPECT_TRUE(syntax->lastError().contains("]:2: '=' expected near 'is'")) << syntax->lastError().toStdString();

    auto debug = load(fixture("load-time-debug.lua"));
    EXPECT_EQ(debug->state(), LuaScriptHost::State::LoadFailed) << "aegisub.debug exists only during a run";
    EXPECT_TRUE(debug->lastError().contains("Error initializing Lua script")) << debug->lastError().toStdString();

    auto twice = load(fixture("duplicate-macro.lua"));
    EXPECT_EQ(twice->state(), LuaScriptHost::State::LoadFailed);
    EXPECT_TRUE(twice->lastError().contains("Macro named 'Twice' is already defined in the script "
                                            "'duplicate-macro.lua'"))
        << twice->lastError().toStdString();

    auto missing = load(fixture("no-such-script.lua"));
    EXPECT_EQ(missing->state(), LuaScriptHost::State::LoadFailed);
}

} // namespace
