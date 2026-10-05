// P9: the tab menu, tab reordering, "show in folder", several dropped files,
// OPEN_SUBS_IN_NEW_TAB, associated-file discovery and the legacy Subs/
// autosaves through the composition and the real QML (legacy Notebook::
// ContextMenu, OnTabSel and OnMouseEvent, HikariSubFrame::OpenFile,
// OpenFiles, FindFile, SaveAll, OnRecent and AutoSaveOpen, Notebook::LoadVideo,
// at 20d647c4). Menu items are triggered as a click would; the tab drag uses
// the mouse.

#include "hikari/app/application.h"
#include "hikari/application/session_file.h"
#include "docking.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
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

QString writeAss(const QString &path, const QByteArray &scriptInfo, const char *text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\n" + scriptInfo +
            "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,," +
            QByteArray(text) + "\n");
    return path;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray hash(const QString &path)
{
    return QCryptographicHash::hash(readAll(path), QCryptographicHash::Sha256);
}

QString media(const char *name)
{
    return QStringLiteral(HIKARI_MEDIA_FIXTURES "/") + QLatin1String(name);
}

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write("x");
}

} // namespace

class TabMenuTests : public QObject {
    Q_OBJECT

    QTemporaryDir home; // the settings folder (LastSession.txt, Subs)
    QTemporaryDir dir;  // subtitles and media
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QStringList revealed; // SelectInFolder calls

    void start()
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("hikari.ini"));
        o.playbackAudio = false;
        o.revealInFolder = [this](const QString &path) { revealed << path; };
        o.spellingBackend = {}; // no spell checker, so no notice over the window
        application = new app::Application(o);
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    QObject *named(const char *name) const
    {
        auto *root = engine->rootObjects().first();
        if (auto *found = root->findChild<QObject *>(QLatin1String(name)))
            return found;
        for (QObject *o : root->findChildren<QObject *>())
            if (o->objectName() == QLatin1String(name))
                return o;
        return nullptr;
    }
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *visualItem(const QString &name) const { return findItem(window->contentItem(), name); }
    // The tab menu opened on tab `index` (-1: not on a tab), as a right click does.
    QObject *openTabMenu(int index)
    {
        auto *bar = named("documentTabBar");
        if (!bar)
            return nullptr;
        QMetaObject::invokeMethod(bar, "openTabMenu", Q_ARG(QVariant, index), Q_ARG(QVariant, QVariant::fromValue(bar)),
                                  Q_ARG(QVariant, 10), Q_ARG(QVariant, 5));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return named("documentTabMenu");
    }
    void closeTabMenu()
    {
        if (auto *menu = named("documentTabMenu"))
            QMetaObject::invokeMethod(menu, "close");
    }
    void trigger(const char *name)
    {
        QObject *item = named(name);
        QVERIFY2(item, name);
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
    }
    QStringList titles() const
    {
        QStringList out;
        for (const QVariant &tab : application->tabs())
            out << tab.toMap().value(QStringLiteral("title")).toString();
        return out;
    }
    application::EditSession *session(int tab) const
    {
        return application->files().session(application->workspace().tabs()[std::size_t(tab)]);
    }
    // Changes the first Line of tab `tab` (one committed step).
    void edit(int tab, const char *text)
    {
        auto *s = session(tab);
        QVERIFY(s);
        const auto line = s->document().lines().front()->id;
        QVERIFY(s->editDraftText(line, std::u8string(reinterpret_cast<const char8_t *>(text))));
        QVERIFY(s->commitDraft());
        QVERIFY(s->isDirty());
    }
    application::Session lastSession() const
    {
        const QByteArray bytes = readAll(application->lastSessionPath());
        auto text = application::decodeSessionBytes(std::string_view(bytes.constData(), bytes.size()));
        return application::parseSession(text.value_or(std::string())).value_or(application::Session{});
    }
    QStringList sessionSubtitles() const
    {
        QStringList out;
        for (const auto &tab : lastSession().tabs)
            out << QFileInfo(QString::fromStdString(tab.subtitles)).fileName();
        return out;
    }
    // A session with tabs a.ass, b.ass and c.ass, the last one shown (legacy
    // LoadLastSession); tab `mediaTab` has its own video, audio and keyframes
    // (placeholder files: not shown while another tab is active).
    void restoreThreeTabs(int mediaTab = 0)
    {
        writeAss(dir.filePath(QStringLiteral("tabs/a.ass")), {}, "a");
        writeAss(dir.filePath(QStringLiteral("tabs/b.ass")), {}, "b");
        writeAss(dir.filePath(QStringLiteral("tabs/c.ass")), {}, "c");
        touch(dir.filePath(QStringLiteral("media/a video.mkv")));
        touch(dir.filePath(QStringLiteral("sound/a.wav")));
        touch(dir.filePath(QStringLiteral("frames/a_keyframes.txt")));
        auto native = [&](const QString &p) { return QDir::toNativeSeparators(dir.filePath(p)).toUtf8(); };
        QByteArray kls = "\xEF\xBB\xBF[HikariSub v0.0.1]\r\n";
        const char *names[] = {"a", "b", "c"};
        for (int i = 0; i < 3; ++i) {
            const bool media = i == mediaTab;
            kls += "Tab: " + QByteArray::number(i) + "\r\nVideo: " +
                   (media ? native(QStringLiteral("media/a video.mkv")) : QByteArray()) +
                   "\r\nPosition: 0\r\nFFMS2: 1\r\nSubtitles: " +
                   native(QStringLiteral("tabs/%1.ass").arg(QLatin1String(names[i]))) + "\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n";
            if (media)
                kls += "Audio: " + native(QStringLiteral("sound/a.wav")) + "\r\nKeyframes: " +
                       native(QStringLiteral("frames/a_keyframes.txt")) + "\r\n";
        }
        const QString file = dir.filePath(QStringLiteral("three.kls"));
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kls);
        f.close();
        const QVariantMap review = application->reviewSession(QUrl::fromLocalFile(file));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("rows")).toList().isEmpty());
        application->finishClose();
        QCOMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.ass")}));
        QCOMPARE(application->currentTab(), 2);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(home.isValid() && dir.isValid());
    }
    void init()
    {
        revealed.clear();
        QFile::remove(home.filePath(QStringLiteral("LastSession.txt")));
        start();
    }
    void cleanup()
    {
        closeTabMenu();
        delete engine;
        engine = nullptr;
        delete application;
        application = nullptr;
        QFile::remove(home.filePath(QStringLiteral("hikari.ini")));
    }

    // Notebook.cpp:886-911: every tab by name with the active one checked,
    // Save only on a modified tab, the folder items of the tab's own files
    // (SelectInFolder on the file), none off a tab.
    void menuListsTabsSaveAndFolders()
    {
        restoreThreeTabs();
        QObject *menu = openTabMenu(0);
        QVERIFY(menu);
        QCOMPARE(named("tabMenuTab0")->property("text").toString(), QStringLiteral("a.ass"));
        QCOMPARE(named("tabMenuTab2")->property("text").toString(), QStringLiteral("c.ass"));
        QVERIFY(!named("tabMenuTab0")->property("checked").toBool());
        QVERIFY(named("tabMenuTab2")->property("checked").toBool());
        QVERIFY(!named("tabMenuSave")->property("enabled").toBool()); // not modified
        QVERIFY(named("tabMenuSaveAll")->property("enabled").toBool());
        QCOMPARE(named("tabMenuSave")->property("text").toString(), QStringLiteral("Save"));
        QCOMPARE(named("tabMenuCloseAll")->property("text").toString(), QStringLiteral("Close all tabs"));
        QCOMPARE(named("tabMenuFolder_subtitles")->property("text").toString(),
                 QStringLiteral("Open the folder containing the subtitles"));
        QCOMPARE(named("tabMenuFolder_video")->property("text").toString(), QStringLiteral("Open video containing folder"));
        QCOMPARE(named("tabMenuFolder_audio")->property("text").toString(), QStringLiteral("Open audio containing folder"));
        QCOMPARE(named("tabMenuFolder_keyframes")->property("text").toString(),
                 QStringLiteral("Open keyframes containing folder"));
        // The legacy order: tabs, separator, Save, Save all, Close all tabs,
        // the folders, Subtitle comparison.
        QStringList order;
        const int count = menu->property("count").toInt();
        for (int i = 0; i < count; ++i) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
            order << (item ? item->objectName() : QString());
        }
        QCOMPARE(order.mid(0, 3), (QStringList{QStringLiteral("tabMenuTab0"), QStringLiteral("tabMenuTab1"),
                                              QStringLiteral("tabMenuTab2")}));
        QCOMPARE(order.mid(4, 7), (QStringList{QStringLiteral("tabMenuSave"), QStringLiteral("tabMenuSaveAll"),
                                              QStringLiteral("tabMenuCloseAll"), QStringLiteral("tabMenuFolder_subtitles"),
                                              QStringLiteral("tabMenuFolder_video"), QStringLiteral("tabMenuFolder_audio"),
                                              QStringLiteral("tabMenuFolder_keyframes")}));
        QCOMPARE(order.size(), 12); // and the comparison submenu's item last
        trigger("tabMenuFolder_video");
        trigger("tabMenuFolder_subtitles");
        QCOMPARE(revealed, (QStringList{QDir::toNativeSeparators(dir.filePath(QStringLiteral("media/a video.mkv"))),
                                        QDir::toNativeSeparators(dir.filePath(QStringLiteral("tabs/a.ass")))}));
        closeTabMenu();
        // b.ass modified: Save on it; it has only its subtitles' folder.
        edit(1, "b2");
        openTabMenu(1);
        QVERIFY(named("tabMenuSave")->property("enabled").toBool());
        QVERIFY(named("tabMenuFolder_subtitles"));
        QVERIFY(!named("tabMenuFolder_video"));
        QVERIFY(!named("tabMenuFolder_keyframes"));
        closeTabMenu();
        // Off a tab: no Save, no folders.
        openTabMenu(-1);
        QVERIFY(!named("tabMenuSave")->property("enabled").toBool());
        QVERIFY(!named("tabMenuFolder_subtitles"));
        QCOMPARE(named("tabMenuTab1")->property("text").toString(), QStringLiteral("b.ass"));
    }

    // OnTabSel (Notebook.cpp:1019-1039): the chosen tab trades places with
    // the first visible one and becomes active there; no session is written.
    void choosingATabMovesItToTheFirstVisiblePlace()
    {
        restoreThreeTabs();
        const QByteArray before = readAll(application->lastSessionPath());
        openTabMenu(-1);
        trigger("tabMenuTab0"); // the first visible tab itself: shown, not moved
        QCOMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.ass")}));
        QCOMPARE(application->currentTab(), 0);
        closeTabMenu();
        openTabMenu(-1);
        trigger("tabMenuTab2");
        QCOMPARE(titles(), (QStringList{QStringLiteral("c.ass"), QStringLiteral("b.ass"), QStringLiteral("a.ass")}));
        QCOMPARE(application->currentTab(), 0);
        QCOMPARE(application->workspace().title(*application->workspace().editingTarget())->c_str(), std::string("c.ass"));
        // With the bar scrolled so that the second tab is the first visible.
        application->chooseTab(0, 1);
        QCOMPARE(titles(), (QStringList{QStringLiteral("b.ass"), QStringLiteral("c.ass"), QStringLiteral("a.ass")}));
        QCOMPARE(application->currentTab(), 1);
        QCOMPARE(readAll(application->lastSessionPath()), before);
    }

    // OnMouseEvent (537-556, 397-400): a tab dragged over others swaps with
    // each in turn and stays active; the session is written once the button
    // is released, with the new order, and a restore keeps it.
    void draggingATabReordersAndKeepsTheOrderInTheSession()
    {
        // c has the media; a, dragged, has none (showing a placeholder video
        // would report a failure in the log while the pointer is down).
        restoreThreeTabs(2);
        application->selectTab(0);
        QCoreApplication::processEvents();
        auto center = [&](int index) {
            QQuickItem *tab = visualItem(QStringLiteral("documentTab%1").arg(index));
            return tab ? tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2)).toPoint() : QPoint();
        };
        QTRY_VERIFY(!center(2).isNull());
        const QPoint from = center(0), over1 = center(1), over2 = center(2);
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        for (int step = 1; step <= 10; ++step)
            QTest::mouseMove(window, from + (over1 - from) * step / 10);
        QCOMPARE(titles(), (QStringList{QStringLiteral("b.ass"), QStringLiteral("a.ass"), QStringLiteral("c.ass")}));
        QCOMPARE(application->currentTab(), 1);
        // Written only when the button comes up.
        QCOMPARE(sessionSubtitles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.ass")}));
        for (int step = 1; step <= 10; ++step)
            QTest::mouseMove(window, over1 + (over2 - over1) * step / 10);
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("b.ass"), QStringLiteral("c.ass"), QStringLiteral("a.ass")}));
        QCOMPARE(application->currentTab(), 2); // the dragged tab stays active
        QTest::mouseRelease(window, Qt::LeftButton, {}, over2);
        QTRY_COMPARE(sessionSubtitles(), (QStringList{QStringLiteral("b.ass"), QStringLiteral("c.ass"), QStringLiteral("a.ass")}));
        // Each tab kept its own media (c's moved with it).
        const auto tabs = lastSession().tabs;
        QCOMPARE(QFileInfo(QString::fromStdString(tabs[1].video)).fileName(), QStringLiteral("a video.mkv"));
        QCOMPARE(QString::fromStdString(tabs[2].video), QString());
        // The order survives a restore.
        delete engine;
        engine = nullptr;
        delete application;
        application = nullptr;
        start();
        const QVariantMap review = application->reviewSession({});
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        application->finishClose();
        QCOMPARE(titles(), (QStringList{QStringLiteral("b.ass"), QStringLiteral("c.ass"), QStringLiteral("a.ass")}));
    }

    // MENU_SAVE + i is Save(false, i) for that tab; Save all (SaveAll) runs
    // Save for every modified tab in order, the dialog for each Untitled one.
    void saveAndSaveAllFromTheTabMenu()
    {
        restoreThreeTabs();
        edit(1, "b saved");
        application->addPage(); // an Untitled tab, modified
        edit(3, "untitled one");
        application->addPage(); // another, modified
        edit(4, "untitled two");
        application->addPage(); // a third, untouched
        application->selectTab(0);
        // Save on b (not the editing target) writes it in place.
        openTabMenu(1);
        trigger("tabMenuSave");
        QTRY_VERIFY(!session(1)->isDirty());
        QVERIFY(readAll(dir.filePath(QStringLiteral("tabs/b.ass"))).contains("b saved"));
        QCOMPARE(application->currentTab(), 0); // the tab shown stays
        closeTabMenu();
        // Save on the Untitled tab asks for its file.
        const auto untitled = application->tabDocument(3);
        QCOMPARE(application->saveRouteFor(untitled), QStringLiteral("dialog"));
        openTabMenu(3);
        trigger("tabMenuSave");
        QObject *dialog = named("saveAsDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("document").toULongLong(), untitled);
        QMetaObject::invokeMethod(dialog, "reject");
        QTRY_VERIFY(!dialog->property("visible").toBool());
        closeTabMenu();
        // Save all: c (unmodified) and the untouched tab are left; the two
        // Untitled ones wait for the dialog, in tab order.
        edit(0, "a saved");
        const QVariantList queue = application->saveAll();
        QCOMPARE(queue.size(), 2);
        QCOMPARE(queue[0].toMap().value(QStringLiteral("id")).toULongLong(), untitled);
        QCOMPARE(queue[1].toMap().value(QStringLiteral("id")).toULongLong(), application->tabDocument(4));
        QTRY_VERIFY(!session(0)->isDirty());
        QVERIFY(readAll(dir.filePath(QStringLiteral("tabs/a.ass"))).contains("a saved"));
        // Through the menu: the dialogs follow one another.
        openTabMenu(0);
        trigger("tabMenuSaveAll");
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("document").toULongLong(), untitled);
        QMetaObject::invokeMethod(dialog, "reject"); // legacy SaveAll goes on with the next tab
        QTRY_COMPARE(dialog->property("document").toULongLong(), application->tabDocument(4));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QMetaObject::invokeMethod(dialog, "reject");
        // The dialog's file for an Untitled tab that is not shown.
        const QString chosen = dir.filePath(QStringLiteral("saved/untitled"));
        QDir().mkpath(dir.filePath(QStringLiteral("saved")));
        QCOMPARE(application->saveChosenFor(untitled, QUrl::fromLocalFile(chosen)), QString());
        QTRY_VERIFY(!session(3)->isDirty());
        QVERIFY(readAll(chosen + QStringLiteral(".ass")).contains("untitled one"));
        QTRY_COMPARE(titles()[3], QStringLiteral("untitled.ass"));
        QCOMPARE(application->currentTab(), 0);
    }

    // MENU_CHOOSE - 1: "All tabs will be closed, continue?" (No keeps
    // everything), then every tab's work is reviewed and one new Untitled
    // tab is left; no session is written.
    void closeAllTabsAsksThenReviews()
    {
        restoreThreeTabs();
        edit(2, "c changed");
        const QByteArray before = readAll(application->lastSessionPath());
        QObject *prompt = named("closeAllPrompt");
        QVERIFY(prompt);
        openTabMenu(1);
        trigger("tabMenuCloseAll");
        QTRY_VERIFY(prompt->property("visible").toBool());
        QCOMPARE(prompt->property("title").toString(), QStringLiteral("Prompt"));
        QMetaObject::invokeMethod(prompt, "reject"); // No
        QTRY_VERIFY(!prompt->property("visible").toBool());
        QCOMPARE(titles().size(), 3);
        openTabMenu(1);
        trigger("tabMenuCloseAll");
        QTRY_VERIFY(prompt->property("visible").toBool());
        QMetaObject::invokeMethod(prompt, "accept"); // Yes
        QObject *review = named("closeReview");
        QTRY_VERIFY(review->property("visible").toBool());
        const QVariantList rows = review->property("rows").toList();
        QCOMPARE(rows.size(), 1); // only c has unsaved work
        QCOMPARE(rows[0].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("c.ass"));
        QSignalSpy finished(application, &app::Application::closeFinished);
        application->resolveClose({QVariantMap{{QStringLiteral("id"), rows[0].toMap().value(QStringLiteral("id"))},
                                               {QStringLiteral("save"), false},
                                               {QStringLiteral("path"), QString()}}});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(titles(), QStringList{QStringLiteral("Untitled")});
        QCOMPARE(readAll(application->lastSessionPath()), before);
        QVERIFY(!readAll(dir.filePath(QStringLiteral("tabs/c.ass"))).contains("c changed"));
        QMetaObject::invokeMethod(review, "close");
    }

    // OnRecent: Ctrl+click on a recent file shows it in its folder and opens
    // nothing; a plain click opens it.
    void controlClickOnARecentFileShowsItInItsFolder()
    {
        const QString file = writeAss(dir.filePath(QStringLiteral("recent/r.ass")), {}, "r");
        QVERIFY(application->openFile(file));
        application->addPage();
        QObject *menu = named("recentSubtitlesMenu");
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
        QTRY_VERIFY(named("recentSubtitles0"));
        const int tabs = titles().size();
        QTest::keyPress(window, Qt::Key_Control, Qt::ControlModifier);
        QCOMPARE(QGuiApplication::keyboardModifiers(), Qt::ControlModifier);
        trigger("recentSubtitles0");
        QTest::keyRelease(window, Qt::Key_Control, Qt::NoModifier);
        QCOMPARE(revealed, QStringList{QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath())});
        QCOMPARE(titles().size(), tabs);
        QCOMPARE(titles().last(), QStringLiteral("Untitled"));
        // Ctrl with Shift is not wxMOD_CONTROL: it opens.
        QTest::keyPress(window, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(!application->revealRecent(file));
        QTest::keyRelease(window, Qt::Key_Shift, Qt::NoModifier);
        trigger("recentSubtitles0");
        QTRY_COMPARE(titles().last(), QStringLiteral("r.ass"));
        QCOMPARE(revealed.size(), 1);
    }

    // OPEN_SUBS_IN_NEW_TAB (OpenFile): with a file in the tab, other
    // subtitles open in a new tab after the last one, unasked; without one,
    // into the tab after the review.
    void openSubtitlesInANewTab()
    {
        const QString one = writeAss(dir.filePath(QStringLiteral("new/one.ass")), {}, "one");
        const QString two = writeAss(dir.filePath(QStringLiteral("new/two.ass")), {}, "two");
        const QString three = writeAss(dir.filePath(QStringLiteral("new/three.ass")), {}, "three");
        application->settingsStore()->set("subtitles.openInNewTab", true);
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first(), "openSubtitles", Q_ARG(QVariant, one)));
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("one.ass")}); // the untouched Untitled tab took it
        edit(0, "one changed");
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first(), "openSubtitles", Q_ARG(QVariant, two)));
        QCOMPARE(titles(), (QStringList{QStringLiteral("one.ass"), QStringLiteral("two.ass")}));
        QCOMPARE(application->currentTab(), 1);
        QVERIFY(session(0)->isDirty()); // not reviewed, not touched
        QVERIFY(!named("closeReview")->property("visible").toBool());
        QVERIFY(lastSession().tabs.size() == 2);
        // Off: into the tab, after the review of its work.
        application->settingsStore()->set("subtitles.openInNewTab", false);
        application->selectTab(0);
        const QVariantMap result = application->reviewOpen(three);
        QCOMPARE(result.value(QStringLiteral("rows")).toList().size(), 1);
        application->cancelClose();
        QCOMPARE(titles(), (QStringList{QStringLiteral("one.ass"), QStringLiteral("two.ass")}));
    }

    // OpenFiles (1831-1916): several dropped files, sorted; the i-th
    // subtitles with the i-th video in one tab, the first into the untouched
    // tab, the others into new tabs; the last one shown; a dirty Untitled
    // target is reviewed first and a Cancel skips only the first file.
    void severalDroppedFilesOpenAsTabs()
    {
        const QString b = writeAss(dir.filePath(QStringLiteral("drop/b.ass")), {}, "b");
        const QString a = writeAss(dir.filePath(QStringLiteral("drop/a.ass")), {}, "a");
        const QString c = writeAss(dir.filePath(QStringLiteral("drop/c.srt")), {}, "c");
        const QString video = dir.filePath(QStringLiteral("drop/v.mkv"));
        QFile::remove(video);
        QVERIFY(QFile::copy(media("cfr.mkv"), video));
        QVariantMap result = application->openDropped({QUrl::fromLocalFile(b), QUrl::fromLocalFile(video),
                                                       QUrl::fromLocalFile(a), QUrl::fromLocalFile(c)});
        QCOMPARE(result.value(QStringLiteral("kind")).toString(), QStringLiteral("files"));
        QVERIFY(result.value(QStringLiteral("rows")).toList().isEmpty());
        QCOMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.srt")}));
        QCOMPARE(application->currentTab(), 2);
        // a.ass has the video (no question), the others only their subtitles.
        const QVariantList folders = application->tabMenu(0).value(QStringLiteral("folders")).toList();
        QCOMPARE(folders.size(), 2);
        QCOMPARE(folders[1].toMap().value(QStringLiteral("path")).toString(), QDir::toNativeSeparators(video));
        QCOMPARE(application->tabMenu(1).value(QStringLiteral("folders")).toList().size(), 1);
        QCOMPARE(sessionSubtitles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.srt")}));
        // A dirty Untitled tab: the review comes first.
        application->addPage();
        edit(3, "mine");
        result = application->openDropped({QUrl::fromLocalFile(a), QUrl::fromLocalFile(b)});
        QCOMPARE(result.value(QStringLiteral("rows")).toList().size(), 1);
        QCOMPARE(titles().size(), 4); // nothing yet
        application->cancelClose(); // the first is skipped, the second opens
        QCOMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.srt"),
                                        QStringLiteral("Untitled"), QStringLiteral("b.ass")}));
        QVERIFY(session(3)->isDirty());
        application->selectTab(3);
        result = application->openDropped({QUrl::fromLocalFile(a), QUrl::fromLocalFile(c)});
        const QVariantList rows = result.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 1);
        QSignalSpy finished(application, &app::Application::closeFinished);
        application->resolveClose({QVariantMap{{QStringLiteral("id"), rows[0].toMap().value(QStringLiteral("id"))},
                                               {QStringLiteral("save"), false},
                                               {QStringLiteral("path"), QString()}}});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.srt"),
                                        QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.srt")}));
        QCOMPARE(application->currentTab(), 5);
    }

    // Notebook::LoadVideo's question when subtitles open: the Script Info
    // video, audio ("Audio File", C05: the Document's own) and keyframes,
    // and the folder's same-named video; Load associated opens the video
    // without its audio, the associated audio and the keyframes.
    void associatedFilesAreOfferedAndLoaded()
    {
        const QString folder = dir.filePath(QStringLiteral("assoc"));
        QDir().mkpath(folder + QStringLiteral("/audio"));
        QFile::remove(folder + QStringLiteral("/ep.mkv"));
        QFile::remove(folder + QStringLiteral("/audio/ep.mka"));
        QVERIFY(QFile::copy(media("cfr.mkv"), folder + QStringLiteral("/ep.mkv")));
        QVERIFY(QFile::copy(media("audioonly.mkv"), folder + QStringLiteral("/audio/ep.mka")));
        {
            QFile k(folder + QStringLiteral("/ep_keyframes.txt"));
            QVERIFY(k.open(QIODevice::WriteOnly));
            k.write("# keyframe format v1\nfps 0\n0\n24\n");
        }
        const QString subs = writeAss(folder + QStringLiteral("/ep.ass"),
                                      "Video File: ep.mkv\nAudio File: audio/ep.mka\nKeyframes File: ep_keyframes.txt\n", "ep");
        const QString n = QDir::toNativeSeparators(folder);
        const QChar sep = QDir::separator();
        QVERIFY(application->openFile(subs));
        QVERIFY(application->video().offering());
        QCOMPARE(application->video().offer(),
                 QStringLiteral("Associated files:\nVideo: %1%2ep.mkv\nAudio: %1%2audio%2ep.mka\nKeyframes: %1%2ep_keyframes.txt\n"
                                "\nVideo from directory:\nep.mkv")
                     .arg(n, sep));
        QCOMPARE(application->video().offerAssociatedLabel(), QStringLiteral("Load associated"));
        QCOMPARE(application->video().offerDirectoryLabel(), QStringLiteral("Load from directory"));
        QQuickItem *load = visualItem(QStringLiteral("loadAssociated"));
        QVERIFY(load && load->isVisible());
        QVERIFY(visualItem(QStringLiteral("loadFromDirectory"))->isVisible());
        QVERIFY(QMetaObject::invokeMethod(load, "clicked"));
        QVERIFY(!application->video().offering());
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        QCOMPARE(QDir::toNativeSeparators(QString::fromStdString(application->video().session().path())), n + sep + QStringLiteral("ep.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(QDir::toNativeSeparators(application->audio().path()), n + sep + QStringLiteral("audio") + sep + QStringLiteral("ep.mka"), 20000);
        const QVariantList folders = application->tabMenu(0).value(QStringLiteral("folders")).toList();
        QStringList kinds;
        for (const QVariant &f : folders)
            kinds << f.toMap().value(QStringLiteral("kind")).toString();
        QCOMPARE(kinds, (QStringList{QStringLiteral("subtitles"), QStringLiteral("video"), QStringLiteral("audio"),
                                     QStringLiteral("keyframes")}));
        QTRY_COMPARE(QFileInfo(QString::fromStdString(lastSession().tabs[0].audio)).fileName(), QStringLiteral("ep.mka"));
        // Asked once: the tab shown again asks nothing.
        application->addPage();
        application->selectTab(0);
        QVERIFY(!application->video().offering());
    }

    // Only the folder's video: "Video from directory" with Yes; No loads
    // nothing and is not asked again; "Apply to All" answers the rest of
    // the same opening.
    void theFoldersVideoAndApplyToAll()
    {
        const QString folder = dir.filePath(QStringLiteral("dirvideo"));
        QDir().mkpath(folder);
        QFile::remove(folder + QStringLiteral("/one.mkv"));
        QFile::remove(folder + QStringLiteral("/two.mkv"));
        QVERIFY(QFile::copy(media("cfr.mkv"), folder + QStringLiteral("/one.mkv")));
        QVERIFY(QFile::copy(media("cfr.mkv"), folder + QStringLiteral("/two.mkv")));
        const QString one = writeAss(folder + QStringLiteral("/one.ass"), {}, "one");
        const QString two = writeAss(folder + QStringLiteral("/two.ass"), {}, "two");
        QVariantMap result = application->openDropped({QUrl::fromLocalFile(one), QUrl::fromLocalFile(two)});
        QCOMPARE(titles(), (QStringList{QStringLiteral("one.ass"), QStringLiteral("two.ass")}));
        QVERIFY(application->video().offering());
        QCOMPARE(application->video().offer(), QStringLiteral("Video from directory:\ntwo.mkv"));
        QCOMPARE(application->video().offerAssociatedLabel(), QString());
        QCOMPARE(application->video().offerDirectoryLabel(), QStringLiteral("Yes"));
        // "Apply to All" with No: the first tab's question goes too.
        application->video().setOfferApplyToAll(true);
        application->video().dismissOffer();
        QVERIFY(!application->video().offering());
        application->selectTab(0);
        QVERIFY(!application->video().offering());
        QVERIFY(!application->video().hasVideo());
        // A new opening asks again; Yes loads the folder's video.
        application->selectTab(1);
        result = application->reviewOpen(one); // into two.ass's tab
        QVERIFY(result.value(QStringLiteral("rows")).toList().isEmpty());
        application->finishClose();
        QVERIFY(application->video().offering());
        QCOMPARE(application->video().offer(), QStringLiteral("Video from directory:\none.mkv"));
        application->video().loadFromDirectory();
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        QCOMPARE(QFileInfo(QString::fromStdString(application->video().session().path())).fileName(), QStringLiteral("one.mkv"));
    }

    // Legacy's question was modal and Escape (its escape id) answered No.
    // The inline question does not take the focus from the Document; with
    // the focus in it (Tab, or "Apply to All" clicked) Escape answers No.
    void escapeInTheQuestionAnswersNo()
    {
        const QString folder = dir.filePath(QStringLiteral("escape"));
        QDir().mkpath(folder);
        QFile::remove(folder + QStringLiteral("/esc.mkv"));
        QVERIFY(QFile::copy(media("cfr.mkv"), folder + QStringLiteral("/esc.mkv")));
        const QString subs = writeAss(folder + QStringLiteral("/esc.ass"), {}, "esc");
        QVERIFY(application->openFile(subs));
        QVERIFY(application->video().offering());
        QQuickItem *applyToAll = visualItem(QStringLiteral("associationApplyToAll"));
        QVERIFY(applyToAll && applyToAll->isVisible());
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowFocused(window));
        applyToAll->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(applyToAll->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!application->video().offering());
        QTest::qWait(200);
        QVERIFY(!application->video().hasVideo()); // No: nothing loads
    }

    // OpenFile on a video: subtitles of the same name beside it are offered
    // ("Load subtitles named ...?"); Yes loads them into the tab, without the
    // association question, then the video.
    void aVideoOffersItsSubtitles()
    {
        const QString folder = dir.filePath(QStringLiteral("withsubs"));
        QDir().mkpath(folder);
        QFile::remove(folder + QStringLiteral("/show.mkv"));
        QVERIFY(QFile::copy(media("cfr.mkv"), folder + QStringLiteral("/show.mkv")));
        writeAss(folder + QStringLiteral("/show.ass"), "Video File: show.mkv\n", "show");
        QObject *commands = named("tabCommands");
        QVERIFY(commands);
        QVERIFY(QMetaObject::invokeMethod(commands, "openVideoFile", Q_ARG(QVariant, folder + QStringLiteral("/show.mkv"))));
        QObject *prompt = named("subtitlesFromVideoPrompt");
        QTRY_VERIFY(prompt->property("visible").toBool());
        QCOMPARE(named("subtitlesFromVideoText")->property("text").toString(), QStringLiteral("Load subtitles named \"show.ass\"?"));
        QMetaObject::invokeMethod(prompt, "accept");
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("show.ass")});
        QVERIFY(!application->video().offering());
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        QCOMPARE(QFileInfo(QString::fromStdString(application->video().session().path())).fileName(), QStringLiteral("show.mkv"));
        // The tab has those subtitles now: opening the video asks nothing.
        QCOMPARE(application->openVideoFile(folder + QStringLiteral("/show.mkv")).value(QStringLiteral("subtitles")).toString(),
                 QString());
    }

    // AutoSaveOpen over the legacy Subs folder: files grouped by name,
    // versions newest first, the filter, and Open as a new unsaved copy
    // (L58-recovery-copy) with the autosave left untouched.
    void legacyAutosavesOpenAsCopies()
    {
        const QString subs = home.filePath(QStringLiteral("Subs"));
        QDir().mkpath(subs);
        const QString older = writeAss(subs + QStringLiteral("/episode_0_1.ass"), {}, "older");
        const QString newer = writeAss(subs + QStringLiteral("/episode_0_2.ass"), {}, "newer");
        writeAss(subs + QStringLiteral("/other_1_1.ass"), {}, "other");
        auto setModified = [](const QString &path, const QDateTime &when) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(when, QFileDevice::FileModificationTime));
        };
        setModified(older, QDateTime::currentDateTime().addDays(-2));
        setModified(newer, QDateTime::currentDateTime().addDays(-1));
        const QByteArray olderHash = hash(older), newerHash = hash(newer);
        const QVariantList files = application->legacyAutosaves();
        QCOMPARE(files.size(), 2);
        QVariantMap episode;
        for (const QVariant &f : files)
            if (f.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("episode.ass"))
                episode = f.toMap();
        const QVariantList versions = episode.value(QStringLiteral("versions")).toList();
        QCOMPARE(versions.size(), 2);
        QCOMPARE(QFileInfo(versions[0].toMap().value(QStringLiteral("file")).toString()).fileName(),
                 QStringLiteral("episode_0_2.ass")); // newest first
        const QDateTime written = QFileInfo(newer).lastModified();
        QCOMPARE(versions[0].toMap().value(QStringLiteral("written")).toString(),
                 written.toString(QStringLiteral("MM/dd/yyyy   HH:mm:ss")));
        // FindFiles: words in the lowered name, all of them or any.
        QCOMPARE(application->filterLegacyAutosaves(files, QStringLiteral("EPI ass"), true).size(), 1);
        QCOMPARE(application->filterLegacyAutosaves(files, QStringLiteral("epi zzz"), true).size(), 0);
        QCOMPARE(application->filterLegacyAutosaves(files, QStringLiteral("epi zzz"), false).size(), 1);
        QCOMPARE(application->filterLegacyAutosaves(files, QString(), true).size(), 2);
        // Through Open auto save.
        QObject *recovery = named("recoveryWindow");
        QVERIFY(QMetaObject::invokeMethod(recovery, "showBundles"));
        QTRY_VERIFY(recovery->property("visible").toBool());
        QObject *find = named("legacyAutosaveFind");
        find->setProperty("text", QStringLiteral("episode"));
        QVERIFY(QMetaObject::invokeMethod(named("legacyAutosaveFilter"), "clicked"));
        QObject *versionList = named("legacyAutosaveVersions");
        QTRY_COMPARE(versionList->property("count").toInt(), 2);
        versionList->setProperty("currentIndex", 1); // the older one
        QVERIFY(QMetaObject::invokeMethod(named("legacyAutosaveOpen"), "clicked"));
        QTRY_VERIFY(!recovery->property("visible").toBool());
        QCOMPARE(titles().last(), QStringLiteral("episode_0_1.ass (recovered)"));
        QCOMPARE(application->currentTab(), titles().size() - 1);
        QVERIFY(application->targetUntitled());
        auto *copy = session(application->currentTab());
        QVERIFY(copy->isDirty());
        const auto *line = copy->document().lines().front();
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(line->text.data()), qsizetype(line->text.size())),
                 QStringLiteral("older"));
        QCOMPARE(hash(older), olderHash);
        QCOMPARE(hash(newer), newerHash);
        QCOMPARE(QDir(subs).entryList(QDir::Files).size(), 3);
    }
};

QTEST_MAIN(TabMenuTests)
#include "tab_menu_tests.moc"
