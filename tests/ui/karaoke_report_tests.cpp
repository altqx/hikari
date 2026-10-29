// KE1-template-timing: the karaoke templater's run report, read and put in
// words, and the composition showing it: the summary in the status bar, the
// per-template details in the log window without popping it up, and the
// progress window kept when the run named template problems.

#include "hikari/app/application.h"
#include "karaoke_report.h"

#include <QDeadlineTimer>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

int macroOrdinal(app::Application &a, const std::string &name)
{
    for (const auto &s : a.automation().scripts())
        for (std::size_t i = 0; i < s.info.macros.size(); ++i)
            if (s.info.macros[i].name == name)
                return static_cast<int>(i);
    return -1;
}

bool loadTemplater(app::Application &a)
{
    a.automation().load(HIKARI_KARAOKE_TEMPLATER);
    QDeadlineTimer deadline(30'000);
    while (!deadline.hasExpired()) {
        const auto scripts = a.automation().scripts();
        if (std::ranges::any_of(scripts, [](const auto &s) { return s.path == HIKARI_KARAOKE_TEMPLATER; }) &&
            std::ranges::none_of(scripts, [](const auto &s) {
                return s.state == application::ScriptStatus::State::Loading;
            }))
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return false;
}

} // namespace

class KaraokeReportTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    app::Application::Options options()
    {
        app::Application::Options o;
        o.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        return o;
    }

    // Opens a copy of a corpus Document and applies the whole template.
    bool templated(app::Application &a, const char *name)
    {
        const QString path = dir.filePath(QLatin1String(name));
        QFile::remove(path);
        if (!QFile::copy(QStringLiteral(HIKARI_KARAOKE_CORPUS "/") + QLatin1String(name), path) || !a.openFile(path) ||
            !loadTemplater(a))
            return false;
        QSignalSpy done(&a.automation(), &app::AutomationShell::runCompleted);
        if (!a.automation().run(HIKARI_KARAOKE_TEMPLATER, macroOrdinal(a, "Apply karaoke template")))
            return false;
        return done.wait(30'000) && done.first().at(0).toBool();
    }

private slots:
    void readsOnlyItsOwnReport()
    {
        QVERIFY(!ui::readKaraokeReport(""));
        QVERIFY(!ui::readKaraokeReport("not json"));
        QVERIFY(!ui::readKaraokeReport(R"({"report":"something else","version":1})"));
        QVERIFY(!ui::readKaraokeReport(R"({"report":"karaoke-templater","version":2})"));
        const auto r = ui::readKaraokeReport(
            R"({"report":"karaoke-templater","version":1,"mode":"selected","seconds":1.5,"prepare_seconds":0.25,)"
            R"("karaoke_lines":2,"generated":30,"problems":1,"templates":[)"
            R"({"line":1,"effect":"code once","style":"Default","seconds":0.0004,"generated":0},)"
            R"({"line":2,"effect":"template syl","style":"Romaji","seconds":0.75,"generated":24},)"
            R"({"line":3,"effect":"template line","style":"Romaji","seconds":1.25,"generated":6}]})");
        QVERIFY(r);
        QVERIFY(r->selected);
        QCOMPARE(r->karaokeLines, 2);
        QCOMPARE(r->generated, 30);
        QCOMPARE(r->problems, 1);
        QCOMPARE(r->templates.size(), std::size_t(3));
        QCOMPARE(r->templates[1].effect, QStringLiteral("template syl"));
        QCOMPARE(r->templates[1].generated, 24);

        QCOMPARE(ui::karaokeReportSummary(*r),
                 QStringLiteral("Karaoke template: 30 line(s) generated from 2 karaoke line(s) in 1.50 s. "
                                "1 template problem(s): see the log."));
        // The slowest template first.
        QCOMPARE(ui::karaokeReportDetails(*r).split(QLatin1Char('\n')),
                 (QStringList{ui::karaokeReportSummary(*r), QStringLiteral("Measuring the karaoke lines: 250 ms"),
                              QStringLiteral("Template line 3 (template line, Romaji): 1.25 s, 6 line(s) generated"),
                              QStringLiteral("Template line 2 (template syl, Romaji): 750 ms, 24 line(s) generated"),
                              QStringLiteral("Code line 1 (code once, Default): 0.4 ms")}));
    }

    void wholeRunReportsToTheStatusBarAndTheLog()
    {
        app::Application a(options());
        QSignalSpy noticed(&a.log(), &ui::LogController::noticed);
        QVERIFY(templated(a, "stock-tutorials.ass"));
        int generated = 0;
        for (const auto *l : a.files().session(*a.workspace().editingTarget())->document().lines())
            generated += l->effect == u8"fx";
        QVERIFY(a.shell().statusText().startsWith(
            QStringLiteral("Karaoke template: %1 line(s) generated from 6 karaoke line(s) in ").arg(generated)));
        QVERIFY(a.log().history().contains(QStringLiteral("Template line 5 (template syl, T2c): ")));
        QVERIFY(a.log().history().contains(QStringLiteral("Measuring the karaoke lines: ")));
        // Silent: no toast (the test profile has no dictionary, which is one).
        for (const QList<QVariant> &notice : std::as_const(noticed))
            QCOMPARE(notice.at(2).toString(), QStringLiteral("Spellchecker"));
        QVERIFY(!a.automation().keepLog());
    }

    void namedProblemsKeepTheProgressLog()
    {
        app::Application a(options());
        QVERIFY(templated(a, "errors.ass"));
        QVERIFY(a.automation().keepLog());
        QVERIFY(a.automation().log().contains(QStringLiteral("Template Line: 3\n")));
        QVERIFY(a.shell().statusText().contains(QStringLiteral("template problem(s): see the log.")));
        auto *s = a.files().session(*a.workspace().editingTarget());
        QCOMPARE(s->selection().active, s->document().lines().front()->id);
        QCOMPARE(s->selection().selected.size(), std::size_t(9));
    }
};

QTEST_MAIN(KaraokeReportTests)
#include "karaoke_report_tests.moc"
