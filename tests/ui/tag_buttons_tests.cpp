// E2: tag button definitions against legacy EditBox::SetTagButtons and
// config::GetTable at 20d647c4: the legacy option text, defaults, the
// 0-20 count and persistence. LE5 (#230): the icon, adding, removing, moving
// and the Settings list's replace, and the tag text check.

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

    // LE5: an icon is a fourth entry, written only when there is one, so a
    // button without one keeps legacy's text; an unknown role is no icon.
    void iconIsAFourthEntry()
    {
        const TagButtonsController::Button plain{QStringLiteral("\\b1"), 0, QStringLiteral("Bold")};
        QCOMPARE(TagButtonsController::toLegacy(plain), QStringLiteral("{\n\t\\b1\n\t0\n\tBold\n}"));
        const TagButtonsController::Button withIcon{QStringLiteral("\\b1"), 0, QStringLiteral("Bold"), QStringLiteral("tag-bold")};
        const QString text = TagButtonsController::toLegacy(withIcon);
        QCOMPARE(text, QStringLiteral("{\n\t\\b1\n\t0\n\tBold\n\ttag-bold\n}"));
        const auto back = TagButtonsController::fromLegacy(text, 0);
        QCOMPARE(back.icon, QStringLiteral("tag-bold"));
        QCOMPARE(back.name, QStringLiteral("Bold"));
        QCOMPARE(back.tag, QStringLiteral("\\b1"));
        QVERIFY(TagButtonsController::fromLegacy(QStringLiteral("{\n\t\\b1\n\t0\n\tBold\n\tno-such-icon\n}"), 0).icon.isEmpty());
        QVERIFY(TagButtonsController::fromLegacy(TagButtonsController::toLegacy(plain), 0).icon.isEmpty());
        for (const QString &role : TagButtonsController::iconRoles())
            QVERIFY2(!role.isEmpty(), qPrintable(role));
        QVERIFY(TagButtonsController::iconRoles().contains(QStringLiteral("alignment-7")));
    }

    void addRemoveMoveAndReplacePersist()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("hikari.ini"));
        {
            TagButtonsController buttons(ini);
            QSignalSpy changed(&buttons, &TagButtonsController::changed);
            QVERIFY(buttons.add(QStringLiteral("Blur"), QStringLiteral("\\blur2"), 0, QStringLiteral("no-such-icon")));
            QVERIFY(buttons.add(QStringLiteral("Bold"), QStringLiteral("\\b1"), 1, QStringLiteral("tag-bold")));
            QVERIFY(buttons.add(QStringLiteral("Note"), QStringLiteral("note"), 2));
            QCOMPARE(changed.count(), 3);
            QCOMPARE(buttons.count(), 3);
            QCOMPARE(buttons.storedCount(), 3);
            QVERIFY(buttons.button(0).icon.isEmpty());
            QCOMPARE(buttons.buttons().at(1).toMap().value(QStringLiteral("icon")).toString(), QStringLiteral("tag-bold"));
            // Moves stop at the ends.
            QVERIFY(!buttons.move(0, -1));
            QVERIFY(!buttons.move(2, 1));
            QVERIFY(buttons.move(2, -1));
            QCOMPARE(buttons.button(1).name, QStringLiteral("Note"));
            QCOMPARE(buttons.button(2).name, QStringLiteral("Bold"));
            // Remove: the ones after it move up, the freed place is unset.
            buttons.remove(0);
            QCOMPARE(buttons.count(), 2);
            QCOMPARE(buttons.button(0).name, QStringLiteral("Note"));
            QCOMPARE(buttons.button(1).name, QStringLiteral("Bold"));
            QCOMPARE(buttons.button(2).name, QStringLiteral("T3"));
            buttons.remove(5); // nothing there
            QCOMPARE(buttons.count(), 2);
        }
        {
            TagButtonsController again(ini);
            QCOMPARE(again.count(), 2);
            QCOMPARE(again.button(0).tag, QStringLiteral("note"));
            QCOMPARE(again.button(0).type, 2);
            QCOMPARE(again.button(1).icon, QStringLiteral("tag-bold"));
            QCOMPARE(again.button(1).type, 1);
            QCOMPARE(again.button(2).name, QStringLiteral("T3"));
            // The Settings list's OK: the same list changes nothing.
            QSignalSpy changed(&again, &TagButtonsController::changed);
            again.replace(again.buttons());
            QCOMPARE(changed.count(), 0);
            QVariantList list = again.buttons();
            list.removeFirst();
            list << QVariantMap{{QStringLiteral("name"), QStringLiteral("Top")}, {QStringLiteral("tag"), QStringLiteral("\\an8")},
                                {QStringLiteral("type"), 1}, {QStringLiteral("icon"), QStringLiteral("alignment-8")}};
            again.replace(list);
            QCOMPARE(changed.count(), 1);
            QCOMPARE(again.count(), 2);
            QCOMPARE(again.button(0).name, QStringLiteral("Bold"));
            QCOMPARE(again.button(1).icon, QStringLiteral("alignment-8"));
        }
        TagButtonsController last(ini);
        QCOMPARE(last.count(), 2);
        QCOMPARE(last.button(1).tag, QStringLiteral("\\an8"));
        last.replace({});
        QCOMPARE(last.count(), 0);
        QCOMPARE(TagButtonsController(ini).count(), 0);
    }

    void atMostTwentyAdded()
    {
        TagButtonsController buttons;
        for (int i = 0; i < TagButtonsController::kMaximum; ++i)
            QVERIFY(buttons.add(QStringLiteral("B"), QStringLiteral("\\b1"), 0));
        QVERIFY(!buttons.add(QStringLiteral("B"), QStringLiteral("\\b1"), 0));
        QCOMPARE(buttons.count(), TagButtonsController::kMaximum);
    }

    // The popover's check: override tags without braces for types 0 and 1,
    // any text but nothing for plain text.
    void tagTextIsChecked()
    {
        TagButtonsController buttons;
        QVERIFY(buttons.problem(QStringLiteral("\\blur2"), 0).isEmpty());
        QVERIFY(buttons.problem(QStringLiteral("\\fad(200,200)\\blur2"), 1).isEmpty());
        QVERIFY(!buttons.problem(QString(), 0).isEmpty());
        QVERIFY(!buttons.problem(QStringLiteral("  "), 2).isEmpty());
        QVERIFY(!buttons.problem(QStringLiteral("{\\b1}"), 0).isEmpty());
        QVERIFY(!buttons.problem(QStringLiteral("blur2"), 0).isEmpty());
        QVERIFY(!buttons.problem(QStringLiteral("\\"), 1).isEmpty());
        QVERIFY(buttons.problem(QStringLiteral("{note}"), 2).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TagButtonsTests)
#include "tag_buttons_tests.moc"
