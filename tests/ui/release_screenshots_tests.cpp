#include "hikari/app/application.h"
#include "audio_controller.h"
#include "docking.h"
#include "line_grid.h"
#include "settings_store.h"
#include "theme.h"
#include "video_controller.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
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

    void shootThemes(const QString &name)
    {
        auto &settings = *application->settingsStore();
        for (const char *code : {"light", "dark"}) {
            settings.setValue(QStringLiteral("appearance.theme"), QString::fromLatin1(code));
            auto *controls = root()->property("palette").value<QObject *>();
            QTRY_COMPARE(controls->property("window").value<QColor>(), ui::theme::current().roles.panel);
            QTest::qWait(300);
            QVERIFY(window->grabWindow().save(out + QLatin1Char('/') + name + QLatin1Char('-') + QLatin1String(code)
                                              + QStringLiteral(".png")));
        }
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

    void editingLayoutWithDialogue()
    {
        QVERIFY(openDialogue());
        applyLayout("Editing");
        showVideoAt(kEditingFrame);
        shootThemes(QStringLiteral("editing"));
    }

    void typesettingLayoutWithOwnSign()
    {
        const QString file = writeFile(QStringLiteral("Sprite Fright signs.ass"),
                                       signsScript("Dialogue: 0,0:00:59.50,0:01:00.50,Sign,,0,0,0,,"
                                                   "{\\pos(1180,150)\\frz-4\\blur1}Brambleden Woods\n"));
        QVERIFY(!file.isEmpty());
        QVERIFY(application->openFile(file));
        applyLayout("Typesetting");
        showVideoAt(kSignFrame);
        shootThemes(QStringLiteral("typesetting"));
    }
};

QTEST_MAIN(ReleaseScreenshotsTests)
#include "release_screenshots_tests.moc"
