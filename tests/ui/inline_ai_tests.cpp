#include "hikari/app/agent_controller.h"
#include "hikari/app/ai_features.h"
#include "hikari/app/application.h"
#include "hikari/app/inline_ai_controller.h"
#include "hikari/application/ai_providers.h"
#include "hikari/application/hotkeys.h"
#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_sidecar_host.h"

#include "docking.h"
#include "log_controller.h"
#include "settings_store.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QScreen>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
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
    void translationRunShots(const QString &out, const QString &state, const QString &shown)
    {
        QVERIFY(QDir().mkpath(out));
        setMode("auto");
        QVERIFY(application->turnOnTranslationMode());
        shell(QStringLiteral("applyPreset('Translation')"));
        QTRY_VERIFY(shell(QStringLiteral("originalShown")).toBool());
        selectAll();
        QVERIFY(inlineAi().start({{QStringLiteral("action"), QStringLiteral("translate")},
                                  {QStringLiteral("language"), QStringLiteral("French")}},
                                 false));
        QTRY_VERIFY(item(shown));
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        for (const QSize size : {QSize(1366, 768), QSize(1280, 800)}) {
            window()->resize(size);
            QTRY_COMPARE(window()->size(), size);
            for (const char *theme : {"light", "dark", "highContrastWhite", "highContrastBlack"}) {
                settings.setValue(QStringLiteral("appearance.theme"), QString::fromLatin1(theme));
                QTest::qWait(300);
                QVERIFY(window()->grabWindow().save(QStringLiteral("%1/translation-run-%2-%3x%4-%5.png")
                                                        .arg(out, state)
                                                        .arg(size.width())
                                                        .arg(size.height())
                                                        .arg(QLatin1String(theme))));
            }
        }
        settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("dark"));
    }

private slots:
    void init()
    {
        QVERIFY(QDir(root.path()).mkpath(QString::fromLatin1(QTest::currentTestFunction())));
        ai::Config config;
        ai::Provider provider = ai::presetProvider(ai::Preset::OpenRouter, "or", "OpenRouter");
        provider.models = {{.id = "inline-upper", .name = "Upper", .price = ai::Price{.input = 0.5, .output = 0}},
                           {.id = "hang", .name = "Hang"},
                           {.id = "fail", .name = "Fail"}};
        config.providers.push_back(provider);
        const QByteArray test = QTest::currentTestFunction();
        const bool cliOnly = test == QByteArrayLiteral("anAgentCliAloneLeavesTheActionsOff");
        const bool hang = test == QByteArrayLiteral("aTranslationRunShowsItsProgressInTheOriginalTextPanel") ||
                          test == QByteArrayLiteral("translationRunningScreenshots");
        const bool fail = test == QByteArrayLiteral("aFailedTranslationRunReportsInTheOriginalTextPanel") ||
                          test == QByteArrayLiteral("translationFailedScreenshots");
        const std::string model = hang ? "hang" : fail ? "fail" : "inline-upper";
        if (!cliOnly && test != QByteArrayLiteral("aProviderAddedWhileRunningIsInEveryModelListAtOnce"))
            config.roles[ai::Role::Chat] = {"or", model};

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
        // MUST KILL: spell check off so its no-dictionary toast stays out of the screenshots
        if (test.endsWith("Screenshots"))
            application->applySettings({{QStringLiteral("editor.spellchecker"), false}});
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
        QVERIFY(QDir().mkpath(at(QStringLiteral("honya"))));
        file.write("[Script Info]\nScriptType: v4.00+\nHonya Project: honya\n\n[Events]\n"
                   "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                   "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\i1}hello{\\i0} there\n"
                   "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,second?\n"
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

    void customActionsFollowASeparatorAndAskingOnesEndInAnEllipsis()
    {
        QVERIFY(shell(QStringLiteral("aiActionsAction.trigger(), gridAiHotkeyMenu.visible")).toBool());
        QTRY_VERIFY(item(QStringLiteral("inlineAiRewrite")));
        QCOMPARE(shell(QStringLiteral("gridAiHotkeyMenu.title")).toString(), QStringLiteral("AI actions"));
        QVERIFY(!shell(QStringLiteral("gridAiHotkeyMenu.itemAt(6).visible")).toBool());
        QCOMPARE(item(QStringLiteral("inlineAiSettings"))->property("text").toString(), QStringLiteral("Edit actions"));
        shell(QStringLiteral("gridAiHotkeyMenu.close()"));
        QTRY_VERIFY(!shell(QStringLiteral("gridAiHotkeyMenu.visible")).toBool());

        inlineAi().setActions({QVariantMap{{QStringLiteral("name"), QStringLiteral("Make it formal")},
                                           {QStringLiteral("instruction"), QStringLiteral("Formal register")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("Explain the idiom")},
                                           {QStringLiteral("ask"), true}}});
        QVERIFY(shell(QStringLiteral("aiActionsAction.trigger(), gridAiHotkeyMenu.visible")).toBool());
        QTRY_VERIFY(item(QStringLiteral("inlineAiCustom1")));
        QCOMPARE(shell(QStringLiteral("[6, 7, 8].map(i => gridAiHotkeyMenu.itemAt(i).objectName).join(' ')")).toString(),
                 QStringLiteral("inlineAiCustomSeparator inlineAiCustom0 inlineAiCustom1"));
        QVERIFY(shell(QStringLiteral("gridAiHotkeyMenu.itemAt(6).visible")).toBool());
        QCOMPARE(item(QStringLiteral("inlineAiCustom0"))->property("text").toString(), QStringLiteral("Make it formal"));
        QCOMPARE(item(QStringLiteral("inlineAiCustom1"))->property("text").toString(), QStringLiteral("Explain the idiom…"));
        shell(QStringLiteral("gridAiHotkeyMenu.close()"));
    }

    void aiActionsScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        window()->resize(1400, 860);
        inlineAi().setActions({QVariantMap{{QStringLiteral("name"), QStringLiteral("Make it formal")},
                                           {QStringLiteral("instruction"), QStringLiteral("Formal register")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("Explain the idiom")},
                                           {QStringLiteral("ask"), true}}});
        selectAll();
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        for (const char *theme : {"light", "dark", "highContrastWhite", "highContrastBlack"}) {
            settings.setValue(QStringLiteral("appearance.theme"), QString::fromLatin1(theme));
            QTest::qWait(300);
            QVERIFY(shell(QStringLiteral("aiActionsAction.trigger(), gridAiHotkeyMenu.visible")).toBool());
            QTRY_VERIFY(item(QStringLiteral("inlineAiCustom1")));
            QTest::qWait(300);
            const QRect frame = window()->frameGeometry();
            QVERIFY(window()->screen()->grabWindow(0, frame.x(), frame.y(), frame.width(), frame.height())
                        .save(out + QStringLiteral("/cx7-ai-actions-") + QLatin1String(theme) + QStringLiteral(".png")));
            shell(QStringLiteral("gridAiHotkeyMenu.close()"));
            QTRY_VERIFY(!shell(QStringLiteral("gridAiHotkeyMenu.visible")).toBool());
        }
    }

    void translationRunningScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        translationRunShots(out, QStringLiteral("running"), QStringLiteral("translationRunRow"));
        if (!QTest::currentTestFailed())
            QVERIFY(click(QStringLiteral("translationRunStop")));
    }

    void translationFailedScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        translationRunShots(out, QStringLiteral("failed"), QStringLiteral("translationRunProviders"));
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
                 QStringLiteral("Add a model in Settings › AI"));
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
        QCOMPARE(text(1), u8"SECOND?");
        QCOMPARE(text(2), u8"a note");
        QCOMPARE(session().historySize(), steps + 1);
        QVERIFY(session().history().back().name.starts_with("AI: Rewrite (2 line"));
        QTRY_COMPARE(shell(QStringLiteral("root.review.waiting")).toInt(), 2);
        QCOMPARE(inlineAi().last().value(QStringLiteral("action")).toString(), QStringLiteral("rewrite"));
        QCOMPARE(inlineAi().recentInstructions(), QStringList{QStringLiteral("Shout it")});
        QVERIFY(shell(QStringLiteral("repeatAiActionAction.enabled")).toBool());
    }

    void suggestSynonymsTakesTheWordAtTheCaretOrAOneWordSelection()
    {
        const QString stored = QString::fromUtf8(reinterpret_cast<const char *>(text(0).data()), qsizetype(text(0).size()));
        const qsizetype there = stored.indexOf(QStringLiteral("there"));
        const qsizetype hello = stored.indexOf(QStringLiteral("hello"));
        QCOMPARE(inlineAi().synonymWord(int(there + 2), int(there + 2), stored), QStringLiteral("there"));
        QCOMPARE(inlineAi().synonymWord(int(there), int(there + 5), stored), QStringLiteral("there"));
        QCOMPARE(inlineAi().synonymWord(int(hello + 5), int(hello + 5), stored), QStringLiteral("hello"));
        QCOMPARE(inlineAi().synonymWord(int(there), int(there + 4), stored), QString());
        QCOMPARE(inlineAi().synonymWord(int(hello), int(there + 5), stored), QString());
        QCOMPARE(inlineAi().synonymWord(2, 2, stored), QString());
        QCOMPARE(inlineAi().synonymWord(int(there + 2), int(there + 2), stored + QStringLiteral("!")), QString());

        editByHand(0, "\\Nwell-known");
        QCOMPARE(inlineAi().synonymWord(2, 2, QStringLiteral("\\Nwell-known")), QStringLiteral("well-known"));
        QCOMPARE(inlineAi().synonymWord(1, 1, QStringLiteral("\\Nwell-known")), QString());
    }

    void aChosenSynonymReplacesOnlyTheWordAsOneHistoryStepEvenInManual()
    {
        setMode("manual");
        const QString stored = QString::fromUtf8(reinterpret_cast<const char *>(text(0).data()), qsizetype(text(0).size()));
        const int at = int(stored.indexOf(QStringLiteral("there"))) + 1;
        const std::size_t steps = session().historySize();
        QVERIFY(inlineAi().suggestSynonyms(at, at, stored));
        QCOMPARE(inlineAi().synonyms().value(QStringLiteral("state")).toString(), QStringLiteral("finding"));
        QTRY_COMPARE(inlineAi().synonyms().value(QStringLiteral("state")).toString(), QStringLiteral("found"));
        QCOMPARE(inlineAi().synonyms().value(QStringLiteral("word")).toString(), QStringLiteral("there"));
        QCOMPARE(inlineAi().synonyms().value(QStringLiteral("words")).toStringList(),
                 (QStringList{QStringLiteral("vast"), QStringLiteral("large"), QStringLiteral("huge")}));
        QVERIFY(!inlineAi().useSynonym(QStringLiteral("enormous")));
        QVERIFY(inlineAi().useSynonym(QStringLiteral("large")));
        QCOMPARE(text(0), u8"{\\i1}hello{\\i0} large");
        QCOMPARE(text(1), u8"second?");
        QCOMPARE(session().historySize(), steps + 1);
        QCOMPARE(session().history().back().name, std::string("AI: Synonym"));
        QVERIFY(inlineAi().synonyms().isEmpty());
        QVERIFY(inlineAi().results().isEmpty());
    }

    void aSynonymForALineEditedMeanwhileChangesNothing()
    {
        const QString stored = QString::fromUtf8(reinterpret_cast<const char *>(text(0).data()), qsizetype(text(0).size()));
        const int at = int(stored.indexOf(QStringLiteral("there"))) + 1;
        QVERIFY(inlineAi().suggestSynonyms(at, at, stored));
        QTRY_COMPARE(inlineAi().synonyms().value(QStringLiteral("state")).toString(), QStringLiteral("found"));
        editByHand(0, "by hand");
        const std::size_t steps = session().historySize();
        QVERIFY(!inlineAi().useSynonym(QStringLiteral("vast")));
        QCOMPARE(text(0), u8"by hand");
        QCOMPARE(session().historySize(), steps);

        editByHand(0, "say nothing");
        QVERIFY(inlineAi().suggestSynonyms(6, 6, QStringLiteral("say nothing")));
        QTRY_COMPARE(inlineAi().synonyms().value(QStringLiteral("state")).toString(), QStringLiteral("none"));
        QVERIFY(!inlineAi().suggestSynonyms(3, 4, QStringLiteral("say nothing")));
        QVERIFY(inlineAi().synonyms().isEmpty());
    }

    void onlyTheLineEditorsMenusOfferSynonyms()
    {
        QVERIFY(shell(QStringLiteral("lineText.cursorPosition = lineText.text.indexOf('there') + 1, editorAiMenu.popup(), "
                                     "editorAiMenu.visible")).toBool());
        QTRY_COMPARE(shell(QStringLiteral("editorAiMenu.synonymWord")).toString(), QStringLiteral("there"));
        shell(QStringLiteral("editorAiMenu.close()"));
        QTRY_VERIFY(!shell(QStringLiteral("editorAiMenu.visible")).toBool());
        QVERIFY(shell(QStringLiteral("aiActionsAction.trigger(), gridAiHotkeyMenu.visible")).toBool());
        QTRY_VERIFY(item(QStringLiteral("inlineAiRewrite")));
        QCOMPARE(shell(QStringLiteral("gridAiHotkeyMenu.synonymWord")).toString(), QString());
        shell(QStringLiteral("gridAiHotkeyMenu.close()"));
    }

    void manualResultsWaitForApplyAndSkipEditedLines()
    {
        setMode("manual");
        selectAll();
        const std::size_t steps = session().historySize();
        QVERIFY(inlineAi().start({{QStringLiteral("action"), QStringLiteral("spelling")}}, false));
        QTRY_COMPARE(inlineAi().results().size(), 2);
        QCOMPARE(text(1), u8"second?");
        QCOMPARE(session().historySize(), steps);
        QTRY_VERIFY(item(QStringLiteral("inlineAiApply")));
        QCOMPARE(item(QStringLiteral("inlineAiApply"))->property("text").toString(), QStringLiteral("Apply 2"));

        editByHand(0, "edited by hand");
        QTRY_VERIFY(inlineAi().results().front().toMap().value(QStringLiteral("stale")).toBool());
        QTRY_COMPARE(item(QStringLiteral("inlineAiApply"))->property("text").toString(), QStringLiteral("Apply 1"));

        QVERIFY(click(QStringLiteral("inlineAiApply")));
        QTRY_COMPARE(text(1), u8"SECOND?");
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
        QCOMPARE(text(1), u8"second?");
        QVERIFY(!QFile::exists(at(QStringLiteral("honya/GLOSSARY.md"))));
    }

    void shortenSendsTheLinesOverTheCpsWarningSetting()
    {
        setMode("manual");
        selectAll();
        QSignalSpy changed(&inlineAi(), &app::InlineAiController::maxCpsChanged);
        QCOMPARE(inlineAi().maxCps(), 15);
        QVERIFY(!inlineAi().start({{QStringLiteral("action"), QStringLiteral("shorten")}}, false));

        application->settingsStore()->set("grid.cpsWarning", 8);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(inlineAi().maxCps(), 8);
        QVERIFY(inlineAi().start({{QStringLiteral("action"), QStringLiteral("shorten")}}, false));
        QTRY_COMPARE(inlineAi().results().size(), 1);
    }
    void translateInTranslationModeWritesTheTranslationAndMarksLinesThatNeedReview()
    {
        setMode("auto");
        QVERIFY(application->turnOnTranslationMode());
        selectAll();
        const std::size_t steps = session().historySize();
        QVERIFY(inlineAi().start({{QStringLiteral("action"), QStringLiteral("translate")},
                                  {QStringLiteral("language"), QStringLiteral("French")}},
                                 false));
        QTRY_COMPARE(session().historySize(), steps + 1);
        const auto lines = session().document().lines();
        QCOMPARE(lines[0]->text, u8"{\\i1}hello{\\i0} there");
        QCOMPARE(lines[0]->translation, u8"{\\i1}HELLO{\\i0} THERE");
        QVERIFY(!lines[0]->unconfirmed);
        QCOMPARE(lines[1]->translation, u8"SECOND?");
        QVERIFY(lines[1]->unconfirmed);
        QCOMPARE(lines[2]->translation, u8"");
        QTRY_COMPARE(inlineAi().notice().value(QStringLiteral("review")).toList(), QVariantList{2});
        QFile glossary(at(QStringLiteral("honya/GLOSSARY.md")));
        QVERIFY(glossary.open(QIODevice::ReadOnly));
        QCOMPARE(glossary.readAll(), QByteArray("# Glossary\n"));
    }

    void aTranslationRunShowsItsProgressInTheOriginalTextPanel()
    {
        setMode("auto");
        QVERIFY(application->turnOnTranslationMode());
        QTRY_VERIFY(shell(QStringLiteral("originalShown")).toBool());
        selectAll();
        QVERIFY(inlineAi().start({{QStringLiteral("action"), QStringLiteral("translate")},
                                  {QStringLiteral("language"), QStringLiteral("French")}},
                                 false));
        QTRY_VERIFY(item(QStringLiteral("translationRunRow")));
        QVERIFY(item(QStringLiteral("translationRunLine")));
        QVERIFY(!item(QStringLiteral("inlineAiProgress")));
        QCOMPARE(item(QStringLiteral("translationRunText"))->property("text").toString(), QStringLiteral("AI translation · 0 of 2 line(s)"));
        QVERIFY(click(QStringLiteral("translationRunStop")));
        QTRY_VERIFY(!item(QStringLiteral("translationRunRow")));
        QTRY_VERIFY(item(QStringLiteral("translationRunNotice")));
        QVERIFY(!item(QStringLiteral("translationRunProviders")));
        QVERIFY(!item(QStringLiteral("inlineAiNotice")));
        QVERIFY(click(QStringLiteral("translationRunDismiss")));
        QTRY_VERIFY(!item(QStringLiteral("translationRunState")));
    }

    void aFailedTranslationRunReportsInTheOriginalTextPanel()
    {
        setMode("auto");
        QVERIFY(application->turnOnTranslationMode());
        QTRY_VERIFY(shell(QStringLiteral("originalShown")).toBool());
        selectAll();
        QVERIFY(inlineAi().start({{QStringLiteral("action"), QStringLiteral("translate")},
                                  {QStringLiteral("language"), QStringLiteral("French")}},
                                 false));
        QTRY_VERIFY(item(QStringLiteral("translationRunProviders")));
        QVERIFY(item(QStringLiteral("translationRunNoticeText"))->property("text").toString().startsWith(QStringLiteral("AI translation stopped. ")));
        QCOMPARE(item(QStringLiteral("translationRunNoticeIcon"))->property("iconRole").toString(), QStringLiteral("notice-error"));
        QVERIFY(!item(QStringLiteral("inlineAiNotice")));
        QTRY_VERIFY(application->log().history().contains(QStringLiteral("AI translation stopped. ")));
        QVERIFY(click(QStringLiteral("translationRunDetails")));
        QTRY_VERIFY(shell(QStringLiteral("panelShown(logDock)")).toBool());

        QVERIFY(QMetaObject::invokeMethod(shell(QStringLiteral("originalDock")).value<QObject *>(), "close"));
        QTRY_VERIFY(!shell(QStringLiteral("originalShown")).toBool());
        QTRY_VERIFY(item(QStringLiteral("inlineAiNotice")));
    }

    void aFailedLineEndsAsASentenceBeforeTheNextPart_data()
    {
        QTest::addColumn<QString>("detail");
        QTest::newRow("no stop") << QStringLiteral("the provider refused the key");
        QTest::newRow("full stop") << QStringLiteral("the provider refused the key.");
        QTest::newRow("exclamation and space") << QStringLiteral("the provider refused the key! ");
    }
    void aFailedLineEndsAsASentenceBeforeTheNextPart()
    {
        QFETCH(QString, detail);
        auto *bar = window()->findChild<QQuickItem *>(QStringLiteral("inlineAiBar"));
        QVERIFY(bar);
        const QVariantMap notice{
            {QStringLiteral("end"), QStringLiteral("done")},
            {QStringLiteral("failed"),
             QVariantList{QVariantMap{{QStringLiteral("row"), 3},
                                      {QStringLiteral("why"), QStringLiteral("failed")},
                                      {QStringLiteral("detail"), detail}}}},
            {QStringLiteral("skipped"), 1}};
        QVariant text;
        QVERIFY(QMetaObject::invokeMethod(bar, "noticeText", Q_RETURN_ARG(QVariant, text), Q_ARG(QVariant, notice)));
        QCOMPARE(text.toString(), QStringLiteral("Line 3 failed: the provider refused the key. 1 line wasn’t sent."));
    }
    void translateSendsTheSourceLanguageOnlyWhenOneIsSet()
    {
        setMode("auto");
        QVERIFY(application->turnOnTranslationMode());
        selectAll();
        const auto translate = [this] {
            const std::size_t steps = session().historySize();
            QVERIFY(inlineAi().start({{QStringLiteral("action"), QStringLiteral("translate")}}, false));
            QTRY_COMPARE(session().historySize(), steps + 1);
        };
        translate();
        QCOMPARE(session().document().lines()[0]->translation, u8"{\\i1}HELLO{\\i0} THERE");

        application->settingsStore()->setValue(QStringLiteral("translation.sourceLanguage"), QStringLiteral(" Japanese "));
        translate();
        QCOMPARE(session().document().lines()[0]->translation, u8"Japanese: {\\i1}HELLO{\\i0} THERE");
    }

    void aProviderAddedWhileRunningIsInEveryModelListAtOnce()
    {
        QVERIFY(!inlineAi().ready().value(QStringLiteral("chat")).toBool());
        const auto listed = [this](const QString &text) {
            const QVariantList models = application->agent()->models();
            return std::ranges::any_of(models, [&](const QVariant &m) { return m.toMap().value(QStringLiteral("text")) == text; });
        };
        QVERIFY(!listed(QStringLiteral("Fresh")));
        QSignalSpy agentModels(application->agent(), &app::AgentController::modelsChanged);
        QSignalSpy inlineModels(&inlineAi(), &app::InlineAiController::changed);

        ai::Provider added = ai::presetProvider(ai::Preset::OpenAI, "openai", "OpenAI");
        added.models = {{.id = "fresh", .name = "Fresh"}};
        bool saved = false;
        application->ai()->providers()->add(added, "sk-test", [&](std::optional<ai::SaveError> error) { saved = !error; });
        QTRY_VERIFY(saved);
        QVERIFY(agentModels.count() >= 1);
        QVERIFY(inlineModels.count() >= 1);
        QVERIFY(listed(QStringLiteral("Fresh")));
        QVERIFY(inlineAi().ready().value(QStringLiteral("chat")).toBool());

        agentModels.clear();
        bool removed = false;
        application->ai()->providers()->remove("openai", [&](std::optional<ai::SaveError> error) { removed = !error; });
        QTRY_VERIFY(removed);
        QVERIFY(agentModels.count() >= 1);
        QVERIFY(!listed(QStringLiteral("Fresh")));
    }
};

QTEST_MAIN(InlineAiTest)
#include "inline_ai_tests.moc"
