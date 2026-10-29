#include "hikari/app/application.h"
#include "hikari/app/session_json.h"
#include "hikari/app/settings_import_store.h"
#include "hikari/application/session_file.h"
#include "docking.h"
#include "line_table_model.h"
#include "settings_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPalette>
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

}

class TabMenuTests : public QObject {
    Q_OBJECT

    QTemporaryDir home;
    QTemporaryDir dir;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QStringList revealed;
    bool legacyRoots = false;

    void start()
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.revealInFolder = [this](const QString &path) { revealed << path; };
        // MUST KILL: no spell checker so its notice does not cover the window
        o.spellingBackend = {};
        if (!legacyRoots)
            o.legacyAutosaveDir = home.filePath(QStringLiteral("Subs"));
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
        // findChild misses a view's delegates: they are visual children only.
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(w))
                if (QQuickItem *found = findItem(quick->contentItem(), QLatin1String(name)))
                    return found;
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
        return app::readSession(std::string_view(bytes.constData(), bytes.size())).value_or(application::Session{});
    }
    QStringList sessionSubtitles() const
    {
        QStringList out;
        for (const auto &tab : lastSession().tabs)
            out << QFileInfo(QString::fromStdString(tab.subtitles)).fileName();
        return out;
    }
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
        QFile::remove(home.filePath(QStringLiteral("last-session.json")));
        start();
    }
    void cleanup()
    {
        closeTabMenu();
        delete engine;
        engine = nullptr;
        delete application;
        application = nullptr;
        for (const char *name : {"settings.toml", "hotkeys.toml", "state.json"})
            QFile::remove(home.filePath(QLatin1String(name)));
    }

    void menuListsTabsSaveAndFolders()
    {
        restoreThreeTabs();
        QObject *menu = openTabMenu(0);
        QVERIFY(menu);
        QCOMPARE(named("tabMenuTab0")->property("text").toString(), QStringLiteral("a.ass"));
        QCOMPARE(named("tabMenuTab2")->property("text").toString(), QStringLiteral("c.ass"));
        QVERIFY(!named("tabMenuTab0")->property("checked").toBool());
        QVERIFY(named("tabMenuTab2")->property("checked").toBool());
        QVERIFY(!named("tabMenuSave")->property("enabled").toBool());
        QVERIFY(named("tabMenuSaveAll")->property("enabled").toBool());
        QCOMPARE(named("tabMenuSave")->property("text").toString(), QStringLiteral("Save"));
        QCOMPARE(named("tabMenuCloseAll")->property("text").toString(), QStringLiteral("Close all tabs"));
        QCOMPARE(named("tabMenuFolder_subtitles")->property("text").toString(),
                 QStringLiteral("Open the folder containing the subtitles"));
        QCOMPARE(named("tabMenuFolder_video")->property("text").toString(), QStringLiteral("Open video containing folder"));
        QCOMPARE(named("tabMenuFolder_audio")->property("text").toString(), QStringLiteral("Open audio containing folder"));
        QCOMPARE(named("tabMenuFolder_keyframes")->property("text").toString(),
                 QStringLiteral("Open keyframes containing folder"));
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
        QCOMPARE(order.size(), 12);
        trigger("tabMenuFolder_video");
        trigger("tabMenuFolder_subtitles");
        QCOMPARE(revealed, (QStringList{QDir::toNativeSeparators(dir.filePath(QStringLiteral("media/a video.mkv"))),
                                        QDir::toNativeSeparators(dir.filePath(QStringLiteral("tabs/a.ass")))}));
        closeTabMenu();
        edit(1, "b2");
        openTabMenu(1);
        QVERIFY(named("tabMenuSave")->property("enabled").toBool());
        QVERIFY(named("tabMenuFolder_subtitles"));
        QVERIFY(!named("tabMenuFolder_video"));
        QVERIFY(!named("tabMenuFolder_keyframes"));
        closeTabMenu();
        openTabMenu(-1);
        QVERIFY(!named("tabMenuSave")->property("enabled").toBool());
        QVERIFY(!named("tabMenuFolder_subtitles"));
        QCOMPARE(named("tabMenuTab1")->property("text").toString(), QStringLiteral("b.ass"));
    }

    void choosingATabMovesItToTheFirstVisiblePlace()
    {
        restoreThreeTabs();
        const QByteArray before = readAll(application->lastSessionPath());
        auto gridText = [&] {
            auto *lines = application->shell().lines();
            return lines->index(0, ui::LineTableModel::TextColumn).data().toString();
        };
        QCOMPARE(window->title(), QStringLiteral("c.ass - HikariSub"));
        QCOMPARE(gridText(), QStringLiteral("c"));
        openTabMenu(-1);
        trigger("tabMenuTab0");
        QCOMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.ass")}));
        QCOMPARE(application->currentTab(), 0);
        QCOMPARE(window->title(), QStringLiteral("a.ass - HikariSub"));
        QCOMPARE(gridText(), QStringLiteral("a"));
        closeTabMenu();
        openTabMenu(-1);
        trigger("tabMenuTab2");
        QCOMPARE(titles(), (QStringList{QStringLiteral("c.ass"), QStringLiteral("b.ass"), QStringLiteral("a.ass")}));
        QCOMPARE(application->currentTab(), 0);
        QCOMPARE(application->workspace().title(*application->workspace().editingTarget())->c_str(), std::string("c.ass"));
        QCOMPARE(window->title(), QStringLiteral("c.ass - HikariSub"));
        QCOMPARE(gridText(), QStringLiteral("c"));
        application->chooseTab(0, 1);
        QCOMPARE(titles(), (QStringList{QStringLiteral("b.ass"), QStringLiteral("c.ass"), QStringLiteral("a.ass")}));
        QCOMPARE(application->currentTab(), 1);
        QCOMPARE(readAll(application->lastSessionPath()), before);
    }

    void draggingATabReordersAndKeepsTheOrderInTheSession()
    {
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
        QCOMPARE(sessionSubtitles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.ass")}));
        for (int step = 1; step <= 10; ++step)
            QTest::mouseMove(window, over1 + (over2 - over1) * step / 10);
        QTRY_COMPARE(titles(), (QStringList{QStringLiteral("b.ass"), QStringLiteral("c.ass"), QStringLiteral("a.ass")}));
        QCOMPARE(application->currentTab(), 2);
        QTest::mouseRelease(window, Qt::LeftButton, {}, over2);
        QTRY_COMPARE(sessionSubtitles(), (QStringList{QStringLiteral("b.ass"), QStringLiteral("c.ass"), QStringLiteral("a.ass")}));
        const auto tabs = lastSession().tabs;
        QCOMPARE(QFileInfo(QString::fromStdString(tabs[1].video)).fileName(), QStringLiteral("a video.mkv"));
        QCOMPARE(QString::fromStdString(tabs[2].video), QString());
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

    void saveAndSaveAllFromTheTabMenu()
    {
        restoreThreeTabs();
        edit(1, "b saved");
        application->addPage();
        edit(3, "untitled one");
        application->addPage();
        edit(4, "untitled two");
        application->addPage();
        application->selectTab(0);
        openTabMenu(1);
        trigger("tabMenuSave");
        QTRY_VERIFY(!session(1)->isDirty());
        QVERIFY(readAll(dir.filePath(QStringLiteral("tabs/b.ass"))).contains("b saved"));
        QCOMPARE(application->currentTab(), 0);
        closeTabMenu();
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
        edit(0, "a saved");
        const QVariantList queue = application->saveAll();
        QCOMPARE(queue.size(), 2);
        QCOMPARE(queue[0].toMap().value(QStringLiteral("id")).toULongLong(), untitled);
        QCOMPARE(queue[1].toMap().value(QStringLiteral("id")).toULongLong(), application->tabDocument(4));
        QTRY_VERIFY(!session(0)->isDirty());
        QVERIFY(readAll(dir.filePath(QStringLiteral("tabs/a.ass"))).contains("a saved"));
        openTabMenu(0);
        trigger("tabMenuSaveAll");
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("document").toULongLong(), untitled);
        QMetaObject::invokeMethod(dialog, "reject");
        QTRY_COMPARE(dialog->property("document").toULongLong(), application->tabDocument(4));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QMetaObject::invokeMethod(dialog, "reject");
        const QString chosen = dir.filePath(QStringLiteral("saved/untitled"));
        QDir().mkpath(dir.filePath(QStringLiteral("saved")));
        QCOMPARE(application->saveChosenFor(untitled, QUrl::fromLocalFile(chosen)), QString());
        QTRY_VERIFY(!session(3)->isDirty());
        QVERIFY(readAll(chosen + QStringLiteral(".ass")).contains("untitled one"));
        QTRY_COMPARE(titles()[3], QStringLiteral("untitled.ass"));
        QCOMPARE(application->currentTab(), 0);
    }

    void closeAllTabsAsksThenReviews()
    {
        restoreThreeTabs();
        edit(2, "c changed");
        const QByteArray before = readAll(application->lastSessionPath());
        openTabMenu(1);
        trigger("tabMenuCloseAll");
        QTRY_VERIFY(named("closeAllPrompt"));
        QObject *prompt = named("closeAllPrompt");
        QTRY_VERIFY(prompt->property("visible").toBool());
        QCOMPARE(prompt->property("title").toString(), QStringLiteral("Close all tabs"));
        QVERIFY(QMetaObject::invokeMethod(prompt, "answer", Q_ARG(QVariant, QStringLiteral("cancel"))));
        QTRY_VERIFY(!prompt->property("visible").toBool());
        QCOMPARE(titles().size(), 3);
        openTabMenu(1);
        trigger("tabMenuCloseAll");
        QTRY_VERIFY(prompt->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(prompt, "answer", Q_ARG(QVariant, QStringLiteral("close"))));
        QObject *review = named("unsavedChanges");
        QTRY_VERIFY(review->property("opened").toBool());
        const QVariantList rows = review->property("rows").toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("c.ass"));
        QSignalSpy finished(application, &app::Application::closeFinished);
        application->resolveClose({QVariantMap{{QStringLiteral("id"), rows[0].toMap().value(QStringLiteral("id"))},
                                               {QStringLiteral("save"), false},
                                               {QStringLiteral("path"), QString()}}});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(titles().isEmpty());
        QCOMPARE(readAll(application->lastSessionPath()), before);
        QVERIFY(!readAll(dir.filePath(QStringLiteral("tabs/c.ass"))).contains("c changed"));
        QMetaObject::invokeMethod(review, "close");
    }

    void closeAllTabsReviewsOnceAndCancelClosesNothing()
    {
        restoreThreeTabs();
        edit(0, "a changed");
        edit(2, "c changed");
        QObject *review = named("unsavedChanges");
        QVERIFY(review);
        openTabMenu(1);
        trigger("tabMenuCloseAll");
        QTRY_VERIFY(named("closeAllPrompt"));
        QObject *prompt = named("closeAllPrompt");
        QTRY_VERIFY(prompt->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(prompt, "answer", Q_ARG(QVariant, QStringLiteral("close"))));
        QTRY_VERIFY(review->property("opened").toBool());
        QVariantList rows = review->property("rows").toList();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("a.ass"));
        QCOMPARE(rows[1].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("c.ass"));
        QObject *cancel = named("closeCancel");
        QVERIFY(cancel);
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QTRY_VERIFY(!review->property("opened").toBool());
        QCOMPARE(titles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.ass")}));
        QVERIFY(session(0)->isDirty());
        QVERIFY(session(2)->isDirty());
        QCOMPARE(application->currentTab(), 2);
        openTabMenu(1);
        trigger("tabMenuCloseAll");
        QTRY_VERIFY(prompt->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(prompt, "answer", Q_ARG(QVariant, QStringLiteral("close"))));
        QTRY_VERIFY(review->property("opened").toBool());
        QCOMPARE(review->property("rows").toList().size(), 2);
        QSignalSpy finished(application, &app::Application::closeFinished);
        QVERIFY(QMetaObject::invokeMethod(named("closeWithoutSaving"), "clicked"));
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(titles().isEmpty());
        QVERIFY(named("homePage")->property("visible").toBool());
        QVERIFY(!readAll(dir.filePath(QStringLiteral("tabs/a.ass"))).contains("a changed"));
        QVERIFY(!readAll(dir.filePath(QStringLiteral("tabs/c.ass"))).contains("c changed"));
        QMetaObject::invokeMethod(review, "close");
    }

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
        QTest::keyPress(window, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(!application->revealRecent(file));
        QTest::keyRelease(window, Qt::Key_Shift, Qt::NoModifier);
        trigger("recentSubtitles0");
        QTRY_COMPARE(titles().last(), QStringLiteral("r.ass"));
        QCOMPARE(revealed.size(), 1);
    }

    void openSubtitlesInANewTab()
    {
        const QString one = writeAss(dir.filePath(QStringLiteral("new/one.ass")), {}, "one");
        const QString two = writeAss(dir.filePath(QStringLiteral("new/two.ass")), {}, "two");
        const QString three = writeAss(dir.filePath(QStringLiteral("new/three.ass")), {}, "three");
        application->settingsStore()->set("subtitles.openInNewTab", true);
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first(), "openSubtitles", Q_ARG(QVariant, one)));
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("one.ass")});
        edit(0, "one changed");
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first(), "openSubtitles", Q_ARG(QVariant, two)));
        QCOMPARE(titles(), (QStringList{QStringLiteral("one.ass"), QStringLiteral("two.ass")}));
        QCOMPARE(application->currentTab(), 1);
        QVERIFY(session(0)->isDirty());
        QVERIFY(!named("unsavedChanges")->property("opened").toBool());
        QVERIFY(lastSession().tabs.size() == 2);
        application->settingsStore()->set("subtitles.openInNewTab", false);
        application->selectTab(0);
        const QVariantMap result = application->reviewOpen(three);
        QCOMPARE(result.value(QStringLiteral("rows")).toList().size(), 1);
        application->cancelClose();
        QCOMPARE(titles(), (QStringList{QStringLiteral("one.ass"), QStringLiteral("two.ass")}));
    }

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
        const QVariantList folders = application->tabMenu(0).value(QStringLiteral("folders")).toList();
        QCOMPARE(folders.size(), 2);
        QCOMPARE(folders[1].toMap().value(QStringLiteral("path")).toString(), QDir::toNativeSeparators(video));
        QCOMPARE(application->tabMenu(1).value(QStringLiteral("folders")).toList().size(), 1);
        QCOMPARE(sessionSubtitles(), (QStringList{QStringLiteral("a.ass"), QStringLiteral("b.ass"), QStringLiteral("c.srt")}));
        application->addPage();
        edit(3, "mine");
        result = application->openDropped({QUrl::fromLocalFile(a), QUrl::fromLocalFile(b)});
        QCOMPARE(result.value(QStringLiteral("rows")).toList().size(), 1);
        QCOMPARE(titles().size(), 4);
        application->cancelClose();
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

    void droppedSubtitlesWithAVideoReviewADirtyUntitledTab()
    {
        const QString a = writeAss(dir.filePath(QStringLiteral("pair/a.ass")), {}, "a");
        const QString b = writeAss(dir.filePath(QStringLiteral("pair/b.ass")), {}, "b");
        const QString video = dir.filePath(QStringLiteral("pair/v.mkv"));
        QFile::remove(video);
        QVERIFY(QFile::copy(media("cfr.mkv"), video));
        application->addPage();
        const QStringList before = titles();
        const int mine = before.size() - 1;
        edit(mine, "mine");
        QVERIFY(application->targetUntitled());
        const QList<QUrl> drop{QUrl::fromLocalFile(b), QUrl::fromLocalFile(video), QUrl::fromLocalFile(a)};
        QVariantMap result = application->openDropped(drop);
        QVariantList rows = result.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(titles(), before);
        QVERIFY(session(mine)->isDirty());
        application->cancelClose();
        QCOMPARE(titles(), before + QStringList{QStringLiteral("b.ass")});
        QVERIFY(session(mine)->isDirty());
        QCOMPARE(application->tabMenu(mine).value(QStringLiteral("folders")).toList().size(), 0);
        application->selectTab(mine);
        result = application->openDropped(drop);
        rows = result.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 1);
        QSignalSpy finished(application, &app::Application::closeFinished);
        application->resolveClose({QVariantMap{{QStringLiteral("id"), rows[0].toMap().value(QStringLiteral("id"))},
                                               {QStringLiteral("save"), false},
                                               {QStringLiteral("path"), QString()}}});
        QTRY_COMPARE(finished.size(), 1);
        QStringList expected = before;
        expected[mine] = QStringLiteral("a.ass");
        expected << QStringLiteral("b.ass") << QStringLiteral("b.ass");
        QCOMPARE(titles(), expected);
        const QVariantList folders = application->tabMenu(mine).value(QStringLiteral("folders")).toList();
        QCOMPARE(folders.size(), 2);
        QCOMPARE(folders[1].toMap().value(QStringLiteral("path")).toString(), QDir::toNativeSeparators(video));
    }

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
        application->addPage();
        application->selectTab(0);
        QVERIFY(!application->video().offering());
    }

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
        application->video().setOfferApplyToAll(true);
        application->video().dismissOffer();
        QVERIFY(!application->video().offering());
        application->selectTab(0);
        QVERIFY(!application->video().offering());
        QVERIFY(!application->video().hasVideo());
        application->selectTab(1);
        result = application->reviewOpen(one);
        QVERIFY(result.value(QStringLiteral("rows")).toList().isEmpty());
        application->finishClose();
        QVERIFY(application->video().offering());
        QCOMPARE(application->video().offer(), QStringLiteral("Video from directory:\none.mkv"));
        application->video().loadFromDirectory();
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        QCOMPARE(QFileInfo(QString::fromStdString(application->video().session().path())).fileName(), QStringLiteral("one.mkv"));
    }

    void lyricsAreOfferedTheFoldersVideo()
    {
        const QString folder = dir.filePath(QStringLiteral("lyricsvideo"));
        QDir().mkpath(folder);
        QFile::remove(folder + QStringLiteral("/song.mkv"));
        QVERIFY(QFile::copy(media("cfr.mkv"), folder + QStringLiteral("/song.mkv")));
        QFile lrc(folder + QStringLiteral("/song.lrc"));
        QVERIFY(lrc.open(QIODevice::WriteOnly));
        lrc.write("[00:01.00]<00:01.00>la <00:02.00>la\n");
        lrc.close();
        QVERIFY(application->openFile(lrc.fileName()));
        QCOMPARE(titles().last(), QStringLiteral("song.ass"));
        QVERIFY(application->video().offering());
        QCOMPARE(application->video().offer(), QStringLiteral("Video from directory:\nsong.mkv"));
        application->video().dismissOffer();
        QVERIFY(!application->video().offering());
    }

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
        QVERIFY(!application->video().hasVideo());
    }

    void theQuestionIsOpaqueBesideTheToolRail()
    {
        const QString folder = dir.filePath(QStringLiteral("opaque"));
        QDir().mkpath(folder);
        QFile::remove(folder + QStringLiteral("/op.mkv"));
        QVERIFY(QFile::copy(media("cfr.mkv"), folder + QStringLiteral("/op.mkv")));
        const QString subs = writeAss(folder + QStringLiteral("/op.ass"), "Video File: op.mkv\n", "op");
        QVERIFY(application->openFile(subs));
        QVERIFY(application->video().offering());
        QQuickItem *offer = visualItem(QStringLiteral("associationOffer"));
        QQuickItem *rail = visualItem(QStringLiteral("visualToolRail"));
        QVERIFY(offer && rail);
        QTRY_VERIFY(offer->isVisible() && offer->width() > 0 && offer->height() > 0);
        QVERIFY(rail->isVisible());
        const QRectF offerRect = offer->mapRectToScene(QRectF(0, 0, offer->width(), offer->height()));
        const QRectF railRect = rail->mapRectToScene(QRectF(0, 0, rail->width(), rail->height()));
        QVERIFY2(!offerRect.intersects(railRect), "the question covers the tool rail");
        QCOMPARE(offerRect.left(), railRect.right());
        auto *fill = findItem(offer, QStringLiteral("associationOfferBackground"));
        QVERIFY(fill);
        const QColor panel = fill->property("color").value<QColor>();
        QCOMPARE(panel.alpha(), 255);
        QCOMPARE(panel, QGuiApplication::palette().color(QPalette::Window));
        const QImage shot = window->grabWindow();
        const qreal dpr = shot.devicePixelRatio();
        for (const QPointF at : {offerRect.topLeft() + QPointF(3, 3), offerRect.bottomLeft() + QPointF(3, -3)})
            QCOMPARE(QColor(shot.pixel((at * dpr).toPoint())), QColor(panel.rgb()));
        application->video().dismissOffer();
        QVERIFY(!offer->isVisible());
    }

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
        QTRY_VERIFY(named("subtitlesFromVideoPrompt"));
        QObject *prompt = named("subtitlesFromVideoPrompt");
        QTRY_VERIFY(prompt->property("visible").toBool());
        QCOMPARE(prompt->property("text").toString(), QStringLiteral("Open “show.ass” with the video?"));
        QVERIFY(QMetaObject::invokeMethod(prompt, "answer", Q_ARG(QVariant, QStringLiteral("both"))));
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("show.ass")});
        QVERIFY(!application->video().offering());
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        QCOMPARE(QFileInfo(QString::fromStdString(application->video().session().path())).fileName(), QStringLiteral("show.mkv"));
        QCOMPARE(application->openVideoFile(folder + QStringLiteral("/show.mkv")).value(QStringLiteral("subtitles")).toString(),
                 QString());
    }

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
                 QStringLiteral("episode_0_2.ass"));
        const QDateTime written = QFileInfo(newer).lastModified();
        QCOMPARE(versions[0].toMap().value(QStringLiteral("written")).toString(),
                 written.toString(QStringLiteral("MM/dd/yyyy   HH:mm:ss")));
        QCOMPARE(application->filterLegacyAutosaves(files, QStringLiteral("EPI ass"), true).size(), 1);
        QCOMPARE(application->filterLegacyAutosaves(files, QStringLiteral("epi zzz"), true).size(), 0);
        QCOMPARE(application->filterLegacyAutosaves(files, QStringLiteral("epi zzz"), false).size(), 1);
        QCOMPARE(application->filterLegacyAutosaves(files, QString(), true).size(), 2);
        QVERIFY(QMetaObject::invokeMethod(named("autosaves"), "show", Q_ARG(QVariant, false)));
        QTRY_VERIFY(named("autosavesWindow"));
        QObject *recovery = named("autosavesWindow");
        QTRY_VERIFY(recovery->property("visible").toBool());
        named("autosavesSearch")->setProperty("text", QStringLiteral("episode"));
        QTRY_COMPARE(named("autosavesList")->property("count").toInt(), 1);
        named("autosavesSearch")->setProperty("text", QStringLiteral("subs"));
        QTRY_COMPARE(named("autosavesList")->property("count").toInt(), 2);
        named("autosavesSearch")->setProperty("text", QStringLiteral("subs episode"));
        QTRY_COMPARE(named("autosavesList")->property("count").toInt(), 1);
        QTRY_VERIFY(named("autosaveVersion0"));
        QObject *versionList = named("autosaveVersion0");
        QTRY_COMPARE(versionList->property("count").toInt(), 2);
        QVERIFY(!named("autosaveDelete0")->property("enabled").toBool());
        versionList->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(named("autosaveOpen0"), "clicked"));
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

    void legacyAutosavesComeFromTheLegacyRoot()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString legacy = data.filePath(QStringLiteral("HikariSub/Legacy"));
        QVERIFY(QDir().mkpath(legacy + QStringLiteral("/Config")) && QDir().mkpath(legacy + QStringLiteral("/Subs")));
        writeAss(legacy + QStringLiteral("/Config/Config.txt"), {}, "config");
        writeAss(legacy + QStringLiteral("/Subs/legacy_0_1.ass"), {}, "legacy");
        QVERIFY(QDir().mkpath(home.filePath(QStringLiteral("Subs"))));
        writeAss(home.filePath(QStringLiteral("Subs/own_0_1.ass")), {}, "own");
#ifdef _WIN32
        const char *variable = "LOCALAPPDATA";
#else
        const char *variable = "XDG_DATA_HOME";
#endif
        const QByteArray before = qgetenv(variable);
        const bool wasSet = qEnvironmentVariableIsSet(variable);
        const auto restore = qScopeGuard([&] {
            if (wasSet)
                qputenv(variable, before);
            else
                qunsetenv(variable);
            legacyRoots = false;
            QFile::remove(home.filePath(QStringLiteral("Subs/own_0_1.ass")));
        });
        qputenv(variable, QDir::toNativeSeparators(data.path()).toLocal8Bit());
        cleanup();
        legacyRoots = true;
        start();
        const QVariantList files = application->legacyAutosaves();
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("legacy.ass"));
        const QString file = files.first().toMap().value(QStringLiteral("versions")).toList().first().toMap().value(
            QStringLiteral("file")).toString();
        QCOMPARE(QFileInfo(file).absolutePath(), QFileInfo(legacy + QStringLiteral("/Subs")).absoluteFilePath());
    }

    void legacyAutosavesComeFromTheImportedRoot()
    {
        QTemporaryDir data;
        QTemporaryDir picked;
        QVERIFY(data.isValid() && picked.isValid());
        QByteArray config = "[HikariSub v0.0.1-rc.1]\r\n";
        for (int i = 1; i <= 11; ++i)
            config += "EDITBOX_TAG_BUTTON_VALUE" + QByteArray::number(i) + "=\r\n";
        QVERIFY(QDir().mkpath(picked.filePath(QStringLiteral("Config"))));
        QFile configFile(picked.filePath(QStringLiteral("Config/Config.txt")));
        QVERIFY(configFile.open(QIODevice::WriteOnly));
        configFile.write(config + "GRID_FONT=Arial\r\n");
        configFile.close();
        writeAss(picked.filePath(QStringLiteral("Subs/picked_0_1.ass")), {}, "picked");
#ifdef _WIN32
        const char *variable = "LOCALAPPDATA";
#else
        const char *variable = "XDG_DATA_HOME";
#endif
        const QByteArray before = qgetenv(variable);
        const bool wasSet = qEnvironmentVariableIsSet(variable);
        const auto restore = qScopeGuard([&] {
            if (wasSet)
                qputenv(variable, before);
            else
                qunsetenv(variable);
            legacyRoots = false;
            QDir(home.filePath(QStringLiteral("Import"))).removeRecursively();
        });
        qputenv(variable, QDir::toNativeSeparators(data.path()).toLocal8Bit());
        cleanup();
        {
            ui::SettingsStore settings(home.filePath(QStringLiteral("settings.toml")));
            app::SettingsImportStore store(settings, home.path());
            const auto root = app::SettingsImportStore::inspectRoot(picked.path());
            QVERIFY(root);
            const auto snapshot = store.snapshot(*root);
            QVERIFY(snapshot);
            const auto plan = hikari::application::settings_import::buildPlan(snapshot->sources, store.destination());
            QCOMPARE(store.activate(plan, hikari::application::settings_import::proposedRows(plan), *snapshot, store.revision()),
                     app::SettingsImportStore::Result::Activated);
            settings.sync();
        }
        legacyRoots = true;
        start();
        const QVariantList files = application->legacyAutosaves();
        QCOMPARE(files.size(), 1);
        QCOMPARE(files.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("picked.ass"));
    }

    void noLegacySubsFolderLogsNothing()
    {
        const QString subs = home.filePath(QStringLiteral("Subs"));
        QVERIFY(QDir(subs).removeRecursively());
        QVERIFY(!QFileInfo::exists(subs));
        const QString before = application->log().history();
        QVERIFY(application->legacyAutosaves().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(named("autosaves"), "show", Q_ARG(QVariant, false)));
        QTRY_VERIFY(named("autosavesWindow"));
        QObject *recovery = named("autosavesWindow");
        QTRY_VERIFY(recovery->property("visible").toBool());
        QCOMPARE(application->log().history(), before);
        QVERIFY(!application->log().history().contains(QStringLiteral("auto save folder")));
        QMetaObject::invokeMethod(recovery, "reject");
        QDir().mkpath(subs);
        QVERIFY(application->legacyAutosaves().isEmpty());
#ifdef _WIN32
        QCOMPARE(application->log().lastMessage(), QStringLiteral("Auto save folder is empty"));
#else
        QCOMPARE(application->log().lastMessage(), QStringLiteral("Cannot open auto save folder"));
#endif
    }
};

QTEST_MAIN(TabMenuTests)
#include "tab_menu_tests.moc"
