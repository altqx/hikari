#include "hikari/app/application.h"
#include "hikari/app/session_json.h"
#include "hikari/application/session_file.h"

#include "docking.h"
#include "legacy_session.h"
#include "log_controller.h"
#include "qtest_parts.h"
#include "settings_store.h"
#include "workspace_layout.h"

#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/core/DockRegistry.h>

#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace hikari;

namespace {

const QByteArray kAss = "[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                        "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,two\n";

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString write(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    f.write(bytes);
    return path;
}

QByteArray layoutLine(const QByteArray &file)
{
    const qsizetype section = file.indexOf("[Hikari]");
    if (section < 0)
        return {};
    const qsizetype at = file.indexOf("\nLayout=", section);
    if (at < 0)
        return {};
    const qsizetype end = file.indexOf('\n', at + 1);
    return file.mid(at + 8, (end < 0 ? file.size() : end) - at - 8).trimmed();
}

QJsonObject payloadOf(const QByteArray &layout)
{
    const QJsonObject envelope = QJsonDocument::fromJson(QByteArray::fromBase64(layout)).object();
    return envelope.value(QStringLiteral("payload")).toObject();
}

application::Session readSession(const QString &path)
{
    const QByteArray bytes = readAll(path);
    return app::readSession(std::string_view(bytes.constData(), bytes.size())).value_or(application::Session{});
}

int floatingWindows()
{
    return static_cast<int>(KDDockWidgets::DockRegistry::self()->floatingWindows().size());
}

QPoint topLeft(const QJsonObject &window)
{
    const QJsonObject geometry = window.value(QStringLiteral("geometry")).toObject();
    return {geometry.value(QStringLiteral("x")).toInt(), geometry.value(QStringLiteral("y")).toInt()};
}

QPoint floatingOffset(const QJsonObject &payload)
{
    return topLeft(payload.value(QStringLiteral("floatingWindows")).toArray().first().toObject()) -
           topLeft(payload.value(QStringLiteral("mainWindows")).toArray().first().toObject());
}

QPoint shownFloatingOffset()
{
    return floatingOffset(QJsonDocument::fromJson(KDDockWidgets::LayoutSaver().serializeLayout()).object());
}

bool settled()
{
    const QJsonObject payload = QJsonDocument::fromJson(KDDockWidgets::LayoutSaver().serializeLayout()).object();
    const QJsonObject main = payload.value(QStringLiteral("mainWindows")).toArray().first().toObject();
    const QJsonObject floating = payload.value(QStringLiteral("floatingWindows")).toArray().first().toObject();
    return floating.value(QStringLiteral("geometry")).toObject().value(QStringLiteral("width")).toInt() > 0 &&
           topLeft(main) == topLeft(QJsonObject{{QStringLiteral("geometry"), main.value(QStringLiteral("normalGeometry"))}});
}

}

class FileLayoutsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QByteArray madeLayout;
    QSet<QByteArray> opened;

    QString path(const char *name) const { return dir.filePath(QLatin1String(name)); }
    void start(const QString &recovery = {})
    {
        app::Application::Options options;
        options.settingsFile = path("settings.toml");
        options.playbackAudio = false;
        options.recoveryDir = recovery;
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());
        application->addPage();
        QCOMPARE(application->currentTab(), 0);
    }
    void stop()
    {
        application->workspaceLayout().save();
        application->endSession();
        engine.reset();
        application.reset();
        for (const char *name : {"layout.json", "layout.json.bak"})
            QFile::remove(path(name));
    }
    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    QObject *dock(const char *name) const { return window()->findChild<QObject *>(QLatin1String(name)); }
    bool isOpen(const char *name) const { return dock(name)->property("isOpen").toBool(); }
    bool isFloating(const char *name) const { return dock(name)->property("isFloating").toBool(); }
    void close(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(dock(name), "close"));
        QTRY_VERIFY(!isOpen(name));
    }
    void float_(const char *name, bool on)
    {
        QVERIFY(dock(name)->setProperty("isFloating", on));
        QTRY_COMPARE(isFloating(name), on);
    }
    void applyPreset(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(window(), "applyPreset", Q_ARG(QVariant, QLatin1String(name))));
    }
    QString preset() const { return application->workspaceLayout().preset(); }
    bool targetDirty() const
    {
        const auto target = application->workspace().editingTarget();
        auto *session = target ? application->files().session(*target) : nullptr;
        return session && session->isDirty();
    }
    // MUST KILL: save ignores its file argument and saves the editing target
    void save(const QString &file)
    {
        Q_UNUSED(file);
        const auto target = *application->workspace().editingTarget();
        QVERIFY(application->saveDocument(0));
        QTRY_VERIFY(application->files().lastSave(target) && application->files().lastSave(target)->outcome);
        QCOMPARE(*application->files().lastSave(target)->outcome, application::WriteOutcome::Written);
        QVERIFY(!targetDirty());
    }
    void setLayoutInFile(bool on) { application->settingsStore()->set("subtitles.layoutInFile", on); }
    void loadSession(const QString &file)
    {
        const QVariantMap review = application->reviewSession(QUrl::fromLocalFile(file));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("rows")).toList().isEmpty());
        application->finishClose();
    }
    QByteArray fileLayout()
    {
        start();
        close("videoDock");
        float_("audioDock", true);
        [&] { QTRY_VERIFY(settled()); }();
        const auto target = application->workspace().editingTarget();
        const QByteArray layout = application->workspaceLayout().fileArrangement(target->value);
        stop();
        return layout;
    }
    QByteArray refusalBase()
    {
        if (madeLayout.isEmpty())
            madeLayout = fileLayout();
        return madeLayout;
    }
    // MUST KILL: opensAsACopyOfTheTab opens the file in the app only on each test function's first row
    void opensAsACopyOfTheTab(const char *name, const QByteArray &layout)
    {
        if (opened.contains(QTest::currentTestFunction()))
            return;
        opened.insert(QTest::currentTestFunction());
        const QString file = write(path(name), kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        start();
        application->addPage();
        QVERIFY(application->openFile(file));
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));
        QVERIFY(application->log().history().contains(QLatin1String(name)));
        stop();
    }

private slots:
    void cleanup()
    {
        engine.reset();
        application.reset();
        for (const char *name : {"layout.json", "layout.json.bak", "layout.json.unrestored", "layout.json.tab.unrestored",
                                 "layouts.json", "last-session.json", "settings.toml", "hotkeys.toml", "state.json"})
            QFile::remove(path(name));
    }

    void theFileFormHasNoScreenPositions()
    {
        const QByteArray payload = R"({"serializationVersion":3,
            "mainWindows":[{"uniqueName":"Classic","geometry":{"x":300,"y":200,"width":1200,"height":800},
                            "normalGeometry":{"x":300,"y":200,"width":1200,"height":800},"screenIndex":1,
                            "screenSize":{"width":2560,"height":1440},"multiSplitterLayout":{"layout":{"x":1}}}],
            "floatingWindows":[{"geometry":{"x":500,"y":260,"width":400,"height":300},"screenIndex":1,
                                "screenSize":{"width":2560,"height":1440}}],
            "allDockWidgets":[{"uniqueName":"Audio","lastPosition":{"lastFloatingGeometry":{"x":320,"y":210,"width":10,"height":10}}},
                              {"uniqueName":"Video","lastPosition":{"lastFloatingGeometry":{"x":0,"y":0,"width":0,"height":0}}}],
            "screenInfo":[{"index":1,"name":"DP-1","geometry":{"x":1920,"y":0,"width":2560,"height":1440}}]})";
        const ui::WorkspaceArrangement arrangement{QStringLiteral("Timing"),
                                                   QJsonDocument::fromJson(payload).toJson(QJsonDocument::Compact)};
        const QByteArray text = arrangement.serialiseForFile();
        QVERIFY(!text.contains(':') && !text.contains('\n'));
        const QJsonObject envelope = QJsonDocument::fromJson(QByteArray::fromBase64(text)).object();
        QCOMPARE(envelope.value(QStringLiteral("preset")).toString(), QStringLiteral("Timing"));
        const QJsonObject written = envelope.value(QStringLiteral("payload")).toObject();
        QVERIFY(!written.contains(QStringLiteral("screenInfo")));
        const QJsonObject main = written.value(QStringLiteral("mainWindows")).toArray().first().toObject();
        QVERIFY(!main.contains(QStringLiteral("screenIndex")) && !main.contains(QStringLiteral("screenSize")));
        QCOMPARE(main.value(QStringLiteral("geometry")).toObject(),
                 QJsonObject({{"x", 0}, {"y", 0}, {"width", 1200}, {"height", 800}}));
        QCOMPARE(main.value(QStringLiteral("normalGeometry")).toObject().value(QStringLiteral("x")).toInt(), 0);
        const QJsonObject floating = written.value(QStringLiteral("floatingWindows")).toArray().first().toObject();
        QVERIFY(!floating.contains(QStringLiteral("screenIndex")) && !floating.contains(QStringLiteral("screenSize")));
        QCOMPARE(topLeft(floating), QPoint(200, 60));
        const QJsonArray docks = written.value(QStringLiteral("allDockWidgets")).toArray();
        auto lastFloating = [](const QJsonValue &dock) {
            return dock.toObject().value(QStringLiteral("lastPosition")).toObject().value(QStringLiteral("lastFloatingGeometry")).toObject();
        };
        QCOMPARE(lastFloating(docks.at(0)).value(QStringLiteral("x")).toInt(), 20);
        QCOMPARE(lastFloating(docks.at(0)).value(QStringLiteral("y")).toInt(), 10);
        QCOMPARE(lastFloating(docks.at(1)).value(QStringLiteral("x")).toInt(), 0);

        QString problem;
        const auto read = ui::WorkspaceArrangement::parseFromFile(text, QPoint(10, 20), &problem);
        QVERIFY2(read, qPrintable(problem));
        QCOMPARE(read->preset, QStringLiteral("Timing"));
        const QJsonObject placed = QJsonDocument::fromJson(read->payload).object();
        QCOMPARE(topLeft(placed.value(QStringLiteral("mainWindows")).toArray().first().toObject()), QPoint(10, 20));
        QCOMPARE(topLeft(placed.value(QStringLiteral("floatingWindows")).toArray().first().toObject()), QPoint(210, 80));
        QCOMPARE(lastFloating(placed.value(QStringLiteral("allDockWidgets")).toArray().at(0)).value(QStringLiteral("x")).toInt(), 30);
        QJsonObject maximized = QJsonDocument::fromJson(arrangement.payload).object();
        QJsonArray mains = maximized.value(QStringLiteral("mainWindows")).toArray();
        QJsonObject full = mains.first().toObject();
        full.insert(QStringLiteral("geometry"), QJsonObject({{"x", 0}, {"y", 0}, {"width", 2560}, {"height", 1440}}));
        mains.replace(0, full);
        maximized.insert(QStringLiteral("mainWindows"), mains);
        const QByteArray maximizedText =
            ui::WorkspaceArrangement{QStringLiteral("Timing"), QJsonDocument(maximized).toJson(QJsonDocument::Compact)}
                .serialiseForFile();
        const QJsonObject maximizedMain = QJsonDocument::fromJson(QByteArray::fromBase64(maximizedText))
                                              .object().value(QStringLiteral("payload")).toObject()
                                              .value(QStringLiteral("mainWindows")).toArray().first().toObject();
        QCOMPARE(maximizedMain.value(QStringLiteral("normalGeometry")).toObject(),
                 QJsonObject({{"x", 0}, {"y", 0}, {"width", 1200}, {"height", 800}}));
        const auto maximizedRead = ui::WorkspaceArrangement::parseFromFile(maximizedText, QPoint(10, 20));
        QVERIFY(maximizedRead);
        QCOMPARE(QJsonDocument::fromJson(maximizedRead->payload).object().value(QStringLiteral("mainWindows")).toArray()
                     .first().toObject().value(QStringLiteral("normalGeometry")).toObject(),
                 QJsonObject({{"x", 10}, {"y", 20}, {"width", 1200}, {"height", 800}}));
        const auto alone = ui::WorkspaceArrangement::parseFromFile(ui::WorkspaceArrangement{QStringLiteral("Translation"), {}}.serialiseForFile(), {});
        QVERIFY(alone && alone->payload.isEmpty() && alone->preset == QStringLiteral("Translation"));
        QVERIFY(!ui::WorkspaceArrangement::parseFromFile("eyJ:", {}, &problem));
        QVERIFY(!ui::WorkspaceArrangement::parseFromFile("", {}, &problem));
    }

    void aSavedFileKeepsItsTabsArrangement()
    {
        const QString file = write(path("a.ass"), kAss);
        start();
        window()->setPosition(60, 40);
        QTRY_COMPARE(window()->position(), QPoint(60, 40));
        QVERIFY(application->openFile(file));
        close("videoDock");
        float_("audioDock", true);
        QTRY_VERIFY(settled());
        QVERIFY(!targetDirty());
        save(file);
        const QByteArray saved = readAll(file);
        QVERIFY2(saved.contains(kAss.mid(kAss.indexOf("[Events]")) + "\n[Hikari]\nLayout="), saved.constData());
        QVERIFY(saved.endsWith("\n"));
        QCOMPARE(saved.count("[Hikari]"), 1);
        const QByteArray layout = layoutLine(saved);
        QVERIFY(!layout.contains(':'));
        if (const QString out = qEnvironmentVariable("HIKARI_WS2_LAYOUT_OUT"); !out.isEmpty())
            write(out, layout + '\n');
        const QJsonObject payload = payloadOf(layout);
        QVERIFY(!payload.isEmpty());
        QVERIFY(!payload.contains(QStringLiteral("screenInfo")));
        for (const char *windows : {"mainWindows", "floatingWindows"})
            for (const auto &w : payload.value(QLatin1String(windows)).toArray()) {
                QVERIFY(!w.toObject().contains(QStringLiteral("screenIndex")));
                QVERIFY(!w.toObject().contains(QStringLiteral("screenSize")));
            }
        const QJsonObject main = payload.value(QStringLiteral("mainWindows")).toArray().first().toObject();
        QCOMPARE(topLeft(main), QPoint(0, 0));
        QVERIFY(main.value(QStringLiteral("geometry")).toObject().value(QStringLiteral("width")).toInt() > 0);
        QCOMPARE(payload.value(QStringLiteral("floatingWindows")).toArray().size(), 1);
        const QPoint offset = shownFloatingOffset();
        QCOMPARE(floatingOffset(payload), offset);
        save(file);
        QCOMPARE(readAll(file), saved);
        stop();

        start();
        QVERIFY(isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));
        window()->setPosition(140, 90);
        QTRY_COMPARE(window()->position(), QPoint(140, 90));
        QVERIFY(application->openFile(file));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        QCOMPARE(floatingWindows(), 1);
        QTRY_COMPARE(shownFloatingOffset(), offset);
        QVERIFY(!targetDirty());
        stop();
    }

    void aFileOpenedInANewTabTakesItsArrangement()
    {
        const QByteArray layout = fileLayout();
        const QString file = write(path("b.ass"), kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        start();
        QVERIFY(application->openFile(write(path("first.ass"), kAss)));
        QVERIFY(application->openFile(file));
        QCOMPARE(application->tabs().size(), qsizetype(2));
        QCOMPARE(application->currentTab(), 0);
        QVERIFY(isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));
        application->selectTab(1);
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        application->selectTab(0);
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));
        stop();
    }

    void theSessionsArrangementTakesPrecedence()
    {
        const QByteArray layout = fileLayout();
        const QString withLayout = write(path("s1.ass"), kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        const QString legacy = write(path("s2.ass"), kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        start();
        application::SessionTab first, second;
        first.subtitles = QDir::toNativeSeparators(withLayout).toStdString();
        first.layout = ui::WorkspaceArrangement{QStringLiteral("Timing"), {}}.serialise().toStdString();
        second.subtitles = QDir::toNativeSeparators(legacy).toStdString();
        const QString kls = write(path("both.kls"), QByteArray::fromStdString(
                                                        testing::writeLegacySession("HikariSub v1", false, {first, second})));
        loadSession(kls);
        QCOMPARE(application->currentTab(), 1);
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        application->selectTab(0);
        QCOMPARE(preset(), QStringLiteral("Timing"));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));
        QVERIFY(application->workspaceLayout().notice().isEmpty());
        stop();
    }

    void aRetriedSessionFileTakesItsArrangement()
    {
        const QByteArray layout = fileLayout();
        const QString later = path("later.ass");
        start();
        application::SessionTab first, second;
        first.subtitles = QDir::toNativeSeparators(later).toStdString();
        second.subtitles = QDir::toNativeSeparators(path("moved.ass")).toStdString();
        second.layout = ui::WorkspaceArrangement{QStringLiteral("Timing"), {}}.serialise().toStdString();
        const QString kls = write(path("missing.kls"), QByteArray::fromStdString(
                                                           testing::writeLegacySession("HikariSub v1", false, {first, second})));
        loadSession(kls);
        QCOMPARE(application->unresolvedRestores().size(), qsizetype(2));
        application->selectTab(0);
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));

        write(later, kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        QVERIFY(application->retryRestore(0));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        save(later);
        QCOMPARE(payloadOf(layoutLine(readAll(later))).value(QStringLiteral("floatingWindows")).toArray().size(), 1);

        const QString found = write(path("found.ass"), kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        QVERIFY(application->relinkRestore(0, QUrl::fromLocalFile(found)));
        application->selectTab(1);
        QCOMPARE(preset(), QStringLiteral("Timing"));
        QTRY_VERIFY(!isFloating("audioDock"));
        stop();
    }

    void aFileOpenedIntoTheCurrentTabTakesItsArrangement()
    {
        const QByteArray layout = fileLayout();
        const QString file = write(path("into.ass"), kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        start();
        QVERIFY(isOpen("videoDock"));
        QVERIFY(application->openFile(file));
        QCOMPARE(application->tabs().size(), qsizetype(1));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        stop();
    }

    void aFileOpenedBeforeTheWindowPlacesItsPanelsFromIt()
    {
        const QByteArray layout = fileLayout();
        const QPoint offset = floatingOffset(payloadOf(layout));
        const QString file = write(path("early.ass"), kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        start();
        window()->setPosition(140, 90);
        QTRY_COMPARE(window()->position(), QPoint(140, 90));
        QVERIFY(application->workspaceLayout().save());
        application->endSession();
        engine.reset();
        application.reset();

        app::Application::Options options;
        options.settingsFile = path("settings.toml");
        options.playbackAudio = false;
        application = std::make_unique<app::Application>(options);
        QVERIFY(application->openFile(file));
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());
        QTRY_COMPARE(window()->position(), QPoint(140, 90));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        QTRY_COMPARE(shownFloatingOffset(), offset);
        stop();
    }

    void aFileOpenedAsTheReferenceKeepsItsArrangement()
    {
        const QByteArray layout = fileLayout();
        const QString file = write(path("ref.ass"), kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        start();
        QVERIFY(application->openFile(write(path("main.ass"), kAss)));
        QVERIFY(application->openReference(file));
        QVERIFY(isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));
        application->closeReference();
        QCOMPARE(application->tabs().size(), qsizetype(2));
        application->selectTab(1);
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        application->selectTab(0);
        QTRY_VERIFY(isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));
        stop();
    }

    void theSettingOffNeitherReadsNorWrites()
    {
        const QByteArray layout = fileLayout();
        const QByteArray section = "\n[Hikari]\r\nLayout=" + layout + "\r\nFuture=kept\r\n";
        const QString file = write(path("off.ass"), kAss + section);
        const QString plain = write(path("plain.ass"), kAss);
        start();
        setLayoutInFile(false);
        QVERIFY(application->openFile(file));
        QVERIFY(isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));
        applyPreset("Timing");
        QTRY_VERIFY(!isOpen("videoDock"));
        save(file);
        QVERIFY(readAll(file).endsWith(section));
        QCOMPARE(layoutLine(readAll(file)), layout);
        application->addPage();
        QVERIFY(application->openFile(plain));
        save(plain);
        QVERIFY(!readAll(plain).contains("[Hikari]"));
        setLayoutInFile(true);
        save(plain);
        QVERIFY(readAll(plain).contains("[Hikari]"));
        stop();
    }

    void anUnusableFileArrangementFollowsTheNewTabRule_data()
    {
        QTest::addColumn<QByteArray>("layout");
        QTest::addColumn<QString>("reason");
        QTest::newRow("not base64") << QByteArray("{\"schema\"%%") << QStringLiteral("not a layout file");
        QTest::newRow("not an envelope") << QByteArray("bm90IGpzb24=") << QStringLiteral("not a layout file");
        QJsonObject newer = QJsonDocument::fromJson(ui::WorkspaceArrangement{}.serialise()).object();
        newer.insert(QStringLiteral("schema"), 7);
        QTest::newRow("newer") << QJsonDocument(newer).toJson(QJsonDocument::Compact).toBase64()
                               << QStringLiteral("newer version");
        constexpr qsizetype longest = (ui::WorkspaceLayoutController::kMaximumBytes + 2) / 3 * 4;
        QTest::newRow("too long") << QByteArray(longest + 1, '!') << QStringLiteral("larger than");
    }
    void anUnusableFileArrangementFollowsTheNewTabRule()
    {
        QFETCH(QByteArray, layout);
        QFETCH(QString, reason);
        QByteArray tl = kAss;
        tl.replace("ScriptType: v4.00+\n", "ScriptType: v4.00+\nTLMode: Yes\n");
        const QString file = write(path("bad.ass"), tl + "\n[Hikari]\nLayout=" + layout + "\n");
        start();
        float_("audioDock", true);
        application->addPage();
        QVERIFY(application->openFile(file));
        QCOMPARE(preset(), QStringLiteral("Translation"));
        QTRY_VERIFY(!isOpen("audioDock"));
        const QString history = application->log().history();
        QVERIFY2(history.contains(QStringLiteral("bad.ass")) && history.contains(reason), qPrintable(history));
        QVERIFY(application->workspaceLayout().notice().isEmpty());
        QCOMPARE(layoutLine(readAll(file)), layout);
        save(file);
        const QByteArray replaced = layoutLine(readAll(file));
        QVERIFY(replaced != layout);
        QCOMPARE(QJsonDocument::fromJson(QByteArray::fromBase64(replaced)).object().value(QStringLiteral("preset")).toString(),
                 QStringLiteral("Translation"));
        stop();
    }

    void aFloatingPanelInAMissingWindowIsRefused_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QJsonValue>("parent");
        const QString index = QStringLiteral("parentIndex"), name = QStringLiteral("parentMainWindowName");
        QTest::newRow("far") << index << QJsonValue(2147483647);
        QTest::newRow("second window") << index << QJsonValue(1);
        QTest::newRow("below") << index << QJsonValue(-2);
        QTest::newRow("not a number") << index << QJsonValue(QStringLiteral("0"));
        QTest::newRow("another main window") << name << QJsonValue(QStringLiteral("Other"));
        QTest::newRow("name not text") << name << QJsonValue(0);
    }
    void aFloatingPanelInAMissingWindowIsRefused()
    {
        QFETCH(QString, key);
        QFETCH(QJsonValue, parent);
        QJsonObject envelope = QJsonDocument::fromJson(QByteArray::fromBase64(refusalBase())).object();
        QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
        QJsonArray floating = payload.value(QStringLiteral("floatingWindows")).toArray();
        QJsonObject window = floating.first().toObject();
        QCOMPARE(window.value(QStringLiteral("parentIndex")).toInt(-5), 0);
        window.insert(key, parent);
        floating.replace(0, window);
        payload.insert(QStringLiteral("floatingWindows"), floating);
        envelope.insert(QStringLiteral("payload"), payload);
        const QByteArray layout = QJsonDocument(envelope).toJson(QJsonDocument::Compact).toBase64();
        QString problem;
        QVERIFY(!ui::WorkspaceArrangement::parseFromFile(layout, {}, &problem));
        QVERIFY2(problem.contains(QStringLiteral("window that does not exist")), qPrintable(problem));

        opensAsACopyOfTheTab("parent.ass", layout);
    }

    void aPanelPlacedInAMissingMainWindowIsRefused_data()
    {
        QTest::addColumn<QJsonValue>("placeholder");
        QTest::newRow("another window")
            << QJsonValue(QJsonObject{{"isFloatingWindow", false}, {"itemIndex", 0}, {"mainWindowUniqueName", "Other"}});
        QTest::newRow("no window") << QJsonValue(QJsonObject{{"itemIndex", 0}});
        QTest::newRow("floating as text")
            << QJsonValue(QJsonObject{{"isFloatingWindow", "true"}, {"indexOfFloatingWindow", 0}, {"itemIndex", 0}});
        QTest::newRow("not an object") << QJsonValue(QStringLiteral("Classic"));
    }
    void aPanelPlacedInAMissingMainWindowIsRefused()
    {
        QFETCH(QJsonValue, placeholder);
        QJsonObject envelope = QJsonDocument::fromJson(QByteArray::fromBase64(refusalBase())).object();
        QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
        QJsonArray docks = payload.value(QStringLiteral("allDockWidgets")).toArray();
        bool replaced = false;
        for (qsizetype i = 0; i < docks.size() && !replaced; ++i) {
            QJsonObject dock = docks.at(i).toObject();
            QJsonObject position = dock.value(QStringLiteral("lastPosition")).toObject();
            QJsonArray places = position.value(QStringLiteral("placeholders")).toArray();
            for (qsizetype j = 0; j < places.size() && !replaced; ++j) {
                const QJsonObject place = places.at(j).toObject();
                if (place.value(QStringLiteral("isFloatingWindow")).toBool())
                    continue;
                QCOMPARE(place.value(QStringLiteral("mainWindowUniqueName")).toString(), QStringLiteral("Classic"));
                places.replace(j, placeholder);
                position.insert(QStringLiteral("placeholders"), places);
                dock.insert(QStringLiteral("lastPosition"), position);
                docks.replace(i, dock);
                replaced = true;
            }
        }
        QVERIFY(replaced);
        payload.insert(QStringLiteral("allDockWidgets"), docks);
        envelope.insert(QStringLiteral("payload"), payload);
        const QByteArray layout = QJsonDocument(envelope).toJson(QJsonDocument::Compact).toBase64();
        QString problem;
        QVERIFY(!ui::WorkspaceArrangement::parseFromFile(layout, {}, &problem));
        QVERIFY2(problem.contains(QStringLiteral("window that does not exist")), qPrintable(problem));

        opensAsACopyOfTheTab("place.ass", layout);
    }

    void aLayoutLargerThanThePanelsIsRefused_data()
    {
        QTest::addColumn<QString>("change");
        QTest::newRow("floating window clones") << QStringLiteral("floating");
        QTest::newRow("second main window") << QStringLiteral("main");
        QTest::newRow("a panel in two groups") << QStringLiteral("group");
        QTest::newRow("layout items") << QStringLiteral("items");
        QTest::newRow("a panel listed twice") << QStringLiteral("dock");
        QTest::newRow("placeholders") << QStringLiteral("placeholders");
        QTest::newRow("a closed panel twice") << QStringLiteral("closed");
    }
    void aLayoutLargerThanThePanelsIsRefused()
    {
        QFETCH(QString, change);
        QJsonObject envelope = QJsonDocument::fromJson(QByteArray::fromBase64(refusalBase())).object();
        QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
        const auto many = [](const QJsonValue &value, int count) {
            QJsonArray array;
            for (int i = 0; i < count; ++i)
                array.append(value);
            return array;
        };
        QJsonArray mainWindows = payload.value(QStringLiteral("mainWindows")).toArray();
        QJsonObject main = mainWindows.first().toObject();
        QJsonObject splitter = main.value(QStringLiteral("multiSplitterLayout")).toObject();
        QJsonArray docks = payload.value(QStringLiteral("allDockWidgets")).toArray();
        if (change == QStringLiteral("floating")) {
            const QJsonArray floating = payload.value(QStringLiteral("floatingWindows")).toArray();
            QVERIFY(!floating.isEmpty());
            payload.insert(QStringLiteral("floatingWindows"), many(floating.first(), 2000));
        } else if (change == QStringLiteral("main")) {
            payload.insert(QStringLiteral("mainWindows"), many(main, 2));
        } else if (change == QStringLiteral("group")) {
            QJsonObject frames = splitter.value(QStringLiteral("frames")).toObject();
            QVERIFY(!frames.isEmpty());
            frames.insert(QStringLiteral("copy"), frames.begin().value());
            splitter.insert(QStringLiteral("frames"), frames);
        } else if (change == QStringLiteral("items")) {
            QJsonObject layout = splitter.value(QStringLiteral("layout")).toObject();
            QJsonArray children = layout.value(QStringLiteral("children")).toArray();
            QVERIFY(!children.isEmpty());
            layout.insert(QStringLiteral("children"), many(QJsonObject{{QStringLiteral("isContainer"), false}}, 2000));
            splitter.insert(QStringLiteral("layout"), layout);
        } else if (change == QStringLiteral("dock")) {
            docks.append(docks.first());
        } else if (change == QStringLiteral("placeholders")) {
            qsizetype at = 0;
            const auto placesOf = [&](qsizetype i) {
                return docks.at(i).toObject().value(QStringLiteral("lastPosition")).toObject().value(QStringLiteral("placeholders")).toArray();
            };
            while (at < docks.size() && placesOf(at).isEmpty())
                ++at;
            QVERIFY(at < docks.size());
            QJsonObject dock = docks.at(at).toObject();
            QJsonObject last = dock.value(QStringLiteral("lastPosition")).toObject();
            const QJsonArray placeholders = last.value(QStringLiteral("placeholders")).toArray();
            last.insert(QStringLiteral("placeholders"), many(placeholders.first(), 2000));
            dock.insert(QStringLiteral("lastPosition"), last);
            docks.replace(at, dock);
        } else {
            payload.insert(QStringLiteral("closedDockWidgets"), many(QStringLiteral("Video"), 2));
        }
        main.insert(QStringLiteral("multiSplitterLayout"), splitter);
        if (change != QStringLiteral("main"))
            payload.insert(QStringLiteral("mainWindows"), QJsonArray{main});
        payload.insert(QStringLiteral("allDockWidgets"), docks);
        envelope.insert(QStringLiteral("payload"), payload);
        const QByteArray layout = QJsonDocument(envelope).toJson(QJsonDocument::Compact).toBase64();
        QString problem;
        QVERIFY(!ui::WorkspaceArrangement::parseFromFile(layout, {}, &problem));
        QVERIFY2(problem.contains(QStringLiteral("more windows or panels")), qPrintable(problem));

        opensAsACopyOfTheTab("large.ass", layout);
    }

    void aLayoutTheEngineCannotReadIsRefused_data()
    {
        QTest::addColumn<QString>("change");
        QTest::newRow("a child not an object") << QStringLiteral("child");
        QTest::newRow("visibility as text") << QStringLiteral("visible");
        QTest::newRow("a size as text") << QStringLiteral("size");
        QTest::newRow("no orientation") << QStringLiteral("orientation");
        QTest::newRow("a group under another id") << QStringLiteral("id");
        QTest::newRow("a group in two items") << QStringLiteral("twice");
        QTest::newRow("the central group") << QStringLiteral("central");
        QTest::newRow("an affinity") << QStringLiteral("affinity");
    }
    void aLayoutTheEngineCannotReadIsRefused()
    {
        QFETCH(QString, change);
        QJsonObject envelope = QJsonDocument::fromJson(QByteArray::fromBase64(refusalBase())).object();
        QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
        QJsonObject main = payload.value(QStringLiteral("mainWindows")).toArray().first().toObject();
        QJsonObject splitter = main.value(QStringLiteral("multiSplitterLayout")).toObject();
        QJsonObject layout = splitter.value(QStringLiteral("layout")).toObject();
        QJsonArray children = layout.value(QStringLiteral("children")).toArray();
        QJsonObject frames = splitter.value(QStringLiteral("frames")).toObject();
        QVERIFY(!children.isEmpty() && !frames.isEmpty());
        QJsonObject frame = frames.begin().value().toObject();
        QCOMPARE(frame.value(QStringLiteral("id")).toString(), frames.begin().key());
        if (change == QStringLiteral("child")) {
            children.replace(0, 1);
        } else if (change == QStringLiteral("visible")) {
            QJsonObject child = children.first().toObject();
            child.insert(QStringLiteral("isVisible"), QStringLiteral("yes"));
            children.replace(0, child);
        } else if (change == QStringLiteral("size")) {
            QJsonObject sizing = layout.value(QStringLiteral("sizingInfo")).toObject();
            QJsonObject geometry = sizing.value(QStringLiteral("geometry")).toObject();
            geometry.insert(QStringLiteral("width"), QStringLiteral("800"));
            sizing.insert(QStringLiteral("geometry"), geometry);
            layout.insert(QStringLiteral("sizingInfo"), sizing);
        } else if (change == QStringLiteral("orientation")) {
            layout.insert(QStringLiteral("orientation"), QJsonValue::Null);
        } else if (change == QStringLiteral("id")) {
            frame.insert(QStringLiteral("id"), QStringLiteral("other"));
        } else if (change == QStringLiteral("twice")) {
            children.append(QJsonObject{{"isContainer", false}, {"isVisible", true}, {"guestId", frames.begin().key()}});
        } else if (change == QStringLiteral("central")) {
            frame.insert(QStringLiteral("options"), 2);
        } else {
            main.insert(QStringLiteral("affinities"), QJsonArray{QStringLiteral("other")});
        }
        frames.insert(frames.begin().key(), frame);
        layout.insert(QStringLiteral("children"), children);
        splitter.insert(QStringLiteral("layout"), layout);
        splitter.insert(QStringLiteral("frames"), frames);
        main.insert(QStringLiteral("multiSplitterLayout"), splitter);
        payload.insert(QStringLiteral("mainWindows"), QJsonArray{main});
        envelope.insert(QStringLiteral("payload"), payload);
        const QByteArray text = QJsonDocument(envelope).toJson(QJsonDocument::Compact).toBase64();
        QString problem;
        QVERIFY(!ui::WorkspaceArrangement::parseFromFile(text, {}, &problem));
        QVERIFY2(problem.contains(QStringLiteral("not a layout file")), qPrintable(problem));

        opensAsACopyOfTheTab("tree.ass", text);
    }

    void aPanelPositionTheEngineCannotReadIsRefused_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<QString>("reason");
        const QString nowhere = QStringLiteral("window that does not exist");
        const QString unreadable = QStringLiteral("not a layout file");
        QTest::newRow("far floating window") << QStringLiteral("indexOfFloatingWindow") << QJsonValue(2147483647) << nowhere;
        QTest::newRow("next floating window") << QStringLiteral("indexOfFloatingWindow") << QJsonValue(1) << nowhere;
        QTest::newRow("floating window below") << QStringLiteral("indexOfFloatingWindow") << QJsonValue(-2) << nowhere;
        QTest::newRow("no floating window") << QStringLiteral("indexOfFloatingWindow") << QJsonValue(QJsonValue::Undefined)
                                            << nowhere;
        QTest::newRow("floating window as text") << QStringLiteral("indexOfFloatingWindow") << QJsonValue(QStringLiteral("0"))
                                                 << nowhere;
        QTest::newRow("tab below") << QStringLiteral("tabIndex") << QJsonValue(-2) << unreadable;
        QTest::newRow("tab past an int") << QStringLiteral("tabIndex") << QJsonValue(1e20) << unreadable;
        QTest::newRow("place past an int") << QStringLiteral("itemIndex") << QJsonValue(1e20) << unreadable;
    }
    void aPanelPositionTheEngineCannotReadIsRefused()
    {
        QFETCH(QString, key);
        QFETCH(QJsonValue, value);
        QFETCH(QString, reason);
        QJsonObject envelope = QJsonDocument::fromJson(QByteArray::fromBase64(refusalBase())).object();
        QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
        QCOMPARE(payload.value(QStringLiteral("floatingWindows")).toArray().size(), 1);
        QJsonArray docks = payload.value(QStringLiteral("allDockWidgets")).toArray();
        bool changed = false;
        for (qsizetype i = 0; i < docks.size() && !changed; ++i) {
            QJsonObject dock = docks.at(i).toObject();
            if (dock.value(QStringLiteral("uniqueName")) != QJsonValue(QStringLiteral("Audio")))
                continue;
            QJsonObject position = dock.value(QStringLiteral("lastPosition")).toObject();
            QJsonArray places = position.value(QStringLiteral("placeholders")).toArray();
            for (qsizetype j = 0; j < places.size() && !changed; ++j) {
                QJsonObject place = places.at(j).toObject();
                if (!place.value(QStringLiteral("isFloatingWindow")).toBool())
                    continue;
                QCOMPARE(place.value(QStringLiteral("indexOfFloatingWindow")).toInt(-5), 0);
                if (key == QStringLiteral("tabIndex"))
                    position.insert(key, value);
                else if (value.isUndefined())
                    place.remove(key);
                else
                    place.insert(key, value);
                places.replace(j, place);
                position.insert(QStringLiteral("placeholders"), places);
                dock.insert(QStringLiteral("lastPosition"), position);
                docks.replace(i, dock);
                changed = true;
            }
        }
        QVERIFY(changed);
        payload.insert(QStringLiteral("allDockWidgets"), docks);
        envelope.insert(QStringLiteral("payload"), payload);
        const QByteArray text = QJsonDocument(envelope).toJson(QJsonDocument::Compact).toBase64();
        QString problem;
        QVERIFY(!ui::WorkspaceArrangement::parseFromFile(text, {}, &problem));
        QVERIFY2(problem.contains(reason), qPrintable(problem));

        opensAsACopyOfTheTab("position.ass", text);
    }

    void aWindowPlaceNoWindowCanHaveIsRefused_data()
    {
        QTest::addColumn<QString>("windows");
        QTest::addColumn<QString>("rect");
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("main window far right") << QStringLiteral("mainWindows") << QStringLiteral("geometry")
                                               << QStringLiteral("x") << QJsonValue(2147483000);
        QTest::newRow("main window normal place far down") << QStringLiteral("mainWindows") << QStringLiteral("normalGeometry")
                                                           << QStringLiteral("y") << QJsonValue(2147483000);
        QTest::newRow("floating window far down") << QStringLiteral("floatingWindows") << QStringLiteral("geometry")
                                                  << QStringLiteral("y") << QJsonValue(2147483000);
        QTest::newRow("floating window far left") << QStringLiteral("floatingWindows") << QStringLiteral("geometry")
                                                  << QStringLiteral("x") << QJsonValue(-2147483000);
        QTest::newRow("floats again past an int") << QStringLiteral("allDockWidgets") << QStringLiteral("lastFloatingGeometry")
                                                  << QStringLiteral("x") << QJsonValue(1e20);
        QTest::newRow("width as text") << QStringLiteral("floatingWindows") << QStringLiteral("geometry")
                                       << QStringLiteral("width") << QJsonValue(QStringLiteral("200"));
    }
    void aWindowPlaceNoWindowCanHaveIsRefused()
    {
        QFETCH(QString, windows);
        QFETCH(QString, rect);
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);
        QJsonObject envelope = QJsonDocument::fromJson(QByteArray::fromBase64(refusalBase())).object();
        QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
        QJsonArray list = payload.value(windows).toArray();
        QVERIFY(!list.isEmpty());
        QJsonObject window = list.first().toObject();
        if (windows == QStringLiteral("allDockWidgets")) {
            QJsonObject position = window.value(QStringLiteral("lastPosition")).toObject();
            QJsonObject r = position.value(rect).toObject();
            r.insert(QStringLiteral("width"), 200);
            r.insert(QStringLiteral("height"), 100);
            r.insert(field, value);
            position.insert(rect, r);
            window.insert(QStringLiteral("lastPosition"), position);
        } else {
            QJsonObject r = window.value(rect).toObject();
            r.insert(QStringLiteral("width"), r.value(QStringLiteral("width")).toInt() > 0 ? r.value(QStringLiteral("width")) : 200);
            r.insert(QStringLiteral("height"), r.value(QStringLiteral("height")).toInt() > 0 ? r.value(QStringLiteral("height")) : 100);
            r.insert(field, value);
            window.insert(rect, r);
        }
        list.replace(0, window);
        payload.insert(windows, list);
        envelope.insert(QStringLiteral("payload"), payload);
        const QByteArray text = QJsonDocument(envelope).toJson(QJsonDocument::Compact).toBase64();
        QString problem;
        QVERIFY(!ui::WorkspaceArrangement::parseFromFile(text, QPoint(1000, 1000), &problem));
        QVERIFY2(problem.contains(QStringLiteral("not a layout file")), qPrintable(problem));

        opensAsACopyOfTheTab("place.ass", text);
    }

    void aRecoveredCopyKeepsItsArrangement()
    {
        const QString file = write(path("r.ass"), kAss);
        const QString recovery = path("Recovery");
        const QString kept = path("Recovery-kept");
        start(recovery);
        QVERIFY(application->openFile(file));
        close("videoDock");
        float_("audioDock", true);
        QTRY_VERIFY(settled());
        const auto target = *application->workspace().editingTarget();
        auto *session = application->files().session(target);
        const auto line = session->document().lines().front()->id;
        session->run(application::Command{"Set text", session->revision(), {line},
                                          [&](core::Document &d) { return d.setLineText(line, u8"changed"); }});
        QVERIFY(application->autosaveNow());
        QVERIFY(targetDirty());
        QByteArray layout;
        QDirIterator files(recovery, QDir::Files, QDirIterator::Subdirectories);
        while (layout.isEmpty() && files.hasNext())
            layout = layoutLine(readAll(files.next()));
        QCOMPARE(layout, application->workspaceLayout().fileArrangement(target.value));
        QVERIFY(!readAll(file).contains("[Hikari]"));
        QVERIFY(!session->document().hikariProperty(application::kLayoutKey));
        for (QDirIterator it(recovery, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
            const QString from = it.next();
            const QString to = kept + from.mid(recovery.size());
            QVERIFY(QDir().mkpath(QFileInfo(to).path()));
            if (!from.endsWith(QStringLiteral(".lock")))
                QVERIFY(QFile::copy(from, to));
        }
        stop();
        QVERIFY(QDir(recovery).removeRecursively());
        QVERIFY(QDir().rename(kept, recovery));

        start(recovery);
        QVERIFY(isOpen("videoDock"));
        const QVariantList bundles = application->recoveryBundles();
        QCOMPARE(bundles.size(), qsizetype(1));
        const QVariantMap bundle = bundles.first().toMap();
        const QVariantMap generation = bundle.value(QStringLiteral("generations")).toList().first().toMap();
        QVERIFY(application->recoverBundle(bundle.value(QStringLiteral("key")).toString(),
                                           generation.value(QStringLiteral("generation")).toULongLong()));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        QCOMPARE(floatingWindows(), 1);
        stop();
    }

    void anAutosaveLeavesNoSectionForASaveWithTheSettingOff()
    {
        const QString file = write(path("off.ass"), kAss);
        start(path("Recovery-off"));
        QVERIFY(application->openFile(file));
        float_("audioDock", true);
        QTRY_VERIFY(settled());
        const auto target = *application->workspace().editingTarget();
        auto *session = application->files().session(target);
        const auto line = session->document().lines().front()->id;
        session->run(application::Command{"Set text", session->revision(), {line},
                                          [&](core::Document &d) { return d.setLineText(line, u8"changed"); }});
        QVERIFY(application->autosaveNow());
        setLayoutInFile(false);
        save(file);
        const QByteArray bytes = readAll(file);
        QVERIFY(bytes.contains("changed"));
        QVERIFY(!bytes.contains("[Hikari]"));
        stop();
    }

    void aLegacyAutosaveOpensWithItsArrangement()
    {
        const QString file = write(path("legacy.ass"), kAss + "\n[Hikari]\nLayout=" + fileLayout() + "\n");
        start();
        QVERIFY(isOpen("videoDock"));
        QVERIFY(application->openLegacyAutosave(file));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        stop();
    }

    void otherFormatsKeepItOnlyInTheSession()
    {
        const QByteArray srt = "1\r\n00:00:01,000 --> 00:00:02,000\r\none\r\n\r\n";
        const QString file = write(path("a.srt"), srt);
        start();
        QVERIFY(application->openFile(file));
        float_("audioDock", true);
        save(file);
        QCOMPARE(readAll(file), srt);
        QVERIFY(application->saveLastSession());
        const auto session = readSession(application->lastSessionPath());
        QCOMPARE(session.tabs.size(), std::size_t(1));
        QVERIFY(!session.tabs[0].layout.empty());
        stop();
    }

    void aFileKeepsTheSavedLayoutItStartedFrom()
    {
        const QString file = write(path("saved.ass"), kAss);
        start();
        QVERIFY(application->openFile(file));
        applyPreset("Timing");
        QTRY_COMPARE(preset(), QStringLiteral("Timing"));
        float_("audioDock", true);
        QVERIFY(application->workspaceLayout().saveLayoutAs(QStringLiteral("Review")));
        QCOMPARE(application->workspaceLayout().layout(), QStringLiteral("Review"));
        save(file);
        const QJsonObject envelope =
            QJsonDocument::fromJson(QByteArray::fromBase64(layoutLine(readAll(file)))).object();
        QCOMPARE(envelope.value(QStringLiteral("layout")).toString(), QStringLiteral("Review"));
        QCOMPARE(envelope.value(QStringLiteral("preset")).toString(), QStringLiteral("Timing"));

        QVERIFY(application->openFile(write(path("copy.ass"), readAll(file))));
        QCOMPARE(application->tabs().size(), qsizetype(2));
        application->selectTab(1);
        QTRY_VERIFY(isFloating("audioDock"));
        QCOMPARE(application->workspaceLayout().layout(), QStringLiteral("Review"));
        QCOMPARE(preset(), QStringLiteral("Timing"));

        QVERIFY(application->workspaceLayout().deleteSavedLayout(QStringLiteral("Review")));
        QVERIFY(application->openFile(write(path("elsewhere.ass"), readAll(file))));
        QCOMPARE(application->tabs().size(), qsizetype(3));
        application->selectTab(2);
        QTRY_VERIFY(isFloating("audioDock"));
        QVERIFY(application->workspaceLayout().layout().isEmpty());
        QCOMPARE(preset(), QStringLiteral("Timing"));
        QVERIFY(application->workspaceLayout().saveLayoutAs(QStringLiteral("Review")));
        application->selectTab(1);
        QTRY_VERIFY(application->workspaceLayout().layout().isEmpty());
        stop();
    }
};

HIKARI_QTEST_MAIN_IN_PARTS(FileLayoutsTest)
#include "file_layouts_tests.moc"
