// Wave 6's Line editor cards through the composition, with a settings file
// so that a restart reads what the last run stored:
// - LE2 (#227): the Times/Frames switch (EDITBOX_TIMES_TO_FRAMES_SWITCH) out
//   of the Line editor, into Settings > Editor and the Video panel's menu.

#include "hikari/app/application.h"

#include "docking.h"
#include "line_editor_controller.h"
#include "settings_store.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace hikari;

namespace {

QString writeFile(const QTemporaryDir &dir, const char *name, const char *events)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 480\n\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
            "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
            "MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
            "Style: Sign,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"
            "\n[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    f.write(events);
    return path;
}

} // namespace

class LineEditorOptionsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString tagged;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;

    QString settingsFile() const { return dir.filePath(QStringLiteral("hikari.ini")); }
    void start()
    {
        // No spell checker: its missing-dictionary notice is not these cards'.
        QSettings(settingsFile(), QSettings::IniFormat).setValue(QStringLiteral("profile/editor.spellchecker"), false);
        app::Application::Options options;
        options.settingsFile = settingsFile();
        options.playbackAudio = false; // CI runners without an audio device
        application = std::make_unique<app::Application>(options);
        auto t = std::make_shared<qint64>(0);
        application->hotkeys().setKeyClock([t] { return *t += 1000; });
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    void stop()
    {
        engine.reset();
        application.reset();
        window = nullptr;
    }
    void restart()
    {
        stop();
        start();
    }
    QObject *root() const { return engine->rootObjects().first(); }
    QObject *named(const char *name) const
    {
        if (auto *found = root()->findChild<QObject *>(QLatin1String(name)))
            return found;
        for (QObject *o : root()->findChildren<QObject *>())
            if (o->objectName() == QLatin1String(name))
                return o;
        return nullptr;
    }
    // Repeater delegates are not QObject children of the window: walk the items.
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    // The items shown first: a Repeater's old delegates stay QObject
    // children until they are deleted.
    QQuickItem *item(const char *name) const
    {
        if (auto *found = findItem(window->contentItem(), QLatin1String(name)))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *q = qobject_cast<QQuickWindow *>(w))
                if (auto *found = findItem(q->contentItem(), QLatin1String(name)))
                    return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *found = w->findChild<QQuickItem *>(QLatin1String(name)))
                return found;
        return nullptr;
    }
    bool showLine(const QString &file)
    {
        if (!application->openFile(file))
            return false;
        item("editingGrid")->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Home);
        return QTest::qWaitFor([&] { return application->editor().hasLine(); });
    }
    // Settings: File > Settings, then the dialog's check box for `setting`
    // clicked and OK.
    void clickSettingAndOk(const char *setting, bool expectedBefore)
    {
        auto *action = named("settingsMenuItem")->property("action").value<QObject *>();
        QVERIFY(action && QMetaObject::invokeMethod(action, "trigger"));
        auto *dialog = named("settingsDialog");
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *content = dialog->property("contentItem").value<QQuickItem *>();
        auto *check = findItem(content, QStringLiteral("setting_") + QLatin1String(setting));
        QVERIFY(check);
        QCOMPARE(check->property("checked").toBool(), expectedBefore);
        QVERIFY(QMetaObject::invokeMethod(check, "click"));
        auto *footer = dialog->property("footer").value<QQuickItem *>();
        auto *ok = findItem(footer, QStringLiteral("settingsOk"));
        QVERIFY(ok && QMetaObject::invokeMethod(ok, "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        tagged = writeFile(dir, "tagged.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\i1}Hi{\\i0} there\n");
    }
    void init()
    {
        QFile::remove(settingsFile());
        start();
    }
    void cleanup() { stop(); }

    // LE2: the counters row keeps Wraps and characters per second only; the
    // switch is stored and read back at the next start, where the Video
    // panel's menu (once a video is open) and Settings show it.
    void framesSwitchLeftTheEditorAndPersists()
    {
        QVERIFY(showLine(tagged));
        QVERIFY(!item("showTimes"));
        QVERIFY(!item("showFrames"));
        QVERIFY(item("charsCounter"));
        QVERIFY(item("cpsCounter"));
        QVERIFY(!application->editor().showFrames());
        application->editor().setShowFrames(true);
        QVERIFY(application->settingsStore()->boolean("editor.timesToFramesSwitch"));
        restart();
        QVERIFY(application->editor().showFrames());
        // Settings > Editor shows the stored choice and turns it off.
        clickSettingAndOk("editor.timesToFramesSwitch", true);
        QVERIFY(!application->editor().showFrames());
        restart();
        QVERIFY(!application->editor().showFrames());
    }
};

QTEST_MAIN(LineEditorOptionsTest)
#include "line_editor_options_tests.moc"
