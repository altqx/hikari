#include "hikari/app/plugin_test_harness.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

using hikari::application::PluginTestArguments;

namespace {

const QString kFixtures = QStringLiteral(HIKARI_HARNESS_FIXTURES);

struct Run {
    int code = -1;
    QString output;
};

Run run(PluginTestArguments arguments)
{
    std::FILE *out = std::tmpfile();
    Run r;
    r.code = hikari::app::runPluginTests(arguments, out);
    std::rewind(out);
    QByteArray text;
    for (int c; (c = std::fgetc(out)) != EOF;)
        text += char(c);
    std::fclose(out);
    r.output = QString::fromUtf8(text);
    return r;
}

PluginTestArguments fixture(const QString &script)
{
    PluginTestArguments a;
    a.folder = (kFixtures + QStringLiteral("/plugin")).toStdString();
    a.script = (kFixtures + u'/' + script).toStdString();
    a.document = (kFixtures + QStringLiteral("/episode.ass")).toStdString();
    a.stepTimeoutMs = 10'000;
    return a;
}

}

class PluginTestHarnessTests : public QObject {
    Q_OBJECT

private slots:
    void everyStepKindPasses()
    {
        const Run r = run(fixture(QStringLiteral("pass.jsonl")));
        QVERIFY2(r.code == 0, qPrintable(r.output));
        QVERIFY(r.output.startsWith(QStringLiteral("Testing Harness 1.0.0 (com.example.harness)\n")));
        QVERIFY(r.output.endsWith(QStringLiteral("All 13 steps passed.\n")));
    }

    void eachFailedStepSaysWhereAndWhy()
    {
        const Run r = run(fixture(QStringLiteral("fail.jsonl")));
        QCOMPARE(r.code, 1);
        QVERIFY2(r.output.contains(QStringLiteral("fail.jsonl:1: run: /hello: \"Ana\", expected \"Bo\"\n")), qPrintable(r.output));
        QVERIFY2(r.output.contains(QStringLiteral("fail.jsonl:2: call: /total: 2, expected 3\n")), qPrintable(r.output));
        QVERIFY2(r.output.contains(QStringLiteral("fail.jsonl:3: run: an input box opened with no answer step before it\n")),
                 qPrintable(r.output));
        QVERIFY2(r.output.contains(QStringLiteral("fail.jsonl:4: expectDocument: the Document differs from episode.ass: line ")),
                 qPrintable(r.output));
        QVERIFY(r.output.endsWith(QStringLiteral("4 of 4 steps failed.\n")));
    }

    void jsonGivesAReportForMachines()
    {
        PluginTestArguments a = fixture(QStringLiteral("fail.jsonl"));
        a.json = true;
        const Run r = run(a);
        QCOMPARE(r.code, 1);
        const QJsonObject report = QJsonDocument::fromJson(r.output.toUtf8()).object();
        QCOMPARE(report.value(QStringLiteral("ok")).toBool(true), false);
        QCOMPARE(report.value(QStringLiteral("plugin")).toString(), QStringLiteral("com.example.harness"));
        QCOMPARE(report.value(QStringLiteral("steps")).toInt(), 4);
        const QJsonObject first = report.value(QStringLiteral("failed")).toArray().first().toObject();
        QCOMPARE(first.value(QStringLiteral("line")).toInt(), 1);
        QCOMPARE(first.value(QStringLiteral("step")).toString(), QStringLiteral("run"));
    }

    void aStuckStepTimesOut()
    {
        QTemporaryDir dir;
        QFile script(dir.filePath(QStringLiteral("stuck.jsonl")));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(R"({"run": "spin"})" "\n" R"({"run": "greet", "expect": {"hello": "you"}})" "\n");
        script.close();
        PluginTestArguments a = fixture(QString());
        a.script = script.fileName().toStdString();
        a.stepTimeoutMs = 1500;
        const Run r = run(a);
        QCOMPARE(r.code, 1);
        QVERIFY2(r.output.contains(QStringLiteral("stuck.jsonl:1: run: timed out after 1500 ms\n")), qPrintable(r.output));
        QVERIFY2(r.output.endsWith(QStringLiteral("1 of 2 steps failed.\n")), qPrintable(r.output));
    }

    void whatStopsTheTestsFromRunningExitsWithTwo()
    {
        PluginTestArguments missing = fixture(QStringLiteral("pass.jsonl"));
        missing.folder = (kFixtures + QStringLiteral("/nothing")).toStdString();
        Run r = run(missing);
        QCOMPARE(r.code, 2);
        QVERIFY2(r.output.startsWith(QStringLiteral("hikarisub --test-plugin: there is no plugin folder at ")), qPrintable(r.output));

        QTemporaryDir dir;
        QFile script(dir.filePath(QStringLiteral("bad.jsonl")));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(R"({"run": "greet"})" "\n" R"({"wait": 5})" "\n");
        script.close();
        PluginTestArguments bad = fixture(QString());
        bad.script = script.fileName().toStdString();
        r = run(bad);
        QCOMPARE(r.code, 2);
        QVERIFY2(r.output.contains(QStringLiteral("bad.jsonl:2: a step needs one of run, event, call")), qPrintable(r.output));

        PluginTestArguments usage;
        usage.problem = "--test-plugin needs a plugin folder and a tests.jsonl file";
        QCOMPARE(run(usage).code, 2);
    }
};

QTEST_MAIN(PluginTestHarnessTests)
#include "plugin_test_harness_tests.moc"
