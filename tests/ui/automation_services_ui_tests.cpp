#include "automation_services_qt.h"
#include "hikari/backends/lua_script_host.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <cstdlib>
#include <optional>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari::application;
using hikari::backends::LuaScriptHost;
using hikari::ui::AutomationFilePickerController;
using hikari::ui::QtClipboardPort;
using hikari::ui::QtTextMeasurePort;

namespace {

std::vector<QJsonObject> legacyExtents()
{
    QFile f(QStringLiteral(HIKARI_VISUAL_T3_MEASURE_OBSERVATIONS));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    std::vector<QJsonObject> out;
    QSet<QByteArray> seen;
    for (const QByteArray &l : f.readAll().split('\n')) {
        if (l.trimmed().isEmpty())
            continue;
        for (const QJsonValue &v : QJsonDocument::fromJson(l).object()[QStringLiteral("extents")].toArray()) {
            const QByteArray key = QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
            if (!seen.contains(key)) {
                seen.insert(key);
                out.push_back(v.toObject());
            }
        }
    }
    return out;
}

std::vector<std::string> style(const std::string &font, const std::string &scaleX = "100",
                               const std::string &scaleY = "100", const std::string &spacing = "0")
{
    return {"Default", font, "40", "&H00FFFFFF", "&H000000FF", "&H00000000", "&H00000000", "0", "0", "0", "0",
            scaleX, scaleY, spacing, "0", "1", "2", "2", "2", "10", "10", "10", "1"};
}

}

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
        const QString bold = QFileInfo(QStringLiteral(HIKARI_TEST_FONT)).dir().filePath(QStringLiteral("TitilliumWeb-Bold.ttf"));
        QVERIFY(QFontDatabase::addApplicationFont(bold) >= 0);
    }

    void clipboardRoundTrips()
    {
        QtClipboardPort clipboard;
        QVERIFY(clipboard.setText("zażółć"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("zażółć"));
        QGuiApplication::clipboard()->setText(QStringLiteral("from the app"));
        QCOMPARE(clipboard.text(), std::string("from the app"));
    }

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
        const auto wide = measure.measure(style(f, "200", "50"), "Hello");
        QVERIFY(wide);
        QVERIFY(qAbs(wide->width - 2 * plain->width) < 1e-3);
        QVERIFY(qAbs(wide->height - plain->height / 2) < 1e-3);
        QVERIFY(qAbs(wide->descent - plain->descent / 2) < 1e-3);
        double perCharacter = 0;
        for (const char *c : {"H", "e", "l", "l", "o"})
            perCharacter += measure.measure(style(f), c)->width;
        const auto spaced = measure.measure(style(f, "100", "100", "2"), "Hello");
        QVERIFY(spaced);
        QVERIFY2(qAbs(spaced->width - (perCharacter + 5 * 2)) < 1e-2,
                 qPrintable(QStringLiteral("%1 vs %2").arg(spaced->width).arg(perCharacter + 10)));
    }

    // The slack covers hinting differences between Qt's and wxGTK's text engines; spaced text rounds each character alone.
    void textExtentsAgainstTheLegacyLinuxMeasure()
    {
#ifdef _WIN32
        QSKIP("legacy's Windows measure is GDI (UtilsWindows.cpp:254-300), which QtTextMeasurePort runs unchanged; "
              "the capture is the Linux build's wx measure");
#else
        QtTextMeasurePort measure;
        int pieces = 0;
        for (const QJsonObject &e : legacyExtents()) {
            const QString text = e[QStringLiteral("text")].toString();
            if (text.isEmpty())
                continue;
            ++pieces;
            const auto str = [&](const char *name) { return e[QLatin1StringView(name)].toString().toStdString(); };
            std::vector<std::string> st = style(str("fontname"), str("scaleX"), str("scaleY"), str("spacing"));
            st[2] = str("fontsize");
            st[7] = e[QStringLiteral("bold")].toBool() ? "-1" : "0";
            st[8] = e[QStringLiteral("italic")].toBool() ? "-1" : "0";
            const auto got = measure.measure(st, text.toStdString());
            QVERIFY(got);
            const double sx = std::atof(st[11].c_str()) / 100, sy = std::atof(st[12].c_str()) / 100;
            const double width = e[QStringLiteral("width")].toDouble();
            const QString what = QStringLiteral("%1 (size %2, bold %3, italic %4, scale %5 %6, spacing %7): legacy %8 %9 %10, "
                                                "rewrite %11 %12 %13")
                                     .arg(text, QString::fromStdString(st[2]), QString::fromStdString(st[7]),
                                          QString::fromStdString(st[8]), QString::fromStdString(st[11]),
                                          QString::fromStdString(st[12]), QString::fromStdString(st[13]))
                                     .arg(width)
                                     .arg(e[QStringLiteral("height")].toDouble())
                                     .arg(e[QStringLiteral("descent")].toDouble())
                                     .arg(got->width)
                                     .arg(got->height)
                                     .arg(got->descent);
            const double slack = st[13] != "0" ? 0.5 * text.size() : 0.015 * width / sx;
            QVERIFY2(qAbs(got->width - width) <= (slack + 1) * sx + 1e-3, qPrintable(what));
            QVERIFY2(qAbs(got->height - e[QStringLiteral("height")].toDouble()) <= 2 * sy + 1e-3, qPrintable(what));
            QVERIFY2(qAbs(got->descent - e[QStringLiteral("descent")].toDouble()) <= sy + 1e-3, qPrintable(what));
            QCOMPARE(got->externalLeading, e[QStringLiteral("extlead")].toDouble());
        }
        QCOMPARE(pieces, 38);
#endif
    }

    void textExtentsOfAFaceAddedAfterAMeasure()
    {
#ifdef _WIN32
        QSKIP("GDI makes the font on every measure; the point size search and its cache are the Linux measure's");
#else
        if (QFontDatabase::families().contains(QStringLiteral("OCRA")))
            QSKIP("OCR A is installed here, so no other face answers for it before it is added");
        std::vector<std::string> st = style("OCRA");
        st[2] = "60";
        QtTextMeasurePort measure;
        const auto before = measure.measure(st, "Hello world");
        QVERIFY(before);
        QVERIFY(QFontDatabase::addApplicationFont(QStringLiteral(HIKARI_LATE_TEST_FONT)) >= 0);
        const auto after = measure.measure(st, "Hello world");
        const auto fresh = QtTextMeasurePort().measure(st, "Hello world");
        QVERIFY(after && fresh);
        QCOMPARE(after->width, fresh->width);
        QCOMPARE(after->height, fresh->height);
        QCOMPARE(after->descent, fresh->descent);
        QCOMPARE(after->height, 60.0);
        QVERIFY(after->width != before->width);
#endif
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
        picker.accept({QUrl::fromLocalFile(chosen)});
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
