// WS2 (#216): an ASS file keeps its Document tab's panel arrangement in a
// [Hikari] section. A save writes the tab's (a layout change alone marks
// nothing modified); opening the file gives its tab that arrangement, the
// floating panels placed from the main window; a restored Session's own
// arrangement takes precedence; a file without one, or one that does not
// validate, follows WS1's new-tab rule. "Save panel layout in subtitle
// files" off, the section is neither read nor written and stays as it is.
// Other formats keep the arrangement only in the Session.

#include "hikari/app/application.h"
#include "hikari/application/session_file.h"

#include "docking.h"
#include "log_controller.h"
#include "settings_store.h"
#include "workspace_layout.h"

#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/core/DockRegistry.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
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

// The "Layout=" value of a file's [Hikari] section; empty without one.
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
    auto text = application::decodeSessionBytes(std::string_view(bytes.constData(), bytes.size()));
    return application::parseSession(text.value_or(std::string())).value_or(application::Session{});
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

// The first floating window's top-left from the main window's in an engine
// payload.
QPoint floatingOffset(const QJsonObject &payload)
{
    return topLeft(payload.value(QStringLiteral("floatingWindows")).toArray().first().toObject()) -
           topLeft(payload.value(QStringLiteral("mainWindows")).toArray().first().toObject());
}

// The same in the arrangement shown.
QPoint shownFloatingOffset()
{
    return floatingOffset(QJsonDocument::fromJson(KDDockWidgets::LayoutSaver().serializeLayout()).object());
}

// Whether the shown arrangement has settled: the first floating window has
// its size and the main window's normal geometry has followed its moves.
bool settled()
{
    const QJsonObject payload = QJsonDocument::fromJson(KDDockWidgets::LayoutSaver().serializeLayout()).object();
    const QJsonObject main = payload.value(QStringLiteral("mainWindows")).toArray().first().toObject();
    const QJsonObject floating = payload.value(QStringLiteral("floatingWindows")).toArray().first().toObject();
    return floating.value(QStringLiteral("geometry")).toObject().value(QStringLiteral("width")).toInt() > 0 &&
           topLeft(main) == topLeft(QJsonObject{{QStringLiteral("geometry"), main.value(QStringLiteral("normalGeometry"))}});
}

} // namespace

class FileLayoutsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QString path(const char *name) const { return dir.filePath(QLatin1String(name)); }
    void start()
    {
        app::Application::Options options;
        options.settingsFile = path("hikari.ini");
        options.playbackAudio = false;
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
        // The next start shows the default arrangement, not this one.
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
    // Saves the editing target in place and waits for the write.
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
    // An arrangement with the Video panel closed and the Audio panel
    // floating, as a file keeps it (made in a running shell, then closed).
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

private slots:
    void cleanup()
    {
        engine.reset();
        application.reset();
        for (const char *name : {"layout.json", "layout.json.bak", "layout.json.unrestored", "layout.json.tab.unrestored",
                                 "LastSession.txt", "hikari.ini"})
            QFile::remove(path(name));
    }

    // The file form: base64 (no ':'), no screens, each window's screen left
    // out, the main window at 0,0 with its size, floating windows and the
    // places closed panels float again at from its top-left; read back,
    // they are placed from the main window's top-left given.
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
        QCOMPARE(lastFloating(docks.at(1)).value(QStringLiteral("x")).toInt(), 0); // never floated: as it was

        QString problem;
        const auto read = ui::WorkspaceArrangement::parseFromFile(text, QPoint(10, 20), &problem);
        QVERIFY2(read, qPrintable(problem));
        QCOMPARE(read->preset, QStringLiteral("Timing"));
        const QJsonObject placed = QJsonDocument::fromJson(read->payload).object();
        QCOMPARE(topLeft(placed.value(QStringLiteral("mainWindows")).toArray().first().toObject()), QPoint(10, 20));
        QCOMPARE(topLeft(placed.value(QStringLiteral("floatingWindows")).toArray().first().toObject()), QPoint(210, 80));
        QCOMPARE(lastFloating(placed.value(QStringLiteral("allDockWidgets")).toArray().at(0)).value(QStringLiteral("x")).toInt(), 30);
        // A preset alone, and text that is not one.
        const auto alone = ui::WorkspaceArrangement::parseFromFile(ui::WorkspaceArrangement{QStringLiteral("Translation"), {}}.serialiseForFile(), {});
        QVERIFY(alone && alone->payload.isEmpty() && alone->preset == QStringLiteral("Translation"));
        QVERIFY(!ui::WorkspaceArrangement::parseFromFile("eyJ:", {}, &problem));
        QVERIFY(!ui::WorkspaceArrangement::parseFromFile("", {}, &problem));
    }

    // A layout change marks nothing modified; Save writes the tab's
    // arrangement at the end of the file, without screen positions, and the
    // file reopened in a new start brings it back.
    void aSavedFileKeepsItsTabsArrangement()
    {
        const QString file = write(path("a.ass"), kAss);
        start();
        window()->setPosition(60, 40); // positions apart from offsets
        QTRY_COMPARE(window()->position(), QPoint(60, 40));
        QVERIFY(application->openFile(file));
        close("videoDock");
        float_("audioDock", true);
        QTRY_VERIFY(settled());
        QVERIFY(!targetDirty());
        save(file);
        const QByteArray saved = readAll(file);
        // At the end, after the Events (Script Info has the active Line, S4).
        QVERIFY2(saved.contains(kAss.mid(kAss.indexOf("[Events]")) + "\n[Hikari]\nLayout="), saved.constData());
        QVERIFY(saved.endsWith("\n"));
        QCOMPARE(saved.count("[Hikari]"), 1);
        const QByteArray layout = layoutLine(saved);
        QVERIFY(!layout.contains(':'));
        // tests/fixtures/hikari-section/layout.txt is captured here.
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
        // The floating panel's place from the main window's top-left.
        const QPoint offset = shownFloatingOffset();
        QCOMPARE(floatingOffset(payload), offset);
        // The Document took the value: the next save without a change writes the same bytes.
        save(file);
        QCOMPARE(readAll(file), saved);
        stop();

        start();
        QVERIFY(isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));
        window()->setPosition(140, 90); // elsewhere: the panel follows the main window
        QTRY_COMPARE(window()->position(), QPoint(140, 90));
        QVERIFY(application->openFile(file));
        QTRY_VERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        QCOMPARE(floatingWindows(), 1);
        QTRY_COMPARE(shownFloatingOffset(), offset);
        QVERIFY(!targetDirty());
        stop();
    }

    // Opened beside the current tab, the file's tab takes its arrangement
    // when it is shown; the current tab keeps its own.
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

    // A restored Session's own arrangement wins over the file's; a Session
    // tab without one takes the file's.
    void theSessionsArrangementTakesPrecedence()
    {
        const QByteArray layout = fileLayout();
        const QString withLayout = write(path("s1.ass"), kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        const QString legacy = write(path("s2.ass"), kAss + "\n[Hikari]\nLayout=" + layout + "\n");
        start();
        application::SessionTab first, second;
        first.subtitles = QDir::toNativeSeparators(withLayout).toStdString();
        first.layout = ui::WorkspaceArrangement{QStringLiteral("Timing"), {}}.serialise().toStdString();
        second.subtitles = QDir::toNativeSeparators(legacy).toStdString(); // no Layout line, as legacy writes
        const QString kls = write(path("both.kls"), QByteArray::fromStdString(
                                                        application::writeSession("HikariSub v1", false, {first, second})));
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

    // A file opened into the untouched tab brings its arrangement there too.
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

    // Off, a file's arrangement is not used, and a save leaves an existing
    // section exactly as it was (and adds none to a file without one).
    void theSettingOffNeitherReadsNorWrites()
    {
        const QByteArray layout = fileLayout();
        const QByteArray section = "\n[Hikari]\r\nLayout=" + layout + "\r\nFuture=kept\r\n";
        const QString file = write(path("off.ass"), kAss + section);
        const QString plain = write(path("plain.ass"), kAss);
        start();
        setLayoutInFile(false);
        QVERIFY(application->openFile(file));
        QVERIFY(isOpen("videoDock")); // the new-tab rule: a copy of the tab as it was
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

    // An arrangement that does not validate is named in the log and not
    // used: the tab follows the new-tab rule (here a TLMode file, so the
    // Translation preset); the section stays until the next save replaces it.
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

    // SRT and the other formats keep the arrangement only in the Session.
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
};

QTEST_MAIN(FileLayoutsTest)
#include "file_layouts_tests.moc"
