#include "hikari/app/plugin_programs.h"
#include "hikari/backends/plugin_process.h"
#include "hikari/backends/program_plugin_link.h"
#include "hikari/backends/program_process.h"
#include "hikari/backends/sandbox.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace hikari;
using app::PluginPrograms;
using backends::PluginProcess;
using backends::ProgramPluginLink;

class ProgramPluginTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_root;
    plugins::Manifest m_manifest;
    PluginPrograms m_programs;
    QStringList m_log;
    std::vector<PluginProcess::ServiceRequest> m_served;
    std::function<std::string(const PluginProcess::ServiceRequest &)> m_answer;

    QString folder(const QString &name) const { return m_root.filePath(name); }

    void useVariant(const std::string &variant)
    {
        plugins::ProgramRun &run = m_manifest.programs.front().platforms.begin()->second;
        run.args = {"rpc", variant};
    }

    std::unique_ptr<PluginProcess> plugin(bool background = false, ProgramPluginLink::Limits limits = {})
    {
        auto process = std::make_unique<PluginProcess>(QString(), [] {
            backends::plugin::LoadRequest load;
            load.id = "tester";
            load.version = "1.0.0";
            load.locale = "pl";
            load.memoryLimit = 1;
            load.settings = R"({"name":"Ann"})";
            load.commands = {"hello", "fail", "subscribe", "edit", "bad", "count"};
            return load;
        });
        process->setLinkFactory([this, background, limits]() -> std::expected<std::unique_ptr<backends::PluginLink>, QString> {
            auto prepared = m_programs.prepare("tester", "main");
            if (!prepared)
                return std::unexpected(QString::fromStdString(prepared.error().message));
            auto link = std::make_unique<ProgramPluginLink>(std::move(*prepared), background, limits);
            connect(link.get(), &ProgramPluginLink::logLine, this, [this](const QString &line) { m_log << line; });
            return link;
        });
        process->setServiceHandler([this](const PluginProcess::ServiceRequest &request, PluginProcess::ServiceReply reply) {
            m_served.push_back(request);
            reply(m_answer ? m_answer(request) : std::string(R"({"result":{"lines":[]}})"));
        });
        return process;
    }

    QString logged(const QString &prefix) const
    {
        for (const QString &line : m_log)
            if (line.startsWith(prefix))
                return line.mid(prefix.size());
        return {};
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
        m_programs.bind({
            [this](const std::string &id) -> std::optional<PluginPrograms::Plugin> {
                if (id != "tester")
                    return std::nullopt;
                return PluginPrograms::Plugin{&m_manifest, folder(QStringLiteral("package")), folder(QStringLiteral("data")),
                                              folder(QStringLiteral("cache")), api::Json::object()};
            },
            [] { return false; },
            {},
            nullptr,
            {},
            {},
        });
    }

    void init()
    {
        m_manifest = {};
        m_manifest.id = "tester";
        m_manifest.mainProgram = "main";
        m_manifest.permissions.programs = "Tests";
        plugins::Program program;
        program.id = "main";
        program.title = "Tester";
        program.api = true;
        program.platforms[std::string(plugins::thisPlatform())] = plugins::ProgramRun{
            "bin/" + QFileInfo(QStringLiteral(HIKARI_TEST_PROGRAM)).fileName().toStdString(), std::nullopt, {"rpc"}, {}};
        m_manifest.programs.push_back(std::move(program));
        m_log.clear();
        m_served.clear();
        m_answer = {};
    }

    void itInitializesThenRunsCommandsThatCallHikariSub()
    {
        auto process = plugin();
        QSignalSpy loaded(process.get(), &PluginProcess::loaded);
        QSignalSpy finished(process.get(), &PluginProcess::finished);
        process->start();
        QVERIFY(loaded.wait(20000));
        QCOMPARE(process->handledCommands().size(), std::size_t(6));
        const QJsonObject init = QJsonDocument::fromJson(logged(QStringLiteral("init ")).toUtf8()).object();
        const QJsonObject params = init.value(QStringLiteral("params")).toObject();
        QCOMPARE(params.value(QStringLiteral("protocol")).toInt(), 1);
        QCOMPARE(params.value(QStringLiteral("locale")).toString(), QStringLiteral("pl"));
        QCOMPARE(params.value(QStringLiteral("plugin")).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("tester"));
        QCOMPARE(params.value(QStringLiteral("settings")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Ann"));

        QVERIFY(process->run("hello"));
        QVERIFY(finished.wait(20000));
        QCOMPARE(finished.first().first().toString(), QStringLiteral("hello"));
        QCOMPARE(m_served.size(), std::size_t(2));
        QCOMPARE(m_served[0].service, backends::plugin::Service::Notify);
        QCOMPARE(m_served[0].level, backends::plugin::Level::Warning);
        QCOMPARE(m_served[0].text, std::string("hi"));
        QCOMPARE(m_served[1].service, backends::plugin::Service::Call);
        QCOMPARE(m_served[1].name, std::string("lines.list"));
        QVERIFY(logged(QStringLiteral("notify ")).contains(QStringLiteral("\"result\":null")));
        QVERIFY(logged(QStringLiteral("call ")).contains(QStringLiteral("\"result\":{\"lines\":[]}")));
        QVERIFY(logged(QStringLiteral("setting ")).contains(QStringLiteral("\"result\":\"Ann\"")));
    }

    void errorsComeBackAsJsonRpcErrors()
    {
        m_answer = [](const PluginProcess::ServiceRequest &) {
            return std::string(R"({"error":{"code":"unknown_operation","message":"there is no operation"}})");
        };
        auto process = plugin();
        QSignalSpy failed(process.get(), &PluginProcess::commandFailed);
        QSignalSpy finished(process.get(), &PluginProcess::finished);
        process->start();
        QVERIFY(process->run("hello"));
        QVERIFY(finished.wait(20000));
        QVERIFY(logged(QStringLiteral("call ")).contains(QStringLiteral("\"code\":-32601")));
        QVERIFY(process->run("fail"));
        QVERIFY(failed.wait(20000));
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("it broke"));
    }

    void eventsPanelsAndSubscriptionsReachTheProgram()
    {
        m_answer = [](const PluginProcess::ServiceRequest &) { return std::string(); };
        auto process = plugin();
        QSignalSpy loaded(process.get(), &PluginProcess::loaded);
        QSignalSpy finished(process.get(), &PluginProcess::finished);
        process->start();
        QVERIFY(loaded.wait(20000));
        QVERIFY(process->run("subscribe"));
        QVERIFY(finished.wait(20000));
        QCOMPARE(m_served.front().service, backends::plugin::Service::Subscribe);
        QCOMPARE(m_served.front().name, std::string("document.changed"));
        QVERIFY(logged(QStringLiteral("subscribe ")).contains(QStringLiteral("\"result\":null")));
        QVERIFY(process->push("document.changed", R"({"document":1})"));
        QVERIFY(process->deliver("panel:main", R"({"kind":"click","id":"go"})", {}));
        QTRY_VERIFY_WITH_TIMEOUT(m_log.filter(QStringLiteral("\"method\":\"panel\"")).size() == 1, 20000);
        const QString event = m_log.filter(QStringLiteral("\"method\":\"event\"")).value(0);
        QVERIFY(event.contains(QStringLiteral("\"name\":\"document.changed\"")));
        QVERIFY(m_log.filter(QStringLiteral("\"panel\":\"main\"")).size() == 1);
    }

    void anEditTakesTheTurnAndMakesOneGroup()
    {
        m_answer = [](const PluginProcess::ServiceRequest &request) {
            if (request.service != backends::plugin::Service::Call)
                return std::string();
            return std::string(request.name == "document.group_end" ? R"({"result":{"steps":2}})" : R"({"result":{}})");
        };
        auto process = plugin();
        QSignalSpy finished(process.get(), &PluginProcess::finished);
        process->start();
        QVERIFY(process->run("edit"));
        QVERIFY(finished.wait(20000));
        QCOMPARE(m_served.size(), std::size_t(4));
        QCOMPARE(m_served[0].service, backends::plugin::Service::Lock);
        QCOMPARE(m_served[1].name, std::string("document.group_begin"));
        QCOMPARE(m_served[1].json, std::string(R"({"name":"Fix"})"));
        QCOMPARE(m_served[2].name, std::string("document.group_end"));
        QCOMPARE(m_served[3].service, backends::plugin::Service::Unlock);
        QVERIFY(logged(QStringLiteral("end ")).contains(QStringLiteral("\"result\":{\"steps\":2}")));
    }

    void callsOutsideARequestNeedTheBackgroundPermission()
    {
        useVariant("early");
        auto quiet = plugin();
        QSignalSpy loaded(quiet.get(), &PluginProcess::loaded);
        quiet->start();
        QVERIFY(loaded.wait(20000));
        QTRY_VERIFY_WITH_TIMEOUT(logged(QStringLiteral("early ")).contains(QStringLiteral("not_background")), 20000);
        QVERIFY(m_served.empty());
        quiet->end();

        m_log.clear();
        auto background = plugin(true);
        QSignalSpy started(background.get(), &PluginProcess::loaded);
        background->start();
        QVERIFY(started.wait(20000));
        QTRY_VERIFY_WITH_TIMEOUT(!logged(QStringLiteral("early ")).isEmpty(), 20000);
        QCOMPARE(m_served.front().text, std::string("too soon"));
        QVERIFY(logged(QStringLiteral("early ")).contains(QStringLiteral("\"result\":null")));
    }

    void anythingButJsonRpcOnStdoutStopsIt()
    {
        auto process = plugin();
        QSignalSpy stopped(process.get(), &PluginProcess::stopped);
        process->start();
        QVERIFY(process->run("bad"));
        QVERIFY(stopped.wait(20000));
        QCOMPARE(stopped.first().first().value<PluginProcess::StopReason>(), PluginProcess::StopReason::Crashed);
        QVERIFY(process->diagnostics().contains("other than JSON-RPC"));
    }

    void aProgramThatNeverInitializesDidntStart()
    {
        useVariant("silent");
        auto process = plugin(false, ProgramPluginLink::Limits{.initializeMs = 300});
        QSignalSpy failed(process.get(), &PluginProcess::loadFailed);
        process->start();
        QVERIFY(failed.wait(20000));
        QCOMPARE(process->state(), PluginProcess::State::LoadFailed);
    }

    void theTypeScriptSampleCountsWords()
    {
#ifndef HIKARI_WORD_COUNT
        QSKIP("Only Linux builds with Bun compile the sample.");
#else
        if (!QFileInfo::exists(QStringLiteral(HIKARI_WORD_COUNT)))
            QSKIP("hikari_word_count_sample_build hasn't compiled the sample.");
        const QString sample = folder(QStringLiteral("package/bin/")) + QFileInfo(QStringLiteral(HIKARI_WORD_COUNT)).fileName();
        QFile::remove(sample);
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_WORD_COUNT), sample));
        QFile::setPermissions(sample, QFile::permissions(sample) | QFileDevice::ExeOwner);
        m_manifest.programs.front().platforms.begin()->second = plugins::ProgramRun{
            "bin/" + QFileInfo(sample).fileName().toStdString(), std::nullopt, {}, {}};
        m_answer = [](const PluginProcess::ServiceRequest &request) {
            return std::string(request.service == backends::plugin::Service::Call
                                   ? R"({"result":{"revision":1,"lines":[{"id":1,"row":1,"text":"one two"},{"id":2,"row":2,"text":"three"}]}})"
                                   : "");
        };
        auto process = plugin();
        QSignalSpy finished(process.get(), &PluginProcess::finished);
        process->start();
        QVERIFY(process->run("count"));
        QVERIFY(finished.wait(20000));
        QCOMPARE(m_served.size(), std::size_t(2));
        QCOMPARE(m_served[0].name, std::string("lines.list"));
        QCOMPARE(m_served[1].service, backends::plugin::Service::Notify);
        QCOMPARE(m_served[1].text, std::string("3 words in 2 lines"));
#endif
    }

    void aMissingProgramFailsTheLoad()
    {
        m_manifest.programs.front().platforms.begin()->second.path = "bin/nothing";
        auto process = plugin();
        QSignalSpy failed(process.get(), &PluginProcess::loadFailed);
        process->start();
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("missing")));
    }
};

QTEST_GUILESS_MAIN(ProgramPluginTests)
#include "program_plugin_tests.moc"
