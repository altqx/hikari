#include "hikari/app/agent_controller.h"
#include "hikari/app/ai_features.h"
#include "hikari/app/application.h"
#include "hikari/app/automation_shell.h"
#include "hikari/app/inline_ai_controller.h"
#include "hikari/app/review_controller.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/application/ai_providers.h"
#include "hikari/application/automation_registry.h"
#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "audio_controller.h"
#include "docking.h"
#include "explorer_model.h"
#include "fake_network.h"
#include "line_grid.h"
#include "node_effects_controller.h"
#include "settings_store.h"
#include "template_editor_controller.h"
#include "theme.h"
#include "video_controller.h"
#include "visual_tools_controller.h"
#include "workspace_layout.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QQmlExpression>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>
#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
namespace ai = hikari::application::ai;

namespace {

class StandInKeys final : public ai::KeyStore {
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

class StandInConfig final : public ai::ConfigStore {
public:
    explicit StandInConfig(ai::Config config) : m_config(std::move(config)) {}
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

class ReleaseScreenshotsTests : public QObject {
    Q_OBJECT

    static constexpr int kEditingFrame = 1812;
    static constexpr int kSignFrame = 1440;
    static constexpr int kTopRowOfTheGridRange = 7;
    static constexpr int kItalicLineFrame = 1550;
    static constexpr int kHiddenLineFrame = 1360;
    static constexpr int kPerspectiveTool = 11;
    static constexpr int kKaraokeFrame = 1818;
    static constexpr int kFuriganaFrame = 1824;
    static constexpr int kMushroomFrame = 3270;
    static constexpr int kNodeEffectsSourceFrame = 165;
    static constexpr int kFallingLettersPreset = 8;
    static constexpr int kGrammarWait = 60000;
    static constexpr std::pair<double, double> kMushroomCap[] = {{1050, 300}, {1068, 294}, {1068, 294}, {1087, 298}, {1087, 298}, {1123, 307}, {1123, 307}, {1149, 312}, {1149, 312}, {1165, 317}, {1165, 317}, {1173, 321}, {1173, 321}, {1175, 322}, {1175, 322}, {1175, 322}, {1175, 322}, {1173, 321}, {1173, 321}, {1171, 321}, {1171, 321}, {1165, 323}, {1165, 323}, {1149, 322}, {1149, 322}, {1135, 322}, {1135, 322}, {1122, 314}, {1122, 314}, {1114, 307}, {1114, 307}, {1103, 303}, {1103, 303}, {1081, 311}, {1081, 311}, {1054, 321}};
    static constexpr int kKaraokeRow = 34;
    static constexpr int kColourSweepPreset = 2;
    static constexpr int kFuriganaRisePreset = 14;
    static constexpr int kBetweenLinesFrame = 2000;
    static constexpr const char *kSignHelpers = R"lua(script_name = "Sign helpers"
script_description = "Small edits for signs"
script_version = "1.0"

local function prefix(tags)
    return function(subs, selected)
        for _, i in ipairs(selected) do
            local line = subs[i]
            line.text = tags .. line.text
            subs[i] = line
        end
        return selected
    end
end

aegisub.register_macro("Signs/Soften the edges", "Adds \\blur1.5 to the selected lines", prefix("{\\blur1.5}"))
aegisub.register_macro("Signs/Fade in and out", "Adds \\fad(200,200) to the selected lines", prefix("{\\fad(200,200)}"))
)lua";

    std::unique_ptr<QTemporaryDir> home;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QString out;
    QString video;
    QString song;
    std::unique_ptr<test::FakeNetwork> marketplace;
    QTranslator english;

    QObject *root() const { return engine->rootObjects().first(); }

    void start()
    {
        home = std::make_unique<QTemporaryDir>();
        QVERIFY(home->isValid());
        app::Application::Options o;
        o.settingsFile = home->filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        o.legacyAutosaveDir = home->filePath(QStringLiteral("Subs"));
        o.harperLs = QStringLiteral(HIKARI_HARPER_LS);
        o.grammarDir = home->filePath(QStringLiteral("Harper"));
        if (QTest::currentTestFunction() == QByteArrayLiteral("homeAiReview")
            || QTest::currentTestFunction() == QByteArrayLiteral("aiAiMenu"))
            o.aiPorts = standInAi();
        if (QTest::currentTestFunction() == QByteArrayLiteral("checkingSpellingMenuWithoutFilm")) {
            o.spellingBackend = app::Application::Options().spellingBackend;
            o.userFolder = home->path();
            QVERIFY(writeAt(home->filePath(QStringLiteral("Dictionary/en_US.aff")), "SET UTF-8\nTRY esianrtolcdugmphbyfvkwz'\n"));
            QVERIFY(writeAt(home->filePath(QStringLiteral("Dictionary/en_US.dic")),
                            "12\nFinally\nfinally\nthe\nThe\ntent\ntents\nten\nnet\nis\nup\nit\ntext\n"));
        }
        if (QTest::currentTestFunction() == QByteArrayLiteral("pluginsAutomationMenu")) {
            const QString folder = home->filePath(QStringLiteral("plugins/com.example.line-tools"));
            QVERIFY(QDir().mkpath(folder));
            for (const char *file : {"hikari-plugin.json", "main.lua"})
                QVERIFY(QFile::copy(QStringLiteral(HIKARI_PLUGIN_EXAMPLES "/line-tools/") + QLatin1String(file),
                                    folder + QLatin1Char('/') + QLatin1String(file)));
            QVERIFY(writeAt(home->filePath(QStringLiteral("plugins.json")),
                            R"({"format": 1, "plugins": {"com.example.line-tools": {"version": "1.2.0", "enabled": true}}})"));
            o.plugins = {home->filePath(QStringLiteral("plugins")), home->filePath(QStringLiteral("plugin-data")),
                         home->filePath(QStringLiteral("plugin-cache")), home->filePath(QStringLiteral("plugins.json"))};
            o.pluginHelper = QStringLiteral(HIKARI_PLUGIN_HELPER);
        }
        if (QTest::currentTestFunction() == QByteArrayLiteral("homePlugins")) {
            o.plugins = {home->filePath(QStringLiteral("plugins")), home->filePath(QStringLiteral("plugin-data")),
                         home->filePath(QStringLiteral("plugin-cache")), home->filePath(QStringLiteral("plugins.json"))};
            o.marketplace = app::MarketplaceFeed::hikarisub();
            o.marketplaceNetwork = liveMarketplaceCopy(o.marketplace.base);
        }
        application = new app::Application(o);
        application->settingsStore()->setValue(QStringLiteral("appearance.followSystem"), false);
        application->setAskForBadResolution(false);
        application->settingsStore()->setValue(QStringLiteral("explorer.groupByName"), true);
        engine = new QQmlApplicationEngine;
        ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(root());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(root()->property("sideBarsLoaded").toBool());
        window->resize(1600, 960);
    }

    app::AiFeatures::Ports standInAi()
    {
        ai::Config config;
        ai::Provider provider = ai::presetProvider(ai::Preset::OpenRouter, "stand-in", "Stand-in");
        provider.models = {{.id = "m", .name = "Stand-in model", .price = ai::Price{.input = 0, .output = 0}}};
        config.providers.push_back(provider);
        config.roles[ai::Role::Chat] = {"stand-in", "m"};
        const QString flags = writeFile(QStringLiteral("stand-in-bundle"),
                                        "--mode ok --replies " + home->filePath(QStringLiteral("replies.json")).toUtf8());
        app::AiFeatures::Ports ports;
        ports.keyStore = [] { return std::make_unique<StandInKeys>(); };
        ports.configStore = [config] { return std::make_unique<StandInConfig>(config); };
        ports.sidecarHost = [this](const backends::ai::SidecarInstall &install) {
            backends::ai::AiSidecarHost::Options o;
            o.program = QStringLiteral(HIKARI_FAKE_AI_SIDECAR);
            o.arguments = {install.bundle};
            o.workingDirectory = home->filePath(QStringLiteral("ai/work"));
            o.handshakeMs = 3'000;
            return std::make_unique<backends::ai::AiSidecarHost>(std::move(o), &application->apiHost()->api(),
                                                                 api::Caller{.id = "ai", .label = "AI", .reviewed = true});
        };
        ports.localSidecar = backends::ai::SidecarInstall{QStringLiteral("0.0.0-dev"), {}, flags, {}};
        ports.agents = [](backends::ai::AgentBroker &, ai::KeyStore &) {};
        ports.agentOnPath = [](const QString &) { return false; };
        return ports;
    }

    test::FakeNetwork *liveMarketplaceCopy(const QUrl &feed)
    {
        marketplace = std::make_unique<test::FakeNetwork>();
        const QDir copy(qEnvironmentVariable("HIKARI_SHOT_MARKETPLACE"));
        QDirIterator files(copy.path(), QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            QFile file(files.next());
            if (file.open(QIODevice::ReadOnly))
                marketplace->files.insert(feed.resolved(QUrl(copy.relativeFilePath(file.fileName()))).toString(), file.readAll());
        }
        return marketplace.get();
    }

    auto dumpTemplateEditorOnFailure()
    {
        return qScopeGuard([this] {
            if (!QTest::currentTestFailed())
                return;
            auto &t = application->templateEditor();
            qWarning() << "template editor: available" << t.available() << "editable" << t.editable() << "templates"
                       << t.templateCount() << "karaoke" << t.karaokeCount() << "trusted" << t.trusted() << "pending"
                       << t.previewPending() << "problem" << t.problem() << "source" << t.sourceNumber() << "unknown"
                       << t.sourceUnknown() << "preview" << t.hasPreview() << "video" << t.videoPreview() << "canApply"
                       << t.canApply();
        });
    }

    QVariant evaluate(const QString &code)
    {
        QQmlExpression expression(qmlContext(window), window, code);
        const QVariant result = expression.evaluate();
        if (expression.hasError())
            qWarning() << expression.error();
        return result;
    }

    QString writeFile(const QString &name, const QByteArray &body) const
    {
        const QString path = home->filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size())
            return {};
        return path;
    }

    void keepMismatchedResolution()
    {
        QObject *host = root()->findChild<QObject *>(QStringLiteral("messages"));
        QQuickWindow *box = nullptr;
        if (QTest::qWaitFor([&] {
                box = host ? qvariant_cast<QQuickWindow *>(host->property("box")) : nullptr;
                return box && box->isVisible() && box->title() == QLatin1String("Open video");
            }, 3000)) {
            QMetaObject::invokeMethod(box, "answer", Q_ARG(QVariant, QStringLiteral("keep")));
            QTRY_VERIFY(!box->isVisible());
        }
    }

    void showVideoAt(int frame)
    {
        auto &controller = application->video();
        if (!controller.hasVideo()) {
            controller.openVideo(video);
            QTRY_VERIFY_WITH_TIMEOUT(controller.hasVideo(), 60000);
            keepMismatchedResolution();
        }
        QVERIFY(controller.session().seekFrame(frame));
        QTRY_VERIFY_WITH_TIMEOUT(controller.session().shownFrame() == std::optional<int>(frame), 60000);
        QVERIFY(application->selectLinesVisibleOnVideo());
        auto *grid = root()->findChild<ui::LineGrid *>(QStringLiteral("editingGrid"));
        QVERIFY(grid);
        QTRY_VERIFY(grid->activeLine().has_value());
        grid->makeLineVisible(grid->activeLine()->value);
        if (!application->audio().hasAudio())
            application->openAudioFromVideo();
        QTRY_VERIFY_WITH_TIMEOUT(application->audio().ready(), 60000);
    }

    void applyLayout(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(root(), "applyPreset", Q_ARG(QVariant, QString::fromLatin1(name))));
    }

    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (!from)
            return nullptr;
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }

    QQuickItem *item(const char *name) const
    {
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(w))
                if (QQuickItem *found = findItem(quick->contentItem(), QLatin1String(name)))
                    return found;
        return nullptr;
    }

    ui::LineGrid *grid() const { return root()->findChild<ui::LineGrid *>(QStringLiteral("editingGrid")); }

    void setTheme(const QString &code)
    {
        application->settingsStore()->setValue(QStringLiteral("appearance.theme"), code);
        auto *controls = root()->property("palette").value<QObject *>();
        QTRY_COMPARE(controls->property("window").value<QColor>(), ui::theme::current().roles.panel);
        QTest::qWait(300);
    }

    static QRect cropOf(QQuickItem *of)
    {
        const qreal dpr = of->window()->effectiveDevicePixelRatio();
        const QRectF r = of->mapRectToScene(QRectF(0, 0, of->width(), of->height()));
        return QRectF(r.topLeft() * dpr, r.size() * dpr).toAlignedRect();
    }

    QImage grab(QQuickWindow *from, const char *crop) const
    {
        const QImage whole = from->grabWindow();
        if (!crop)
            return whole;
        QQuickItem *part = item(crop);
        return part ? whole.copy(cropOf(part)) : QImage();
    }

    void save(const QImage &image, const QString &page, const QString &name)
    {
        QVERIFY(!image.isNull());
        QVERIFY(QDir().mkpath(out + QLatin1Char('/') + page));
        QVERIFY(image.save(out + QLatin1Char('/') + page + QLatin1Char('/') + name + QStringLiteral(".png")));
    }

    void shoot(const QString &page, const QString &name, const char *crop = nullptr, QQuickWindow *from = nullptr)
    {
        for (const char *code : {"light", "dark"}) {
            setTheme(QString::fromLatin1(code));
            save(grab(from ? from : window, crop), page, name + QLatin1Char('-') + QLatin1String(code));
        }
    }

    void shootFourThemesTiled(const QString &page, const QString &name)
    {
        QImage tiled(window->width() * 2, window->height() * 2, QImage::Format_RGB32);
        QPainter painter(&tiled);
        int i = 0;
        for (const char *code : {"light", "dark", "highContrastWhite", "highContrastBlack"}) {
            setTheme(QString::fromLatin1(code));
            const QImage shot = window->grabWindow().scaled(window->size());
            painter.drawImage(QPoint((i % 2) * window->width(), (i / 2) * window->height()), shot);
            ++i;
        }
        painter.end();
        save(tiled, page, name);
    }

    void selectAt(int frame)
    {
        auto &controller = application->video();
        QVERIFY(controller.session().seekFrame(frame));
        QTRY_VERIFY_WITH_TIMEOUT(controller.session().shownFrame() == std::optional<int>(frame), 60000);
        QVERIFY(application->selectLinesVisibleOnVideo());
        QTRY_VERIFY(grid()->activeLine().has_value());
        grid()->scrollToTopRow(kTopRowOfTheGridRange);
    }

    void openCommonsDialogue(int frame)
    {
        QVERIFY(openDialogue());
        applyLayout("Editing");
        showVideoAt(frame);
        resampleToTheVideo();
        giveDefaultAFilmSubtitleLook();
        selectAt(frame);
    }

    static bool onRayMv() { return QByteArray(QTest::currentTestFunction()).endsWith("OnRayMv"); }
    static bool withoutFilm() { return QByteArray(QTest::currentTestFunction()).endsWith("WithoutFilm"); }

    QObject *named(const char *name) const { return root()->findChild<QObject *>(QLatin1String(name)); }

    void openMenu(QObject *menu)
    {
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "open"));
        QTRY_VERIFY(menu->property("opened").toBool());
    }

    static void closeMenu(QObject *menu)
    {
        QMetaObject::invokeMethod(menu, "close");
        QTRY_VERIFY(!menu->property("visible").toBool());
    }

    void popupFromMenuBar(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(root(), "buildMenus"));
        QObject *menu = named(name);
        auto *menus = named("appMenus");
        QVERIFY2(menu && menus, name);
        for (int i = 0; i < menus->property("count").toInt(); ++i) {
            QQuickItem *title = nullptr;
            QVERIFY(QMetaObject::invokeMethod(menus, "itemAt", Q_RETURN_ARG(QQuickItem *, title), Q_ARG(int, i)));
            if (title && title->property("menu").value<QObject *>() == menu) {
                QVERIFY(QMetaObject::invokeMethod(menu, "popup", Q_ARG(QQuickItem *, title),
                                                  Q_ARG(QPointF, QPointF(0, title->height()))));
                QTRY_VERIFY(menu->property("opened").toBool());
                return;
            }
        }
        QFAIL(name);
    }

    static QObject *popupOf(QObject *comboBox) { return comboBox ? comboBox->property("popup").value<QObject *>() : nullptr; }

    void shootOpen(const QString &page, const QString &name, const std::function<void(QList<QObject *> &shown)> &open,
                   const char *crop = nullptr, QQuickWindow *from = nullptr)
    {
        for (const char *code : {"light", "dark"}) {
            setTheme(QString::fromLatin1(code));
            QList<QObject *> menus;
            open(menus);
            if (QTest::currentTestFailed())
                return;
            QVERIFY(!menus.isEmpty() && !menus.contains(nullptr));
            QTest::qWait(300);
            save(grab(from ? from : window, crop), page, name + QLatin1Char('-') + QLatin1String(code));
            for (auto it = menus.rbegin(); it != menus.rend(); ++it)
                closeMenu(*it);
        }
    }

    void seekTo(int frame)
    {
        auto &controller = application->video();
        QVERIFY(controller.session().seekFrame(frame));
        QTRY_VERIFY_WITH_TIMEOUT(controller.session().shownFrame() == std::optional<int>(frame), 60000);
    }

    qulonglong lineIdAt(int row) const
    {
        const auto &lines = application->files().session(*application->workspace().editingTarget())->document().lines();
        return row >= 0 && std::size_t(row) < lines.size() ? lines[std::size_t(row)]->id.value : 0;
    }

    int rowOfLine(qulonglong id) const
    {
        const auto &lines = application->files().session(*application->workspace().editingTarget())->document().lines();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i]->id.value == id)
                return int(i);
        return -1;
    }

    qulonglong groupLines(qulonglong first, int count, const QString &description)
    {
        application->selectLine(first);
        application->extendSelection(count - 1);
        if (!application->makeGroups())
            return 0;
        const qulonglong made = lineIdAt(rowOfLine(first) - 1);
        return application->renameGroup(made, description) ? made : 0;
    }

    void openKaraokeSong()
    {
        QVERIFY(application->openFile(song));
        applyLayout("Editing");
        auto &controller = application->video();
        controller.openVideo(video);
        QTRY_VERIFY_WITH_TIMEOUT(controller.hasVideo(), 60000);
        keepMismatchedResolution();
        QVERIFY(controller.session().seekFrame(kKaraokeFrame));
        QTRY_VERIFY_WITH_TIMEOUT(controller.session().shownFrame() == std::optional<int>(kKaraokeFrame), 60000);
        const auto lines = application->files().session(*application->workspace().editingTarget())->document().lines();
        const auto id = lines[kKaraokeRow - 1]->id.value;
        application->selectLine(id);
        grid()->makeLineVisible(id);
        application->openAudioFromVideo();
        QTRY_VERIFY_WITH_TIMEOUT(application->audio().ready(), 60000);
    }

    void openOwnKaraokeLines(const QByteArray &events)
    {
        const QString file = writeFile(QStringLiteral("Our karaoke.ass"),
            QByteArrayLiteral("[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
                              "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                              "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, "
                              "Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                              "Style: Default,Noto Sans CJK JP,80,&H00FFFFFF,&H00FF8C3C,&H00301018,&H80000000,-1,0,0,0,100,100,"
                              "0,0,1,3,0,8,10,10,80,1\n\n"
                              "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n")
                + events);
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Editing");
        showVideoAt(kKaraokeFrame);
    }

    bool loadTemplater()
    {
        application->automation().load(std::string(hikari::application::kBuiltinKaraokeTemplater));
        QDeadlineTimer deadline(30'000);
        while (!deadline.hasExpired()) {
            const auto scripts = application->automation().scripts();
            if (std::ranges::any_of(scripts, [](const auto &s) { return s.path == std::string(hikari::application::kBuiltinKaraokeTemplater); })
                && std::ranges::none_of(scripts, [](const auto &s) { return s.state == hikari::application::ScriptStatus::State::Loading; }))
                return true;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        return false;
    }

    static bool writeAt(const QString &path, const QByteArray &body)
    {
        QFile file(path);
        return QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly)
            && file.write(body) == body.size();
    }

    QQuickWindow *otherShellWindow() const
    {
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (auto *shell = qobject_cast<QQuickWindow *>(w);
                shell && shell != window && shell->isVisible() && shell->property("sideBarsLoaded").isValid())
                return shell;
        return nullptr;
    }

    bool openDialogue(const QString &target = {})
    {
        const QString commons = qEnvironmentVariable("HIKARI_SHOT_SUBTITLES");
        if (!commons.isEmpty())
            return application->openFile(commons)
                && application->saveChosenFor(application->editingDocument(),
                                              QUrl::fromLocalFile(target.isEmpty() ? home->filePath(QStringLiteral("Sprite Fright.ass")) : target),
                                              QStringLiteral("ass"))
                       .isEmpty();
        const QString own = writeFile(QStringLiteral("Sprite Fright.ass"),
                                      signsScript("Dialogue: 0,0:01:14.42,0:01:17.50,Default,,0,0,0,,Finally, the tent is up!\n"));
        return !own.isEmpty() && application->openFile(own);
    }

    void resampleToTheVideo()
    {
        const QVariantMap v = application->resampleValues();
        QVERIFY(v.value(QStringLiteral("hasVideo")).toBool());
        const int width = v.value(QStringLiteral("videoWidth")).toInt();
        const int height = v.value(QStringLiteral("videoHeight")).toInt();
        QVERIFY(application->resample(v.value(QStringLiteral("subsWidth")).toInt(),
                                      v.value(QStringLiteral("subsHeight")).toInt(), width, height, false));
        QCOMPARE(application->resampleValues().value(QStringLiteral("subsHeight")).toInt(), height);
    }

    void giveDefaultAFilmSubtitleLook()
    {
        auto &styles = application->styleManager();
        QVariantMap values = styles.beginEdit(false, 0);
        QCOMPARE(values.value(QStringLiteral("name")).toString(), QStringLiteral("Default"));
        const auto colour = [](int r, int g, int b, int a) {
            return QVariantMap{{QStringLiteral("r"), r}, {QStringLiteral("g"), g}, {QStringLiteral("b"), b}, {QStringLiteral("a"), a}};
        };
        values.insert(QStringLiteral("fontname"), QStringLiteral("Noto Sans Medium"));
        values.insert(QStringLiteral("fontsize"), QStringLiteral("48"));
        values.insert(QStringLiteral("bold"), false);
        values.insert(QStringLiteral("primary"), colour(246, 244, 238, 0));
        values.insert(QStringLiteral("outline"), colour(18, 18, 22, 0));
        values.insert(QStringLiteral("back"), colour(0, 0, 0, 140));
        values.insert(QStringLiteral("borderStyle"), 1);
        values.insert(QStringLiteral("outlineWidth"), QStringLiteral("2.5"));
        values.insert(QStringLiteral("shadow"), QStringLiteral("1.5"));
        values.insert(QStringLiteral("alignment"), QStringLiteral("2"));
        values.insert(QStringLiteral("marginLeft"), QStringLiteral("120"));
        values.insert(QStringLiteral("marginRight"), QStringLiteral("120"));
        values.insert(QStringLiteral("marginVertical"), QStringLiteral("40"));
        QCOMPARE(styles.commitEdit(values, {}, false, false), QString());
        styles.endEdit();
    }

    static QByteArray signsScript(const QByteArray &events)
    {
        return QByteArrayLiteral("[Script Info]\nScriptType: v4.00+\nPlayResX: 2048\nPlayResY: 858\n\n"
                                 "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                                 "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, "
                                 "Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                                 "Style: Default,Noto Sans,64,&H00FFFFFF,&H000000FF,&H00202020,&H80000000,0,0,0,0,100,100,0,0,1,3,"
                                 "1,2,60,60,40,1\n"
                                 "Style: Sign,Noto Serif,72,&H00E8F4FF,&H000000FF,&H00283020,&H00000000,1,0,0,0,100,100,2,0,1,4,"
                                 "0,7,0,0,0,1\n\n"
                                 "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n")
            + events;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(english.load(QStringLiteral(HIKARI_EN_CATALOG)));
        QVERIFY(QCoreApplication::installTranslator(&english));
    }

    void cleanupTestCase() { QCoreApplication::removeTranslator(&english); }

    void init()
    {
        out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        if (onRayMv()) {
            video = qEnvironmentVariable("HIKARI_SHOT_KARAOKE_VIDEO");
            song = qEnvironmentVariable("HIKARI_SHOT_KARAOKE_SUBTITLES");
            const QString fonts = qEnvironmentVariable("HIKARI_SHOT_KARAOKE_FONTS");
            if (video.isEmpty() || song.isEmpty() || fonts.isEmpty())
                QSKIP("HIKARI_SHOT_KARAOKE_VIDEO, _SUBTITLES and _FONTS are not all set");
            for (const QFileInfo &font : QDir(fonts).entryInfoList(QDir::Files))
                QFontDatabase::addApplicationFont(font.absoluteFilePath());
        } else {
            video = qEnvironmentVariable("HIKARI_SHOT_VIDEO");
            if (video.isEmpty() && !withoutFilm())
                QSKIP("HIKARI_SHOT_VIDEO is not set");
        }
        QVERIFY(video.isEmpty() || QFileInfo(video).isFile());
        QVERIFY(QDir().mkpath(out));
        start();
    }

    void cleanup()
    {
        delete engine;
        engine = nullptr;
        delete application;
        application = nullptr;
        marketplace.reset();
        home.reset();
    }

    void getStartedTour()
    {
        openCommonsDialogue(kEditingFrame);
        shoot(QStringLiteral("get-started"), QStringLiteral("window"));
        shoot(QStringLiteral("basics"), QStringLiteral("grid"), "gridPanel");
        shoot(QStringLiteral("basics"), QStringLiteral("video-panel"), "videoPanel");
        shootFourThemesTiled(QStringLiteral("customize"), QStringLiteral("themes"));
        application->audio().setSpectrumOn(true);
        shoot(QStringLiteral("basics"), QStringLiteral("audio-spectrum"), "audioPanel");
    }

    void getStartedTypesetting()
    {
        const QString file = writeFile(QStringLiteral("Sprite Fright signs.ass"),
                                       signsScript("Dialogue: 0,0:00:59.50,0:01:00.50,Sign,,0,0,0,,"
                                                   "{\\pos(1180,150)\\frz-4\\blur1}Brambleden Woods\n"));
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Typesetting");
        showVideoAt(kSignFrame);
        shoot(QStringLiteral("get-started"), QStringLiteral("typesetting"));
    }

    void basicsLineEditor()
    {
        openCommonsDialogue(kItalicLineFrame);
        shoot(QStringLiteral("basics"), QStringLiteral("line-editor"), "editorPanel");
    }

    void basicsVideoFullscreen()
    {
        openCommonsDialogue(kEditingFrame);
        application->settingsStore()->setValue(QStringLiteral("video.fullscreenBar"), 1);
        QVERIFY(application->videoFullscreen().toggle(0));
        const auto fullscreenWindow = [] {
            for (QWindow *w : QGuiApplication::allWindows())
                if (w->objectName() == QLatin1String("videoFullscreen") && w->isExposed())
                    return qobject_cast<QQuickWindow *>(w);
            return static_cast<QQuickWindow *>(nullptr);
        };
        QTRY_VERIFY(fullscreenWindow());
        QQuickWindow *fullscreen = fullscreenWindow();
        QTest::mouseMove(fullscreen, QPoint(fullscreen->width() / 2, fullscreen->height() - 4));
        QTRY_VERIFY_WITH_TIMEOUT(!item("videoFullscreenHintText")->isVisible(), 5000);
        shoot(QStringLiteral("basics"), QStringLiteral("fullscreen"), nullptr, fullscreen);
    }

    void translationTranslationMode()
    {
        openCommonsDialogue(kEditingFrame);
        QVERIFY(application->turnOnTranslationMode());
        const std::pair<int, const char *> ourThai[] = {
            {1680, "โอ้! ดูเธอบินไปสิ!"},
            {1700, "ว้าว!"},
            {1750, "หลีกไป เจ้าเนิร์ด! พวกเราจะตั้งแคมป์ตรงนี้"},
            {kEditingFrame, "ในที่สุด! ได้เวลาจุดเตาบาร์บีคิวแล้ว"},
            {1900, "โอ้ แย่แล้ว"},
        };
        for (const auto &[frame, thai] : ourThai) {
            selectAt(frame);
            const QString text = QString::fromUtf8(thai);
            application->editor().translationEdited(text, int(text.size()));
            QVERIFY(application->editor().commit());
        }
        applyLayout("Translation");
        selectAt(kEditingFrame);
        shoot(QStringLiteral("translation"), QStringLiteral("translation-mode"));
    }
    void basicsGridHiddenLines()
    {
        openCommonsDialogue(kEditingFrame);
        selectAt(kHiddenLineFrame);
        QVERIFY(application->hideSelectedLines());
        QTRY_VERIFY(application->shell().filtered());
        selectAt(kEditingFrame);
        grid()->scrollToTopRow(kTopRowOfTheGridRange - 1);
        shoot(QStringLiteral("basics"), QStringLiteral("hidden-lines"), "gridPanel");
    }

    void checkingSubtitleComparison()
    {
        openCommonsDialogue(kEditingFrame);
        const std::pair<int, const char *> ourEdits[] = {
            {1700, "Wow, look!"},
            {kEditingFrame, "Finally! Time to get the barbecue going."},
        };
        for (const auto &[frame, text] : ourEdits) {
            selectAt(frame);
            const QString edited = QString::fromUtf8(text);
            application->editor().textEdited(edited, int(edited.size()));
            QVERIFY(application->editor().commit());
        }
        QVERIFY(application->openFile(qEnvironmentVariable("HIKARI_SHOT_SUBTITLES")));
        application->selectTab(0);
        QVERIFY(application->compareWithTab(1));
        selectAt(kEditingFrame);
        shoot(QStringLiteral("checking"), QStringLiteral("comparison-grid"));
    }

    void homeTypesetting()
    {
        const QString file = writeFile(QStringLiteral("Sprite Fright signs.ass"),
                                       signsScript("Dialogue: 0,0:00:59.50,0:01:00.50,Sign,,0,0,0,,"
                                                   "{\\an5\\pos(1180,150)\\org(1180,150)\\frx14\\fry-22\\frz-4\\blur1}"
                                                   "Brambleden Woods\n"));
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Typesetting");
        showVideoAt(kSignFrame);
        application->visualTools().selectFamily(kPerspectiveTool);
        QTRY_VERIFY(item("visualOption_plane"));
        QTest::mouseMove(window, QPoint(1, 1));
        shoot(QStringLiteral("home"), QStringLiteral("typesetting"), "videoPanel");
    }

    void homeNodeEffectsOnRayMv()
    {
        openKaraokeSong();
        QVERIFY(QMetaObject::invokeMethod(root(), "setNodeEffects", Q_ARG(QVariant, true)));
        QTRY_VERIFY(application->workspaceLayout().nodeEffects());
        auto &effects = application->nodeEffects();
        QVERIFY(effects.addPreset(kFallingLettersPreset));
        effects.setPreviewOnVideo(true);
        QTRY_VERIFY_WITH_TIMEOUT(effects.videoPreview(), 20000);
        auto &controller = application->video();
        QVERIFY(controller.session().seekFrame(kNodeEffectsSourceFrame));
        QTRY_VERIFY_WITH_TIMEOUT(controller.session().shownFrame() == std::optional<int>(kNodeEffectsSourceFrame), 60000);
        const qint64 shownMs = qint64(kNodeEffectsSourceFrame) * 1000 / 24;
        QTRY_VERIFY(std::ranges::any_of(effects.previewLines(), [shownMs](const QVariant &line) {
            const auto l = line.toMap();
            return l.value(QStringLiteral("startMs")).toLongLong() <= shownMs && shownMs < l.value(QStringLiteral("endMs")).toLongLong();
        }));
        QTest::qWait(1500);
        QTest::mouseMove(window, QPoint(1, 1));
        shoot(QStringLiteral("home"), QStringLiteral("node-effects"));
        shoot(QStringLiteral("karaoke"), QStringLiteral("ne-mode"));
        shoot(QStringLiteral("karaoke"), QStringLiteral("ne-preview"), "videoPanel");
    }

    void karaokeKaraokeModeOnRayMv()
    {
        openKaraokeSong();
        application->audio().setKaraoke(true);
        QTest::qWait(500);
        shoot(QStringLiteral("karaoke"), QStringLiteral("karaoke-mode"));
        shoot(QStringLiteral("home"), QStringLiteral("karaoke"), "audioPanel");
        application->audio().setSpectrumOn(true);
        shoot(QStringLiteral("readme"), QStringLiteral("audio"));
    }

    void customizeKaraokeLayoutOnRayMv()
    {
        openKaraokeSong();
        applyLayout("Karaoke");
        QTest::qWait(500);
        shoot(QStringLiteral("customize"), QStringLiteral("karaoke-layout"));
    }

    void karaokeSplitAndJoinOnRayMv()
    {
        openKaraokeSong();
        application->audio().setKaraoke(true);
        application->audio().leadIn();
        QTest::qWait(500);
        shoot(QStringLiteral("karaoke"), QStringLiteral("lead-in"), "audioPanel");
    }

    void karaokeOnsetSnapOnRayMv()
    {
        openKaraokeSong();
        application->audio().setKaraoke(true);
        application->audio().setOnsetSnap(true);
        QTest::qWait(1500);
        shoot(QStringLiteral("karaoke"), QStringLiteral("onsets"), "audioPanel");
    }

    void karaokeFuriganaOnRayMv()
    {
        openOwnKaraokeLines("Dialogue: 0,0:01:14.00,0:01:18.00,Default,,0,0,0,karaoke,"
                            "{\\k40}夜|よ{\\k40}空|ぞら{\\k30}の{\\k60}星|ほし{\\k50}を{\\k70}見|み{\\k40}る\n");
        QVERIFY(loadTemplater());
        auto &templates = application->templateEditor();
        templates.setShown(true);
        const auto dumpOnFailure = dumpTemplateEditorOnFailure();
        QVERIFY(templates.insertPreset(kFuriganaRisePreset));
        templates.trustDocument();
        selectAt(kFuriganaFrame);
        QTRY_VERIFY_WITH_TIMEOUT(templates.videoPreview() && !templates.previewPending(), 20000);
        QTest::qWait(1500);
        shoot(QStringLiteral("karaoke"), QStringLiteral("furigana-video"), "videoPanel");
    }

    void karaokeTemplaterOnRayMv()
    {
        openOwnKaraokeLines("Dialogue: 0,0:01:14.00,0:01:18.00,Default,,0,0,0,karaoke,"
                            "{\\k40}ki{\\k35}ra{\\k45}ki{\\k30}ra {\\k60}hi{\\k50}ka{\\k40}ru\n");
        QVERIFY(loadTemplater());
        auto &templates = application->templateEditor();
        templates.setShown(true);
        const auto dumpOnFailure = dumpTemplateEditorOnFailure();
        QVERIFY(templates.insertPreset(kColourSweepPreset));
        templates.trustDocument();
        QTRY_VERIFY_WITH_TIMEOUT(templates.canApply(), 20000);
        QVERIFY(templates.apply(false));
        QTRY_VERIFY_WITH_TIMEOUT(!templates.lastApply().isEmpty(), 20000);
        selectAt(kKaraokeFrame);
        QTest::qWait(1500);
        shoot(QStringLiteral("karaoke"), QStringLiteral("templater-result"));
    }

    void homeProjects()
    {
        const QString project = home->filePath(QStringLiteral("Sprite Fright"));
        const QString film = project + QStringLiteral("/Sprite Fright.webm");
        QVERIFY(QDir().mkpath(project + QStringLiteral("/fonts")));
        QVERIFY(QFile::link(video, film));
        video = film;
        QVERIFY(writeAt(project + QStringLiteral("/.hikari/project.toml"), "version = 1\n"));
        QVERIFY(writeAt(project + QStringLiteral("/Sprite Fright_keyframes.txt"), "# keyframe format v1\nfps 0\n0\n"));
        QVERIFY(writeAt(project + QStringLiteral("/Sprite Fright signs.ass"),
                        signsScript("Dialogue: 0,0:00:59.50,0:01:00.50,Sign,,0,0,0,,{\\pos(1180,150)}Brambleden Woods\n")));
        QVERIFY(application->openFolder(project, 0).value(QStringLiteral("ok")).toBool());
        QVERIFY(openDialogue(project + QStringLiteral("/Sprite Fright.ass")));
        applyLayout("Editing");
        showVideoAt(kEditingFrame);
        resampleToTheVideo();
        giveDefaultAFilmSubtitleLook();
        selectAt(kEditingFrame);
        auto *dock = root()->findChild<QObject *>(QStringLiteral("explorerDock"));
        QVERIFY(dock);
        QVERIFY(QMetaObject::invokeMethod(root(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock))));
        QTRY_VERIFY(dock->property("isOpen").toBool());
        auto *explorer = window->findChild<ui::ExplorerModel *>();
        QVERIFY(explorer);
        QVERIFY(explorer->groupByName());
        const QString episode = project + QStringLiteral("/Sprite Fright.ass");
        QTRY_VERIFY(explorer->rowOf(episode) >= 0
                    && explorer->rowAt(explorer->rowOf(episode)).value(QStringLiteral("expandable")).toBool());
        explorer->setExpanded(explorer->rowOf(episode), true);
        QTRY_VERIFY(explorer->rowOf(project + QStringLiteral("/Sprite Fright signs.ass")) >= 0);
        QTest::mouseMove(window, QPoint(1, 1));
        shoot(QStringLiteral("home"), QStringLiteral("projects-window"));
    }

    void homeWindows()
    {
        openCommonsDialogue(kEditingFrame);
        const QString signs = writeFile(QStringLiteral("Sprite Fright signs.ass"),
                                        signsScript("Dialogue: 0,0:00:59.50,0:01:00.50,Sign,,0,0,0,,"
                                                    "{\\pos(1180,150)\\frz-4\\blur1}Brambleden Woods\n"));
        QVERIFY(application->openFile(signs));
        const int first = window->property("shellWindow").toInt();
        qulonglong signsDocument = 0;
        for (const QVariant &tab : application->windowTabs(first))
            if (tab.toMap().value(QStringLiteral("title")).toString().contains(QStringLiteral("signs")))
                signsDocument = tab.toMap().value(QStringLiteral("id")).toULongLong();
        QVERIFY(signsDocument != 0);
        QVERIFY(application->moveTabToNewWindow(signsDocument, {}, false));
        QQuickWindow *second = nullptr;
        QTRY_VERIFY((second = otherShellWindow()) && second->isExposed());
        second->resize(1600, 960);
        second->requestActivate();
        application->activateShellWindow(second->property("shellWindow").toInt());
        QTRY_COMPARE(application->editingDocument(), signsDocument);
        QVERIFY(QMetaObject::invokeMethod(second, "applyPreset", Q_ARG(QVariant, QStringLiteral("Typesetting"))));
        auto &controller = application->video();
        controller.openVideo(video);
        QTRY_VERIFY_WITH_TIMEOUT(controller.hasVideo(), 60000);
        keepMismatchedResolution();
        QVERIFY(controller.session().seekFrame(kSignFrame));
        QTRY_VERIFY_WITH_TIMEOUT(controller.session().shownFrame() == std::optional<int>(kSignFrame), 60000);
        QVERIFY(application->selectLinesVisibleOnVideo());
        QTRY_VERIFY_WITH_TIMEOUT(application->audio().ready(), 180000);
        for (const char *code : {"light", "dark"}) {
            setTheme(QString::fromLatin1(code));
            save(window->grabWindow(), QStringLiteral("home"), QStringLiteral("windows-dialogue-") + QLatin1String(code));
            save(second->grabWindow(), QStringLiteral("home"), QStringLiteral("windows-signs-") + QLatin1String(code));
        }
    }

    void homeMotionTracking()
    {
        const QString file = writeFile(QStringLiteral("Sprite Fright signs.ass"),
                                       signsScript("Dialogue: 0,0:02:16.25,0:02:17.75,Sign,,0,0,0,,"
                                                   "{\\an2\\pos(1050,175)\\bord5\\blur1}Sporeling\n"));
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Typesetting");
        showVideoAt(kMushroomFrame);
        QString data = QStringLiteral("Adobe After Effects 6.0 Keyframe Data\n\n\tUnits Per Second\t24\n\tSource Width\t2048\n"
                                      "\tSource Height\t858\n\nPosition\n\tFrame\tX pixels\tY pixels\tZ pixels\n");
        int frame = 0;
        for (const auto &[x, y] : kMushroomCap)
            data += QStringLiteral("\t%1\t%2\t%3\t0\n").arg(frame++).arg(x).arg(y);
        data += QStringLiteral("\nScale\n\tFrame\tX percent\tY percent\tZ percent\n");
        for (int i = 0; i < frame; ++i)
            data += QStringLiteral("\t%1\t100\t100\t100\n").arg(i);
        data += QStringLiteral("\nRotation\n\tFrame\tDegrees\n");
        for (int i = 0; i < frame; ++i)
            data += QStringLiteral("\t%1\t0\n").arg(i);
        data += QStringLiteral("\nEnd of Keyframe Data\n");
        const QVariantMap options{
            {QStringLiteral("x"), true}, {QStringLiteral("y"), true}, {QStringLiteral("absolute"), false},
            {QStringLiteral("scale"), false}, {QStringLiteral("rotation"), false}, {QStringLiteral("origin"), false},
            {QStringLiteral("border"), true}, {QStringLiteral("shadow"), true}, {QStringLiteral("blur"), true},
            {QStringLiteral("blurPercent"), 100}, {QStringLiteral("rectClips"), true}, {QStringLiteral("vectorClips"), true},
            {QStringLiteral("rectToVector"), false}, {QStringLiteral("clipsOnly"), false}, {QStringLiteral("clipData"), false},
            {QStringLiteral("bake"), true}, {QStringLiteral("linear"), false}, {QStringLiteral("extrapolate"), false},
            {QStringLiteral("relativeFrame"), 1}, {QStringLiteral("dataStart"), 0}};
        QCOMPARE(application->applyMotionData(data, {}, options), QString());
        const int middle = kMushroomFrame + int(std::size(kMushroomCap)) * 2 / 3;
        auto &controller = application->video();
        QVERIFY(controller.session().seekFrame(middle));
        QTRY_VERIFY_WITH_TIMEOUT(controller.session().shownFrame() == std::optional<int>(middle), 60000);
        QVERIFY(application->selectLinesVisibleOnVideo());
        QTest::mouseMove(window, QPoint(1, 1));
        shoot(QStringLiteral("home"), QStringLiteral("motion-tracking"), "videoPanel");
    }

    void homeChecking()
    {
        const QString file = writeFile(QStringLiteral("Sprite Fright.ass"), signsScript(
            "Dialogue: 0,0:01:10.00,0:01:11.50,Default,,0,0,0,,Finally, the tent are up!\n"
            "Dialogue: 0,0:01:11.50,0:01:13.00,Default,,0,0,0,,Their going to love this spot.\n"
            "Dialogue: 0,0:01:13.00,0:01:14.50,Default,,0,0,0,,I has a idea for the fire.\n"
            "Dialogue: 0,0:01:14.50,0:01:16.00,Default,,0,0,0,,We should of brought more marshmallows.\n"
            "Dialogue: 0,0:01:16.00,0:01:17.50,Default,,0,0,0,,Look at that mushrom!\n"
            "Dialogue: 0,0:01:17.50,0:01:19.00,Default,,0,0,0,,It dont look friendly.\n"
            "Dialogue: 0,0:01:19.00,0:01:20.50,Default,,0,0,0,,Everyone, stay close together.\n"
            "Dialogue: 0,0:01:20.50,0:01:22.00,Default,,0,0,0,,This is fine.\n"));
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Editing");
        showVideoAt(kEditingFrame);
        application->settingsStore()->setValue(QStringLiteral("grammar.language"), 1);
        auto *dock = root()->findChild<QObject *>(QStringLiteral("spellDock"));
        QVERIFY(dock);
        QVERIFY(QMetaObject::invokeMethod(root(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock))));
        QQuickItem *panel = nullptr;
        QTRY_VERIFY((panel = item("spellCheckerPanel")));
        QVERIFY(QMetaObject::invokeMethod(panel, "setMode", Q_ARG(QVariant, QStringLiteral("grammar"))));
        QVERIFY(QMetaObject::invokeMethod(panel, "start"));
        QTRY_VERIFY_WITH_TIMEOUT(application->grammarWalkState(0).value(QStringLiteral("done")).toBool(), kGrammarWait);
        QTRY_VERIFY(item("grammarMessage") && item("grammarMessage")->isVisible());
        QTest::mouseMove(window, QPoint(1, 1));
        shoot(QStringLiteral("home"), QStringLiteral("checking-window"));
    }

    void homePlugins()
    {
        if (!marketplace || marketplace->files.isEmpty())
            QSKIP("HIKARI_SHOT_MARKETPLACE is not set to a copy of the marketplace");
        app::attachMarketplaceImages(*engine, *application->pluginMarketplace());
        application->pluginMarketplace()->start();
        QTRY_COMPARE(application->pluginMarketplace()->status(), QStringLiteral("ready"));
        auto *dialog = root()->findChild<QObject *>(QStringLiteral("pluginManagerDialog"));
        auto *manager = qobject_cast<QQuickWindow *>(dialog);
        QVERIFY(manager);
        QVERIFY(QMetaObject::invokeMethod(dialog, "show"));
        QTRY_VERIFY(manager->isVisible());
        QVERIFY(QMetaObject::invokeMethod(dialog, "showView", Q_ARG(QVariant, QStringLiteral("market"))));
        dialog->setProperty("chosenListingId", QStringLiteral("hikarisub.macro-chains"));
        QTRY_VERIFY(item("marketDetailsOfficial") && item("marketDetailsOfficial")->isVisible());
        QTRY_VERIFY(item("marketIcon") && item("marketIcon")->property("shown").toBool());
        QTest::qWait(400);
        shoot(QStringLiteral("home"), QStringLiteral("plugins-window"), nullptr, manager);
    }

    void homeAiReview()
    {
        const QString file = writeFile(QStringLiteral("Sprite Fright.ass"), signsScript(
            "Dialogue: 0,0:01:10.00,0:01:11.50,Default,,0,0,0,,Right, everyone. Tents up before it gets dark.\n"
            "Dialogue: 0,0:01:11.50,0:01:13.00,Default,,0,0,0,,Who packed the marshmallows?\n"
            "Dialogue: 0,0:01:13.00,0:01:14.50,Default,,0,0,0,,Well, I did think that somebody would probably want the marshmallows.\n"
            "Dialogue: 0,0:01:14.50,0:01:17.00,Default,,0,0,0,,Okay, so what we are basically going to do now is go and find some firewood.\n"
            "Dialogue: 0,0:01:17.00,0:01:18.50,Default,,0,0,0,,Stay on the path, all of you, because this forest is very easy to get lost in.\n"
            "Dialogue: 0,0:01:18.50,0:01:20.00,Default,,0,0,0,,What was that noise?\n"
            "Dialogue: 0,0:01:20.00,0:01:21.50,Default,,0,0,0,,Probably just a bird.\n"
            "Dialogue: 0,0:01:21.50,0:01:23.00,Default,,0,0,0,,Probably.\n"));
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Editing");
        showVideoAt(kEditingFrame);

        const auto &session = *application->files().session(*application->workspace().editingTarget());
        const auto &lines = session.document().lines();
        const QString prompt = QStringLiteral("Shorten the long lines around 1:15 so they read at a comfortable speed.");
        const QString edit = QStringLiteral(R"({"op": "set_line", "line": %1, "fields": {"text": "%2"}})");
        const QString replies =
            QStringLiteral(R"({"%1": {"say": "%2", "calls": [{"operation": "document.apply", "params": {"revision": %3,
                "name": "Shorter lines", "edits": [%4, %5, %6]}}]}})")
                .arg(prompt, QStringLiteral("I shortened lines 3, 4 and 5 so each reads under 15 characters per second."))
                .arg(session.revision())
                .arg(edit.arg(lines.at(2)->id.value).arg(QStringLiteral("I thought someone would want them.")),
                     edit.arg(lines.at(3)->id.value).arg(QStringLiteral("Let's go find some firewood.")),
                     edit.arg(lines.at(4)->id.value).arg(QStringLiteral("Stay on the path. It's easy to get lost.")));
        QVERIFY(!writeFile(QStringLiteral("replies.json"), replies.toUtf8()).isEmpty());

        application->settingsStore()->set(app::AgentController::kModeSetting, QStringLiteral("manual"));
        QVERIFY(evaluate(QStringLiteral("newAiThreadAction.trigger(), agentDock.isOpen")).toBool());
        QTRY_VERIFY(item("agentView"));
        QTRY_VERIFY_WITH_TIMEOUT(application->agent()->ready(), 20000);
        QQuickItem *composer = item("agentComposer");
        QVERIFY(composer);
        composer->setProperty("text", prompt);
        composer->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Return);
        const auto dumpOnFailure = qScopeGuard([this] {
            if (!QTest::currentTestFailed())
                return;
            if (QFile f(out + QStringLiteral("/ai-review-thread.md")); f.open(QIODevice::WriteOnly))
                f.write(application->agent()->markdown(application->agent()->current()).toUtf8());
            window->grabWindow().save(out + QStringLiteral("/ai-review-failed.png"));
        });
        auto *review = application->review();
        QQuickItem *apply = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((apply = item("agentProposalApply")), 20000);
        QVERIFY(QMetaObject::invokeMethod(apply, "click"));
        QTRY_COMPARE(review->waiting(), 3);
        QTRY_VERIFY(!application->agent()->thread().value(QStringLiteral("running")).toBool());
        application->agent()->rename(application->agent()->current(), QStringLiteral("Shorten the long lines"));

        const qulonglong change = review->turns().front().toMap().value(QStringLiteral("changes")).toList().front()
                                      .toMap().value(QStringLiteral("change")).toULongLong();
        review->keep(change, 0);
        QVERIFY(review->undo(change, 1, false));
        QCOMPARE(review->waiting(), 1);
        QCOMPARE(session.document().lines().at(2)->text, u8"I thought someone would want them.");
        QCOMPARE(session.document().lines().at(3)->text, u8"Okay, so what we are basically going to do now is go and find some firewood.");

        window->resize(1920, 1080);
        QTest::qWait(500);
        evaluate(QStringLiteral("root.moveToSideBar(reviewDock, true), root.moveToSideBar(agentDock, false)"));
        evaluate(QStringLiteral("root.showPanel(agentDock), root.showPanel(reviewDock)"));
        QTRY_VERIFY(evaluate(QStringLiteral("agentPanel.visible && reviewPanel.visible")).toBool());
        QVERIFY(evaluate(QStringLiteral("root.sideBarOf(agentDock) !== root.sideBarOf(reviewDock)")).toBool());
        selectAt(kEditingFrame);
        QTest::qWait(1500);
        QTest::mouseMove(window, QPoint(1, 1));
        shoot(QStringLiteral("home"), QStringLiteral("ai-review-window"));
    }

    void pluginsAutomationMenu()
    {
        openCommonsDialogue(kEditingFrame);
        seekTo(kBetweenLinesFrame);
        QTest::mouseMove(window, QPoint(1, 1));
        shootOpen(QStringLiteral("plugins"), QStringLiteral("automation-menu"), [this](QList<QObject *> &shown) {
            popupFromMenuBar("automationMenu");
            QObject *lineTools = nullptr;
            QTRY_VERIFY((lineTools = named("pluginCommandMenu_Line tools")));
            openMenu(lineTools);
            shown << named("automationMenu") << lineTools;
        });
    }

    void homeAutomation()
    {
        openCommonsDialogue(kEditingFrame);
        const QString script = writeFile(QStringLiteral("sign-helpers.lua"), kSignHelpers);
        QVERIFY(!script.isEmpty());
        auto &automation = application->automation();
        automation.load(script.toStdString());
        QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(automation.scripts(), [&](const auto &s) {
            return s.path == script.toStdString() && s.state == hikari::application::ScriptStatus::State::Ready;
        }), 30000);
        QSignalSpy ran(&automation, &app::AutomationShell::runCompleted);
        QVERIFY(automation.run(script.toStdString(), 0));
        QVERIFY(ran.wait(30000));
        QVERIFY(ran.first().first().toBool());
        QTRY_VERIFY(automation.canRepeat());
        seekTo(kBetweenLinesFrame);
        QTest::mouseMove(window, QPoint(1, 1));
        shootOpen(QStringLiteral("home"), QStringLiteral("automation"), [this](QList<QObject *> &shown) {
            popupFromMenuBar("automationMenu");
            shown << named("automationMenu");
        });
    }

    void basicsExplorerMenuWithoutFilm()
    {
        const QString project = home->filePath(QStringLiteral("Sprite Fright"));
        QVERIFY(QDir().mkpath(project + QStringLiteral("/fonts")));
        QVERIFY(writeAt(project + QStringLiteral("/.hikari/project.toml"), "version = 1\n"));
        QVERIFY(writeAt(project + QStringLiteral("/Sprite Fright_keyframes.txt"), "# keyframe format v1\nfps 0\n0\n"));
        QVERIFY(writeAt(project + QStringLiteral("/Sprite Fright.ass"),
                        signsScript("Dialogue: 0,0:01:14.42,0:01:17.50,Default,,0,0,0,,Finally, the tent is up!\n")));
        QVERIFY(writeAt(project + QStringLiteral("/Sprite Fright signs.ass"),
                        signsScript("Dialogue: 0,0:00:59.50,0:01:00.50,Sign,,0,0,0,,{\\pos(1180,150)}Brambleden Woods\n")));
        QVERIFY(application->openFolder(project, 0).value(QStringLiteral("ok")).toBool());
        QVERIFY(application->openFile(project + QStringLiteral("/Sprite Fright.ass")));
        applyLayout("Editing");
        auto *dock = named("explorerDock");
        QVERIFY(dock);
        QVERIFY(QMetaObject::invokeMethod(root(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock))));
        QTRY_VERIFY(dock->property("isOpen").toBool());
        auto *explorer = window->findChild<ui::ExplorerModel *>();
        QVERIFY(explorer);
        const QString episode = project + QStringLiteral("/Sprite Fright.ass");
        QQuickItem *row = nullptr;
        QTRY_VERIFY(explorer->rowOf(episode) >= 0 && (row = item("explorerRow_Sprite Fright.ass")));
        QTest::mouseMove(window, QPoint(1, 1));
        shootOpen(QStringLiteral("basics"), QStringLiteral("explorer-menu"), [&](QList<QObject *> &shown) {
            QVERIFY(QMetaObject::invokeMethod(item("explorerTool"), "showMenu", Q_ARG(QVariant, explorer->rowOf(episode)),
                                              Q_ARG(QVariant, QVariant::fromValue(row)), Q_ARG(QVariant, row->width() * 3 / 4),
                                              Q_ARG(QVariant, row->height() / 2)));
            QTRY_VERIFY(named("explorerRowMenu")->property("opened").toBool());
            shown << named("explorerRowMenu");
        });
    }

    void basicsGridMenu()
    {
        openCommonsDialogue(kEditingFrame);
        application->extendSelection(1);
        QTest::mouseMove(window, QPoint(1, 1));
        shootOpen(QStringLiteral("basics"), QStringLiteral("grid-menu"), [this](QList<QObject *> &shown) {
            grid()->requestMenu();
            QTRY_VERIFY(named("gridMenu")->property("opened").toBool());
            shown << named("gridMenu");
        });
    }

    void basicsLineGroups()
    {
        openCommonsDialogue(kEditingFrame);
        const int first = rowOfLine(grid()->activeLine()->value) - 4;
        QVERIFY(first >= 0);
        const qulonglong campsite = lineIdAt(first), fire = lineIdAt(first + 2), after = lineIdAt(first + 7);
        const qulonglong inner = groupLines(fire, 2, QStringLiteral("The barbecue"));
        QVERIFY(inner);
        const qulonglong outer = groupLines(campsite, 5, QStringLiteral("At the campsite"));
        QVERIFY(outer);
        QVERIFY(application->toggleGroup(outer));
        QVERIFY(application->toggleGroup(inner));
        QVERIFY(groupLines(after, 2, QStringLiteral("Into the woods")));
        application->selectLine(outer);
        grid()->makeLineVisible(outer);
        grid()->scrollToTopRow(std::max(0, rowOfLine(outer) - 2));
        QTest::mouseMove(window, QPoint(1, 1));
        shootOpen(QStringLiteral("basics"), QStringLiteral("line-groups"), [this](QList<QObject *> &shown) {
            grid()->requestMenu();
            QTRY_VERIFY(named("groupMenu")->property("opened").toBool());
            shown << named("groupMenu");
        });
    }

    void basicsVideoMenu()
    {
        openCommonsDialogue(kEditingFrame);
        seekTo(kBetweenLinesFrame);
        shootOpen(QStringLiteral("basics"), QStringLiteral("video-menu"), [this](QList<QObject *> &shown) {
            auto *menu = named("videoContextMenu");
            QVERIFY(QMetaObject::invokeMethod(menu, "openAt", Q_ARG(QVariant, QPointF(160, 60))));
            QTRY_VERIFY(menu->property("opened").toBool());
            shown << menu;
        });
    }

    void basicsPhantom()
    {
        openCommonsDialogue(kEditingFrame);
        QQuickItem *lineText = item("lineText");
        QVERIFY(lineText);
        QTRY_VERIFY(!lineText->property("text").toString().isEmpty());
        shootOpen(QStringLiteral("basics"), QStringLiteral("phantom"), [&](QList<QObject *> &shown) {
            const int length = int(lineText->property("text").toString().size());
            QVERIFY(QMetaObject::invokeMethod(lineText, "select", Q_ARG(int, 0), Q_ARG(int, std::min(length, 8))));
            lineText->forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(lineText->hasActiveFocus());
            QTest::keyClick(window, Qt::Key_Menu);
            QObject *menu = lineText->findChild<QObject *>(QStringLiteral("lineTextMenu"));
            QTRY_VERIFY(menu && menu->property("opened").toBool());
            QObject *phantom = named("lineTextPhantomMenu");
            openMenu(phantom);
            shown << menu << phantom;
        });
    }

    void basicsStyleCatalogueWithoutFilm()
    {
        auto &styles = application->styleManager();
        for (const char *name : {"Film subtitles", "Signs", "Sprite Fright"})
            QVERIFY(styles.createCatalog(QString::fromLatin1(name), QStringLiteral("mine")));
        QVERIFY(styles.chooseCatalog(QStringLiteral("Sprite Fright"), QStringLiteral("mine")));
        QVERIFY(openDialogue());
        app::attachStylePreview(*engine, styles);
        QVERIFY(QMetaObject::invokeMethod(root(), "buildMenus"));
        QVERIFY(QMetaObject::invokeMethod(named("styleManagerMenuItem")->property("action").value<QObject *>(), "trigger"));
        auto *manager = qobject_cast<QQuickWindow *>(named("styleManager"));
        QVERIFY(manager);
        QTRY_VERIFY(manager->isVisible());
        QVERIFY(QTest::qWaitForWindowExposed(manager));
        auto *box = findItem(manager->contentItem(), QStringLiteral("styleCatalog"));
        QVERIFY(box);
        shootOpen(QStringLiteral("basics"), QStringLiteral("style-catalogue"), [&](QList<QObject *> &shown) {
            QObject *popup = popupOf(box);
            openMenu(popup);
            shown << popup;
        }, nullptr, manager);
    }

    void basicsTimingHelpersWithoutFilm()
    {
        const QString file = writeFile(QStringLiteral("Sprite Fright.ass"), signsScript(
            "Dialogue: 0,0:01:10.00,0:01:11.50,Default,,0,0,0,,This clearing looks perfect.\n"
            "Dialogue: 0,0:01:11.50,0:01:14.42,Default,,0,0,0,,Who brought the tent poles?\n"
            "Dialogue: 0,0:01:14.42,0:01:17.50,Default,,0,0,0,,Finally, the tent is up!\n"
            "Dialogue: 0,0:01:18.42,0:01:20.17,Default,,0,0,0,,Did anyone else hear that?\n"));
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Editing");
        QTest::mouseMove(window, QPoint(1, 1));
        shootOpen(QStringLiteral("basics"), QStringLiteral("timing-helpers"), [this](QList<QObject *> &shown) {
            popupFromMenuBar("subtitlesMenu");
            QObject *subtitles = named("subtitlesMenu");
            QObject *helpers = named("timingHelpersMenu");
            openMenu(helpers);
            shown << subtitles << helpers;
        });
    }

    void checkingSpellingMenuWithoutFilm()
    {
        const QString file = writeFile(QStringLiteral("Sprite Fright.ass"),
                                       signsScript("Dialogue: 0,0:01:14.42,0:01:17.50,Default,,0,0,0,,Finally, the tnet is up!\n"));
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Editing");
        QQuickItem *lineText = item("lineText");
        QVERIFY(lineText);
        QTRY_COMPARE(lineText->property("text").toString(), QStringLiteral("Finally, the tnet is up!"));
        QObject *menu = lineText->findChild<QObject *>(QStringLiteral("lineTextMenu"));
        QVERIFY(menu);
        shootOpen(QStringLiteral("checking"), QStringLiteral("spelling-menu"), [&](QList<QObject *> &shown) {
            QRectF at;
            QVERIFY(QMetaObject::invokeMethod(lineText, "positionToRectangle", Q_RETURN_ARG(QRectF, at), Q_ARG(int, 15)));
            const QPoint word = lineText->mapToScene(QPointF(at.x() + 2, at.center().y())).toPoint();
            QTest::mouseMove(window, word);
            QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, word);
            QTRY_VERIFY(menu->property("opened").toBool());
            QQuickItem *first = nullptr;
            QVERIFY(QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, first), Q_ARG(int, 0)));
            QVERIFY(first);
            QCOMPARE(first->property("text").toString(), QStringLiteral("tent"));
            shown << menu;
        });
    }

    void karaokeTemplateLanguageWithoutFilm()
    {
        const QString file = writeFile(QStringLiteral("Our karaoke.ass"),
            QByteArrayLiteral("[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
                              "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                              "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, "
                              "Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                              "Style: Default,Noto Sans,80,&H00FFFFFF,&H00FF8C3C,&H00301018,&H80000000,-1,0,0,0,100,100,"
                              "0,0,1,3,0,8,10,10,80,1\n\n"
                              "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                              "Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,template syl,"
                              "{\\an5\\pos($scenter,$smiddle)\\t($start,$end,\\fscx120\\fscy120)}\n"
                              "Dialogue: 0,0:01:14.00,0:01:18.00,Default,,0,0,0,karaoke,"
                              "{\\k40}ki{\\k35}ra{\\k45}ki{\\k30}ra {\\k60}hi{\\k50}ka{\\k40}ru\n"));
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Editing");
        QVERIFY(loadTemplater());
        auto *dock = named("templateDock");
        QVERIFY(dock);
        QVERIFY(QMetaObject::invokeMethod(root(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock))));
        QTRY_VERIFY(dock->property("isOpen").toBool());
        QQuickItem *mode = nullptr;
        QTRY_VERIFY((mode = item("templateMode")) && mode->isVisible() && mode->isEnabled());
        QTest::mouseMove(window, QPoint(1, 1));
        shootOpen(QStringLiteral("karaoke"), QStringLiteral("template-language"), [&](QList<QObject *> &shown) {
            QObject *popup = popupOf(mode);
            openMenu(popup);
            shown << popup;
        });
    }

    void typesettingPerspectiveOrigin()
    {
        const QString file = writeFile(QStringLiteral("Sprite Fright signs.ass"),
                                       signsScript("Dialogue: 0,0:00:59.50,0:01:00.50,Sign,,0,0,0,,"
                                                   "{\\an5\\pos(1180,150)\\org(1180,150)\\frx14\\fry-22\\frz-4\\blur1}"
                                                   "Brambleden Woods\n"));
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Typesetting");
        showVideoAt(kSignFrame);
        application->visualTools().selectFamily(kPerspectiveTool);
        QTRY_VERIFY(item("visualOption_plane"));
        QTest::mouseMove(window, QPoint(1, 1));
        shootOpen(QStringLiteral("typesetting"), QStringLiteral("perspective-origin"), [this](QList<QObject *> &shown) {
            QQuickItem *origin = nullptr;
            QTRY_VERIFY((origin = findItem(window->contentItem(), QStringLiteral("visualOption_orgMode"))) && origin->isVisible());
            QObject *popup = popupOf(origin);
            openMenu(popup);
            shown << popup;
        }, "videoPanel");
    }

    void aiAiMenu()
    {
        application->settingsStore()->setValue(QString::fromLatin1(app::AiFeatures::kOffSetting), false);
        QTRY_VERIFY(application->inlineAi()->on());
        openCommonsDialogue(kEditingFrame);
        application->extendSelection(1);
        QTRY_VERIFY_WITH_TIMEOUT(application->inlineAi()->ready().value(QStringLiteral("chat")).toBool(), 20000);
        QTest::mouseMove(window, QPoint(1, 1));
        shootOpen(QStringLiteral("ai"), QStringLiteral("ai-menu"), [this](QList<QObject *> &shown) {
            grid()->requestMenu();
            QObject *menu = named("gridMenu");
            QTRY_VERIFY(menu->property("opened").toBool());
            QObject *ai = named("gridAiMenu");
            openMenu(ai);
            shown << menu << ai;
        });
    }
};

QTEST_MAIN(ReleaseScreenshotsTests)
#include "release_screenshots_tests.moc"
