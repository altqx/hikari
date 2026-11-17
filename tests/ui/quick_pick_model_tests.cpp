#include "quick_pick_model.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

using hikari::ui::fuzzyMatch;
using hikari::ui::QuickPickModel;

namespace {

QVariantMap item(const QString &id, const QString &label, const QVariantMap &more = {})
{
    QVariantMap out = more;
    out.insert(QStringLiteral("id"), id);
    out.insert(QStringLiteral("label"), label);
    return out;
}

QStringList rows(const QuickPickModel &model)
{
    QStringList out;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex at = model.index(row);
        const bool heading = model.data(at, QuickPickModel::KindRole).toString() == QLatin1String("heading");
        out << (heading ? QStringLiteral("# ") : QString()) + model.data(at, QuickPickModel::LabelRole).toString();
    }
    return out;
}

}

class QuickPickModelTests : public QObject {
    Q_OBJECT

private slots:
    void lettersMatchInOrderButNotNextToEachOther()
    {
        QVERIFY(fuzzyMatch(QStringLiteral("spfr"), QStringLiteral("sprite-fright.ass")));
        QVERIFY(fuzzyMatch(QStringLiteral("JT"), QStringLiteral("Jump to")));
        QVERIFY(!fuzzyMatch(QStringLiteral("tj"), QStringLiteral("Jump to")));
        QVERIFY(!fuzzyMatch(QStringLiteral("jumpx"), QStringLiteral("Jump to")));
        QCOMPARE(fuzzyMatch(QString(), QStringLiteral("anything"))->score, 0);
    }

    void spacesInTheQueryAreIgnored()
    {
        QVERIFY(fuzzyMatch(QStringLiteral("jump to"), QStringLiteral("Jumpto")));
    }

    void theMatchPrefersWordStarts()
    {
        const auto match = fuzzyMatch(QStringLiteral("jt"), QStringLiteral("Jump to"));
        QVERIFY(match);
        QCOMPARE(match->positions, (std::vector<int>{0, 5}));
        const auto camel = fuzzyMatch(QStringLiteral("fr"), QStringLiteral("spriteFright"));
        QCOMPARE(camel->positions, (std::vector<int>{6, 7}));
        QVERIFY(fuzzyMatch(QStringLiteral("jt"), QStringLiteral("Jump to"))->score >
                fuzzyMatch(QStringLiteral("jt"), QStringLiteral("Adjust"))->score);
    }

    void runsScoreAboveScatteredLetters()
    {
        QVERIFY(fuzzyMatch(QStringLiteral("save"), QStringLiteral("Save subtitles"))->score >
                fuzzyMatch(QStringLiteral("save"), QStringLiteral("Select all video events"))->score);
    }

    void groupsShowHeadingsUntilTheUserTypes()
    {
        QuickPickModel model;
        QAbstractItemModelTester tester(&model);
        model.setItems({item(QStringLiteral("a"), QStringLiteral("Save"), {{QStringLiteral("group"), QStringLiteral("Recently used")}}),
                        item(QStringLiteral("b"), QStringLiteral("Open"), {{QStringLiteral("group"), QStringLiteral("All commands")}}),
                        item(QStringLiteral("c"), QStringLiteral("Save as"))});
        QCOMPARE(rows(model), (QStringList{QStringLiteral("# Recently used"), QStringLiteral("Save"),
                                           QStringLiteral("# All commands"), QStringLiteral("Open"), QStringLiteral("Save as")}));
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.current(), 1);
        QVERIFY(model.data(model.index(0), QuickPickModel::FirstHeadingRole).toBool());
        QVERIFY(!model.data(model.index(2), QuickPickModel::FirstHeadingRole).toBool());

        model.setFilter(QStringLiteral("sa"));
        QCOMPARE(rows(model), (QStringList{QStringLiteral("Save"), QStringLiteral("Save as")}));
        QCOMPARE(model.currentId(), QStringLiteral("a"));
    }

    void separatorsStartGroups()
    {
        QuickPickModel model;
        model.setItems({item(QStringLiteral("main"), QStringLiteral("Main")),
                        QVariantMap{{QStringLiteral("kind"), QStringLiteral("separator")}, {QStringLiteral("label"), QStringLiteral("Signs")}},
                        item(QStringLiteral("sign"), QStringLiteral("Sign"))});
        QCOMPARE(rows(model), (QStringList{QStringLiteral("Main"), QStringLiteral("# Signs"), QStringLiteral("Sign")}));
    }

    void theNameRanksAboveTheDescriptionAndRankBreaksTies()
    {
        QuickPickModel model;
        model.setMatchOn({QStringLiteral("description")});
        model.setItems({item(QStringLiteral("zoom"), QStringLiteral("Zoom in"), {{QStringLiteral("description"), QStringLiteral("Automation > Video")}}),
                        item(QStringLiteral("old"), QStringLiteral("Automation manager")),
                        item(QStringLiteral("new"), QStringLiteral("Automation manager"), {{QStringLiteral("rank"), 0}})});
        model.setFilter(QStringLiteral("auto"));
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.itemAt(0).value(QStringLiteral("id")).toString(), QStringLiteral("new"));
        QCOMPARE(model.itemAt(1).value(QStringLiteral("id")).toString(), QStringLiteral("old"));
        QCOMPARE(model.itemAt(2).value(QStringLiteral("id")).toString(), QStringLiteral("zoom"));
        QCOMPARE(model.data(model.index(2), QuickPickModel::DescriptionMarkupRole).toString(),
                 QStringLiteral("<b>Auto</b>mation &gt; Video"));
    }

    void withoutMatchOnTheDescriptionIsNotSearched()
    {
        QuickPickModel model;
        model.setItems({item(QStringLiteral("zoom"), QStringLiteral("Zoom in"), {{QStringLiteral("description"), QStringLiteral("Video")}})});
        model.setFilter(QStringLiteral("video"));
        QCOMPARE(model.count(), 0);
    }

    void matchedLettersAreBoldAndTextIsNeverMarkup()
    {
        QuickPickModel model;
        model.setItems({item(QStringLiteral("x"), QStringLiteral("<i>Jump</i> to"))});
        model.setFilter(QStringLiteral("jt"));
        QCOMPARE(model.data(model.index(0), QuickPickModel::LabelMarkupRole).toString(),
                 QStringLiteral("&lt;i&gt;<b>J</b>ump&lt;/i&gt; <b>t</b>o"));
    }

    void keepOrderAndAlwaysShow()
    {
        QuickPickModel model;
        model.setKeepOrder(true);
        model.setItems({item(QStringLiteral("1"), QStringLiteral("abc x")), item(QStringLiteral("2"), QStringLiteral("x")),
                        item(QStringLiteral("3"), QStringLiteral("New…"), {{QStringLiteral("alwaysShow"), true}})});
        model.setFilter(QStringLiteral("x"));
        QCOMPARE(rows(model), (QStringList{QStringLiteral("abc x"), QStringLiteral("x"), QStringLiteral("New…")}));
    }

    void withoutFilteringItemsShowAsGiven()
    {
        QuickPickModel model;
        model.setFiltering(false);
        model.setItems({item(QStringLiteral("1"), QStringLiteral("One")), item(QStringLiteral("2"), QStringLiteral("Two"))});
        model.setFilter(QStringLiteral("zzz"));
        QCOMPARE(model.count(), 2);
    }

    void arrowsWrapAndSkipHeadingsWhilePagesStop()
    {
        QuickPickModel model;
        QVariantList items;
        for (int i = 0; i < 5; ++i)
            items << item(QString::number(i), QStringLiteral("Item %1").arg(i),
                          {{QStringLiteral("group"), i < 2 ? QStringLiteral("A") : QStringLiteral("B")}});
        model.setItems(items);
        QCOMPARE(model.currentId(), QStringLiteral("0"));
        model.move(-1);
        QCOMPARE(model.currentId(), QStringLiteral("4"));
        model.move(1);
        QCOMPARE(model.currentId(), QStringLiteral("0"));
        model.move(2);
        QCOMPARE(model.currentId(), QStringLiteral("2"));
        model.page(1, 12);
        QCOMPARE(model.currentId(), QStringLiteral("4"));
        model.page(-1, 12);
        QCOMPARE(model.currentId(), QStringLiteral("0"));
        model.last();
        QCOMPARE(model.currentId(), QStringLiteral("4"));
    }

    void theCurrentRowCanBeDisabled()
    {
        QuickPickModel model;
        model.setItems({item(QStringLiteral("a"), QStringLiteral("Split"), {{QStringLiteral("disabled"), QStringLiteral("Needs a selection")}}),
                        item(QStringLiteral("b"), QStringLiteral("Save"))});
        QCOMPARE(model.currentId(), QStringLiteral("a"));
        QCOMPARE(model.data(model.index(0), QuickPickModel::DisabledRole).toString(), QStringLiteral("Needs a selection"));
    }

    void pickingSeveral()
    {
        QuickPickModel model;
        model.setMany(true);
        QSignalSpy picked(&model, &QuickPickModel::pickedChanged);
        model.setItems({item(QStringLiteral("a"), QStringLiteral("Alpha"), {{QStringLiteral("picked"), true}}),
                        item(QStringLiteral("b"), QStringLiteral("Beta")),
                        item(QStringLiteral("c"), QStringLiteral("Gamma"), {{QStringLiteral("disabled"), QStringLiteral("No")}}),
                        item(QStringLiteral("d"), QStringLiteral("Delta"))});
        QCOMPARE(model.pickedIds(), QStringList{QStringLiteral("a")});
        model.toggle(1);
        model.toggle(0);
        model.toggle(2);
        QCOMPARE(model.pickedIds(), QStringList{QStringLiteral("b")});
        model.setFilter(QStringLiteral("ta"));
        model.pickAllShown();
        QCOMPARE(model.pickedIds(), (QStringList{QStringLiteral("b"), QStringLiteral("d")}));
        QCOMPARE(model.pickedCount(), 2);
        QVERIFY(picked.count() >= 3);
    }

    void togglingNeedsMany()
    {
        QuickPickModel model;
        model.setItems({item(QStringLiteral("a"), QStringLiteral("Alpha"))});
        model.toggle(0);
        QVERIFY(model.pickedIds().isEmpty());
    }

    void anEmptyListHasNoCurrentRow()
    {
        QuickPickModel model;
        model.setItems({item(QStringLiteral("a"), QStringLiteral("Alpha"))});
        model.setFilter(QStringLiteral("zz"));
        QCOMPARE(model.current(), -1);
        QVERIFY(model.currentId().isEmpty());
        model.move(1);
        QCOMPARE(model.current(), -1);
    }

    void newItemsKeepTheCurrentIdOrStartAtTheTop()
    {
        QuickPickModel model;
        model.setItems({item(QStringLiteral("a"), QStringLiteral("Alpha")), item(QStringLiteral("b"), QStringLiteral("Beta")),
                        item(QStringLiteral("c"), QStringLiteral("Gamma"))});
        model.last();
        QCOMPARE(model.currentId(), QStringLiteral("c"));
        model.setItems({item(QStringLiteral("x"), QStringLiteral("Other"))});
        QCOMPARE(model.currentId(), QStringLiteral("x"));
        model.setItems({item(QStringLiteral("y"), QStringLiteral("Why")), item(QStringLiteral("x"), QStringLiteral("Other"))});
        QCOMPARE(model.currentId(), QStringLiteral("x"));
    }
};

QTEST_GUILESS_MAIN(QuickPickModelTests)
#include "quick_pick_model_tests.moc"
