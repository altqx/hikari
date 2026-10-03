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

} // namespace
