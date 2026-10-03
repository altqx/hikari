// L3: the Qt adapters behind automation host services — the system
// clipboard (also through the real Lua helper), text measurement for
// text_extents, wildcard conversion and the fixed QML file picker.
#include "automation_services_qt.h"
#include "hikari/backends/lua_script_host.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari::application;
using hikari::backends::LuaScriptHost;
using hikari::ui::AutomationFilePickerController;
using hikari::ui::QtClipboardPort;
using hikari::ui::QtTextMeasurePort;

namespace {

std::vector<std::string> style(const std::string &font, const std::string &scaleX = "100",
                               const std::string &scaleY = "100", const std::string &spacing = "0")
{
    return {"Default", font, "40", "&H00FFFFFF", "&H000000FF", "&H00000000", "&H00000000", "0", "0", "0", "0",
            scaleX, scaleY, spacing, "0", "1", "2", "2", "2", "10", "10", "10", "1"};
}

} // namespace

class AutomationServicesUiTests : public QObject {
    Q_OBJECT

    QString family;

private slots:
    void initTestCase()
    {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(HIKARI_TEST_FONT));
        QVERIFY(id >= 0);
        family = QFontDatabase::applicationFontFamilies(id).value(0);
        QVERIFY(!family.isEmpty());
    }

    void clipboardRoundTrips()
    {
        QtClipboardPort clipboard;
        QVERIFY(clipboard.setText("zażółć"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("zażółć"));
        QGuiApplication::clipboard()->setText(QStringLiteral("from the app"));
        QCOMPARE(clipboard.text(), std::string("from the app"));
    }

    // The script's aegisub.__init_clipboard table reaches the system clipboard.
    void clipboardThroughTheHelper()
    {
        QtClipboardPort clipboard;
        AutomationServiceRouter router;
        router.setClipboard(&clipboard);
        LuaScriptHost host(QStringLiteral(HIKARI_LUA_HELPER), QStringLiteral(HIKARI_LUA_FIXTURES "/services.lua"),
                           QStringLiteral(HIKARI_LUA_INCLUDE));
        host.setServiceHandler([&](const HostServiceRequest &r, LuaScriptHost::ServiceReply reply) {
            router.handle(r, std::move(reply));
        });
        QStringList log;
        std::optional<LuaScriptHost::RunOutcome> outcome;
        connect(&host, &LuaScriptHost::logged, this, [&](const QString &t) { log << t; });
        connect(&host, &LuaScriptHost::finished, this,
                [&](LuaScriptHost::RunOutcome o, const QString &) { outcome = o; });
        host.load();
        QTRY_VERIFY(host.state() == LuaScriptHost::State::Ready);
        int index = -1;
        for (std::size_t i = 0; i < host.info().macros.size(); ++i)
            if (host.info().macros[i].name == "Clipboard")
                index = static_cast<int>(i);
        QVERIFY(host.run(index));
        QTRY_VERIFY(outcome.has_value());
        QCOMPARE(*outcome, LuaScriptHost::RunOutcome::Ok);
        QCOMPARE(log, (QStringList{QStringLiteral("true"), QStringLiteral("copied")}));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("copied"));
    }

    void textExtentsFollowTheLegacyScaling()
    {
        QtTextMeasurePort measure;
        const std::string f = family.toStdString();
        const auto plain = measure.measure(style(f), "Hello");
        QVERIFY(plain);
        QVERIFY(plain->width > 0);
        QVERIFY(plain->height > 0);
        QVERIFY(plain->descent > 0);
        const auto empty = measure.measure(style(f), "");
        QVERIFY(empty && empty->width == 0 && empty->height == 0);
        // ScaleX and ScaleY scale the measured extents.
        const auto wide = measure.measure(style(f, "200", "50"), "Hello");
        QVERIFY(wide);
        QVERIFY(qAbs(wide->width - 2 * plain->width) < 1e-3);
        QVERIFY(qAbs(wide->height - plain->height / 2) < 1e-3);
        QVERIFY(qAbs(wide->descent - plain->descent / 2) < 1e-3);
        // Spacing measures each character alone and adds the spacing to each.
        double perCharacter = 0;
        for (const char *c : {"H", "e", "l", "l", "o"})
            perCharacter += measure.measure(style(f), c)->width;
        const auto spaced = measure.measure(style(f, "100", "100", "2"), "Hello");
        QVERIFY(spaced);
        QVERIFY2(qAbs(spaced->width - (perCharacter + 5 * 2)) < 1e-2,
                 qPrintable(QStringLiteral("%1 vs %2").arg(spaced->width).arg(perCharacter + 10)));
    }

    void wildcardsBecomeNameFilters()
    {
        QCOMPARE(hikari::ui::nameFiltersOf("Text files (*.txt)|*.txt|All files|*.*;*"),
                 (QStringList{QStringLiteral("Text files (*.txt)"), QStringLiteral("All files (*.* *)")}));
        QVERIFY(hikari::ui::nameFiltersOf("").isEmpty());
    }

    void pickerAnswersOnceAndDropsAfterWithdraw()
    {
        AutomationFilePickerController picker;
        std::vector<std::optional<std::vector<std::string>>> answers;
        const auto collect = [&](std::optional<std::vector<std::string>> a) { answers.push_back(std::move(a)); };
        FilePickerRequest request;
        request.mode = FilePickerRequest::Mode::OpenMultiple;
        request.title = "Pick";
        request.dir = QDir::tempPath().toStdString();
        request.file = "a.txt";
        request.wildcard = "Text|*.txt";
        picker.pick(request, collect);
        QVERIFY(picker.isOpen());
        QVERIFY(picker.isMultiple());
        QCOMPARE(picker.title(), QStringLiteral("Pick"));
        QCOMPARE(picker.folder(), QUrl::fromLocalFile(QDir::tempPath()));
        QCOMPARE(picker.nameFilters(), QStringList{QStringLiteral("Text (*.txt)")});
        const QString chosen = QDir::temp().absoluteFilePath(QStringLiteral("b.txt"));
        picker.accept({QUrl::fromLocalFile(chosen)});
        picker.accept({QUrl::fromLocalFile(chosen)}); // only the first answer counts
        QCOMPARE(answers.size(), std::size_t(1));
        QCOMPARE(answers[0].value(), std::vector<std::string>{QDir::toNativeSeparators(chosen).toStdString()});

        picker.pick(request, collect);
        picker.reject();
        QCOMPARE(answers.size(), std::size_t(2));
        QVERIFY(!answers[1]);

        picker.pick(request, collect);
        picker.withdraw();
        QVERIFY(!picker.isOpen());
        picker.accept({QUrl::fromLocalFile(chosen)});
        QCOMPARE(answers.size(), std::size_t(2));
    }

    // The fixed QML FileDialog over the picker. Native dialogs block, so this
    // runs only with the offscreen platform's Qt Quick implementation.
    void qmlPickerAcceptsTheChosenFile()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("native file dialogs are modal; the QML picker is driven offscreen");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AutomationFilePickerController picker;
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{QStringLiteral("picker"), QVariant::fromValue(&picker)}});
        engine.loadFromModule("Hikari.Ui", "AutomationFilePicker");
        QCOMPARE(engine.rootObjects().size(), qsizetype(1));
        QObject *dialog = engine.rootObjects().first();
        QQuickWindow host; // a Qt Quick dialog opens inside its parent window
        host.resize(800, 600);
        host.show();
        dialog->setProperty("parentWindow", QVariant::fromValue(static_cast<QWindow *>(&host)));
        std::optional<std::optional<std::vector<std::string>>> answer;
        FilePickerRequest request;
        request.mode = FilePickerRequest::Mode::Save;
        request.title = "Save";
        request.dir = dir.path().toStdString();
        request.file = "out.txt";
        request.promptOverwrite = false;
        picker.pick(request, [&](std::optional<std::vector<std::string>> a) { answer = std::move(a); });
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("title").toString(), QStringLiteral("Save"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(answer.has_value());
        QVERIFY(answer->has_value());
        QCOMPARE(answer->value().size(), std::size_t(1));
        QCOMPARE(QString::fromStdString(answer->value()[0]),
                 QDir::toNativeSeparators(QDir(dir.path()).absoluteFilePath(QStringLiteral("out.txt"))));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
};

QTEST_MAIN(AutomationServicesUiTests)
#include "automation_services_ui_tests.moc"
