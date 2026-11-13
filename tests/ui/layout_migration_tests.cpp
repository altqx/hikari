#include "hikari/app/application.h"

#include "docking.h"
#include "workspace_layout.h"

#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/kddockwidgets_version.h>
#include <kddockwidgets/qtquick/View.h>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cstdlib>
#include <memory>

using namespace hikari;
using KDDockWidgets::DockRegistry;

namespace {

constexpr double kSlack = 3;
const QByteArray kDirMark = "@DIR@";

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeAll(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}

QString writeSubtitles(const QString &path)
{
    return writeAll(path, "[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                          "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                          "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n")
               ? path
               : QString();
}

KDDockWidgets::Core::DockWidget *panel(const char *name)
{
    return DockRegistry::self()->dockByName(QLatin1String(name));
}

QJsonObject rectOf(const QQuickItem *item)
{
    const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    return {{QStringLiteral("x"), qRound(r.x())},
            {QStringLiteral("y"), qRound(r.y())},
            {QStringLiteral("width"), qRound(r.width())},
            {QStringLiteral("height"), qRound(r.height())}};
}

QJsonArray panelsOf(const KDDockWidgets::Core::Group *group)
{
    QJsonArray names;
    for (const auto *dock : group->dockWidgets())
        names.append(dock->uniqueName());
    return names;
}

QJsonArray sorted(QJsonArray list, const QString &key)
{
    QList<QJsonValue> values(list.begin(), list.end());
    const auto order = [&](const QJsonValue &v) { return QJsonDocument(v.toObject().value(key).toArray()).toJson(); };
    std::sort(values.begin(), values.end(), [&](const QJsonValue &a, const QJsonValue &b) { return order(a) < order(b); });
    QJsonArray out;
    for (const auto &value : values)
        out.append(value);
    return out;
}

QJsonObject describeDocking(const QWindow *main, const QString &preset)
{
    QJsonArray groups;
    for (const auto *group : DockRegistry::self()->groups()) {
        if (group->dockWidgets().isEmpty())
            continue;
        groups.append(QJsonObject{{QStringLiteral("panels"), panelsOf(group)},
                                  {QStringLiteral("current"), group->currentDockWidget()->uniqueName()},
                                  {QStringLiteral("floating"), group->isInFloatingWindow()},
                                  {QStringLiteral("rect"), rectOf(KDDockWidgets::QtQuick::asQQuickItem(group->view()))}});
    }
    QJsonArray floating;
    for (const auto *window : DockRegistry::self()->floatingWindows()) {
        QJsonArray members;
        for (const auto *group : window->groups())
            members.append(panelsOf(group).first());
        const QWindow *native = KDDockWidgets::QtQuick::asQQuickItem(window->view())->window();
        floating.append(QJsonObject{{QStringLiteral("groups"), members},
                                    {QStringLiteral("width"), native->width()},
                                    {QStringLiteral("height"), native->height()}});
    }
    QJsonArray closed;
    for (const QString &id : ui::WorkspaceLayoutController::panelIds())
        if (const auto *dock = DockRegistry::self()->dockByName(id); dock && !dock->isOpen())
            closed.append(id);
    return {{QStringLiteral("preset"), preset},
            {QStringLiteral("window"), QJsonObject{{QStringLiteral("width"), main->width()},
                                                   {QStringLiteral("height"), main->height()}}},
            {QStringLiteral("groups"), sorted(groups, QStringLiteral("panels"))},
            {QStringLiteral("floatingWindows"), sorted(floating, QStringLiteral("groups"))},
            {QStringLiteral("closed"), closed}};
}

QString difference(const QJsonValue &expected, const QJsonValue &actual, const QString &at = QStringLiteral("$"))
{
    const auto mismatch = [&] {
        return QStringLiteral("%1: expected %2, got %3")
            .arg(at, QString::fromUtf8(QJsonDocument(QJsonArray{expected}).toJson(QJsonDocument::Compact)),
                 QString::fromUtf8(QJsonDocument(QJsonArray{actual}).toJson(QJsonDocument::Compact)));
    };
    if (expected.isDouble() && actual.isDouble())
        return std::abs(expected.toDouble() - actual.toDouble()) <= kSlack ? QString() : mismatch();
    if (expected.isArray() && actual.isArray()) {
        const QJsonArray e = expected.toArray(), a = actual.toArray();
        if (e.size() != a.size())
            return mismatch();
        for (qsizetype i = 0; i < e.size(); ++i)
            if (const QString d = difference(e.at(i), a.at(i), QStringLiteral("%1[%2]").arg(at).arg(i)); !d.isEmpty())
                return d;
        return {};
    }
    if (expected.isObject() && actual.isObject()) {
        const QJsonObject e = expected.toObject(), a = actual.toObject();
        if (e.keys() != a.keys())
            return mismatch();
        for (auto it = e.begin(); it != e.end(); ++it)
            if (const QString d = difference(it.value(), a.value(it.key()), at + QLatin1Char('.') + it.key()); !d.isEmpty())
                return d;
        return {};
    }
    return expected == actual ? QString() : mismatch();
}

QHash<QString, int> liveMinimumHeights()
{
    QHash<QString, int> out;
    for (const auto *group : DockRegistry::self()->groups())
        if (!group->dockWidgets().isEmpty() && !group->isInFloatingWindow())
            out.insert(QString::fromUtf8(QJsonDocument(panelsOf(group)).toJson(QJsonDocument::Compact)),
                       group->view()->minSize().height());
    return out;
}

int edgeOf(const QJsonObject &group, bool top)
{
    const QJsonObject r = group.value(QStringLiteral("rect")).toObject();
    return top ? r.value(QStringLiteral("y")).toInt() : r.value(QStringLiteral("y")).toInt() + r.value(QStringLiteral("height")).toInt();
}

QJsonObject moved(QJsonObject group, int top, int height)
{
    QJsonObject r = group.value(QStringLiteral("rect")).toObject();
    r.insert(QStringLiteral("y"), r.value(QStringLiteral("y")).toInt() + top);
    r.insert(QStringLiteral("height"), r.value(QStringLiteral("height")).toInt() + height);
    group.insert(QStringLiteral("rect"), r);
    return group;
}

QString expectHeightsAllowingTheLiveMinimum(QJsonObject &expected, const QJsonObject &actual,
                                            const QHash<QString, int> &liveMinimums, QString &summary)
{
    QJsonArray groups = expected.value(QStringLiteral("groups")).toArray();
    const QJsonArray live = actual.value(QStringLiteral("groups")).toArray();
    const auto key = [](const QJsonObject &g) {
        return QString::fromUtf8(QJsonDocument(g.value(QStringLiteral("panels")).toArray()).toJson(QJsonDocument::Compact));
    };
    const auto liveOf = [&](const QJsonObject &g) {
        for (const auto &l : live)
            if (key(l.toObject()) == key(g))
                return l.toObject();
        return QJsonObject();
    };
    for (qsizetype i = 0; i < groups.size(); ++i) {
        const QJsonObject group = groups.at(i).toObject();
        const int recorded = group.value(QStringLiteral("rect")).toObject().value(QStringLiteral("height")).toInt();
        const int liveMinimum = liveMinimums.value(key(group), 0);
        const int allowedHeight = std::max(recorded, liveMinimum);
        const int delta = allowedHeight - recorded;
        if (group.value(QStringLiteral("floating")).toBool() || delta <= kSlack)
            continue;
        const QJsonObject restored = liveOf(group);
        const bool top = edgeOf(restored, true) < edgeOf(group, true) - kSlack;
        const int edge = edgeOf(group, top);
        QString why = QStringLiteral("%1 recorded %2 below its live minimum %3, so it restores %4 taller; across its %5 edge:")
                          .arg(group.value(QStringLiteral("current")).toString()).arg(recorded).arg(liveMinimum).arg(delta)
                          .arg(top ? QStringLiteral("top") : QStringLiteral("bottom"));
        int given = 0;
        for (qsizetype j = 0; j < groups.size(); ++j) {
            const QJsonObject other = groups.at(j).toObject();
            if (other.value(QStringLiteral("floating")).toBool())
                continue;
            if (std::abs(edgeOf(other, top) - edge) <= kSlack) {
                groups.replace(j, top ? moved(other, -delta, delta) : moved(other, 0, delta));
            } else if (std::abs(edgeOf(other, !top) - edge) <= kSlack + 1) {
                const int was = other.value(QStringLiteral("rect")).toObject().value(QStringLiteral("height")).toInt();
                const int now = liveOf(other).value(QStringLiteral("rect")).toObject().value(QStringLiteral("height")).toInt();
                why += QStringLiteral(" %1 %2").arg(other.value(QStringLiteral("current")).toString()).arg(now - was);
                if (std::abs(now - was + delta) > kSlack)
                    return why + QStringLiteral(" (each must give up %1)").arg(delta);
                given = std::max(given, was - now);
                groups.replace(j, top ? moved(other, 0, -delta) : moved(other, delta, -delta));
            }
        }
        if (given == 0)
            return why + QStringLiteral(" nothing gave up the room");
        summary += QLatin1Char(' ') + why;
    }
    expected.insert(QStringLiteral("groups"), groups);
    return {};
}

QString expectationsFile()
{
    return QStringLiteral("expected-%1.json").arg(QGuiApplication::platformName());
}

QStringList engines()
{
    const QDir fixtures(QStringLiteral(HIKARI_LAYOUT_FIXTURES));
    QStringList recorded = fixtures.entryList({QStringLiteral("kddw-*")}, QDir::Dirs);
    recorded.removeIf([&](const QString &engineDir) { return !fixtures.exists(engineDir + QLatin1Char('/') + expectationsFile()); });
    return recorded;
}

}

class LayoutMigrationTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QString path(const QString &name) const { return dir.filePath(name); }
    void start()
    {
        app::Application::Options options;
        options.settingsFile = path(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());
        if (application->currentTab() < 0)
            application->addPage();
    }
    void stop()
    {
        application->workspaceLayout().save();
        application->endSession();
        engine.reset();
        application.reset();
    }
    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    ui::WorkspaceLayoutController &layouts() { return application->workspaceLayout(); }
    QJsonObject settled()
    {
        QJsonObject last = describeDocking(window(), layouts().preset());
        for (int still = 0, waited = 0; still < 5 && waited < 100; ++waited) {
            QTest::qWait(100);
            const QJsonObject now = describeDocking(window(), layouts().preset());
            still = now == last ? still + 1 : 0;
            last = now;
        }
        return last;
    }
    void applyPreset(const QString &name)
    {
        QVERIFY(QMetaObject::invokeMethod(window(), "applyPreset", Q_ARG(QVariant, name)));
        QTRY_COMPARE(layouts().preset(), name);
    }
    void arrangeTabs()
    {
        applyPreset(QStringLiteral("Editing"));
        panel("Grid")->addDockWidgetAsTab(panel("Search"));
        panel("Grid")->addDockWidgetAsTab(panel("Styles"));
        panel("Grid")->setAsCurrentTab();
        panel("Audio")->close();
    }
    void arrangeFloating()
    {
        applyPreset(QStringLiteral("Editing"));
        panel("Video")->close();
        QVERIFY(panel("Audio")->setFloating(true));
        panel("Styles")->open();
        QVERIFY(panel("Styles")->setFloating(true));
        panel("Styles")->addDockWidgetAsTab(panel("Template"));
        panel("Styles")->addDockWidgetToContainingWindow(panel("Search"), KDDockWidgets::Location_OnRight);
        QTRY_COMPARE(DockRegistry::self()->floatingWindows().size(), 2);
    }
    void expectRestored(const QJsonObject &expected, const QString &what)
    {
        QVERIFY2(layouts().notice().isEmpty(), qPrintable(what + QStringLiteral(": ") + layouts().notice()));
        const QJsonObject actual = settled();
        QJsonObject allowingTheLiveMinimum = expected;
        QString summary;
        const QString grown = expectHeightsAllowingTheLiveMinimum(allowingTheLiveMinimum, actual, liveMinimumHeights(), summary);
        QVERIFY2(grown.isEmpty(), qPrintable(what + QStringLiteral(": ") + grown));
        const QString d = difference(allowingTheLiveMinimum, actual);
        QVERIFY2(d.isEmpty(), qPrintable(what + QStringLiteral(": ") + d + summary));
        QVERIFY(DockRegistry::self()->isSane());
    }
    QJsonObject expectations(const QString &engineDir) const
    {
        return QJsonDocument::fromJson(readAll(fixture(engineDir, expectationsFile()))).object();
    }
    QString fixture(const QString &engineDir, const QString &name) const
    {
        return QStringLiteral(HIKARI_LAYOUT_FIXTURES "/%1/%2").arg(engineDir, name);
    }

private slots:
    void cleanup()
    {
        engine.reset();
        application.reset();
        for (const QString &name : QDir(dir.path()).entryList(QDir::Files))
            QFile::remove(path(name));
    }

    void recordThisEnginesLayouts()
    {
        const QString out = qEnvironmentVariable("HIKARI_RECORD_LAYOUTS");
        if (out.isEmpty())
            QSKIP("HIKARI_RECORD_LAYOUTS names no directory to record into");
        QVERIFY(QDir().mkpath(out));
        QJsonObject files, saved;
        start();
        const auto record = [&](const QString &name) {
            files.insert(name, settled());
            layouts().save();
            return writeAll(out + QStringLiteral("/layout-%1.json").arg(name), readAll(path(QStringLiteral("layout.json"))));
        };
        for (const QString &preset : ui::WorkspaceLayoutController::presets()) {
            applyPreset(preset);
            QVERIFY(record(preset.toLower()));
        }
        arrangeTabs();
        QVERIFY(record(QStringLiteral("tabs")));
        QVERIFY(layouts().saveLayoutAs(QStringLiteral("Tabs")));
        saved.insert(QStringLiteral("Tabs"), files.value(QStringLiteral("tabs")));
        arrangeFloating();
        QVERIFY(record(QStringLiteral("floating")));
        QVERIFY(layouts().saveLayoutAs(QStringLiteral("Floating")));
        saved.insert(QStringLiteral("Floating"), files.value(QStringLiteral("floating")));
        QVERIFY(writeAll(out + QStringLiteral("/layouts.json"), readAll(path(QStringLiteral("layouts.json")))));
        stop();

        start();
        QJsonArray tabs;
        QVERIFY(application->openFile(writeSubtitles(path(QStringLiteral("first.ass")))));
        applyPreset(QStringLiteral("Timing"));
        tabs.append(settled());
        application->addPage();
        QVERIFY(application->openFile(writeSubtitles(path(QStringLiteral("second.ass")))));
        arrangeFloating();
        tabs.append(settled());
        QVERIFY(application->saveLastSession());
        QByteArray session = readAll(application->lastSessionPath());
        session.replace(dir.path().toUtf8(), kDirMark);
        QVERIFY(writeAll(out + QStringLiteral("/session.json"), session));
        stop();

        start();
        application->settingsStore()->set("subtitles.layoutInFile", true);
        QVERIFY(application->openFile(writeSubtitles(path(QStringLiteral("section.ass")))));
        arrangeTabs();
        QVERIFY(panel("Video")->setFloating(true));
        const QJsonObject section = settled();
        QVERIFY(application->saveDocument(0));
        QTRY_VERIFY(readAll(path(QStringLiteral("section.ass"))).contains("[Hikari]"));
        QVERIFY(writeAll(out + QStringLiteral("/section.ass"), readAll(path(QStringLiteral("section.ass")))));
        stop();

        const QJsonObject expected{{QStringLiteral("engine"), QStringLiteral(KDDOCKWIDGETS_VERSION_STRING)},
                                   {QStringLiteral("layouts"), files},
                                   {QStringLiteral("saved"), saved},
                                   {QStringLiteral("session"), tabs},
                                   {QStringLiteral("section"), section}};
        QVERIFY(writeAll(out + QLatin1Char('/') + expectationsFile(), QJsonDocument(expected).toJson()));
    }

    void anOlderEnginesLayoutFileRestores_data()
    {
        QTest::addColumn<QString>("engineDir");
        QTest::addColumn<QString>("name");
        for (const QString &engineDir : engines())
            for (const QString &name : expectations(engineDir).value(QStringLiteral("layouts")).toObject().keys())
                QTest::addRow("%s %s", qPrintable(engineDir), qPrintable(name)) << engineDir << name;
    }
    void anOlderEnginesLayoutFileRestores()
    {
        QFETCH(QString, engineDir);
        QFETCH(QString, name);
        QVERIFY(QFile::copy(fixture(engineDir, QStringLiteral("layout-%1.json").arg(name)), path(QStringLiteral("layout.json"))));
        start();
        expectRestored(expectations(engineDir).value(QStringLiteral("layouts")).toObject().value(name).toObject(), name);
        stop();
    }

    void aLayoutWithTheRemovedFixMinorErrorsPanelRestoresWithoutIt_data()
    {
        QTest::addColumn<QByteArray>("file");
        QTest::addColumn<int>("floatingWindows");
        QTest::addColumn<QString>("closed");
        QTest::addColumn<QString>("current");
        const QByteArray floating = readAll(fixture(QStringLiteral("kddw-2.4.1"), QStringLiteral("layout-floating.json")));
        const auto swapped = [&](const QByteArray &with) {
            QByteArray out = floating;
            return out.replace("\"FixMinorErrors\"", "\"@SWAP@\"").replace("\"" + with + "\"", "\"FixMinorErrors\"")
                .replace("\"@SWAP@\"", "\"" + with + "\"");
        };
        QJsonObject tabbed = QJsonDocument::fromJson(floating).object();
        QJsonObject payload = tabbed.value(QStringLiteral("payload")).toObject();
        QJsonArray windows = payload.value(QStringLiteral("floatingWindows")).toArray();
        QJsonObject window = windows.at(1).toObject();
        QJsonObject split = window.value(QStringLiteral("multiSplitterLayout")).toObject();
        QJsonObject frames = split.value(QStringLiteral("frames")).toObject();
        QJsonObject styles = frames.value(QStringLiteral("7790")).toObject();
        QJsonArray docks = styles.value(QStringLiteral("dockWidgets")).toArray();
        QJsonValue removed = docks.at(0);
        removed = removed.isObject() ? QJsonValue(QJsonObject{{QStringLiteral("uniqueName"), QStringLiteral("FixMinorErrors")}})
                                     : QJsonValue(QStringLiteral("FixMinorErrors"));
        docks.append(removed);
        styles.insert(QStringLiteral("dockWidgets"), docks);
        styles.insert(QStringLiteral("currentTabIndex"), 2);
        frames.insert(QStringLiteral("7790"), styles);
        split.insert(QStringLiteral("frames"), frames);
        window.insert(QStringLiteral("multiSplitterLayout"), split);
        windows.replace(1, window);
        payload.insert(QStringLiteral("floatingWindows"), windows);
        tabbed.insert(QStringLiteral("payload"), payload);
        QTest::newRow("a tab beside Styles") << QJsonDocument(tabbed).toJson() << 2 << QString() << QStringLiteral("Styles");
        QTest::newRow("alone in a floating window") << swapped("Audio") << 1 << QStringLiteral("Audio") << QStringLiteral("Template");
        QTest::newRow("alone in a docked group") << swapped("Grid") << 2 << QStringLiteral("Grid") << QStringLiteral("Template");
    }
    void aLayoutWithTheRemovedFixMinorErrorsPanelRestoresWithoutIt()
    {
        QFETCH(QByteArray, file);
        QFETCH(int, floatingWindows);
        QFETCH(QString, closed);
        QFETCH(QString, current);
        QString problem;
        const auto payload = ui::WorkspaceLayoutController::payloadOf(file, &problem);
        QVERIFY2(payload, qPrintable(problem));
        QVERIFY(!payload->contains("FixMinorErrors"));
        QVERIFY(writeAll(path(QStringLiteral("layout.json")), file));
        start();
        QVERIFY2(layouts().notice().isEmpty(), qPrintable(layouts().notice()));
        settled();
        QVERIFY(DockRegistry::self()->isSane());
        QVERIFY(!DockRegistry::self()->dockByName(QStringLiteral("FixMinorErrors")));
        for (const auto *group : DockRegistry::self()->groups())
            QVERIFY(!group->dockWidgets().isEmpty());
        QCOMPARE(DockRegistry::self()->floatingWindows().size(), floatingWindows);
        for (const char *name : {"Editor", "Styles", "Template", "Search"})
            QVERIFY2(panel(name)->isOpen(), name);
        QVERIFY(panel(qPrintable(current))->isCurrentTab());
        if (!closed.isEmpty())
            QVERIFY(!panel(qPrintable(closed))->isOpen());
        stop();
        QVERIFY(!readAll(path(QStringLiteral("layout.json"))).contains("FixMinorErrors"));
    }

    void anOlderEnginesSavedLayoutsApply_data()
    {
        QTest::addColumn<QString>("engineDir");
        for (const QString &engineDir : engines())
            QTest::addRow("%s", qPrintable(engineDir)) << engineDir;
    }
    void anOlderEnginesSavedLayoutsApply()
    {
        QFETCH(QString, engineDir);
        QVERIFY(QFile::copy(fixture(engineDir, QStringLiteral("layouts.json")), path(QStringLiteral("layouts.json"))));
        start();
        const QJsonObject saved = expectations(engineDir).value(QStringLiteral("saved")).toObject();
        QCOMPARE(layouts().savedLayouts().size(), saved.size());
        for (const QVariant &entry : layouts().savedLayouts())
            QVERIFY2(entry.toMap().value(QStringLiteral("problem")).toString().isEmpty(),
                     qPrintable(entry.toMap().value(QStringLiteral("problem")).toString()));
        for (auto it = saved.begin(); it != saved.end(); ++it) {
            QVERIFY(layouts().applySavedLayout(it.key()));
            QCOMPARE(layouts().layout(), it.key());
            expectRestored(it.value().toObject(), it.key());
            if (QTest::currentTestFailed())
                return;
        }
        stop();
    }

    void anOlderEnginesSessionRestoresEachTab_data() { anOlderEnginesSavedLayoutsApply_data(); }
    void anOlderEnginesSessionRestoresEachTab()
    {
        QFETCH(QString, engineDir);
        QByteArray session = readAll(fixture(engineDir, QStringLiteral("session.json")));
        session.replace(kDirMark, dir.path().toUtf8());
        QVERIFY(writeAll(path(QStringLiteral("old-session.json")), session));
        writeSubtitles(path(QStringLiteral("first.ass")));
        writeSubtitles(path(QStringLiteral("second.ass")));
        start();
        const QVariantMap review = application->reviewSession(QUrl::fromLocalFile(path(QStringLiteral("old-session.json"))));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        application->finishClose();
        const QJsonArray tabs = expectations(engineDir).value(QStringLiteral("session")).toArray();
        QTRY_COMPARE(application->tabs().size(), tabs.size());
        for (qsizetype i = tabs.size() - 1; i >= 0; --i) {
            application->selectTab(int(i));
            expectRestored(tabs.at(i).toObject(), QStringLiteral("tab %1").arg(i));
            if (QTest::currentTestFailed())
                return;
        }
        stop();
    }

    void anOlderEnginesFileSectionRestores_data() { anOlderEnginesSavedLayoutsApply_data(); }
    void anOlderEnginesFileSectionRestores()
    {
        QFETCH(QString, engineDir);
        QVERIFY(QFile::copy(fixture(engineDir, QStringLiteral("section.ass")), path(QStringLiteral("section.ass"))));
        start();
        QVERIFY(application->openFile(path(QStringLiteral("section.ass"))));
        expectRestored(expectations(engineDir).value(QStringLiteral("section")).toObject(), QStringLiteral("[Hikari]"));
        stop();
    }
};

QTEST_MAIN(LayoutMigrationTest)
#include "layout_migration_tests.moc"
