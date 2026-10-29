#include "hikari/app/application.h"
#include "hikari/application/generated_runs.h"
#include "docking.h"
#include "shell_controller.h"
#include "template_editor_controller.h"
#include "video_view_controller.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>
#include <set>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QObject *findIn(QQuickItem *at, const QString &name)
{
    if (!at)
        return nullptr;
    if (at->objectName() == name)
        return at;
    if (QObject *o = at->findChild<QObject *>(name, Qt::FindDirectChildrenOnly))
        return o;
    for (QQuickItem *child : at->childItems())
        if (QObject *found = findIn(child, name))
            return found;
    return nullptr;
}

}

class TemplateEditorShellTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    QObject *root() const { return engine->rootObjects().first(); }
    QObject *object(const QString &name) const
    {
        if (QObject *o = root()->findChild<QObject *>(name))
            return o;
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (auto *q = qobject_cast<QQuickWindow *>(w))
                if (QObject *o = findIn(q->contentItem(), name))
                    return o;
        return nullptr;
    }
    QQuickItem *item(const QString &name) const { return qobject_cast<QQuickItem *>(object(name)); }
    QString textOf(const QString &name) const
    {
        const QObject *o = object(name);
        return o ? o->property("text").toString() : QString();
    }
    ui::TemplateEditorController &editor() { return application->templateEditor(); }
    application::EditSession *session()
    {
        const auto target = application->workspace().editingTarget();
        return target ? application->files().session(*target) : nullptr;
    }
    void showPanel(const char *dock)
    {
        QVERIFY(QMetaObject::invokeMethod(root(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(object(QLatin1String(dock))))));
        QTRY_VERIFY(object(QLatin1String(dock))->property("isOpen").toBool());
    }
    void click(QQuickItem *at)
    {
        QVERIFY(at);
        QTest::mouseClick(at->window(), Qt::LeftButton, {},
                          at->mapToScene(QPointF(at->width() / 2, at->height() / 2)).toPoint());
    }

    bool loadTemplater()
    {
        application->automation().load(HIKARI_KARAOKE_TEMPLATER);
        QDeadlineTimer deadline(30'000);
        while (!deadline.hasExpired()) {
            const auto scripts = application->automation().scripts();
            if (std::ranges::any_of(scripts, [](const auto &s) { return s.path == HIKARI_KARAOKE_TEMPLATER; }) &&
                std::ranges::none_of(scripts, [](const auto &s) {
                    return s.state == application::ScriptStatus::State::Loading;
                }))
                return true;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        return false;
    }
    bool openSong(const char *name, bool video, bool trust = true)
    {
        const QString path = dir.filePath(QLatin1String(name));
        QFile::remove(path);
        if (!QFile::copy(QStringLiteral(HIKARI_KARAOKE_CORPUS "/") + QLatin1String(name), path))
            return false;
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        return openPath(path, video, trust);
    }
    bool openPath(const QString &path, bool video, bool trust = true)
    {
        if (!application->openFile(path) || !loadTemplater())
            return false;
        if (trust)
            editor().trustDocument();
        if (video) {
            application->video().openVideo(QStringLiteral("?dummy:23.976000:1000:1280:720:28:30:64:"));
            if (!QTest::qWaitFor([&] { return application->video().session().shownFrame().value_or(-1) >= 0; }, 20'000))
                return false;
        }
        for (const auto *l : session()->document().lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l)) {
                application->clickLine(l->id.value, 0);
                return true;
            }
        return false;
    }

private slots:
    void init()
    {
        QVERIFY(dir.isValid());
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        if (QTest::currentTestFunction() == QByteArrayLiteral("aTemplateTypedBeforeATabChangeIsAutosaved"))
            options.recoveryDir = dir.filePath(QStringLiteral("Recovery"));
        application = new app::Application(options);
        // MUST KILL: spelling off only to keep its missing-dictionary notice off the panel
        application->settingsStore()->setValue(QStringLiteral("editor.spellchecker"), false);
        engine = new QQmlApplicationEngine;
        ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(root());
        QVERIFY(window);
        window->resize(1280, 800);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    void cleanup()
    {
        delete engine;
        delete application;
        engine = nullptr;
        application = nullptr;
    }

    void itIsATabBesideTheLineEditorAndPreviewsOnlyWhileShown()
    {
        auto *dock = object(QStringLiteral("templateDock"));
        QVERIFY(dock);
        QVERIFY(!dock->property("isOpen").toBool());
        QCOMPARE(dock->property("title").toString(), QStringLiteral("Template editor"));
        QVERIFY(object(QStringLiteral("panelMenuTemplate")));
        QVERIFY(openSong("stock-tutorials.ass", false));
        QVERIFY(!editor().shown());
        showPanel("templateDock");
        QTRY_VERIFY(editor().shown());
        auto *docking = engine->singletonInstance<QObject *>("Hikari.Ui", "Docking");
        QVERIFY(docking);
        int tabs = 0, editorTabs = 0;
        QVERIFY(QMetaObject::invokeMethod(docking, "tabCount", Q_RETURN_ARG(int, tabs), Q_ARG(QString, QStringLiteral("Template"))));
        QVERIFY(QMetaObject::invokeMethod(docking, "tabCount", Q_RETURN_ARG(int, editorTabs), Q_ARG(QString, QStringLiteral("Editor"))));
        QVERIFY(tabs >= 2);
        QCOMPARE(tabs, editorTabs);
        auto *eye = item(QStringLiteral("templatePreviewOnVideo"));
        QVERIFY(eye);
        QTRY_VERIFY(eye->isVisible());
        QCOMPARE(eye->window(), window);
        QTRY_VERIFY2_WITH_TIMEOUT(editor().hasPreview(),
                                  qPrintable(QStringLiteral("available %1 source %2 shown %3 problems %4: %5")
                                                 .arg(editor().available()).arg(editor().source())
                                                 .arg(editor().shown()).arg(editor().hasProblems())
                                                 .arg(editor().problem())),
                                  20'000);
        showPanel("editorDock");
        QTRY_VERIFY(!editor().shown());
        QTRY_VERIFY(!eye->isVisible());
        showPanel("templateDock");
        QTRY_VERIFY(editor().shown());
    }

    void theVideoShowsThePreviewWithABadge()
    {
        QVERIFY(openSong("stock-tutorials.ass", true));
        showPanel("templateDock");
        QTRY_VERIFY_WITH_TIMEOUT(editor().videoPreview(), 20'000);
        auto *badge = item(QStringLiteral("videoTemplateBadge"));
        QVERIFY(badge);
        QTRY_VERIFY(badge->isVisible());
        QCOMPARE(textOf(QStringLiteral("videoTemplateBadgeText")),
                 QStringLiteral("<b>Preview</b> · line %1 · not applied").arg(editor().sourceNumber()));
        QVERIFY(application->videoView().toggleRenderCost());
        auto *cost = item(QStringLiteral("videoRenderCost"));
        QTRY_VERIFY(cost->isVisible());
        QVERIFY(cost->property("textTop").toReal() >= badge->height() + 8);
        click(item(QStringLiteral("templatePreviewOnVideo")));
        QTRY_VERIFY(!editor().previewOnVideo());
        QTRY_VERIFY(!badge->isVisible());
        QCOMPARE(cost->property("textTop").toReal(), 8.0);
        QVERIFY(application->videoView().toggleRenderCost());
    }

    void applyIsTheShellsRunAndOneUndoStep()
    {
        QVERIFY(openSong("stock-tutorials.ass", false));
        showPanel("templateDock");
        const std::size_t lines = session()->document().lines().size();
        auto *apply = item(QStringLiteral("templateApply"));
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 20'000);
        QSignalSpy done(&application->automation(), &app::AutomationShell::runCompleted);
        click(apply);
        QVERIFY(done.wait(30'000));
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(session()->document().lines().size() > lines);
        QTRY_VERIFY(textOf(QStringLiteral("templateStatus")).startsWith(QStringLiteral("Applied: ")));
        QVERIFY(session()->undo());
        QCOMPARE(session()->document().lines().size(), lines);
    }

    void aTemplateTypedBeforeATabChangeIsKept()
    {
        QVERIFY(openSong("km-minimal.ass", false));
        QVERIFY(openSong("stock-tutorials.ass", false));
        application->selectTab(1);
        showPanel("templateDock");
        QTRY_VERIFY_WITH_TIMEOUT(!editor().text().isEmpty(), 20'000);
        application::EditSession *typed = session();
        const core::LineId id{editor().current()};
        const auto textOfTyped = [&] {
            for (const auto *l : typed->document().lines())
                if (l->id == id)
                    return std::u8string(l->text);
            return std::u8string();
        };
        editor().setText(editor().text() + QStringLiteral("{\\bord7}"));
        application->selectTab(0);
        QVERIFY(std::u8string_view(textOfTyped()).ends_with(u8"{\\bord7}"));
        QVERIFY(typed->isDirty());
        QVERIFY(!session()->isDirty());
        QTRY_VERIFY_WITH_TIMEOUT(!editor().text().isEmpty(), 20'000);
        editor().setText(editor().text() + QStringLiteral("{\\bord8}"));
        QCOMPARE(application->reviewCloseTab(0).size(), qsizetype(1));
    }

    void aTemplateTypedBeforeATabChangeIsAutosaved()
    {
        QVERIFY(openSong("km-minimal.ass", false));
        QVERIFY(openSong("stock-tutorials.ass", false));
        application->selectTab(1);
        showPanel("templateDock");
        QTRY_VERIFY_WITH_TIMEOUT(!editor().text().isEmpty(), 20'000);
        const auto typed = *application->workspace().editingTarget();
        QVERIFY(!application->autosavePending(typed));
        editor().setText(editor().text() + QStringLiteral("{\\bord7}"));
        application->selectTab(0);
        QVERIFY(application->autosavePending(typed));
    }

    void thePreviewFollowsTheLineEditorsMoves()
    {
        QVERIFY(openSong("stock-tutorials.ass", false));
        showPanel("templateDock");
        const auto first = session()->selection().active;
        QVERIFY(first);
        QTRY_COMPARE_WITH_TIMEOUT(editor().source(), first->value, 20'000);
        std::optional<core::LineId> reached;
        for (int k = 0; k < 20 && !reached; ++k) {
            QVERIFY(application->editor().nextLine(1));
            const auto active = session()->selection().active;
            for (const auto *l : session()->document().lines())
                if (active && l->id == *active && application::isKaraokeLine(*l) &&
                    !application::templateKindOf(*l) && *active != *first)
                    reached = active;
        }
        QVERIFY(reached);
        QCOMPARE(editor().source(), reached->value);
    }

    void aShortcutsRunSeesTheEditJustTyped()
    {
        QVERIFY(openSong("stock-tutorials.ass", false));
        showPanel("templateDock");
        QTRY_VERIFY_WITH_TIMEOUT(item(QStringLiteral("templateApply"))->isEnabled(), 20'000);
        editor().setText(editor().text() + QStringLiteral("{\\bord7}"));
        QSignalSpy done(&application->automation(), &app::AutomationShell::runCompleted);
        QVERIFY(application->automation().run(HIKARI_KARAOKE_TEMPLATER, 0, app::AutomationShell::RunOrigin::Hotkey));
        QVERIFY(done.wait(30'000));
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(std::ranges::any_of(session()->document().lines(), [](const core::LineRecord *l) {
            return application::isGeneratedLine(*l) && std::u8string_view(l->text).find(u8"bord7") != std::u8string_view::npos;
        }));
    }

    void aSaveWritesTheTemplateJustTyped()
    {
        QVERIFY(openSong("stock-tutorials.ass", false));
        showPanel("templateDock");
        const QString path = dir.filePath(QStringLiteral("stock-tutorials.ass"));
        const auto saved = [&](const char *tag) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) && f.readAll().contains(tag);
        };
        QVERIFY(!session()->isDirty());
        editor().setText(editor().text() + QStringLiteral("{\\bord8}"));
        application->saveAll();
        QTRY_VERIFY_WITH_TIMEOUT(saved("bord8"), 10'000);
        QTRY_VERIFY(!session()->isDirty());
        QTest::qWait(1'000);
        QVERIFY(!session()->isDirty());
        editor().setText(editor().text() + QStringLiteral("{\\bord9}"));
        QVERIFY(application->editor().save());
        QTRY_VERIFY_WITH_TIMEOUT(saved("bord9"), 10'000);
        QTest::qWait(1'000);
        QVERIFY(!session()->isDirty());
    }

    void aFileRunsNoCodeUntilTheUserPreviewsIt()
    {
        showPanel("templateDock");
        QVERIFY(openSong("stock-tutorials.ass", false, false));
        QTest::qWait(500);
        QVERIFY(!editor().trusted());
        QVERIFY(!editor().hasPreview());
        editor().trustDocument();
        QTRY_VERIFY_WITH_TIMEOUT(editor().hasPreview(), 20'000);
        QVERIFY(openSong("km-minimal.ass", false, false));
        application->selectTab(1);
        QTRY_VERIFY(!editor().trusted());
        application->selectTab(0);
        QTRY_VERIFY(editor().trusted());
    }

    void thePreviewFollowsTheLineClicked()
    {
        QVERIFY(openSong("stock-tutorials.ass", true));
        showPanel("templateDock");
        QTRY_VERIFY_WITH_TIMEOUT(editor().source() != 0, 20'000);
        const auto first = editor().source();
        // MUST KILL: a throwaway first playback with a fixed 500 ms wait opens the player
        application->video().loopRange(editor().sourceStartMs(), editor().sourceEndMs());
        QTest::qWait(500);
        application->video().pause();
        application->video().loopRange(editor().sourceStartMs(), editor().sourceEndMs());
        QTRY_VERIFY(application->video().playing());
        for (const auto *l : session()->document().lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l) && l->id.value != first) {
                application->clickLine(l->id.value, 0);
                QTRY_COMPARE(editor().source(), l->id.value);
                return;
            }
        QFAIL("no second karaoke Line");
    }

    void theLoopFollowsTheLinePreviewed()
    {
        QVERIFY(openSong("stock-tutorials.ass", true));
        showPanel("templateDock");
        QTRY_VERIFY_WITH_TIMEOUT(editor().source() != 0, 20'000);
        auto *loop = item(QStringLiteral("templateLoop"));
        QTRY_VERIFY(loop->isEnabled());
        // MUST KILL: a throwaway first playback with a fixed 500 ms wait opens the player
        application->video().loopRange(editor().sourceStartMs(), editor().sourceEndMs());
        QTest::qWait(500);
        application->video().pause();
        click(loop);
        QTRY_VERIFY(application->video().looping());
        const auto first = editor().source();
        QCOMPARE(application->video().session().loopSpan(),
                 std::pair(editor().sourceStartMs(), editor().sourceEndMs()));
        for (const auto *l : session()->document().lines())
            if (application::isKaraokeLine(*l) && !application::templateKindOf(*l) && l->id.value != first) {
                application->clickLine(l->id.value, 0);
                break;
            }
        QTRY_VERIFY(editor().source() != first);
        QTRY_COMPARE(application->video().session().loopSpan(),
                     std::pair(editor().sourceStartMs(), editor().sourceEndMs()));
        QVERIFY(application->video().looping());
        click(loop);
        QTRY_VERIFY(!application->video().looping());
        click(loop);
        QTRY_VERIFY(application->video().looping());
        QVERIFY(application->video().session().seekToMs(static_cast<int>(editor().sourceEndMs()) + 500));
        QTRY_VERIFY(!loop->property("checked").toBool());
        QTest::qWait(300);
        QVERIFY(application->video().playing());
    }

    void aReloadedFileAsksAgain()
    {
        showPanel("templateDock");
        QVERIFY(openSong("stock-tutorials.ass", false));
        QVERIFY(editor().trusted());
        QTRY_VERIFY_WITH_TIMEOUT(editor().hasPreview(), 20'000);
        QVERIFY(application->reloadTarget());
        QTRY_VERIFY(!editor().trusted());
        QTest::qWait(300);
        QVERIFY(!editor().hasPreview());
    }

    void anApplyAcrossAReloadIsRefused()
    {
        QVERIFY(openSong("stock-tutorials.ass", false, false));
        showPanel("templateDock");
        const std::size_t lines = session()->document().lines().size();
        auto *apply = item(QStringLiteral("templateApply"));
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 20'000);
        QSignalSpy done(&application->automation(), &app::AutomationShell::runCompleted);
        click(apply);
        QVERIFY(application->automation().running());
        QVERIFY(application->reloadTarget());
        QVERIFY(done.wait(30'000));
        QVERIFY(!done.first().at(0).toBool());
        QCOMPARE(session()->document().lines().size(), lines);
        QVERIFY(!editor().trusted());
        QVERIFY(editor().lastApply().isEmpty());
    }

    void applyOnAnotherTabLeavesThisTabsSummary()
    {
        QVERIFY(openSong("km-minimal.ass", false));
        QVERIFY(openSong("stock-tutorials.ass", false));
        QCOMPARE(application->tabs().size(), qsizetype(2));
        application->selectTab(1);
        QCOMPARE(application->currentTab(), 1);
        showPanel("templateDock");
        auto *apply = item(QStringLiteral("templateApply"));
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 20'000);
        QSignalSpy done(&application->automation(), &app::AutomationShell::runCompleted);
        click(apply);
        QVERIFY(application->automation().running());
        application->selectTab(0);
        QVERIFY(done.wait(30'000));
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(editor().lastApply().isEmpty());
        QTest::qWait(100);
        QVERIFY(!textOf(QStringLiteral("templateStatus")).startsWith(QStringLiteral("Applied: ")));
        application->selectTab(1);
        QTRY_VERIFY(!editor().lastApply().isEmpty());
        QCOMPARE(editor().changedCount(), 0);
        QVERIFY(editor().trusted());
    }

    void screenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_TEMPLATE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_TEMPLATE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        window->resize(1600, 1000);
        const QString path = dir.filePath(QStringLiteral("song.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\nWrapStyle: 0\n\n"
                "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
                "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: T1a,Noto Sans,72,&H00FFFFFF,&H00B469FF,&H00301018,&H80000000,-1,0,0,0,100,100,0,0,1,3,0,8,10,10,"
                "40,1\n"
                "Style: T2a,Noto Sans,44,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,"
                "70,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Comment: 0,0:00:00.00,0:00:00.00,T1a,,0,0,0,code once,function fade_in(t) return t * 2 end\n"
                "Comment: 0,0:00:00.00,0:00:00.00,T1a,,0,0,0,template pre-line,"
                "{\\an8\\pos($center,$top)\\fad(300,200)\\1a&H80&}\n"
                "Comment: 1,0:00:00.00,0:00:00.00,T1a,,0,0,0,template syl noblank,"
                "!retime(\"syl\",-200,0)!{\\an5\\pos($scenter,$smiddle)\\fad(150,0)\\t(200,!200+$sdur!,"
                "\\fscx125\\fscy125\\1c&HB469FF&)\\t(!200+$sdur!,!400+$sdur!,\\fscx100\\fscy100)}\n"
                "Comment: 0,0:00:00.00,0:00:00.00,T2a,,0,0,0,template line keeptags,{\\an2\\fad(200,200)}\n"
                "Dialogue: 0,0:00:01.00,0:00:04.50,T1a,,0,0,0,,{\\k32}ki {\\k28}mi {\\k41}no {\\k25}na {\\k60}mae\n"
                "Dialogue: 0,0:00:04.60,0:00:08.00,T1a,,0,0,0,,{\\k30}ha{\\k30}ru {\\k40}no {\\k30}ka{\\k40}ze\n"
                "Dialogue: 0,0:00:01.00,0:00:08.00,T2a,,0,0,0,,{\\k60}Your {\\k60}name, {\\k60}carried {\\k40}on "
                "{\\k40}the {\\k60}spring {\\k80}wind\n");
        f.close();
        QVERIFY(openPath(path, true, false));
        QVERIFY(QMetaObject::invokeMethod(object(QStringLiteral("audioDock")), "close"));
        showPanel("templateDock");
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        const auto shoot = [&](const QString &name) {
            for (const char *code : {"light", "dark", "highContrastWhite", "highContrastBlack"}) {
                settings.setValue(QStringLiteral("appearance.theme"), QString::fromLatin1(code));
                QTest::qWait(400);
                QImage shot;
                QTRY_VERIFY((shot = window->grabWindow(), !shot.isNull() && shot.width() > 0));
                QVERIFY(shot.save(out + QStringLiteral("/template-") + name + QLatin1Char('-') + QLatin1String(code) +
                                  QStringLiteral(".png")));
            }
        };
        QTRY_VERIFY(item(QStringLiteral("templatePreviewTrust"))->isVisible());
        QTest::qWait(500);
        shoot(QStringLiteral("trust"));
        editor().trustDocument();
        QTRY_VERIFY_WITH_TIMEOUT(editor().videoPreview(), 20'000);
        for (const QVariant &v : editor().templates())
            if (v.toMap().value(QStringLiteral("effect")).toString().startsWith(QStringLiteral("template syl"))) {
                editor().setCurrent(v.toMap().value(QStringLiteral("id")).toULongLong());
                break;
            }
        application->video().showFrameAt(qRound(1.75 * 23.976));
        QTest::qWait(1500);
        shoot(QStringLiteral("editor"));
        const QString text = editor().text();
        editor().setText(QStringLiteral("{\\fscx!math.rnd(1,5)!}") + text);
        QTRY_VERIFY_WITH_TIMEOUT(editor().hasProblems(), 20'000);
        QTest::qWait(500);
        shoot(QStringLiteral("problem"));
        editor().setText(text);
        QTRY_VERIFY_WITH_TIMEOUT(!editor().hasProblems(), 20'000);
        auto *field = item(QStringLiteral("templateText"));
        field->forceActiveFocus();
        field->setProperty("cursorPosition", 0);
        for (const char c : QByteArray("{\\pos($s"))
            QTest::keyClick(window, c);
        auto *list = object(QStringLiteral("templateCompletion"));
        QTRY_VERIFY(list->property("opened").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!editor().values().isEmpty(), 20'000);
        QTest::qWait(300);
        shoot(QStringLiteral("variables"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!list->property("opened").toBool());
        editor().setText(text);
        editor().commit();
        auto *dock = object(QStringLiteral("templateDock"));
        dock->setProperty("isFloating", true);
        auto *panel = item(QStringLiteral("templatePanel"));
        QTRY_VERIFY(panel && panel->window() != window);
        QQuickWindow *floating = panel->window();
        floating->resize(400, 760);
        QTest::qWait(500);
        for (const char *code : {"light", "dark", "highContrastWhite", "highContrastBlack"}) {
            settings.setValue(QStringLiteral("appearance.theme"), QString::fromLatin1(code));
            QTest::qWait(400);
            QImage shot;
            QTRY_VERIFY((shot = floating->grabWindow(), !shot.isNull() && shot.width() > 0));
            QVERIFY(shot.save(out + QStringLiteral("/template-narrow-") + QLatin1String(code) + QStringLiteral(".png")));
        }
        dock->setProperty("isFloating", false);
    }

    void aPresetsLinesFoldInTheKaraokeGrid()
    {
        const QString path = dir.filePath(QStringLiteral("fold.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\nWrapStyle: 0\n\n"
                "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
                "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: Romaji,Noto Sans,64,&H00FFFFFF,&H00FF8C3C,&H00301018,&H80000000,-1,0,0,0,100,100,0,0,1,3,0,8,10,"
                "10,60,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:03.00,Romaji,,0,0,0,,{\\k50}ki{\\k50}ra{\\k100}ri\n"
                "Dialogue: 0,0:00:04.00,0:00:06.00,Romaji,,0,0,0,,{\\k100}yo{\\k100}ru\n");
        f.close();
        QVERIFY(openPath(path, false));
        QVERIFY(QMetaObject::invokeMethod(root(), "applyPreset", Q_ARG(QVariant, QStringLiteral("Karaoke"))));
        auto &shell = application->shell();
        QVERIFY(shell.foldGenerated());
        QVERIFY(editor().insertPreset(0));
        QSignalSpy done(&application->automation(), &app::AutomationShell::runCompleted);
        QTRY_VERIFY_WITH_TIMEOUT(editor().canApply(), 20'000);
        QVERIFY(editor().apply(false));
        QVERIFY(done.wait(30'000));
        QVERIFY(done.first().at(0).toBool());
        std::set<core::LineId> generated;
        for (const auto *l : session()->document().lines())
            if (application::isFoldableGeneratedLine(*l))
                generated.insert(l->id);
        QVERIFY(generated.size() > 1);
        std::size_t rows = 0;
        for (const auto id : shell.displayedLines()) {
            if (!generated.contains(id))
                continue;
            QCOMPARE(shell.runFirst(id), std::optional(id));
            QVERIFY(shell.runFolded(id));
            ++rows;
        }
        QVERIFY(rows > 0 && rows < generated.size());
    }

    void presetScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_TEMPLATE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_TEMPLATE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        window->resize(1600, 1000);
        const QString path = dir.filePath(QStringLiteral("presets.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\nWrapStyle: 0\n\n"
                "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
                "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: Romaji,Noto Sans,64,&H00FFFFFF,&H00FF8C3C,&H00301018,&H80000000,-1,0,0,0,100,100,0,0,1,3,0,8,10,"
                "10,60,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:04.50,Romaji,,0,0,0,,{\\k40}ki{\\k35}ra{\\k0} {\\k45}ki{\\k30}ra "
                "{\\k60}hi{\\k50}ka{\\k40}ru\n"
                "Dialogue: 0,0:00:05.00,0:00:07.50,Romaji,,0,0,0,,{\\k50}yo{\\k25}ru{\\k25}no {\\k80}so{\\k70}ra\n");
        f.close();
        QVERIFY(openPath(path, true));
        QVERIFY(QMetaObject::invokeMethod(object(QStringLiteral("audioDock")), "close"));
        showPanel("templateDock");
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        const auto shoot = [&](const QString &name) {
            for (const char *code : {"light", "dark", "highContrastWhite", "highContrastBlack"}) {
                settings.setValue(QStringLiteral("appearance.theme"), QString::fromLatin1(code));
                QTest::qWait(400);
                QImage shot;
                QTRY_VERIFY((shot = window->grabWindow(), !shot.isNull() && shot.width() > 0));
                QVERIFY(shot.save(out + QStringLiteral("/template-") + name + QLatin1Char('-') + QLatin1String(code) +
                                  QStringLiteral(".png")));
            }
        };
        auto *menu = object(QStringLiteral("templateNewMenu"));
        QVERIFY(QMetaObject::invokeMethod(item(QStringLiteral("templateNew")), "clicked"));
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *presets = object(QStringLiteral("templatePresetMenu"));
        QVERIFY(QMetaObject::invokeMethod(presets, "open"));
        QTRY_VERIFY(presets->property("opened").toBool());
        auto *sparkles = item(QStringLiteral("templatePreset_6"));
        QVERIFY(sparkles);
        QTRY_VERIFY(sparkles->isVisible() && sparkles->width() > 0);
        QTest::mouseMove(sparkles->window(), sparkles->mapToScene(QPointF(sparkles->width() / 2, sparkles->height() / 2))
                                                 .toPoint());
        // MUST KILL: fixed 1500 ms wait for the description's tooltip
        QTest::qWait(1500);
        shoot(QStringLiteral("presets-menu"));
        QVERIFY(QMetaObject::invokeMethod(sparkles, "triggered"));
        QTRY_VERIFY(!menu->property("opened").toBool() || QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY_WITH_TIMEOUT(editor().videoPreview(), 20'000);
        application->video().showFrameAt(qRound(2.0 * 23.976));
        QTest::qWait(1500);
        shoot(QStringLiteral("presets-preview"));
        QVERIFY(session()->undo());
        QVERIFY(editor().insertPreset(8));
        QTRY_VERIFY_WITH_TIMEOUT(editor().videoPreview() && !editor().previewPending(), 20'000);
        application->video().showFrameAt(qRound(0.5 * 23.976));
        QTest::qWait(1500);
        shoot(QStringLiteral("presets-preview-letters"));
    }
};

QTEST_MAIN(TemplateEditorShellTests)
#include "template_editor_shell_tests.moc"
