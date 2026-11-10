#include "font_family_filter.h"

#include <QSignalSpy>
#include <QtTest>

using hikari::ui::FontFamilyFilter;

class FontFamilyFilterTest : public QObject {
    Q_OBJECT
private slots:
    void anEmptyTermShowsEveryFamily()
    {
        FontFamilyFilter filter;
        const QStringList all{"Impact", "Arial", "Tahoma"};
        filter.setFamilies(all);
        QCOMPARE(filter.matches(), all);
        filter.setText(QStringLiteral("   "));
        QCOMPARE(filter.matches(), all);
        QCOMPARE(filter.rowCount(), 3);
    }

    void exactThenPrefixThenSubstring()
    {
        FontFamilyFilter filter;
        filter.setFamilies({"Impact", "Comic Sans MS", "Tahoma", "Times", "Ta", "Verdana"});
        filter.setText(QStringLiteral("ta"));
        QCOMPARE(filter.matches(), (QStringList{"Ta", "Tahoma"}));
        filter.setText(QStringLiteral("a"));
        QCOMPARE(filter.matches(), (QStringList{"Impact", "Comic Sans MS", "Tahoma", "Ta", "Verdana"}));
        filter.setText(QStringLiteral("act"));
        QCOMPARE(filter.matches(), QStringList{"Impact"});
        filter.setText(QStringLiteral("T"));
        QCOMPARE(filter.matches(), (QStringList{"Tahoma", "Times", "Ta", "Impact"}));
        filter.setText(QStringLiteral(" sans "));
        QCOMPARE(filter.matches(), QStringList{"Comic Sans MS"});
        filter.setText(QStringLiteral("zz"));
        QCOMPARE(filter.matches(), QStringList());
        QCOMPARE(filter.count(), 0);
    }

    void matchingIgnoresCase()
    {
        FontFamilyFilter filter;
        filter.setFamilies({QStringLiteral("STRASSE Sans"), QStringLiteral("ΟΔΟΣ")});
        filter.setText(QStringLiteral("strasse"));
        QCOMPARE(filter.matches(), QStringList{"STRASSE Sans"});
        filter.setText(QStringLiteral("οδος"));
        QCOMPARE(filter.matches(), QStringList{QStringLiteral("ΟΔΟΣ")});
    }

    void rowFindsAShownFamilyByName()
    {
        FontFamilyFilter filter;
        filter.setFamilies({"Arial", "Tahoma", "Verdana"});
        QCOMPARE(filter.row(QStringLiteral(" tahoma")), 1);
        QCOMPARE(filter.row(QStringLiteral("Futura")), -1);
        filter.setText(QStringLiteral("a"));
        QCOMPARE(filter.row(QStringLiteral("Arial")), 0);
        QCOMPARE(filter.at(0), QStringLiteral("Arial"));
        QCOMPARE(filter.data(filter.index(2), Qt::DisplayRole).toString(), QStringLiteral("Verdana"));
        QCOMPARE(filter.roleNames().value(Qt::DisplayRole), QByteArray("family"));
    }

    void anUnchangedResultResetsNothing()
    {
        FontFamilyFilter filter;
        filter.setFamilies({"Arial", "Tahoma"});
        QSignalSpy resets(&filter, &QAbstractItemModel::modelReset);
        QSignalSpy matches(&filter, &FontFamilyFilter::matchesChanged);
        filter.setText(QStringLiteral("ta"));
        QCOMPARE(resets.size(), 1);
        filter.setText(QStringLiteral("tah"));
        QCOMPARE(resets.size(), 1);
        QCOMPARE(matches.size(), 1);
    }
};

QTEST_GUILESS_MAIN(FontFamilyFilterTest)
#include "font_family_filter_tests.moc"
