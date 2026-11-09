#include "hikari/app/agent_controller.h"
#include "hikari/app/ai_features.h"
#include "hikari/app/application.h"
#include "hikari/application/ai_providers.h"
#include "hikari/application/hotkeys.h"
#include "hikari/backends/ai_sidecar_host.h"

#include "docking.h"
#include "line_table_model.h"
#include "settings_store.h"
#include "shell_controller.h"
#include "theme.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QtQuickTest/quicktest.h>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace hikari;
namespace ai = application::ai;
using api::Json;

namespace {

class Keys final : public ai::KeyStore {
public:
    void read(const std::string &, std::function<void(ai::KeyResult)> done) override { done(ai::KeyResult("key")); }
    void write(const std::string &, const std::string &, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        done(std::nullopt);
    }
    void remove(const std::string &, std::function<void(std::optional<ai::KeyError>)> done) override
    {
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
    std::unique_ptr<app::Application> application;
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
        if (QQuickItem *found = find(window()->contentItem()))
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

private slots:
    void init()
    {
        const bool chatModel = QTest::currentTestFunction() != QByteArrayLiteral("withoutAChatModelThePanelPointsToSettings");
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
        ports.agents = [](backends::ai::AgentBroker &) {};

        app::Application::Options options;
        options.settingsFile = at(QStringLiteral("settings.toml"));
        options.userFolder = at(QStringLiteral("user"));
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
    }

    void aTurnStreamsIntoTheTimelineAndItsChangeWaitsInTheReview()
    {
        application->settingsStore()->set(app::AgentController::kModeSetting, QStringLiteral("manual"));
        QVERIFY(shell(QStringLiteral("newAiThreadAction.trigger(), agentDock.isOpen")).toBool());
        QTRY_VERIFY(item(QStringLiteral("agentView")));
        QVERIFY(agent().ready());
        QCOMPARE(agent().thread().value(QStringLiteral("mode")).toString(), QStringLiteral("manual"));
        QCOMPARE(item(QStringLiteral("agentActiveTab"))->property("text").toString(),
                 QStringLiteral("episode.ass · active tab"));

        type(applyScript("I tightened the first line."));
        QTRY_COMPARE(kinds().last(), QStringLiteral("change"));
        QCOMPARE(kinds(), (QStringList{"user", "text", "change"}));
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

    void fullAccessWarnsOnceAndTheAlwaysAskListStillAsks()
    {
        openPanel();
        QVERIFY(click(QStringLiteral("agentNewThread")));
        QVERIFY(click(QStringLiteral("agentModeButton")));
        QTRY_VERIFY(item(QStringLiteral("agentMode_full")));
        QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("agentMode_full")), "triggered"));
        QTRY_VERIFY(item(QStringLiteral("agentDontWarn")));
        QCOMPARE(agent().thread().value(QStringLiteral("mode")).toString(), QStringLiteral("auto"));
        item(QStringLiteral("agentDontWarn"))->setProperty("checked", true);
        QVERIFY(click(QStringLiteral("agentTurnOnFullAccess")));
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
        QVERIFY(!item(QStringLiteral("agentDontWarn")));
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
};

QTEST_MAIN(AgentPanelTest)
#include "agent_panel_tests.moc"
