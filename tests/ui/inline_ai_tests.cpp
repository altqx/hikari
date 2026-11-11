#include "hikari/app/agent_controller.h"
#include "hikari/app/ai_features.h"
#include "hikari/app/application.h"
#include "hikari/app/inline_ai_controller.h"
#include "hikari/application/ai_providers.h"
#include "hikari/application/hotkeys.h"
#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_sidecar_host.h"

#include "docking.h"
#include "settings_store.h"

#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace hikari;
namespace ai = application::ai;

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

class InlineAiTest : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QString at(const QString &name) const
    {
        return root.filePath(QString::fromLatin1(QTest::currentTestFunction()) + QLatin1Char('/') + name);
    }
    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    app::InlineAiController &inlineAi() const { return *application->inlineAi(); }
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
        for (QObject *popup : window()->findChildren<QObject *>())
            if (auto *popupItem = qobject_cast<QQuickItem *>(popup->property("popupItem").value<QObject *>()))
                if (QQuickItem *found = find(popupItem))
                    return found;
        return nullptr;
    }
    bool click(const QString &name)
    {
        QQuickItem *button = item(name);
        return button && button->isEnabled() && QMetaObject::invokeMethod(button, "click");
    }
    std::u8string text(std::size_t row) const { return session().document().lines()[row]->text; }
    void setMode(const char *mode)
    {
        application->settingsStore()->set(app::AgentController::kModeSetting, QString::fromLatin1(mode));
    }
    void editByHand(std::size_t row, const char *to)
    {
        using api::Json;
        const auto line = session().document().lines()[row]->id;
        const Json params = Json::object(
            {{"revision", Json(session().revision())},
             {"name", "By hand"},
             {"edits", Json::array({Json::object({{"op", "set_line"},
                                                  {"line", Json(line.value)},
                                                  {"fields", Json::object({{"text", to}})}})})}});
        bool ok = false;
        application->apiHost()->api().call(api::Caller{.id = "test", .label = "Test"}, "document.apply", params,
                                           [&ok](api::Result result) { ok = result.has_value(); });
        QVERIFY(ok);
    }
    void selectAll() { QVERIFY(QMetaObject::invokeMethod(application.get(), "selectAllLines")); }

private slots:
    void init()
    {
        QVERIFY(QDir(root.path()).mkpath(QString::fromLatin1(QTest::currentTestFunction())));
        ai::Config config;
        ai::Provider provider = ai::presetProvider(ai::Preset::OpenRouter, "or", "OpenRouter");
        provider.models = {{.id = "inline-upper", .name = "Upper", .price = ai::Price{.input = 0.5, .output = 0}}};
        config.providers.push_back(provider);
        const bool cliOnly = QTest::currentTestFunction() == QByteArrayLiteral("anAgentCliAloneLeavesTheActionsOff");
        if (!cliOnly)
            config.roles[ai::Role::Chat] = {"or", "inline-upper"};

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
        ports.agentOnPath = [cliOnly](const QString &agent) { return cliOnly && agent == QStringLiteral("claude"); };

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
                   "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\i1}hello{\\i0} there\n"
                   "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,second\n"
                   "Comment: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,a note\n");
        file.close();
        QVERIFY(application->openFile(path));
        QTRY_VERIFY(application->agent()->ready());
    }
    void cleanup()
    {
        engine.reset();
        application.reset();
    }

    void theMenusOfferTheActionsOnlyWithAiOn()
    {
        QVERIFY(inlineAi().on());
        QVERIFY(shell(QStringLiteral("editorStrip.aiShown")).toBool());
        QVERIFY(shell(QStringLiteral("aiActionsAction.enabled")).toBool());
        QVERIFY(!shell(QStringLiteral("repeatAiActionAction.enabled")).toBool());
        QVERIFY(application::aiCommand(application::hotkeyIdOf("GLOBAL_AI_ACTIONS")));
        QVERIFY(application::aiCommand(application::hotkeyIdOf("GLOBAL_REPEAT_AI_ACTION")));

        QVERIFY(shell(QStringLiteral("aiActionsAction.trigger(), gridAiHotkeyMenu.visible")).toBool());
        QTRY_VERIFY(item(QStringLiteral("inlineAiRewrite")));
        QVERIFY(item(QStringLiteral("inlineAiShorten")));
        shell(QStringLiteral("gridAiHotkeyMenu.close()"));
        QTRY_VERIFY(!shell(QStringLiteral("gridAiHotkeyMenu.visible")).toBool());

        application->settingsStore()->set(app::AiFeatures::kOffSetting, true);
        QTRY_VERIFY(!inlineAi().on());
        QVERIFY(!shell(QStringLiteral("editorStrip.aiShown")).toBool());
        QVERIFY(!shell(QStringLiteral("aiActionsAction.enabled")).toBool());
        QVERIFY(!shell(QStringLiteral("gridAiHotkeyMenu.shown")).toBool());
    }

    void anAgentCliAloneLeavesTheActionsOff()
    {
        QVERIFY(application->agent()->ready());
        QVERIFY(inlineAi().on());
        const QVariantMap ready = inlineAi().ready();
        QVERIFY(!ready.value(QStringLiteral("chat")).toBool());
        QVERIFY(!ready.value(QStringLiteral("translator")).toBool());
        QVERIFY(!ready.value(QStringLiteral("reviewer")).toBool());
        QVERIFY(!ready.value(QStringLiteral("any")).toBool());

        QVERIFY(shell(QStringLiteral("aiActionsAction.trigger(), gridAiHotkeyMenu.visible")).toBool());
        QTRY_VERIFY(item(QStringLiteral("inlineAiRewrite")));
        for (const char *name : {"inlineAiRewrite", "inlineAiShorten", "inlineAiSpelling", "inlineAiTranslate",
                                 "inlineAiTranslateTo", "inlineAiRepeat"})
            QVERIFY2(!item(QString::fromLatin1(name))->isEnabled(), name);
        QCOMPARE(item(QStringLiteral("inlineAiSettings"))->property("text").toString(),
                 QStringLiteral("Add a model in Settings › AI…"));
        shell(QStringLiteral("gridAiHotkeyMenu.close()"));
        QVERIFY(!shell(QStringLiteral("repeatAiActionAction.enabled")).toBool());
    }

    void autoRewriteWritesOneReviewTurnAndOneHistoryStep()
    {
        setMode("auto");
        selectAll();
        const std::size_t steps = session().historySize();
        shell(QStringLiteral("inlineAiBox.begin({action: 'rewrite'}, false, null)"));
        QTRY_VERIFY(item(QStringLiteral("inlineAiInstruction")));
        QCOMPARE(item(QStringLiteral("inlineAiSkipped")) != nullptr, true);
        QVERIFY(!item(QStringLiteral("inlineAiRun"))->isEnabled());
        item(QStringLiteral("inlineAiInstruction"))->setProperty("text", QStringLiteral("Shout it"));
        QVERIFY(click(QStringLiteral("inlineAiRun")));

        QTRY_COMPARE(text(0), u8"{\\i1}HELLO{\\i0} THERE");
        QCOMPARE(text(1), u8"SECOND");
        QCOMPARE(text(2), u8"a note");
        QCOMPARE(session().historySize(), steps + 1);
        QVERIFY(session().history().back().name.starts_with("AI: Rewrite (2 line"));
        QTRY_COMPARE(shell(QStringLiteral("root.review.waiting")).toInt(), 2);
        QCOMPARE(inlineAi().last().value(QStringLiteral("action")).toString(), QStringLiteral("rewrite"));
        QCOMPARE(inlineAi().recentInstructions(), QStringList{QStringLiteral("Shout it")});
        QVERIFY(shell(QStringLiteral("repeatAiActionAction.enabled")).toBool());
    }

    void manualResultsWaitForApplyAndSkipEditedLines()
    {
        setMode("manual");
        selectAll();
        const std::size_t steps = session().historySize();
        QVERIFY(inlineAi().start({{QStringLiteral("action"), QStringLiteral("spelling")}}, false));
        QTRY_COMPARE(inlineAi().results().size(), 2);
        QCOMPARE(text(1), u8"second");
        QCOMPARE(session().historySize(), steps);
        QTRY_VERIFY(item(QStringLiteral("inlineAiApply")));
        QCOMPARE(item(QStringLiteral("inlineAiApply"))->property("text").toString(), QStringLiteral("Apply 2"));

        editByHand(0, "edited by hand");
        QTRY_VERIFY(inlineAi().results().front().toMap().value(QStringLiteral("stale")).toBool());
        QTRY_COMPARE(item(QStringLiteral("inlineAiApply"))->property("text").toString(), QStringLiteral("Apply 1"));

        QVERIFY(click(QStringLiteral("inlineAiApply")));
        QTRY_COMPARE(text(1), u8"SECOND");
        QCOMPARE(text(0), u8"edited by hand");
        QVERIFY(inlineAi().results().isEmpty());
        QTRY_VERIFY(!item(QStringLiteral("inlineAiResults")));
    }

    void discardWritesNothing()
    {
        setMode("manual");
        selectAll();
        const std::size_t steps = session().historySize();
        QVERIFY(!inlineAi().start({{QStringLiteral("action"), QStringLiteral("shorten")}}, false));
        QVERIFY(inlineAi().start({{QStringLiteral("action"), QStringLiteral("translate")},
                                  {QStringLiteral("language"), QStringLiteral("French")}},
                                 false));
        QTRY_COMPARE(inlineAi().results().size(), 2);
        QVERIFY(click(QStringLiteral("inlineAiDiscard")));
        QVERIFY(inlineAi().results().isEmpty());
        QCOMPARE(session().historySize(), steps);
        QCOMPARE(text(1), u8"second");
    }
};

QTEST_MAIN(InlineAiTest)
#include "inline_ai_tests.moc"
