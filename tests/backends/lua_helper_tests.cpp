// N8: the isolated Lua helper against real scripts: the unchanged bundled
// edgeblur script and authored fixtures. Dialogs are answered in code here;
// the QML rendering has its own test.
#include "hikari/backends/lua_protocol.h"
#include "hikari/backends/lua_script_host.h"
#include "hikari/application/automation_services.h"
#include "hikari/application/macro_transaction.h"
#include "hikari/core/ass_load.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

    std::unique_ptr<LuaScriptHost> load(const QString &script, LuaScriptHost::ServiceHandler services = {})
    {
        auto host = std::make_unique<LuaScriptHost>(QStringLiteral(HIKARI_LUA_HELPER), script,
                                                    QStringLiteral(HIKARI_LUA_INCLUDE));
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
    EXPECT_EQ(host->info().macros.size(), 11u);
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

TEST_F(LuaHelper, ColourAndNumberControlsFollowTheLegacyCoercions)
{
    auto host = load(fixture("host-fixture.lua"));
    std::optional<DialogRequest> asked;
    host->setDialogHandler([&](const DialogRequest &request, LuaScriptHost::DialogReply reply) {
        asked = request;
        auto result = hikari::application::initialDialogResult(request); // closed untouched
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
    EXPECT_DOUBLE_EQ(c[7].step, 0.5) << "carried, then ignored, as legacy ignores it";
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
    // Legacy add_stack_trace: a "File ..., line ..." block per frame, the
    // message after each, its "[string ...]:N: " location removed.
    EXPECT_TRUE(run.message.contains(QStringLiteral("File \"%1\", line ").arg(fixture("host-fixture.lua"))))
        << run.message.toStdString();
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

// L4: macros run against a snapshot of a Document; their staged edits come
// back as one result that applies as one undo step.
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
    // Lines keep their identity, and the selection is unchanged (none returned).
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
    EXPECT_EQ(result.dialogues[0].id, 0u); // added by the macro
    EXPECT_EQ(result.dialogues[1].id, 1u); // replaced in its slot
    EXPECT_EQ(result.selected, std::vector<int>{4});
    EXPECT_EQ(result.active, 4);
    ASSERT_TRUE(applyMacroResult(session, *snapshot, result, "Edit"));
    EXPECT_EQ(texts(session), (std::vector<std::string>{"negative", "one!", "three", "appended"}));
    EXPECT_EQ(session.selection().active, session.document().lines()[0]->id);
}

// A33-subinspector-linux: SubInspector's native library (the Windows DLL
// legacy shipped; built from the same v0.5.1 source on Linux) loads through
// requireffi and measures a rendered line through libass. The bounds depend
// on the fonts found, so only their presence is checked.
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

// L3: host services through the real helper. The fake ports answer in code
// and record every request with the identity the host attached.
struct FakeServices {
    std::vector<hikari::application::HostServiceRequest> requests;
    std::string clipboard;
    bool available = true;
    bool audio = true;        // FrequencyPeaks: Unavailable without audio
    std::int64_t peaksStatus = 0; // 1: an audio box without audio yet

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
                // 2x1 BGRA: (0,0) black, (1,0) B=0x10 G=0x20 R=0x30 A=0x40.
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
    // Script Info fields are looked up by their Lua names, as legacy does.
    EXPECT_EQ(run.log[4], "12,v.mkv,a.wav,k.txt,,1,,");
    EXPECT_EQ(run.log[5], "ep1.ass,/home/u/x");
    EXPECT_EQ(run.log[6], QStringLiteral("2,1,%1,&H102030&,nil").arg(0x30 * 65536 + 0x20 * 256 + 0x10));
    // Every request names the script and the run that asked.
    for (const auto &r : services.requests) {
        EXPECT_EQ(r.script, fixture("services.lua").toStdString());
        EXPECT_EQ(r.run, services.requests.front().run);
        EXPECT_NE(r.run, 0u);
    }
    using hikari::application::HostService;
    EXPECT_EQ(services.requests.back().service, HostService::Frame);
    EXPECT_EQ(services.requests.back().integers, (std::vector<std::int64_t>{3, 1}));
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
    // Without any handler the host answers Unavailable itself.
    auto bare = load(fixture("services.lua"));
    ASSERT_TRUE(runMacro(*bare, macro(*bare, "Unavailable"), {}, run));
    EXPECT_EQ(run.log.value(0), "nil,nil,nil,nil,nil,nil,nil,nil,nil");
}

// O5: aegisub.gettext asks the host (HostService::Gettext) on each call, at
// the top level as in a run, and keeps legacy get_translation's argument
// handling (check_string: a number converts, anything else raises) and
// lua_pushstring's result, which ends at the first NUL (Automation.cpp:83-88,
// AutomationUtils.h:162-165). The fake catalog stands in for the
// application's QM lookup.
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
            // The application turns bytes that are not UTF-8 into "" (Localisation::gettext).
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
    // script_name and the macro's name, translated while the script loaded.
    EXPECT_EQ(host->info().name, "Pasek szukania");
    ASSERT_FALSE(catalog.requests.empty());
    EXPECT_EQ(catalog.requests.front().run, 0u); // the top level
    EXPECT_EQ(catalog.requests.front().strings, std::vector<std::string>{"Search bar"});
    ASSERT_TRUE(runToEnd(*host, "Pasek szukania"));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    ASSERT_EQ(run.log.size(), 7);
    // A missing key and printf text come back as they are; a number converts.
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

    // The language changes between runs: the next lookup reads the new
    // catalog; what the script already holds (its top-level value, its
    // registered names) stays until it is reloaded.
    catalog.entries = {{"Search bar", "Barre de recherche"}};
    ASSERT_TRUE(runToEnd(*host, "Pasek szukania"));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log.value(0), "Pasek szukania|Barre de recherche|no such key|%d element|12|");
    EXPECT_EQ(host->info().name, "Pasek szukania");
}

// No answer (no handler, or Unavailable): the source, as for a key the
// catalog does not have.
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
    EXPECT_EQ(run.log[1], "0,0,0,0"); // empty text: no host call
    EXPECT_EQ(run.log[2], "0");       // not a style: no values
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
    // (title, dir, file, wildcard). Legacy reads must_exist as
    // toboolean(6) || isnil(6), and isnil is false past the stack top, so an
    // omitted must_exist is false (only an explicit nil is true).
    EXPECT_EQ(services.requests[0].strings, (std::vector<std::string>{"Open", "/d", "f.txt", "Text|*.txt"}));
    EXPECT_EQ(services.requests[0].integers, (std::vector<std::int64_t>{1, 0}));
    EXPECT_EQ(services.requests[1].integers, (std::vector<std::int64_t>{0, 0}));
    EXPECT_EQ(services.requests[2].integers, std::vector<std::int64_t>{0}); // true: don't prompt
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

// L6: legacy AutoToFile adds parse_karaoke_data and get_frequency_peaks
// (with set_undo_point) to the aegisub table for a run; they are absent
// while the script loads and stay afterwards (legacy capture: present during
// a macro run).
TEST_F(LuaHelper, KaraokeAndPeaksExistFromTheFirstRun)
{
    auto host = load(fixture("karaoke.lua"));
    ASSERT_EQ(host->state(), LuaScriptHost::State::Ready) << host->lastError().toStdString();
    ASSERT_TRUE(runMacro(*host, macro(*host, "Presence"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok) << run.message.toStdString();
    EXPECT_EQ(run.log, QStringList{"nil,nil,function,function"});
}

// Legacy LuaParseKaraokeData at 20d647c4: the syllables go into the line
// table passed in, from index 0 (an empty one), and that table comes back.
// Text before a tag and after its value up to the next block is the
// syllable's; "{}" is dropped and blocks are stripped for text_stripped.
TEST_F(LuaHelper, ParseKaraokeDataFollowsTheLegacyParser)
{
    auto host = load(fixture("karaoke.lua"));
    ASSERT_TRUE(runMacro(*host, macro(*host, "Karaoke"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok)
        << run.message.toStdString() << "\nlogged:\n" << run.log.join(QLatin1Char('\n')).toStdString();
    ASSERT_EQ(run.log.size(), 8);
    EXPECT_EQ(run.log[0].toStdString(), "0:0,0,0,,| 1:100,0,100,k,a|a 2:200,100,300,k,b|b 3:300,300,600,kf,c|c");
    EXPECT_EQ(run.log[1].toStdString(), "0:0,0,0,,| 1:100,0,100,k,{\\b1}ab{\\i1}c|abc 2:50,100,150,k,d|d");
    // No karaoke tag: the whole line, timed as the line, as one "k" syllable.
    EXPECT_EQ(run.log[2].toStdString(), "0:0,0,0,,| 1:2500,1000,3500,k,Hello {\\b1}world|Hello world");
    // An empty text with text_translation parses the translation (TextTl/Text swap).
    EXPECT_EQ(run.log[3].toStdString(), "0:0,0,0,,| 1:50,0,50,k,x|x");
    EXPECT_EQ(run.log[4].toStdString(), "true,dialogue,a");
    EXPECT_EQ(run.log[5].toStdString(), "You try to parse karaoke from non dialogue line");
    EXPECT_EQ(run.log[6].toStdString(), "You try to parse karaoke from non dialogue line");
    EXPECT_EQ(run.log[7].toStdString(), "Cannot convert non table value");
}

// A5: what the audio box's karaoke mode writes (Karaoke::GetText) parses
// into the syllables and times the box had: the durations and their order
// agree with audio_karaoke_tests' captures, the times start from 0.
TEST_F(LuaHelper, TheAudioBoxKaraokeTextParses)
{
    auto host = load(fixture("karaoke.lua"));
    ASSERT_TRUE(runMacro(*host, macro(*host, "Karaoke text"), {}, run));
    ASSERT_EQ(*run.outcome, LuaScriptHost::RunOutcome::Ok)
        << run.message.toStdString() << "\nlogged:\n" << run.log.join(QLatin1Char('\n')).toStdString();
    ASSERT_EQ(run.log.size(), 3);
    EXPECT_EQ(run.log[0].toStdString(),
              "0:0,0,0,,| 1:200,0,200,kf,ka|ka 2:300,200,500,ko,ra|ra 3:500,500,1000,K,o|o 4:400,1000,1400,k,ke|ke");
    // the tags after a \k stay with their syllable (the box's "{\fs20}ka")
    EXPECT_EQ(run.log[1].toStdString(), "0:0,0,0,,| 1:200,0,200,k,{\\fs20}ka|ka 2:300,200,500,k,{\\fs30}ra|ra "
                                        "3:100,500,600,k,{\\i1}o|o 4:400,600,1000,k,ke{\\i0}|ke");
    EXPECT_EQ(run.log[2].toStdString(), "0:0,0,0,,| 1:330,0,330,k,ka\\N|ka\\N 2:330,330,660,k,ra\\h|ra\\h "
                                        "3:340,660,1000,k,oke|oke");
}

// Legacy LuaGetFreqencyReach's argument checks, error texts and returns; the
// host answers the spectrum (application tests cover the computation).
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
    // The peek clamped to [0, 1000]; numbers truncated (lua_tointeger), a
    // numeric string taken; no request after a non-number argument.
    EXPECT_EQ(asked, (std::vector<std::vector<std::int64_t>>{{0, 1000, 0, 100, 0},
                                                             {0, 1000, 0, 100, 1000},
                                                             {0, 1000, 0, 100, 0},
                                                             {1, 2000, 3, 4, 0},
                                                             {500, 500, 0, 0, 0},
                                                             {-1, 10, 0, 0, 0}}));

    // An audio box still without audio, then no audio: checked before the times.
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
    EXPECT_EQ(services.requests[0].run, 0u); // the top level, not a run
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
    EXPECT_GE(ticks - before, 10); // the event loop ran while the script waited
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
    (*pending)({}); // dropped: the run is over
    EXPECT_EQ(host->state(), LuaScriptHost::State::Ready);
    // The script still runs afterwards.
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
    (*pending)({}); // dropped: the helper is gone
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
    // Truncated, trailing bytes, an unknown service, an absurd count.
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

// L6: the legacy native preloads, MoonScript, and the bundled corpus.
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
    // The bundled lfs.moon wraps mkdir, touch and rmdir in tonumber(), which
    // turns their C bool into nil: legacy returns nil, nil on success.
    // attributes() returns its value and a nil error.
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
    // The frame names the .moon source line, mapped through MoonScript's line tables.
    EXPECT_TRUE(run.message.contains(QStringLiteral("File \"%1\", line 7").arg(fixture("moon-macro.moon"))))
        << run.message.toStdString();
}

// DependencyControl's first require in a fresh configuration runs its
// update check, which fetches live third-party feeds; with them reachable,
// DependencyControl's own UpdateFeed fails on the current Aegisub-Motion feed
// ("attempt to index a nil value" in its template expansion), in any host and
// at load or macro time alike (the check runs once per update interval).
// The tests keep the updater off so what they observe does not depend on the
// network or on those feeds (the legacy capture's check evidently reached none).
void disableDependencyControlUpdater(const QString &automationDir)
{
    QDir().mkpath(automationDir + QStringLiteral("/config"));
    QFile f(automationDir + QStringLiteral("/config/l0.DependencyControl.json"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"config": {"updaterEnabled": false}})");
}

// Every bundled Autoload script, loaded unchanged. The results are written to
// an artifact (A33-compat corpus); each must load and register its macros.
TEST_F(LuaHelper, BundledScriptsLoadUnchanged)
{
    QDir dir(QStringLiteral(HIKARI_AUTOLOAD_DIR));
    const QStringList files = dir.entryList({QStringLiteral("*.lua"), QStringLiteral("*.moon")}, QDir::Files, QDir::Name);
    ASSERT_FALSE(files.isEmpty());
    QJsonArray corpus;
    std::map<QString, std::pair<LuaScriptHost::State, QStringList>> results;
    // ?user and ?data resolve to an Automation directory laid out as the
    // package ships it (log and autosave beside the script tree), as legacy
    // decode_path does; scripts such as DependencyControl write there.
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
    // Every bundled script, including the third-party Aegisub-Motion and
    // DependencyControl Toolbox (with DependencyControl's native modules).
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

// S3/L6: the legacy capture probe (tools/legacy-capture/automation) run in
// this host over the same corpus; its JSON line is the rewrite side of the
// legacy comparison (artifacts/automation-capture-corpus.json).
// L6: the host's load-time environment (aegisub API, decode_path answered
// during the load, include path, native modules) is enough for
// DependencyControl, as in legacy, where the capture probe required it from
// its top level.
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

// L6: the LuaJIT build as the legacy Windows build had it, on every platform
// (L6-lua52-linux): Lua 5.2 extensions (table.pack, __ipairs, which
// LibLyger's ipairs(sub) and DependencyControl's Functional need; break
// anywhere, which MoonScript's `continue` needs) and no string.buffer. The
// legacy Linux package's distribution LuaJIT had neither (its capture:
// table.pack nil, ipairs of the subtitles object fails); that is the
// approved departure.
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
    // The helper process inherits the environment, as the legacy app's Lua does.
    qputenv("HIKARI_CAPTURE_OUT", output.toLocal8Bit());
    qputenv("HIKARI_CAPTURE_CORPUS", list.toLocal8Bit());
    // As in the legacy capture: once while the host loads the probe, then from its macro.
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
    // The load-time record compares with the legacy capture (taken the same way).
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
    // Every bundled script loads and registers, as in the host's own load.
    const QJsonArray scripts = result.value(QStringLiteral("scripts")).toArray();
    EXPECT_EQ(scripts.size(), files.size());
    for (const auto &value : scripts) {
        const QJsonObject script = value.toObject();
        const std::string file = script.value(QStringLiteral("file")).toString().toStdString();
        EXPECT_TRUE(script.value(QStringLiteral("loaded")).toBool())
            << file << ": " << script.value(QStringLiteral("error")).toString().toStdString();
        EXPECT_FALSE(script.value(QStringLiteral("registrations")).toArray().isEmpty()) << file;
    }
    // DependencyControl loads from the probe's top level, as in the legacy capture.
    EXPECT_TRUE(QJsonDocument::fromJson(atLoad).object().value(QStringLiteral("modules")).toObject().value(
        QStringLiteral("l0.DependencyControl")).toObject().value(QStringLiteral("ok")).toBool())
        << atLoad.toStdString();
    for (const char *module : {"aegisub.re", "aegisub.unicode", "lfs", "lpeg", "luabins", "ffi"})
        EXPECT_TRUE(result.value(QStringLiteral("modules")).toObject().value(QLatin1String(module)).toObject().value(
            QStringLiteral("ok")).toBool())
            << module;
}

} // namespace
