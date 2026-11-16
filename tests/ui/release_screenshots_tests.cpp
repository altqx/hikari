#include "hikari/app/agent_controller.h"
#include "hikari/app/ai_features.h"
#include "hikari/app/application.h"
#include "hikari/app/review_controller.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/application/ai_providers.h"
#include "hikari/application/automation_registry.h"
#include "hikari/backends/ai_agent_broker.h"
#include "hikari/backends/ai_sidecar_host.h"
#include "audio_controller.h"
#include "docking.h"
#include "explorer_model.h"
#include "line_grid.h"
#include "node_effects_controller.h"
#include "settings_store.h"
#include "template_editor_controller.h"
#include "theme.h"
#include "video_controller.h"
#include "visual_tools_controller.h"
#include "workspace_layout.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QQmlExpression>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
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
    static constexpr int kMushroomFrame = 3270;
    static constexpr int kNodeEffectsSourceFrame = 165;
    static constexpr int kFallingLettersPreset = 8;
    static constexpr int kGrammarWait = 60000;
    static constexpr std::pair<double, double> kMushroomCap[] = {{1050, 300}, {1068, 294}, {1068, 294}, {1087, 298}, {1087, 298}, {1123, 307}, {1123, 307}, {1149, 312}, {1149, 312}, {1165, 317}, {1165, 317}, {1173, 321}, {1173, 321}, {1175, 322}, {1175, 322}, {1175, 322}, {1175, 322}, {1173, 321}, {1173, 321}, {1171, 321}, {1171, 321}, {1165, 323}, {1165, 323}, {1149, 322}, {1149, 322}, {1135, 322}, {1135, 322}, {1122, 314}, {1122, 314}, {1114, 307}, {1114, 307}, {1103, 303}, {1103, 303}, {1081, 311}, {1081, 311}, {1054, 321}};
    static constexpr int kKaraokeRow = 34;
    static constexpr int kColourSweepPreset = 2;
    static constexpr int kFuriganaRisePreset = 14;

    std::unique_ptr<QTemporaryDir> home;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QString out;
    QString video;
    QString song;

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
        if (QTest::currentTestFunction() == QByteArrayLiteral("homeAiReview"))
            o.aiPorts = standInAi();
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
            if (video.isEmpty())
                QSKIP("HIKARI_SHOT_VIDEO is not set");
        }
        QVERIFY(QFileInfo(video).isFile());
        QVERIFY(QDir().mkpath(out));
        start();
    }

    void cleanup()
    {
        delete engine;
        engine = nullptr;
        delete application;
        application = nullptr;
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
        QVERIFY(templates.insertPreset(kFuriganaRisePreset));
        QTRY_VERIFY_WITH_TIMEOUT(templates.videoPreview() && !templates.previewPending(), 20000);
        selectAt(kKaraokeFrame);
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
        QVERIFY(templates.insertPreset(kColourSweepPreset));
        templates.trustDocument();
        QVERIFY(templates.apply(false));
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
};

QTEST_MAIN(ReleaseScreenshotsTests)
#include "release_screenshots_tests.moc"
