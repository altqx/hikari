#include "hikari/app/application.h"
#include "docking.h"

#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(bytes);
    return path;
}

QString fixture(const char *name)
{
    return QStringLiteral(HIKARI_ATTACHMENT_FIXTURES "/") + QLatin1String(name);
}

QByteArray restrictedFont()
{
    QByteArray font;
    const auto put = [&](quint32 value, int width) {
        for (int shift = (width - 1) * 8; shift >= 0; shift -= 8)
            font.append(char((value >> shift) & 0xFF));
    };
    put(0x00010000, 4);
    put(1, 2);
    put(0, 6);
    put(0x4F532F32, 4);
    put(0, 4);
    put(28, 4);
    put(10, 4);
    put(0x0004, 2);
    put(500, 2);
    put(400, 2);
    put(5, 2);
    put(0x0002, 2);
    return font;
}

QStringList names(const QVariantList &rows)
{
    QStringList out;
    for (const auto &row : rows)
        out << row.toMap().value(QStringLiteral("name")).toString();
    return out;
}

}

class AttachmentsTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    app::Application::Options options()
    {
        app::Application::Options o;
        o.recoveryDir = dir.filePath(QStringLiteral("Recovery-") + QLatin1String(QTest::currentTestFunction()));
        o.settingsFile = dir.filePath(QStringLiteral("ui-") + QLatin1String(QTest::currentTestFunction()) + QStringLiteral(".ini"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        return o;
    }
    QString script(const char *name)
    {
        return writeBytes(dir.filePath(QLatin1String(name)),
                          "[Script Info]\nScriptType: v4.00+\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, "
                          "MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x\n");
    }
    application::EditSession *target(app::Application &a)
    {
        return a.files().session(*a.workspace().editingTarget());
    }
    static QQuickItem *itemNamed(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = itemNamed(child, name))
                return found;
        return nullptr;
    }
    static QObject *find(QQuickWindow *window, const char *name)
    {
        if (QObject *found = window->findChild<QObject *>(QLatin1String(name)))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(w))
                if (QQuickItem *found = itemNamed(quick->contentItem(), QLatin1String(name)))
                    return found;
        return nullptr;
    }

private slots:
    void attachingAndRemovingAreUndoStepsThatSave()
    {
        const QString path = script("attach.ass");
        app::Application a(options());
        QVERIFY(a.openFile(path));
        QVERIFY(a.canAttach());
        QVERIFY(a.attachmentRows().isEmpty());
        const QUrl font = QUrl::fromLocalFile(fixture("HikariAttachedTest.ttf"));
        const QUrl png = QUrl::fromLocalFile(fixture("red.png"));

        QVERIFY(a.attachFiles(true, {font}));
        QVERIFY(a.attachFiles(false, {png}));
        QCOMPARE(names(a.attachmentRows()), (QStringList{QStringLiteral("HikariAttachedTest_0.ttf"), QStringLiteral("red.png")}));
        const auto first = a.attachmentRows().first().toMap();
        QVERIFY(first.value(QStringLiteral("font")).toBool());
        QCOMPARE(first.value(QStringLiteral("size")).toLongLong(), readAll(fixture("HikariAttachedTest.ttf")).size());
        auto history = target(a)->history();
        QCOMPARE(history[history.size() - 2].name, std::string("Attach fonts"));
        QCOMPARE(history.back().name, std::string("Attach graphics"));

        QVERIFY(target(a)->undo());
        QCOMPARE(names(a.attachmentRows()), QStringList{QStringLiteral("HikariAttachedTest_0.ttf")});
        QVERIFY(target(a)->redo());
        QVERIFY(a.removeAttachments({0}));
        QCOMPARE(target(a)->history().back().name, std::string("Remove attachment"));
        QCOMPARE(names(a.attachmentRows()), QStringList{QStringLiteral("red.png")});

        QVERIFY(!a.attachFiles(true, {QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.ttf")))}));
        QCOMPARE(a.attachmentRows().size(), qsizetype(1));

        QVERIFY(a.attachFiles(true, {font, font}));
        QCOMPARE(a.attachmentRows().size(), qsizetype(3));
        QVERIFY(a.removeAttachments({2, 0, 2}));
        QCOMPARE(names(a.attachmentRows()), QStringList{QStringLiteral("HikariAttachedTest_0.ttf")});
        QCOMPARE(target(a)->history().back().name, std::string("Remove attachment"));

        QVERIFY(a.saveDocument(0));
        a.waitForWrites();
        const QByteArray saved = readAll(path);
        QVERIFY(saved.contains("\n\n[Fonts]\nfontname: HikariAttachedTest_0.ttf\n"));
        QVERIFY(!saved.contains("[Graphics]"));

        app::Application b(options());
        QVERIFY(b.openFile(path));
        QCOMPARE(names(b.attachmentRows()), QStringList{QStringLiteral("HikariAttachedTest_0.ttf")});
        const QUrl out = b.attachmentFile(QUrl::fromLocalFile(dir.path()), 0);
        QCOMPARE(out.fileName(), QStringLiteral("HikariAttachedTest_0.ttf"));
        QCOMPARE(b.extractAttachment(0, out, false), QStringLiteral("written"));
        QCOMPARE(readAll(out.toLocalFile()), readAll(fixture("HikariAttachedTest.ttf")));
        QCOMPARE(b.extractAttachment(0, out, false), QStringLiteral("exists"));
        writeBytes(out.toLocalFile(), "old");
        QCOMPARE(b.extractAttachment(0, out, true), QStringLiteral("written"));
        QCOMPARE(readAll(out.toLocalFile()), readAll(fixture("HikariAttachedTest.ttf")));
    }

    void largeFilesAndRestrictedFontsAskFirst()
    {
        app::Application a(options());
        QVERIFY(a.openFile(script("warn.ass")));
        const QUrl font = QUrl::fromLocalFile(fixture("HikariAttachedTest.ttf"));
        QVERIFY(a.attachmentWarnings(true, {font}).isEmpty());

        const QUrl restricted = QUrl::fromLocalFile(writeBytes(dir.filePath(QStringLiteral("Locked.ttf")), restrictedFont()));
        const QStringList licence = a.attachmentWarnings(true, {font, restricted});
        QCOMPARE(licence.size(), qsizetype(1));
        QVERIFY(licence.first().contains(QStringLiteral("“Locked.ttf”")));
        QVERIFY(licence.first().contains(QStringLiteral("licence")));
        QVERIFY(a.attachmentWarnings(false, {restricted}).isEmpty());

        const QUrl big = QUrl::fromLocalFile(
            writeBytes(dir.filePath(QStringLiteral("big.png")), QByteArray(11 * 1024 * 1024, 'x')));
        const QStringList large = a.attachmentWarnings(false, {big});
        QCOMPARE(large.size(), qsizetype(1));
        QVERIFY(large.first().contains(QStringLiteral("over 10 MB")));
        const QStringList total = a.attachmentWarnings(false, {big, big, big, big, big});
        QCOMPARE(total.size(), qsizetype(6));
        QVERIFY(total.last().contains(QStringLiteral("over 50 MB")));
    }

    void otherFormatsTakeNoAttachments()
    {
        const QString path = writeBytes(dir.filePath(QStringLiteral("plain.srt")), "1\n00:00:00,000 --> 00:00:01,000\nx\n");
        app::Application a(options());
        QVERIFY(a.openFile(path));
        QVERIFY(!a.canAttach());
        QVERIFY(!a.attachFiles(true, {QUrl::fromLocalFile(fixture("HikariAttachedTest.ttf"))}));
        QVERIFY(a.attachmentRows().isEmpty());
    }

    void theWindowListsRemovesAndFollowsUndo()
    {
        app::Application a(options());
        QVERIFY(a.openFile(script("window.ass")));
        QVERIFY(a.attachFiles(false, {QUrl::fromLocalFile(fixture("red.png"))}));
        QQmlApplicationEngine engine;
        hikari::ui::attachDocking(engine);
        engine.setInitialProperties(a.qmlProperties());
        engine.loadFromModule("Hikari.Ui", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        const auto named = [window](const char *name) { return find(window, name); };

        QVERIFY(QMetaObject::invokeMethod(named("attachments"), "show"));
        QTRY_VERIFY(named("attachmentsWindow"));
        auto *attachments = qobject_cast<QQuickWindow *>(named("attachmentsWindow"));
        QTRY_VERIFY(attachments->isVisible());
        QTRY_COMPARE(named("attachmentsList")->property("count").toInt(), 1);
        QTRY_VERIFY(named("attachmentName0"));
        QCOMPARE(named("attachmentName0")->property("text").toString(), QStringLiteral("red.png"));
        QVERIFY(!named("attachmentsRemove")->property("enabled").toBool());
        QVERIFY(!named("attachmentsExtract")->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("attachments"), "select", Q_ARG(QVariant, 0), Q_ARG(QVariant, 0)));
        QTRY_VERIFY(named("attachmentsRemove")->property("enabled").toBool());
        QVERIFY(named("attachmentsExtract")->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("attachmentsRemove"), "clicked"));
        QTRY_COMPARE(named("attachmentsList")->property("count").toInt(), 0);
        QVERIFY(named("attachmentsEmpty")->property("visible").toBool());
        QVERIFY(a.editor().undo());
        QTRY_COMPARE(named("attachmentsList")->property("count").toInt(), 1);
        QVERIFY(named("attachmentsAttachFont")->property("enabled").toBool());
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    AttachmentsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "attachments_tests.moc"
