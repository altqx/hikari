#include "hikari/app/application.h"
#include "hikari/app/font_catalogs_controller.h"
#include "hikari/app/font_collector_controller.h"
#include "hikari/app/forwarded_launches.h"
#include "hikari/app/project_windows.h"
#include "hikari/app/session_json.h"
#include "hikari/app/skills_settings.h"
#include "hikari/api/skill_check.h"
#include "hikari/backends/desktop_handoff.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/application/owned_file.h"
#include "hikari/application/session_file.h"
#include "docking.h"
#include "hikari/application/all_tags.h"
#include "hikari/application/shape_presets.h"
#include "list_files.h"
#include "line_table_model.h"
#include "settings_store.h"
#include "shell_controller.h"
#include "shift_times_controller.h"
#include "spelling/fake_spelling.h"
#include "visual_tools_controller.h"
#include "workspace_layout.h"

#include "qtest_parts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

QByteArray catalogLine(const char *style)
{
    return QByteArray("\xEF\xBB\xBFStyle: ") + style + ",Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\r\n";
}
QStringList rowKeys(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &row : rows) {
        const QVariantMap m = row.toMap();
        out << m.value(QStringLiteral("tier")).toString() + QLatin1Char(':') + m.value(QStringLiteral("name")).toString()
                   + (m.value(QStringLiteral("hidden")).toBool() ? QStringLiteral(" (hidden)") : QString());
    }
    return out;
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
    QString userFolder;

    void start(application::SpellingBackendLoader spelling = {})
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = std::move(spelling);
        o.userFolder = userFolder;
        application = new app::Application(o);
        forwarded = new app::ForwardedLaunches(*application);
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first(), "buildMenus"));
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

    static QVariantList rowsOf(const QVariantList &groups, const QString &group)
    {
        for (const QVariant &g : groups)
            if (g.toMap().value(QStringLiteral("group")).toString() == group)
                return g.toMap().value(QStringLiteral("rows")).toList();
        return {};
    }
    static QStringList summary(const QVariantList &rows)
    {
        QStringList out;
        for (const QVariant &r : rows) {
            const QVariantMap m = r.toMap();
            const QString state = !m.value(QStringLiteral("problem")).toString().isEmpty() ? QStringLiteral("broken")
                                  : !m.value(QStringLiteral("hidden")).toString().isEmpty() ? QStringLiteral("hidden")
                                                                                             : QStringLiteral("offered");
            out << m.value(QStringLiteral("name")).toString() + u':' + state;
        }
        return out;
    }
    static QByteArray skillMd(const char *description) { return QByteArray("---\ndescription: ") + description + "\n---\n"; }

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
                                 "gone.hksession", "fonts.toml", "layouts.json", "layout.json", "shapes.toml", "tags.toml"})
            QFile::remove(home.filePath(QLatin1String(name)));
        QDir(home.filePath(QStringLiteral("projects"))).removeRecursively();
        QDir(home.filePath(QStringLiteral("Dictionary"))).removeRecursively();
        QDir(home.filePath(QStringLiteral("Catalog"))).removeRecursively();
        QDir(dir.path()).removeRecursively();
        QDir().mkpath(dir.path());
        if (!userFolder.isEmpty())
            QDir(userFolder).removeRecursively();
        userFolder.clear();
    }

    void theSkillsListIsTheWindowsProjectOverMineOverBuiltIn()
    {
        userFolder = home.filePath(QStringLiteral("user"));
        start();
        QCOMPARE(application->userFolder(), QDir::toNativeSeparators(userFolder));
        const QString mine = userFolder + QStringLiteral("/ai/skills");
        writeAll(mine + QStringLiteral("/signs/SKILL.md"), skillMd("Mine."));
        writeAll(mine + QStringLiteral("/typesetting/SKILL.md"), skillMd("Our way."));
        writeAll(mine + QStringLiteral("/notes/SKILL.md"), "no frontmatter");
        const QString show = folder("Show S02", true);
        writeAll(show + QStringLiteral("/.hikari/skills/signs/SKILL.md"), skillMd("The show's signs."));
        writeAll(show + QStringLiteral("/.hikari/skills/honorifics/SKILL.md"), skillMd("Honorifics."));
        app::SkillsSettings &skills = *application->skillsSettings();

        QVariantList groups = skills.groups(0);
        QCOMPARE(groups.size(), 2);
        QCOMPARE(summary(rowsOf(groups, QStringLiteral("mine"))),
                 (QStringList{"notes:broken", "signs:offered", "typesetting:offered"}));
        QCOMPARE(summary(rowsOf(groups, QStringLiteral("builtin"))),
                 (QStringList{"karaoke-templater:offered", "translation:offered", "typesetting:hidden"}));
        QCOMPARE(rowsOf(groups, QStringLiteral("mine")).first().toMap().value(QStringLiteral("problem")).toString(),
                 QStringLiteral("SKILL.md doesn’t start with frontmatter (---)."));

        QVERIFY(application->openFolder(show, 0).value(QStringLiteral("ok")).toBool());
        groups = skills.groups(0);
        QCOMPARE(groups.size(), 3);
        QCOMPARE(groups.first().toMap().value(QStringLiteral("group")).toString(), QStringLiteral("project"));
        QCOMPARE(groups.first().toMap().value(QStringLiteral("folder")).toString(),
                 QDir::toNativeSeparators(QStringLiteral("Show S02/.hikari/skills")));
        QCOMPARE(summary(rowsOf(groups, QStringLiteral("project"))), (QStringList{"honorifics:offered", "signs:offered"}));
        QCOMPARE(summary(rowsOf(groups, QStringLiteral("mine"))),
                 (QStringList{"notes:broken", "signs:hidden", "typesetting:offered"}));
    }

    void theBuiltInSkillsAreTheSidecarsOwn()
    {
        QStringList source;
        const QDir skills(QStringLiteral(HIKARI_AI_SKILLS_SOURCE));
        for (const QString &name : skills.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QFile file(skills.filePath(name + QStringLiteral("/SKILL.md")));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray text = file.readAll();
            const api::SkillCheck check = api::checkSkill(name.toStdString(), std::string_view(text.data(), text.size()));
            QCOMPARE(int(check.problem), int(api::SkillProblem::None));
            source << name + u'=' + QString::fromStdString(check.description);
        }
        QStringList compiled;
        for (const app::BuiltinSkill &skill : app::builtinSkills())
            compiled << QString::fromStdString(skill.name) + u'=' + QString::fromStdString(skill.description);
        QVERIFY(!source.isEmpty());
        QCOMPARE(compiled, source);
    }

    void openProjectFolderMakesTheProjectsSkillsFolder()
    {
        start();
        QList<QUrl> opened;
        auto routes = backends::desktop::replaceRoutes({[&](const QUrl &url) { opened << url; return true; }, {}, {}});
        auto restore = qScopeGuard([&] { backends::desktop::replaceRoutes(routes); });
        app::SkillsSettings &skills = *application->skillsSettings();
        QVERIFY(!skills.openProjectFolder(0));
        const QString show = folder("Show S03", true);
        QVERIFY(application->openFolder(show, 0).value(QStringLiteral("ok")).toBool());
        QVERIFY(skills.openProjectFolder(0));
        QVERIFY(QFileInfo(show + QStringLiteral("/.hikari/skills")).isDir());
        QCOMPARE(opened, QList<QUrl>{QUrl::fromLocalFile(canonical(show) + QStringLiteral("/.hikari/skills"))});
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

    void aProjectsWordsSpellOnlyInItsWindowsAndAddToDictionaryGoesThere()
    {
        writeAll(home.filePath(QStringLiteral("Dictionary/en_US.aff")), {});
        writeAll(home.filePath(QStringLiteral("Dictionary/en_US.dic")), "1\nHello\n");
        start(fakes::FakeSpelling::loader());
        const QString plain = folder("plain");
        const QString show = folder("show", true);
        const QString words = show + QStringLiteral("/.hikari/dictionary.txt");
        writeAll(words, "Haruhi\n");
        QCOMPARE(application->openFolder(plain, 0).value(QStringLiteral("window")).toInt(), 0);
        QVERIFY(application->openFile(writeAss(plain + QStringLiteral("/mine.ass"), "Hello Haruhi Kyon")));
        QTRY_COMPARE(marks(0).size(), 4);
        const int w = openProjectWindow(show);
        QVERIFY(w > 0);
        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep01.ass"), "Hello Haruhi Kyon")));
        QTRY_COMPARE(marks(w).size(), 2);

        QVERIFY(application->addEditorWord(QStringLiteral("Kyon"), true));
        QCOMPARE(readAll(words), QByteArray("Haruhi\nKyon\n"));
        QVERIFY(!application->addedDictionaryWords().contains(QStringLiteral("Kyon")));
        QTRY_VERIFY(marks(w).isEmpty());
        QCOMPARE(marks(0).size(), 4);

        auto *menu = shell(w)->findChild<QObject *>(QStringLiteral("lineTextMenu"));
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
        QCOMPARE(menu->property("dictionaryTier").toString(), QStringLiteral("project"));
        QVERIFY(QMetaObject::invokeMethod(menu, "addWord", Q_ARG(QVariant, QStringLiteral("Itsuki")),
                                          Q_ARG(QVariant, QStringLiteral("mine"))));
        QVERIFY(application->addedDictionaryWords().contains(QStringLiteral("Itsuki")));
        QVERIFY(!readAll(words).contains("Itsuki"));
        QVERIFY(application->settingsStore()->list("workspace.saveTiers").contains(QStringLiteral("dictionary\tmine")));
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
        QCOMPARE(menu->property("dictionaryTier").toString(), QStringLiteral("mine"));
    }

    void theExportDefaultComesAfterTheFilesOwnChoiceAndBeforeTheLastExport()
    {
        start();
        const QString plain = folder("plain");
        const QString show = folder("show", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"),
                 "version = 1\n[export]\nformat = \"vtt\"\nmergeOverlaps = true\n");
        const QString exported = show + QStringLiteral("/done.ass");
        application->settingsStore()->setValue(
            QStringLiteral("export.choices"),
            QStringList{QFileInfo(exported).absoluteFilePath() + QStringLiteral("\t{\"format\":\"mdvd\",\"fps\":25}"),
                        QStringLiteral("\t{\"format\":\"sbv\",\"lines\":\"selected\"}")});
        const auto setup = [&] { return application->exportSetup(); };
        const auto format = [&] { return setup().value(QStringLiteral("format")).toString(); };
        const auto option = [&](const char *name) {
            return setup().value(QStringLiteral("options")).toMap().value(QLatin1String(name));
        };

        QCOMPARE(application->openFolder(plain, 0).value(QStringLiteral("window")).toInt(), 0);
        QVERIFY(application->openFile(writeAss(plain + QStringLiteral("/new.ass"), "one")));
        QCOMPARE(format(), QStringLiteral("sbv"));

        const int w = openProjectWindow(show);
        QVERIFY(w > 0);
        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep01.ass"), "one")));
        QCOMPARE(format(), QStringLiteral("vtt"));
        QVERIFY(option("mergeOverlaps").toBool());
        QCOMPARE(option("lines").toString(), QStringLiteral("selected"));
        QVERIFY(application->openFile(writeAss(exported, "one")));
        const QVariantList tabs = application->tabs();
        for (int i = 0; i < tabs.size(); ++i)
            if (tabs[i].toMap().value(QStringLiteral("title")).toString().contains(QStringLiteral("done.ass")))
                application->selectTab(i);
        QTRY_COMPARE(setup().value(QStringLiteral("name")).toString(), QStringLiteral("done"));
        QCOMPARE(format(), QStringLiteral("mdvd"));
        QCOMPARE(option("fps").toDouble(), 25.0);
        QVERIFY(!readAll(show + QStringLiteral("/.hikari/project.toml")).contains("mdvd"));
    }

    void theProjectsTargetLanguageIsInlineTranslatesDefault()
    {
        start();
        application->settingsStore()->setValue(QStringLiteral("ai.inline.translateTo"), QStringLiteral("German"));
        const QString show = folder("show", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"),
                 "version = 1\n[translation]\nsourceLanguage = \"Japanese\"\ntargetLanguage = \"Polish\"\n");
        const QString plain = folder("plain");
        QCOMPARE(application->openFolder(plain, 0).value(QStringLiteral("window")).toInt(), 0);
        QVERIFY(application->openFile(writeAss(plain + QStringLiteral("/mine.ass"), "one")));
        QCOMPARE(application->inlineAi()->translateTo(), QStringLiteral("German"));
        const int w = openProjectWindow(show);
        QVERIFY(w > 0);
        QCOMPARE(application->inlineAi()->translateTo(), QStringLiteral("Polish"));
        QVERIFY(application->projectOverrides(w).contains(QStringLiteral("translation.sourceLanguage")));
        application->activateShellWindow(0);
        QTRY_COMPARE(application->workspace().activeWindow(), 0);
        QCOMPARE(application->inlineAi()->translateTo(), QStringLiteral("German"));
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
        QVERIFY(QMetaObject::invokeMethod(shell(w), "openSettings", Q_ARG(QVariant, QStringLiteral("subtitleProperties")),
                                          Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(shown("overridden_scriptProperties.title"));
        QVERIFY(!shown("overridden_scriptProperties.script"));
        QVERIFY(!shown("overridden_scriptProperties.askForChange"));
        settings->setProperty("projectTab", true);

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

    void aProjectsOwnAiKeysWaitForTrust()
    {
        qputenv("HIKARI_PJ10_TEST_KEY", "sk-from-env");
        start();
        const QString show = folder("keyed", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"),
                 "version = 1\n[ai.keys]\nopenai = { env = \"HIKARI_PJ10_TEST_KEY\", entry = \"openai\" }\n");
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        const QString root = application->projectRoot(w);
        QCOMPARE(application->projectTrustReasons(w), QStringList{QStringLiteral("keys")});
        QObject *bar = named(shell(w), "projectTrustBar");
        QTRY_VERIFY(bar->property("visible").toBool());
        QVERIFY(bar->property("text").toString().contains(QStringLiteral("AI keys")));
        QVERIFY(!application->projectAiKey(root, "openai"));
        auto row = application->projectAiKeys(w).value(QStringLiteral("rows")).toList().value(0).toMap();
        QCOMPARE(row.value(QStringLiteral("id")).toString(), QStringLiteral("openai"));
        QCOMPARE(row.value(QStringLiteral("used")).toString(), QStringLiteral("untrusted"));

        QVERIFY(QMetaObject::invokeMethod(named(shell(w), "projectTrust"), "clicked"));
        QTRY_VERIFY(!bar->property("visible").toBool());
        QCOMPARE(application->projectAiKey(root, "openai"), std::optional<std::string>("sk-from-env"));
        QVERIFY(!application->projectAiKey(root, "anthropic"));
        row = application->projectAiKeys(w).value(QStringLiteral("rows")).toList().value(0).toMap();
        QCOMPARE(row.value(QStringLiteral("used")).toString(), QStringLiteral("env"));
        QCOMPARE(row.value(QStringLiteral("from")).toString(), QStringLiteral("HIKARI_PJ10_TEST_KEY"));
        for (const QVariant &value : row)
            QVERIFY(!value.toString().contains(QStringLiteral("sk-from-env")));

        qunsetenv("HIKARI_PJ10_TEST_KEY");
        writeAll(show + QStringLiteral("/.hikari/credentials.toml"), "version = 1\n[keys]\nopenai = \"sk-from-file\"\n");
        QCOMPARE(application->projectAiKey(root, "openai"), std::optional<std::string>("sk-from-file"));
    }

    void aNamedKeyNobodyHasAsksNoTrust()
    {
        start();
        const QString show = folder("unkeyed", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"), "version = 1\n[ai.keys]\nopenai = { entry = \"openai\" }\n");
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        QVERIFY(!application->projectNeedsTrust(w));
        QVERIFY(!named(shell(w), "projectTrustBar")->property("visible").toBool());
        QCOMPARE(application->projectAiKeys(w).value(QStringLiteral("rows")).toList().value(0).toMap()
                     .value(QStringLiteral("used")).toString(), QStringLiteral("untrusted"));
    }

    void theCredentialsFileOpensThroughTheDesktopHandoff()
    {
        start();
        const QString show = folder("credentialed", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"), "version = 1\n");
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        QList<QUrl> opened;
        auto routes = backends::desktop::replaceRoutes({[&](const QUrl &url) { opened << url; return true; }, {}, {}});
        auto restore = qScopeGuard([&] { backends::desktop::replaceRoutes(routes); });
        application->openProjectCredentials(w);
        QVERIFY(opened.isEmpty());
        const QString credentials = show + QStringLiteral("/.hikari/credentials.toml");
        writeAll(credentials, "version = 1\n[keys]\nopenai = \"sk-handed-off\"\n");
        application->openProjectCredentials(w);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(QFileInfo(opened.first().toLocalFile()).canonicalFilePath(), QFileInfo(credentials).canonicalFilePath());
    }

    void aKeyInProjectTomlIsIgnoredAndMovesToTheCredentialsFile()
    {
        start();
        const QString show = folder("leaky", true);
        const QString shared = show + QStringLiteral("/.hikari/project.toml");
        writeAll(shared, "version = 1\n# Shared by the team.\n[ai.keys]\nopenrouter = \"sk-or-leaked\"\n");
        application->settingsStore()->setValue(QStringLiteral("trust.folders"),
                                               QStringList{QDir::toNativeSeparators(canonical(show))});
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        const QString root = application->projectRoot(w);
        QCOMPARE(application->projectKeyValuesFound(w), QStringList{QStringLiteral("openrouter")});
        QVERIFY(!application->projectAiKey(root, "openrouter"));
        QObject *bar = named(shell(w), "projectKeyValuesBar");
        QTRY_VERIFY(bar->property("visible").toBool());
        QVERIFY(!bar->property("text").toString().contains(QStringLiteral("sk-or")));

        QVERIFY(application->moveProjectKeysToCredentials(w));
        const QByteArray after = readAll(shared);
        QVERIFY2(!after.contains("sk-or-leaked"), after);
        QVERIFY2(after.contains("# Shared by the team."), after);
        QVERIFY2(after.contains("openrouter = { entry = \"openrouter\" }"), after);
        QVERIFY(readAll(show + QStringLiteral("/.hikari/credentials.toml")).contains("openrouter = \"sk-or-leaked\""));
        QVERIFY(readAll(show + QStringLiteral("/.hikari/.gitignore")).contains("credentials.toml"));
        QVERIFY(application->projectKeyValuesFound(w).isEmpty());
        QTRY_VERIFY(!bar->property("visible").toBool());
        QCOMPARE(application->projectAiKey(root, "openrouter"), std::optional<std::string>("sk-or-leaked"));
    }

    void createCredentialsFileFromTheProjectAiPage()
    {
        start();
        const QString show = folder("fresh", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"), "version = 1\n[ai.keys]\nanthropic = { entry = \"anthropic\" }\n");
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        QVERIFY(QMetaObject::invokeMethod(shell(w), "openProjectSettingsPage", Q_ARG(QVariant, QStringLiteral("ai"))));
        QTRY_VERIFY(dialog("settingsDialog"));
        QQuickItem *content = dialog("settingsDialog")->contentItem();
        QTRY_VERIFY(item(content, QStringLiteral("projectAiKeys")) && item(content, QStringLiteral("projectAiKeys"))->isVisible());
        QQuickItem *create = item(content, QStringLiteral("projectCredentialsCreate"));
        QVERIFY(create && create->isVisible());
        QVERIFY(QMetaObject::invokeMethod(create, "clicked"));
        QTRY_VERIFY(!create->isVisible());
        QVERIFY(readAll(show + QStringLiteral("/.hikari/credentials.toml")).contains("anthropic = \"\""));
        QCOMPARE(readAll(show + QStringLiteral("/.hikari/.gitignore")), QByteArray("credentials.toml\nproject.local.toml\n"));
        QVERIFY(!QFileInfo::exists(show + QStringLiteral("/.gitignore")));
        QVERIFY(item(content, QStringLiteral("projectCredentialsOpen"))->isVisible());
    }

    void projectScriptsLoadOnlyOnceTrustedAndOnlyInTheirWindow()
    {
        start();
        const QString show = folder("autoloaded", true);
        writeAll(show + QStringLiteral("/.hikari/automation/autoload/fx.lua"), "script_name = 'fx'\n");
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        const QString script =
            QDir::toNativeSeparators(canonical(show) + QStringLiteral("/.hikari/automation/autoload/fx.lua"));
        auto &automation = application->automation();
        QTRY_COMPARE(automation.lockedScripts(w), QStringList{script});
        QVERIFY(!automation.autoloadScripts().contains(script));

        application->trustProject(w);
        QTRY_VERIFY(automation.lockedScripts(w).isEmpty());
        QVERIFY(automation.autoloadScripts().contains(script));
        QVERIFY(automation.shownIn(script, w));
        QCOMPARE(automation.tierOf(script, w), QStringLiteral("project"));
        const int other = openProjectWindow(folder("plain", true));
        QVERIFY(other >= 0 && other != w);
        QVERIFY(!automation.shownIn(script, other));
        QVERIFY(automation.tierOf(script, other).isEmpty());

        QVERIFY(application->reviewCloseProject(w).isEmpty());
        application->finishClose();
        QTRY_VERIFY(!automation.autoloadScripts().contains(script));
    }

    void aTrustedProjectOffersItsMissingPlugins()
    {
        start();
        const QString show = folder("plugins", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"),
                 "version = 1\n[plugins]\nenabled = [\"not-installed\"]\n[plugins.\"kf\"]\nsize = 3\n");
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        QVariantMap plugins = application->projectPlugins(w);
        QCOMPARE(plugins.value(QStringLiteral("trusted")).toBool(), false);
        QCOMPARE(plugins.value(QStringLiteral("named")).toStringList(), (QStringList{"not-installed", "kf"}));
        QVERIFY(plugins.value(QStringLiteral("missing")).toStringList().isEmpty());

        application->trustProject(w);
        QTRY_VERIFY(application->projectPlugins(w).value(QStringLiteral("trusted")).toBool());
        plugins = application->projectPlugins(w);
        QCOMPARE(plugins.value(QStringLiteral("missing")).toStringList(), QStringList{"not-installed"});
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
        const QString family = QStringLiteral("HikariAttachedTest");
        QVERIFY(!catalogs.allFonts(0).contains(family));

        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        const QVariantMap result = resolved();
        QCOMPARE(result.value(QStringLiteral("kind")).toString(), QStringLiteral("requested"));
        QVERIFY2(result.value(QStringLiteral("file")).toString().endsWith(QStringLiteral("HikariAttachedTest.ttf")),
                 qPrintable(result.value(QStringLiteral("file")).toString()));
        QCOMPARE(named(shell(w), "fontPicker")->property("shellWindow").toInt(), w);
        QVERIFY(catalogs.allFonts(w).contains(family));
        QVERIFY(catalogs.fontList(0, QString(), false, QString(), w).contains(family));
        QVERIFY(!catalogs.allFonts().contains(family));

        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep01.ass"), "{\\fnHikariAttachedTest}Abc")));
        auto &collector = application->fontCollector();
        collector.open();
        QCOMPARE(collector.directory(), QDir::toNativeSeparators(canonical(show) + QStringLiteral("/fonts")));
        collector.changeOptions(int(application::CollectorAction::Check), false);
        collector.start(QString(), false);
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Done));
        const QString log = collector.logText();
        QVERIFY2(log.contains(QStringLiteral("Found font “HikariAttachedTest”")), qPrintable(log));
        QVERIFY2(!log.contains(QStringLiteral("Cannot find")), qPrintable(log));
    }

    void styleCatalogsHaveAProjectTier()
    {
        writeAll(home.filePath(QStringLiteral("Catalog/Default.sty")), catalogLine("Mine default"));
        writeAll(home.filePath(QStringLiteral("Catalog/Show.sty")), catalogLine("Mine show"));
        start();
        const QString show = folder("tiered", true);
        const QString projectCatalogs = show + QStringLiteral("/.hikari/catalogs");
        writeAll(projectCatalogs + QStringLiteral("/Show.sty"), catalogLine("Project show"));
        auto &styles = application->styleManager();
        QVERIFY(!styles.hasProject());
        QCOMPARE(rowKeys(styles.catalogRows()), (QStringList{QStringLiteral("mine:Default"), QStringLiteral("mine:Show")}));

        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep.ass"), "a")));
        QTRY_VERIFY(styles.hasProject());
        QCOMPARE(rowKeys(styles.catalogRows()), (QStringList{QStringLiteral("project:Show"), QStringLiteral("mine:Default"),
                                                             QStringLiteral("mine:Show (hidden)")}));
        QVERIFY(styles.chooseCatalog(QStringLiteral("Show")));
        QCOMPARE(styles.catalogTier(), QStringLiteral("project"));
        QCOMPARE(styles.storeStyles(), QStringList{QStringLiteral("Project show")});
        QCOMPARE(application->settingsStore()->text("styles.lastCatalog"), QStringLiteral("Show"));

        QVERIFY(QMetaObject::invokeMethod(named(shell(w), "styleManagerMenuItem")->property("action").value<QObject *>(), "trigger"));
        auto *manager = shell(w)->findChild<QQuickWindow *>(QStringLiteral("styleManager"));
        QTRY_VERIFY(manager && manager->isVisible());
        QQuickItem *content = manager->contentItem();
        const auto field = [&](const char *name) { return item(content, QLatin1String(name)); };
        QTRY_VERIFY(field("styleCatalogTier") && field("styleCatalogTier")->isVisible());
        QCOMPARE(field("styleCatalogTier")->property("text").toString(), QStringLiteral("Project"));

        QVERIFY(QMetaObject::invokeMethod(manager, "chooseCatalogIn", Q_ARG(QVariant, QStringLiteral("Show")),
                                          Q_ARG(QVariant, QStringLiteral("mine"))));
        QTRY_VERIFY(styles.catalogReadOnly());
        QCOMPARE(styles.storeStyles(), QStringList{QStringLiteral("Mine show")});
        QTRY_VERIFY(field("styleCatalogHidden")->isVisible());
        QVERIFY(!field("storeCommands")->isEnabled());
        QVERIFY(styles.removeStyles(true, {0}).isEmpty());
        QCOMPARE(styles.storeStyles(), QStringList{QStringLiteral("Mine show")});

        QVERIFY(QMetaObject::invokeMethod(manager, "transferCatalog", Q_ARG(QVariant, false)));
        auto *review = manager->findChild<QObject *>(QStringLiteral("styleClashReview"));
        QTRY_VERIFY(review && review->property("visible").toBool());
        QCOMPARE(review->property("rows").toList().size(), 1);
        QVERIFY(QMetaObject::invokeMethod(review, "setAll", Q_ARG(QVariant, QStringLiteral("rename"))));
        QCOMPARE(review->property("actions").toList(), QVariantList{QStringLiteral("rename")});
        QVERIFY(QMetaObject::invokeMethod(review, "finish", Q_ARG(QVariant, true)));
        QTRY_VERIFY(QFile::exists(projectCatalogs + QStringLiteral("/Show (2).sty")));
        QVERIFY(readAll(projectCatalogs + QStringLiteral("/Show (2).sty")).contains("Mine show"));
        QVERIFY(readAll(projectCatalogs + QStringLiteral("/Show.sty")).contains("Project show"));

        QVERIFY(styles.chooseCatalog(QStringLiteral("Show (2)"), QStringLiteral("project")));
        QVERIFY(QMetaObject::invokeMethod(manager, "transferCatalog", Q_ARG(QVariant, true)));
        QTRY_VERIFY(QFile::exists(home.filePath(QStringLiteral("Catalog/Show (2).sty"))));
        QVERIFY(!QFile::exists(projectCatalogs + QStringLiteral("/Show (2).sty")));
        QCOMPARE(styles.catalogTier(), QStringLiteral("mine"));

        QVERIFY(QMetaObject::invokeMethod(field("newCatalog"), "click"));
        auto *popover = manager->findChild<QObject *>(QStringLiteral("newCatalogPopover"));
        QTRY_VERIFY(popover->property("opened").toBool());
        auto *popoverContent = popover->property("contentItem").value<QQuickItem *>();
        QQuickItem *create = item(popoverContent, QStringLiteral("newCatalogCreate"));
        QCOMPARE(create->property("text").toString(), QStringLiteral("Save to project"));
        item(popoverContent, QStringLiteral("newCatalogName"))->setProperty("text", QStringLiteral("Signs"));
        QVERIFY(QMetaObject::invokeMethod(create, "click"));
        QTRY_VERIFY(!popover->property("visible").toBool());
        QCOMPARE(styles.catalog(), QStringLiteral("Signs"));
        QCOMPARE(styles.catalogTier(), QStringLiteral("project"));
        styles.saveCatalog();
        QVERIFY(QFile::exists(projectCatalogs + QStringLiteral("/Signs.sty")));

        QVERIFY(QMetaObject::invokeMethod(field("newCatalog"), "click"));
        QTRY_VERIFY(popover->property("opened").toBool());
        auto *mine = popover->findChild<QObject *>(QStringLiteral("newCatalogSaveMine"));
        QVERIFY(mine);
        QVERIFY(QMetaObject::invokeMethod(mine, "triggered"));
        QCOMPARE(create->property("text").toString(), QStringLiteral("Save to mine"));
        QVERIFY(application->settingsStore()->list("workspace.saveTiers").contains(QStringLiteral("styleCatalogs\tmine")));
        item(popoverContent, QStringLiteral("newCatalogName"))->setProperty("text", QStringLiteral("Personal"));
        QVERIFY(QMetaObject::invokeMethod(create, "click"));
        QTRY_COMPARE(styles.catalog(), QStringLiteral("Personal"));
        QCOMPARE(styles.catalogTier(), QStringLiteral("mine"));
        styles.saveCatalog();
        QVERIFY(QFile::exists(home.filePath(QStringLiteral("Catalog/Personal.sty"))));

        QVERIFY(QMetaObject::invokeMethod(field("newCatalog"), "click"));
        QTRY_VERIFY(popover->property("opened").toBool());
        QCOMPARE(create->property("text").toString(), QStringLiteral("Save to mine"));
    }

    void fontCatalogsAndLayoutsHaveAProjectTier()
    {
        start();
        const QString show = folder("tieredlists", true);
        const QString hikariDir = show + QStringLiteral("/.hikari");
        auto &fonts = application->fontCatalogs();
        QVERIFY(fonts.addCatalog(QStringLiteral("Signs"), false));
        QVERIFY(!fonts.hasProject());
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        QVERIFY(!QDir(hikariDir).exists(QStringLiteral("catalogs")));
        QTRY_VERIFY(fonts.hasProject());
        QVERIFY(fonts.addCatalog(QStringLiteral("signs"), false, QStringLiteral("project")));
        fonts.setFontsInCatalog(QStringLiteral("signs"), {QStringLiteral("Arial")}, true);
        QCOMPARE(rowKeys(fonts.catalogRows()),
                 (QStringList{QStringLiteral("project:signs"), QStringLiteral("mine:Signs (hidden)")}));
        QCOMPARE(fonts.catalogNames(), QStringList{QStringLiteral("signs")});
        QCOMPARE(fonts.catalogSize(QStringLiteral("signs")), 1);
        QCOMPARE(fonts.catalogSize(QStringLiteral("Signs"), QStringLiteral("mine")), 0);
        QVERIFY(fonts.save());
        const QString projectFonts = hikariDir + QStringLiteral("/catalogs/fonts.toml");
        const QString mineFonts = QString::fromStdU16String((fonts.catalogDir() / "fonts.toml").u16string());
        QVERIFY(readAll(projectFonts).contains("Arial"));
        QVERIFY(!readAll(mineFonts).contains("Arial"));

        const QVariantList clashes = fonts.catalogClashes(QStringLiteral("Signs"), QStringLiteral("mine"));
        QCOMPARE(clashes.size(), 1);
        QCOMPARE(clashes[0].toMap().value(QStringLiteral("suggestion")).toString(), QStringLiteral("signs (2)"));
        const QVariantList rename{QVariantMap{{QStringLiteral("name"), QStringLiteral("Signs")},
                                              {QStringLiteral("action"), QStringLiteral("rename")},
                                              {QStringLiteral("newName"), QString()}}};
        QCOMPARE(fonts.copyCatalog(QStringLiteral("Signs"), QStringLiteral("mine"), rename), QStringLiteral("Signs (2)"));
        QCOMPARE(fonts.moveCatalog(QStringLiteral("Signs (2)"), QStringLiteral("project")), QStringLiteral("Signs (2)"));
        QCOMPARE(rowKeys(fonts.catalogRows()), (QStringList{QStringLiteral("project:signs"), QStringLiteral("mine:Signs (hidden)"),
                                                            QStringLiteral("mine:Signs (2)")}));
        QVERIFY(readAll(mineFonts).contains("Signs (2)"));
        QVERIFY(!readAll(projectFonts).contains("Signs (2)"));

        auto *layouts = qobject_cast<ui::WorkspaceLayoutController *>(shell(w)->property("workspaceLayout").value<QObject *>());
        QVERIFY(layouts && layouts->hasProject());
        QTRY_VERIFY(layouts->saveLayoutAs(QStringLiteral("Review")));
        QVERIFY(!QFile::exists(hikariDir + QStringLiteral("/layouts.json")));
        QVERIFY(layouts->saveLayoutAs(QStringLiteral("review"), QStringLiteral("project")));
        QCOMPARE(rowKeys(layouts->savedLayouts()),
                 (QStringList{QStringLiteral("project:review"), QStringLiteral("mine:Review (hidden)")}));
        const QString projectLayouts = hikariDir + QStringLiteral("/layouts.json");
        const QString mineLayouts = layouts->savedLayoutsFile();
        const QByteArray projectBefore = readAll(projectLayouts), mineBefore = readAll(mineLayouts);
        QVERIFY(projectBefore.contains("review"));
        QVERIFY(layouts->applySavedLayout(QStringLiteral("Review")));
        QCOMPARE(layouts->layout(), QStringLiteral("review"));
        QCOMPARE(readAll(projectLayouts), projectBefore);
        QCOMPARE(readAll(mineLayouts), mineBefore);
        QCOMPARE(layouts->layoutClashes(QStringLiteral("review"), QStringLiteral("project")).size(), 1);
        const QVariantList replace{QVariantMap{{QStringLiteral("name"), QStringLiteral("review")}, {QStringLiteral("action"), QStringLiteral("replace")}}};
        QCOMPARE(layouts->moveLayout(QStringLiteral("review"), QStringLiteral("project"), replace), QStringLiteral("review"));
        QCOMPARE(rowKeys(layouts->savedLayouts()), QStringList{QStringLiteral("mine:review")});
        QVERIFY(!readAll(projectLayouts).contains("review"));
        application->resolveClose(discardAll(application->reviewCloseProject(w)));
        QTRY_VERIFY(application->projectRoot(w).isEmpty());
        QTRY_VERIFY(!layouts->hasProject());
        QCOMPARE(rowKeys(layouts->savedLayouts()), QStringList{QStringLiteral("mine:review")});
    }

    void shapesAndTagsReadTheProjectsLists()
    {
        start();
        const QString show = folder("tieredtools", true);
        application::visual::ShapePreset star{u"Star", u"m 0 0 l 5 5", 0, 0};
        application::visual::ShapePreset circle{u"Circle", u"m 0 0 l 9 9", 0, 0};
        ui::ListFile::make(show + QStringLiteral("/.hikari/shapes.toml"), {}, ui::kShapesFormat, nullptr)
            ->write(ui::shapesToToml({star, circle}));
        auto blur = application::visual::AllTagsSetting::named(u"Blur");
        blur.tag = u"be";
        ui::ListFile::make(show + QStringLiteral("/.hikari/tags.toml"), {}, ui::kTagsFormat, nullptr)->write(ui::tagsToToml({blur}));
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        auto *tools = shell(w)->property("visualTools").value<ui::VisualToolsController *>();
        QVERIFY(tools);
        const auto *shapes = tools->shapePresets();
        QVERIFY(shapes && shapes->size() >= 2);
        QCOMPARE(shapes->at(0).name, u"Star");
        QCOMPARE(shapes->at(1).name, u"Circle");
        const auto circles = std::ranges::count_if(*shapes, [](const auto &p) {
            return application::visual::sameNameIgnoringCase(p.name, u"circle");
        });
        QCOMPARE(circles, 1);
        const auto *tags = tools->allTagsSettings();
        QVERIFY(tags && !tags->empty());
        QCOMPARE(tags->at(0).name, u"Blur");
        QCOMPARE(tags->at(0).tag, u"be");
        QCOMPARE(std::ranges::count_if(*tags, [](const auto &t) { return t.name == u"blur"; }), 0);
        QVERIFY(application->visualTools().shapePresets()->at(0).name == u"Star");
    }

    void shiftTimesPresetsHaveAProjectTier()
    {
        start();
        const QString show = folder("presets", true);
        const QString presets = show + QStringLiteral("/.hikari/presets.toml");
        writeAll(presets, "version = 1\n[shiftTimes]\npresets = ['{\"name\":\"Late\",\"values\":{\"timeMs\":900}}']\n");
        auto &shift = application->shiftTimesSettings();
        auto values = shift.settingsMap();
        values.insert(QStringLiteral("timeMs"), 100);
        shift.setSettingsMap(values);
        QVERIFY(shift.savePreset(QStringLiteral("shift"), QStringLiteral("Late")));
        QVERIFY(!shift.hasProject());

        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        QVERIFY(application->openFile(writeAss(show + QStringLiteral("/ep.ass"), "a")));
        QTRY_VERIFY(shift.hasProject());
        QCOMPARE(rowKeys(shift.shiftPresetRows()), (QStringList{QStringLiteral("project:Late"), QStringLiteral("mine:Late (hidden)")}));
        QVERIFY(shift.applyPreset(QStringLiteral("shift"), QStringLiteral("Late")));
        QCOMPARE(shift.settings().timeMs, 900);

        QVERIFY(QMetaObject::invokeMethod(named(shell(w), "panelShowTiming"), "triggered"));
        QQuickItem *content = shell(w)->contentItem();
        const auto field = [&](const char *name) { return item(content, QLatin1String(name)); };
        QTRY_VERIFY(field("shiftPresetTier") && field("shiftPresetTier")->isVisible());
        QCOMPARE(field("shiftPresetTier")->property("text").toString(), QStringLiteral("Project"));
        QVERIFY(named(shell(w), "shiftRenamePreset")->property("enabled").toBool());

        QVERIFY(shift.applyPreset(QStringLiteral("shift"), QStringLiteral("Late"), QStringLiteral("mine")));
        QCOMPARE(shift.settings().timeMs, 100);
        QCOMPARE(field("shiftPresetTier")->property("text").toString(), QStringLiteral("Mine"));
        QTRY_VERIFY(!named(shell(w), "shiftRenamePreset")->property("enabled").toBool());
        QVERIFY(!named(shell(w), "shiftDeletePreset")->property("enabled").toBool());
        QCOMPARE(named(shell(w), "shiftCopyPresetToTier")->property("text").toString(), QStringLiteral("Copy to project"));

        QVERIFY(QMetaObject::invokeMethod(field("shiftPresetBar"), "transfer", Q_ARG(QVariant, false)));
        auto *review = shell(w)->findChild<QObject *>(QStringLiteral("shiftPresetClashReview"));
        QTRY_VERIFY(review && review->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(review, "setAll", Q_ARG(QVariant, QStringLiteral("rename"))));
        QVERIFY(QMetaObject::invokeMethod(review, "finish", Q_ARG(QVariant, true)));
        QTRY_VERIFY(readAll(presets).contains("Late (2)"));

        auto *popover = shell(w)->findChild<QObject *>(QStringLiteral("shiftPresetPopover"));
        QVERIFY(QMetaObject::invokeMethod(popover, "openFor", Q_ARG(QVariant, QString())));
        QTRY_VERIFY(popover->property("opened").toBool());
        auto *popoverContent = popover->property("contentItem").value<QQuickItem *>();
        QQuickItem *save = item(popoverContent, QStringLiteral("shiftPresetSave"));
        QCOMPARE(save->property("text").toString(), QStringLiteral("Save to project"));
        item(popoverContent, QStringLiteral("shiftPresetName"))->setProperty("text", QStringLiteral("House"));
        QVERIFY(QMetaObject::invokeMethod(save, "click"));
        QTRY_VERIFY(readAll(presets).contains("House"));
        QCOMPARE(shift.shiftPresetTier(), QStringLiteral("project"));

        QVERIFY(QMetaObject::invokeMethod(popover, "openFor", Q_ARG(QVariant, QString())));
        QTRY_VERIFY(popover->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(popover->findChild<QObject *>(QStringLiteral("shiftPresetSaveTierMine")), "triggered"));
        QCOMPARE(save->property("text").toString(), QStringLiteral("Save to mine"));
        QVERIFY(application->settingsStore()->list("workspace.saveTiers").contains(QStringLiteral("shiftPresets\tmine")));
        QVERIFY(QMetaObject::invokeMethod(popover, "revert"));
    }

    void userOnlyStoresIgnoreTheProjectsFiles()
    {
        start();
        auto *store = application->settingsStore();
        const QStringList recent = store->list("recent.subtitles");
        const QStringList hotkeys = store->list("shortcuts.hotkeys");
        const QString show = folder("useronly", true);
        writeAll(show + QStringLiteral("/.hikari/project.toml"),
                 "version = 1\n[shortcuts]\nhotkeys = [\"Default\\tCtrl+Q\\tAPP_QUIT\"]\n[recent]\nsubtitles = [\"/x.ass\"]\n"
                 "[workspace]\nwindowSize = \"10,10\"\n[ai.mcp]\nserver = true\n");
        writeAll(show + QStringLiteral("/.hikari/hotkeys.toml"), "[Default]\n\"Ctrl+Q\" = \"APP_QUIT\"\n");
        writeAll(show + QStringLiteral("/.hikari/state.json"), "{\"recent\": {\"subtitles\": [\"/y.ass\"]}}\n");
        writeAll(show + QStringLiteral("/.hikari/ai/providers.json"), "{}\n");
        writeAll(show + QStringLiteral("/.hikari/dictionaries/en_US.dic"), "1\nzzz\n");
        const int w = openProjectWindow(show);
        QVERIFY(w >= 0);
        const QStringList overrides = application->projectOverrides(w);
        for (const char *id : {"shortcuts.hotkeys", "recent.subtitles", "workspace.windowSize", "ai.mcp.server"})
            QVERIFY2(!overrides.contains(QLatin1String(id)), id);
        QCOMPARE(store->list("recent.subtitles"), recent);
        QCOMPARE(store->list("shortcuts.hotkeys"), hotkeys);
        QCOMPARE(store->value(QStringLiteral("ai.mcp.server")).toBool(), false);
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

HIKARI_QTEST_MAIN_IN_PARTS(ProjectWindowsTests)
#include "project_windows_tests.moc"
