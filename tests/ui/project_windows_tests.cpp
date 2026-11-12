#include "hikari/app/application.h"
#include "hikari/app/font_catalogs_controller.h"
#include "hikari/app/font_collector_controller.h"
#include "hikari/app/forwarded_launches.h"
#include "hikari/app/project_windows.h"
#include "hikari/app/session_json.h"
#include "hikari/application/owned_file.h"
#include "hikari/application/session_file.h"
#include "docking.h"
#include "line_table_model.h"
#include "settings_store.h"
#include "shell_controller.h"
#include "spelling/fake_spelling.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QString writeAss(const QString &path, const char *text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\n"
            "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,," +
            QByteArray(text) + "\n");
    return path;
}

QVariantList discardAll(const QVariantList &rows)
{
    QVariantList out;
    for (const QVariant &r : rows)
        out << QVariantMap{{QStringLiteral("id"), r.toMap().value(QStringLiteral("id"))},
                           {QStringLiteral("save"), false},
                           {QStringLiteral("path"), QString()}};
    return out;
}

}

class ProjectWindowsTests : public QObject {
    Q_OBJECT

    QTemporaryDir home;
    QTemporaryDir dir;
    app::Application *application = nullptr;
    app::ForwardedLaunches *forwarded = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    void start(application::SpellingBackendLoader spelling = {})
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = std::move(spelling);
        application = new app::Application(o);
        forwarded = new app::ForwardedLaunches(*application);
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        forwarded->setWindow(window);
    }
    static QObject *named(QQuickWindow *in, const char *name) { return in->findChild<QObject *>(QLatin1String(name)); }
    QObject *named(const char *name) const { return named(window, name); }
    static QQuickWindow *shell(int id)
    {
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (w->objectName() == QLatin1String("mainWindow") && w->property("shellWindow").toInt() == id)
                return qobject_cast<QQuickWindow *>(w);
        return nullptr;
    }
    QString folder(const char *name, bool project = false) const
    {
        const QString path = dir.filePath(QLatin1String(name));
        QDir().mkpath(project ? path + QStringLiteral("/.hikari") : path);
        return path;
    }
    static void writeAll(const QString &path, const QByteArray &bytes)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
    }
    static QVariantList marks(int shellWindow)
    {
        auto *controller = shell(shellWindow)->property("shell").value<ui::ShellController *>();
        return controller->lines()->index(0, 0).data(ui::LineTableModel::SpellMarksRole).toList();
    }
    static QString canonical(const QString &path) { return app::ProjectWindows::canonicalFolder(path); }
    QStringList titles(int shellWindow) const
    {
        QStringList out;
        for (const QVariant &tab : application->windowTabs(shellWindow))
            out << tab.toMap().value(QStringLiteral("title")).toString();
        return out;
    }
    void edit(const char *text)
    {
        auto *s = application->files().session(*application->workspace().editingTarget());
        QVERIFY(s);
        const auto line = s->document().lines().front()->id;
        QVERIFY(s->editDraftText(line, std::u8string(reinterpret_cast<const char8_t *>(text))));
        QVERIFY(s->commitDraft());
    }
    bool load(const QUrl &file)
    {
        const QVariantMap review = application->reviewSession(file);
        if (!review.value(QStringLiteral("ok")).toBool())
            return false;
        const QVariantList rows = review.value(QStringLiteral("rows")).toList();
        if (rows.isEmpty())
            application->finishClose();
        else
            application->resolveClose(discardAll(rows));
        return true;
    }
    static void closeShell(int id)
    {
        QPointer<QQuickWindow> opened = shell(id);
        if (!opened)
            return;
        opened->close();
        QTRY_VERIFY(!opened || !opened->isVisible());
    }

    static QByteArray readAll(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static QQuickItem *item(QQuickItem *in, const QString &name)
    {
        if (in->objectName() == name)
            return in;
        for (QQuickItem *child : in->childItems())
            if (QQuickItem *found = item(child, name))
                return found;
        return nullptr;
    }
    static QQuickWindow *dialog(const char *name)
    {
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (w->objectName() == QLatin1String(name) && w->isVisible())
                return qobject_cast<QQuickWindow *>(w);
        return nullptr;
    }
    void myScriptInfoDefaults()
    {
        auto *store = application->settingsStore();
        store->setValue(QStringLiteral("scriptProperties.titleOn"), true);
        store->setValue(QStringLiteral("scriptProperties.title"), QStringLiteral("Show"));
        store->setValue(QStringLiteral("scriptProperties.translationOn"), true);
        store->setValue(QStringLiteral("scriptProperties.translation"), QStringLiteral("Mine"));
        store->setValue(QStringLiteral("scriptProperties.timing"), QStringLiteral("Off"));
    }
    int openProjectWindow(const QString &project)
    {
        const int opened = application->openFolder(project, 0).value(QStringLiteral("window")).toInt();
        if (!QTest::qWaitFor([&] { return shell(opened) && shell(opened)->isVisible(); }))
            return -1;
        application->activateShellWindow(opened);
        return QTest::qWaitFor([&] { return application->workspace().activeWindow() == opened; }) ? opened : -1;
    }

private slots:
    void initTestCase() { QVERIFY(home.isValid() && dir.isValid()); }
    void cleanup()
    {
        for (int id = 1; id < 4; ++id)
            closeShell(id);
        delete engine;
        engine = nullptr;
        delete forwarded;
        forwarded = nullptr;
        delete application;
        application = nullptr;
        for (const char *name : {"settings.toml", "hotkeys.toml", "state.json", "last-session.json", "projects.hksession",
                                 "gone.hksession"})
            QFile::remove(home.filePath(QLatin1String(name)));
        QDir(home.filePath(QStringLiteral("projects"))).removeRecursively();
        QDir(home.filePath(QStringLiteral("Dictionary"))).removeRecursively();
        QDir(dir.path()).removeRecursively();
        QDir().mkpath(dir.path());
    }

    void aWindowRowKeepsItsProject()
    {
        application::Session session{"HikariSub", true, {}, 0};
        session.windows.resize(2);
        session.windows[1].project = "/subs/Show S01";
        const QByteArray bytes = app::writeSessionJson(session);
        const QJsonObject o = QJsonDocument::fromJson(bytes).object();
        QVERIFY(!o.contains(QStringLiteral("project")));
        const QJsonArray rows = o.value(QStringLiteral("windows")).toArray();
        QCOMPARE(rows.size(), 2);
        QVERIFY(!rows[0].toObject().contains(QStringLiteral("project")));
        QCOMPARE(rows[1].toObject().value(QStringLiteral("project")).toString(), QStringLiteral("/subs/Show S01"));
        const auto read = app::readSession(std::string_view(bytes.constData(), std::size_t(bytes.size())));
        QVERIFY(read);
        QCOMPARE(read->windows, session.windows);
        QVERIFY(read->windows[1].unknown.empty());
        application::ParseFailure failure;
        QVERIFY(!app::readSession(R"({"version":1,"program":"HikariSub","windows":[{"project":5}],"tabs":[]})", &failure));
        QCOMPARE(failure.message, std::string("Window 1: “project” has the wrong type."));
    }

    void aSessionGivesEachWindowItsProjectBack()
    {
        start();
        const QString show = folder("show");
        application->openFolder(show, 0);
        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep01.ass"), "one")));
        const QString saved = home.filePath(QStringLiteral("projects.hksession"));
        QCOMPARE(application->saveSessionTo(QUrl::fromLocalFile(saved)), QString());
        QFile file(saved);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonArray rows = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("windows")).toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toObject().value(QStringLiteral("project")).toString(), canonical(show));

        QVERIFY(application->reviewCloseProject(0).isEmpty());
        application->finishClose();
        QTRY_VERIFY(titles(0).isEmpty());
        QVERIFY(application->projectRoot(0).isEmpty());
        QVERIFY(load(QUrl::fromLocalFile(saved)));
        QTRY_COMPARE(titles(0), QStringList{QStringLiteral("ep01.ass")});
        QCOMPARE(application->projectRoot(0), canonical(show));

        const QString gone = home.filePath(QStringLiteral("gone.hksession"));
        QFile out(gone);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                            {QStringLiteral("windows"),
                                             QJsonArray{QJsonObject{{QStringLiteral("project"), dir.filePath(QStringLiteral("gone"))}}}},
                                            {QStringLiteral("tabs"), QJsonArray{}}})
                      .toJson());
        out.close();
        QVERIFY(load(QUrl::fromLocalFile(gone)));
        QTRY_VERIFY(application->projectRoot(0).isEmpty());
    }

    void anEmptyWindowTakesTheFolder()
    {
        start();
        const QString show = folder("Show S01");
        const QVariantMap opened = application->openFolder(show, 0);
        QVERIFY(opened.value(QStringLiteral("ok")).toBool());
        QCOMPARE(opened.value(QStringLiteral("window")).toInt(), 0);
        QCOMPARE(application->projectRoot(0), canonical(show));
        QCOMPARE(application->projectOf(0), canonical(show));
        QTRY_COMPARE(window->title(), QStringLiteral("Show S01 - HikariSub"));
        QTRY_COMPARE(named("homeProjectName")->property("text").toString(), QStringLiteral("Show S01"));
        QVERIFY(named("closeProjectMenuItem")->property("enabled").toBool());
        const QVariantList recent = application->homeRecentProjects();
        QCOMPARE(recent.size(), 1);
        QCOMPARE(recent.first().toMap().value(QStringLiteral("path")).toString(), canonical(show));
        QTRY_VERIFY(qobject_cast<QQuickItem *>(named("homeProjectsRecent"))->isVisible());

        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep01.ass"), "one")));
        QTRY_COMPARE(window->title(), QStringLiteral("ep01.ass - Show S01 - HikariSub"));
    }

    void anOpenProjectComesToTheFrontAndABusyWindowOpensANewOne()
    {
        start();
        QVERIFY(application->openFile(writeAss(dir.filePath(QStringLiteral("loose.ass")), "loose")));
        const QString show = folder("show");
        const QVariantMap opened = application->openFolder(show, 0);
        QVERIFY(opened.value(QStringLiteral("ok")).toBool());
        const int other = opened.value(QStringLiteral("window")).toInt();
        QVERIFY(other != 0);
        QTRY_VERIFY(shell(other) && shell(other)->isVisible());
        QCOMPARE(application->projectRoot(other), canonical(show));
        QVERIFY(application->projectRoot(0).isEmpty());
        QCOMPARE(titles(0), QStringList{QStringLiteral("loose.ass")});

        const QVariantMap again = application->openFolder(show, 0, 1);
        QCOMPARE(again.value(QStringLiteral("window")).toInt(), other);
        QVERIFY(!shell(other + 1));

        const QString extras = folder("extras");
        QCOMPARE(application->openFolder(extras, 0, 2).value(QStringLiteral("window")).toInt(), 0);
        QCOMPARE(application->projectRoot(0), canonical(extras));
        QCOMPARE(titles(0), QStringList{QStringLiteral("loose.ass")});
    }

    void aTabMovedIntoAProjectWindowNamesTheProject()
    {
        start();
        QVERIFY(application->openFile(writeAss(dir.filePath(QStringLiteral("loose.ass")), "loose")));
        QVERIFY(application->openFile(writeAss(dir.filePath(QStringLiteral("kept.ass")), "kept")));
        const QVariantMap opened = application->openFolder(folder("Show S01"), 0);
        const int other = opened.value(QStringLiteral("window")).toInt();
        QVERIFY(other != 0);
        QTRY_VERIFY(shell(other) && shell(other)->isVisible());
        const auto toasts = [](int shellWindow) {
            QStringList out;
            for (const QVariant &entry : named(shell(shellWindow), "toastStack")->property("entries").toList())
                out << entry.toMap().value(QStringLiteral("title")).toString();
            return out;
        };
        const QString text = QStringLiteral("This tab now uses the project Show S01.");
        QSignalSpy joined(application, &app::Application::tabJoinedProject);
        const qulonglong loose = application->windowTabs(0).first().toMap().value(QStringLiteral("id")).toULongLong();

        QVERIFY(application->moveTab(loose, other, -1));
        QCOMPARE(joined.size(), 1);
        QCOMPARE(joined.first().first().toInt(), other);
        QTRY_VERIFY(toasts(other).contains(text));

        QVERIFY(application->moveTab(loose, 0, -1));
        QVERIFY(application->moveTabToNewWindow(loose, QPointF(), false));
        QCOMPARE(joined.size(), 1);
    }

    void aFileOrAMissingFolderIsNoProject()
    {
        start();
        QSignalSpy changed(application, &app::Application::projectsChanged);
        const QString file = writeAss(dir.filePath(QStringLiteral("a.ass")), "a");
        QVERIFY(!application->openFolder(file, 0).value(QStringLiteral("ok")).toBool());
        QVERIFY(!application->openFolder(dir.filePath(QStringLiteral("gone")), 0).value(QStringLiteral("ok")).toBool());
        QCOMPARE(changed.size(), 0);
        QVERIFY(application->homeRecentProjects().isEmpty());
    }

    void aFileInAProjectOffersIt()
    {
        start();
        const QString show = folder("show", true);
        QSignalSpy offered(application, &app::Application::projectOffered);
        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/sub/ep01.ass"), "one")));
        QTRY_COMPARE(offered.size(), 1);
        QCOMPARE(offered.first().at(0).toInt(), 0);
        QCOMPARE(offered.first().at(1).toString(), canonical(show));
        QCOMPARE(offered.first().at(2).toString(), QStringLiteral("show"));
        QVERIFY(application->projectRoot(0).isEmpty());
        auto *bar = qobject_cast<QQuickItem *>(named("projectOfferBar"));
        QTRY_VERIFY(bar->isVisible());
        QCOMPARE(bar->property("text").toString(), QStringLiteral("This file is in the project show."));
        QVERIFY(QMetaObject::invokeMethod(named("projectOfferOpen"), "clicked"));
        QCOMPARE(application->projectRoot(0), canonical(show));
        QTRY_VERIFY(!bar->isVisible());
        QCOMPARE(titles(0), QStringList{QStringLiteral("ep01.ass")});

        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep02.ass"), "two")));
        QCOMPARE(offered.size(), 1);
    }

    void aTrustedFolderTakesItsProjectWithoutAsking()
    {
        start();
        const QString show = folder("show", true);
        application->settingsStore()->setValue(QStringLiteral("trust.folders"),
                                               QStringList{QFileInfo(dir.path()).canonicalFilePath()});
        QSignalSpy offered(application, &app::Application::projectOffered);
        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep01.ass"), "one")));
        QTRY_COMPARE(application->projectRoot(0), canonical(show));
        QCOMPARE(offered.size(), 0);
    }

    void closeProjectReviewsClosesAndKeepsTheTabs()
    {
        start();
        const QString show = folder("show");
        application->openFolder(show, 0);
        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep01.ass"), "one")));
        edit("changed");

        QVariantList rows = application->reviewCloseProject(0);
        QCOMPARE(rows.size(), 1);
        application->cancelClose();
        QCOMPARE(application->projectRoot(0), canonical(show));
        QCOMPARE(titles(0), QStringList{QStringLiteral("ep01.ass")});

        rows = application->reviewCloseProject(0);
        QCOMPARE(rows.size(), 1);
        application->resolveClose(discardAll(rows));
        QTRY_VERIFY(titles(0).isEmpty());
        QVERIFY(application->projectRoot(0).isEmpty());
        QTRY_VERIFY(qobject_cast<QQuickItem *>(named("homePage"))->isVisible());
        QTRY_COMPARE(window->title(), QStringLiteral("HikariSub"));
        QVERIFY(!named("closeProjectMenuItem")->property("enabled").toBool());
        QCOMPARE(application->projectLastTabs(0).value(QStringLiteral("count")).toInt(), 0);

        application->openFolder(show, 0);
        const QVariantMap last = application->projectLastTabs(0);
        QCOMPARE(last.value(QStringLiteral("count")).toInt(), 1);
        QCOMPARE(last.value(QStringLiteral("names")).toStringList(), QStringList{QStringLiteral("ep01.ass")});
        auto *reopen = qobject_cast<QQuickItem *>(named("homeReopenTabs"));
        QTRY_VERIFY(reopen->isVisible());
        QCOMPARE(reopen->property("text").toString(), QStringLiteral("Reopen last tabs (1)"));
        QVERIFY(QMetaObject::invokeMethod(reopen, "clicked"));
        QTRY_COMPARE(titles(0), QStringList{QStringLiteral("ep01.ass")});
        QCOMPARE(application->projectRoot(0), canonical(show));
    }

    void closeProjectWithNothingToSaveNeedsNoReview()
    {
        start();
        const QString show = folder("show");
        application->openFolder(show, 0);
        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep01.ass"), "one")));
        QVERIFY(application->reviewCloseProject(0).isEmpty());
        application->finishClose();
        QTRY_VERIFY(titles(0).isEmpty());
        QVERIFY(application->projectRoot(0).isEmpty());

        application->openFolder(show, 0);
        QVERIFY(application->reviewCloseProject(0).isEmpty());
        application->finishClose();
        application->openFolder(show, 0);
        QCOMPARE(application->projectLastTabs(0).value(QStringLiteral("count")).toInt(), 1);
    }

    void aForwardedFolderOpensAsAProject()
    {
        start();
        const QString show = folder("show");
        forwarded->take({{show}, {}, application::WindowRequest::Default});
        QTRY_COMPARE(application->projectRoot(0), canonical(show));
        QVERIFY(titles(0).isEmpty());

        const QString loose = writeAss(dir.filePath(QStringLiteral("loose.ass")), "loose");
        forwarded->take({{loose}, {}, application::WindowRequest::New});
        QTRY_VERIFY(shell(1) && shell(1)->isVisible());
        QTRY_COMPARE(titles(1), QStringList{QStringLiteral("loose.ass")});
        QVERIFY(titles(0).isEmpty());

        const QString extras = folder("extras");
        const QString ncop = writeAss(extras + QStringLiteral("/ncop.ass"), "ncop");
        forwarded->take({{extras, ncop}, {}, application::WindowRequest::Default});
        QTRY_VERIFY(shell(2) && shell(2)->isVisible());
        QTRY_COMPARE(titles(2), QStringList{QStringLiteral("ncop.ass")});
        QCOMPARE(application->projectRoot(2), canonical(extras));
        QCOMPARE(titles(1), QStringList{QStringLiteral("loose.ass")});
    }

    void eachWindowSpellsWithItsProjectsDictionary()
    {
        for (const char *language : {"en_US", "en_GB"}) {
            writeAll(home.filePath(QStringLiteral("Dictionary/%1.aff").arg(QLatin1String(language))), {});
            writeAll(home.filePath(QStringLiteral("Dictionary/%1.dic").arg(QLatin1String(language))),
                     language == std::string_view("en_US") ? "2\nHello\ncolor\n" : "2\nHello\ncolour\n");
        }
        start(fakes::FakeSpelling::loader());
        const QString american = folder("american");
        const QString british = folder("british", true);
        writeAll(british + QStringLiteral("/.hikari/project.toml"),
                 "version = 1\n# The show is British.\n[editor]\ndictionaryLanguage = \"en_GB\"\n");

        QCOMPARE(application->openFolder(american, 0).value(QStringLiteral("window")).toInt(), 0);
        QVERIFY(application->openFile(writeAss(american + QStringLiteral("/us.ass"), "Hello colr")));
        const int other = application->openFolder(british, 0).value(QStringLiteral("window")).toInt();
        QVERIFY(other != 0);
        QTRY_VERIFY(shell(other) && shell(other)->isVisible());
        application->activateShellWindow(other);
        QTRY_COMPARE(application->workspace().activeWindow(), other);
        QVERIFY(application->openFile(writeAss(british + QStringLiteral("/gb.ass"), "Hello colr")));
        QCOMPARE(titles(other), QStringList{QStringLiteral("gb.ass")});

        QCOMPARE(application->dictionaryLanguage(), QStringLiteral("en_GB"));
        QCOMPARE(application->settingsStore()->text("editor.dictionaryLanguage"), QStringLiteral("en_US"));
        QTRY_COMPARE(marks(other), (QVariantList{6, 9}));
        QTRY_COMPARE(marks(0), (QVariantList{6, 9}));
        const auto firstSuggestion = [&] {
            return application->editorMisspellAt(0, 7).value(QStringLiteral("suggestions")).toStringList().value(0);
        };
        QTRY_COMPARE(firstSuggestion(), QStringLiteral("colour"));

        application->activateShellWindow(0);
        QTRY_COMPARE(application->dictionaryLanguage(), QStringLiteral("en_US"));
        QTRY_COMPARE(firstSuggestion(), QStringLiteral("color"));
        QCOMPARE(marks(other), (QVariantList{6, 9}));
    }

    void aProjectsMissingDictionaryLeavesMySpellingOn()
    {
        writeAll(home.filePath(QStringLiteral("Dictionary/en_US.aff")), {});
        writeAll(home.filePath(QStringLiteral("Dictionary/en_US.dic")), "1\nHello\n");
        start(fakes::FakeSpelling::loader());
        const QString show = folder("show", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"), "version = 1\n[editor]\ndictionaryLanguage = \"pl_PL\"\n");
        application->openFolder(show, 0);
        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep01.ass"), "Hello wrold")));
        QCOMPARE(application->dictionaryLanguage(), QStringLiteral("pl_PL"));
        QVERIFY(application->settingsStore()->boolean("editor.spellchecker"));
    }

    void saveWritesScriptInfoFromTheWindowsProject()
    {
        start();
        myScriptInfoDefaults();
        const QString loose = folder("loose");
        const QString show = folder("show", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"), "version = 1\n[scriptProperties]\ntranslation = \"Team\"\n");
        application->openFolder(loose, 0);
        const QString mine = writeAss(loose + QStringLiteral("/mine.ass"), "a");
        QVERIFY(application->openFile(mine));
        const auto mineId = application->workspace().editingTarget()->value;
        QVERIFY(openProjectWindow(show) > 0);
        const QString team = writeAss(show + QStringLiteral("/ep01.ass"), "a");
        QVERIFY(application->openFile(team));

        QVERIFY(application->saveDocument(application->workspace().editingTarget()->value));
        QVERIFY(application->saveDocument(mineId));
        application->waitForWrites();
        QVERIFY2(readAll(team).contains("Title: Show\n"), readAll(team));
        QVERIFY2(readAll(team).contains("Original Translation: Team\n"), readAll(team));
        QVERIFY(!readAll(team).contains("Original Timing"));
        QVERIFY2(readAll(mine).contains("Original Translation: Mine\n"), readAll(mine));
    }

    void askForChangeShowsTheChangesBeforeSaving()
    {
        start();
        myScriptInfoDefaults();
        application->settingsStore()->setValue(QStringLiteral("scriptProperties.askForChange"), true);
        const QString show = folder("asked", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"), "version = 1\n[scriptProperties]\ntranslation = \"Team\"\n");
        QVERIFY(openProjectWindow(show) >= 0);
        const QString path = writeAss(show + QStringLiteral("/ep02.ass"), "a");
        QVERIFY(application->openFile(path));
        QQuickWindow *from = shell(application->workspace().activeWindow());
        const auto save = [&] {
            return QMetaObject::invokeMethod(from, "saveSubtitles", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
        };
        const auto answer = [&](const char *button) {
            QQuickWindow *asked = dialog("scriptInfoOnSaveDialog");
            QObject *b = asked ? asked->findChild<QObject *>(QLatin1String(button)) : nullptr;
            return b && QMetaObject::invokeMethod(b, "clicked");
        };

        QVERIFY(save());
        QTRY_VERIFY(dialog("scriptInfoOnSaveDialog"));
        QCOMPARE(dialog("scriptInfoOnSaveDialog")->property("rows").toList().size(), 2);
        QVERIFY(answer("scriptInfoOnSaveCancel"));
        QTRY_VERIFY(!dialog("scriptInfoOnSaveDialog"));
        application->waitForWrites();
        QVERIFY(!readAll(path).contains("Title: Show"));

        QVERIFY(save());
        QTRY_VERIFY(dialog("scriptInfoOnSaveDialog"));
        QVERIFY(answer("scriptInfoOnSaveSkip"));
        QTRY_VERIFY(!dialog("scriptInfoOnSaveDialog"));
        application->waitForWrites();
        QTRY_VERIFY(readAll(path).contains("Dialogue"));
        QVERIFY(!readAll(path).contains("Title: Show"));
        QVERIFY(application->settingsStore()->value(QStringLiteral("scriptProperties.askForChange")).toBool());

        QVERIFY(save());
        QTRY_VERIFY(dialog("scriptInfoOnSaveDialog"));
        QQuickItem *title = item(dialog("scriptInfoOnSaveDialog")->contentItem(), QStringLiteral("scriptInfoOnSaveField0"));
        QVERIFY(title);
        title->setProperty("checked", false);
        QVERIFY(QMetaObject::invokeMethod(title, "toggled"));
        QVERIFY(answer("scriptInfoOnSaveOk"));
        QTRY_VERIFY(!dialog("scriptInfoOnSaveDialog"));
        application->waitForWrites();
        QTRY_VERIFY2(readAll(path).contains("Original Translation: Team\n"), readAll(path));
        QVERIFY(!readAll(path).contains("Title: Show"));
    }

    void theProjectTabWritesOnlyProjectToml()
    {
        start();
        application->settingsStore()->setValue(QStringLiteral("editor.spellchecker"), true);
        const QString show = folder("tabbed", true);
        const QByteArray local = "version = 1\n[grammar]\nlanguage = 2\n";
        writeAll(show + QStringLiteral("/.hikari/project.toml"), "version = 1\n# Shared by the team.\n[editor]\nspellchecker = false\n");
        writeAll(show + QStringLiteral("/.hikari/project.local.toml"), local);
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        QVERIFY(!application->projectNeedsTrust(w));
        QVERIFY(QMetaObject::invokeMethod(shell(w), "openSettings", Q_ARG(QVariant, QStringLiteral("spelling")),
                                          Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(dialog("settingsDialog"));
        QQuickWindow *settings = dialog("settingsDialog");
        QQuickItem *content = settings->contentItem();
        const auto shown = [&](const char *name) {
            QQuickItem *found = item(content, QLatin1String(name));
            return found && found->isVisible();
        };
        const auto text = [&](const char *name) { return item(content, QLatin1String(name))->property("text").toString(); };
        QVERIFY(shown("settingsScope"));
        QTRY_VERIFY(shown("overridden_editor.spellchecker"));
        QVERIFY(shown("overridden_grammar.language"));
        QVERIFY(!shown("overridden_editor.dictionaryLanguage"));

        settings->setProperty("projectTab", true);
        QTRY_VERIFY(shown("projectSettings"));
        QCOMPARE(text("projectState_editor.spellchecker"), QStringLiteral("Set in this project"));
        QCOMPARE(text("projectState_grammar.language"), QStringLiteral("Set in project.local.toml"));
        QVERIFY(!item(content, QStringLiteral("project_grammar.language"))->isEnabled());
        QCOMPARE(text("projectState_scriptProperties.title"), QStringLiteral("Uses yours"));
        QQuickItem *pages = item(content, QStringLiteral("projectSettings"));
        QVERIFY(QMetaObject::invokeMethod(pages, "put", Q_ARG(QVariant, QStringLiteral("scriptProperties.title")),
                                          Q_ARG(QVariant, QStringLiteral("Show"))));
        QVERIFY(QMetaObject::invokeMethod(pages, "put", Q_ARG(QVariant, QStringLiteral("scriptProperties.titleOn")),
                                          Q_ARG(QVariant, true)));
        QVERIFY(QMetaObject::invokeMethod(pages, "useYours", Q_ARG(QVariant, QStringLiteral("editor.spellchecker"))));
        QVERIFY(QMetaObject::invokeMethod(settings, "apply"));

        QByteArray shared = readAll(show + QStringLiteral("/.hikari/project.toml"));
        QVERIFY2(shared.contains("# Shared by the team."), shared);
        QVERIFY2(!shared.contains("spellchecker"), shared);
        QVERIFY2(shared.contains("title = \"Show\""), shared);
        QVERIFY2(shared.contains("titleOn = true"), shared);
        QCOMPARE(readAll(show + QStringLiteral("/.hikari/project.local.toml")), local);
        const QStringList overrides = application->projectOverrides(w);
        QVERIFY(overrides.contains(QStringLiteral("scriptProperties.title")));
        QVERIFY(overrides.contains(QStringLiteral("grammar.language")));
        QVERIFY(!overrides.contains(QStringLiteral("editor.spellchecker")));
        QCOMPARE(text("projectState_scriptProperties.title"), QStringLiteral("Set in this project"));

        QVERIFY(QMetaObject::invokeMethod(pages, "put", Q_ARG(QVariant, QStringLiteral("editor.spellchecker")),
                                          Q_ARG(QVariant, false)));
        QVERIFY(QMetaObject::invokeMethod(settings, "apply"));
        shared = readAll(show + QStringLiteral("/.hikari/project.toml"));
        QVERIFY2(shared.contains("spellchecker = false"), shared);
        QVERIFY(application->settingsStore()->value(QStringLiteral("editor.spellchecker")).toBool());
    }

    void aProjectWithScriptsAsksForTrust()
    {
        start();
        const QString show = folder("scripted", true);
        QDir().mkpath(show + QStringLiteral("/.hikari/automation"));
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        QObject *bar = named(shell(w), "projectTrustBar");
        QVERIFY(bar);
        QTRY_VERIFY(bar->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named(shell(w), "projectTrustNotNow"), "clicked"));
        QTRY_VERIFY(!bar->property("visible").toBool());
        QVERIFY(application->projectNeedsTrust(w));
        bar->setProperty("dismissed", QString());
        QTRY_VERIFY(bar->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named(shell(w), "projectTrust"), "clicked"));
        QTRY_VERIFY(!bar->property("visible").toBool());
        QVERIFY(!application->projectNeedsTrust(w));
        QVERIFY(application->settingsStore()->value(QStringLiteral("trust.folders")).toStringList().contains(
            QDir::toNativeSeparators(canonical(show))));

        const QString plugged = folder("plugged", true);
        writeAll(plugged + QStringLiteral("/.hikari/project.toml"), "version = 1\n[plugins]\nsample = true\n");
        const int other = openProjectWindow(plugged);
        QVERIFY(other > 0);
        QVERIFY(application->projectNeedsTrust(other));
    }

    void projectScriptsLoadOnlyOnceTrustedAndOnlyInTheirWindow()
    {
        start();
        const QString show = folder("autoloaded", true);
        writeAll(show + QStringLiteral("/.hikari/automation/autoload/fx.lua"), "script_name = 'fx'\n");
        const int w = openProjectWindow(show);
        QVERIFY(w > 0);
        const QString script =
            QDir::toNativeSeparators(canonical(show) + QStringLiteral("/.hikari/automation/autoload/fx.lua"));
        auto &automation = application->automation();
        QTRY_COMPARE(automation.lockedScripts(w), QStringList{script});
        QVERIFY(!automation.autoloadScripts().contains(script));

        application->trustProject(w);
        QTRY_VERIFY(automation.lockedScripts(w).isEmpty());
        QVERIFY(automation.autoloadScripts().contains(script));
        QVERIFY(automation.shownIn(script, w));
        QVERIFY(!automation.shownIn(script, 0));

        closeShell(w);
        QTRY_VERIFY(!automation.autoloadScripts().contains(script));
    }

    void aTrustedProjectOffersItsMissingPlugins()
    {
        start();
        const QString show = folder("plugins", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"),
                 "version = 1\n[plugins]\nenabled = [\"not-installed\"]\n[plugins.\"kf\"]\nsize = 3\n");
        const int w = openProjectWindow(show);
        QVERIFY(w > 0);
        QVariantMap plugins = application->projectPlugins(w);
        QCOMPARE(plugins.value(QStringLiteral("trusted")).toBool(), false);
        QCOMPARE(plugins.value(QStringLiteral("named")).toStringList(), (QStringList{"not-installed", "kf"}));
        QVERIFY(plugins.value(QStringLiteral("missing")).toStringList().isEmpty());

        application->trustProject(w);
        QTRY_VERIFY(application->projectPlugins(w).value(QStringLiteral("trusted")).toBool());
        plugins = application->projectPlugins(w);
        QCOMPARE(plugins.value(QStringLiteral("missing")).toStringList(), QStringList{"not-installed"});
        QVERIFY(application->projectPlugins(0).isEmpty());
    }

    void projectFontFoldersJoinTheWindowsFonts()
    {
        start();
        const QString show = folder("fonted", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"), "version = 1\nfonts = [\"fonts\", \"../missing-fonts\"]\n");
        QDir().mkpath(show + QStringLiteral("/fonts"));
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_ATTACHMENT_FIXTURES "/HikariAttachedTest.ttf"),
                            show + QStringLiteral("/fonts/HikariAttachedTest.ttf")));
        auto &catalogs = application->fontCatalogs();
        QSignalSpy answers(&catalogs, &app::FontCatalogsController::resolutionReady);
        const auto resolved = [&] {
            catalogs.resolveFamily(QStringLiteral("HikariAttachedTest"), false, false, true, QStringLiteral("Abc"));
            return catalogs.waitResolved() && !answers.isEmpty() ? answers.takeLast().at(1).toMap() : QVariantMap();
        };
        QVERIFY(resolved().value(QStringLiteral("kind")).toString() != QLatin1String("requested"));

        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        const QVariantMap result = resolved();
        QCOMPARE(result.value(QStringLiteral("kind")).toString(), QStringLiteral("requested"));
        QVERIFY2(result.value(QStringLiteral("file")).toString().endsWith(QStringLiteral("HikariAttachedTest.ttf")),
                 qPrintable(result.value(QStringLiteral("file")).toString()));

        auto &collector = application->fontCollector();
        collector.open();
        QCOMPARE(collector.directory(), QDir::toNativeSeparators(canonical(show) + QStringLiteral("/fonts")));
    }

    void recentProjectsDropMissingFolders()
    {
        start();
        const QString kept = folder("kept");
        const QString gone = folder("gone");
        application->openFolder(gone, 0);
        application->openFolder(kept, 0, 2);
        QCOMPARE(application->homeRecentProjects().size(), 2);
        QVERIFY(QDir(gone).removeRecursively());
        const QVariantList rows = application->homeRecentProjects();
        QCOMPARE(rows.first().toMap().value(QStringLiteral("path")).toString(), canonical(kept));
        QVERIFY(rows.last().toMap().value(QStringLiteral("missing")).toBool());
        application->removeMissingRecentProjects();
        QCOMPARE(application->homeRecentProjects().size(), 1);
        application->removeRecentProject(canonical(kept));
        QVERIFY(application->homeRecentProjects().isEmpty());
    }
};

QTEST_MAIN(ProjectWindowsTests)
#include "project_windows_tests.moc"
