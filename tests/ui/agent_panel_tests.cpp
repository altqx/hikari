#include "hikari/app/agent_controller.h"
#include "hikari/app/ai_features.h"
#include "hikari/app/ai_settings.h"
#include "hikari/app/application.h"
#include "hikari/app/automation_shell.h"
#include "hikari/application/ai_providers.h"
#include "hikari/application/hotkeys.h"
#include "hikari/application/project.h"
#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_antigravity.h"
#include "hikari/backends/ai_agent_cli.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "hikari/backends/ai_signin.h"
#include "hikari/backends/desktop_handoff.h"

#include "docking.h"
#include "line_table_model.h"
#include "qtest_parts.h"
#include "settings_store.h"
#include "shell_controller.h"
#include "side_bar_model.h"
#include "stale_temporary_dirs.h"
#include "theme.h"

#include <QAccessible>
#include <QFile>
#include <QJsonDocument>
#include <QFileInfo>
#include <QProcess>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QtQuickTest/quicktest.h>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <optional>
#include <utility>

using namespace hikari;
namespace ai = application::ai;
using api::Json;

namespace {

constexpr int kPastCodexsDefaultToolTimeoutMs = 75'000;

/// HikariSub's keychain item for `id` in the user's own Secret Service at HIKARI_REAL_AGY_BUS, which vx11's private
/// bus hides; only secret-tool gets that bus, and the secret moves over its pipes only.
class RealKeychain {
public:
    static bool reaches(const std::string &id)
    {
        return id == backends::ai::kAntigravitySecret && !qEnvironmentVariable("HIKARI_REAL_AGY_BUS").isEmpty();
    }
    static int run(const QStringList &arguments, const QByteArray &input = {}, QByteArray *output = nullptr)
    {
        QProcess tool;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), qEnvironmentVariable("HIKARI_REAL_AGY_BUS"));
        tool.setProcessEnvironment(environment);
        tool.setProcessChannelMode(QProcess::SeparateChannels);
        tool.start(QStringLiteral("secret-tool"), arguments);
        tool.write(input);
        tool.closeWriteChannel();
        if (!tool.waitForFinished(30'000))
            return -1;
        if (output)
            *output = tool.readAllStandardOutput();
        return tool.exitStatus() == QProcess::NormalExit ? tool.exitCode() : -1;
    }
    static QStringList item(const std::string &id, bool typed = true)
    {
        QStringList attributes{QStringLiteral("server"), QStringLiteral("HikariSub"), QStringLiteral("user"),
                               QStringLiteral("ai-provider/") + QString::fromStdString(id)};
        if (typed)
            attributes << QStringLiteral("type") << QStringLiteral("plaintext");
        return attributes;
    }
};

/// Every key is "key", except that HIKARI_REAL_CODEX_SESSION, if set, holds HikariSub's Codex sign-in, and
/// Antigravity's comes from RealKeychain when HIKARI_REAL_AGY_BUS is set.
class Keys final : public ai::KeyStore {
public:
    void read(const std::string &id, std::function<void(ai::KeyResult)> done) override
    {
        if (RealKeychain::reaches(id)) {
            QByteArray secret;
            if (RealKeychain::run(QStringList{QStringLiteral("lookup")} + RealKeychain::item(id), {}, &secret) != 0 ||
                secret.isEmpty())
                return done(ai::KeyError::NotFound);
            return done(ai::KeyResult(secret.toStdString()));
        }
        QFile file(qEnvironmentVariable("HIKARI_REAL_CODEX_SESSION"));
        if (id == backends::ai::kCodexSecret && file.open(QIODevice::ReadOnly))
            return done(ai::KeyResult(file.readAll().toStdString()));
        done(ai::KeyResult("key"));
    }
    void write(const std::string &id, const std::string &key, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        if (RealKeychain::reaches(id)) {
            const QStringList store{QStringLiteral("store"),
                                    QStringLiteral("--label=ai-provider/%1@HikariSub").arg(QString::fromStdString(id))};
            return done(RealKeychain::run(store + RealKeychain::item(id), QByteArray::fromStdString(key)) == 0
                            ? std::nullopt
                            : std::optional(ai::KeyError::Other));
        }
        if (const QString path = qEnvironmentVariable("HIKARI_REAL_CODEX_SESSION");
            id == backends::ai::kCodexSecret && !path.isEmpty()) {
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
                file.write(QByteArray::fromStdString(key)) < 0 || !file.commit())
                return done(ai::KeyError::Other);
        }
        done(std::nullopt);
    }
    void remove(const std::string &id, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        if (RealKeychain::reaches(id))
            return done(RealKeychain::run(QStringList{QStringLiteral("clear")} + RealKeychain::item(id, false)) == 0
                            ? std::nullopt
                            : std::optional(ai::KeyError::Other));
        done(std::nullopt);
    }
};

class Configs final : public ai::ConfigStore {
public:
    explicit Configs(ai::Config config) : m_config(std::move(config)) {}
    ai::Config load() override { return m_config; }
    bool save(const ai::Config &config) override
    {
        m_config = config;
        return true;
    }

private:
    ai::Config m_config;
};

}

class AgentPanelTest : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    std::optional<bool> firstStartEmptyBefore;
    std::unique_ptr<app::Application> application;
    QPointer<backends::ai::AiSidecarHost> realHost;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QString at(const QString &name) const
    {
        return root.filePath(QString::fromLatin1(QTest::currentTestFunction()) + QLatin1Char('/') + name);
    }
    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    app::AgentController &agent() const { return *application->agent(); }
    application::EditSession &session() const
    {
        return *application->files().session(*application->workspace().editingTarget());
    }
    QVariant shell(const QString &code)
    {
        QQmlExpression expression(qmlContext(window()), window(), code);
        const QVariant out = expression.evaluate();
        if (expression.hasError())
            qWarning() << expression.error();
        return out;
    }
    QQuickItem *item(const QString &name) const
    {
        std::function<QQuickItem *(QQuickItem *)> find = [&](QQuickItem *at) -> QQuickItem * {
            if (at->objectName() == name && at->isVisible())
                return at;
            for (QQuickItem *child : at->childItems())
                if (QQuickItem *found = find(child))
                    return found;
            return nullptr;
        };
        for (QWindow *top : QGuiApplication::topLevelWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(top); quick && quick->isVisible())
                if (QQuickItem *found = find(quick->contentItem()))
                    return found;
        for (QObject *popup : window()->findChildren<QObject *>()) {
            if (auto *popupItem = qobject_cast<QQuickItem *>(popup->property("popupItem").value<QObject *>()))
                if (QQuickItem *found = find(popupItem))
                    return found;
        }
        return nullptr;
    }
    bool click(const QString &name)
    {
        QQuickItem *button = item(name);
        return button && button->isEnabled() && QMetaObject::invokeMethod(button, "click");
    }
    QStringList kinds() const
    {
        QStringList out;
        auto *items = qobject_cast<QAbstractItemModel *>(agent().items());
        for (int row = 0; row < items->rowCount(); ++row)
            out << items->index(row, 0).data(app::AgentItemsModel::KindRole).toString();
        return out;
    }
    QVariantMap lastItem() const
    {
        auto *items = qobject_cast<QAbstractItemModel *>(agent().items());
        return items->index(items->rowCount() - 1, 0).data(app::AgentItemsModel::ItemRole).toMap();
    }
    QVariantMap lastOf(QLatin1String kind) const
    {
        auto *items = qobject_cast<QAbstractItemModel *>(agent().items());
        for (int row = items->rowCount() - 1; row >= 0; --row)
            if (items->index(row, 0).data(app::AgentItemsModel::KindRole).toString() == kind)
                return items->index(row, 0).data(app::AgentItemsModel::ItemRole).toMap();
        return {};
    }
    QVariantMap lastProposal() const { return lastOf(QLatin1String("proposal")); }
    QString applyScript(const char *say) const
    {
        const auto line = session().document().lines().front()->id;
        const Json params = *Json::parse(QStringLiteral(R"({"revision": %1, "name": "Tightened", "edits": [
            {"op": "set_line", "line": %2, "fields": {"text": "Shorter."}}]})")
                                             .arg(session().revision())
                                             .arg(line.value)
                                             .toStdString());
        return QString::fromStdString(
            Json::object({{"say", say},
                          {"calls", Json::array({Json::object({{"operation", "document.apply"}, {"params", params}})})}})
                .dump());
    }
    void type(const QString &text)
    {
        QQuickItem *composer = item(QStringLiteral("agentComposer"));
        QVERIFY(composer);
        composer->setProperty("text", text);
        composer->forceActiveFocus();
        QTest::keyClick(window(), Qt::Key_Return);
    }
    void openPanel()
    {
        QVERIFY(shell(QStringLiteral("root.showPanel(agentDock), agentDock.isOpen")).toBool());
        QTRY_VERIFY(item(QStringLiteral("agentView")));
    }
    void shoot(const QString &name, const QString &title = {})
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            return;
        QVERIFY(QDir().mkpath(out));
        if (!title.isEmpty() && !agent().current().isEmpty())
            agent().rename(agent().current(), title);
        const QSize size = window()->size();
        window()->resize(1440, 1000);
        shell(QStringLiteral("notices.visible = false"));
        auto restore = qScopeGuard([this, size] {
            window()->resize(size);
            ui::theme::endPreview();
            shell(QStringLiteral("notices.visible = true"));
        });
        for (const auto code : ui::theme::kCodes) {
            ui::theme::preview({{QStringLiteral("appearance.theme"), ui::theme::codeName(code)},
                                {QStringLiteral("appearance.followSystem"), false}});
            QQuickTest::qWaitForPolish(window());
            QTest::qWait(200);
            QVERIFY(window()->grabWindow().save(out + QLatin1Char('/') + name + QLatin1Char('-') +
                                                ui::theme::codeName(code) + QStringLiteral(".png")));
        }
    }

    auto keepTranscriptIn(const QString &variable)
    {
        return qScopeGuard([this, path = qEnvironmentVariable(variable.toLatin1().constData())] {
            if (QFile file(path); file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                file.write(agent().markdown(agent().current()).toUtf8());
            if (QFile file(path + QStringLiteral(".sidecar-stderr.txt"));
                realHost && qEnvironmentVariableIsSet("HIKARI_REAL_SIDECAR_STDERR") && file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                file.write(realHost->diagnostics());
        });
    }

    void realAgents(app::AiFeatures::Ports &ports)
    {
        const QString runtime = qEnvironmentVariable("HIKARI_REAL_AI_RUNTIME");
        const QString bundle = qEnvironmentVariable("HIKARI_REAL_AI_BUNDLE");
        if (runtime.isEmpty() || bundle.isEmpty())
            return;
        ports.sidecarHost = [this, runtime, bundle](const backends::ai::SidecarInstall &) {
            backends::ai::AiSidecarHost::Options o;
            o.program = runtime;
            o.arguments = {bundle, QStringLiteral("--vendor"), at(QStringLiteral("ai/vendor"))};
            o.workingDirectory = at(QStringLiteral("ai/work"));
            QDir().mkpath(at(QStringLiteral("ai/vendor")));
            o.sandbox = backends::sandbox::Profile{.name = QStringLiteral("HikariSub.AI.Sidecar"),
                                                   .readExecute = {QFileInfo(runtime).absolutePath(), QFileInfo(bundle).absolutePath()},
                                                   .readWrite = {o.workingDirectory, at(QStringLiteral("ai/vendor"))}};
            auto host = std::make_unique<backends::ai::AiSidecarHost>(std::move(o), &application->apiHost()->api(),
                                                                      api::Caller{.id = "ai", .label = "AI", .reviewed = true});
            realHost = host.get();
            return host;
        };
        ports.agents = [this](backends::ai::AgentBroker &broker, application::ai::KeyStore &keys) {
            broker.add("claude", backends::ai::claudeAgent(backends::ai::findAgentCli(QStringLiteral("claude")),
                                                         QDir::homePath(), at(QStringLiteral("ai/agents/claude"))));
            const QString home = at(QStringLiteral("ai/agents/codex/home"));
            if (auto codex = backends::ai::codexAgent(backends::ai::findAgentCli(QStringLiteral("codex")), home,
                                                      at(QStringLiteral("ai/agents/codex/work")),
                                                      std::make_shared<backends::ai::CodexLease>(keys, home),
                                                      QCoreApplication::applicationFilePath()))
                broker.add("codex", std::move(*codex));
        };
        ports.agentOnPath = [](const QString &) { return true; };
    }

    QString antigravityProfile() const { return at(QStringLiteral("user/ai/agents/antigravity")); }
    void realAntigravity(app::AiFeatures::Ports &ports)
    {
        const QString agy = qEnvironmentVariable("HIKARI_REAL_AGY");
        const QString bridge = qEnvironmentVariable("HIKARI_REAL_HIKARISUB");
        if (agy.isEmpty() || bridge.isEmpty())
            return;
        realAgents(ports);
        ports.agents = [](backends::ai::AgentBroker &, application::ai::KeyStore &) {};
        ports.agentOnPath = [](const QString &) { return false; };
        ports.acpCli = [agy](const QString &command) { return command == QLatin1String("agy") ? agy : QString(); };
        ports.acpSpec = [this, bridge](const app::AiFeatures::AcpAgent &agent,
                                       std::shared_ptr<backends::ai::TokenFileLease> lease) -> std::optional<backends::ai::AgentSpec> {
            if (agent.id != QLatin1String(app::AiFeatures::kAntigravity))
                return std::nullopt;
            return backends::ai::antigravityAgent(agent.program, antigravityProfile(), bridge, std::move(lease));
        };
        ports.antigravityTokenFile = antigravityProfile() + u'/' + QLatin1String(backends::ai::kAntigravityTokenFile);
    }

    static QRectF onScene(const QQuickItem *at) { return at->mapRectToScene(QRectF(0, 0, at->width(), at->height())); }
    static QStringList sticksOut(QQuickItem *from, const QRectF &bounds)
    {
        QStringList out;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *at) {
            if (!at->isVisible() || at->opacity() == 0)
                return;
            const QRectF r = onScene(at);
            if (r.width() > 0 && (r.left() < bounds.left() - 1 || r.right() > bounds.right() + 1))
                out << QStringLiteral("%1 \"%2\" spans %3..%4, room %5..%6")
                           .arg(QString::fromLatin1(at->metaObject()->className()), at->objectName())
                           .arg(r.left()).arg(r.right()).arg(bounds.left()).arg(bounds.right());
            for (QQuickItem *child : at->childItems())
                walk(child);
        };
        walk(from);
        return out;
    }
    QQuickItem *newest(const QString &name) const
    {
        QQuickItem *last = nullptr;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *at) {
            if (!at->isVisible())
                return;
            if (at->objectName() == name)
                last = at;
            for (QQuickItem *c : at->childItems())
                walk(c);
        };
        walk(window()->contentItem());
        return last;
    }
    QQuickItem *child(QQuickItem *in, const QString &name) const
    {
        for (QQuickItem *at : in->childItems()) {
            if (at->objectName() == name)
                return at;
            if (QQuickItem *found = child(at, name))
                return found;
        }
        return nullptr;
    }
    QObject *popup(const QString &name) const
    {
        for (QObject *at : window()->findChildren<QObject *>(name))
            if (at->inherits("QQuickPopup"))
                return at;
        return nullptr;
    }
    QRectF popupOnScene(const QString &name) const
    {
        QObject *at = popup(name);
        auto *background = qobject_cast<QQuickItem *>(at->property("background").value<QObject *>());
        QQuickItem *body = background ? background->parentItem()
                                      : qobject_cast<QQuickItem *>(at->property("contentItem").value<QObject *>())->parentItem();
        return onScene(body);
    }
    void narrowPanel(int width)
    {
        QVERIFY(shell(QStringLiteral("agentLoader.anchors.fill = undefined, agentLoader.width = %1,"
                                     " agentLoader.height = agentLoader.parent.height, true").arg(width)).toBool());
        QQuickTest::qWaitForPolish(window());
    }
    QList<qulonglong> openTabs(const QStringList &names)
    {
        for (const QString &name : names) {
            QFile file(at(name));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return {};
            file.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                       "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                       "Dialogue: 0,0:00:01.00,0:00:03.00,Default,Ellie,0,0,0,,Ellie, wait up! The trail splits here.\n");
            file.close();
            if (!application->openFile(at(name)))
                return {};
        }
        QList<qulonglong> ids;
        for (const QVariant &tab : agent().attachable())
            ids << tab.toMap().value(QStringLiteral("id")).toULongLong();
        return ids;
    }
    void keys(const QString &text)
    {
        for (const QChar c : text)
            QTest::keyClick(window(), c.toLatin1());
    }
    QString autoTurn(const Json &call) const
    {
        return QString::fromStdString(
            Json::object({{"say", "On it."}, {"calls", Json::array({call})}}).dump());
    }

private slots:
    void initTestCase() { testing::removeStaleTemporaryDirs(); }

    void init()
    {
        const QByteArray test(QTest::currentTestFunction());
        const bool chatModel = test != "withoutAChatModelThePanelPointsToSettings" && test != "withOnlyClaudeCodeThePanelRunsOnIt";
        QVERIFY(QDir(root.path()).mkpath(QString::fromLatin1(QTest::currentTestFunction())));
        ai::Config config;
        ai::Provider provider = ai::presetProvider(ai::Preset::OpenRouter, "or", "OpenRouter");
        provider.models = {{.id = "m", .name = "Fake model", .price = ai::Price{.input = 0, .output = 0}}};
        config.providers.push_back(provider);
        if (chatModel)
            config.roles[ai::Role::Chat] = {"or", "m"};

        const QString flags = at(QStringLiteral("local-bundle"));
        QFile bundle(flags);
        QVERIFY(bundle.open(QIODevice::WriteOnly | QIODevice::Truncate));
        bundle.write("--mode ok");
        bundle.close();

        app::AiFeatures::Ports ports;
        ports.keyStore = [] { return std::make_unique<Keys>(); };
        ports.configStore = [config] { return std::make_unique<Configs>(config); };
        ports.sidecarHost = [this](const backends::ai::SidecarInstall &install) {
            backends::ai::AiSidecarHost::Options o;
            o.program = QStringLiteral(HIKARI_FAKE_AI_SIDECAR);
            o.arguments = {install.bundle};
            o.workingDirectory = at(QStringLiteral("ai/work"));
            o.handshakeMs = 3'000;
            return std::make_unique<backends::ai::AiSidecarHost>(std::move(o), &application->apiHost()->api(),
                                                                 api::Caller{.id = "ai", .label = "AI", .reviewed = true});
        };
        ports.localSidecar = backends::ai::SidecarInstall{QStringLiteral("0.0.0-dev"), {}, flags, {}};
        ports.agents = [](backends::ai::AgentBroker &, application::ai::KeyStore &) {};
        const bool found = QTest::currentTestFunction() == QByteArrayLiteral("agentsAreModelsOnlyWhenTheyAreFound");
        const bool onlyClaude = QTest::currentTestFunction() == QByteArrayLiteral("withOnlyClaudeCodeThePanelRunsOnIt");
        ports.agentOnPath = [found, onlyClaude](const QString &agent) {
            return found || (onlyClaude && agent == QStringLiteral("claude"));
        };
        if (test == "withoutFileSandboxingThePanelOffersOnlyClaude") {
            ports.agentOnPath = [](const QString &) { return true; };
            ports.filesConfined = [] { return false; };
        }
        if (test == "theInstalledClaudeEditsIntoTheReview" || test == "theInstalledCodexEditsIntoTheReview")
            realAgents(ports);
        if (test == "theInstalledAntigravityRepliesAndItsSignInGoesOnSignOut")
            realAntigravity(ports);

        if (test == "theAgentAndReviewOpenInTheAiSideBar")
            firstStartEmptyBefore = ui::SideBarModel::setFirstStartEmpty(false);
        app::Application::Options options;
        options.settingsFile = at(QStringLiteral("settings.toml"));
        options.userFolder = at(QStringLiteral("user"));
        options.playbackAudio = false;
        options.aiPorts = std::move(ports);
        if (test == "anAgentsPluginInstallsOnlyWhenTheUserSaysSoInEveryMode" || test == "cardsFitANarrowPanel")
            options.plugins = {at(QStringLiteral("plugins")), at(QStringLiteral("plugin-data")), at(QStringLiteral("plugin-cache")),
                               at(QStringLiteral("plugin-registry"))};
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());

        const QString path = at(QStringLiteral("episode.ass"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                   "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                   "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,A rather long first line\n"
                   "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,second\n");
        file.close();
        QVERIFY(application->openFile(path));
    }
    void cleanup()
    {
        engine.reset();
        application.reset();
        if (firstStartEmptyBefore)
            ui::SideBarModel::setFirstStartEmpty(*std::exchange(firstStartEmptyBefore, std::nullopt));
    }

    void theAgentAndReviewOpenInTheAiSideBar()
    {
        QVERIFY(shell(QStringLiteral("root.inSideBar(agentDock) && root.inSideBar(reviewDock)")).toBool());
        QVERIFY(!shell(QStringLiteral("showAiAgentAction.trigger(), agentDock.isOpen")).toBool());
        QTRY_VERIFY(shell(QStringLiteral("agentPanel.visible && agentPanel.parent.objectName === 'sideBarSlot'"
                                         " && root.sideBarOnLeft(root.sideBarOf(agentDock))")).toBool());
        QTRY_VERIFY(item(QStringLiteral("agentView")));
        auto *aiSlot = shell(QStringLiteral("agentPanel.parent")).value<QObject *>();
        QVERIFY(aiSlot);
        QVERIFY(!shell(QStringLiteral("root.showPanel(reviewDock), reviewDock.isOpen")).toBool());
        QTRY_VERIFY(shell(QStringLiteral("reviewPanel.visible")).toBool());
        QCOMPARE(shell(QStringLiteral("reviewPanel.parent")).value<QObject *>(), aiSlot);
        QVERIFY(!shell(QStringLiteral("agentPanel.visible")).toBool());
    }

    void agentsAreModelsOnlyWhenTheyAreFound()
    {
        openPanel();
        const QVariantList models = agent().models();
        QCOMPARE(models.size(), 3);
        QCOMPARE(models[0].toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Claude Code"));
        QCOMPARE(models[0].toMap().value(QStringLiteral("value")).toString(), QStringLiteral("agent:claude\t"));
        QCOMPARE(models[1].toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Codex"));
        const QString codex = models[1].toMap().value(QStringLiteral("value")).toString();
        QCOMPARE(codex, QStringLiteral("agent:codex\t"));
        agent().newThread();
        agent().setModel(codex);
        QTRY_COMPARE(agent().thread().value(QStringLiteral("modelText")).toString(), QStringLiteral("Codex"));
        QCOMPARE(agent().thread().value(QStringLiteral("model")).toString(), codex);
    }
    void withoutFileSandboxingThePanelOffersOnlyClaude()
    {
        openPanel();
        QStringList offered;
        for (const QVariant &model : agent().models())
            if (model.toMap().value(QStringLiteral("value")).toString().startsWith(QLatin1String("agent:")))
                offered << model.toMap().value(QStringLiteral("text")).toString();
        QCOMPARE(offered, QStringList{QStringLiteral("Claude Code")});

        agent().newThread();
        agent().setModel(QStringLiteral("agent:codex\t"));
        agent().send(QStringLiteral("Fix the typo"));
        QTRY_COMPARE(lastItem().value(QStringLiteral("notice")).toString(), QStringLiteral("notOffered"));
        QTRY_VERIFY(item(QStringLiteral("agentNotice_notOffered")));
        QVERIFY(!agent().thread().value(QStringLiteral("running")).toBool());
        shoot(QStringLiteral("agent-not-offered"), QStringLiteral("Fix the typo"));
    }
    void theInstalledClaudeEditsIntoTheReview()
    {
        if (qEnvironmentVariable("HIKARI_REAL_AI_RUNTIME").isEmpty())
            QSKIP("Evidence only: set HIKARI_REAL_AI_RUNTIME and HIKARI_REAL_AI_BUNDLE to run the installed claude");
        application->settingsStore()->set(app::AgentController::kModeSetting, QStringLiteral("manual"));
        openPanel();
        agent().newThread();
        agent().setModel(QStringLiteral("agent:claude\t"));
        auto transcript = keepTranscriptIn(QStringLiteral("HIKARI_REAL_CLAUDE_OUT"));
        type(QStringLiteral("In this subtitle file, line 2 reads \"second\". Use HikariSub's tools to change it to "
                            "\"Second\". Change nothing else, then say done."));
        QTRY_VERIFY_WITH_TIMEOUT(item(QStringLiteral("agentProposalApply")), 240'000);
        shoot(QStringLiteral("agent-claude-proposal"));
        QTest::qWait(kPastCodexsDefaultToolTimeoutMs);
        QVERIFY(click(QStringLiteral("agentProposalApply")));
        QTRY_VERIFY(kinds().contains(QStringLiteral("change")));
        QTRY_VERIFY_WITH_TIMEOUT(!agent().thread().value(QStringLiteral("running")).toBool(), 120'000);
        QCOMPARE(session().document().lines()[1]->text, u8"Second");
        QCOMPARE(session().document().lines()[0]->text, u8"A rather long first line");
        shoot(QStringLiteral("agent-claude"));
    }
    void theInstalledCodexEditsIntoTheReview()
    {
        if (qEnvironmentVariable("HIKARI_REAL_AI_RUNTIME").isEmpty() || qEnvironmentVariable("HIKARI_REAL_CODEX_SESSION").isEmpty())
            QSKIP("Evidence only: set HIKARI_REAL_AI_RUNTIME, HIKARI_REAL_AI_BUNDLE and HIKARI_REAL_CODEX_SESSION");
        application->settingsStore()->set(app::AgentController::kModeSetting, QStringLiteral("manual"));
        openPanel();
        agent().newThread();
        agent().setModel(QStringLiteral("agent:codex\t"));
        auto transcript = keepTranscriptIn(QStringLiteral("HIKARI_REAL_CODEX_OUT"));
        type(QStringLiteral("In this subtitle file, line 2 reads \"second\". Use HikariSub's tools to change it to "
                            "\"Second\". Change nothing else, then say done."));
        QTRY_VERIFY_WITH_TIMEOUT(item(QStringLiteral("agentProposalApply")), 240'000);
        shoot(QStringLiteral("agent-codex-proposal"));
        QTest::qWait(kPastCodexsDefaultToolTimeoutMs);
        QVERIFY(click(QStringLiteral("agentProposalApply")));
        QTRY_VERIFY(kinds().contains(QStringLiteral("change")));
        QTRY_VERIFY_WITH_TIMEOUT(!agent().thread().value(QStringLiteral("running")).toBool(), 120'000);
        QCOMPARE(session().document().lines()[1]->text, u8"Second");
        QCOMPARE(session().document().lines()[0]->text, u8"A rather long first line");
        shoot(QStringLiteral("agent-codex"));
    }
    void theInstalledAntigravityRepliesAndItsSignInGoesOnSignOut()
    {
        for (const char *variable : {"HIKARI_REAL_AI_RUNTIME", "HIKARI_REAL_AGY", "HIKARI_REAL_AGY_BUS", "HIKARI_REAL_HIKARISUB"})
            if (qEnvironmentVariable(variable).isEmpty())
                QSKIP("Evidence only: set HIKARI_REAL_AI_RUNTIME, HIKARI_REAL_AI_BUNDLE, HIKARI_REAL_AGY, HIKARI_REAL_AGY_BUS and "
                      "HIKARI_REAL_HIKARISUB to run the installed agy on HikariSub's own Google sign-in");
        const QStringList lookup = QStringList{QStringLiteral("lookup")} + RealKeychain::item(backends::ai::kAntigravitySecret);
        QCOMPARE(RealKeychain::run(lookup), 0);
        openPanel();
        agent().newThread();
        agent().setModel(QStringLiteral("agent:acp:antigravity\t"));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("modelText")).toString(), QStringLiteral("Antigravity"));
        QElapsedTimer took;
        took.start();
        type(QStringLiteral("Reply with the word ready"));
        QTRY_VERIFY_WITH_TIMEOUT(kinds().contains(QStringLiteral("text")) && !agent().thread().value(QStringLiteral("running")).toBool(),
                                 900'000);
        const QString reply = lastItem().value(QStringLiteral("text")).toString();
        qInfo().noquote() << "agy replied in" << took.elapsed() << "ms:" << reply.left(200);
        if (QFile out(qEnvironmentVariable("HIKARI_REAL_AGY_OUT")); out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(reply.toUtf8());
        QVERIFY2(reply.contains(QStringLiteral("ready"), Qt::CaseInsensitive), qPrintable(reply.left(200)));

        QObject *acp = application->aiSettings()->property("acp").value<QObject *>();
        QVERIFY(acp);
        QVERIFY(QMetaObject::invokeMethod(acp, "signOut"));
        QTRY_COMPARE(RealKeychain::run(lookup), 1);
        const QString tokenFile = antigravityProfile() + u'/' + QLatin1String(backends::ai::kAntigravityTokenFile);
        engine.reset();
        application.reset();
        QTRY_VERIFY(!QFileInfo::exists(tokenFile));
        QCOMPARE(RealKeychain::run(lookup), 1);
    }
    void agentsAreNoModelsWhenTheyAreMissing()
    {
        openPanel();
        QCOMPARE(agent().models().size(), 1);
    }
    void aTurnStreamsIntoTheTimelineAndItsChangeWaitsInTheReview()
    {
        application->settingsStore()->set(app::AgentController::kModeSetting, QStringLiteral("manual"));
        QVERIFY(shell(QStringLiteral("newAiThreadAction.trigger(), agentDock.isOpen")).toBool());
        QTRY_VERIFY(item(QStringLiteral("agentView")));
        QVERIFY(agent().ready());
        QCOMPARE(agent().thread().value(QStringLiteral("mode")).toString(), QStringLiteral("manual"));
        QCOMPARE(item(QStringLiteral("agentActiveTab"))->property("name").toString(), QStringLiteral("episode.ass"));
        QCOMPARE(item(QStringLiteral("agentActiveTab"))->property("note").toString(), QStringLiteral("active tab"));

        type(applyScript("I tightened the first line."));
        QTRY_COMPARE(kinds().last(), QStringLiteral("proposal"));
        QTRY_VERIFY(item(QStringLiteral("agentProposalApply")));
        QCOMPARE(session().document().lines().front()->text, u8"A rather long first line");
        QVERIFY(item(QStringLiteral("agentComposer"))->hasActiveFocus());
        QVERIFY(!item(QStringLiteral("agentProposalApply"))->hasActiveFocus());
        shoot(QStringLiteral("agent-proposal"), QStringLiteral("Tighten the long first line"));
        QVERIFY(click(QStringLiteral("agentProposalApply")));
        QTRY_COMPARE(kinds().last(), QStringLiteral("change"));
        QCOMPARE(kinds(), (QStringList{"user", "text", "proposal", "change"}));
        QVERIFY(!item(QStringLiteral("agentProposal")));
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());
        QCOMPARE(lastItem().value(QStringLiteral("name")).toString(), QStringLiteral("Tightened"));
        QCOMPARE(session().document().lines().front()->text, u8"Shorter.");
        const QAbstractItemModel *lines = application->shell().lines();
        QCOMPARE(lines->index(0, 0).data(ui::LineTableModel::ReviewRole).toInt(),
                 int(ui::LineTableModel::ReviewState::Waiting));
        QCOMPARE(item(QStringLiteral("agentComposer"))->property("text").toString(), QString());
        QCOMPARE(agent().threads().size(), 1);
        QCOMPARE(agent().threads().front().toMap().value(QStringLiteral("files")).toString(), QStringLiteral("episode.ass"));
        shoot(QStringLiteral("agent-turn"), QStringLiteral("Tighten the long first line"));

        QVERIFY(click(QStringLiteral("agentThreadsButton")));
        QTRY_VERIFY(item(QStringLiteral("agentThreadRow")));
        shoot(QStringLiteral("agent-threads"), QStringLiteral("Tighten the long first line"));
    }

    void aProjectListsItsThreadsFirstAndShowsWhenItsAgentsFileIsOn()
    {
        QStringList opened;
        const auto previous = backends::desktop::replaceRoutes({.openUrl = [&](const QUrl &url) {
            opened << url.toLocalFile();
            return true;
        }});
        auto restore = qScopeGuard([&] { backends::desktop::replaceRoutes(previous); });
        const QString show = at(QStringLiteral("Show S02"));
        QVERIFY(QDir().mkpath(show));
        QFile agents(show + QStringLiteral("/AGENTS.md"));
        QVERIFY(agents.open(QIODevice::WriteOnly));
        agents.write("Keep honorifics.");
        agents.close();

        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        const QString outside = agent().thread().value(QStringLiteral("id")).toString();
        QVERIFY(!agent().thread().value(QStringLiteral("projectInstructions")).toBool());
        QVERIFY(!item(QStringLiteral("agentProjectInstructions")));

        QVERIFY(application->openFolder(show, 0, int(application::WindowRequest::Reuse)).value(QStringLiteral("ok")).toBool());
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().value(QStringLiteral("id")).toString() != outside);
        const QString inProject = agent().thread().value(QStringLiteral("id")).toString();
        QVERIFY(agent().thread().value(QStringLiteral("projectInstructions")).toBool());
        QTRY_VERIFY(item(QStringLiteral("agentProjectInstructions")));
        QVERIFY(click(QStringLiteral("agentOpenProjectInstructions")));
        QCOMPARE(opened, QStringList{QFileInfo(agents).absoluteFilePath()});

        agent().open(outside);
        QTest::qWait(5);
        agent().rename(outside, QStringLiteral("Newest, outside the Project"));
        const auto ids = [&] {
            QStringList out;
            for (const QVariant &t : agent().threads())
                out << t.toMap().value(QStringLiteral("id")).toString() + QLatin1Char(':')
                           + t.toMap().value(QStringLiteral("day")).toString();
            return out;
        };
        QCOMPARE(ids(), (QStringList{inProject + QStringLiteral(":project"), outside + QStringLiteral(":today")}));
        QTRY_VERIFY(!item(QStringLiteral("agentProjectInstructions")));
        QVERIFY(click(QStringLiteral("agentThreadsButton")));
        QTRY_VERIFY(item(QStringLiteral("agentThreadRow")));
        shoot(QStringLiteral("agent-project-threads"), QStringLiteral("Show S02"));
    }

    void aProposalTakesTheFocusFromThePanelButNotFromTheComposer()
    {
        application->settingsStore()->set(app::AgentController::kModeSetting, QStringLiteral("manual"));
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        QQuickItem *mode = item(QStringLiteral("agentModeButton"));
        QVERIFY(mode);
        mode->forceActiveFocus();
        QTRY_VERIFY(mode->hasActiveFocus());
        agent().send(applyScript("Tightened."));
        QTRY_VERIFY(item(QStringLiteral("agentProposalApply")));
        QTRY_VERIFY(item(QStringLiteral("agentProposalApply"))->hasActiveFocus());
        QCOMPARE(session().document().lines().front()->text, u8"A rather long first line");
        QTest::keyClick(window(), Qt::Key_Return);
        QTRY_COMPARE(kinds().last(), QStringLiteral("change"));
        QCOMPARE(session().document().lines().front()->text, u8"Shorter.");
    }

    void aDeclinedProposalWritesNothing()
    {
        application->settingsStore()->set(app::AgentController::kModeSetting, QStringLiteral("manual"));
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        type(applyScript("Tightened."));
        QTRY_VERIFY(item(QStringLiteral("agentProposalTell")));
        QVERIFY(click(QStringLiteral("agentProposalTell")));
        QTRY_VERIFY(item(QStringLiteral("agentProposalReply")));
        item(QStringLiteral("agentProposalReply"))->setProperty("text", QStringLiteral("Keep it long"));
        QTest::keyClick(window(), Qt::Key_Return);
        QTRY_COMPARE(lastProposal().value(QStringLiteral("status")).toString(), QStringLiteral("declined"));
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());
        QCOMPARE(session().document().lines().front()->text, u8"A rather long first line");
        QVERIFY(!kinds().contains(QStringLiteral("change")));
    }

    void aThreadIsRenamedInItsOwnWindow()
    {
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        QVERIFY(click(QStringLiteral("agentThreadMenuButton")));
        QTRY_VERIFY(item(QStringLiteral("agentRenameThread")));
        QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("agentRenameThread")), "triggered"));
        QTRY_VERIFY(item(QStringLiteral("agentRenameField")));
        item(QStringLiteral("agentRenameField"))->setProperty("text", QStringLiteral("Honorifics pass"));
        QVERIFY(click(QStringLiteral("agentRenameOk")));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("title")).toString(), QStringLiteral("Honorifics pass"));
        QTRY_VERIFY(!item(QStringLiteral("agentRenameField")));
    }

    void fullAccessWarnsOnceAndTheAlwaysAskListStillAsks()
    {
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QVERIFY(click(QStringLiteral("agentModeButton")));
        QTRY_VERIFY(item(QStringLiteral("agentMode_full")));
        QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("agentMode_full")), "triggered"));
        QTRY_VERIFY(item(QStringLiteral("messageBoxDontAsk")));
        QCOMPARE(agent().thread().value(QStringLiteral("mode")).toString(), QStringLiteral("auto"));
        item(QStringLiteral("messageBoxDontAsk"))->setProperty("checked", true);
        QVERIFY(click(QStringLiteral("messageBoxChoice_full")));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("mode")).toString(), QStringLiteral("full"));
        QVERIFY(agent().fullAccessWarned());
        QVERIFY(item(QStringLiteral("agentFullAccessBadge")));

        const QString script = QString::fromStdString(
            Json::object({{"say", "Raising the daily cap."},
                          {"calls", Json::array({Json::object(
                                        {{"operation", "settings.set"},
                                         {"params", Json::object({{"setting", "ai.agent.dailyCapCents"},
                                                                  {"value", std::int64_t{5000}}})}})})}})
                .dump());
        type(script);
        QTRY_COMPARE(kinds().last(), QStringLiteral("ask"));
        QCOMPARE(lastItem().value(QStringLiteral("type")).toString(), QStringLiteral("always"));
        QTRY_VERIFY(item(QStringLiteral("agentAllowOnce")));
        QVERIFY(!item(QStringLiteral("agentAllowInThread")));
        QCOMPARE(application->settingsStore()->integer(app::AgentController::kDailyCapSetting), 1000);
        shoot(QStringLiteral("agent-always-ask"), QStringLiteral("Raise my daily cap"));

        QVERIFY(click(QStringLiteral("agentAllowOnce")));
        QTRY_COMPARE(application->settingsStore()->integer(app::AgentController::kDailyCapSetting), 5000);
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());

        QVERIFY(click(QStringLiteral("agentNewThread")));
        QVERIFY(click(QStringLiteral("agentModeButton")));
        QTRY_VERIFY(item(QStringLiteral("agentMode_full")));
        QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("agentMode_full")), "triggered"));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("mode")).toString(), QStringLiteral("full"));
        QVERIFY(!item(QStringLiteral("messageBoxDontAsk")));
    }

    void anApprovedLuaScriptRunsAsOneMacroUndoStepAndFullAccessStillAsks()
    {
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        agent().setFullAccessWarned();
        agent().setMode(QStringLiteral("full"));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("mode")).toString(), QStringLiteral("full"));
        std::string lua = "script_name = \"Shout\"\n";
        for (int i = 0; i < 10; ++i)
            lua += "-- padding " + std::to_string(i) + "\n";
        lua += "aegisub.register_macro(\"Shout\", \"Upper-cases every Line\", function(subs)\n"
               "    for i = 1, #subs do\n"
               "        local line = subs[i]\n"
               "        if line.class == \"dialogue\" then line.text = line.text:upper(); subs[i] = line end\n"
               "    end\n"
               "end)\n";
        const Json run = Json::object({{"operation", "automation.run"},
                                       {"params", Json::object({{"name", "Shout"}, {"script", lua}})}});
        const QString script = QString::fromStdString(
            Json::object({{"say", "I wrote a macro."}, {"calls", Json::array({run, run})}}).dump());
        const std::size_t steps = session().historySize();
        type(script);
        QTRY_COMPARE(kinds().last(), QStringLiteral("script"));
        QTRY_VERIFY(item(QStringLiteral("agentScriptRun")));
        QVERIFY(item(QStringLiteral("agentScriptShowAll")));
        QVERIFY(!item(QStringLiteral("agentAllowInThread")));
        QCOMPARE(session().document().lines()[0]->text, u8"A rather long first line");
        QVERIFY(!QFileInfo::exists(at(QStringLiteral("user/ai/scripts/Shout.lua"))));
        shoot(QStringLiteral("agent-lua-script"), QStringLiteral("Shout every Line"));

        QVERIFY(click(QStringLiteral("agentScriptShowAll")));
        QTRY_VERIFY(!item(QStringLiteral("agentScriptShowAll")));
        QVERIFY(click(QStringLiteral("agentScriptRun")));
        QTRY_COMPARE_WITH_TIMEOUT(session().document().lines()[0]->text, u8"A RATHER LONG FIRST LINE", 20000);
        QCOMPARE(session().historySize(), steps + 1);
        QCOMPARE(session().history().back().name, std::string("Automation: Shout"));
        QVERIFY(QFileInfo::exists(at(QStringLiteral("user/ai/scripts/Shout.lua"))));

        QTRY_VERIFY_WITH_TIMEOUT(item(QStringLiteral("agentScriptRun")), 10000);
        QVERIFY(click(QStringLiteral("agentScriptDecline")));
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());
        QCOMPARE(session().historySize(), steps + 1);
        QVERIFY(session().undo());
        QCOMPARE(session().document().lines()[0]->text, u8"A rather long first line");
        const auto aiScriptLoaded = [this] {
            return std::ranges::any_of(application->automation().manager().scripts(), [](const auto &loaded) {
                return QString::fromStdString(loaded.path).contains(QStringLiteral("ai/scripts"));
            });
        };
        QTRY_VERIFY(!aiScriptLoaded());
        QVERIFY(application->automation().autoloadScripts().filter(QStringLiteral("Shout")).isEmpty());
    }

    void anAgentsPluginInstallsOnlyWhenTheUserSaysSoInEveryMode()
    {
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        agent().setFullAccessWarned();
        agent().setMode(QStringLiteral("full"));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("mode")).toString(), QStringLiteral("full"));
        const auto turn = [](const std::string &plugin, const std::string &name, const std::string &tier) {
            const Json scaffold = Json::object(
                {{"operation", "plugins.scaffold"},
                 {"params", Json::object({{"plugin", plugin}, {"name", name}, {"tier", tier},
                                          {"commands", Json::array({Json::object({{"id", "count"}, {"title", "Count lines"}})})}})}});
            const Json install = Json::object({{"operation", "plugins.install"}, {"params", Json::object({{"plugin", plugin}})}});
            return QString::fromStdString(
                Json::object({{"say", "I made a plugin."}, {"calls", Json::array({scaffold, install})}}).dump());
        };
        const auto installed = [this](const QString &id) {
            return std::ranges::any_of(application->pluginManager()->plugins(),
                                       [&](const QVariant &row) { return row.toMap().value(QStringLiteral("id")) == id; });
        };

        type(turn("com.example.line-counter", "Line counter", "restricted"));
        QTRY_VERIFY(kinds().contains(QStringLiteral("install")));
        QTRY_VERIFY(item(QStringLiteral("agentInstallAccept")));
        QVERIFY(!item(QStringLiteral("agentInstallRisky")));
        QVERIFY(!item(QStringLiteral("agentAllowInThread")));
        QVERIFY(QFileInfo::exists(at(QStringLiteral("user/ai/plugins/com.example.line-counter/main.lua"))));
        QVERIFY(!installed(QStringLiteral("com.example.line-counter")));
        shoot(QStringLiteral("agent-plugin-install"), QStringLiteral("Count the lines"));

        QVERIFY(click(QStringLiteral("agentInstallAccept")));
        QTRY_COMPARE(lastOf(QLatin1String("install")).value(QStringLiteral("status")).toString(), QStringLiteral("installed"));
        QVERIFY(installed(QStringLiteral("com.example.line-counter")));
        QTRY_VERIFY(item(QStringLiteral("agentInstallOpen")));
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());

        type(turn("com.example.full-counter", "Full counter", "unrestricted"));
        QTRY_VERIFY(item(QStringLiteral("agentInstallRisky")));
        QVERIFY(!item(QStringLiteral("agentInstallAccept")));
        shoot(QStringLiteral("agent-plugin-install-full"), QStringLiteral("Count with full access"));
        QVERIFY(click(QStringLiteral("agentInstallDecline")));
        QTRY_COMPARE(lastOf(QLatin1String("install")).value(QStringLiteral("status")).toString(), QStringLiteral("declined"));
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());
        QVERIFY(!installed(QStringLiteral("com.example.full-counter")));
    }

    void aThreadAtItsCapStopsAndRaiseContinues()
    {
        openPanel();
        type(QString::fromStdString(
            Json::object({{"say", "Working."}, {"hang", true}, {"usage", Json::object({{"reportedCost", 2.5}})}}).dump()));
        QTRY_COMPARE(kinds().last(), QStringLiteral("notice"));
        QCOMPARE(lastItem().value(QStringLiteral("notice")).toString(), QStringLiteral("cap"));
        QTRY_VERIFY(item(QStringLiteral("agentRaiseCap")));
        QVERIFY(!agent().thread().value(QStringLiteral("running")).toBool());
        shoot(QStringLiteral("agent-cap"), QStringLiteral("Retime episode 3"));

        QVERIFY(click(QStringLiteral("agentRaiseCap")));
        QVERIFY(agent().thread().value(QStringLiteral("cap")).toDouble() > 2.5);
        QTRY_COMPARE(kinds().last(), QStringLiteral("text"));
        QCOMPARE(lastItem().value(QStringLiteral("text")).toString(), QStringLiteral("ok: Continue."));
    }

    void aSubAgentShowsAsACardOpensReadOnlyAndStopsWithStopAll()
    {
        openPanel();
        const Json child = Json::object({{"say", "Working on part 1."}, {"hang", true}});
        const Json scope = Json::object({{"document", static_cast<std::int64_t>(application->workspace().editingTarget()->value)},
                                         {"lines", Json::array({Json(static_cast<std::int64_t>(
                                                       session().document().lines().front()->id.value))})}});
        const Json request = Json::object(
            {{"method", "_hikari/task.request"},
             {"params", Json::object({{"task", child.dump()}, {"title", "Part 1"}, {"scope", scope}})}});
        type(QString::fromStdString(
            Json::object({{"say", "Delegated."}, {"calls", Json::array({request})}}).dump()));
        QTRY_VERIFY(kinds().contains(QStringLiteral("tasks")));
        QTRY_VERIFY(item(QStringLiteral("agentTask_t1")));
        QCOMPARE(agent().threads().size(), 1);
        QQuickItem *row = item(QStringLiteral("agentTask_t1"));
        QAccessibleInterface *named = QAccessible::queryAccessibleInterface(row);
        QVERIFY(named);
        QCOMPARE(named->text(QAccessible::Name), QStringLiteral("Sub-agent Part 1, running"));
        const QString parent = agent().current();
        shoot(QStringLiteral("agent-subagents"), QStringLiteral("Translate in parts"));

        row->forceActiveFocus();
        QTest::keyClick(window(), Qt::Key_Return);
        QTRY_VERIFY(agent().current() != parent);
        QTRY_VERIFY(item(QStringLiteral("agentChildBanner")));
        QVERIFY(!item(QStringLiteral("agentComposer")));
        QVERIFY(!item(QStringLiteral("agentThreadsButton")));
        QCOMPARE(item(QStringLiteral("agentChildMode"))->property("text").toString(), QStringLiteral("Auto · capped by the parent"));
        QVERIFY(item(QStringLiteral("agentChildScope"))->property("text").toString().startsWith(QStringLiteral("scope: episode.ass · 1 line")));
        QCOMPARE(kinds().front(), QStringLiteral("user"));
        QVERIFY(item(QStringLiteral("agentStopChild")));
        shoot(QStringLiteral("agent-subagent-open"), QStringLiteral("Part 1"));

        item(QStringLiteral("agentView"))->forceActiveFocus();
        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_COMPARE(agent().current(), parent);
        QTRY_VERIFY(click(QStringLiteral("agentStopChildren")));
        QTRY_VERIFY(!item(QStringLiteral("agentStopChildren")));
        QTRY_VERIFY(item(QStringLiteral("agentTask_t1")));
        QVERIFY(!item(QStringLiteral("agentStopTask")));
    }

    void aMessageSentWhileATurnRunsIsQueuedAndCanSteer()
    {
        openPanel();
        type(QStringLiteral(R"({"say": "Thinking.", "hang": true})"));
        QTRY_VERIFY(agent().thread().value(QStringLiteral("running")).toBool());
        QVERIFY(item(QStringLiteral("agentStop")));
        type(QStringLiteral("Only the first episode."));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("queued")).toString(), QStringLiteral("Only the first episode."));
        QTRY_VERIFY(item(QStringLiteral("agentQueued")));
        shoot(QStringLiteral("agent-queued"), QStringLiteral("Check the honorifics"));

        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());
        QVERIFY(kinds().contains(QStringLiteral("notice")));
    }

    void compactionShowsTheMeterTheDividerTheOfferAndAFailure()
    {
        openPanel();
        const std::string summary = "## Goal\n- QC Ep01–Ep12\n## Open work\n- Ep06 Lines 201–388";
        const Json compacted = Json::object({{"sessionUpdate", "compacted"}, {"stage", 2}, {"before", 313000},
            {"after", 21200}, {"stubs", Json::array()}, {"summary", summary},
            {"message", Json::object({{"author", "assistant"},
                                      {"parts", Json::array({Json::object({{"type", "text"}, {"text", summary}})})}})},
            {"headEnd", 1}, {"keptFrom", 1}});
        const Json failed = Json::object({{"error", Json::object({{"code", -32010}, {"message", "timeout"},
            {"data", Json::object({{"kind", "timeout"}, {"httpStatus", 0}, {"detail", "provider timeout"}})}})}});
        type(QString::fromStdString(Json::object({{"say", "Working."}, {"hang", true},
            {"usage", Json::object({{"inputTokens", 117000}, {"outputTokens", 1000}})},
            {"compact", Json::array({Json::object({{"updates", Json::array({compacted})},
                                                   {"result", Json::object({{"compacted", true}, {"summary", summary}})}}),
                                     failed})}}).dump()));
        QTRY_VERIFY(agent().thread().value(QStringLiteral("running")).toBool());
        QTRY_VERIFY(item(QStringLiteral("agentTokens")));
        QTRY_VERIFY(item(QStringLiteral("agentTokens"))->property("text").toString()
                        .endsWith(QStringLiteral(" · compacts at the next tool call")));
        shoot(QStringLiteral("agent-meter-hot"), QStringLiteral("QC Ep01–Ep12"));

        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());
        type(QStringLiteral("/compact keep the glossary decisions"));
        QTRY_COMPARE(kinds().last(), QStringLiteral("compaction"));
        QCOMPARE(agent().thread().value(QStringLiteral("contextTokens")).toLongLong(), 21200);
        QTRY_VERIFY(item(QStringLiteral("agentCompactionToggle")));
        QVERIFY(!item(QStringLiteral("agentCompactionSummary")));
        QVERIFY(click(QStringLiteral("agentCompactionToggle")));
        QTRY_VERIFY(item(QStringLiteral("agentCompactionSummary")));
        shoot(QStringLiteral("agent-compacted"), QStringLiteral("QC Ep01–Ep12"));

        application->settingsStore()->set(app::AgentController::kAutoCompactSetting, false);
        QVERIFY(!item(QStringLiteral("agentCompactOffer")));
        type(QString::fromStdString(Json::object({{"say", "Ep08 done."},
            {"usage", Json::object({{"inputTokens", 125000}, {"outputTokens", 0}})}}).dump()));
        QTRY_VERIFY(item(QStringLiteral("agentCompactOffer")));
        shoot(QStringLiteral("agent-compact-offer"), QStringLiteral("QC Ep01–Ep12"));
        QVERIFY(click(QStringLiteral("agentOfferCompact")));
        QTRY_COMPARE(lastItem().value(QStringLiteral("notice")).toString(), QStringLiteral("compactFailed"));
        QTRY_VERIFY(item(QStringLiteral("agentRetryCompact")));
        QVERIFY(item(QStringLiteral("agentSummaryThread")));
        shoot(QStringLiteral("agent-compact-failed"), QStringLiteral("QC Ep01–Ep12"));
    }

    void theGlobeSearchesPerThreadAndAnAnswerShowsItsSourcesAndSearches()
    {
        QStringList opened;
        const auto previous = backends::desktop::replaceRoutes({.openUrl = [&](const QUrl &url) {
            opened << url.toString();
            return true;
        }});
        auto restore = qScopeGuard([&] { backends::desktop::replaceRoutes(previous); });
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QQuickItem *globe = item(QStringLiteral("agentWebSearch"));
        QVERIFY(globe);
        QVERIFY(globe->property("checked").toBool());
        QCOMPARE(globe->property("tip").toString(), QStringLiteral("Search the web: on for this thread"));
        QVERIFY(click(QStringLiteral("agentWebSearch")));
        QVERIFY(!agent().thread().value(QStringLiteral("webSearch")).toBool());
        QTRY_COMPARE(globe->property("tip").toString(), QStringLiteral("Search the web: off for this thread"));
        QVERIFY(click(QStringLiteral("agentWebSearch")));
        QVERIFY(agent().thread().value(QStringLiteral("webSearch")).toBool());

        const std::string say = "Aimer sings it. The single came out in 2011.";
        Json citations = Json::array();
        for (int i = 1; i <= 6; ++i) {
            Json c = Json::object({{"index", i}, {"url", "https://site" + std::to_string(i) + ".example/page"},
                                   {"title", "Page " + std::to_string(i)}});
            if (i == 1) {
                c["quote"] = "Aimer is the singer.";
                c["start"] = 0;
                c["end"] = 15;
            }
            citations.push(std::move(c));
        }
        const Json updates = Json::array(
            {Json::object({{"sessionUpdate", "sources"}, {"textLength", static_cast<std::int64_t>(say.size())},
                           {"citations", citations},
                           {"searches", Json::array({Json::object({{"kind", "search"}, {"query", "aimer single"},
                                                                   {"urls", Json::array({"https://site1.example/page"})}}),
                                                     Json::object({{"kind", "read"}, {"url", "https://www.site2.example/page"}})})}}),
             Json::object({{"sessionUpdate", "search_failed"}, {"service", "brave"}, {"reason", "usedUp"}, {"httpStatus", 402}})});
        type(QString::fromStdString(Json::object({{"say", say}, {"calls", Json::array()}, {"updates", updates}}).dump()));
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());
        QCOMPARE(kinds(), (QStringList{"user", "search", "text", "notice"}));

        QTRY_VERIFY(item(QStringLiteral("agentSources")));
        QQuickItem *answer = item(QStringLiteral("agentAnswer"));
        QVERIFY(answer);
        QVERIFY(answer->property("text").toString().contains(QLatin1String("href=\"cite:1\"")));
        QVERIFY(item(QStringLiteral("agentSource_5")));
        QVERIFY(!item(QStringLiteral("agentSource_6")));
        QCOMPARE(item(QStringLiteral("agentMoreSources"))->property("text").toString(), QStringLiteral("Show 1 more"));
        QVERIFY(click(QStringLiteral("agentMoreSources")));
        QTRY_VERIFY(item(QStringLiteral("agentSource_6")));
        QVERIFY(click(QStringLiteral("agentSource_2")));
        QCOMPARE(opened, (QStringList{"https://site2.example/page"}));

        QStringList rows;
        std::function<void(QQuickItem *)> collect = [&](QQuickItem *at) {
            if (at->objectName() == QLatin1String("agentSearchRow") && at->isVisible())
                rows << at->property("text").toString();
            for (QQuickItem *child : at->childItems())
                collect(child);
        };
        collect(window()->contentItem());
        QCOMPARE(rows, (QStringList{"Searched the web · aimer single", "Read site2.example"}));
        QVERIFY(item(QStringLiteral("agentNotice_searchUsedUp")));
        QCOMPARE(lastItem().value(QStringLiteral("service")).toString(), QStringLiteral("brave"));
        QVERIFY(!agent().openLink(QStringLiteral("file:///etc/passwd")));
        QVERIFY(!agent().openLink(QStringLiteral("javascript:alert(1)")));
        QCOMPARE(opened.size(), 1);
        shoot(QStringLiteral("agent-web-search"), QStringLiteral("Who sings it?"));

        application->settingsStore()->set(app::AiFeatures::kWebSearchSetting, false);
        QTRY_VERIFY(!item(QStringLiteral("agentWebSearch")));
    }

    void withoutAChatModelThePanelPointsToSettings()
    {
        openPanel();
        QVERIFY(!agent().ready());
        QVERIFY(item(QStringLiteral("agentNoProvider")));
        QVERIFY(!item(QStringLiteral("agentTimeline")));
        QVERIFY(!item(QStringLiteral("agentNewThread")));
        QVERIFY(!item(QStringLiteral("agentThreadsButton")));
        shoot(QStringLiteral("agent-no-provider"));
        QVERIFY(click(QStringLiteral("agentOpenSettings")));
        QTRY_VERIFY(shell(QStringLiteral("settingsLoader.item !== null && settingsLoader.item.visible")).toBool());
    }

    void withOnlyClaudeCodeThePanelRunsOnIt()
    {
        openPanel();
        QVERIFY(agent().ready());
        QVERIFY(!item(QStringLiteral("agentNoProvider")));
        agent().newThread();
        QTRY_COMPARE(agent().thread().value(QStringLiteral("model")).toString(), QStringLiteral("agent:claude\t"));
        QCOMPARE(agent().thread().value(QStringLiteral("modelText")).toString(), QStringLiteral("Claude Code"));
    }

    void withAiOffThePanelAndItsCommandsAreGone()
    {
        openPanel();
        QVERIFY(shell(QStringLiteral("root.builtInDocks.indexOf(agentDock) >= 0")).toBool());
        QVERIFY(application::aiCommand(application::hotkeyIdOf("GLOBAL_SHOW_AI_AGENT")));
        QVERIFY(application::aiCommand(application::hotkeyIdOf("GLOBAL_NEW_AI_THREAD")));

        application->settingsStore()->set(app::AiFeatures::kOffSetting, true);
        QVERIFY(!agent().on());
        QVERIFY(agent().engine() == nullptr);
        QTRY_VERIFY(!shell(QStringLiteral("agentDock.isOpen")).toBool());
        QVERIFY(shell(QStringLiteral("root.builtInDocks.indexOf(agentDock) < 0")).toBool());
        QVERIFY(!shell(QStringLiteral("newAiThreadAction.enabled")).toBool());

        application->settingsStore()->set(app::AiFeatures::kOffSetting, false);
        QVERIFY(agent().on());
        QVERIFY(agent().engine() != nullptr);
        QVERIFY(shell(QStringLiteral("root.builtInDocks.indexOf(agentDock) >= 0")).toBool());
    }

    void theComposerGrowsToSixLinesThenScrollsAndTheTimelineKeepsItsRoom()
    {
        window()->resize(1920, 1080);
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        QQuickItem *composer = item(QStringLiteral("agentComposer"));
        QQuickItem *scroll = item(QStringLiteral("agentComposerScroll"));
        QQuickItem *box = item(QStringLiteral("agentComposerBox"));
        const qreal oneLine = scroll->height();
        composer->setProperty("text", QStringLiteral("One\nTwo"));
        QTRY_VERIFY2(scroll->height() > oneLine,
                     qPrintable(QStringLiteral("one %1 now %2 max %3 implicit %4 panel %5")
                                    .arg(oneLine).arg(scroll->height()).arg(box->property("maxFieldHeight").toReal())
                                    .arg(composer->implicitHeight()).arg(item(QStringLiteral("agentView"))->height())));
        QStringList lines;
        for (int i = 1; i <= 12; ++i)
            lines << QStringLiteral("Line %1").arg(i);
        composer->setProperty("text", lines.join(QLatin1Char('\n')));
        QQuickTest::qWaitForPolish(window());
        QTRY_COMPARE(scroll->height(), box->property("maxFieldHeight").toReal());
        QVERIFY(composer->implicitHeight() > scroll->height());
        const QRectF view = onScene(item(QStringLiteral("agentView")));
        QVERIFY(onScene(item(QStringLiteral("agentAttach"))).bottom() <= view.bottom());
        QVERIFY(onScene(item(QStringLiteral("agentModeButton"))).bottom() <= view.bottom());
        QVERIFY(item(QStringLiteral("agentTimeline"))->height() >= 80);

        window()->resize(1280, 720);
        QQuickTest::qWaitForPolish(window());
        QTRY_VERIFY(scroll->height() <= box->property("maxFieldHeight").toReal());
        const QRectF small = onScene(item(QStringLiteral("agentView")));
        QVERIFY(onScene(item(QStringLiteral("agentModeButton"))).bottom() <= small.bottom());
        QVERIFY(item(QStringLiteral("agentTimeline"))->height() >= 80);
    }

    void theComposerRingShowsOnlyForTheKeyboard()
    {
        openPanel();
        QQuickItem *composer = item(QStringLiteral("agentComposer"));
        composer->setProperty("focus", false);
        QVERIFY(QMetaObject::invokeMethod(composer, "forceActiveFocus", Q_ARG(Qt::FocusReason, Qt::MouseFocusReason)));
        QTRY_VERIFY(composer->hasActiveFocus());
        QVERIFY(!item(QStringLiteral("agentComposerFocusRing")));
        QCOMPARE(item(QStringLiteral("agentComposerBox"))->property("border").value<QObject *>()->property("color"),
                 shell(QStringLiteral("Theme.accent")));
        item(QStringLiteral("agentAttach"))->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(composer, "forceActiveFocus", Q_ARG(Qt::FocusReason, Qt::TabFocusReason)));
        QTRY_VERIFY(item(QStringLiteral("agentComposerFocusRing")));
    }

    void fileChipsRemoveOnlyFromTheirButtonAndFoldAfterThree()
    {
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        const QList<qulonglong> tabs = openTabs({QStringLiteral("Sprite Fright - 01 - The Forest Path [Dialogue].ass"),
                                                 QStringLiteral("Sprite Fright - 02 - Mushroom Hunt [Signs].ass"),
                                                 QStringLiteral("Sprite Fright - 03 - Tiny Footsteps [Dialogue].ass"),
                                                 QStringLiteral("Sprite Fright - OP [Karaoke].ass")});
        QCOMPARE(tabs.size(), 4);
        for (const qulonglong id : tabs)
            agent().attach(id);
        const auto attached = [this] { return agent().thread().value(QStringLiteral("attached")).toList().size(); };
        QTRY_COMPARE(attached(), 4);
        QTRY_VERIFY(item(QStringLiteral("agentMoreFiles")));
        QCOMPARE(item(QStringLiteral("agentMoreFiles"))->property("text").toString(), QStringLiteral("+2 more"));
        QQuickItem *first = item(QStringLiteral("agentChip_%1").arg(tabs[0]));
        QVERIFY(first);
        QVERIFY(item(QStringLiteral("agentChip_%1").arg(tabs[1])));
        QVERIFY(!item(QStringLiteral("agentChip_%1").arg(tabs[2])));
        QTest::mouseClick(window(), Qt::LeftButton, {}, onScene(child(first, QStringLiteral("fileChipName"))).center().toPoint());
        QCOMPARE(attached(), 4);

        QQuickItem *remove = child(first, QStringLiteral("fileChipRemove"));
        QVERIFY(remove);
        QCOMPARE(remove->property("text").toString(), QStringLiteral("Remove Sprite Fright - 01 - The Forest Path [Dialogue].ass"));
        QVERIFY(remove->property("activeFocusOnTab").toBool());
        QVERIFY(QMetaObject::invokeMethod(remove, "click"));
        QTRY_COMPARE(attached(), 3);
        QTRY_VERIFY(item(QStringLiteral("agentChip_%1").arg(tabs[2])));
        QQuickItem *next = child(item(QStringLiteral("agentChip_%1").arg(tabs[1])), QStringLiteral("fileChipRemove"));
        next->forceActiveFocus();
        QTest::keyClick(window(), Qt::Key_Delete);
        QTRY_COMPARE(attached(), 2);
        QTRY_VERIFY(!item(QStringLiteral("agentMoreFiles")));
    }

    void attachHasItsOwnIconAndItsMenuOpensAboveTheComposerShowingWhatIsAttached()
    {
        window()->resize(1280, 720);
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        QCOMPARE(item(QStringLiteral("agentAttach"))->property("iconRole").toString(), QStringLiteral("attach"));
        QVERIFY(item(QStringLiteral("agentAttach"))->property("iconRole") != item(QStringLiteral("agentNewThread"))->property("iconRole"));
        QStringList names;
        for (int i = 1; i <= 12; ++i)
            names << QStringLiteral("Sprite Fright - %1 - The Long Way Home Through the Very Dark Forest [Dialogue].ass").arg(i, 2, 10, QLatin1Char('0'));
        const QList<qulonglong> tabs = openTabs(names);
        QCOMPARE(tabs.size(), 12);
        agent().attach(tabs[3]);
        QTRY_COMPARE(agent().thread().value(QStringLiteral("attached")).toList().size(), 1);

        QQuickItem *composer = item(QStringLiteral("agentComposer"));
        composer->forceActiveFocus();
        keys(QStringLiteral("@"));
        QTRY_VERIFY(popup(QStringLiteral("agentAttachMenu"))->property("opened").toBool());
        const QRectF menu = popupOnScene(QStringLiteral("agentAttachMenu"));
        const QRectF box = onScene(item(QStringLiteral("agentComposerBox")));
        QVERIFY2(menu.left() >= box.left() - 1 && menu.right() <= box.right() + 1, qPrintable(QStringLiteral("%1..%2").arg(menu.left()).arg(menu.right())));
        QVERIFY2(menu.bottom() <= box.top() + 1,
                 qPrintable(QStringLiteral("menu %1..%2 box top %3").arg(menu.top()).arg(menu.bottom()).arg(box.top())));
        QVERIFY(menu.top() >= 0);
        QQuickItem *activeTab = item(QStringLiteral("agentAttachActiveTab"));
        QVERIFY(activeTab->property("checked").toBool());
        QVERIFY(!activeTab->isEnabled());
        QQuickItem *row = item(QStringLiteral("agentAttach_%1").arg(tabs[3]));
        QVERIFY(row->property("checked").toBool());
        QCOMPARE(row->property("help").toString(), names[3]);
        QVERIFY(!item(QStringLiteral("agentAttach_%1").arg(tabs[4]))->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(row, "triggered"));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("attached")).toList().size(), 0);
        QCOMPARE(composer->property("text").toString(), QString());
    }

    void theModelAndModeMenusOpenAboveTheirButtonsAndModeChecksSitBesideTheirWords()
    {
        window()->resize(1280, 720);
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        const QRectF view = onScene(item(QStringLiteral("agentView")));
        for (const auto &[button, menu] : {std::pair{"agentModelButton", "agentModelMenu"}, std::pair{"agentModeButton", "agentModeMenu"}}) {
            const QRectF at = onScene(item(QLatin1String(button)));
            QVERIFY(click(QLatin1String(button)));
            QTRY_VERIFY(popup(QLatin1String(menu))->property("opened").toBool());
            const QRectF r = popupOnScene(QLatin1String(menu));
            QVERIFY2(r.left() >= view.left() - 1 && r.right() <= view.right() + 1,
                     qPrintable(QStringLiteral("%1 %2..%3 view %4..%5").arg(QLatin1String(menu)).arg(r.left()).arg(r.right()).arg(view.left()).arg(view.right())));
            QVERIFY2(r.bottom() <= at.top() + 1,
                     qPrintable(QStringLiteral("%1 bottom %2 button top %3").arg(QLatin1String(menu)).arg(r.bottom()).arg(at.top())));
            if (QLatin1String(menu) == QLatin1String("agentModeMenu")) {
                for (const char *mode : {"agentMode_manual", "agentMode_auto", "agentMode_full"}) {
                    QQuickItem *row = item(QLatin1String(mode));
                    QQuickItem *check = row->property("indicator").value<QQuickItem *>();
                    QQuickItem *words = child(row, QStringLiteral("menuItemReason"));
                    QVERIFY(check && words && words->isVisible());
                    QVERIFY2(onScene(check).right() <= onScene(words).left(), mode);
                    QVERIFY2(onScene(words).right() <= r.right(), mode);
                }
                QVERIFY(item(QStringLiteral("agentMode_auto"))->property("checked").toBool());
            }
            QVERIFY(QMetaObject::invokeMethod(popup(QLatin1String(menu)), "close"));
            QTRY_VERIFY(!popup(QLatin1String(menu))->property("visible").toBool());
        }
    }

    void cardsFitANarrowPanel()
    {
        application->settingsStore()->set(app::AgentController::kModeSetting, QStringLiteral("manual"));
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        agent().setModel(agent().thread().value(QStringLiteral("model")).toString());
        narrowPanel(300);
        const auto fits = [this](const char *card) {
            QQuickTest::qWaitForPolish(window());
            const QRectF view = onScene(item(QStringLiteral("agentView")));
            QVERIFY2(onScene(item(QStringLiteral("agentTimeline"))).right() <= view.right() + 1, card);
            const QStringList out = sticksOut(item(QLatin1String(card)), view);
            QVERIFY2(out.isEmpty(), qPrintable(out.join(QLatin1Char('\n'))));
        };
        type(applyScript("I tightened the first line."));
        QTRY_VERIFY(item(QStringLiteral("agentProposalApply")));
        fits("agentProposal");
        QVERIFY(click(QStringLiteral("agentProposalTell")));
        QTRY_VERIFY(item(QStringLiteral("agentProposalReply")));
        fits("agentProposal");
        QVERIFY(click(QStringLiteral("agentProposalDecline")) || (QTest::keyClick(window(), Qt::Key_Return), true));
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());

        agent().setFullAccessWarned();
        agent().setMode(QStringLiteral("full"));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("mode")).toString(), QStringLiteral("full"));
        const Json scaffold = Json::object(
            {{"operation", "plugins.scaffold"},
             {"params", Json::object({{"plugin", "com.example.sign-counter"}, {"name", "Sign counter"},
                                      {"permissions", Json::object({{"network", Json::object({{"hosts", Json::array({"api.example.com"})},
                                                                                              {"reason", "Looks up sign translations"}})}})},
                                      {"commands", Json::array({Json::object({{"id", "count"}, {"title", "Count signs"}})})}})}});
        const Json install = Json::object({{"operation", "plugins.install"}, {"params", Json::object({{"plugin", "com.example.sign-counter"}})}});
        type(QString::fromStdString(Json::object({{"say", "I made a plugin."}, {"calls", Json::array({scaffold, install})}}).dump()));
        QTRY_VERIFY2(item(QStringLiteral("agentInstallAccept")),
                     qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(QVariantList{lastOf(QLatin1String("tool"))}).toJson(QJsonDocument::Compact)) +
                                [this] {
                                    auto *items = qobject_cast<QAbstractItemModel *>(agent().items());
                                    QString all;
                                    for (int row = 0; row < items->rowCount(); ++row)
                                        all += QString::fromUtf8(QJsonDocument::fromVariant(items->index(row, 0).data(app::AgentItemsModel::ItemRole)).toJson(QJsonDocument::Compact)).left(400) + QLatin1Char('\n');
                                    return all;
                                }()));
        QVERIFY(item(QStringLiteral("agentInstallPermission_0")));
        fits("agentInstall");
        QVERIFY(click(QStringLiteral("agentInstallTell")));
        QTRY_VERIFY(item(QStringLiteral("agentInstallReply"))->hasActiveFocus());
        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_VERIFY(!item(QStringLiteral("agentInstallReply")));
        QVERIFY(item(QStringLiteral("agentInstallDecline"))->hasActiveFocus());
        QVERIFY(click(QStringLiteral("agentInstallDecline")));
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());
    }

    void anApprovalSaysWhatItDoesAndKeepsItsArgumentsUnderDetails()
    {
        application->settingsStore()->set(app::AgentController::kModeSetting, QStringLiteral("manual"));
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        narrowPanel(300);
        type(autoTurn(Json::object({{"operation", "settings.set"},
                                     {"params", Json::object({{"setting", "spelling.language"}, {"value", "en_GB"}})}})));
        QTRY_VERIFY(item(QStringLiteral("agentAllowOnce")));
        QCOMPARE(item(QStringLiteral("agentAskTitle"))->property("text").toString(), QStringLiteral("Change a setting?"));
        QCOMPARE(item(QStringLiteral("agentAskSummary"))->property("text").toString(), QStringLiteral("Set “spelling.language” to en_GB"));
        QVERIFY(!item(QStringLiteral("agentDetailsText")));
        QVERIFY(click(QStringLiteral("agentDetailsToggle")));
        QTRY_VERIFY(item(QStringLiteral("agentDetailsText")));
        QQuickTest::qWaitForPolish(window());
        QVERIFY(item(QStringLiteral("agentDetailsText"))->property("text").toString().startsWith(QLatin1String("settings.set\n{")));
        const QRectF view = onScene(item(QStringLiteral("agentView")));
        QStringList out = sticksOut(item(QStringLiteral("agentAsk")), view);
        QVERIFY2(out.isEmpty(), qPrintable(out.join(QLatin1Char('\n'))));
        QVERIFY(!item(QStringLiteral("agentDetailsText"))->property("truncated").toBool());

        QVERIFY(click(QStringLiteral("agentAskTell")));
        QTRY_VERIFY(item(QStringLiteral("agentAskReply")));
        QTRY_VERIFY(item(QStringLiteral("agentAskReply"))->hasActiveFocus());
        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_VERIFY(!item(QStringLiteral("agentAskReply")));
        QVERIFY(item(QStringLiteral("agentAllowOnce"))->hasActiveFocus());
        QVERIFY(click(QStringLiteral("agentAskTell")));
        QTRY_VERIFY(item(QStringLiteral("agentAskReply"))->hasActiveFocus());
        keys(QStringLiteral("Keep American English"));
        QTest::keyClick(window(), Qt::Key_Return);
        QTRY_COMPARE(lastOf(QLatin1String("ask")).value(QStringLiteral("status")).toString(), QStringLiteral("denied"));
        QTRY_VERIFY(!agent().thread().value(QStringLiteral("running")).toBool());

        agent().setMode(QStringLiteral("auto"));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("mode")).toString(), QStringLiteral("auto"));
        type(applyScript("Tightened."));
        QTRY_VERIFY(item(QStringLiteral("agentAllowOnce")));
        QCOMPARE(newest(QStringLiteral("agentAskTitle"))->property("text").toString(), QStringLiteral("Change the subtitles?"));
        QCOMPARE(newest(QStringLiteral("agentAskSummary"))->property("text").toString(), QStringLiteral("Change line 1 to “Shorter.”"));
        QQuickItem *reason = newest(QStringLiteral("agentAskReason"));
        if (reason)
            QVERIFY(!reason->property("truncated").toBool());
        QQuickTest::qWaitForPolish(window());
        out = sticksOut(newest(QStringLiteral("agentAsk")), onScene(item(QStringLiteral("agentView"))));
        QVERIFY2(out.isEmpty(), qPrintable(out.join(QLatin1Char('\n'))));
    }

    void aScriptReplyTakesTheKeyboardAndEscGoesBack()
    {
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        type(autoTurn(Json::object({{"operation", "automation.run"},
                                     {"params", Json::object({{"name", "Shout"}, {"script", "script_name = \"Shout\"\n"}})}})));
        QTRY_VERIFY(item(QStringLiteral("agentScriptRun")));
        QVERIFY(click(QStringLiteral("agentScriptTell")));
        QTRY_VERIFY(item(QStringLiteral("agentScriptReply"))->hasActiveFocus());
        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_VERIFY(!item(QStringLiteral("agentScriptReply")));
        QVERIFY(item(QStringLiteral("agentScriptRun"))->hasActiveFocus());
        QVERIFY(click(QStringLiteral("agentScriptTell")));
        QTRY_VERIFY(item(QStringLiteral("agentScriptReply"))->hasActiveFocus());
        keys(QStringLiteral("Use a macro"));
        QTest::keyClick(window(), Qt::Key_Return);
        QTRY_COMPARE(lastOf(QLatin1String("script")).value(QStringLiteral("status")).toString(), QStringLiteral("declined"));
    }

    void aSourceCardStaysWhileThePointerMovesToItAndOpensAtTheCitation()
    {
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        const std::string say = "Aimer sings it. The single came out in 2011.";
        const Json citations = Json::array({Json::object({{"index", 1}, {"url", "https://site1.example/page"}, {"title", "Page 1"},
                                                          {"quote", "Aimer is the singer."}, {"start", 0}, {"end", 15}})});
        const Json updates = Json::array({Json::object({{"sessionUpdate", "sources"}, {"textLength", static_cast<std::int64_t>(say.size())},
                                                        {"citations", citations}, {"searches", Json::array()}})});
        type(QString::fromStdString(Json::object({{"say", say}, {"calls", Json::array()}, {"updates", updates}}).dump()));
        QTRY_VERIFY(item(QStringLiteral("agentSource_1")));
        QQuickItem *source = item(QStringLiteral("agentSource_1"));
        QObject *card = popup(QStringLiteral("agentCitationHover"));
        QVERIFY(QMetaObject::invokeMethod(card, "show", Q_ARG(QVariant, QVariant(QVariantMap{{QStringLiteral("index"), 1},
                                                                                           {QStringLiteral("title"), QStringLiteral("Page 1")},
                                                                                           {QStringLiteral("url"), QStringLiteral("https://site1.example/page")}})),
                                          Q_ARG(QVariant, QVariant::fromValue(source)), Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(card->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(card, "leave"));
        QTest::qWait(150);
        QVERIFY(card->property("visible").toBool());
        QVERIFY(item(QStringLiteral("agentCitationOpen")));
        QTRY_VERIFY2(!card->property("visible").toBool(), card->property("pointerInside").toBool() ? "pointer inside" : "pointer outside");

        QQuickItem *answer = item(QStringLiteral("agentAnswer"));
        const QRectF word = answer->property("cursorRectangle").toRectF();
        QVERIFY(QMetaObject::invokeMethod(card, "show", Q_ARG(QVariant, QVariant(QVariantMap{{QStringLiteral("index"), 1}})),
                                          Q_ARG(QVariant, QVariant::fromValue(answer)), Q_ARG(QVariant, QVariant(QRectF(40, 0, 1, word.height())))));
        QTRY_VERIFY(card->property("opened").toBool());
        const QRectF at = popupOnScene(QStringLiteral("agentCitationHover"));
        QVERIFY(at.top() < onScene(answer).top() + word.height() + 12);
        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_VERIFY(!card->property("visible").toBool());
    }

    void renamingNeedsANameAndDeletingNamesTheThread()
    {
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        agent().rename(agent().current(), QStringLiteral("Honorifics pass"));
        QTRY_COMPARE(agent().thread().value(QStringLiteral("title")).toString(), QStringLiteral("Honorifics pass"));
        QVERIFY(click(QStringLiteral("agentThreadMenuButton")));
        QTRY_VERIFY(item(QStringLiteral("agentRenameThread")));
        QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("agentRenameThread")), "triggered"));
        QTRY_VERIFY(item(QStringLiteral("agentRenameField")));
        QQuickItem *field = item(QStringLiteral("agentRenameField"));
        QCOMPARE(field->property("selectedText").toString(), QStringLiteral("Honorifics pass"));
        QVERIFY(!item(QStringLiteral("agentRenameOk"))->isEnabled());
        field->setProperty("text", QStringLiteral("   "));
        QVERIFY(!item(QStringLiteral("agentRenameOk"))->isEnabled());
        field->setProperty("text", QStringLiteral("Honorifics, second pass"));
        QVERIFY(item(QStringLiteral("agentRenameOk"))->isEnabled());
        field->setProperty("text", QString());
        QTest::keyClick(field->window(), Qt::Key_Return);
        QTest::qWait(100);
        QCOMPARE(agent().thread().value(QStringLiteral("title")).toString(), QStringLiteral("Honorifics pass"));
        QVERIFY(QMetaObject::invokeMethod(field->window(), "reject"));
        QTRY_VERIFY(!item(QStringLiteral("agentRenameField")));

        for (QObject *open : window()->findChildren<QObject *>())
            if (open->inherits("QQuickPopup") && open->property("visible").toBool())
                QMetaObject::invokeMethod(open, "close");
        QTest::qWait(200);
        QVERIFY(click(QStringLiteral("agentThreadMenuButton")));
        QTRY_VERIFY(item(QStringLiteral("agentDeleteThread")));
        QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("agentDeleteThread")), "triggered"));
        const auto question = [] {
            for (QWindow *top : QGuiApplication::topLevelWindows())
                if (top->objectName() == QLatin1String("agentDeleteQuestion") && top->isVisible())
                    return top;
            return static_cast<QWindow *>(nullptr);
        };
        QTRY_VERIFY(question());
        QWindow *box = question();
        QCOMPARE(box->title(), QStringLiteral("Delete thread"));
        QCOMPARE(box->property("text").toString(), QStringLiteral("Delete “Honorifics pass”?"));
        QCOMPARE(box->property("detail").toString(),
                 QStringLiteral("Its messages are deleted. Changes it made stay in your subtitles and in the review."));
        QVERIFY(click(QStringLiteral("messageBoxChoice_cancel")));
        QTest::qWait(100);
        QVERIFY(agent().thread().contains(QStringLiteral("id")));
    }

    void escLeavesTheThreadsListAndASearchWithoutMatchesSaysSo()
    {
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QTRY_VERIFY(agent().thread().contains(QStringLiteral("id")));
        agent().rename(agent().current(), QStringLiteral("Honorifics pass"));
        QVERIFY(click(QStringLiteral("agentThreadsButton")));
        QTRY_VERIFY(item(QStringLiteral("agentThreadRow")));
        QQuickItem *search = item(QStringLiteral("agentThreadSearch"));
        search->forceActiveFocus();
        keys(QStringLiteral("karaoke"));
        QTRY_VERIFY(item(QStringLiteral("agentNoThreadsMatch")));
        QCOMPARE(item(QStringLiteral("agentNoThreadsMatch"))->property("text").toString(), QStringLiteral("No threads match “karaoke”"));
        QVERIFY(!item(QStringLiteral("agentThreadRow")));
        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_VERIFY(item(QStringLiteral("agentThreadRow")));
        QCOMPARE(search->property("text").toString(), QString());
        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_VERIFY(!item(QStringLiteral("agentThreadsList")));
        QVERIFY(item(QStringLiteral("agentTimeline")));
        QVERIFY(click(QStringLiteral("agentThreadsButton")));
        QTRY_VERIFY(item(QStringLiteral("agentThreadRow")));
        item(QStringLiteral("agentView"))->forceActiveFocus();
        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_VERIFY(!item(QStringLiteral("agentThreadsList")));
    }
};

HIKARI_QTEST_MAIN_IN_PARTS(AgentPanelTest)
#include "agent_panel_tests.moc"
