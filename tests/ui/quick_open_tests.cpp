#include "hikari/app/api_host.h"
#include "hikari/app/application.h"
#include "docking.h"
#include "quick_pick_model.h"
#include "settings_store.h"

#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

void writeAll(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray ass(std::initializer_list<const char *> lines)
{
    QByteArray out("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                   "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                   "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                   "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                   "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
                   "Style: Sign,Georgia,36,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"
                   "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    int second = 0;
    for (const char *text : lines) {
        out += QByteArray("Dialogue: 0,0:00:0") + QByteArray::number(second) + ".00,0:00:0" +
               QByteArray::number(second + 1) + ".00,Default,,0,0,0,," + text + "\n";
        ++second;
    }
    return out;
}

}

class QuickOpenTests : public QObject {
    Q_OBJECT

    QTemporaryDir home{QDir::current().filePath(QStringLiteral("quick-open-XXXXXX"))};
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    void start()
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        application = new app::Application(o);
        engine = new QQmlApplicationEngine;
        ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->property("arrangementSettled").toBool());
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
    }
    void stop()
    {
        application->cancelClose();
        const QVariantList rows = application->reviewClose(QStringLiteral("quit"));
        QVariantList discard;
        for (const QVariant &r : rows)
            discard << QVariantMap{{QStringLiteral("id"), r.toMap().value(QStringLiteral("id"))},
                                   {QStringLiteral("save"), false},
                                   {QStringLiteral("path"), QString()}};
        if (!discard.isEmpty())
            application->resolveClose(discard);
        delete engine;
        engine = nullptr;
        delete application;
        application = nullptr;
        window = nullptr;
    }
    QObject *popup() const
    {
        QObject *quick = window->findChild<QObject *>(QStringLiteral("quickOpen"));
        return quick ? quick->property("popup").value<QObject *>() : nullptr;
    }
    ui::QuickPickModel *model() const { return popup()->property("model").value<ui::QuickPickModel *>(); }
    bool shown() const { return popup()->property("visible").toBool(); }
    QString text() const { return popup()->property("text").toString(); }
    void type(const QString &chars)
    {
        for (const QChar c : chars)
            QTest::keyClick(window, c.toLatin1());
    }
    QStringList labels() const
    {
        QStringList out;
        for (int row = 0; row < model()->rowCount(); ++row)
            out << model()->data(model()->index(row), ui::QuickPickModel::LabelRole).toString();
        return out;
    }
    QString document(const QString &name, std::initializer_list<const char *> lines)
    {
        const QString path = home.filePath(name);
        writeAll(path, ass(lines));
        return path;
    }
    QVariantMap api(const char *operation, const QVariantMap &params = {})
    {
        return application->apiHost()->call(QString::fromLatin1(operation), params).toMap();
    }

private slots:
    void init()
    {
        start();
    }
    void cleanup() { stop(); }

    void ctrlShiftPOpensThePaletteAndEnterRunsTheCommand()
    {
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(shown());
        QCOMPARE(text(), QStringLiteral(">"));
        QCOMPARE(popup()->property("accessibleName").toString(), QStringLiteral("Command palette"));
        QVERIFY(model()->count() > 100);
        type(QStringLiteral("clear command"));
        QTRY_COMPARE(model()->currentId(), QStringLiteral("GLOBAL_CLEAR_COMMAND_HISTORY"));
        application->settingsStore()->setValue(QStringLiteral("recent.commands"), QStringList{QStringLiteral("GLOBAL_UNDO")});
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!shown());
        QTRY_COMPARE(application->settingsStore()->list("recent.commands"),
                     QStringList{QStringLiteral("GLOBAL_CLEAR_COMMAND_HISTORY")});
    }

    void recentlyUsedCommandsComeFirstAndShowTheirMenuPathAndKey()
    {
        application->settingsStore()->setValue(QStringLiteral("recent.commands"),
                                               QStringList{QStringLiteral("GLOBAL_SAVE_SUBS"), QStringLiteral("GLOBAL_UNDO")});
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(shown());
        const QStringList rows = labels();
        QCOMPARE(rows.mid(0, 3), (QStringList{QStringLiteral("Recently used"), QStringLiteral("Save"), QStringLiteral("Undo")}));
        QCOMPARE(rows.at(3), QStringLiteral("All commands"));
        const QVariantMap save = model()->itemAt(1);
        QCOMPARE(save.value(QStringLiteral("description")).toString(), QStringLiteral("File"));
        QCOMPARE(save.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+S"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!shown());
    }

    void typingAPrefixSwitchesAndBackspaceOnItGoesBackToFiles()
    {
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(shown());
        QCOMPARE(text(), QString());
        QCOMPARE(popup()->property("accessibleName").toString(), QStringLiteral("Quick Open"));
        type(QStringLiteral(">"));
        QTRY_COMPARE(popup()->property("accessibleName").toString(), QStringLiteral("Command palette"));
        QTest::keyClick(window, Qt::Key_Backspace);
        QTRY_COMPARE(popup()->property("accessibleName").toString(), QStringLiteral("Quick Open"));
        type(QStringLiteral("view "));
        QTRY_VERIFY(labels().contains(QStringLiteral("Panels")));
        QTest::keyClick(window, Qt::Key_Backspace);
        QTRY_COMPARE(text(), QString());
        type(QStringLiteral("abc"));
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(text(), QStringLiteral(">abc"));
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
        QTRY_COMPARE(text(), QStringLiteral("abc"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!shown());
    }

    void quickOpenFindsAProjectFileAndOpensIt()
    {
        const QString project = home.filePath(QStringLiteral("project"));
        writeAll(QDir(project).filePath(QStringLiteral("ep01.ass")), ass({"one"}));
        writeAll(QDir(project).filePath(QStringLiteral("signs/sprite-fright.ass")), ass({"two"}));
        writeAll(QDir(project).filePath(QStringLiteral("notes.md")), "x");
        QVERIFY(application->openFolder(project, 0).value(QStringLiteral("ok")).toBool());
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(shown());
        QTRY_VERIFY(labels().contains(QStringLiteral("sprite-fright.ass")));
        QVERIFY(labels().contains(QStringLiteral("Project files")));
        QVERIFY(!labels().contains(QStringLiteral("notes.md")));
        type(QStringLiteral("spfr"));
        QTRY_COMPARE(model()->count(), 1);
        QCOMPARE(model()->itemAt(model()->current()).value(QStringLiteral("description")).toString(), QStringLiteral("signs"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!shown());
        const QString opened = QDir(project).filePath(QStringLiteral("signs/sprite-fright.ass"));
        QTRY_VERIFY(application->tabOfFile(0, opened) >= 0);
    }

    void colonGoesToALineByNumber()
    {
        QVERIFY(application->openInTab(document(QStringLiteral("lines.ass"), {"first", "second", "third"})));
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(shown());
        type(QStringLiteral(":"));
        QTRY_COMPARE(popup()->property("foot").toString(), QStringLiteral("Type a line number, 1 to 3."));
        type(QStringLiteral("9"));
        QTRY_COMPARE(popup()->property("emptyText").toString(),
                     QStringLiteral("Not a line: 9. This Document has 3 lines."));
        QTest::keyClick(window, Qt::Key_Backspace);
        type(QStringLiteral("2"));
        QTRY_COMPARE(model()->count(), 1);
        QVERIFY(model()->itemAt(0).value(QStringLiteral("description")).toString().endsWith(QStringLiteral("second")));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!shown());
        const QVariantMap selection = api("selection.get");
        const QVariantMap line = api("lines.get", {{QStringLiteral("ids"), QVariantList{selection.value(QStringLiteral("active"))}}})
                                     .value(QStringLiteral("lines")).toList().value(0).toMap();
        QCOMPARE(line.value(QStringLiteral("row")).toInt(), 2);
    }

    void atWithoutVideoSelectsTheLineShownAtATimeAndRefusesFrames()
    {
        QVERIFY(application->openInTab(document(QStringLiteral("times.ass"), {"first", "second", "third"})));
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(shown());
        type(QStringLiteral("@3:99"));
        QTRY_COMPARE(popup()->property("emptyText").toString(),
                     QStringLiteral("Not a time or frame: 3:99. Try 3:40 or a frame number."));
        for (int i = 0; i < 4; ++i)
            QTest::keyClick(window, Qt::Key_Backspace);
        type(QStringLiteral("120"));
        QTRY_COMPARE(popup()->property("emptyText").toString(), QStringLiteral("Frames need a video."));
        for (int i = 0; i < 3; ++i)
            QTest::keyClick(window, Qt::Key_Backspace);
        type(QStringLiteral("0:01.5"));
        QTRY_COMPARE(model()->count(), 1);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!shown());
        const QVariant active = api("selection.get").value(QStringLiteral("active"));
        const QVariantMap line = api("lines.get", {{QStringLiteral("ids"), QVariantList{active}}})
                                     .value(QStringLiteral("lines")).toList().value(0).toMap();
        QCOMPARE(line.value(QStringLiteral("row")).toInt(), 2);
    }

    void hashListsStylesAndNeedsASelectionToApply()
    {
        QVERIFY(application->openInTab(document(QStringLiteral("styles.ass"), {"first", "second"})));
        api("selection.set", {{QStringLiteral("selected"), QVariantList{}}});
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(shown());
        type(QStringLiteral("#sig"));
        QTRY_COMPARE(model()->count(), 1);
        const QVariantMap sign = model()->itemAt(model()->current());
        QCOMPARE(sign.value(QStringLiteral("description")).toString(), QStringLiteral("Georgia 36"));
        QCOMPARE(sign.value(QStringLiteral("disabled")).toString(), QStringLiteral("Needs a selection"));
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(shown());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!shown());
    }

    void escapeCancelsAndAPressOutsideCloses()
    {
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(shown());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!shown());
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(shown());
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(window->width() / 2, window->height() - 20));
        QTRY_VERIFY(!shown());
    }

    void helpListsThePrefixesAndChoosingOneTypesIt()
    {
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(shown());
        type(QStringLiteral("?"));
        QTRY_COMPARE(labels(), (QStringList{QStringLiteral(">"), QStringLiteral(":"), QStringLiteral("@"),
                                            QStringLiteral("#"), QStringLiteral("view"), QStringLiteral("?")}));
        QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(text(), QStringLiteral(":"));
        QTest::keyClick(window, Qt::Key_Escape);
    }
};

QTEST_MAIN(QuickOpenTests)
#include "quick_open_tests.moc"
