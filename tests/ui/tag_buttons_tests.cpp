// E2: tag button definitions against legacy EditBox::SetTagButtons and
// config::GetTable at 20d647c4: the legacy option text, defaults, the
// 0-20 count and persistence.

#include "tag_buttons_controller.h"

#include <QTemporaryDir>
#include <QtTest>

using hikari::ui::TagButtonsController;

class TagButtonsTests : public QObject {
    Q_OBJECT

private slots:
    void legacyOptionTextIsRead()
    {
        auto b = TagButtonsController::fromLegacy(QStringLiteral("{\n\t\\bord4\n\t1\n\tBorder\n}"), 0);
        QCOMPARE(b.tag, QStringLiteral("\\bord4"));
        QCOMPARE(b.type, 1);
        QCOMPARE(b.name, QStringLiteral("Border"));
        // Empty entries are kept, so an empty tag keeps its type and name.
        b = TagButtonsController::fromLegacy(QStringLiteral("{\n\t\n\t2\n\tNote\n}"), 0);
        QCOMPARE(b.tag, QString());
        QCOMPARE(b.type, 2);
        QCOMPARE(b.name, QStringLiteral("Note"));
        // Nothing stored: "T<n>", type 0, no tag.
        b = TagButtonsController::fromLegacy(QString(), 4);
        QCOMPARE(b.name, QStringLiteral("T5"));
        QCOMPARE(b.type, 0);
        QVERIFY(b.tag.isEmpty());
        // wxAtoi of a type that is not a number is 0.
        QCOMPARE(TagButtonsController::fromLegacy(QStringLiteral("{\n\t\\b1\n\tx\n\tB\n}"), 0).type, 0);
        const TagButtonsController::Button round{QStringLiteral("\\i1"), 2, QStringLiteral("It")};
        const auto back = TagButtonsController::fromLegacy(TagButtonsController::toLegacy(round), 0);
        QCOMPARE(back.tag, round.tag);
        QCOMPARE(back.type, round.type);
        QCOMPARE(back.name, round.name);
    }

    void noButtonsByDefaultAndAtMostTwenty()
    {
        TagButtonsController buttons;
        QCOMPARE(buttons.count(), 0);
        buttons.setCount(25);
        QCOMPARE(buttons.count(), 20);
        QCOMPARE(buttons.buttons().size(), qsizetype(20));
        QCOMPARE(buttons.buttons().last().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("T20"));
        buttons.setCount(-1);
        QCOMPARE(buttons.count(), 0);
    }

    void definitionsPersist()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("hikari.ini"));
        {
            TagButtonsController buttons(ini);
            buttons.setCount(3);
            buttons.edit(1, QStringLiteral("Blur"), QStringLiteral("\\blur2"), 1);
        }
        TagButtonsController again(ini);
        QCOMPARE(again.count(), 3);
        QCOMPARE(again.button(1).tag, QStringLiteral("\\blur2"));
        QCOMPARE(again.button(1).type, 1);
        QCOMPARE(again.button(1).name, QStringLiteral("Blur"));
        QCOMPARE(again.button(0).name, QStringLiteral("T1"));
    }
};

QTEST_GUILESS_MAIN(TagButtonsTests)
#include "tag_buttons_tests.moc"
