#include "hikari/app/application.h"
#include "hikari/app/style_manager_controller.h"
#include "audio_controller.h"
#include "docking.h"
#include "line_grid.h"
#include "settings_store.h"
#include "theme.h"
#include "video_controller.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

class ReleaseScreenshotsTests : public QObject {
    Q_OBJECT

    static constexpr int kEditingFrame = 1812;
    static constexpr int kSignFrame = 1440;
    static constexpr int kTopRowOfTheGridRange = 7;
    static constexpr int kItalicLineFrame = 1550;

    std::unique_ptr<QTemporaryDir> home;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QString out;
    QString video;

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
        application = new app::Application(o);
        application->settingsStore()->setValue(QStringLiteral("appearance.followSystem"), false);
        application->setAskForBadResolution(false);
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

    bool openDialogue()
    {
        const QString commons = qEnvironmentVariable("HIKARI_SHOT_SUBTITLES");
        if (!commons.isEmpty())
            return application->openFile(commons)
                && application->saveChosenFor(application->editingDocument(),
                                              QUrl::fromLocalFile(home->filePath(QStringLiteral("Sprite Fright.ass"))),
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
        video = qEnvironmentVariable("HIKARI_SHOT_VIDEO");
        if (out.isEmpty() || video.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR and HIKARI_SHOT_VIDEO are not both set");
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
};

QTEST_MAIN(ReleaseScreenshotsTests)
#include "release_screenshots_tests.moc"
