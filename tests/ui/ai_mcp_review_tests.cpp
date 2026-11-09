#include "hikari/app/application.h"
#include "hikari/application/ai_providers.h"
#include "hikari/backends/ai_mcp_bridge.h"
#include "hikari/backends/ai_sidecar_host.h"

#include "docking.h"
#include "settings_store.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <vector>

using namespace hikari;
namespace ai = hikari::application::ai;

namespace {

class NoKeys final : public ai::KeyStore {
public:
    void read(const std::string &, std::function<void(ai::KeyResult)> done) override { done(ai::KeyError::NotFound); }
    void write(const std::string &, const std::string &, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        done(std::nullopt);
    }
    void remove(const std::string &, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        done(std::nullopt);
    }
};

class NoConfigs final : public ai::ConfigStore {
public:
    ai::Config load() override { return {}; }
    bool save(const ai::Config &) override { return true; }
};

}

// An agent outside HikariSub, on `hikarisub --mcp`'s bridge, edits through the real hikari-ai into the running app.
class AiMcpReviewTest : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    std::unique_ptr<backends::ai::McpBridge> bridge;
    std::vector<api::Json> bridged;

    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    application::EditSession &session() const
    {
        return *application->files().session(*application->workspace().editingTarget());
    }
    std::u8string textAt(std::size_t row) const { return session().document().lines().at(row)->text; }
    QVariant shell(const QString &code)
    {
        QQmlExpression expression(qmlContext(window()), window(), code);
        const QVariant out = expression.evaluate();
        if (expression.hasError())
            qWarning() << expression.error();
        return out;
    }
    api::Json ask(const QString &message)
    {
        const std::size_t before = bridged.size();
        bridge->fromAgent(message.simplified().toUtf8() + '\n');
        if (!QTest::qWaitFor([&] { return bridged.size() > before; }, 30'000))
            return {};
        const api::Json *result = bridged.back().find("result");
        if (!result)
            qWarning() << bridged.back().dump().c_str();
        return result ? *result : api::Json();
    }
    bool agentWrites(const char *text)
    {
        const api::Json applied = ask(QStringLiteral(R"({"jsonrpc": "2.0", "id": 9, "method": "tools/call", "params": {
            "name": "document_apply", "arguments": {"revision": %1, "name": "Tightened", "edits": [
            {"op": "set_line", "line": %2, "fields": {"text": "%3"}}]}}})")
                                          .arg(session().revision())
                                          .arg(session().document().lines().front()->id.value)
                                          .arg(QString::fromUtf8(text)));
        const api::Json *error = applied.isObject() ? applied.find("isError") : nullptr;
        return applied.isObject() && !(error && error->asBool());
    }
    qulonglong waitingChange() const
    {
        const QVariantList turns = application->review()->turns();
        if (turns.isEmpty())
            return 0;
        const QVariantList changes = turns.front().toMap().value(QStringLiteral("changes")).toList();
        return changes.isEmpty() ? 0 : changes.front().toMap().value(QStringLiteral("change")).toULongLong();
    }

private slots:
    void init()
    {
        app::AiFeatures::Ports ports;
        ports.keyStore = [] { return std::make_unique<NoKeys>(); };
        ports.configStore = [] { return std::make_unique<NoConfigs>(); };
        ports.localSidecar = backends::ai::SidecarInstall{QStringLiteral("0.0.0-dev"), {}, {}, {}};
        ports.sidecarHost = [this](const backends::ai::SidecarInstall &) {
            backends::ai::AiSidecarHost::Options o;
            o.program = QStringLiteral(HIKARI_AI_RUNTIME_FILE);
            o.arguments = {QStringLiteral(HIKARI_AI_BUNDLE_FILE)};
            o.workingDirectory = root.filePath(QStringLiteral("ai/work"));
            o.buildId = HIKARI_AI_BUILD_ID;
            o.handshakeMs = 30'000;
            return std::make_unique<backends::ai::AiSidecarHost>(std::move(o), &application->apiHost()->api(),
                                                                 api::Caller{.id = "ai", .label = "AI", .reviewed = true});
        };
        app::Application::Options options;
        options.settingsFile = root.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        options.aiPorts = std::move(ports);
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());

        const QString path = root.filePath(QStringLiteral("episode.ass"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                   "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                   "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,first\n");
        file.close();
        QVERIFY(application->openFile(path));

        application->settingsStore()->set(app::AiMcp::kSetting, true);
        auto *mcp = qobject_cast<app::AiMcp *>(application->aiSettings()->mcp());
        QVERIFY(mcp && mcp->server());
        bridged.clear();
        bridge = std::make_unique<backends::ai::McpBridge>([this, buffer = std::make_shared<QByteArray>()](const QByteArray &bytes) {
            *buffer += bytes;
            for (qsizetype end; (end = buffer->indexOf('\n')) >= 0; buffer->remove(0, end + 1))
                bridged.push_back(api::Json::parse(buffer->left(end).toStdString()).value_or(api::Json()));
        });
        QString error;
        QVERIFY2(bridge->open(mcp->endpoint(), &error), qPrintable(error));
        const api::Json initialized = ask(QStringLiteral(R"({"jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {"protocolVersion": "2025-11-25", "capabilities": {},
            "clientInfo": {"name": "claude-code", "title": "Claude Code", "version": "3"}}})"));
        QVERIFY(initialized.isObject());
        bridge->fromAgent(R"({"jsonrpc": "2.0", "method": "notifications/initialized"})" "\n");
    }
    void cleanup()
    {
        bridge.reset();
        engine.reset();
        application.reset();
    }

    void aClaudeCodeEditWaitsInTheReviewPanelUntilKept()
    {
        QVERIFY(agentWrites("changed by Claude Code"));
        QCOMPARE(textAt(0), std::u8string(u8"changed by Claude Code"));
        QCOMPARE(application->review()->waiting(), 1);
        QCOMPARE(session().history().back().name, std::string("Claude Code: Tightened"));

        QVERIFY(shell(QStringLiteral("root.showPanel(reviewDock), reviewDock.isOpen")).toBool());
        QTRY_VERIFY(shell(QStringLiteral("reviewLoader.item !== null && reviewLoader.item.here.length === 1")).toBool());
        QCOMPARE(shell(QStringLiteral("reviewLoader.item.here[0].changes[0].caller")).toString(), QStringLiteral("Claude Code"));
        QCOMPARE(shell(QStringLiteral("reviewLoader.item.here[0].changes[0].name")).toString(), QStringLiteral("Tightened"));

        const std::size_t steps = session().historySize();
        QVERIFY(shell(QStringLiteral("reviewLoader.item.review.keep(reviewLoader.item.here[0].changes[0].change, -1), true")).toBool());
        QCOMPARE(application->review()->waiting(), 0);
        QCOMPARE(session().historySize(), steps);
        QCOMPARE(textAt(0), std::u8string(u8"changed by Claude Code"));
        QTRY_COMPARE(shell(QStringLiteral("reviewLoader.item.here[0].changes[0].waiting")).toInt(), 0);
    }

    void undoInTheReviewPanelPutsTheLineBackAsOneStep()
    {
        QVERIFY(agentWrites("changed by Claude Code"));
        const qulonglong change = waitingChange();
        QVERIFY(change != 0);
        const std::size_t steps = session().historySize();
        QVERIFY(application->review()->undo(change, -1, false));
        QCOMPARE(textAt(0), std::u8string(u8"first"));
        QCOMPARE(session().historySize(), steps + 1);
        QCOMPARE(session().history().back().name, std::string("Undo Claude Code: Tightened"));
        QCOMPARE(application->review()->waiting(), 0);
    }
};

QTEST_MAIN(AiMcpReviewTest)
#include "ai_mcp_review_tests.moc"
