#include "hikari/app/language_servers.h"
#include "hikari/app/plugin_programs.h"
#include "hikari/backends/sandbox.h"
#include "hikari/core/ass_load.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <cstring>

using namespace hikari;
using app::LanguageServers;
using application::Problem;
using application::ProblemField;
using application::ProblemSeverity;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

constexpr std::string_view kThree = "[Events]\n"
                                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a {\\i1}bad\\Nend\n"
                                    "Comment: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,bad note\n"
                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,\xC3\xA9\xF0\x9F\x98\x80 bad\n";

QJsonObject at(int line, int character)
{
    return {{QStringLiteral("line"), line}, {QStringLiteral("character"), character}};
}

QJsonObject diagnostic(QJsonObject start, QJsonObject end, QJsonValue severity, QJsonValue code, QString message)
{
    return {{QStringLiteral("range"), QJsonObject{{QStringLiteral("start"), start}, {QStringLiteral("end"), end}}},
            {QStringLiteral("severity"), severity},
            {QStringLiteral("code"), code},
            {QStringLiteral("message"), message}};
}

}

class LanguageServersTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_root;
    plugins::Manifest m_manifest;
    app::PluginPrograms m_programs;
    QStringList m_log;
    application::Problems m_problems;
    std::vector<app::PluginRuntime::LanguageServer> m_servers;
    std::unique_ptr<application::EditSession> m_session;
    bool m_open = true;

    QString folder(const QString &name) const { return m_root.filePath(name); }

    std::unique_ptr<LanguageServers> servers(LanguageServers::Limits limits = {})
    {
        limits.syncMs = 10;
        return std::make_unique<LanguageServers>(
            LanguageServers::Wiring{
                [this] { return m_servers; },
                [this](const std::string &plugin, const std::string &server)
                    -> std::expected<backends::ProgramProcess::Options, QString> {
                    const auto declared = std::ranges::find(m_manifest.languageServers, server, &plugins::LanguageServer::id);
                    auto options = m_programs.options(plugin, declared->program);
                    if (!options)
                        return std::unexpected(QString::fromStdString(options.error().message));
                    return std::move(*options);
                },
                [](const std::string &) { return *api::Json::parse("{\"style\":{\"level\":\"high\"}}"); },
                [this](const std::string &, const QString &line) { m_log << line; },
                [this] {
                    LanguageServers::Open open;
                    if (m_open)
                        open.emplace_back(application::DocumentId{1}, m_session.get());
                    return open;
                },
                &m_problems,
            },
            limits);
    }

    void useServer(std::vector<std::string> args)
    {
        m_manifest.programs.front().platforms.begin()->second.args = std::move(args);
        m_servers = {{"tester", QStringLiteral("Tester"), m_manifest.languageServers.front(), 1}};
    }

    int logged(const QString &prefix) const
    {
        return int(std::ranges::count_if(m_log, [&](const QString &line) { return line.startsWith(prefix); }));
    }

    QString firstLogged(const QString &prefix) const
    {
        for (const QString &line : m_log)
            if (line.startsWith(prefix))
                return line;
        return {};
    }

    std::vector<application::HeldProblem> held() const { return m_problems.of(application::DocumentId{1}); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        QVERIFY(QDir().mkpath(folder(QStringLiteral("package/bin"))));
        const QString copy = folder(QStringLiteral("package/bin/")) + QFileInfo(QStringLiteral(HIKARI_TEST_PROGRAM)).fileName();
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_TEST_PROGRAM), copy));
        QFile::setPermissions(copy, QFile::permissions(copy) | QFileDevice::ExeOwner);
        m_programs.bind({
            [this](const std::string &id) -> std::optional<app::PluginPrograms::Plugin> {
                if (id != "tester")
                    return std::nullopt;
                return app::PluginPrograms::Plugin{&m_manifest, folder(QStringLiteral("package")), folder(QStringLiteral("data")),
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
        m_manifest.permissions.programs = "Tests";
        plugins::Program program;
        program.id = "ls";
        program.title = "Server";
        program.platforms[std::string(plugins::thisPlatform())] = plugins::ProgramRun{
            "bin/" + QFileInfo(QStringLiteral(HIKARI_TEST_PROGRAM)).fileName().toStdString(), std::nullopt, {"lsp"}, {}};
        m_manifest.programs.push_back(std::move(program));
        plugins::LanguageServer server;
        server.id = "prose";
        server.title = "Prose";
        server.program = "ls";
        server.fields = {"text"};
        server.initializationOptions = *api::Json::parse("{\"mode\":\"strict\"}");
        m_manifest.languageServers.push_back(server);
        m_servers = {{"tester", QStringLiteral("Tester"), server, 1}};
        m_log.clear();
        m_problems = {};
        m_session = std::make_unique<application::EditSession>(load(kThree));
        m_open = true;
    }

    void tagsAreBlankedToSpacesAndCommentsAreEmptyLines()
    {
        QCOMPARE(app::blankTags(QStringLiteral("a {\\i1}b\\Nc\\hd\\ne{x")), QStringLiteral("a      b  c  d  e{x"));
        const app::ServerText blank = app::serverText(m_session->document(), ProblemField::Text, plugins::LanguageServer::Tags::Blank);
        QCOMPARE(blank.text, QStringLiteral("a      bad  end\n\n") + QString::fromUtf8("\xC3\xA9\xF0\x9F\x98\x80 bad"));
        QCOMPARE(blank.lengths, (std::vector<int>{15, 0, 7}));
        QCOMPARE(blank.comments, (std::vector<bool>{false, true, false}));
        const app::ServerText kept = app::serverText(m_session->document(), ProblemField::Text, plugins::LanguageServer::Tags::Keep);
        QVERIFY(kept.text.startsWith(QStringLiteral("a {\\i1}bad\\Nend\n\n")));
    }

    void diagnosticsLandOnTheirLineAndRunningPastItEndWithIt()
    {
        const app::ServerText text = app::serverText(m_session->document(), ProblemField::Text, plugins::LanguageServer::Tags::Blank);
        const QJsonArray diagnostics{
            diagnostic(at(0, 7), at(0, 10), 2, QStringLiteral("Bad"), QStringLiteral("Avoid bad")),
            diagnostic(at(1, 0), at(1, 3), 1, {}, QStringLiteral("on a Comment")),
            diagnostic(at(2, 4), at(3, 1), 3, 7, QStringLiteral("runs on")),
            diagnostic(at(2, 99), at(2, 120), 4, {}, QStringLiteral("past the end")),
            diagnostic(at(5, 0), at(5, 1), 1, {}, QStringLiteral("no such Line")),
            diagnostic(at(0, 0), at(0, 1), 1, {}, QString()),
        };
        const std::vector<Problem> problems = app::diagnosticProblems(diagnostics, text, ProblemField::Text);
        QCOMPARE(problems.size(), std::size_t(3));
        QCOMPARE(problems[0].line, text.lines[0]);
        QCOMPARE(*problems[0].from, std::size_t(7));
        QCOMPARE(*problems[0].to, std::size_t(10));
        QCOMPARE(problems[0].severity, ProblemSeverity::Warning);
        QCOMPARE(problems[0].code, std::string("Bad"));
        QCOMPARE(problems[0].message, std::string("Avoid bad"));
        QVERIFY(problems[0].against == text.fields[0]);
        QCOMPARE(problems[1].line, text.lines[2]);
        QCOMPARE(*problems[1].from, std::size_t(4));
        QCOMPARE(*problems[1].to, std::size_t(7));
        QCOMPARE(problems[1].severity, ProblemSeverity::Info);
        QCOMPARE(problems[1].code, std::string("7"));
        QCOMPARE(*problems[2].from, std::size_t(7));
        QCOMPARE(*problems[2].to, std::size_t(7));
        QCOMPARE(problems[2].severity, ProblemSeverity::Hint);
    }

    void aServerSeesTheDocumentAndItsDiagnosticsBecomeProblems()
    {
        if (!backends::sandbox::systemCoverage(backends::sandbox::Network::None).complete())
            QSKIP("This machine can't confine programs.");
        auto ls = servers();
        ls->serversChanged();
        QTRY_COMPARE_WITH_TIMEOUT(held().size(), std::size_t(2), 20000);
        QCOMPARE(ls->state("tester", "prose"), LanguageServers::State::Running);
        const auto problems = held();
        QCOMPARE(problems[0].source, std::string("plugin:tester/prose"));
        QCOMPARE(m_problems.name("plugin:tester/prose"), std::string("Prose"));
        QCOMPARE(problems[0].problem.line, m_session->document().lines()[0]->id);
        QCOMPARE(*problems[0].problem.from, std::size_t(7));
        QCOMPARE(problems[1].problem.line, m_session->document().lines()[2]->id);
        QCOMPARE(*problems[1].problem.from, std::size_t(4));

        const QString init = firstLogged(QStringLiteral("init "));
        QVERIFY2(init.contains(QStringLiteral("\"positionEncodings\":[\"utf-16\"]")), qPrintable(init));
        QVERIFY2(init.contains(QStringLiteral("\"initializationOptions\":{\"mode\":\"strict\"}")), qPrintable(init));
        QTRY_VERIFY_WITH_TIMEOUT(logged(QStringLiteral("answer ")) == 1, 20000);
        const QString answer = firstLogged(QStringLiteral("answer "));
        QVERIFY2(answer.contains(QStringLiteral("\"result\":[{\"level\":\"high\"},{\"style\":{\"level\":\"high\"}}]")), qPrintable(answer));
        QCOMPARE(logged(QStringLiteral("ready")), 1);
        QCOMPARE(logged(QStringLiteral("look")), 1);
        const QString opened = firstLogged(QStringLiteral("open "));
        QVERIFY2(opened.startsWith(QStringLiteral("open hikari://document/1/text %1 a      bad  end").arg(m_session->revision())),
                 qPrintable(opened));

        const core::LineId first = m_session->document().lines()[0]->id;
        QVERIFY(m_session->editDraftText(first, u8"all good"));
        QVERIFY(m_session->commitDraft());
        ls->sync();
        QTRY_COMPARE_WITH_TIMEOUT(held().size(), std::size_t(1), 20000);
        QCOMPARE(logged(QStringLiteral("change hikari://document/1/text %1 all good").arg(m_session->revision())), 1);
        QCOMPARE(held()[0].problem.line, m_session->document().lines()[2]->id);

        m_open = false;
        ls->sync();
        QTRY_COMPARE_WITH_TIMEOUT(logged(QStringLiteral("close hikari://document/1/text")), 1, 20000);
        QVERIFY(held().empty());

        m_open = true;
        ls->sync();
        QTRY_COMPARE_WITH_TIMEOUT(held().size(), std::size_t(1), 20000);
        m_servers.clear();
        ls->serversChanged();
        QVERIFY(held().empty());
        QTRY_COMPARE_WITH_TIMEOUT(logged(QStringLiteral("shutdown")), 1, 20000);
        QCOMPARE(logged(QStringLiteral("init ")), 1);
    }

    void aRenewedServerStartsAfresh()
    {
        if (!backends::sandbox::systemCoverage(backends::sandbox::Network::None).complete())
            QSKIP("This machine can't confine programs.");
        auto ls = servers();
        ls->serversChanged();
        QTRY_COMPARE_WITH_TIMEOUT(held().size(), std::size_t(2), 20000);
        m_servers.front().revision = 2;
        ls->serversChanged();
        QTRY_COMPARE_WITH_TIMEOUT(logged(QStringLiteral("init ")), 2, 20000);
        QTRY_COMPARE_WITH_TIMEOUT(held().size(), std::size_t(2), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(logged(QStringLiteral("shutdown")), 1, 20000);
    }

    void aCrashingServerStartsAgainThreeTimesThenStops()
    {
        if (!backends::sandbox::systemCoverage(backends::sandbox::Network::None).complete())
            QSKIP("This machine can't confine programs.");
        useServer({"lsp", "crash"});
        auto ls = servers();
        QSignalSpy states(ls.get(), &LanguageServers::statesChanged);
        ls->serversChanged();
        QTRY_COMPARE_WITH_TIMEOUT(ls->state("tester", "prose"), LanguageServers::State::Stopped, 30000);
        QCOMPARE(logged(QStringLiteral("init ")), 4);
        QCOMPARE(logged(QStringLiteral("Prose stopped: ")), 4);
        QVERIFY(!ls->stoppedReason("tester", "prose").isEmpty());
        QVERIFY(held().empty());

        ls->sync();
        QCOMPARE(logged(QStringLiteral("init ")), 4);
        ls->startAgain("tester", "prose");
        QCOMPARE_NE(ls->state("tester", "prose"), LanguageServers::State::Stopped);
        QTRY_COMPARE_WITH_TIMEOUT(ls->state("tester", "prose"), LanguageServers::State::Stopped, 30000);
        QCOMPARE(logged(QStringLiteral("init ")), 8);
        QVERIFY(states.count() > 0);
    }
};

QTEST_GUILESS_MAIN(LanguageServersTests)
#include "language_servers_tests.moc"
