#include "hikari/app/application.h"
#include "hikari/application/grid_clipboard.h"
#include "hikari/application/settings.h"
#include "docking.h"
#include "line_table_model.h"
#include "theme.h"

#include <QCryptographicHash>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using ui::LineTableModel;

namespace {

constexpr const char *kStyles =
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n";

QString write(const QTemporaryDir &dir, const char *name, const QByteArray &styles, const QByteArray &events)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    QByteArray styleLines(kStyles);
    for (const QByteArray &style : styles.split(','))
        styleLines += "Style: " + style + ",Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,"
                                          "2,2,2,10,10,10,1\n";
    f.write("[Script Info]\nScriptType: v4.00+\n\n" + styleLines +
            "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" + events);
    return path;
}

QByteArray hash(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256) : QByteArray();
}

}

class ComparisonTest : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString first, second;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

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
    QObject *openTabMenu(int index)
    {
        auto *bar = named("documentTabBar");
        if (!bar)
            return nullptr;
        QMetaObject::invokeMethod(bar, "openTabMenu", Q_ARG(QVariant, index), Q_ARG(QVariant, QVariant::fromValue(bar)),
                                  Q_ARG(QVariant, 10), Q_ARG(QVariant, 5));
        // MUST KILL: flushes deferred deletes so the previous opening's style items are gone
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return named("subtitleComparisonMenu");
    }
    void closeTabMenu()
    {
        if (auto *menu = named("documentTabMenu"))
            QMetaObject::invokeMethod(menu, "close");
    }
    bool enabled(const char *name) const { return named(name)->property("enabled").toBool(); }
    bool checked(const char *name) const { return named(name)->property("checked").toBool(); }
    void trigger(const char *name)
    {
        QObject *item = named(name);
        QVERIFY2(item, name);
        QVERIFY(QMetaObject::invokeMethod(item, "toggle") || !item->property("checkable").toBool());
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
    }
    QAbstractItemModel *grid(const char *name) const
    {
        return named(name)->property("model").value<QAbstractItemModel *>();
    }
    QString states(const char *gridName = "editingGrid") const
    {
        QString out;
        auto *model = grid(gridName);
        for (int r = 0; r < model->rowCount(); ++r) {
            const int state = model->index(r, 0).data(LineTableModel::ComparisonRole).toInt();
            out += state == 2 ? QLatin1Char('x') : state == 1 ? QLatin1Char('=') : QLatin1Char('.');
        }
        return out;
    }
    application::EditSession &session(int tab) const
    {
        return *application->files().session(application->workspace().tabs()[std::size_t(tab)]);
    }
    int type() const { return application->settingsStore()->integer("comparison.type"); }

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
    QQuickItem *entryOf(QObject *menu) const
    {
        for (QQuickItem *item : engine->rootObjects().first()->findChildren<QQuickItem *>())
            if (item->property("subMenu").value<QObject *>() == menu)
                return item;
        return nullptr;
    }
    static void click(QQuickItem *item, Qt::MouseButton button = Qt::LeftButton)
    {
        QVERIFY(item);
        const QPoint at = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::mouseClick(item->window(), button, {}, at);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        first = write(dir, "first.ass", "Default,Sign",
                      "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                      "Dialogue: 0,0:00:03.00,0:00:04.00,Sign,,0,0,0,,two\n"
                      "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,three\n");
        second = write(dir, "second.ass", "Sign,Other,Default",
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                       "Dialogue: 0,0:00:03.50,0:00:04.00,Sign,,0,0,0,,too\n"
                       "Dialogue: 0,0:00:05.00,0:00:06.00,Sign,,0,0,0,,three\n"
                       "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,four\n");
    }
    void init()
    {
        application = new app::Application;
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(application->openFile(first));
        QVERIFY(application->openFile(second));
        application->selectTab(0);
        QCOMPARE(application->currentTab(), 0);
    }
    void cleanup()
    {
        closeTabMenu();
        delete engine;
        delete application;
    }

    void menuIsEnabledOnAnotherTab()
    {
        QObject *menu = openTabMenu(-1);
        QVERIFY(menu);
        QVERIFY(!menu->property("enabled").toBool());
        QVERIFY(!entryOf(menu)->isEnabled());
        closeTabMenu();
        openTabMenu(0);
        QVERIFY(!menu->property("enabled").toBool());
        QVERIFY(!enabled("compareSubtitles"));
        closeTabMenu();
        openTabMenu(1);
        QTRY_VERIFY(named("documentTabMenu")->property("opened").toBool());
        QVERIFY(menu->property("enabled").toBool());
        QVERIFY(entryOf(menu)->isEnabled());
        for (const char *name : {"compareByTimes", "compareByVisible", "compareByStyles", "compareSubtitles"})
            QVERIFY2(enabled(name), name);
        QVERIFY(!enabled("turnOffComparison"));
        QVERIFY(!session(1).selection().selected.empty());
        QVERIFY(enabled("compareBySelections"));
        closeTabMenu();
        session(1).setSelection({});
        QVERIFY(session(1).selection().selected.empty());
        openTabMenu(1);
        QVERIFY(!enabled("compareBySelections"));
        QVERIFY(enabled("compareByTimes"));
        trigger("compareSubtitles");
        openTabMenu(0);
        QVERIFY(menu->property("enabled").toBool());
        QVERIFY(!enabled("compareSubtitles"));
        QVERIFY(enabled("turnOffComparison"));
        QVERIFY(!enabled("compareByTimes"));
        closeTabMenu();
        openTabMenu(-1);
        QVERIFY(menu->property("enabled").toBool());
    }

    void menuShowsTheSavedCriteria()
    {
        application->settingsStore()->set("comparison.type", 1 | 8);
        application->settingsStore()->set("comparison.styles", QStringList{"Sign"});
        openTabMenu(1);
        QVERIFY(checked("compareByTimes"));
        QVERIFY(checked("compareByVisible"));
        QVERIFY(!checked("compareBySelections"));
        QVERIFY(!checked("compareByStyles"));
        QVERIFY(named("compareStyle_Default"));
        QVERIFY(!checked("compareStyle_Default"));
        QVERIFY(checked("compareStyle_Sign"));
        QVERIFY(!named("compareStyle_Other"));
        QVERIFY(checked("compareByChosenStyles"));
        const auto items = application->openComparisonMenu(1).value(QStringLiteral("styleItems")).toList();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Default"));
        QCOMPARE(items[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Sign"));
    }

    void criteriaAreSavedAsTheyChange()
    {
        openTabMenu(1);
        QCOMPARE(type(), 0);
        trigger("compareByTimes");
        QCOMPARE(type(), 1);
        openTabMenu(1);
        QVERIFY(checked("compareByTimes"));
        trigger("compareByStyles");
        QCOMPARE(type(), 1 | 2);
        openTabMenu(1);
        trigger("compareByVisible");
        openTabMenu(1);
        trigger("compareByTimes");
        QCOMPARE(type(), 2 | 8);
        openTabMenu(1);
        QVERIFY(!checked("compareByChosenStyles"));
        trigger("compareStyle_Sign");
        QCOMPARE(application->settingsStore()->list("comparison.styles"), QStringList{"Sign"});
        QCOMPARE(type(), 2 | 4 | 8);
        QVERIFY(checked("compareByChosenStyles"));
        trigger("compareStyle_Sign");
        QVERIFY(application->settingsStore()->list("comparison.styles").isEmpty());
        QCOMPARE(type(), 2 | 8);
        QVERIFY(!checked("compareByChosenStyles"));
    }

    void compareColoursBothDocumentsAndWritesNeither()
    {
        const QByteArray before1 = hash(first), before2 = hash(second);
        const auto history1 = session(0).historySize(), history2 = session(1).historySize();
        openTabMenu(1);
        trigger("compareSubtitles");
        const auto &c = application->comparison();
        QVERIFY(c.active());
        QCOMPARE(c.first(), application->workspace().tabs()[0]);
        QCOMPARE(c.second(), application->workspace().tabs()[1]);
        QCOMPARE(states(), QStringLiteral("=x="));
        auto *model = grid("editingGrid");
        QCOMPARE(model->index(1, 0).data(LineTableModel::ComparisonMarksRole).toList(), (QVariantList{1, 1}));
        application->selectTab(1);
        QCOMPARE(states(), QStringLiteral("=x=."));
        application->selectTab(0);
        QCOMPARE(session(0).historySize(), history1);
        QCOMPARE(session(1).historySize(), history2);
        QVERIFY(!session(0).isDirty());
        QVERIFY(!session(1).isDirty());
        QCOMPARE(hash(first), before1);
        QCOMPARE(hash(second), before2);
        openTabMenu(1);
        trigger("turnOffComparison");
        QVERIFY(!c.active());
        QCOMPARE(states(), QStringLiteral("..."));
        application->selectTab(1);
        QCOMPARE(states(), QStringLiteral("...."));
    }

    void criteriaChooseThePairs()
    {
        openTabMenu(1);
        trigger("compareByTimes");
        openTabMenu(1);
        trigger("compareSubtitles");
        QCOMPARE(states(), QStringLiteral("=.="));
        openTabMenu(1);
        trigger("compareByStyles");
        openTabMenu(1);
        trigger("compareSubtitles");
        QCOMPARE(states(), QStringLiteral("=.."));
        openTabMenu(1);
        trigger("compareByTimes");
        openTabMenu(1);
        trigger("compareByStyles");
        openTabMenu(1);
        trigger("compareStyle_Sign");
        openTabMenu(1);
        trigger("compareSubtitles");
        QCOMPARE(states(), QStringLiteral(".x."));
    }

    void editsAndUndoCompareAgain()
    {
        application->settingsStore()->set("comparison.type", 16);
        const auto lines = session(0).document().lines();
        application->selectLine(lines[1]->id.value);
        application->selectTab(1);
        application->selectLine(session(1).document().lines()[1]->id.value);
        application->selectTab(0);
        QVERIFY(application->compareWithTab(1));
        QCOMPARE(states(), QStringLiteral(".x."));
        application->selectLine(lines[0]->id.value);
        QCOMPARE(states(), QStringLiteral(".x."));
        auto &editor = application->editor();
        editor.textEdited(QStringLiteral("too"), 3);
        QVERIFY(editor.commit());
        QCOMPARE(states(), QStringLiteral("=.."));
        QVERIFY(editor.undo());
        QCOMPARE(states(), QStringLiteral("x.."));
        application->selectTab(1);
        QCOMPARE(states(), QStringLiteral(".x.."));
        editor.textEdited(QStringLiteral("one"), 3);
        QVERIFY(editor.commit());
        QCOMPARE(states(), QStringLiteral(".=.."));
        application->selectTab(0);
        QCOMPARE(states(), QStringLiteral("=.."));
    }

    void openingAHiddenBlockComparesAgainOnlyByVisibleLines()
    {
        for (const bool visible : {false, true}) {
            cleanup();
            init();
            application->settingsStore()->set("comparison.type", 16 | (visible ? 8 : 0));
            const auto lines = session(0).document().lines();
            application->selectLine(lines[1]->id.value);
            application->selectTab(1);
            application->selectAllLines();
            application->selectTab(0);
            QVERIFY(application->compareWithTab(1));
            QCOMPARE(states(), QStringLiteral(".x."));
            QVERIFY(application->hideSelectedLines());
            application->selectLine(lines[0]->id.value);
            QVERIFY(application->toggleHiddenBlock(0));
            QVERIFY(session(0).document().lines()[1]->visibility != core::LineVisibility::Hidden);
            QCOMPARE(states(), visible ? QStringLiteral("=..") : QStringLiteral(".x."));
        }
    }

    void closingATabTurnsComparisonOff()
    {
        application->openFile(write(dir, "third.ass", "Default", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x\n"));
        application->selectTab(0);
        QVERIFY(application->compareWithTab(1));
        QVERIFY(application->comparison().active());
        QVERIFY(application->reviewCloseTab(2).isEmpty());
        application->finishClose();
        QVERIFY(!application->comparison().active());
        QCOMPARE(states(), QStringLiteral("..."));
    }

    void rightClickOnATabComparesIt()
    {
        click(findItem(window->contentItem(), QStringLiteral("documentTab1")), Qt::RightButton);
        QObject *tabMenu = named("documentTabMenu");
        QTRY_VERIFY(tabMenu->property("opened").toBool());
        QObject *menu = named("subtitleComparisonMenu");
        QCOMPARE(menu->property("tabIndex").toInt(), 1);
        click(entryOf(menu));
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *styles = named("compareByChosenStyles");
        QVERIFY(!styles->property("checked").toBool());
        click(qobject_cast<QQuickItem *>(styles));
        QObject *stylesMenu = named("compareStylesMenu");
        QTRY_VERIFY(stylesMenu->property("opened").toBool());
        QVERIFY(!styles->property("checked").toBool());
        // A Qt Quick submenu takes a click only once its parent menu's cascade timer has run.
        QTest::qWait(300);
        click(qobject_cast<QQuickItem *>(named("compareStyle_Sign")));
        QCOMPARE(application->settingsStore()->list("comparison.styles"), QStringList{"Sign"});
        QTest::qWait(100);
        QVERIFY(tabMenu->property("visible").toBool());
        QVERIFY(menu->property("visible").toBool());
        QVERIFY(stylesMenu->property("visible").toBool());
        QVERIFY(named("compareStyle_Sign")->property("checked").toBool());
        QVERIFY(styles->property("checked").toBool());
        click(qobject_cast<QQuickItem *>(named("compareSubtitles")));
        QTRY_VERIFY(application->comparison().active());
        QCOMPARE(states(), QStringLiteral(".x."));
        QTRY_VERIFY(!tabMenu->property("visible").toBool());
        QTRY_VERIFY(!stylesMenu->property("visible").toBool());
        click(findItem(window->contentItem(), QStringLiteral("documentTab1")), Qt::RightButton);
        QTRY_VERIFY(tabMenu->property("opened").toBool());
        click(entryOf(menu));
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(named("compareByChosenStyles")->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(tabMenu, "close"));
    }

    void loadingOtherSubtitlesIntoAComparedTabKeepsThePair()
    {
        QVERIFY(application->compareWithTab(1));
        QCOMPARE(states(), QStringLiteral("=x="));
        const auto firstId = application->workspace().tabs()[0];
        application->selectTab(1);
        const QString third = write(dir, "third.ass", "Default",
                                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,uno\n"
                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two\n");
        const auto review = application->reviewOpen(third);
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("rows")).toList().isEmpty());
        application->finishClose();
        QCOMPARE(application->workspace().tabs().size(), std::size_t(2));
        const auto loaded = application->workspace().tabs()[1];
        QCOMPARE(application->files().session(loaded)->document().lines().size(), std::size_t(2));
        const auto &c = application->comparison();
        QVERIFY(c.active());
        QCOMPARE(c.first(), firstId);
        QCOMPARE(c.second(), loaded);
        QVERIFY(!c.table(loaded));
        QCOMPARE(states(), QStringLiteral(".."));
        application->selectTab(0);
        QCOMPARE(states(), QStringLiteral("=x="));
        auto &editor = application->editor();
        editor.textEdited(QStringLiteral("uno"), 3);
        QVERIFY(editor.commit());
        QCOMPARE(states(), QStringLiteral("==."));
        application->selectTab(1);
        QCOMPARE(states(), QStringLiteral("=="));
    }

    void loadingASessionTurnsComparisonOff()
    {
        const QString later = dir.filePath(QStringLiteral("later.ass"));
        QFile::remove(later);
        QByteArray text = "[HikariSub v0.0.1]\n";
        int tab = 0;
        for (const QString &path : {first, later})
            text += "Tab: " + QByteArray::number(tab++) + "\nVideo: \nPosition: 0\nFFMS2: 1\nSubtitles: " +
                    QDir::fromNativeSeparators(path).toUtf8() + "\nActive: 0\nScroll: 0\nEditor: 1\n";
        const QString kls = dir.filePath(QStringLiteral("comparison.kls"));
        {
            QFile f(kls);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        }
        QVERIFY(application->compareWithTab(1));
        const auto review = application->reviewSession(QUrl::fromLocalFile(kls));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("rows")).toList().isEmpty());
        application->finishClose();
        QCOMPARE(application->workspace().tabs().size(), std::size_t(2));
        QCOMPARE(application->unresolvedRestores().size(), qsizetype(1));
        const auto &c = application->comparison();
        QVERIFY(!c.active());
        QVERIFY(!c.first());
        QVERIFY(!c.second());
        QVERIFY(c.tabled().empty());
        QObject *menu = openTabMenu(-1);
        QVERIFY(!menu->property("enabled").toBool());
        QVERIFY(!enabled("turnOffComparison"));
        closeTabMenu();
        QCOMPARE(application->currentTab(), 1);
        application->selectTab(0);
        QCOMPARE(states(), QStringLiteral("..."));
        auto &editor = application->editor();
        editor.textEdited(QStringLiteral("uno"), 3);
        QVERIFY(editor.commit());
        QCOMPARE(states(), QStringLiteral("..."));
        QVERIFY(application->compareWithTab(1));
        QCOMPARE(c.first(), application->workspace().tabs()[0]);
        QVERIFY(c.table(application->workspace().tabs()[1]));
        QVERIFY(QFile::copy(second, later));
        QVERIFY(application->retryRestore(0));
        const auto loaded = application->workspace().tabs()[1];
        QVERIFY(c.active());
        QCOMPARE(c.second(), loaded);
        QVERIFY(!c.table(loaded));
        editor.textEdited(QStringLiteral("too"), 3);
        QVERIFY(editor.commit());
        QCOMPARE(states(), QStringLiteral("xx="));
        application->selectTab(1);
        QCOMPARE(states(), QStringLiteral("xx=."));
    }

    void anEarlierPairKeepsNoTable()
    {
        application->openFile(write(dir, "third.ass", "Default", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"));
        application->selectTab(0);
        QVERIFY(application->compareWithTab(1));
        application->selectTab(1);
        QCOMPARE(states(), QStringLiteral("=x=."));
        application->selectTab(0);
        QVERIFY(application->compareWithTab(2));
        QVERIFY(!application->comparison().table(application->workspace().tabs()[1]));
        application->selectTab(1);
        QCOMPARE(states(), QStringLiteral("...."));
        application->selectTab(0);
        application->turnOffComparison();
        QVERIFY(application->comparison().tabled().empty());
        application->selectTab(1);
        application->selectLine(session(1).document().lines()[3]->id.value);
        QVERIFY(application->duplicateLines());
        QCOMPARE(session(1).document().lines().size(), std::size_t(5));
        QCOMPARE(states(), QStringLiteral("....."));
        QVERIFY(application->comparison().tabled().empty());
    }

    void translationModeComparesTheTranslation()
    {
        const QString tl = dir.filePath(QStringLiteral("translation.ass"));
        {
            QFile f(tl);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray("[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: TLmode\n\n") + kStyles +
                    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
                    "10,10,10,1\n"
                    "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,TLmode,,0,0,0,,one\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,uno\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,TLmode,,0,0,0,,two\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,dos\n");
        }
        QVERIFY(application->openFile(tl));
        QVERIFY(application->openFile(write(dir, "plain.ass", "Default",
                                            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,uno\n"
                                            "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two\n")));
        QVERIFY(application::translationMode(session(2)));
        QCOMPARE(session(2).document().lines().size(), std::size_t(2));
        application->selectTab(2);
        QVERIFY(application->compareWithTab(3));
        QCOMPARE(states(), QStringLiteral("=x"));
        application->selectTab(3);
        QCOMPARE(states(), QStringLiteral("=x"));
        QVERIFY(application->compareWithTab(2));
        QCOMPARE(states(), QStringLiteral("=x"));
        application->selectTab(2);
        QCOMPARE(states(), QStringLiteral("=x"));
    }

    void aStyleShownTwiceIsUncheckedAtOnce()
    {
        application->settingsStore()->set("comparison.styles", QStringList{"Sign"});
        openTabMenu(1);
        closeTabMenu();
        openTabMenu(1);
        QVERIFY(checked("compareStyle_Sign"));
        trigger("compareStyle_Sign");
        QCOMPARE(application->settingsStore()->list("comparison.styles"), QStringList{});
        QCOMPARE(type(), 0);
        openTabMenu(1);
        QVERIFY(!checked("compareStyle_Sign"));
        QVERIFY(!checked("compareByChosenStyles"));
    }

    void coloursAreFixedPerTheme()
    {
        for (const char *id : {"grid.comparisonOutline", "grid.comparisonMismatch", "grid.comparisonMatch",
                               "grid.comparisonCommentMismatch", "grid.comparisonCommentMatch"})
            QVERIFY2(!application::findSetting(id), id);
        const auto colours = [this](bool reference) {
            auto *model = reference ? application->shell().referenceLines() : application->shell().lines();
            return model->headerData(0, Qt::Horizontal, LineTableModel::ComparisonColoursRole).toList();
        };
        const auto list = [] {
            QVariantList out;
            for (const QColor &c : LineTableModel::themeComparisonColours())
                out << c;
            return out;
        };
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        ui::theme::preview({{QStringLiteral("appearance.theme"), QStringLiteral("dark")},
                            {QStringLiteral("appearance.followSystem"), false}});
        QCOMPARE(colours(false), list());
        QCOMPARE(colours(true), list());
        QCOMPARE(list(), (QVariantList{QColor(0x27, 0x00, 0xFF), QColor(0x27, 0x2B, 0x32), QColor(0x3A, 0x3E, 0x45),
                                       QColor(0x00, 0x31, 0x76), QColor(0x36, 0x62, 0xA1)}));
        ui::theme::preview({{QStringLiteral("appearance.theme"), QStringLiteral("light")},
                            {QStringLiteral("appearance.followSystem"), false}});
        QCOMPARE(list(), (QVariantList{QColor(0xFF, 0xFF, 0xFF), QColor(0xFF, 0x00, 0x0C), QColor(0xB7, 0xAC, 0x00),
                                       QColor(0x9C, 0x00, 0x00), QColor(0x81, 0x79, 0x00)}));
        QCOMPARE(colours(false), list());
        QCOMPARE(colours(true), list());
    }
};

QTEST_MAIN(ComparisonTest)
#include "comparison_tests.moc"
