// G7: the Grid's CPS and Wraps against legacy SpellChecker::CheckTextAndBrackets,
// SpellChecker::Check and TextData::GetCPS at 20d647c4.

#include "line_measures.h"

#include <QtTest>

using namespace hikari;
using hikari::ui::legacyCps;
using hikari::ui::measureLine;

class LineMeasuresTests : public QObject {
    Q_OBJECT

private slots:
    void lettersAndNumbersAreCountedPerWrap()
    {
        auto m = measureLine(QStringLiteral("Hello world"), core::SubtitleFormat::Ass);
        QCOMPARE(m.chars, 10);
        QCOMPARE(m.wraps, QStringLiteral("10"));
        QVERIFY(!m.badWraps);
        m = measureLine(QStringLiteral("{\\i1}Hello{\\i0}\\Nworld, 2nd!"), core::SubtitleFormat::Ass);
        QCOMPARE(m.wraps, QStringLiteral("5/8"));
        QCOMPARE(m.chars, 13);
        // An apostrophe inside a word belongs to it (Unicode word segmentation).
        QCOMPARE(measureLine(QStringLiteral("don't"), core::SubtitleFormat::Ass).chars, 5);
        // \h is a space; other escapes are text.
        QCOMPARE(measureLine(QStringLiteral("a\\hb"), core::SubtitleFormat::Ass).wraps, QStringLiteral("2"));
        QCOMPARE(measureLine(QString(), core::SubtitleFormat::Ass).wraps, QStringLiteral("0"));
    }

    void drawingsAndBlocksAreSkipped()
    {
        QCOMPARE(measureLine(QStringLiteral("{\\p1}m 0 0 l 10 10{\\p0}text"), core::SubtitleFormat::Ass).chars, 4);
        // SRT blocks are <...>; braces are text there.
        const auto srt = measureLine(QStringLiteral("<i>abc</i>\\N{d}"), core::SubtitleFormat::Srt);
        QCOMPARE(srt.wraps, QStringLiteral("3/1"));
        // MicroDVD, MPL2 and TMPlayer wrap at '|'.
        QCOMPARE(measureLine(QStringLiteral("abc|de"), core::SubtitleFormat::MicroDvd).wraps, QStringLiteral("3/2"));
    }

    void badWrapsAreTooLongOrTooMany()
    {
        QVERIFY(!measureLine(QStringLiteral("a\\Nb"), core::SubtitleFormat::Ass).badWraps);
        QVERIFY(measureLine(QStringLiteral("a\\Nb\\Nc"), core::SubtitleFormat::Ass).badWraps);
        QVERIFY(!measureLine(QString(43, QLatin1Char('x')), core::SubtitleFormat::Ass).badWraps);
        QVERIFY(measureLine(QString(44, QLatin1Char('x')), core::SubtitleFormat::Ass).badWraps);
    }

    void allCharactersWhenAsked()
    {
        ui::MeasureOptions all{true, true};
        const auto m = measureLine(QStringLiteral("Hi, you!\\Na\\hb"), core::SubtitleFormat::Ass, all);
        // "Hi, you!" is 8; "a  b" is 4 less the hard space's extra one.
        QCOMPARE(m.wraps, QStringLiteral("8/3"));
        QCOMPARE(m.chars, 11);
    }

    void cpsIsTruncatedAndCapped()
    {
        QCOMPARE(legacyCps(10, 0, 2000), 5);
        QCOMPARE(legacyCps(10, 0, 3000), 3);
        QCOMPARE(legacyCps(0, 0, 2000), 0);
        QCOMPARE(legacyCps(10, 1000, 1000), 999); // no duration
        QCOMPARE(legacyCps(0, 1000, 1000), 999);
        QCOMPARE(legacyCps(10, 2000, 1000), 999); // negative
        QCOMPARE(legacyCps(5000, 0, 1000), 999);
    }
};

QTEST_GUILESS_MAIN(LineMeasuresTests)
#include "line_measures_tests.moc"
