#include "hikari/app/application.h"
#include "docking.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>
#include <deque>
#include <functional>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

const QString kScript = QStringLiteral(HIKARI_LUA_FIXTURES "/repeat-dialogs.lua");

QByteArray document()
{
    return "[Script Info]\nTitle: ad2\nScriptType: v4.00+\n\n[V4+ Styles]\n"
           "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
           "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
           "Alignment, MarginL, MarginR, MarginV, Encoding\n"
           "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
           "10,10,10,1\n\n"
           "[Events]\n"
           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,One\n"
           "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Two\n"
           "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,Three\n"
           "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,Four\n";
}

struct Shown {
    QVariantList controls;
    QString notice;
    QVariant value(const QString &name) const
    {
        for (const QVariant &c : controls) {
            const QVariantMap m = c.toMap();
            if (m.value(QStringLiteral("name")).toString() != name)
                continue;
            const QString kind = m.value(QStringLiteral("kind")).toString();
            return kind == QLatin1String("intedit")    ? m.value(QStringLiteral("intValue"))
                   : kind == QLatin1String("checkbox") ? m.value(QStringLiteral("checked"))
                                                       : m.value(QStringLiteral("text"));
        }
        return {};
    }
};

}

class AutomationRepeatTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    std::unique_ptr<app::Application> app;
    struct Answer {
        int pressed = 0;
        QVariantMap values;
    };
    std::deque<Answer> answers;
    QList<Shown> shown;

    app::Application::Options options()
    {
        app::Application::Options o;
        o.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        return o;
    }
    application::EditSession &session() { return *app->files().session(*app->workspace().editingTarget()); }
    void select(int count)
    {
        const auto &lines = session().document().lines();
        application::Selection s{lines.front()->id, {}};
        for (int i = 0; i < count; ++i)
            s.selected.insert(lines[static_cast<std::size_t>(i)]->id);
        session().setSelection(s);
    }
    void answer()
    {
        auto *dialogs = app->automation().dialogs();
        if (!dialogs->isOpen())
            return;
        shown << Shown{dialogs->controls(), dialogs->notice()};
        QVERIFY2(!answers.empty(), "a dialog was shown that the test did not expect");
        const Answer a = answers.front();
        answers.pop_front();
        QVariantList values;
        for (const QVariant &c : dialogs->controls()) {
            const QVariantMap m = c.toMap();
            const QString name = m.value(QStringLiteral("name")).toString();
            const QString kind = m.value(QStringLiteral("kind")).toString();
            values << (a.values.contains(name)          ? a.values.value(name)
                       : kind == QLatin1String("intedit")  ? m.value(QStringLiteral("intValue"))
                       : kind == QLatin1String("checkbox") ? m.value(QStringLiteral("checked"))
                       : kind == QLatin1String("label")    ? QVariant()
                                                           : m.value(QStringLiteral("text")));
        }
        dialogs->finish(a.pressed, values);
    }
    QString ran(const std::function<bool()> &start)
    {
        QSignalSpy done(&app->automation(), &app::AutomationShell::runCompleted);
        if (!start())
            return QStringLiteral("not started");
        if (!done.wait(30'000))
            return QStringLiteral("not finished");
        return app->automation().log();
    }
    QString run() { return ran([this] { return app->automation().run(kScript.toStdString(), 0); }); }
    QString repeat() { return ran([this] { return app->automation().repeatLast(); }); }

private slots:
    void init()
    {
        app = std::make_unique<app::Application>(options());
        const QString path = dir.filePath(QStringLiteral("ad2.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(document());
        f.close();
        QVERIFY(app->openFile(path));
        app->automation().load(kScript.toStdString());
        QDeadlineTimer deadline(30'000);
        const auto ready = [this] {
            const auto scripts = app->automation().scripts();
            return std::ranges::any_of(scripts, [](const auto &s) {
                return s.path == kScript.toStdString() && s.state == application::ScriptStatus::State::Ready;
            });
        };
        while (!ready() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(ready());
        connect(app->automation().dialogs(), &ui::AutomationDialogController::changed, this,
                [this] { QTimer::singleShot(0, this, [this] { answer(); }); });
        answers.clear();
        shown.clear();
    }
    void cleanup() { app.reset(); }

    void repeatReplaysUnchangedDialogs()
    {
        QVERIFY(!app->automation().canRepeat());
        select(1);
        answers = {{0, {{QStringLiteral("tags"), QStringLiteral("\\fs")},
                        {QStringLiteral("count"), 7},
                        {QStringLiteral("flag"), false}}},
                   {0, {{QStringLiteral("flag"), true}}}};
        QCOMPARE(run(), QStringLiteral("first button=Apply tags=\\fs count=7 flag=false clip=nil\n"
                                       "second button=Yes tags=nil count=nil flag=true clip=nil\n"));
        QVERIFY(app->automation().canRepeat());
        QCOMPARE(app->automation().repeatText(),
                 QStringLiteral("Repeat last run: Two dialogs, as Yes with the same answers"));

        select(2);
        shown.clear();
        QCOMPARE(repeat(), QStringLiteral("Repeated with the last answers\n"
                                          "first button=Apply tags=\\fs count=7 flag=false clip=nil\n"
                                          "second button=Yes tags=nil count=nil flag=true clip=nil\n"));
        QVERIFY(shown.isEmpty());
        answers = {{0, {}}, {1, {}}};
        QVERIFY(ran([this] { return app->automation().rerunLast(); }).startsWith(
            QStringLiteral("first button=Apply tags=\\blur count=2 flag=true")));
        QCOMPARE(shown.size(), 2);
        QVERIFY(shown[0].notice.isEmpty());
    }

    void repeatShowsAChangedDialogFilledIn()
    {
        select(1);
        answers = {{0, {{QStringLiteral("tags"), QStringLiteral("\\fs")}, {QStringLiteral("count"), 7}}},
                   {0, {{QStringLiteral("flag"), true}}}};
        run();
        select(3);
        shown.clear();
        answers = {{0, {}}, {0, {}}};
        const QString log = repeat();
        QCOMPARE(shown.size(), 2);
        QCOMPARE(shown[0].value(QStringLiteral("tags")).toString(), QStringLiteral("\\fs"));
        QCOMPARE(shown[0].value(QStringLiteral("count")).toInt(), 7);
        QCOMPARE(shown[0].value(QStringLiteral("clip")).toBool(), false);
        QCOMPARE(shown[0].notice, QStringLiteral("This dialog has changed since the last run, so it is shown with "
                                                 "your last answers filled in."));
        QCOMPARE(shown[1].value(QStringLiteral("flag")).toBool(), true);
        QCOMPARE(shown[1].notice, QStringLiteral("An earlier dialog of this run has changed, so this one is shown "
                                                 "with your last answers filled in."));
        QVERIFY(log.contains(QStringLiteral("first button=Apply tags=\\fs count=7 flag=true clip=false")));
    }

    void cancelledRunsAreNotRecorded()
    {
        select(1);
        answers = {{0, {{QStringLiteral("count"), 9}}}, {0, {}}};
        run();
        answers = {{1, {{QStringLiteral("count"), 4}}}};
        QVERIFY(run().startsWith(QStringLiteral("first button=false")));
        answers = {{-1, {}}};
        run();
        shown.clear();
        QVERIFY(repeat().contains(QStringLiteral("first button=Apply tags=\\blur count=9 flag=true")));
        QVERIFY(shown.isEmpty());
        const auto key = ui::ScriptDialogMemory::keyOf(kScript, [] {
            application::DialogRequest r;
            for (const char *kind : {"label", "edit", "intedit", "checkbox"})
                r.controls.push_back(application::DialogControl{.kind = kind});
            r.controls[1].name = "tags";
            r.controls[2].name = "count";
            r.controls[3].name = "flag";
            return r;
        }());
        ui::ScriptDialogMemory memory(app->settingsStore());
        const auto last = memory.lastAnswer(key);
        QVERIFY(last);
        QCOMPARE(std::get<double>(last->answered.at("count")), 9.0);
    }

    void fillLastAnswersKeepsWhatTheScriptWorksOut()
    {
        select(1);
        answers = {{0, {{QStringLiteral("tags"), QStringLiteral("\\fs")},
                        {QStringLiteral("count"), 7},
                        {QStringLiteral("flag"), false}}},
                   {0, {}}};
        run();
        shown.clear();
        answers = {{1, {}}};
        run();
        QCOMPARE(shown[0].value(QStringLiteral("tags")).toString(), QStringLiteral("\\blur"));
        QCOMPARE(shown[0].value(QStringLiteral("count")).toInt(), 1);

        app->settingsStore()->set("automation.fillLastAnswers", true);
        shown.clear();
        answers = {{1, {}}};
        run();
        QCOMPARE(shown[0].value(QStringLiteral("tags")).toString(), QStringLiteral("\\fs"));
        QCOMPARE(shown[0].value(QStringLiteral("count")).toInt(), 7);
        QCOMPARE(shown[0].value(QStringLiteral("flag")).toBool(), false);
        QVERIFY(shown[0].notice.isEmpty());

        select(2);
        shown.clear();
        answers = {{1, {}}};
        run();
        QCOMPARE(shown[0].value(QStringLiteral("tags")).toString(), QStringLiteral("\\fs"));
        QCOMPARE(shown[0].value(QStringLiteral("count")).toInt(), 2);
    }

    void clearingTheAnswersEndsRepeat()
    {
        select(1);
        answers = {{0, {}}, {0, {}}};
        run();
        QVERIFY(app->automation().canRepeat());
        QVERIFY(!app->settingsStore()->list("automation.dialogAnswers").isEmpty());
        app->applySettings({{QStringLiteral("automation.dialogAnswers"), QStringList()}}, false);
        QVERIFY(app->settingsStore()->list("automation.dialogAnswers").isEmpty());
        QVERIFY(!app->automation().canRepeat());
    }

    void repeatFollowsTheMacroAcrossAReload()
    {
        const QString copy = dir.filePath(QStringLiteral("moving.lua"));
        QFile from(kScript);
        QVERIFY(from.open(QIODevice::ReadOnly));
        const QByteArray source = from.readAll();
        const auto write = [&copy](const QByteArray &text) {
            QFile f(copy);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(text);
        };
        auto &shell = app->automation();
        const auto macros = [&shell, &copy] {
            for (const auto &s : shell.scripts())
                if (s.path == copy.toStdString() && s.state == application::ScriptStatus::State::Ready) {
                    QStringList names;
                    for (const auto &m : s.info.macros)
                        names << QString::fromStdString(m.name);
                    return names;
                }
            return QStringList();
        };
        write(source);
        shell.load(copy.toStdString());
        QTRY_COMPARE_WITH_TIMEOUT(macros(), (QStringList{QStringLiteral("Two dialogs"), QStringLiteral("Save and close"),
                                                  QStringLiteral("Trust prompt")}),
                                  30'000);
        select(1);
        answers = {{0, {}}, {0, {}}};
        QVERIFY(ran([&] { return shell.run(copy.toStdString(), 0); }).startsWith(QStringLiteral("first button=Apply")));
        QVERIFY(shell.canRepeat());

        QByteArray moved = source;
        moved.replace("aegisub.register_macro(\"Two dialogs\"",
                      "aegisub.register_macro(\"Other\", \"\", function() aegisub.debug.out(\"other ran\\n\") end)\n"
                      "aegisub.register_macro(\"Two dialogs\"");
        write(moved);
        QVERIFY(shell.reload(copy.toStdString()));
        QTRY_COMPARE_WITH_TIMEOUT(macros(), (QStringList{QStringLiteral("Other"), QStringLiteral("Two dialogs"),
                                                  QStringLiteral("Save and close"), QStringLiteral("Trust prompt")}),
                                  30'000);
        QVERIFY(repeat().startsWith(QStringLiteral("Repeated with the last answers\nfirst button=Apply")));

        QByteArray renamed = source;
        renamed.replace("\"Two dialogs\"", "\"Renamed\"");
        write(renamed);
        QVERIFY(shell.reload(copy.toStdString()));
        QTRY_COMPARE_WITH_TIMEOUT(macros(), (QStringList{QStringLiteral("Renamed"), QStringLiteral("Save and close"),
                                                  QStringLiteral("Trust prompt")}),
                                  30'000);
        QCOMPARE(repeat(), QStringLiteral("not started"));
        QVERIFY(!shell.canRepeat());
        QCOMPARE(shell.lastMessage(), QStringLiteral("The last run's macro is no longer loaded, so it can't be repeated."));
    }

    void repeatTextShowsTheButtonAsShown()
    {
        answers = {{0, {}}};
        QCOMPARE(ran([this] { return app->automation().run(kScript.toStdString(), 1); }),
                 QStringLiteral("button=Save && &Close backup=true\n"));
        QCOMPARE(app->automation().repeatText(),
                 QStringLiteral("Repeat last run: Save and close, as Save & Close with the same answers"));
    }

    void repeatAsksQuestionsAgain()
    {
        app->settingsStore()->setValue(QStringLiteral("automation.dialogAnswers"), QStringList());
        answers = {{1, {}}, {0, {{QStringLiteral("tags"), QStringLiteral("\\fs")}}}};
        QCOMPARE(ran([this] { return app->automation().run(kScript.toStdString(), 2); }),
                 QStringLiteral("trust=Trust this time\nform=Apply tags=\\fs\n"));
        const QStringList kept = app->settingsStore()->list("automation.dialogAnswers");
        QCOMPARE(kept.size(), 1);
        QVERIFY(kept[0].contains(QStringLiteral("tags")));
        QCOMPARE(app->automation().repeatText(),
                 QStringLiteral("Repeat last run: Trust prompt, as Apply with the same answers"));
        shown.clear();
        answers = {{1, {}}};
        QCOMPARE(repeat(), QStringLiteral("Repeated with the last answers\ntrust=Trust this time\nform=Apply tags=\\fs\n"));
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown[0].notice, QStringLiteral("Repeat last run asks questions again rather than answering them for you."));
        answers = {{0, {}}};
        QCOMPARE(repeat(), QStringLiteral("Repeated with the last answers\ntrust=false\n"));
    }

    void resetAllSettingsKeepsAnswersAndPresets()
    {
        select(1);
        answers = {{0, {}}, {0, {}}};
        run();
        auto &settings = *app->settingsStore();
        const QStringList kept = settings.list("automation.dialogAnswers");
        QVERIFY(!kept.isEmpty());
        const QStringList presets{QStringLiteral("0123456789abcdef\t[{\"name\":\"Mine\",\"values\":{\"tags\":\"\\\\fs\"}}]")};
        settings.setValue(QStringLiteral("automation.dialogPresets"), presets);
        settings.setValue(QStringLiteral("automation.fillLastAnswers"), true);
        const QVariantMap shown = app->resetSettings(app->openSettingsDialog().value(QStringLiteral("values")).toMap());
        QCOMPARE(shown.value(QStringLiteral("automation.dialogAnswers")).toStringList(), kept);
        app->applySettings(shown, true);
        QCOMPARE(settings.list("automation.dialogAnswers"), kept);
        QCOMPARE(settings.list("automation.dialogPresets"), presets);
        QVERIFY(!settings.boolean("automation.fillLastAnswers"));
        QVERIFY(app->automation().canRepeat());

        QVariantMap staged = app->openSettingsDialog().value(QStringLiteral("values")).toMap();
        staged.insert(QStringLiteral("automation.dialogAnswers"), QStringList());
        app->applySettings(app->resetSettings(staged), true);
        QVERIFY(settings.list("automation.dialogAnswers").isEmpty());
        QCOMPARE(settings.list("automation.dialogPresets"), presets);
        QVERIFY(!app->automation().canRepeat());
    }

    void screenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        select(1);
        answers = {{0, {{QStringLiteral("tags"), QStringLiteral("\\fs")}}}, {0, {}}};
        run();
        QVERIFY(app->automation().canRepeat());
        QQmlApplicationEngine engine;
        ui::attachDocking(engine);
        engine.setInitialProperties(app->qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *root = engine.rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        window->resize(1280, 800);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto &settings = *app->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        auto restore = qScopeGuard([&] { settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("dark")); });
        const auto named = [&](const char *name) { return root->findChild<QObject *>(QLatin1String(name)); };
        // MUST KILL: the earlier apps of this case leave their session open, so a crash prompt can show here
        if (auto *prompt = named("crashSessionPrompt"); prompt && prompt->property("visible").toBool())
            QVERIFY(QMetaObject::invokeMethod(prompt, "reject"));
        QTRY_VERIFY(!root->property("workspaceLayout").value<QObject *>()->property("modalOpen").toBool());
        const auto save = [&](const QString &name) {
            QTest::qWait(400);
            QImage image = QGuiApplication::platformName() == QLatin1String("xcb")
                               ? window->screen()->grabWindow(0, window->x(), window->y(), window->width(), window->height()).toImage()
                               : window->grabWindow();
            QVERIFY(image.save(out + QStringLiteral("/ad2-") + name + QStringLiteral(".png")));
        };
        QVERIFY(QMetaObject::invokeMethod(root, "showPanel", Q_ARG(QVariant, QVariant::fromValue(named("automationDock")))));
        QTRY_VERIFY(named("automationDock")->property("isOpen").toBool());
        for (const QString &code : {QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("highContrastWhite"),
                                    QStringLiteral("highContrastBlack")}) {
            settings.setValue(QStringLiteral("appearance.theme"), code);
            auto *repeatButton = qobject_cast<QQuickItem *>(named("automationRepeat"));
            QVERIFY(repeatButton);
            QTRY_VERIFY(repeatButton->isEnabled());
            QTest::mouseMove(window, repeatButton->mapToScene(QPointF(repeatButton->width() / 2,
                                                                      repeatButton->height() / 2)).toPoint());
            // MUST KILL: fixed 1200 ms wait for the tooltip's delay
            QTest::qWait(1200);
            save(QStringLiteral("panel-") + code);
            QTest::mouseMove(window, QPoint(640, 790));

            auto *menu = named("automationMenu");
            QVERIFY(QMetaObject::invokeMethod(menu, "open"));
            auto *repeatItem = qobject_cast<QQuickItem *>(named("repeatRunMenuItem"));
            QTRY_VERIFY(repeatItem && repeatItem->isVisible() && repeatItem->isEnabled());
            QTest::mouseMove(repeatItem->window(), repeatItem->mapToScene(QPointF(repeatItem->width() / 2,
                                                                                  repeatItem->height() / 2)).toPoint());
            save(QStringLiteral("menu-") + code);
            QVERIFY(QMetaObject::invokeMethod(menu, "close"));

            QVariant made;
            QVERIFY(QMetaObject::invokeMethod(engine.rootObjects().first(), "settingsWindow", Q_RETURN_ARG(QVariant, made)));
            auto *dialog = made.value<QObject *>();
            QVERIFY(dialog);
            QVERIFY(QMetaObject::invokeMethod(dialog, "openDialog", Q_ARG(QVariant, QStringLiteral("automation")),
                                              Q_ARG(QVariant, QVariant())));
            QTRY_VERIFY(dialog->property("visible").toBool());
            auto *group = qobject_cast<QQuickItem *>(dialog->findChild<QObject *>(QStringLiteral("settingsScriptDialogsGroup")));
            QVERIFY(group);
            QTRY_VERIFY(group->isVisible() && group->height() > 0);
            {
                QTest::qWait(400);
                auto *settingsWindow = qobject_cast<QQuickWindow *>(dialog);
                QVERIFY(settingsWindow);
                const QRect area = settingsWindow->frameGeometry();
                QImage image = QGuiApplication::platformName() == QLatin1String("xcb")
                                   ? settingsWindow->screen()->grabWindow(0, area.x(), area.y(), area.width(), area.height()).toImage()
                                   : settingsWindow->grabWindow();
                QVERIFY(image.save(out + QStringLiteral("/ad2-settings-") + code + QStringLiteral(".png")));
            }
            auto *clear = dialog->findChild<QObject *>(QStringLiteral("settingsClearAnswers"));
            QVERIFY(clear->property("enabled").toBool());
            QVERIFY(QMetaObject::invokeMethod(clear, "clicked"));
            QQuickWindow *box = nullptr;
            QTRY_VERIFY((box = qobject_cast<QQuickWindow *>(dialog->property("question").value<QObject *>())) &&
                        box->isVisible());
            QVERIFY(QTest::qWaitForWindowExposed(box));
            QTest::qWait(300);
            QVERIFY(box->grabWindow().save(out + QStringLiteral("/ad2-clear-answers-") + code + QStringLiteral(".png")));
            QVERIFY(QMetaObject::invokeMethod(box, "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
            QTRY_VERIFY(!box->isVisible());
            QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
            QTRY_VERIFY(!dialog->property("visible").toBool());
        }
    }
};

QTEST_MAIN(AutomationRepeatTests)
#include "automation_repeat_tests.moc"
