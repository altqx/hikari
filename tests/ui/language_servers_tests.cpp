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
    std::optional<core::LineId> m_editing;

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
                [this]() -> LanguageServers::Editing {
                    if (!m_editing)
                        return std::nullopt;
                    return std::pair{application::DocumentId{1}, *m_editing};
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
        m_editing.reset();
    }

    void tagsAreBlankedToSpacesAndCommentsAreEmptyLines()
    {
        QCOMPARE(app::blankTags(QStringLiteral("a {\\i1}b\\Nc\\hd\\ne{x")), QStringLiteral("a      b  c  d  e{x"));
        const app::ServerText blank = app::serverText(m_session->document(), ProblemField::Text, plugins::LanguageServer::Tags::Blank);
        QCOMPARE(blank.text, QStringLiteral("a      bad  end\n\n") + QString::fromUtf8("\xC3\xA9\xF0\x9F\x98\x80 bad"));
        QCOMPARE(blank.lengths, (std::vector<int>{15, 0, 7}));
        QCOMPARE(blank.comments, boost::dynamic_bitset<>(std::string("010")));
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
        const auto stopped = ls->stopped("tester");
        QCOMPARE(stopped.size(), std::size_t(1));
        QCOMPARE(stopped[0].server, std::string("prose"));
        QCOMPARE(stopped[0].title, QStringLiteral("Prose"));
        QVERIFY(ls->stopped("other").empty());
    }

    void aQuickFixStaysInItsLineAndOutOfBlankedTags()
    {
        const app::ServerText text = app::serverText(m_session->document(), ProblemField::Text, plugins::LanguageServer::Tags::Blank);
        const QString uri = QStringLiteral("u");
        const auto change = [&](int line, int from, int toLine, int to, const QString &insert) {
            return QJsonObject{{QStringLiteral("range"), QJsonObject{{QStringLiteral("start"), at(line, from)}, {QStringLiteral("end"), at(toLine, to)}}},
                               {QStringLiteral("newText"), insert}};
        };
        const auto action = [&](QJsonArray edits, const QString &where = QStringLiteral("u")) {
            return QJsonObject{{QStringLiteral("title"), QStringLiteral("Fix")},
                               {QStringLiteral("kind"), QStringLiteral("quickfix")},
                               {QStringLiteral("edit"), QJsonObject{{QStringLiteral("changes"), QJsonObject{{where, edits}}}}}};
        };
        const auto fix = [&](const QJsonObject &a, int line = 0, plugins::LanguageServer::Tags tags = plugins::LanguageServer::Tags::Blank) {
            return app::quickFix(a, uri, text, line, tags);
        };

        const auto good = fix(action({change(0, 7, 0, 10, QStringLiteral("good"))}));
        QVERIFY(good);
        QCOMPARE(good->title, std::string("Fix"));
        QCOMPARE(good->edits, (std::vector<application::ProblemEdit>{{7, 10, "good"}}));
        QVERIFY(!fix(action({change(0, 3, 0, 4, QStringLiteral("x"))})));
        QVERIFY(fix(action({change(0, 3, 0, 4, QStringLiteral("x"))}), 0, plugins::LanguageServer::Tags::Keep));
        QVERIFY(fix(action({change(0, 10, 0, 10, QStringLiteral("!"))})));
        QVERIFY(!fix(action({change(0, 11, 0, 11, QStringLiteral("!"))})));
        QVERIFY(!fix(action({change(0, 7, 1, 0, QString())})));
        QVERIFY(!fix(action({change(2, 4, 2, 7, QStringLiteral("good"))})));
        QVERIFY(!fix(action({change(0, 7, 0, 10, QStringLiteral("a\nb"))})));
        QVERIFY(!fix(action({change(0, 7, 0, 16, QStringLiteral("good"))})));
        QVERIFY(!fix(action({change(0, 7, 0, 10, QStringLiteral("good"))}, QStringLiteral("other"))));
        QVERIFY(!fix(action({change(1, 0, 1, 0, QStringLiteral("x"))}), 1));
        QVERIFY(!fix(action({change(0, 7, 0, 9, QStringLiteral("x")), change(0, 8, 0, 10, QStringLiteral("y"))})));
        QJsonObject command = action({change(0, 7, 0, 10, QStringLiteral("good"))});
        command.insert(QStringLiteral("command"), QJsonObject{{QStringLiteral("command"), QStringLiteral("run")}});
        QVERIFY(!fix(command));
        QJsonObject refactor = action({change(0, 7, 0, 10, QStringLiteral("good"))});
        refactor.insert(QStringLiteral("kind"), QStringLiteral("refactor"));
        QVERIFY(!fix(refactor));
        const QJsonObject documentChanges{
            {QStringLiteral("title"), QStringLiteral("Both")},
            {QStringLiteral("edit"),
             QJsonObject{{QStringLiteral("documentChanges"),
                          QJsonArray{QJsonObject{{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri}, {QStringLiteral("version"), 1}}},
                                                 {QStringLiteral("edits"), QJsonArray{change(0, 13, 0, 15, QStringLiteral("ND")),
                                                                                      change(0, 0, 0, 1, QStringLiteral("A"))}}}}}}}};
        const auto both = fix(documentChanges);
        QVERIFY(both);
        QCOMPARE(both->edits, (std::vector<application::ProblemEdit>{{0, 1, "A"}, {13, 15, "ND"}}));
    }

    void completionsAndHoversReadWhatServersAnswer()
    {
        const QString text = QStringLiteral("say \\fa hello");
        const QJsonObject list{
            {QStringLiteral("isIncomplete"), false},
            {QStringLiteral("items"),
             QJsonArray{
                 QJsonObject{{QStringLiteral("label"), QStringLiteral("hello")}, {QStringLiteral("sortText"), QStringLiteral("b")}},
                 QJsonObject{{QStringLiteral("label"), QStringLiteral("fad")},
                             {QStringLiteral("sortText"), QStringLiteral("a")},
                             {QStringLiteral("insertTextFormat"), 2},
                             {QStringLiteral("detail"), QStringLiteral("Fade")},
                             {QStringLiteral("textEdit"),
                              QJsonObject{{QStringLiteral("newText"), QStringLiteral("\\fad(${1:200})")},
                                          {QStringLiteral("range"), QJsonObject{{QStringLiteral("start"), at(3, 4)}, {QStringLiteral("end"), at(3, 7)}}}}}},
                 QJsonObject{{QStringLiteral("label"), QStringLiteral("elsewhere")},
                             {QStringLiteral("textEdit"),
                              QJsonObject{{QStringLiteral("newText"), QStringLiteral("x")},
                                          {QStringLiteral("range"), QJsonObject{{QStringLiteral("start"), at(4, 0)}, {QStringLiteral("end"), at(4, 0)}}}}}},
                 QJsonObject{{QStringLiteral("label"), QString()}},
             }},
        };
        const auto items = app::serverCompletions(list, 3, text, 10, QStringLiteral("Prose"));
        QCOMPARE(items.size(), std::size_t(2));
        QCOMPARE(items[0].label, QStringLiteral("fad"));
        QCOMPARE(items[0].insert, QStringLiteral("\\fad(${1:200})"));
        QCOMPARE(items[0].from, 4);
        QCOMPARE(items[0].to, 7);
        QVERIFY(items[0].snippet);
        QCOMPARE(items[0].detail, QStringLiteral("Fade"));
        QCOMPARE(items[0].source, QStringLiteral("Prose"));
        QCOMPARE(items[1].label, QStringLiteral("hello"));
        QCOMPARE(items[1].insert, QStringLiteral("hello"));
        QCOMPARE(items[1].from, 8);
        QCOMPARE(items[1].to, 10);
        QVERIFY(!items[1].snippet);
        QCOMPARE(app::serverCompletions(QJsonArray{QJsonObject{{QStringLiteral("label"), QStringLiteral("x")}}}, 0, text, 0, {}).size(),
                 std::size_t(1));

        const auto hover = app::serverHover(
            QJsonObject{{QStringLiteral("contents"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("markdown")}, {QStringLiteral("value"), QStringLiteral("Fade")}}},
                        {QStringLiteral("range"), QJsonObject{{QStringLiteral("start"), at(3, 4)}, {QStringLiteral("end"), at(3, 7)}}}},
            3, text, 5, QStringLiteral("Prose"));
        QVERIFY(hover);
        QCOMPARE(*hover, (app::HoverText{QStringLiteral("Fade"), QStringLiteral("Prose"), 4, 7}));
        const auto word = app::serverHover(QJsonObject{{QStringLiteral("contents"), QJsonArray{QStringLiteral("one"), QJsonObject{{QStringLiteral("value"), QStringLiteral("two")}}}}},
                                           3, text, 10, {});
        QCOMPARE(*word, (app::HoverText{QStringLiteral("one\n\ntwo"), QString(), 8, 13}));
        QVERIFY(!app::serverHover(QJsonObject{{QStringLiteral("contents"), QString()}}, 3, text, 0, {}));
    }

    void aServerFixesCompletesAndHovers()
    {
        if (!backends::sandbox::systemCoverage(backends::sandbox::Network::None).complete())
            QSKIP("This machine can't confine programs.");
        auto ls = servers();
        ls->serversChanged();
        QTRY_VERIFY_WITH_TIMEOUT(held().size() == 2 && !held()[0].problem.fixes.empty() && !held()[1].problem.fixes.empty(), 20000);
        const auto problems = held();
        QCOMPARE(problems[0].problem.fixes.size(), std::size_t(2));
        QCOMPARE(problems[0].problem.fixes[0].title, std::string("Use good"));
        QCOMPARE(problems[0].problem.fixes[0].edits, (std::vector<application::ProblemEdit>{{7, 10, "good"}}));
        QCOMPARE(problems[0].problem.fixes[1].title, std::string("Into the start"));
        QCOMPARE(problems[1].problem.fixes[0].edits, (std::vector<application::ProblemEdit>{{4, 7, "good"}}));
        QCOMPARE(logged(QStringLiteral("actions hikari://document/1/text ")), 2);

        QCOMPARE(ls->triggers(QStringLiteral("text")), QStringList{QStringLiteral("\\")});
        QVERIFY(ls->triggers(QStringLiteral("translation")).isEmpty());
        QVERIFY(ls->hovers(QStringLiteral("text")));
        QVERIFY(!ls->hovers(QStringLiteral("translation")));

        const core::LineRecord *third = m_session->document().lines()[2];
        const QString field = QString::fromUtf8("\xC3\xA9\xF0\x9F\x98\x80 bad");
        std::optional<std::vector<app::CompletionItem>> items;
        ls->complete({1, third->id.value, QStringLiteral("text"), field, 4, QStringLiteral("\\")},
                     [&](std::vector<app::CompletionItem> answer) { items = std::move(answer); });
        QTRY_VERIFY_WITH_TIMEOUT(items.has_value(), 5000);
        QCOMPARE(logged(QStringLiteral("complete hikari://document/1/text 2 4 '\\'")), 1);
        QCOMPARE(items->size(), std::size_t(2));
        QCOMPARE((*items)[0].label, QStringLiteral("\\fad"));
        QCOMPARE((*items)[0].from, 3);
        QCOMPARE((*items)[0].to, 4);
        QVERIFY((*items)[0].snippet);
        QCOMPARE((*items)[1].label, QStringLiteral("zebra"));
        QCOMPARE((*items)[1].source, QStringLiteral("Prose"));

        items.reset();
        ls->complete({1, third->id.value, QStringLiteral("text"), field, 4, QStringLiteral("x")},
                     [&](std::vector<app::CompletionItem> answer) { items = std::move(answer); });
        QVERIFY(items && items->empty());

        std::optional<std::optional<app::HoverText>> hover;
        ls->hover({1, third->id.value, QStringLiteral("text"), field, 5, {}},
                  [&](std::optional<app::HoverText> answer) { hover = std::move(answer); });
        QTRY_VERIFY_WITH_TIMEOUT(hover.has_value(), 5000);
        QCOMPARE(**hover, (app::HoverText{QStringLiteral("hover at 5"), QStringLiteral("Prose"), 0, 3}));

        hover.reset();
        ls->hover({1, m_session->document().lines()[1]->id.value, QStringLiteral("text"), QStringLiteral("bad note"), 1, {}},
                  [&](std::optional<app::HoverText> answer) { hover = std::move(answer); });
        QVERIFY(hover && !*hover);
    }

    void aLineScopeServerSeesTheEditedLineAlone()
    {
        if (!backends::sandbox::systemCoverage(backends::sandbox::Network::None).complete())
            QSKIP("This machine can't confine programs.");
        m_manifest.languageServers.front().scope = plugins::LanguageServer::Scope::Line;
        m_servers = {{"tester", QStringLiteral("Tester"), m_manifest.languageServers.front(), 1}};
        const core::LineId first = m_session->document().lines()[0]->id;
        const core::LineId third = m_session->document().lines()[2]->id;
        auto ls = servers();
        ls->serversChanged();
        QCOMPARE(logged(QStringLiteral("init ")), 0);

        m_editing = first;
        ls->sync();
        QTRY_COMPARE_WITH_TIMEOUT(held().size(), std::size_t(1), 20000);
        QCOMPARE(held()[0].problem.line, first);
        QCOMPARE(*held()[0].problem.from, std::size_t(7));
        const QString uri = QStringLiteral("hikari://document/1/line/%1/text").arg(first.value);
        QCOMPARE(logged(QStringLiteral("open %1 %2 a      bad  end").arg(uri).arg(m_session->revision())), 1);

        m_editing = third;
        ls->sync();
        QTRY_COMPARE_WITH_TIMEOUT(logged(QStringLiteral("close %1").arg(uri)), 1, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(held().size() == 1 && held()[0].problem.line == third, 20000);
        QCOMPARE(*held()[0].problem.from, std::size_t(4));

        m_editing.reset();
        ls->sync();
        QVERIFY(held().empty());
        QTRY_COMPARE_WITH_TIMEOUT(logged(QStringLiteral("close hikari://document/1/line/%1/text").arg(third.value)), 1, 20000);
        QCOMPARE(logged(QStringLiteral("init ")), 1);
    }
};

QTEST_GUILESS_MAIN(LanguageServersTests)
#include "language_servers_tests.moc"
