#include "hikari/app/plugin_programs.h"
#include "hikari/backends/sandbox.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace hikari;
using api::Json;
using app::PluginPrograms;

namespace {

const api::Caller kPlugin = [] {
    api::Caller caller{"plugin:tester", "Tester"};
    caller.programs = true;
    return caller;
}();

std::string reasonOf(const api::Result &result)
{
    if (result || !result.error().data.isObject())
        return {};
    const Json *reason = result.error().data.find("reason");
    return reason ? reason->asString() : std::string();
}

}

class PluginProgramsTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_root;
    plugins::Manifest m_manifest;
    std::unique_ptr<PluginPrograms> m_programs;
    std::unique_ptr<api::Api> m_api;
    QStringList m_log;
    bool m_network = false;
    QString m_downloaded;

    QString folder(const QString &name) const { return m_root.filePath(name); }

    plugins::Program program(std::string id, std::vector<std::string> args = {})
    {
        plugins::Program p;
        p.id = id;
        p.title = "Test " + id;
        p.platforms[std::string(plugins::thisPlatform())] = plugins::ProgramRun{"bin/" + QFileInfo(QStringLiteral(HIKARI_TEST_PROGRAM)).fileName().toStdString(),
                                                                                 std::nullopt, std::move(args), {}};
        return p;
    }

    api::Result call(const std::string &operation, const Json &params, const api::Caller &caller = kPlugin)
    {
        std::optional<api::Result> out;
        m_api->call(caller, operation, params, [&out](api::Result result) { out = std::move(result); });
        if (!QTest::qWaitFor([&out] { return out.has_value(); }, 20000))
            return api::fail(api::errors::kUnavailable, "no answer");
        return *out;
    }

private slots:
    void initTestCase()
    {
        if (!backends::sandbox::systemCoverage(backends::sandbox::Network::None).complete())
            QSKIP("This machine can't confine programs.");
        QVERIFY(m_root.isValid());
        QVERIFY(QDir().mkpath(folder(QStringLiteral("package/bin"))));
        const QString copy = folder(QStringLiteral("package/bin/")) + QFileInfo(QStringLiteral(HIKARI_TEST_PROGRAM)).fileName();
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_TEST_PROGRAM), copy));
        QFile::setPermissions(copy, QFile::permissions(copy) | QFileDevice::ExeOwner);
    }

    void init()
    {
        m_manifest = {};
        m_manifest.id = "tester";
        m_manifest.name = "Tester";
        m_manifest.permissions.programs = "Tests";
        m_manifest.programs = {program("echo", {"echo"}), program("args", {"args", "${setting:name}", "${package}"}),
                               program("lines", {"lines"}), program("read", {"read"}), program("flood", {"flood"}),
                               program("noisy", {"noisy"}), program("sleep", {"sleep"})};
        plugins::Program elsewhere = program("elsewhere");
        elsewhere.platforms = {{"plan9-x64", plugins::ProgramRun{"bin/nothing", std::nullopt, {}, {}}}};
        m_manifest.programs.push_back(std::move(elsewhere));
        plugins::Program downloaded = program("downloaded");
        downloaded.platforms.begin()->second = plugins::ProgramRun{
            {}, plugins::ProgramDownload{"https://example.com/a.zip", std::string(64, 'a'), 10, "zip", "a"}, {}, {}};
        m_manifest.programs.push_back(std::move(downloaded));
        m_log.clear();
        m_downloaded.clear();
        m_programs = std::make_unique<PluginPrograms>();
        m_api = std::make_unique<api::Api>(api::Host{.programs = m_programs.get()});
        m_programs->bind({
            [this](const std::string &id) -> std::optional<PluginPrograms::Plugin> {
                if (id != "tester")
                    return std::nullopt;
                return PluginPrograms::Plugin{&m_manifest, folder(QStringLiteral("package")), folder(QStringLiteral("data")),
                                              folder(QStringLiteral("cache")), Json::object({{"name", Json("Ann")}})};
            },
            [this] { return m_network; },
            [this](const std::string &, const QString &line) { m_log << line; },
            m_api.get(),
            {},
            [this](const std::string &, const plugins::ProgramRun &) { return m_downloaded; },
        });
    }

    void cleanup()
    {
        m_programs->end("tester");
        m_api.reset();
        m_programs.reset();
    }

    void runAnswersOutputAndExitCode()
    {
        const api::Result result = call("programs.run", Json::object({{"program", Json("echo")}, {"stdin", Json("hello")}}));
        QVERIFY2(result, result ? "" : result.error().message.c_str());
        QCOMPARE(result->find("exitCode")->asInt(), 3);
        QCOMPARE(result->find("stdout")->asString(), std::string("hello"));
        QCOMPARE(result->find("stderr")->asString(), std::string("a warning\n"));
        QCOMPARE(result->find("encoding")->asString(), std::string("utf8"));
        QCOMPARE(m_log, QStringList{QStringLiteral("a warning")});
    }

    void runTakesAndGivesBase64()
    {
        const api::Result result = call("programs.run", Json::object({{"program", Json("echo")},
                                                                      {"stdin", Json(QByteArray("\x00\xff", 2).toBase64().toStdString())},
                                                                      {"encoding", Json("base64")}}));
        QVERIFY(result);
        QCOMPARE(result->find("stdout")->asString(), QByteArray("\x00\xff", 2).toBase64().toStdString());
        QCOMPARE(result->find("encoding")->asString(), std::string("base64"));
    }

    void argumentsExpandAndTheEnvironmentIsBare()
    {
        qputenv("HIKARI_TEST_SECRET", "x");
        const api::Result result = call("programs.run", Json::object({{"program", Json("args")}, {"args", Json::array({Json("${data}"), Json("${nothing}")})}}));
        qunsetenv("HIKARI_TEST_SECRET");
        QVERIFY(result);
        const QStringList lines = QString::fromStdString(result->find("stdout")->asString()).split(u'\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 6);
        QCOMPARE(lines[0], QStringLiteral("Ann"));
        QCOMPARE(lines[1], QDir::toNativeSeparators(folder(QStringLiteral("package"))));
        QCOMPARE(lines[2], QDir::toNativeSeparators(folder(QStringLiteral("data"))));
        QCOMPARE(lines[3], QStringLiteral("${nothing}"));
#ifdef Q_OS_WIN
        QVERIFY2(lines[4].startsWith(QStringLiteral("TEMP=")) && lines[4].endsWith(QStringLiteral("\\AC\\Temp"), Qt::CaseInsensitive),
                 qPrintable(lines[4]));
#else
        QCOMPARE(lines[4], QStringLiteral("TEMP=") + QDir::toNativeSeparators(folder(QStringLiteral("cache/tmp"))));
#endif
        QCOMPARE(lines[5], QStringLiteral("SECRET=unset"));
    }

    void aFileHandedOverIsReadableAndOthersAreNot()
    {
        QFile handed(folder(QStringLiteral("handed.txt")));
        QVERIFY(handed.open(QIODevice::WriteOnly));
        handed.write("inside");
        handed.close();
        QFile other(folder(QStringLiteral("other.txt")));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.write("outside");
        other.close();

        std::optional<api::Result> out;
        api::ProgramCall read{"read", {"${file:0}"}, {{handed.fileName().toStdString(), false}}};
        m_programs->run(kPlugin, read, [&out](api::Result result) { out = std::move(result); });
        QTRY_VERIFY_WITH_TIMEOUT(out.has_value(), 20000);
        QVERIFY(*out);
        QCOMPARE((*out)->find("stdout")->asString(), std::string("inside"));

        out.reset();
        api::ProgramCall denied{"read", {other.fileName().toStdString()}, {}};
        m_programs->run(kPlugin, denied, [&out](api::Result result) { out = std::move(result); });
        QTRY_VERIFY_WITH_TIMEOUT(out.has_value(), 20000);
        QVERIFY(*out);
        QCOMPARE((*out)->find("exitCode")->asInt(), 2);
    }

    void aFileIndexOutOfRangeIsInvalid()
    {
        std::optional<api::Result> out;
        m_programs->run(kPlugin, api::ProgramCall{"read", {"${file:1}"}, {}}, [&out](api::Result result) { out = std::move(result); });
        QVERIFY(out.has_value());
        QVERIFY(!*out);
        QCOMPARE(out->error().code, std::string(api::errors::kInvalidParams));
    }

    void runEndsAProgramThatTakesTooLong()
    {
        const api::Result result = call("programs.run", Json::object({{"program", Json("sleep")}, {"timeout", Json(1000)}}));
        QVERIFY(!result);
        QCOMPARE(reasonOf(result), std::string("timed_out"));
    }

    void runRefusesTooMuchOutput()
    {
        const api::Result result = call("programs.run", Json::object({{"program", Json("flood")}}));
        QVERIFY(!result);
        QCOMPARE(reasonOf(result), std::string("too_large"));
    }

    void stderrReachesTheLogAtTwentyLinesAMinute()
    {
        QVERIFY(call("programs.run", Json::object({{"program", Json("noisy")}})));
        QCOMPARE(m_log.size(), 20);
        QCOMPARE(m_log.front(), QStringLiteral("line 0"));
    }

    void programsThatCantRunHereAreUnavailable()
    {
        QCOMPARE(reasonOf(call("programs.run", Json::object({{"program", Json("elsewhere")}}))), std::string("platform"));
        QCOMPARE(reasonOf(call("programs.run", Json::object({{"program", Json("downloaded")}}))), std::string("not_downloaded"));
        const api::Result unknown = call("programs.run", Json::object({{"program", Json("nope")}}));
        QVERIFY(!unknown);
        QCOMPARE(unknown.error().code, std::string(api::errors::kNotFound));
    }

    void aDownloadedProgramRunsFromWhereItIsKept()
    {
        const QString name = QFileInfo(QStringLiteral(HIKARI_TEST_PROGRAM)).fileName();
        for (plugins::Program &p : m_manifest.programs)
            if (p.id == "downloaded")
                p.platforms.begin()->second.download->entry = "bin/" + name.toStdString();
        const QString kept = folder(QStringLiteral("kept/0123/bin/")) + name;
        QVERIFY(QDir().mkpath(QFileInfo(kept).absolutePath()));
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_TEST_PROGRAM), kept));
        QFile::setPermissions(kept, QFile::permissions(kept) | QFileDevice::ExeOwner);
        m_downloaded = kept;
        const api::Result result = call("programs.run", Json::object({{"program", Json("downloaded")}, {"args", Json::array({Json("args")})}}));
        QVERIFY2(result, result ? "" : result.error().message.c_str());
        QCOMPARE(result->find("exitCode")->asInt(), 0);
    }

    void onlyPluginsWithThePermissionRunPrograms()
    {
        api::Caller without = kPlugin;
        without.programs = false;
        const api::Result refused = call("programs.run", Json::object({{"program", Json("echo")}}), without);
        QVERIFY(!refused);
        QCOMPARE(refused.error().code, std::string(api::errors::kNotGranted));
        api::Caller ai{"ai:chat", "AI"};
        ai.programs = true;
        QCOMPARE(call("programs.start", Json::object({{"program", Json("echo")}}), ai).error().code,
                 std::string(api::errors::kNotGranted));
    }

    void aStartedProgramTalksThroughEvents()
    {
        std::vector<api::Event> events;
        m_api->subscribe(kPlugin, {"program.output", "program.exited"}, [&events](const api::Event &e) { events.push_back(e); });
        const api::Result started = call("programs.start", Json::object({{"program", Json("lines")}}));
        QVERIFY(started);
        const std::string process = started->find("process")->asString();
        QVERIFY(call("programs.send", Json::object({{"process", Json(process)}, {"data", Json("one\n")}})));
        QTRY_VERIFY_WITH_TIMEOUT(!events.empty(), 20000);
        QCOMPARE(events[0].name, std::string("program.output"));
        QCOMPARE(events[0].data.find("data")->asString(), std::string("got one\n"));
        QCOMPARE(events[0].data.find("stream")->asString(), std::string("stdout"));
        QVERIFY(call("programs.send", Json::object({{"process", Json(process)}, {"close", Json(true)}})));
        QTRY_VERIFY_WITH_TIMEOUT(events.back().name == "program.exited", 20000);
        QCOMPARE(events.back().data.find("exitCode")->asInt(), 0);
        QCOMPARE(events.back().data.find("stopped")->asBool(), false);
        QCOMPARE(call("programs.send", Json::object({{"process", Json(process)}, {"data", Json("x")}})).error().code,
                 std::string(api::errors::kNotFound));
    }

    void stopEndsAStartedProgram()
    {
        std::vector<api::Event> events;
        m_api->subscribe(kPlugin, {"program.exited"}, [&events](const api::Event &e) { events.push_back(e); });
        const api::Result started = call("programs.start", Json::object({{"program", Json("sleep")}}));
        QVERIFY(started);
        const std::string process = started->find("process")->asString();
        api::Caller stranger{"plugin:stranger", "Stranger"};
        stranger.programs = true;
        QCOMPARE(m_programs->stop(stranger, process).error().code, std::string(api::errors::kNotFound));
        QVERIFY(call("programs.stop", Json::object({{"process", Json(process)}})));
        QTRY_COMPARE_WITH_TIMEOUT(events.size(), std::size_t(1), 20000);
        QCOMPARE(events[0].data.find("stopped")->asBool(), true);
    }

    void aPluginKeepsAtMostEightRunning()
    {
        for (int i = 0; i < 8; ++i)
            QVERIFY(call("programs.start", Json::object({{"program", Json("sleep")}})));
        QCOMPARE(reasonOf(call("programs.start", Json::object({{"program", Json("sleep")}}))), std::string("too_many"));
        QCOMPARE(m_programs->started("tester"), std::size_t(8));
        m_programs->end("tester");
        QTRY_COMPARE_WITH_TIMEOUT(m_programs->started("tester"), std::size_t(0), 20000);
    }
};

QTEST_GUILESS_MAIN(PluginProgramsTests)
#include "plugin_programs_tests.moc"
