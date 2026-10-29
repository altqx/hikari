#include "hikari/backends/lua_protocol.h"
#include "hikari/backends/lua_script_host.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/core/ass_load.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <functional>
#include <map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#endif
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

    std::unique_ptr<LuaScriptHost> load(const QString &script, LuaScriptHost::ServiceHandler services = {},
                                        const QString &include = QStringLiteral(HIKARI_LUA_INCLUDE))
    {
        auto host = std::make_unique<LuaScriptHost>(QStringLiteral(HIKARI_LUA_HELPER), script, include);
        host->setServiceHandler(std::move(services));
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

    bool runToEnd(LuaScriptHost &host, const std::string &name, int ms = 10'000)
    {
        run = {};
        if (!host.run(macro(host, name)))
            return false;
        return waitFor([&] { return run.outcome.has_value(); }, ms);
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
    EXPECT_EQ(host->info().macros.size(), 11u);
}

TEST_F(LuaHelper, DialogRoundTripPreservesCoercionsFalseAndNil)
{
    auto host = load(fixture("host-fixture.lua"));
    std::optional<DialogRequest> asked;
    host->setDialogHandler([&](const DialogRequest &request, LuaScriptHost::DialogReply reply) {
        asked = request;
        DialogResult result;
        result.pressed = 1;
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
    EXPECT_EQ(asked->buttonIds, (std::vector<std::string>{"ok", "cancel"}));

    ASSERT_EQ(run.log.size(), 1);
    EXPECT_EQ(run.log[0].toStdString(),
              "button=boolean:false 7=number:99 f=number:2.5 flag=boolean:false pick=string:a text=string:typed")
        << "the cancel button is false; false survives; the label's nil leaves no key";
}

// Aegisub's LuaReadBack (auto4_lua_dialog.cpp at 5bc77e4f): false for no button or wxID_CANCEL, else the label.
TEST_F(LuaHelper, ScriptDialogsAnswerAsInAegisub)
{
    auto host = load(fixture("script-dialogs.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    int pressed = 0;
    std::optional<DialogRequest> asked;
    host->setDialogHandler([&](const DialogRequest &request, LuaScriptHost::DialogReply reply) {
        asked = request;
        auto result = hikari::application::initialDialogResult(request);
        result.pressed = pressed;
        reply(result);
    });
    struct Case {
        const char *macro;
        int pressed;
        const char *log;
    };
    const std::string semi = " al=number:50 bg=string:#000000 c1=boolean:false c2=boolean:false c3=boolean:false "
                             "c4=boolean:false tg=string:#000000";
    const std::string config = " logging.traceLevel=string:3 paths.logDir=string:?user/log "
                               "updates.updateInterval=number:302400 updates.updaterEnabled=boolean:true";
    const std::vector<std::pair<Case, std::string>> cases = {
        {{"GitSigns Configure", 0, "saved button=string:Save git_path=string:git"}, ""},
        {{"GitSigns Configure", 1, "not saved button=boolean:false git_path=string:git"}, "Cancel saves nothing"},
        {{"GitSigns Configure", -1, "not saved button=boolean:false git_path=string:git"}, "closed"},
        {{"SemiColorCalc", 0, "calculated button=string:Go"}, semi},
        {{"SemiColorCalc", 1, "calculated button=boolean:false"}, semi},
        {{"Toolbox install", 0, "install button=string: macro=string: module=string:"}, ""},
        {{"Toolbox install", 1, "nothing button=boolean:false macro=string: module=string:"}, ""},
        {{"Toolbox confirm", 0, "confirmed button=string:Yes"}, ""},
        {{"Toolbox confirm", 1, "declined button=boolean:false"}, ""},
        {{"Toolbox feed trust", 0, "feed button=string:Trust"}, ""},
        {{"Toolbox feed trust", 1, "feed button=string:Fetch once"}, ""},
        {{"Toolbox feed trust", 3, "feed button=boolean:false"}, ""},
        {{"Toolbox config", 0, "applied button=string:Save"}, config},
        {{"Toolbox config", 1, "applied button=string:Restore defaults"}, config},
        {{"Toolbox config", 2, "closed button=boolean:false"}, config},
        {{"Ok only", 0, "ok-only button=string:Go"}, ""},
        {{"Ok only", -1, "ok-only button=boolean:false"}, "Esc never presses a button"},
        {{"Unknown id", 0, "unknown button=string:A"}, "an id Aegisub does not know names no button"},
        {{"Unknown id", 1, "unknown button=boolean:false"}, ""},
        {{"Affirmative ids", 1, "affirmative button=string:&Yes"}, "the label keeps its &"},
        {{"Affirmative ids", 3, "affirmative button=string:Close"}, "close is not cancel"},
    };
    for (const auto &[c, rest] : cases) {
        pressed = c.pressed;
        ASSERT_TRUE(runToEnd(*host, c.macro)) << c.macro;
        EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << c.macro << run.message.toStdString();
        const std::string want = std::string(c.log) + (rest.empty() || rest[0] != ' ' ? "" : rest);
        EXPECT_EQ(run.log.value(0).toStdString(), want) << c.macro << " pressed " << c.pressed << " " << rest;
    }

    ASSERT_TRUE(runToEnd(*host, "GitSigns Configure"));
    EXPECT_EQ(asked->buttonIds, (std::vector<std::string>{"ok", "cancel"}));
    ASSERT_TRUE(runToEnd(*host, "Toolbox install"));
    EXPECT_TRUE(asked->buttons.empty());
    EXPECT_TRUE(asked->buttonIds.empty());
    ASSERT_TRUE(runToEnd(*host, "Unknown id"));
    EXPECT_EQ(asked->buttonIds, (std::vector<std::string>{"", "cancel"}));
    ASSERT_TRUE(runToEnd(*host, "Affirmative ids"));
    EXPECT_EQ(asked->buttonIds, (std::vector<std::string>{"", "yes", "save", "close"}));
    ASSERT_TRUE(runToEnd(*host, "SemiColorCalc"));
    ASSERT_EQ(asked->controls.size(), 11u);
    for (const auto &c : asked->controls)
        if (c.kind == "floatedit")
            EXPECT_DOUBLE_EQ(c.step, 1.0) << "AD1-float-step: the step reaches the dialog";

    asked.reset();
    ASSERT_TRUE(runToEnd(*host, "Invalid id"));
    EXPECT_FALSE(asked) << "an id naming no button is an error before anything shows";
    EXPECT_EQ(run.log.value(0).toStdString(), "invalid false Invalid button for id ok");
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

TEST_F(LuaHelper, ColourAndNumberControlsFollowTheLegacyCoercions)
{
    auto host = load(fixture("host-fixture.lua"));
    std::optional<DialogRequest> asked;
    host->setDialogHandler([&](const DialogRequest &request, LuaScriptHost::DialogReply reply) {
        asked = request;
        auto result = hikari::application::initialDialogResult(request);
        result.pressed = 0;
        reply(result);
    });
    ASSERT_TRUE(runToEnd(*host, "Controls"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    ASSERT_TRUE(asked);
    const auto &c = asked->controls;
    EXPECT_EQ(c[0].text, "#FF0000") << "ASS &HBBGGRR&";
    EXPECT_EQ(c[1].text, "#00FF00") << "HTML, normalized to upper case";
    EXPECT_EQ(c[2].text, "#800000FF") << "#TTRRGGBB: ASS transparency first";
    EXPECT_EQ(c[3].text, "#FF0000") << "a decimal SSA colour";
    EXPECT_EQ(c[4].text, "#0F0000") << "legacy Upper() discards its result: a lowercase &h is not stripped";
    EXPECT_EQ(c[5].text, "#000000") << "malformed components stay 0";
    EXPECT_EQ(c[6].intValue, 10) << "NumCtrl clamps what it shows";
    EXPECT_DOUBLE_EQ(c[7].number, 0.0);
    EXPECT_DOUBLE_EQ(c[7].step, 0.5) << "carried to the dialog (AD1-float-step)";
    ASSERT_EQ(run.log.size(), 1);
    EXPECT_EQ(run.log[0].toStdString(),
              "button=string:OK al=string:&H40& alpha=string:#800000FF ass=string:#FF0000 clamped=number:10 "
              "decimal=string:#FF0000 f=number:0 html=string:#00FF00 junk=string:#000000 lower=string:#0F0000 "
              "tb=string:multi")
        << "colours come back normalized; the alpha class is an edit field";
}

TEST_F(LuaHelper, MalformedDialogResultFailsTheRun)
{
    auto host = load(fixture("host-fixture.lua"));
    host->setDialogHandler([&](const DialogRequest &, LuaScriptHost::DialogReply reply) {
        DialogResult wrong;
        wrong.pressed = 0;
        wrong.values = {42};
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
    (*pending)(hikari::application::initialDialogResult(request));
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
    EXPECT_TRUE(run.message.contains(QStringLiteral("File \"%1\", line ").arg(fixture("host-fixture.lua"))))
        << run.message.toStdString();
}

TEST_F(LuaHelper, LoadFailuresAreReportedWithTheirReasons)
{
    auto syntax = load(fixture("syntax-error.lua"));
    EXPECT_EQ(syntax->state(), LuaScriptHost::State::LoadFailed);
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

constexpr std::string_view kMacroScript =
    "[Script Info]\n"
    "Title: macro\n"
    "ScriptType: v4.00+\n"
    "\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two\n"
    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,three\n";

hikari::application::EditSession macroSession()
{
    std::vector<std::byte> bytes(kMacroScript.size());
    std::memcpy(bytes.data(), kMacroScript.data(), bytes.size());
    return hikari::application::EditSession(hikari::core::loadAss(bytes).document);
}

std::vector<std::string> texts(const hikari::application::EditSession &s)
{
    std::vector<std::string> out;
    for (const auto *l : s.document().lines())
        out.emplace_back(l->text.begin(), l->text.end());
    return out;
}

TEST_F(LuaHelper, EdgeblurAppliesToTheSelectedLinesAsOneUndoStep)
{
    using namespace hikari::application;
    auto session = macroSession();
    const hikari::core::LineId l1{1}, l2{2}, l3{3};
    session.setSelection(Selection{l3, {l1, l3}});
    auto host = load(QStringLiteral(HIKARI_EDGEBLUR_SCRIPT));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready);
    const auto snapshot = snapshotForMacro(session);
    ASSERT_TRUE(snapshot);
    run = {};
    session.setReadOnly(true);
    ASSERT_TRUE(host->run(0, *snapshot));
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    session.setReadOnly(false);
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    ASSERT_TRUE(host->lastResult());
    const auto steps = session.historySize();
    ASSERT_TRUE(applyMacroResult(session, *snapshot, *host->lastResult(), "Add edgeblur"));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"{\\be1}one", "two", "{\\be1}three"}));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.document().lines()[1]->id, l2);
    EXPECT_EQ(session.selection().selected, (std::set<hikari::core::LineId>{l1, l3}));
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(session), (std::vector<std::string>{"one", "two", "three"}));
}

TEST_F(LuaHelper, StagedSubtitlesFollowTheLegacyObject)
{
    using namespace hikari::application;
    auto session = macroSession();
    session.setSelection(Selection{hikari::core::LineId{1}, {hikari::core::LineId{1}}});
    auto host = load(fixture("subs-api.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready);
    const auto snapshot = snapshotForMacro(session);
    ASSERT_TRUE(snapshot);
    run = {};
    ASSERT_TRUE(host->run(macro(*host, "Edit"), *snapshot));
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    const auto &result = *host->lastResult();
    ASSERT_EQ(result.dialogues.size(), 4u);
    EXPECT_EQ(result.dialogues[0].id, 0u);
    EXPECT_EQ(result.dialogues[1].id, 1u);
    EXPECT_EQ(result.selected, std::vector<int>{4});
    EXPECT_EQ(result.active, 4);
    ASSERT_TRUE(applyMacroResult(session, *snapshot, result, "Edit"));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"negative", "one!", "three", "appended"}));
    EXPECT_EQ(session.selection().active, session.document().lines()[0]->id);
}

TEST_F(LuaHelper, GeneratedLineMarksAndTheReportRoundTrip)
{
    using namespace hikari::application;
    auto session = macroSession();
    auto host = load(fixture("hikari-api.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready);
    auto snapshot = snapshotForMacro(session);
    ASSERT_TRUE(snapshot);
    run = {};
    ASSERT_TRUE(host->run(macro(*host, "Mark"), *snapshot));
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    const auto &result = *host->lastResult();
    ASSERT_EQ(result.dialogues.size(), 5u);
    EXPECT_EQ(result.dialogues[0].generatedFrom, 0u);
    EXPECT_EQ(result.dialogues[3].generatedFrom, 1u);
    EXPECT_EQ(result.dialogues[4].generatedFrom, 1u);
    EXPECT_EQ(result.report, "marked");
    ASSERT_TRUE(applyMacroResult(session, *snapshot, result, "Mark"));
    const auto &lines = session.document().lines();
    ASSERT_EQ(lines.size(), 5u);
    EXPECT_EQ(lines[3]->generatedFrom, lines[0]->id);
    EXPECT_EQ(lines[4]->generatedFrom, lines[0]->id);
    EXPECT_FALSE(lines[0]->generatedFrom.value);

    snapshot = snapshotForMacro(session);
    ASSERT_TRUE(snapshot);
    run = {};
    ASSERT_TRUE(host->run(macro(*host, "Sources"), *snapshot));
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(host->lastResult()->report, "8=5,9=5");

    ASSERT_TRUE(session.undo());
    for (const auto *l : session.document().lines())
        EXPECT_FALSE(l->generatedFrom.value);
}

TEST_F(LuaHelper, ValidationDecidesWhetherTheMacroRuns)
{
    using namespace hikari::application;
    auto session = macroSession();
    const hikari::core::LineId l1{1}, l2{2};
    session.setSelection(Selection{l1, {l1}});
    auto host = load(fixture("validation.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    EXPECT_TRUE(host->info().macros[0].hasValidate);
    auto runValidated = [&](const char *name, bool validateFirst = true) {
        run = {};
        const auto snapshot = snapshotForMacro(session);
        return snapshot && host->run(macro(*host, name), *snapshot, validateFirst) &&
               waitFor([&] { return run.outcome.has_value(); });
    };
    ASSERT_TRUE(runValidated("One line"));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log.value(0), QStringLiteral("validated 6 1 4"));
    EXPECT_TRUE(host->lastResult()->valid);
    EXPECT_EQ(host->lastResult()->dialogues[0].text, "ran:one");

    session.setSelection(Selection{l1, {l1, l2}});
    ASSERT_TRUE(runValidated("One line"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::NotValid);
    EXPECT_TRUE(run.message.isEmpty());
    ASSERT_TRUE(host->lastResult());
    EXPECT_FALSE(host->lastResult()->valid);
    EXPECT_TRUE(host->lastResult()->dialogues.empty());
    ASSERT_TRUE(runValidated("Never"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::NotValid);
    EXPECT_TRUE(host->lastResult()->dialogues.empty());

    ASSERT_TRUE(runValidated("Broken"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::NotValid);
    EXPECT_TRUE(run.message.startsWith(QStringLiteral("Runtime error in Lua macro validation function:\n")))
        << run.message.toStdString();
    EXPECT_TRUE(run.message.contains(QStringLiteral("validation broke")));

    ASSERT_TRUE(runValidated("Unvalidated"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok);
    ASSERT_TRUE(runValidated("Never", false));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok);
    EXPECT_EQ(host->lastResult()->dialogues[0].text, "should not run");
}

TEST_F(LuaHelper, ValidationEditsReachTheMacroAsOneStep)
{
    using namespace hikari::application;
    auto session = macroSession();
    const hikari::core::LineId l1{1};
    session.setSelection(Selection{l1, {l1}});
    auto host = load(fixture("validation.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready);
    const auto snapshot = snapshotForMacro(session);
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->selected, std::vector<int>{4});
    run = {};
    ASSERT_TRUE(host->run(macro(*host, "Edits while validating"), *snapshot, true));
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    const auto steps = session.historySize();
    ASSERT_TRUE(applyMacroResult(session, *snapshot, *host->lastResult(), "Edits while validating"));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(texts(session)[0], "one|5|5|Validated");
    EXPECT_EQ(session.document().scriptInfo(u8"Validated"), std::u8string(u8"yes"));
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(texts(session)[0], "one");
    EXPECT_FALSE(session.document().scriptInfo(u8"Validated"));
}

TEST_F(LuaHelper, ValidationEditsAreDroppedWhenItAnswersFalse)
{
    using namespace hikari::application;
    auto session = macroSession();
    const hikari::core::LineId l1{1};
    session.setSelection(Selection{l1, {l1}});
    auto host = load(fixture("validation.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready);
    const auto steps = session.historySize();
    const bool dirty = session.isDirty();
    const auto before = texts(session);
    for (const char *name : {"Edits then false", "Edits then error"}) {
        SCOPED_TRACE(name);
        const auto snapshot = snapshotForMacro(session);
        ASSERT_TRUE(snapshot);
        run = {};
        ASSERT_TRUE(host->run(macro(*host, name), *snapshot, true));
        ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
        EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::NotValid);
        ASSERT_TRUE(host->lastResult());
        const auto &result = *host->lastResult();
        EXPECT_FALSE(result.valid);
        EXPECT_TRUE(result.info.empty());
        EXPECT_TRUE(result.styles.empty());
        EXPECT_TRUE(result.dialogues.empty());
        const auto applied = applyMacroResult(session, *snapshot, result, name);
        ASSERT_FALSE(applied);
        EXPECT_EQ(applied.error().refusal, CommandRefusal::Invalid);
        EXPECT_EQ(texts(session), before);
        EXPECT_FALSE(session.document().scriptInfo(u8"Validated"));
        EXPECT_EQ(session.historySize(), steps);
        EXPECT_EQ(session.isDirty(), dirty);
    }
}

TEST_F(LuaHelper, ValidateCallsOnlyTheValidationFunction)
{
    using namespace hikari::application;
    auto session = macroSession();
    const hikari::core::LineId l1{1}, l2{2};
    session.setSelection(Selection{l1, {l1}});
    auto host = load(fixture("validation.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    auto validate = [&](const char *name) {
        run = {};
        const auto snapshot = snapshotForMacro(session);
        return snapshot && host->validate(macro(*host, name), *snapshot) &&
               waitFor([&] { return run.outcome.has_value(); });
    };
    ASSERT_TRUE(validate("One line"));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log.value(0), QStringLiteral("validated 6 1 4"));
    ASSERT_TRUE(host->lastResult());
    EXPECT_TRUE(host->lastResult()->valid);
    EXPECT_TRUE(host->lastResult()->dialogues.empty()) << "the macro did not run";
    EXPECT_TRUE(host->lastResult()->info.empty());

    ASSERT_TRUE(validate("Edits while validating"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok);
    EXPECT_TRUE(host->lastResult()->info.empty());

    session.setSelection(Selection{l1, {l1, l2}});
    ASSERT_TRUE(validate("One line"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::NotValid);
    EXPECT_TRUE(run.message.isEmpty());
    ASSERT_TRUE(validate("Broken"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::NotValid);
    EXPECT_TRUE(run.message.startsWith(QStringLiteral("Runtime error in Lua macro validation function:\n")));
    ASSERT_TRUE(validate("Unvalidated"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok);
    EXPECT_TRUE(host->lastResult()->dialogues.empty());
    EXPECT_EQ(host->state(), LuaScriptHost::State::Ready);
}

TEST_F(LuaHelper, LoadListsTheFilesTheTopLevelLoaded)
{
    auto host = load(fixture("dependencies/main.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    const QDir dir(fixture("dependencies"));
    auto canonical = [&](const char *name) {
        return QDir::toNativeSeparators(QFileInfo(dir.filePath(QLatin1String(name))).canonicalFilePath()).toStdString();
    };
    EXPECT_EQ(host->info().loadedFiles,
              (std::vector<std::string>{canonical("dep.lua"), canonical("nested.lua"), canonical("included.lua")}));
    ASSERT_TRUE(runToEnd(*host, "Main"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(host->info().loadedFiles.size(), 3u);
}

TEST_F(LuaHelper, SubInspectorMeasuresARenderedLine)
{
    auto session = macroSession();
    session.setSelection(hikari::application::Selection{hikari::core::LineId{1}, {hikari::core::LineId{1}}});
    auto host = load(fixture("subinspector.lua"),
                     [](const hikari::application::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
                         hikari::application::HostServiceReply out = hikari::application::HostServiceReply::unavailable();
                         if (r.service == hikari::application::HostService::DecodePath) {
                             out = {};
                             out.strings = {r.strings.at(0)};
                         }
                         reply(out);
                     });
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    const auto snapshot = hikari::application::snapshotForMacro(session);
    ASSERT_TRUE(snapshot);
    run = {};
    ASSERT_TRUE(host->run(macro(*host, "Bounds"), *snapshot));
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, QStringList{"1,true,true"});
}

TEST_F(LuaHelper, StagedSubtitlesRaiseTheLegacyErrors)
{
    auto session = macroSession();
    auto host = load(fixture("subs-api.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready);
    auto snapshot = *hikari::application::snapshotForMacro(session);
    const auto failure = [&](const char *name) {
        run = {};
        EXPECT_TRUE(host->run(macro(*host, name), snapshot));
        EXPECT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
        EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Failed) << name;
        EXPECT_FALSE(host->lastResult()) << name;
        return run.message.toStdString();
    };
    EXPECT_NE(failure("Out of range").find("Line index is out of range"), std::string::npos);
    EXPECT_NE(failure("Wrong class").find("Cannot add a line of class dialogs to a field of class info"),
              std::string::npos);
    EXPECT_NE(failure("Bad field").find("Invalid number 'layer' field in 'dialogue' class subtitle line"),
              std::string::npos);
    snapshot.canModify = false;
    EXPECT_NE(failure("Write").find("You cannot modify read-only subtitles"), std::string::npos);
}

struct FakeServices {
    std::vector<hikari::application::HostServiceRequest> requests;
    std::string clipboard;
    bool available = true;
    bool audio = true;
    std::int64_t peaksStatus = 0;
    std::vector<std::int64_t> resolution{1920, 1080};

    LuaScriptHost::ServiceHandler handler()
    {
        return [this](const hikari::application::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
            using hikari::application::HostService;
            using hikari::application::HostServiceReply;
            requests.push_back(r);
            if (!available)
                return reply(HostServiceReply::unavailable());
            HostServiceReply out;
            switch (r.service) {
            case HostService::FrameFromMs: out.integers = {r.integers.at(0) / 40}; break;
            case HostService::MsFromFrame: out.integers = {r.integers.at(0) * 40}; break;
            case HostService::VideoSize: out.integers = {640, 360, 16, 9}; break;
            case HostService::Keyframes: out.integers = {0, 24, 48}; break;
            case HostService::AudioSelection: out.integers = {1000, 2000}; break;
            case HostService::ProjectProperties:
                out.integers = {12};
                out.strings = {"a.wav", "v.mkv", "k.txt"};
                break;
            case HostService::FileName: out.strings = {"ep1.ass"}; break;
            case HostService::DecodePath: out.strings = {"/home/u/" + r.strings.at(0).substr(6)}; break;
            case HostService::Frame:
                out.integers = {2, 1};
                out.pixels = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                              std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
                break;
            case HostService::TextExtents: out.numbers = {10.5, 20, 4, 1}; break;
            case HostService::ClipboardGet: out.strings = {clipboard}; break;
            case HostService::ClipboardSet:
                clipboard = r.strings.at(0);
                out.integers = {1};
                break;
            case HostService::OpenFiles:
                out.strings = r.integers.at(0) ? std::vector<std::string>{"/d/a.txt", "/d/b.txt"}
                                               : std::vector<std::string>{"/d/a.txt"};
                break;
            case HostService::SaveFile: out.strings = {"/d/out.txt"}; break;
            case HostService::EditorCursor: out.integers = {7}; break;
            case HostService::EditorSelection: out.integers = {7, 3}; break;
            case HostService::EditorModified: out.integers = {1}; break;
            case HostService::ScriptResolution:
                if (resolution.empty())
                    return reply(HostServiceReply::unavailable());
                out.integers = resolution;
                break;
            case HostService::FrequencyPeaks:
                if (!audio)
                    return reply(HostServiceReply::unavailable());
                out.integers = {peaksStatus, 10, 20};
                out.numbers = {5, 6};
                break;
            default: break;
            }
            reply(std::move(out));
        };
    }
};

bool runMacro(LuaScriptHost &host, int index, const hikari::application::MacroSnapshot &snapshot, RunRecord &run)
{
    run = {};
    if (!host.run(index, snapshot))
        return false;
    return waitFor([&] { return run.outcome.has_value(); });
}

TEST_F(LuaHelper, MediaServicesAnswerInTheLegacyShapes)
{
    FakeServices services;
    auto host = load(fixture("services.lua"), services.handler());
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready);
    ASSERT_TRUE(runMacro(*host, macro(*host, "Media"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    ASSERT_EQ(run.log.size(), 7);
    EXPECT_EQ(run.log[0], "25,1000");
    EXPECT_EQ(run.log[1], "640,360,2,true");
    EXPECT_EQ(run.log[2], "3,24");
    EXPECT_EQ(run.log[3], "1000,2000");
    EXPECT_EQ(run.log[4], "12,v.mkv,a.wav,k.txt,,1,,");
    EXPECT_EQ(run.log[5], "ep1.ass,/home/u/x");
    EXPECT_EQ(run.log[6], QStringLiteral("2,1,%1,&H102030&,nil").arg(0x30 * 65536 + 0x20 * 256 + 0x10));
    for (const auto &r : services.requests) {
        EXPECT_EQ(r.script, fixture("services.lua").toStdString());
        EXPECT_EQ(r.run, services.requests.front().run);
        EXPECT_NE(r.run, 0u);
    }
    using hikari::application::HostService;
    EXPECT_EQ(services.requests.back().service, HostService::Frame);
    EXPECT_EQ(services.requests.back().integers, (std::vector<std::int64_t>{3, 1}));
}

TEST_F(LuaHelper, ScriptResolutionAsksTheHostWhileItIsMissing)
{
    using namespace hikari::application;
    using hikari::application::HostService;
    const auto withInfo = [](std::string_view extra) {
        std::string text(kMacroScript);
        text.insert(text.find("ScriptType"), extra);
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), bytes.size());
        return EditSession(hikari::core::loadAss(bytes).document);
    };
    const auto asked = [](const FakeServices &f) {
        std::vector<std::vector<std::int64_t>> out;
        for (const auto &r : f.requests)
            if (r.service == HostService::ScriptResolution)
                out.push_back(r.integers);
        return out;
    };
    FakeServices services;
    auto host = load(fixture("script-resolution.lua"), services.handler());
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();

    auto session = macroSession();
    auto snapshot = snapshotForMacro(session);
    ASSERT_TRUE(snapshot);
    ASSERT_TRUE(runMacro(*host, macro(*host, "Read resolution"), *snapshot, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log.value(0), "1920 x 1080, 1920 x 1080, PlayResX=1920;PlayResY=1080");
    EXPECT_EQ(asked(services), (std::vector<std::vector<std::int64_t>>{{0, 0}}));
    ASSERT_TRUE(host->lastResult());
    auto info = snapshot->info;
    info.push_back({"PlayResX", "1920"});
    info.push_back({"PlayResY", "1080"});
    EXPECT_EQ(host->lastResult()->info, info);

    services.requests.clear();
    auto half = withInfo("PlayResY: 480\n");
    snapshot = snapshotForMacro(half);
    ASSERT_TRUE(runMacro(*host, macro(*host, "Read resolution"), *snapshot, run));
    EXPECT_EQ(run.log.value(0), "1920 x 1080, 1920 x 1080, PlayResY=1080;PlayResX=1920");
    EXPECT_EQ(asked(services), (std::vector<std::vector<std::int64_t>>{{0, 480}}));

    services.requests.clear();
    snapshot = snapshotForMacro(session);
    ASSERT_TRUE(runMacro(*host, macro(*host, "Karaskel head"), *snapshot, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log.value(0), "1920 x 1080");
    EXPECT_EQ(asked(services).size(), 1u);

    services.requests.clear();
    auto set = withInfo("PlayResX: 640\nPlayResY: 360\n");
    snapshot = snapshotForMacro(set);
    ASSERT_TRUE(runMacro(*host, macro(*host, "Read resolution"), *snapshot, run));
    EXPECT_EQ(run.log.value(0), "640 x 360, 640 x 360, PlayResX=640;PlayResY=360");
    EXPECT_TRUE(asked(services).empty());

    services.requests.clear();
    services.resolution.clear();
    snapshot = snapshotForMacro(half);
    ASSERT_TRUE(runMacro(*host, macro(*host, "Read resolution"), *snapshot, run));
    EXPECT_EQ(run.log.value(0), "853 x 480, 853 x 480, PlayResY=480");
    EXPECT_EQ(asked(services).size(), 2u);
    EXPECT_EQ(host->lastResult()->info, snapshot->info);
    snapshot = snapshotForMacro(session);
    ASSERT_TRUE(runMacro(*host, macro(*host, "Read resolution"), *snapshot, run));
    EXPECT_EQ(run.log.value(0), "1280 x 720, 1280 x 720, ");
    auto bare = load(fixture("script-resolution.lua"));
    ASSERT_TRUE(runMacro(*bare, macro(*bare, "Read resolution"), *snapshot, run));
    EXPECT_EQ(run.log.value(0), "1280 x 720, 1280 x 720, ");
}

TEST_F(LuaHelper, UnavailableServicesReturnNil)
{
    FakeServices services;
    services.available = false;
    auto host = load(fixture("services.lua"), services.handler());
    ASSERT_TRUE(runMacro(*host, macro(*host, "Unavailable"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    ASSERT_EQ(run.log.size(), 1);
    EXPECT_EQ(run.log[0], "nil,nil,nil,nil,nil,nil,nil,nil,nil");
    auto bare = load(fixture("services.lua"));
    ASSERT_TRUE(runMacro(*bare, macro(*bare, "Unavailable"), {}, run));
    EXPECT_EQ(run.log.value(0), "nil,nil,nil,nil,nil,nil,nil,nil,nil");
}

struct FakeCatalog {
    std::map<std::string, std::string> entries;
    std::vector<hikari::application::HostServiceRequest> requests;

    LuaScriptHost::ServiceHandler handler()
    {
        return [this](const hikari::application::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
            using hikari::application::HostServiceReply;
            if (r.service != hikari::application::HostService::Gettext)
                return reply(HostServiceReply::unavailable());
            requests.push_back(r);
            const std::string &source = r.strings.at(0);
            HostServiceReply out;
            const auto found = entries.find(source);
            out.strings = {found != entries.end() ? found->second : source.starts_with('\xff') ? std::string() : source};
            reply(std::move(out));
        };
    }
};

TEST_F(LuaHelper, GettextAsksTheHostWithTheLegacyArguments)
{
    FakeCatalog catalog;
    catalog.entries = {{"Search bar", "Pasek szukania"}};
    auto host = load(fixture("gettext.lua"), catalog.handler());
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    EXPECT_EQ(host->info().name, "Pasek szukania");
    ASSERT_FALSE(catalog.requests.empty());
    EXPECT_EQ(catalog.requests.front().run, 0u);
    EXPECT_EQ(catalog.requests.front().strings, std::vector<std::string>{"Search bar"});
    ASSERT_TRUE(runToEnd(*host, "Pasek szukania"));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    ASSERT_EQ(run.log.size(), 7);
    EXPECT_EQ(run.log[0], "Pasek szukania|Pasek szukania|no such key|%d element|12|");
    EXPECT_EQ(run.log[1], "3 element");
    EXPECT_EQ(run.log[2], "1|string");
    EXPECT_TRUE(run.log[3].startsWith("false|")) << run.log[3].toStdString();
    EXPECT_TRUE(run.log[3].contains("string expected, got no value")) << run.log[3].toStdString();
    EXPECT_TRUE(run.log[4].contains("string expected, got table")) << run.log[4].toStdString();
    EXPECT_EQ(run.log[5], "6|Search"); // the host gets every byte, Lua the text to its NUL
    EXPECT_EQ(run.log[6], "0");
    const auto nul = std::find_if(catalog.requests.begin(), catalog.requests.end(),
                                  [](const auto &r) { return r.strings.at(0).find('\0') != std::string::npos; });
    ASSERT_NE(nul, catalog.requests.end());
    EXPECT_EQ(nul->strings.at(0), std::string("Search\0bar", 10));

    catalog.entries = {{"Search bar", "Barre de recherche"}};
    ASSERT_TRUE(runToEnd(*host, "Pasek szukania"));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log.value(0), "Pasek szukania|Barre de recherche|no such key|%d element|12|");
    EXPECT_EQ(host->info().name, "Pasek szukania");
}

TEST_F(LuaHelper, GettextWithoutAnAnswerIsTheSource)
{
    auto host = load(fixture("gettext.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    EXPECT_EQ(host->info().name, "Search bar");
    ASSERT_TRUE(runToEnd(*host, "Search bar"));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log.value(0), "Search bar|Search bar|no such key|%d element|12|");
    EXPECT_EQ(run.log.value(6), "4");
}

TEST_F(LuaHelper, TextExtentsFollowTheLegacyChecks)
{
    FakeServices services;
    auto host = load(fixture("services.lua"), services.handler());
    auto session = macroSession();
    const auto snapshot = hikari::application::snapshotForMacro(session);
    ASSERT_TRUE(runMacro(*host, macro(*host, "Text"), *snapshot, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    ASSERT_EQ(run.log.size(), 5);
    EXPECT_EQ(run.log[0], "10.5,20,4,1");
    EXPECT_EQ(run.log[1], "0,0,0,0");
    EXPECT_EQ(run.log[2], "0");
    EXPECT_EQ(run.log[3], "First argument of text_extents must be a table");
    EXPECT_EQ(run.log[4], "Second argument of text_extents must be a string but is of type table");
    ASSERT_EQ(services.requests.size(), 1u);
    EXPECT_EQ(services.requests[0].strings, std::vector<std::string>{"Hello"});
    EXPECT_EQ(services.requests[0].style.at(0), "Default");
    EXPECT_EQ(services.requests[0].style.at(2), "20");
}

TEST_F(LuaHelper, ClipboardRoundTripsThroughTheFfiTable)
{
    FakeServices services;
    auto host = load(fixture("services.lua"), services.handler());
    ASSERT_TRUE(runMacro(*host, macro(*host, "Clipboard"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, (QStringList{"true", "copied"}));
    EXPECT_EQ(services.clipboard, "copied");
}

TEST_F(LuaHelper, FilePickersKeepTheLegacyArguments)
{
    FakeServices services;
    auto host = load(fixture("services.lua"), services.handler());
    ASSERT_TRUE(runMacro(*host, macro(*host, "Pickers"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, (QStringList{"2,/d/a.txt,/d/b.txt", "/d/a.txt", "/d/out.txt"}));
    ASSERT_EQ(services.requests.size(), 3u);
    // must_exist is toboolean(6) || isnil(6), and isnil is false past the stack top, so an omitted one is false.
    EXPECT_EQ(services.requests[0].strings, (std::vector<std::string>{"Open", "/d", "f.txt", "Text|*.txt"}));
    EXPECT_EQ(services.requests[0].integers, (std::vector<std::int64_t>{1, 0}));
    EXPECT_EQ(services.requests[1].integers, (std::vector<std::int64_t>{0, 0}));
    EXPECT_EQ(services.requests[2].integers, std::vector<std::int64_t>{0});
}

TEST_F(LuaHelper, EditorServicesUseOneBasedPositions)
{
    FakeServices services;
    auto host = load(fixture("services.lua"), services.handler());
    ASSERT_TRUE(runMacro(*host, macro(*host, "Gui"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, (QStringList{"8", "4,8", "true", "0"}));
    using hikari::application::HostService;
    std::vector<std::pair<HostService, std::vector<std::int64_t>>> writes;
    for (const auto &r : services.requests)
        if (r.service == HostService::SetEditorCursor || r.service == HostService::SetEditorSelection)
            writes.emplace_back(r.service, r.integers);
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0].second, std::vector<std::int64_t>{4});
    EXPECT_EQ(writes[1].second, (std::vector<std::int64_t>{1, 3}));
    EXPECT_EQ(services.requests.back().strings, std::vector<std::string>{"busy"});
}

TEST_F(LuaHelper, KaraokeAndPeaksExistFromTheFirstRun)
{
    auto host = load(fixture("karaoke.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    ASSERT_TRUE(runMacro(*host, macro(*host, "Presence"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, QStringList{"nil,nil,function,function"});
}

TEST_F(LuaHelper, ParseKaraokeDataFollowsTheLegacyParser)
{
    auto host = load(fixture("karaoke.lua"));
    ASSERT_TRUE(runMacro(*host, macro(*host, "Karaoke"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok)
        << run.message.toStdString() << "\nlogged:\n" << run.log.join(QLatin1Char('\n')).toStdString();
    ASSERT_EQ(run.log.size(), 8);
    EXPECT_EQ(run.log[0].toStdString(), "0:0,0,0,,| 1:100,0,100,k,a|a 2:200,100,300,k,b|b 3:300,300,600,kf,c|c");
    EXPECT_EQ(run.log[1].toStdString(), "0:0,0,0,,| 1:100,0,100,k,{\\b1}ab{\\i1}c|abc 2:50,100,150,k,d|d");
    EXPECT_EQ(run.log[2].toStdString(), "0:0,0,0,,| 1:2500,1000,3500,k,Hello {\\b1}world|Hello world");
    EXPECT_EQ(run.log[3].toStdString(), "0:0,0,0,,| 1:50,0,50,k,x|x");
    EXPECT_EQ(run.log[4].toStdString(), "true,dialogue,a");
    EXPECT_EQ(run.log[5].toStdString(), "You try to parse karaoke from non dialogue line");
    EXPECT_EQ(run.log[6].toStdString(), "You try to parse karaoke from non dialogue line");
    EXPECT_EQ(run.log[7].toStdString(), "Cannot convert non table value");
}

TEST_F(LuaHelper, TheAudioBoxKaraokeTextParses)
{
    auto host = load(fixture("karaoke.lua"));
    ASSERT_TRUE(runMacro(*host, macro(*host, "Karaoke text"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok)
        << run.message.toStdString() << "\nlogged:\n" << run.log.join(QLatin1Char('\n')).toStdString();
    ASSERT_EQ(run.log.size(), 3);
    EXPECT_EQ(run.log[0].toStdString(),
              "0:0,0,0,,| 1:200,0,200,kf,ka|ka 2:300,200,500,ko,ra|ra 3:500,500,1000,K,o|o 4:400,1000,1400,k,ke|ke");
    EXPECT_EQ(run.log[1].toStdString(), "0:0,0,0,,| 1:200,0,200,k,{\\fs20}ka|ka 2:300,200,500,k,{\\fs30}ra|ra "
                                        "3:100,500,600,k,{\\i1}o|o 4:400,600,1000,k,ke{\\i0}|ke");
    EXPECT_EQ(run.log[2].toStdString(), "0:0,0,0,,| 1:330,0,330,k,ka\\N|ka\\N 2:330,330,660,k,ra\\h|ra\\h "
                                        "3:340,660,1000,k,oke|oke");
}

TEST_F(LuaHelper, GetFrequencyPeaksKeepsTheLegacyChecks)
{
    FakeServices services;
    auto host = load(fixture("karaoke.lua"), services.handler());
    ASSERT_TRUE(runMacro(*host, macro(*host, "Peaks"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, (QStringList{"n=2,t=10;20,i=5;6", "n=1,t=10;20,i=", "n=2,t=10;20,i=5;6",
                                    "n=2,t=10;20,i=5;6", "n=1,t=,i=",
                                    "get_frequency_peaks start or end time less than zero",
                                    "Non number argument of function get_frequency_peaks",
                                    "Non number argument of function get_frequency_peaks"}));
    using hikari::application::HostService;
    std::vector<std::vector<std::int64_t>> asked;
    for (const auto &r : services.requests)
        if (r.service == HostService::FrequencyPeaks)
            asked.push_back(r.integers);
    EXPECT_EQ(asked, (std::vector<std::vector<std::int64_t>>{{0, 1000, 0, 100, 0},
                                                             {0, 1000, 0, 100, 1000},
                                                             {0, 1000, 0, 100, 0},
                                                             {1, 2000, 3, 4, 0},
                                                             {500, 500, 0, 0, 0},
                                                             {-1, 10, 0, 0, 0}}));

    services.peaksStatus = 1;
    ASSERT_TRUE(runMacro(*host, macro(*host, "Peaks"), {}, run));
    EXPECT_EQ(run.log.value(0), "get_frequency_peaks cannot get audio provider");
    EXPECT_EQ(run.log.value(5), "get_frequency_peaks cannot get audio provider");
    services.audio = false;
    ASSERT_TRUE(runMacro(*host, macro(*host, "Peaks"), {}, run));
    EXPECT_EQ(run.log.value(0), "get_frequency_peaks needs loaded audio by FFMS2");
    EXPECT_EQ(run.log.value(5), "get_frequency_peaks needs loaded audio by FFMS2");
    EXPECT_EQ(run.log.value(6), "Non number argument of function get_frequency_peaks");
}

TEST_F(LuaHelper, ServicesAnswerWhileTheTopLevelLoads)
{
    FakeServices services;
    auto host = load(fixture("services-load.lua"), services.handler());
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    EXPECT_EQ(host->info().name, "/home/u/name");
    ASSERT_EQ(services.requests.size(), 1u);
    EXPECT_EQ(services.requests[0].run, 0u);
}

TEST_F(LuaHelper, TheGuiKeepsRunningWhileAScriptAwaitsAService)
{
    std::optional<LuaScriptHost::ServiceReply> pending;
    auto host = load(fixture("services.lua"),
                     [&](const hikari::application::HostServiceRequest &, LuaScriptHost::ServiceReply reply) {
                         pending = std::move(reply);
                     });
    int ticks = 0;
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&] { ++ticks; });
    ticker.start(10);
    run = {};
    ASSERT_TRUE(host->run(macro(*host, "Wait"), {}));
    ASSERT_TRUE(waitFor([&] { return pending.has_value(); }));
    const int before = ticks;
    waitFor([] { return false; }, 300);
    EXPECT_GE(ticks - before, 10);
    EXPECT_EQ(host->pendingServices(), 1u);
    hikari::application::HostServiceReply reply;
    reply.integers = {42};
    (*pending)(reply);
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok);
    EXPECT_EQ(run.log, QStringList{"42"});
}

TEST_F(LuaHelper, CancelWhileAServiceWaitsWithdrawsItAndDropsTheLateAnswer)
{
    std::optional<LuaScriptHost::ServiceReply> pending;
    auto host = load(fixture("services.lua"),
                     [&](const hikari::application::HostServiceRequest &, LuaScriptHost::ServiceReply reply) {
                         pending = std::move(reply);
                     });
    int withdrawn = 0;
    QObject::connect(host.get(), &LuaScriptHost::servicesWithdrawn, [&] { ++withdrawn; });
    run = {};
    ASSERT_TRUE(host->run(macro(*host, "Wait"), {}));
    ASSERT_TRUE(waitFor([&] { return pending.has_value(); }));
    host->cancel();
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Cancelled);
    EXPECT_EQ(withdrawn, 1);
    EXPECT_EQ(host->pendingServices(), 0u);
    (*pending)({});
    EXPECT_EQ(host->state(), LuaScriptHost::State::Ready);
    FakeServices services;
    host->setServiceHandler(services.handler());
    ASSERT_TRUE(runMacro(*host, macro(*host, "Wait"), {}, run));
    EXPECT_EQ(run.log, QStringList{"0"});
}

TEST_F(LuaHelper, HelperLossWhileAServiceWaitsEndsTheRunAndDropsTheAnswer)
{
    std::optional<LuaScriptHost::ServiceReply> pending;
    auto host = load(fixture("services.lua"),
                     [&](const hikari::application::HostServiceRequest &, LuaScriptHost::ServiceReply reply) {
                         pending = std::move(reply);
                     });
    int withdrawn = 0;
    QObject::connect(host.get(), &LuaScriptHost::servicesWithdrawn, [&] { ++withdrawn; });
    run = {};
    ASSERT_TRUE(host->run(macro(*host, "Wait"), {}));
    ASSERT_TRUE(waitFor([&] { return pending.has_value(); }));
    const qint64 pid = host->processId();
    ASSERT_GT(pid, 0);
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    ASSERT_NE(process, nullptr);
    TerminateProcess(process, 9);
    CloseHandle(process);
#else
    ::kill(static_cast<pid_t>(pid), SIGKILL);
#endif
    ASSERT_TRUE(waitFor([&] { return run.outcome.has_value(); }));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::HelperLost);
    EXPECT_EQ(withdrawn, 1);
    (*pending)({});
    EXPECT_EQ(host->state(), LuaScriptHost::State::Unavailable);
}

TEST(LuaProtocol, HostServiceFramesRoundTripAndRejectMalformedOnes)
{
    using namespace hikari::application;
    HostServiceRequest request;
    request.service = HostService::OpenFiles;
    request.integers = {1, 0};
    request.strings = {"t", "/d", "f", "w"};
    request.style = {"Default"};
    const auto bytes = lua::encodeHostRequest(request);
    const auto decoded = lua::decodeHostRequest(bytes);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->service, HostService::OpenFiles);
    EXPECT_EQ(decoded->integers, request.integers);
    EXPECT_EQ(decoded->strings, request.strings);
    EXPECT_EQ(decoded->style, request.style);
    EXPECT_FALSE(lua::decodeHostRequest({bytes.begin(), bytes.end() - 1}));
    auto longer = bytes;
    longer.push_back(std::byte{0});
    EXPECT_FALSE(lua::decodeHostRequest(longer));
    helper::Writer unknown;
    unknown.i32(kLastHostService + 1).i32(0).i32(0).i32(0);
    EXPECT_FALSE(lua::decodeHostRequest(unknown.take()));
    helper::Writer huge;
    huge.i32(static_cast<std::int32_t>(HostService::Keyframes)).i32(0x7fffffff);
    EXPECT_FALSE(lua::decodeHostRequest(huge.take()));

    HostServiceReply reply;
    reply.integers = {2, 1};
    reply.numbers = {1.5};
    reply.strings = {"x"};
    reply.pixels = {std::byte{1}, std::byte{2}};
    const auto replyBytes = lua::encodeHostReply(reply);
    const auto back = lua::decodeHostReply(replyBytes);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->integers, reply.integers);
    EXPECT_EQ(back->numbers, reply.numbers);
    EXPECT_EQ(back->strings, reply.strings);
    EXPECT_EQ(back->pixels, reply.pixels);
    EXPECT_FALSE(lua::decodeHostReply({replyBytes.begin(), replyBytes.end() - 1}));
    helper::Writer status;
    status.i32(7).i32(0).i32(0).i32(0).bytes({});
    EXPECT_FALSE(lua::decodeHostReply(status.take()));
}

TEST_F(LuaHelper, NativePreloadsMatchTheLegacyModules)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    auto host = load(fixture("natives.lua"),
                     [&](const hikari::application::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
                         hikari::application::HostServiceReply out;
                         if (r.service == hikari::application::HostService::DecodePath)
                             out.strings = {temp.path().toStdString()};
                         reply(out);
                     });
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    const auto logOf = [&](const char *name) {
        EXPECT_TRUE(runToEnd(*host, name)) << name;
        EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Ok) << name << ": " << run.message.toStdString();
        return run.log;
    };
    EXPECT_EQ(logOf("Regex"), (QStringList{"Hello World,Hello,World,1,5", "a#b#c#", "a#b#c333", "true", "4",
                                           "false,true"}));
    EXPECT_EQ(logOf("RegexTraced"), (QStringList{"0"}));
    EXPECT_EQ(logOf("Unicode"), (QStringList{QStringLiteral("STRASSE,àéî,strasse"), "3,26085"}));
    EXPECT_EQ(logOf("Lpeg"), (QStringList{"4,0.10"}));
    EXPECT_EQ(logOf("Luabins"), (QStringList{"true,1,two,3"}));
    // The bundled lfs.moon wraps mkdir, touch and rmdir in tonumber(), which turns their bool into nil.
    EXPECT_EQ(logOf("Lfs"), (QStringList{"nil,nil", "directory,nil", "nil,nil", "file,0,nil", "a.txt", "nil,nil",
                                         "nil,nil"}));
}

TEST_F(LuaHelper, MoonScriptMacrosLoadAndMapErrorLines)
{
    auto host = load(fixture("moon-macro.moon"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    EXPECT_EQ(host->info().name, "Moon");
    ASSERT_TRUE(runToEnd(*host, "Moon"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, QStringList{"moon ran 2,4,6"});
    ASSERT_TRUE(runToEnd(*host, "Moon error"));
    EXPECT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Failed);
    EXPECT_TRUE(run.message.contains("moon failure")) << run.message.toStdString();
    EXPECT_TRUE(run.message.contains(QStringLiteral("File \"%1\", line 7").arg(fixture("moon-macro.moon"))))
        << run.message.toStdString();
}

// DependencyControl's first require runs an update check against live third-party feeds.
void disableDependencyControlUpdater(const QString &automationDir)
{
    QDir().mkpath(automationDir + QStringLiteral("/config"));
    QFile f(automationDir + QStringLiteral("/config/l0.DependencyControl.json"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"config": {"updaterEnabled": false}})");
}

TEST_F(LuaHelper, BundledScriptsLoadUnchanged)
{
    QDir dir(QStringLiteral(HIKARI_AUTOLOAD_DIR));
    const QStringList files = dir.entryList({QStringLiteral("*.lua"), QStringLiteral("*.moon")}, QDir::Files, QDir::Name);
    ASSERT_FALSE(files.isEmpty());
    QJsonArray corpus;
    std::map<QString, std::pair<LuaScriptHost::State, QStringList>> results;
    QTemporaryDir app;
    ASSERT_TRUE(app.isValid());
    const QString automation = app.filePath(QStringLiteral("Automation"));
    for (const char *sub : {"log", "autosave", "temp"})
        QDir().mkpath(automation + QLatin1Char('/') + QLatin1String(sub));
    disableDependencyControlUpdater(automation);
    hikari::application::AutomationPathContext paths;
    paths.automationDir = automation.toStdString();
    paths.dictionaryDir = app.filePath(QStringLiteral("Dictionary")).toStdString();
#ifdef _WIN32
    paths.windows = true;
#endif
    const auto services = [&](const hikari::application::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
        hikari::application::HostServiceReply out = hikari::application::HostServiceReply::unavailable();
        if (r.service == hikari::application::HostService::DecodePath) {
            out = {};
            out.strings = {hikari::application::decodeAutomationPath(r.strings.at(0), paths)};
        }
        reply(out);
    };
    for (const QString &file : files) {
        auto host = load(dir.filePath(file), services);
        QStringList macros;
        for (const auto &m : host->info().macros)
            macros << QString::fromStdString(m.name);
        const QString error = host->lastError();
        results[file] = {host->state(), macros};
        corpus.append(QJsonObject{{QStringLiteral("script"), file},
                                  {QStringLiteral("loaded"), host->state() == LuaScriptHost::State::Ready},
                                  {QStringLiteral("name"), QString::fromStdString(host->info().name)},
                                  {QStringLiteral("macros"), QJsonArray::fromStringList(macros)},
                                  {QStringLiteral("error"), error}});
        std::fprintf(stderr, "corpus %s: %s %s\n", qPrintable(file),
                     host->state() == LuaScriptHost::State::Ready ? "loaded" : "failed",
                     qPrintable(host->state() == LuaScriptHost::State::Ready ? macros.join(QStringLiteral(" | "))
                                                                             : error.section(QLatin1Char('\n'), 0, 2)));
    }
    QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
    QFile out(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/automation-corpus.json"));
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(QJsonDocument(corpus).toJson());
    for (const char *firstParty : {"macro-1-edgeblur.lua", "macro-2-mkfullwitdh.lua", "strip-tags.lua",
                                   "cleantags-autoload.lua", "karaoke-auto-leadin.lua", "kara-templater.lua",
                                   "select-overlaps.moon", "BezierToText.lua", "gradient-factory.lua",
                                   "skew gradient.lua", "a-mo.Aegisub-Motion.moon",
                                   "l0.DependencyControl.Toolbox.moon"}) {
        const auto it = results.find(QString::fromLatin1(firstParty));
        ASSERT_NE(it, results.end()) << firstParty;
        EXPECT_EQ(it->second.first, LuaScriptHost::State::Ready) << firstParty;
        EXPECT_FALSE(it->second.second.isEmpty()) << firstParty;
    }
}

TEST_F(LuaHelper, DependencyControlLoadsWhileTheScriptLoads)
{
    QTemporaryDir work;
    ASSERT_TRUE(work.isValid());
    const QString automation = work.filePath(QStringLiteral("Automation"));
    for (const char *sub : {"log", "autosave", "temp"})
        QDir().mkpath(automation + QLatin1Char('/') + QLatin1String(sub));
    disableDependencyControlUpdater(automation);
    hikari::application::AutomationPathContext paths;
    paths.automationDir = automation.toStdString();
#ifdef _WIN32
    paths.windows = true;
#endif
    int decodedWhileLoading = 0;
    auto host = load(fixture("depctrl-load.lua"),
                     [&](const hikari::application::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
                         if (r.service != hikari::application::HostService::DecodePath)
                             return reply(hikari::application::HostServiceReply::unavailable());
                         ++decodedWhileLoading;
                         hikari::application::HostServiceReply out;
                         out.strings = {hikari::application::decodeAutomationPath(r.strings.at(0), paths)};
                         reply(out);
                     });
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    EXPECT_GT(decodedWhileLoading, 0);
    ASSERT_TRUE(runToEnd(*host, "Show"));
    EXPECT_EQ(run.log, QStringList{"true,table,"});
}

TEST_F(LuaHelper, DependencyControlConfigSuitsOldScriptsAndOtherWriters)
{
    QTemporaryDir work;
    ASSERT_TRUE(work.isValid());
    const QString automation = work.filePath(QStringLiteral("Automation"));
    for (const char *sub : {"log", "autosave", "temp"})
        QDir().mkpath(automation + QLatin1Char('/') + QLatin1String(sub));
    disableDependencyControlUpdater(automation);
    hikari::application::AutomationPathContext paths;
    paths.automationDir = automation.toStdString();
#ifdef _WIN32
    paths.windows = true;
#endif
    auto host = load(fixture("depctrl-config.lua"),
                     [&](const hikari::application::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
                         if (r.service != hikari::application::HostService::DecodePath)
                             return reply(hikari::application::HostServiceReply::unavailable());
                         hikari::application::HostServiceReply out;
                         out.strings = {hikari::application::decodeAutomationPath(r.strings.at(0), paths)};
                         reply(out);
                     });
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    ASSERT_TRUE(runToEnd(*host, "Show"));
    EXPECT_EQ(run.log, QStringList{"true,false"});
    ASSERT_TRUE(runToEnd(*host, "Writers"));
    EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, QStringList{"3,1"});
    ASSERT_TRUE(runToEnd(*host, "Migrate"));
    EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, QStringList{"true,1,test-2"});
}

TEST_F(LuaHelper, DependencyControlFindsTheShippedModules)
{
    QTemporaryDir work;
    ASSERT_TRUE(work.isValid());
    const QString shipped = QDir(QStringLiteral(HIKARI_LUA_INCLUDE "/../..")).absolutePath();
    const QString user = work.filePath(QStringLiteral("Automation"));
    hikari::application::AutomationPathContext paths;
    paths.automationDir = shipped.toStdString();
    paths.userAutomationDir = user.toStdString();
#ifdef _WIN32
    paths.windows = true;
#endif
    const auto folders = hikari::application::automationFolders(paths.automationDir, paths.userAutomationDir,
                                                                 paths.windows);
    for (const auto &dir : folders.made)
        QDir().mkpath(QDir::fromNativeSeparators(QString::fromStdString(dir)));
    QStringList include;
    for (const auto &dir : folders.include)
        include << QString::fromStdString(dir);
    disableDependencyControlUpdater(user);
    const auto services = [&](const hikari::application::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
        if (r.service != hikari::application::HostService::DecodePath)
            return reply(hikari::application::HostServiceReply::unavailable());
        hikari::application::HostServiceReply out;
        out.strings = {hikari::application::decodeAutomationPath(r.strings.at(0), paths)};
        reply(out);
    };
    auto host = load(fixture("depctrl-entry.lua"), services, include.join(QLatin1Char('\n')));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    const auto found = [&] {
        EXPECT_TRUE(runToEnd(*host, "Find"));
        EXPECT_EQ(run.log.size(), 1);
        const QString line = run.log.value(0);
        const qsizetype comma = line.indexOf(QLatin1Char(','));
        return std::pair{line.left(comma).toStdString(),
                         QDir::cleanPath(QDir::fromNativeSeparators(line.mid(comma + 1))).toStdString()};
    };
    EXPECT_EQ(found(), std::pair(std::string("false"),
                                 (shipped + QStringLiteral("/automation/Include/l0/Functional.moon")).toStdString()));
    const QString copy = user + QStringLiteral("/automation/include/l0/Functional.moon");
    ASSERT_TRUE(QDir().mkpath(QFileInfo(copy).absolutePath()));
    ASSERT_TRUE(QFile::copy(shipped + QStringLiteral("/automation/Include/l0/Functional.moon"), copy));
    EXPECT_EQ(found(), std::pair(std::string("true"), copy.toStdString()));
}

class FeedServer : public QObject {
public:
    FeedServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { answer(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool listening() const { return m_server.isListening(); }
    QString url(const QString &path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path);
    }

    std::map<QString, QByteArray> files;

private:
    void answer(QTcpSocket *socket)
    {
        const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n"))
            return;
        const auto it = files.find(QString::fromLatin1(request.split(' ').value(1)));
        const QByteArray body = it == files.end() ? QByteArray("not found") : it->second;
        socket->write(QByteArray(it == files.end() ? "HTTP/1.1 404 Not Found" : "HTTP/1.1 200 OK") +
                      "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" +
                      body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
};

TEST_F(LuaHelper, DependencyControlInstallsAndUpdatesFromAFeed)
{
    FeedServer server;
    ASSERT_TRUE(server.listening());
    const QString feed = server.url(QStringLiteral("/feed.json"));
    const auto module = [&](const QString &version) {
        return QStringLiteral("local DepCtrl = require(\"l0.DependencyControl\")\n"
                              "local version = DepCtrl{name = \"Greeter\", version = \"%1\", "
                              "moduleName = \"test.Greeter\", feed = \"%2\"}\n"
                              "return version:register({version = version, greet = function() "
                              "return \"hello %1\" end})\n")
            .arg(version, feed)
            .toUtf8();
    };
    const QString other = server.url(QStringLiteral("/other.json"));
    const auto publish = [&](const QString &version, const QString &feedPath = QStringLiteral("/feed.json")) {
        const QString path = QStringLiteral("/Greeter-%1.lua").arg(version);
        server.files[path] = module(version);
        const QJsonObject file{
            {QStringLiteral("name"), QStringLiteral(".lua")},
            {QStringLiteral("url"), server.url(path)},
            {QStringLiteral("sha1"), QString::fromLatin1(QCryptographicHash::hash(module(version),
                                                                                  QCryptographicHash::Sha1)
                                                             .toHex())}};
        const QJsonObject channel{{QStringLiteral("version"), version},
                                  {QStringLiteral("released"), QStringLiteral("2026-10-27")},
                                  {QStringLiteral("default"), true},
                                  {QStringLiteral("files"), QJsonArray{file}}};
        const QJsonObject greeter{{QStringLiteral("name"), QStringLiteral("Greeter")},
                                  {QStringLiteral("author"), QStringLiteral("test")},
                                  {QStringLiteral("channels"), QJsonObject{{QStringLiteral("stable"), channel}}}};
        server.files[feedPath] =
            QJsonDocument(QJsonObject{{QStringLiteral("dependencyControlFeedFormatVersion"), QStringLiteral("0.4.0")},
                                      {QStringLiteral("name"), QStringLiteral("test feed")},
                                      {QStringLiteral("modules"),
                                       QJsonObject{{QStringLiteral("test.Greeter"), greeter}}}})
                .toJson();
    };

    QTemporaryDir work;
    ASSERT_TRUE(work.isValid());
    const QString shipped = QDir(QStringLiteral(HIKARI_LUA_INCLUDE "/../..")).absolutePath();
    const QString user = work.filePath(QStringLiteral("Automation"));
    hikari::application::AutomationPathContext paths;
    paths.automationDir = shipped.toStdString();
    paths.userAutomationDir = user.toStdString();
#ifdef _WIN32
    paths.windows = true;
#endif
    const auto folders = hikari::application::automationFolders(paths.automationDir, paths.userAutomationDir,
                                                                 paths.windows);
    for (const auto &dir : folders.made)
        QDir().mkpath(QDir::fromNativeSeparators(QString::fromStdString(dir)));
    QStringList include;
    for (const auto &dir : folders.include)
        include << QString::fromStdString(dir);
    // DependencyControl refuses loopback feeds unless private hosts are allowed, and asks before an untrusted feed.
    QDir().mkpath(user + QStringLiteral("/config"));
    QFile config(user + QStringLiteral("/config/l0.DependencyControl.json"));
    ASSERT_TRUE(config.open(QIODevice::WriteOnly));
    config.write(QJsonDocument(
                     QJsonObject{{QStringLiteral("config"),
                                  QJsonObject{{QStringLiteral("updates"),
                                               QJsonObject{{QStringLiteral("mode"), QStringLiteral("dependency-resolution")},
                                                           {QStringLiteral("blockPrivateHosts"), false}}},
                                              {QStringLiteral("feeds"),
                                               QJsonObject{{QStringLiteral("trustedFeeds"), QJsonArray{feed, other}},
                                                           {QStringLiteral("cacheMaxAge"), 0},
                                                           {QStringLiteral("feedFetchTimeout"), 5}}}}}})
                     .toJson());
    config.close();
    qputenv("HIKARI_TEST_FEED", feed.toUtf8());
    qputenv("HIKARI_TEST_VERSION", "1.1.0");

    const auto services = [&](const hikari::application::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
        if (r.service != hikari::application::HostService::DecodePath)
            return reply(hikari::application::HostServiceReply::unavailable());
        hikari::application::HostServiceReply out;
        out.strings = {hikari::application::decodeAutomationPath(r.strings.at(0), paths)};
        reply(out);
    };
    // DependencyControl -16: an untrusted feed's install, declined.
    publish(QStringLiteral("1.0.0"), QStringLiteral("/other.json"));
    qputenv("HIKARI_TEST_FEED", server.url(QStringLiteral("/gone.json")).toUtf8());
    qputenv("HIKARI_TEST_ADD_FEED", other.toUtf8());
    auto host = load(fixture("depctrl-feed.lua"), services, include.join(QLatin1Char('\n')));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    std::string asked;
    host->setDialogHandler([&](const DialogRequest &request, LuaScriptHost::DialogReply reply) {
        asked = request.controls.empty() ? std::string() : request.controls.front().label;
        auto result = hikari::application::initialDialogResult(request);
        result.pressed = -1;
        reply(result);
    });
    ASSERT_TRUE(runToEnd(*host, "Install", 60'000));
    EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_NE(asked.find(other.toStdString()), std::string::npos) << asked;
    ASSERT_FALSE(run.log.isEmpty());
    EXPECT_TRUE(run.log.last().startsWith(QStringLiteral("-16,"))) << run.log.join(QLatin1Char('\n')).toStdString();
    EXPECT_FALSE(QFile::exists(user + QStringLiteral("/automation/include/test/Greeter.lua")));
    server.files.erase(QStringLiteral("/other.json"));

    qputenv("HIKARI_TEST_FEED", feed.toUtf8());
    qunsetenv("HIKARI_TEST_ADD_FEED");
    publish(QStringLiteral("1.0.0"));
    host = load(fixture("depctrl-feed.lua"), services, include.join(QLatin1Char('\n')));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    ASSERT_TRUE(runToEnd(*host, "Install", 60'000));
    EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    ASSERT_FALSE(run.log.isEmpty());
    EXPECT_EQ(run.log.last(), QStringLiteral("1,1.0.0")) << run.log.join(QLatin1Char('\n')).toStdString();
    QFile installed(user + QStringLiteral("/automation/include/test/Greeter.lua"));
    ASSERT_TRUE(installed.open(QIODevice::ReadOnly));
    EXPECT_EQ(installed.readAll(), module(QStringLiteral("1.0.0")));
    installed.close();
    ASSERT_TRUE(runToEnd(*host, "Direct", 60'000));
    EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log.value(run.log.size() - 1), QStringLiteral("test feed")) << run.log.join(QLatin1Char('\n')).toStdString();

    publish(QStringLiteral("1.1.0"));
    ASSERT_TRUE(runToEnd(*host, "Update", 60'000));
    EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    ASSERT_FALSE(run.log.isEmpty());
    EXPECT_EQ(run.log.last(), QStringLiteral("1,1.1.0,hello 1.1.0")) << run.log.join(QLatin1Char('\n')).toStdString();
    ASSERT_TRUE(installed.open(QIODevice::ReadOnly));
    EXPECT_EQ(installed.readAll(), module(QStringLiteral("1.1.0")));
    installed.close();

    // DependencyControl -6: no suitable package.
    server.files.erase(QStringLiteral("/feed.json"));
    publish(QStringLiteral("1.2.0"), QStringLiteral("/other.json"));
    qputenv("HIKARI_TEST_ADD_FEED", other.toUtf8());
    qputenv("HIKARI_TEST_VERSION", "1.2.0");
    host = load(fixture("depctrl-feed.lua"), services, include.join(QLatin1Char('\n')));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    ASSERT_TRUE(runToEnd(*host, "Update", 60'000));
    EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    ASSERT_FALSE(run.log.isEmpty());
    EXPECT_TRUE(run.log.last().startsWith(QStringLiteral("-6,")) &&
                run.log.last().endsWith(QStringLiteral(",hello 1.1.0")))
        << run.log.join(QLatin1Char('\n')).toStdString();
    ASSERT_TRUE(installed.open(QIODevice::ReadOnly));
    EXPECT_EQ(installed.readAll(), module(QStringLiteral("1.1.0")));
    qunsetenv("HIKARI_TEST_FEED");
    qunsetenv("HIKARI_TEST_VERSION");
    qunsetenv("HIKARI_TEST_ADD_FEED");
}

// LibLyger and DependencyControl need Lua 5.2's table.pack and __ipairs; MoonScript's continue needs break anywhere.
TEST_F(LuaHelper, LuaJitBuildMatchesTheLegacyPlatform)
{
    auto host = load(fixture("luajit-build.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    ASSERT_TRUE(runToEnd(*host, "Show"));
    EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, QStringList{"function,true,true,false,true"});
}

TEST_F(LuaHelper, CaptureProbeCorpusRunsInThisHost)
{
    QDir dir(QStringLiteral(HIKARI_AUTOLOAD_DIR));
    const QStringList files = dir.entryList({QStringLiteral("*.lua"), QStringLiteral("*.moon")}, QDir::Files, QDir::Name);
    ASSERT_FALSE(files.isEmpty());
    QTemporaryDir work;
    ASSERT_TRUE(work.isValid());
    const QString list = work.filePath(QStringLiteral("corpus.txt"));
    const QString output = work.filePath(QStringLiteral("capture.jsonl"));
    {
        QFile f(list);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        for (const QString &file : files)
            f.write((dir.filePath(file) + QLatin1Char('\n')).toUtf8());
    }
    qputenv("HIKARI_CAPTURE_OUT", output.toLocal8Bit());
    qputenv("HIKARI_CAPTURE_CORPUS", list.toLocal8Bit());
    qputenv("HIKARI_CAPTURE_AT_LOAD", "1");
    const QString automation = work.filePath(QStringLiteral("Automation"));
    for (const char *sub : {"log", "autosave", "temp"})
        QDir().mkpath(automation + QLatin1Char('/') + QLatin1String(sub));
    disableDependencyControlUpdater(automation);
    hikari::application::AutomationPathContext paths;
    paths.automationDir = automation.toStdString();
    paths.dictionaryDir = work.filePath(QStringLiteral("Dictionary")).toStdString();
#ifdef _WIN32
    paths.windows = true;
#endif
    auto host = load(QStringLiteral(HIKARI_CAPTURE_PROBE),
                     [&](const hikari::application::HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
                         hikari::application::HostServiceReply out = hikari::application::HostServiceReply::unavailable();
                         if (r.service == hikari::application::HostService::DecodePath) {
                             out = {};
                             out.strings = {hikari::application::decodeAutomationPath(r.strings.at(0), paths)};
                         }
                         reply(out);
                     });
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    ASSERT_TRUE(runToEnd(*host, "Capture 10 corpus"));
    EXPECT_EQ(run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    qunsetenv("HIKARI_CAPTURE_OUT");
    qunsetenv("HIKARI_CAPTURE_CORPUS");
    qunsetenv("HIKARI_CAPTURE_AT_LOAD");
    QFile f(output);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray atLoad = f.readLine(), inMacro = f.readLine();
    QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
    for (const auto &[line, name] : {std::pair{atLoad, "automation-capture-corpus.json"},
                                     std::pair{inMacro, "automation-capture-corpus-macro.json"}}) {
        const QJsonObject record = QJsonDocument::fromJson(line).object();
        ASSERT_EQ(record.value(QStringLiteral("case")).toString(), QStringLiteral("corpus")) << line.toStdString();
        ASSERT_FALSE(record.contains(QStringLiteral("error"))) << line.toStdString();
        QFile artifact(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/") + QLatin1String(name));
        ASSERT_TRUE(artifact.open(QIODevice::WriteOnly));
        artifact.write(QJsonDocument(record).toJson());
    }
    const QJsonObject result = QJsonDocument::fromJson(inMacro).object();
    const QJsonArray scripts = result.value(QStringLiteral("scripts")).toArray();
    EXPECT_EQ(scripts.size(), files.size());
    for (const auto &value : scripts) {
        const QJsonObject script = value.toObject();
        const std::string file = script.value(QStringLiteral("file")).toString().toStdString();
        EXPECT_TRUE(script.value(QStringLiteral("loaded")).toBool())
            << file << ": " << script.value(QStringLiteral("error")).toString().toStdString();
        EXPECT_FALSE(script.value(QStringLiteral("registrations")).toArray().isEmpty()) << file;
    }
    EXPECT_TRUE(QJsonDocument::fromJson(atLoad).object().value(QStringLiteral("modules")).toObject().value(
        QStringLiteral("l0.DependencyControl")).toObject().value(QStringLiteral("ok")).toBool())
        << atLoad.toStdString();
    for (const char *module : {"aegisub.re", "aegisub.unicode", "lfs", "lpeg", "luabins", "ffi"})
        EXPECT_TRUE(result.value(QStringLiteral("modules")).toObject().value(QLatin1String(module)).toObject().value(
            QStringLiteral("ok")).toBool())
            << module;
}

}

TEST_F(LuaHelper, SharedIncludesAreSearchedInOrder)
{
    QTemporaryDir user, scripts;
    ASSERT_TRUE(user.isValid() && scripts.isValid());
    const auto write = [](const QString &path, const QByteArray &text) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(text);
    };
    const QString upper = user.filePath(QStringLiteral("automation/Include"));
    const QString lower = user.filePath(QStringLiteral("automation/include"));
    write(upper + QStringLiteral("/json.lua"), "return { who = 'user' }\n");
    write(lower + QStringLiteral("/depctrl_module.lua"), "return { who = 'depctrl' }\n");
    write(upper + QStringLiteral("/user-include.lua"), "INCLUDED = 'user include'\n");
    const QString script = scripts.filePath(QStringLiteral("includes.lua"));
    write(script, "include('user-include.lua')\n"
                  "script_name = require('json').who .. ',' .. require('depctrl_module').who\n"
                  "script_description = INCLUDED .. ',' .. type(require('moonscript'))\n");
    LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), script,
                       QStringList{upper, lower, QStringLiteral(HIKARI_LUA_INCLUDE)}.join(QLatin1Char('\n')));
    host.load();
    ASSERT_TRUE(waitFor([&] { return host.state() != LuaScriptHost::State::Loading; }));
    ASSERT_EQ(host.state(), LuaScriptHost::State::Ready) << host.lastError().toStdString();
    EXPECT_EQ(host.info().name, "user,depctrl");
    EXPECT_EQ(host.info().description, "user include,table");
}
