#include "cited_answer.h"

#include <QTest>
#include <QTextDocument>

using hikari::ui::citedAnswerHtml;
using hikari::ui::siteOf;

namespace {

QVariantMap cited(int index, qsizetype start, qsizetype end)
{
    return {{QStringLiteral("index"), index}, {QStringLiteral("url"), QStringLiteral("https://a.example/")},
            {QStringLiteral("start"), start}, {QStringLiteral("end"), end}};
}

QString shown(const QString &html)
{
    QTextDocument document;
    document.setHtml(html);
    return document.toPlainText();
}

}

class CitedAnswerTests final : public QObject {
    Q_OBJECT
private slots:
    void aChipFollowsEachCitedSpanInOrder()
    {
        const QString text = QStringLiteral("Aimer sings. Kalafina too.");
        const QString html = citedAnswerHtml(text, {cited(2, 13, 26), cited(1, 0, 12)}, Qt::gray, Qt::black);
        QCOMPARE(shown(html), QStringLiteral("Aimer sings. 1  Kalafina too. 2 "));
        QVERIFY(html.contains(QLatin1String("href=\"cite:1\"")));
        QVERIFY(html.indexOf(QLatin1String("cite:1")) < html.indexOf(QLatin1String("cite:2")));
    }

    void theAnswersTextIsEscapedAndKeepsItsLines()
    {
        const QString html = citedAnswerHtml(QStringLiteral("<b>a</b>\nb & c"), {cited(1, 0, 3)}, Qt::gray, Qt::black);
        QVERIFY(!html.contains(QLatin1String("<b>")));
        QCOMPARE(shown(html), QStringLiteral("<b> 1 a</b>\nb & c"));
    }

    void spansOffTheTextOrInsideAPairGetNoChip()
    {
        const QString text = QStringLiteral("Hi \U0001F600!");
        const QString html =
            citedAnswerHtml(text, {cited(1, 0, 4), cited(2, 0, 99), cited(3, 2, 2), QVariantMap{}}, Qt::gray, Qt::black);
        QCOMPARE(shown(html), text);
        QCOMPARE(shown(citedAnswerHtml(text, {cited(1, 0, 5)}, Qt::gray, Qt::black)),
                 QStringLiteral("Hi \U0001F600 1 !"));
    }

    void theSiteIsTheHostWithoutWww()
    {
        QCOMPARE(siteOf(QStringLiteral("https://www.example.org/a?b")), QStringLiteral("example.org"));
        QCOMPARE(siteOf(QStringLiteral("https://ja.wikipedia.org/wiki/x")), QStringLiteral("ja.wikipedia.org"));
        QCOMPARE(siteOf(QStringLiteral("not a url")), QStringLiteral("not a url"));
    }
};

QTEST_MAIN(CitedAnswerTests)
#include "cited_answer_tests.moc"
